# Async Library

A guide to the optional `Async` subsystem for the Component Pascal programmer. It provides
channels, a single-assignment variable, selection among several waits, a dialogue protocol,
non-blocking I/O, a worker-thread pool and computons. The language part — exclusive procedures,
`ACTIVE.AWAIT` and yield points — is defined in `Docu/Active-CP-Lang.md`; tasks are described in
`Docu/Tasks.md`.

The library is written on the language extension and does not change the framework: framework
modules are compiled without the yield key and do not wait. The subsystem is built for AMD64 only;
the core extension also works on i386. Section 10 lists the exports of each module with pre- and
postconditions.

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
distinguish them. A channel is an object with a monitor: if a task holds the monitor
(`Kernel.Lock`), `Send` and `Recv` wait for its release. A waiting sender or receiver does not hold
the monitor.

### 3.1. Rendezvous

`Chan` stores no messages: it is a bounded channel of capacity 0. `Send` without a waiting receiver
joins the sender queue and waits until a receiver takes the message; `Recv` without a waiting sender
joins the receiver queue. A send completes only after the receive; senders are served in arrival
order.

```
c := AsyncChan.NewChan();
c.Send(m);        (* returns after Recv *)
c.Recv(m);        (* waits for a sender *)
```

The side that arrives second performs the offer of the first and wakes it. A send and a receive of
one `Select` on one channel do not perform each other.

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
3. `Recv` of all kinds and `Send` of a rendezvous and a buffer are switch points. `Send` into a
   mailbox waits only if another task holds the channel monitor. A cancelled task traps in the wait
   with code 127.
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
2. The number of alternatives is at most 32; otherwise trap 21. Every alternative is filled by
   `AltSend`/`AltRecv`/`AltTimeout`/`AltSignal` (otherwise trap 22); a send or a receive has a channel
   (otherwise trap 23), a signal alternative has a signal (otherwise trap 24). Preconditions are
   checked before any registration. Trap 22 occurs only if the alternative kind is out of the
   allowed range. An unfilled alternative with kind zero counts as a send and gives trap 23.
3. Alternatives are checked in index order. The first one that can be performed immediately wins.
4. Of several timeouts the smallest `ms` is used; on a tie, the first by index. A timeout with
   `ms <= 0` fires immediately if no alternative is ready.
5. An external signal ready during registration (`Arm` returned `TRUE`) wins immediately. Otherwise
   the signal owner later calls `SignalReady`.
6. After waking, the task withdraws the losing offers. An offer is not performed against an offer
   of the same `Select`. The field `got` is filled only for the winning receive.
7. Cancellation while waiting traps the task in `Tasks.Park` with code 127. Offers remain in the
   channels and are removed lazily: a channel considers an offer dead when the task has reached
   `Finished` or `Cancelled`.
8. If the protocol rejected a send of this task, `Select` withdraws all offers and then traps with
   code 21.

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

The operations of `AsyncProto` and `AsyncChan.SetProtocol` are listed in section 10.

Checks:

1. The protocol checks a message at the moment the send takes place. This holds both for a direct
   `Send` and for `SendAlt`.
2. A send takes place in one of three places: at entry, if a receiver waits or there is room; when
   a receiver takes a waiting offer; when room appears in a buffer.
3. A losing `Select` offer does not change the protocol state.
4. A violation prints `AsyncChan protocol: <text>` to the console. The sender traps with code 21,
   and the message is not delivered. If the sender was waiting, it gets the trap, not the receiving
   task.
5. A rejected send does not change the protocol state. The violation text remains in
   `LastViolation` and lists the expected messages.
6. Without a protocol the check returns after one `NIL` test.
7. Attaching a protocol resets the state to the start state.

Limits: at most 32 states and 128 transitions; a state name and a label are at most 31 characters;
a violation text is at most 255 characters. The machine is deterministic; there are no `*`, `+`,
`?` or `|` operators.

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
`Ready` and retries. `CommTCP__Lin` works over IPv4, the accept backlog is 128. The registry uses
`poll`, not `select`: `fd_set` is limited to 1024 descriptors.

## 7. Worker Threads

A *worker thread* is an operating-system thread that performs a blocking call outside the main loop.
The `AsyncWork` pool moves such a call out of a task, so that the main loop and the other tasks keep
running.

