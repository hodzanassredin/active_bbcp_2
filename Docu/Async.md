# Async Library

A guide to the optional `Async` subsystem for the Component Pascal programmer. It provides
channels, a single-assignment variable, selection among several waits, a dialogue protocol,
non-blocking I/O, a worker-thread pool and computons. The language part — exclusive procedures,
`ACTIVE.AWAIT` and yield points — is defined in `Docu/Active-CP-Lang.md`; tasks are described in
`Docu/Tasks.md`.

The library is written on the language extension and does not change the framework: framework
modules are still compiled without the yield key and do not wait. The subsystem is built for AMD64
only; the core extension also works on i386.

## 1. Message

A *message* is an object of type `AsyncSelect.Msg`:

```
Msg* = POINTER TO ABSTRACT RECORD END;
```

An application type extends this record:

```
TYPE NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
```

Rules:

1. A channel passes a pointer, not a copy of a value.
2. After sending, a message is not modified. This is a convention, not a check: the library neither
   copies the object nor forbids writing it.
3. The receiver distinguishes the kind of message with `WITH`; the field `n` is available only after
   the type check.

```
VAR m: AsyncSelect.Msg;
...
WITH m: NumMsg DO total := total + m.n END
```

## 2. Single-Assignment Variable

The *single-assignment variable* `AsyncOnce.Once` holds one value and hands it out after it has
been filled.

Operations:

```
o := AsyncOnce.New();
o.FillInt(x);            (* fill with an integer *)
x := o.ReadInt();        (* wait and read the integer *)
o.FillMsg(m);            (* fill with a message *)
o.ReadMsg(m);            (* wait and read the message *)
```

Rules:

1. Filling happens exactly once. A second `FillInt` traps with code 21, a second `FillMsg` with
   code 22.
2. The variable has one flag `done` for both kinds of value. Therefore `FillInt` after `FillMsg` and
   the reverse are also a repeated fill and trap. The kind of fill and the kind of read must match.
3. `ReadInt` and `ReadMsg` wait for the fill; there is no timeout. A read before the fill suspends
   the task in `ACTIVE.AWAIT(done)`.
4. There is no kind check on reading: `ReadInt` after `FillMsg` returns 0, `ReadMsg` after `FillInt`
   returns `NIL`.
5. The object is a monitor: the methods are `[exclusive]`. `Fill` does not wait and may run outside
   a task if the monitor is free; `Read` waits, so it is called only from a task.

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN; s: ARRAY 16 OF CHAR;

	TYPE Reader = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (r: Reader) Run;
	BEGIN got := o.ReadInt(); done := TRUE END Run;

	PROCEDURE Run*;
		VAR r: Reader;
	BEGIN
		o := AsyncOnce.New();
		o.FillInt(42);		(* outside a task: the monitor is free, Fill does not wait *)
		NEW(r); done := FALSE; Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Run;

END ObxOnce.
```

`Fill` runs outside a task, `ReadInt` inside one: the read waits for the fill, and the fill has
already happened.

Limits: one value per object; there is no value kind; the wait is indefinite; a repeated fill traps.

## 3. Channels

A *channel* is an object with the methods `Send` and `Recv`. The library provides three kinds:

| Kind | Constructor | Capacity | `Send` waits |
|---|---|---|---|
| rendezvous, `Chan` | `AsyncChan.NewChan()` | 0 | until the receiver takes the message |
| bounded buffer, `Buffer` | `AsyncChan.NewBuffer(capacity)` | `capacity` | while there is room |
| mailbox, `Mailbox` | `AsyncChan.NewMailbox()` | unbounded | never |

All three are extensions of one record `AsyncSelect.Core`; the fields `kind` and `capacity`
distinguish them. All three are monitors: the methods are `[exclusive]`, and waiting on a condition
releases the monitor.

### 3.1. Rendezvous

`Chan` stores no messages. `Send` deposits the message and waits until the receiver takes it; `Recv`
waits for a deposit or a sender offer. A send completes only after the receive.

```
c := AsyncChan.NewChan();
c.Send(m);        (* returns after Recv *)
c.Recv(m);        (* waits for a sender *)
```

`Send` and `Recv` are two phases of the rendezvous: the sender leaves a deposit, the receiver takes
it and wakes the sender.

### 3.2. Bounded Buffer

`Buffer` stores up to `capacity` messages in sending order. `Recv` waits on an empty buffer; `Send`
waits on a full one. The capacity must be positive: `NewBuffer(0)` traps with code 20. There is no
upper bound on the capacity.

### 3.3. Mailbox

`Mailbox` is unbounded: `Send` never waits, `Recv` waits while the mailbox is empty. The message
order is the sending order.

### 3.4. Common Rules

1. A message is an `AsyncSelect.Msg` pointer; after sending it is not modified.
2. Direct `Send` and `Recv` also serve `Select` offers: a send into a buffer completes a pending
   receive, and a receive from a buffer makes room for a pending send. A channel can therefore be
   mixed with selection.
3. `Send` and `Recv` are wait points. Cancellation of a task waiting in a channel works as usual:
   trap 21.
4. A channel may carry an optional dialogue protocol (section 5).

```
MODULE ObxChan;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
	     Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
	     Receiver = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Run;

	PROCEDURE (r: Receiver) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan(); NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan: sum="); Console.WriteStr(d); Console.WriteLn
	END Run;