```
status := AsyncWork.Execute(S.ADR(SomeJob), S.VAL(S.ADDRESS, arg));  (* from a task; waits *)

id := AsyncWork.Submit(S.ADR(SomeJob), S.VAL(S.ADDRESS, arg));   (* does not wait *)
IF AsyncWork.Done(id) THEN status := AsyncWork.Result(id); AsyncWork.Release(id) END;
```

`Execute` frees the slot itself and returns the job status. The operations are listed in section 10.

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
4. `Execute` is a switch point of the task; outside a task it traps with code 21. `Submit`, `Done`,
   `Result` and `Release` do not wait.
5. After `Submit` a call of `Release` is mandatory: otherwise the slot stays busy until the end of
   the process.
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

The operations of `AsyncComputon` are listed in section 10. Rules:

1. `Start(n)` starts `n` executors; `n` is from 1 to 16, otherwise trap 20. `Rank()` of the
   controller is 0.
2. `Push` serializes the computon and sends it to executor `rank`; errors are visible through
   `LastError` or `Hook.Failed`.
3. `Pull` waits for the result for at most 10,000 ms; the wait is `Tasks.Sleep(10)`, a switch
   point. Outside a task `Pull` traps with code 21. On timeout `Pull` returns `NIL` and
   `LastError = -4`.
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

`Do` runs in a separate process, so `NEW` inside it is allowed. `Pull` is a switch point and is called
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
7. `Push`, `Pull` and `Try` with a rank outside `1..size−1` trap with code 20.

## 9. General Limits

1. The library is built for AMD64 only. The language core (tasks, monitors, `ACTIVE.AWAIT`, the `^`
   key) also works on i386.
2. One operating-system thread executes Component Pascal code. Worker threads and computon
   processes do not execute common code on the common heap.
3. `Select`, `AsyncIO` and `Pull` are called only from a task. `Send` into a mailbox, `Fill`,
   `Push`, `Try` and `Unload` do not require a task.
4. `Once` is a monitor: the methods are `[exclusive]`, waiting uses `ACTIVE.AWAIT`. Channels wait in
   `Tasks.Park` and respect the monitor of the channel object when a task holds it.
5. Cancellation of a task during a wait traps it with code 127; `Select` offers are removed lazily, and unfinished
   computon requests remain busy until `Stop`.

## 10. Interface

This section lists the module exports in the form of BlackBox documentation: a `DEFINITION` block,
then a description of each procedure. A "Pre" line names a condition and the trap number on its
violation. A condition marked "not checked" is not checked by the code; its violation gives an
undefined result or a `NIL` dereference trap. A "Post" line names the state after return.

### 10.1. AsyncSelect

```
DEFINITION AsyncSelect;

	IMPORT AsyncProto;

	CONST
		Rendezvous = 0; Bounded = 1; Mailbox = 2;

	TYPE
		Msg = POINTER TO ABSTRACT RECORD END;

		Ctx = POINTER TO RECORD END;

		Core = POINTER TO EXTENSIBLE RECORD
			count-: INTEGER;
			protoState-: INTEGER
		END;

		Signal = POINTER TO ABSTRACT RECORD
			(s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(s: Signal) Disarm, NEW, ABSTRACT
		END;

		Op = RECORD
			got-: Msg
		END;

	PROCEDURE InitCore (c: Core; kind, capacity: INTEGER);
	PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol);
	PROCEDURE QueueSend (c: Core; m: Msg);
	PROCEDURE QueueRecv (c: Core; OUT m: Msg);
	PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op);
	PROCEDURE AltRecv (core: Core; OUT op: Op);
	PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op);
	PROCEDURE AltSignal (sig: Signal; OUT op: Op);
	PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER;
	PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER);

END AsyncSelect.
```

`Msg` is the base message type. `Core` is the channel core: `count` is the number of messages in
the ring, `protoState` is the protocol state (0 without a protocol). `Op` is a `Select` alternative;
`got` is the message of the winning receive. `Ctx` is the decision of one `Select` or one wait.

**PROCEDURE InitCore (c: Core; kind, capacity: INTEGER)**
Sets the kind and capacity of a new channel core.
Pre: `c` has just been created by `NEW` — not checked; `kind` is `Rendezvous`, `Bounded` or
`Mailbox` — trap 20; `capacity > 0` for `Bounded`, otherwise `capacity = 0` — trap 21.
Post: `c` is an empty channel of kind `kind` without a protocol.

**PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol)**
Attaches a protocol to a channel or removes it.
Pre: `c # NIL` — not checked.
Post: every send into `c` is checked by protocol `p` at the moment it takes place; `protoState` is
the start state of `p`. With `p = NIL` there is no protocol, `protoState = 0`.

**PROCEDURE QueueSend (c: Core; m: Msg)**
Sends `m` into the channel; waits if there is no receiver or no room.
Pre: `c # NIL` — not checked; waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is given to a receiver or lies in the ring. The protocol rejected `m` — trap 21, `m` is
not sent.

**PROCEDURE QueueRecv (c: Core; OUT m: Msg)**
Receives a message; waits if there are no messages and no waiting senders.
Pre: `c # NIL` — not checked; waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is the first message of the ring or the message of the first waiting sender.

**PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op)**
Builds a send alternative.
Pre: `core # NIL` — trap 23 in `Select`.
Post: `op` is a send of `msg` into `core`.

**PROCEDURE AltRecv (core: Core; OUT op: Op)**
Builds a receive alternative.
Pre: `core # NIL` — trap 23 in `Select`.
Post: `op` is a receive from `core`.

**PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op)**
Builds a timeout alternative.
Post: `op` is a timeout of `ms` ms from the start of the wait; `ms <= 0` fires at once.

**PROCEDURE AltSignal (sig: Signal; OUT op: Op)**
Builds an external-signal alternative.
Pre: `sig # NIL` — trap 24 in `Select`.
Post: `op` is a wait for signal `sig`.

**PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER**
Waits until exactly one alternative is performed and returns its index.
Pre: call from a task — trap 20; `LEN(ops) <= 32` — trap 21; the kind of every alternative is
valid — trap 22; a send or a receive has a non-`NIL` channel — trap 23; a signal alternative has a
non-`NIL` signal — trap 24.
Post: exactly one alternative `i` is performed, the result is `i`. For a receive `ops[i].got` is the
message. Losing offers are withdrawn, signals are disarmed. The protocol rejected a send — trap 21
after all offers are withdrawn.

**PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER)**
Reports readiness of an external signal.
Pre: called by the signal owner; `ctx` and `idx` come from `Arm`; `Disarm` is not yet called — not
checked.
Post: if there was no decision, alternative `idx` won; the waiting task is woken.

**PROCEDURE (s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN**
Abstract method; `Select` calls it during registration.
Post: `TRUE` — the signal is ready. `FALSE` — the owner later calls `SignalReady(ctx, idx)` at most
once, unless `Disarm` is called first.

**PROCEDURE (s: Signal) Disarm**
Abstract method; `Select` calls it after the decision.
Post: the signal owner no longer uses `ctx` from `Arm` and does not call `SignalReady` with it.

### 10.2. AsyncChan

```
DEFINITION AsyncChan;

	IMPORT AsyncSelect, AsyncProto;

	TYPE
		Chan = POINTER TO RECORD (AsyncSelect.Core)
			(c: Chan) Send (m: AsyncSelect.Msg), NEW;
			(c: Chan) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Buffer = POINTER TO RECORD (AsyncSelect.Core)
			(c: Buffer) Send (m: AsyncSelect.Msg), NEW;
			(c: Buffer) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Mailbox = POINTER TO RECORD (AsyncSelect.Core)
			(c: Mailbox) Send (m: AsyncSelect.Msg), NEW;
			(c: Mailbox) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE NewChan (): Chan;
	PROCEDURE NewBuffer (capacity: INTEGER): Buffer;
	PROCEDURE NewMailbox (): Mailbox;
	PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op);
	PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;

END AsyncChan.
```

**PROCEDURE NewChan (): Chan**
Post: a new empty rendezvous without a protocol.

**PROCEDURE NewBuffer (capacity: INTEGER): Buffer**
Pre: `capacity > 0` — trap 20.
Post: a new empty buffer for `capacity` messages without a protocol.

**PROCEDURE NewMailbox (): Mailbox**
Post: a new empty unbounded mailbox without a protocol.

**PROCEDURE (c: Chan) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Send (m: AsyncSelect.Msg)**
Send `m`; the same as `AsyncSelect.QueueSend(c, m)`.
Pre: waiting only from a task — trap 20 in `Tasks.Park`. `Mailbox.Send` waits only if another task
holds the channel monitor.
Post: rendezvous — the receiver took `m`; buffer and mailbox — `m` is the last in the queue. The
protocol rejected `m` — trap 21, `m` is not sent.

**PROCEDURE (c: Chan) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Recv (OUT m: AsyncSelect.Msg)**
Receive a message; the same as `AsyncSelect.QueueRecv(c, m)`.
Pre: waiting only from a task — trap 20 in `Tasks.Park`.
Post: `m` is the first message of the queue or of the first waiting sender.

**PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol)**
The same as `AsyncSelect.SetProtocol`.

**PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op)**
**PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op)**
**PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op)**
**PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER**
The same as `AsyncSelect.AltSend`, `AltRecv`, `AltTimeout` and `Select` (section 10.1).

### 10.3. AsyncOnce

```
DEFINITION AsyncOnce;

	IMPORT AsyncSelect;

	TYPE
		Once = POINTER TO RECORD
			done: BOOLEAN;
			(o: Once) FillInt (x: INTEGER), NEW;
			(o: Once) ReadInt (): INTEGER, NEW;
			(o: Once) FillMsg (m: AsyncSelect.Msg), NEW;
			(o: Once) ReadMsg (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE New (): Once;

END AsyncOnce.
```

All four methods are exclusive procedures (`[exclusive]`). The field `done` is exported for reading
and writing; an outside write breaks single assignment, and the code does not forbid it.

**PROCEDURE New (): Once**
Post: an unfilled variable, `done = FALSE`.

**PROCEDURE (o: Once) FillInt (x: INTEGER)**
Pre: `~o.done` — trap 21.
Post: `o.done`; the value is `x`.

**PROCEDURE (o: Once) ReadInt (): INTEGER**
Waits for filling in `ACTIVE.AWAIT(o.done)`.
Pre: waiting only from a task — monitor trap 24 if the variable is not filled.
Post: the result is the integer from `FillInt`; after `FillMsg` it is 0.

**PROCEDURE (o: Once) FillMsg (m: AsyncSelect.Msg)**
Pre: `~o.done` — trap 22.
Post: `o.done`; the value is `m`.

**PROCEDURE (o: Once) ReadMsg (OUT m: AsyncSelect.Msg)**
Waits for filling in `ACTIVE.AWAIT(o.done)`.
Pre: waiting only from a task — monitor trap 24 if the variable is not filled.
Post: `m` is the message from `FillMsg`; after `FillInt` it is `NIL`.

### 10.4. AsyncProto

```
DEFINITION AsyncProto;

	TYPE
		Message = ARRAY 256 OF CHAR;
		Protocol = POINTER TO RECORD END;

	PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol;
	PROCEDURE LastError (OUT msg: Message);
	PROCEDURE Start (p: Protocol): INTEGER;
	PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR);
	PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN;
	PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN);
	PROCEDURE LastViolation (p: Protocol; OUT msg: Message);

END AsyncProto.
```

**PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol**
Builds a state machine from a description (section 5).
Post: the machine, or `NIL` on a description error; the error text is in `LastError`.

**PROCEDURE LastError (OUT msg: Message)**
Post: `msg` is the text of the last parse error.

**PROCEDURE Start (p: Protocol): INTEGER**
Post: the index of the start state; 0 when `p = NIL`.

**PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR)**
Post: `name` is the state name; `""` if `p = NIL` or the index does not exist.

**PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN**
Pre: `p # NIL` — not checked.
Post: `TRUE` if `state` has no outgoing transitions.

**PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN)**
Checks the next message with label `label`.
Post: `ok` — `state` follows the transition `label`. `~ok` — `state` is unchanged, the violation
text is in `LastViolation`. With `p = NIL` — `ok`.

**PROCEDURE LastViolation (p: Protocol; OUT msg: Message)**
Post: `msg` is the text of the last violation; `""` when `p = NIL`.

### 10.5. AsyncIO

```
DEFINITION AsyncIO;

	IMPORT CommStreams;

	PROCEDURE WaitFd (fd: INTEGER; events: SET);
	PROCEDURE Poll (timeout: LONGINT);
	PROCEDURE Read (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT read: INTEGER);
	PROCEDURE Write (s: CommStreams.Stream; IN x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT written: INTEGER);
	PROCEDURE Accept (l: CommStreams.Listener; OUT s: CommStreams.Stream;
		timeout: LONGINT): BOOLEAN;

END AsyncIO.
```

**PROCEDURE WaitFd (fd: INTEGER; events: SET)**
Waits for descriptor readiness; the registration is one-shot.
Pre: call from a task — trap 20.
Post: `fd` is ready for `events`; with `fd < 0` one `Tasks.Sleep(1)` has passed.