END ObxChan.
```

Limits: the rendezvous buffers nothing; a buffer requires a positive capacity; the mailbox is
unbounded; a message is not copied; a channel lives in one OS thread.

## 4. Selection

An *alternative* describes one operation that may be performed, or a wait for a signal. *Selection*
(`Select`) waits until exactly one alternative can be performed and returns its index.

The alternative type is `AsyncSelect.Op`. Alternatives are built by:

```
AsyncChan.RecvAlt(c, op);          (* receive from a channel *)
AsyncChan.SendAlt(c, m, op);       (* send into a channel *)
AsyncChan.TimeoutAlt(ms, op);      (* expiration of a delay *)
AsyncSelect.AltSignal(sig, op);    (* external signal *)
```

`AsyncChan` wraps the first three; `Select` is the same operation:

```
i := AsyncChan.Select(ops);        (* index of the alternative that fired *)
m := ops[i].got;                   (* result of a receive *)
```

Rules:

1. `Select` is called only from a task; otherwise trap 20.
2. The number of alternatives is at most 32; otherwise trap 21.
3. Alternatives are checked in index order. The first one that can be performed immediately wins.
4. Of several timeouts the smallest `ms` is used; on a tie, the first by index. A timeout with
   `ms <= 0` fires immediately if no alternative is ready.
5. An external signal ready during registration (`Arm` returned `TRUE`) wins immediately. Otherwise
   the signal owner later calls `SignalReady`.
6. After waking, the task withdraws the losing offers; an active sender deposit is rolled back. The
   field `got` is filled only for the winning receive.
7. Cancellation while waiting traps the task in `Tasks.Park`. Offers remain in the channels and are
   removed lazily: a channel considers an offer dead when the task has reached `Finished` or
   `Cancelled`.

```
VAR ops: ARRAY 3 OF AsyncSelect.Op;
AsyncChan.RecvAlt(a, ops[0]);
AsyncChan.RecvAlt(b, ops[1]);
AsyncChan.TimeoutAlt(100, ops[2]);
i := AsyncChan.Select(ops);
IF i = 0 THEN ... ELSIF i = 1 THEN ... ELSE ... END
```

Limits: at most 32 alternatives; only from a task; when several are ready, the index order decides;
cancellation does not remove offers immediately.

## 5. Channel Protocol

A *protocol* is a deterministic state machine built from a textual description. A channel may carry
it as an optional check of the message sequence.

The description consists of clauses:

```
start: RequestMsg > wait; wait: ResponseMsg > wait;
wait: EndMsg > done; done:
```

Description rules:

1. A clause is `state: Message > state`.
2. The arrow is written `>` or `->`; clauses are separated by `;` or a newline.
3. `state:` without an arrow declares a state with no outgoing transition. Finiteness is computed:
   a state is final while it has no outgoing transition.
4. The first mentioned state is the start state.
5. Two transitions from one state by the same label are a description error.
6. The message label is the dynamic type name of the `AsyncSelect.Msg` record; the suffix `^` is
   stripped. `NIL` gives the label `NIL`, an unknown type gives `?`.

Operations:

```
p := AsyncProto.New(desc);        (* NIL on a description error *)
AsyncProto.LastError(msg);        (* description error text *)
AsyncChan.SetProtocol(c, p);      (* attach the protocol; NIL removes it *)
AsyncProto.IsFinal(p, state);     (* no outgoing transitions *)
AsyncProto.LastViolation(p, msg); (* text of the last violation *)
```

Checks:

1. A direct `Send` checks the message at entry, before any possible wait.
2. `SendAlt` checks the message at the moment the offer is actually performed: on immediate
   registration, when a receiver takes a deposited offer, or when room appears in a buffer. A losing
   offer does not change the state.
3. A violation prints `AsyncChan protocol: <text>` to the console and traps the task with code 21.
   The text remains in `LastViolation`; the expected messages are listed in the violation text.
4. Without a protocol `CheckSend` returns after one `NIL` test; there is no overhead.
5. Attaching a protocol resets the state to the start state.

Limits: at most 32 states and 128 transitions; a state name and a label are at most 31 characters;
a violation text is at most 255 characters. The machine is deterministic; there are no `*`, `+`,
`?` or `|` operators. A direct `Send` advances the state at entry, so cancellation after entry does
not restore it.

## 6. I/O

*Readiness waiting* is built on the `Poller` registry and non-blocking `CommStreams` streams.

```
AsyncIO.WaitFd(fd, events);       (* wait for descriptor readiness *)
AsyncIO.Read(s, buf, beg, len, read);      (* read at least one byte *)
AsyncIO.Write(s, buf, beg, len, written);  (* write at least one byte *)
AsyncIO.Accept(l, s, timeout);    (* accept a connection *)
AsyncIO.Poll(timeout);            (* run the waits, for a test pump *)
```

Rules:

1. `Read` returns `read > 0` as soon as at least one byte has been read; `read = 0` means end or
   disconnection. `Write` returns `written > 0` as soon as at least one byte has been written;
   `written = 0` means disconnection.
2. While there is no data or no room, the task registers the descriptor in `Poller` and sleeps in
   `Tasks.Park`. Registration is one-shot: when readiness arrives the entry is removed, and the
   waiter registers again if readiness was not enough.
3. `Accept` with `timeout < 0` waits indefinitely; with a non-negative timeout it returns `FALSE`
   when the time expires. The deadline is checked between iterations; there is no separate timer.
4. If a stream has no descriptor (`Handle() = -1`), waiting degenerates to `Tasks.Sleep(1)`. This is
   slow but does not block the main loop.
5. `WaitFd` is called only from a task; otherwise trap 20.
6. The main loop (`LinConsole.WaitInput`) polls stdin and the registered descriptors in one `poll`.

`CommStreams.Stream` and `CommStreams.Listener` have a `Handle()` method that returns −1 by default.
Only `CommTCP__Lin` overrides it: there `Handle` returns the socket. On other platforms `AsyncIO`
works through `Sleep(1)`.

Limits: at most 4096 simultaneous waits (`LinConsole`); on overflow the waiter immediately receives
`Ready` and retries. `CommTCP__Lin` works over IPv4, the accept backlog is 128; `poll` replaced
`select` because `fd_set` is limited to 1024 descriptors.

## 7. Worker Threads

A *worker thread* is an operating-system thread that performs a blocking call outside the main loop.
The `AsyncWork` pool moves such a call out of a task, so that the main loop and the other tasks keep
running.

```
id := AsyncWork.Execute(S.ADR(SomeJob), S.ADDRESS(arg));  (* from a task; waits *)
status := AsyncWork.Result(id);
AsyncWork.Release(id);