**PROCEDURE Poll (timeout: LONGINT)**
Runs the `Poller` waits for a test pump.
Post: ready waiters are woken within at most `timeout` ms.

**PROCEDURE Read (s; VAR x; beg, len: INTEGER; OUT read: INTEGER)**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `read > 0` — `read` bytes are read; `read = 0` — end or disconnection.

**PROCEDURE Write (s; IN x; beg, len: INTEGER; OUT written: INTEGER)**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `written > 0` — `written` bytes are written; `written = 0` — disconnection.

**PROCEDURE Accept (l; OUT s; timeout: LONGINT): BOOLEAN**
Pre: waiting only from a task — trap 20 in `WaitFd` or `Tasks.Sleep`.
Post: `TRUE` — `s` is the accepted connection; `FALSE` — `timeout` ms expired, `s = NIL`. With
`timeout < 0` there is no deadline.

### 10.6. AsyncWork

```
DEFINITION AsyncWork;

	IMPORT SYSTEM;

	CONST
		maxPath = 256; maxBuf = 4096; maxHost = 128;

	TYPE
		Proc = PROCEDURE [ccall] (arg: SYSTEM.ADDRESS);

		FileReq = RECORD [untagged]
			path: ARRAY maxPath OF SHORTCHAR;
			buf: ARRAY maxBuf OF SHORTCHAR;
			len, status: INTEGER
		END;

		AddrReq = RECORD [untagged]
			host: ARRAY maxHost OF SHORTCHAR;
			count, status: INTEGER
		END;

		FileReqPtr = POINTER [untagged] TO FileReq;
		AddrReqPtr = POINTER [untagged] TO AddrReq;

		Impl = POINTER TO ABSTRACT RECORD
			(i: Impl) Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Done (id: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(i: Impl) Result (id: INTEGER): INTEGER, NEW, ABSTRACT;
			(i: Impl) Release (id: INTEGER), NEW, ABSTRACT;
			(i: Impl) Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Workers (): INTEGER, NEW, ABSTRACT;
			(i: Impl) Scan, NEW, ABSTRACT;
			(i: Impl) ReadFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) WriteFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) ResolveJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT
		END;

	PROCEDURE Install (i: Impl);
	PROCEDURE Active (): BOOLEAN;
	PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Done (id: INTEGER): BOOLEAN;
	PROCEDURE Result (id: INTEGER): INTEGER;
	PROCEDURE Release (id: INTEGER);
	PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Workers (): INTEGER;
	PROCEDURE Scan;
	PROCEDURE [ccall] ReadFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] WriteFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] ResolveJob (arg: SYSTEM.ADDRESS);

END AsyncWork.
```

`Impl` is the platform delegate; `AsyncWorkLin` and `AsyncWorkWin` install it. The delegate trap
numbers are given for Linux and Windows; they coincide.

**PROCEDURE Install (i: Impl)**
Installs the delegate. The delegate's own `Install` calls it.
Post: the pool is served by `i`.

**PROCEDURE Active (): BOOLEAN**
Post: `TRUE` if a delegate is installed.

**PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Queues a job; installs the delegate if needed.
Pre: a delegate is found — trap 22.
Post: the slot number; −1 — no free slot or the queue is full.

**PROCEDURE Done (id: INTEGER): BOOLEAN**
Pre: a delegate is installed — trap 21; `0 <= id <` the number of slots — trap 20 in the delegate.
Post: `TRUE` if job `id` is done.

**PROCEDURE Result (id: INTEGER): INTEGER**
Pre: the same as for `Done`.
Post: the status of job `id`.

**PROCEDURE Release (id: INTEGER)**
Pre: the same as for `Done`.
Post: slot `id` is free.

**PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Runs a job in a worker thread and waits for it; a switch point.
Pre: a delegate is found — trap 22; call from a task — trap 21 in the delegate.
Post: the job status, the slot is freed; −1 — no free slot.

**PROCEDURE Workers (): INTEGER**
Post: the number of created worker threads; 0 without a delegate.

**PROCEDURE Scan**
Scans completed jobs; the main loop calls it.
Post: the tasks whose jobs are done are woken.

**PROCEDURE ReadFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE WriteFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE ResolveJob (arg: SYSTEM.ADDRESS)**
Jobs for a worker thread; `arg` is the address of a `FileReq` or an `AddrReq`.
Pre: a delegate is installed — trap 21.
Post: the job status is in the record field `status` (codes in section 7).

### 10.7. AsyncComputon

```
DEFINITION AsyncComputon;

	IMPORT Stores, AsyncSelect;

	TYPE
		Computon = POINTER TO ABSTRACT RECORD (Stores.Store)
			(c: Computon) Do, NEW, ABSTRACT
		END;

		Hook = POINTER TO ABSTRACT RECORD
			(h: Hook) Failed (rank, tag, err: INTEGER), NEW, ABSTRACT
		END;

		Transport = POINTER TO ABSTRACT RECORD
			(t: Transport) Start (n: INTEGER; OUT size: INTEGER), NEW, ABSTRACT;
			(t: Transport) SendFrame (rank, tag, kind: INTEGER;
				buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(t: Transport) Poll, NEW, ABSTRACT;
			(t: Transport) Stop, NEW, ABSTRACT;
			(t: Transport) Alive (rank: INTEGER): BOOLEAN, NEW, ABSTRACT
		END;

	PROCEDURE Serialize (c: Computon; OUT buf: POINTER TO ARRAY OF SHORTCHAR; OUT len: INTEGER);
	PROCEDURE Deserialize (VAR b: ARRAY OF SHORTCHAR; len: INTEGER): Computon;
	PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE SetTransportObject (t: Transport);
	PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal;
	PROCEDURE Push (c: Computon; rank, tag: INTEGER);
	PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon;
	PROCEDURE Pull (rank, tag: INTEGER): Computon;
	PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN;
	PROCEDURE Wait;
	PROCEDURE LastError (): INTEGER;
	PROCEDURE SetHook (h: Hook);
	PROCEDURE LogMsg (IN msg: ARRAY OF CHAR);
	PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Size (): INTEGER;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorkerPid (rank: INTEGER): INTEGER;
	PROCEDURE WorkerDead (rank: INTEGER): BOOLEAN;
	PROCEDURE Stop;
	PROCEDURE Start (n: INTEGER);
	PROCEDURE MarkDead (idx: INTEGER);
	PROCEDURE DeliverFrame (rank, tag, status: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER);
	PROCEDURE RunDo (c: Computon; OUT err: INTEGER);
	PROCEDURE WorkerLoop;

END AsyncComputon.
```

The `LastError` codes are listed in section 8.1. `Transport`, `SetTransportObject`, `MarkDead`,
`DeliverFrame`, `RunDo`, `Serialize` and `Deserialize` serve the transports and the executor.

**PROCEDURE Start (n: INTEGER)**
Stops the previous pool and starts `n` executors.
Pre: `1 <= n <= 16` — trap 20; for the `mpi` transport an external transport is installed — trap 21.
Post: `Size()` is the number of started executors.

**PROCEDURE Stop**
Post: the executors are killed, the channels are closed, `Size() = 0`.

**PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN**
Post: `TRUE` — transport `"pipe"` (also `""`), `"tcp"` or `"mpi"` is chosen; `FALSE` — unknown name.

**PROCEDURE Push (c: Computon; rank, tag: INTEGER)**
Serializes `c` and sends it to executor `rank`.
Post: request `tag` is queued; on error `LastError = -1`; on a failed send — a retry on a live one.

**PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon**
**PROCEDURE Pull (rank, tag: INTEGER): Computon**
Wait for the result of request `tag`; `Pull` uses `ms = 10000`.
Pre: call from a task — trap 21.
Post: the result, or `NIL` with a code in `LastError`; the request is freed.

**PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN**
Post: `TRUE` — the request is done and freed, `c` is the result or `NIL` with a code in
`LastError`; `FALSE` — no result yet, `c = NIL`.

**PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal**
Post: a signal for `Select` that is ready when request `tag` is done.

**PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN**
Post: `TRUE` — all live executors dropped the module, or the pool is not started; `FALSE` — a
refusal, the code is in `LastError`.

**PROCEDURE Size (): INTEGER**, **Rank (): INTEGER**, **WorkerPid (rank): INTEGER**,
**WorkerDead (rank): BOOLEAN**, **LastError (): INTEGER**
Post: the number of executors; 0; the executor pid or −1; the death flag (`TRUE` out of range); the
code of the last error.

**PROCEDURE SetHook (h: Hook)**, **LogMsg (IN msg)**, **Wait**
Install the error handler, write a line to the console; `Wait` does nothing.