id := AsyncWork.Submit(S.ADR(SomeJob), S.ADDRESS(arg));   (* does not wait *)
IF AsyncWork.Done(id) THEN status := AsyncWork.Result(id) END;
AsyncWork.Release(id);
```

Operations:

| Operation | Effect |
|---|---|
| `Execute(procAdr, arg)` | submits a job, waits for completion, returns the status; from a task only |
| `Submit(procAdr, arg)` | submits a job and returns its number; −1 — no slot or the queue is full |
| `Done(id)` | whether the job is done |
| `Result(id)` | the job status |
| `Release(id)` | frees the slot; mandatory |
| `Workers()` | number of created threads |
| `Scan` | scans completed jobs; called by the main loop |
| `Install(impl)` | installs the platform delegate |

Provided jobs: `ReadFileJob`, `WriteFileJob`, `ResolveJob`. The `FileReq` and `AddrReq` records are
declared in the facade; the caller allocates them in advance, and the worker thread receives only
their address.

Rules:

1. The delegate is installed lazily on the first `Execute` or `Submit`: `AsyncWork.Ensure` tries
   `Dialog.Call("AsyncWorkLin.Install")`, then `AsyncWorkWin.Install`. The base init does not start
   a pool.
2. The pool has up to four threads. On Linux readiness arrives as a byte in a pipe; `Poller` wakes
   the main loop, and `Scan` wakes the task with `Tasks.Unpark`. On Windows readiness is collected
   by a `Services` action that polls the `done` counter every 5 ms while slots are busy.
3. A worker thread must not touch the heap, call `NEW`, `Kernel.Try`, the journal or the framework.
   This is a rule of discipline, not a barrier.
4. `Execute` is a wait point of the task. `Submit`, `Done`, `Result` and `Release` do not wait.
5. `Release` is mandatory: otherwise the slot stays busy until the end of the process.
6. On Windows the pool works while the `Services` loop runs; blocking console input stops the
   delivery of readiness.

Job codes: `ReadFileJob` returns −30 if the read produced no bytes; `WriteFileJob` returns −20 on a
short write; `ResolveJob` returns −1 on error. `Submit` and `Execute` return −1 if there is no free
slot.

Measurement (Linux, worker thread, a job that calls `usleep` for 300 ms): the task waits in
`Execute`, the main loop makes 286 steps, the test takes about 301 ms. The command is
`tools/test.sh Async_devWorkers`.

Limits: the pool exists only on Linux AMD64 and Windows x64; a path is at most 255 bytes, a file
buffer is 4096 bytes, a host name is at most 127 bytes; a job performs one read or write system
call; `ResolveJob` only counts addresses and does not store them.

## 8. Computons

A *computon* is a serializable `Stores.Store` object with a `Do` method that runs in a separate
BlackBox process with its own heap and collector. `Do` may therefore call `NEW`.

```
TYPE Computon* = POINTER TO ABSTRACT RECORD (Stores.Store) END;
     PROCEDURE (c: Computon) Do*, NEW, ABSTRACT;
```

A computon type must implement `Externalize` and `Internalize`: the object is passed through
`Stores`.

### 8.1. Operations

```
AsyncComputon.Start(n);                        (* n executors, 1..16 *)
AsyncComputon.Push(c, rank, tag);
r := AsyncComputon.Pull(rank, tag);            (* wait; a wait point *)
ok := AsyncComputon.Try(rank, tag, r);         (* do not wait *)
sig := AsyncComputon.AltPull(rank, tag);       (* a Select alternative *)
AsyncComputon.Stop;
AsyncComputon.Size(); AsyncComputon.Rank();
AsyncComputon.LastError(); AsyncComputon.WorkerPid(rank);
AsyncComputon.Unload(module);                  (* drop module code on executors *)
```

Rules:

1. `Start(n)` starts `n` executors; `n` is from 1 to 16. `Rank()` of the controller is 0.
2. `Push` serializes the computon and sends it to executor `rank`; errors are visible through
   `LastError` or `Hook.Failed`.
3. `Pull` waits for the result for at most 10,000 ms; the wait is `Tasks.Sleep(10)`, a wait point.
   On timeout `Pull` returns `NIL` and `LastError = -4`.
4. `Try` does not wait: it returns `TRUE` if the request is done, and the result or `NIL` with a code
   in `LastError`.
5. A request is matched **by `tag` only**; `rank` in `Pull` is used only for `Hook`, in `Try` and
   `AltPull` it is not used. Equal `tag` values of simultaneous requests conflict.
6. When an executor dies, the request is retried on a live one. A retry is possible after each
   further death; there is no attempt counter. The semantics is at-least-once: if an executor died
   after computing but before answering, `Do` runs again.
7. `AltPull` gives a signal for `Select`: a selection can wait for a computon result together with
   channels and a timeout. `AltPull` does not consume the request; after the selection, `Pull` or
   `Try` is needed.
8. `Unload(module)` asks the executors to drop the module code; the next `Push` loads the new
   `.ocf`. It returns `FALSE` and `LastError = 20` if the module is busy. An unloaded module is a
   success. Over MPI unloading is not supported: `FALSE`, `LastError = -1`.
9. `Stop` kills the executors (`SIGKILL`) and closes the channels; `waitpid` is not called. `Stop`
   does not mark unfinished requests: after a stop they remain busy.

Error codes: `-1` — no request, empty serialization or no slot; `-2` — no live executors left;
`-3` — sending failed; `-4` — timeout; `-5` — response could not be parsed; `-6` — result does not
serialize; `20` — module busy on unload; `1` — a trap in `Do`.

### 8.2. Transports

```
ok := AsyncComputon.SetTransport("pipe");   (* default *)
ok := AsyncComputon.SetTransport("tcp");
ok := AsyncComputon.SetTransport("mpi");
```

The transport is chosen before `Start`; the external variable `ASYNC_TRANSPORT` sets the same
value.

1. `pipe` — `fork` and `exec` of the current process (`/proc/self/exe`) with three channels per
   executor: command on stdin, requests, responses. In a console host this is the same
   `blackbox-console`; in a GUI process `exec` starts a different binary.
2. `tcp` — the controller listens on `127.0.0.1:0`, the executor connects to the address from
   `--ASYNC_TCP=...`. The frame format and retry are the same. The executor socket is blocking, the
   controller socket is non-blocking for `Poller`.
3. `mpi` — the external `AsyncMpi` transport; see 8.4.

A frame is a 12-byte header (length, `tag`, kind; response — length, `tag`, code) and a body. Over
pipe and TCP the response body is bounded so that the practical result limit is about 59,988 bytes.

### 8.3. Example

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Console, Strings;

	TYPE Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
	     Client = POINTER TO RECORD (Tasks.Task) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN c.y := c.x * c.x END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN wr.WriteInt(c.x); wr.WriteInt(c.y) END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN rd.ReadInt(c.x); rd.ReadInt(c.y) END Internalize;

	PROCEDURE (cl: Client) Run;
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		WITH r: Square DO Strings.IntToString(r.y, d) END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Run;

END ObxComputon.
```