### 10.8. AsyncMpi

```
DEFINITION AsyncMpi;

	PROCEDURE Install (): BOOLEAN;
	PROCEDURE Available (): BOOLEAN;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorldSize (): INTEGER;
	PROCEDURE WorkerLoop;

END AsyncMpi.
```

**PROCEDURE Install (): BOOLEAN**
Post: `TRUE` — `libbbmpi.so` is bound, the transport is installed in `AsyncComputon`.

**PROCEDURE Available (): BOOLEAN**
Post: `TRUE` — the MPICH library and the wrapper are found; `MPI_Init` is not called.

**PROCEDURE Rank (): INTEGER**, **WorldSize (): INTEGER**
Post: the MPI rank and world size; before initialization −1 and 0.

**PROCEDURE WorkerLoop**
Entry point of an executor process of rank greater than 0.

## 11. Implementation Scheme

This section does not define behaviour; it describes how the behaviour is obtained.

### 11.1. Channel and Selection

`AsyncSelect.Core` is an extensible record with a kind, a capacity, a message queue and the heads of
two offer queues (`waitS`, `waitR`). An offer (`Offer`) stores a common context `Ctx`, the index of
the alternative, the operation kind and the message.

Offers and contexts come from the free lists `freeOffers` and `freeCtx` and return there after the
operation. A free-list record lies in no channel queue. Selection without a timeout does not
allocate. The message queue of `Buffer` and `Mailbox` is a ring in an array; for `Buffer` its length
is the capacity, for `Mailbox` the ring doubles when full.

An operation on a channel between two switch points runs without switches: there is one thread and
no preemption. So `Enter` takes the channel monitor only when someone holds it
(`Monitors.IsLocked`). Then the operation waits for the monitor release; otherwise exclusion holds
without the monitor.

Two procedures perform an operation on a channel. `Put` gives the message to a waiting receiver or
puts it into the ring and checks the protocol. `Take` takes a message from the ring or from a
waiting sender. After a `Take` from the ring, `CompleteSend` performs waiting sends while there is
room. Both procedures skip offers with the caller's decision: an offer is not performed against an
offer with the same `Ctx`.

`Select` walks alternatives in index order. For a send or a receive it takes an offer and calls
`Register`. `Register` performs the operation through `Put` or `Take` or puts the offer into
`waitS` or `waitR`. The timeout with the smallest `ms` schedules a `Services` action. The task
sleeps in `Tasks.Park` until `Ctx.decided` becomes true.

The decision is made by the side that performed the offer first. It writes `ctx.decided` and
`ctx.winner`, takes or gives the message and wakes the task with `Tasks.Unpark`. The channel
monitor is held at that moment only if someone else held it. `Wake` wakes only when
`ctx.ready = TRUE`. During registration the task may wait for the monitor of another channel, and
`Unpark` would move it to `Ready` without the monitor. After waking, the owner withdraws the offers
(`Withdraw`). Withdrawal is idempotent, so the winner is withdrawn by the same call.

Direct `Send` and `Recv` of all three kinds are `QueueSend` and `QueueRecv`. The operation runs at
once through `Put` or `Take`; otherwise the offer joins `waitS` or `waitR`, and the task sleeps in
`Tasks.Park`. The other side performs the offer and wakes the task without handing over the
monitor. A rendezvous is a channel of capacity 0: a send without a receiver always waits in
`waitS`. Dead offers are removed when a queue is walked. An offer is dead when its task is in
`Finished` or `Cancelled`.

### 11.2. I/O

`Poller` stores only a pointer to the platform hook. The descriptor registry and `poll` live in
`LinConsole`: an array of 4096 entries, stdin as the first element, one-shot registration. Ready
entries are removed before `Waiter.Ready` is called, so that a repeated registration does not
disturb the walk.

### 11.3. Worker Threads

`AsyncWork` is the facade; `AsyncWorkLin` and `AsyncWorkWin` are the delegates. Linux: up to four
pthreads, a static slot array under a mutex and a condition variable, a ring queue of 256 jobs, a
byte in a pipe as the readiness signal. Windows: up to four `CreateThread` threads, slots under a
`CRITICAL_SECTION`, a queue event; readiness is collected by a `Services` action that polls `done`
every 5 ms.

Synchronization primitives live in the heap, not in global module records: global records do not get
guaranteed alignment, and futex and `CRITICAL_SECTION` require it.

### 11.4. Computons

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