`Do` runs in a separate process, so `NEW` inside it is allowed. `Pull` is a wait point and is called
from a task; the `Run` command drives `Tasks.Step` until the client gets the result.

### 8.4. MPI

`AsyncMpi` provides an external transport over MPICH. The order is:

```
ok := AsyncMpi.Install();                 (* load libbbmpi.so, install the transport *)
ok := AsyncComputon.SetTransport("mpi");
AsyncComputon.Start(n);                   (* a world of n+1 ranks *)
```

World layout: rank 0 is the BlackBox controller, ranks 1..n run `AsyncMpi.WorkerLoop`. The frames are
the same; the header and body use MPI tags 1000 and 1001, responses use 2000. The world must be
exactly `n+1`; otherwise `Start` gives size 0.

Limits: a rank death is not detected and not retried (mpiexec usually kills the world); `Unload` is
not supported; `WorkerPid` returns 0 and `WorkerDead` returns `TRUE`; a second `Start` after `Stop`
calls `MPI_Init` again after `MPI_Finalize`.

### 8.5. RobustParallel Facade

`RobustParallel` reproduces the published definition from I. Denisov's talk (2017, p. 19):

```
CONST tag* = 1;
TYPE Computon* = AsyncComputon.Computon;
     Hook* = AsyncComputon.Hook;
VAR rank-, size-: INTEGER;
PROCEDURE LogMsg* (a: ARRAY OF CHAR);
PROCEDURE Push* (store: Computon; rank, tag: INTEGER);
PROCEDURE Pull* (rank, tag: INTEGER): Computon;
PROCEDURE Try* (rank, tag: INTEGER): Computon;
PROCEDURE SetHook* (h: Hook);
PROCEDURE Wait*;
PROCEDURE Stop*;
PROCEDURE Unload* (module: ARRAY OF CHAR);
```

Rules:

1. Ranks as in MPI: 0 is the controller, 1..size−1 are executors; `Push`, `Pull` and `Try` shift
   the rank by −1.
2. The number of executors is set by the external variable `ROBUST_SIZE`: `n > 1` starts `n−1`
   local `AsyncComputon` executors; with `n = 1` no pool starts.
3. `ROBUST_SIZE` is not connected to MPI: the transport is local `pipe` (or the one set by
   `ASYNC_TRANSPORT`), not an MPI world.
4. In an executor process (any of `ASYNC_IN`, `ASYNC_OUT`, `ASYNC_TCP` is set) the facade does not
   start its own pool.
5. `Unload` returns no result: a refusal is printed to the console.
6. `Wait` is an empty procedure; all waits are expressed by `Pull` and `Try`.

## 9. General Limits

1. The library is built for AMD64 only. The language core (tasks, monitors, `ACTIVE.AWAIT`, the `^`
   key) also works on i386.
2. One operating-system thread executes Component Pascal code. Worker threads and computon
   processes do not execute common code on the common heap.
3. `Select`, `AsyncIO` and `Pull` are called only from a task. `Send` into a mailbox, `Fill`,
   `Push`, `Try` and `Unload` do not require a task.
4. All primitives are monitors: the methods are `[exclusive]`, waiting uses `ACTIVE.AWAIT` or
   `Tasks.Park`.
5. Cancellation of a task during a wait traps it; `Select` offers are removed lazily, and unfinished
   computon requests remain busy until `Stop`.

## 10. Implementation Scheme

This section does not define behaviour; it describes how the behaviour is obtained.

### 10.1. Channel and Selection

`AsyncSelect.Core` is an extensible record with a kind, a capacity, a message queue and the heads of
two offer queues (`waitS`, `waitR`). An offer (`Offer`) stores a common context `Ctx`, the index of
the alternative, the operation kind and the message.

`Select` walks alternatives in index order. For a send or a receive it creates an offer and calls
`Register` under the channel monitor. If `Register` performed the operation immediately, the
decision is made; otherwise the offer stays in the queue. The timeout with the smallest `ms`
schedules a `Services` action; the task sleeps in `Tasks.Park` until `Ctx.decided` becomes true.

The decision is made by the first channel able to perform an offer: under its monitor it writes
`ctx.decided` and `ctx.winner`, takes or gives the message and wakes the task with `Tasks.Unpark`.
`Wake` wakes only when `ctx.ready = TRUE`: during registration the task is not yet in `Park`, and
`Unpark` would move it to `Ready` without the monitor. After waking, the owner withdraws the losing
offers (`Withdraw`); withdrawal is idempotent, so the winner is withdrawn by the same call.

A direct rendezvous `Send` has two phases: it deposits the message with a `deposit` flag and waits
for `taken`. `Prune` on every channel access rolls back a deposit whose offer lost or whose task
finished. An offer is dead when the task is in `Finished` or `Cancelled`, not merely cancelled.

### 10.2. I/O

`Poller` stores only a pointer to the platform hook. The descriptor registry and `poll` live in
`LinConsole`: an array of 4096 entries, stdin as the first element, one-shot registration. Ready
entries are removed before `Waiter.Ready` is called, so that a repeated registration does not
disturb the walk.

### 10.3. Worker Threads

`AsyncWork` is the facade; `AsyncWorkLin` and `AsyncWorkWin` are the delegates. Linux: up to four
pthreads, a static slot array under a mutex and a condition variable, a ring queue of 256 jobs, a
byte in a pipe as the readiness signal. Windows: up to four `CreateThread` threads, slots under a
`CRITICAL_SECTION`, a queue event; readiness is collected by a `Services` action that polls `done`
every 5 ms.

Synchronization primitives live in the heap, not in global module records: global records do not get
guaranteed alignment, and futex and `CRITICAL_SECTION` require it.

### 10.4. Computons

`AsyncComputon` keeps up to 16 executors and up to 128 requests. A request (`Req`) stores a `tag`, a
rank, a buffer, a task and a `Select` context. `Push` serializes the object through `Stores` into a
temporary file and sends a frame. The response is read by the main loop: on Linux through `Poller`,
on Windows by a `Services` action; `Pull` waits in `Tasks.Sleep(10)` with a deadline.

`MarkDead` closes the dead executor's channel and retries its unfinished requests on a live one.
`Resend` chooses the next live executor; if none is live, the request gets code `-2`. `Unload` sends
kind `kindUnload` and waits for responses; the executor calls `Kernel.UnloadMod` and clears the
pointer to the unloaded module object, so that the next collection does not dereference its method
table.

The MPI transport (`AsyncMpi`) loads `libbbmpi.so` through `LinDl` and binds seven functions. The
controller checks response readiness with `MPI_Iprobe` from a `Services` action; the `WorkerLoop`
executor receives frames on the stdin command and answers through MPI.
