# Tasks, monitors, `ACTIVE.AWAIT` and `[exclusive]`

This page describes the concurrency extensions of this BlackBox build:
tasks with their own stack, monitors on any object, and the compiler
extension that makes the common patterns concise. It is written for
programmers who write Component Pascal code; implementation notes for
the port are in `docs/tasks.md` and in the sources.

The implementation is **cooperative and single-threaded**: tasks are
switched only in explicit wait points, the framework still runs on the
main (only) OS thread, and there is no preemption and no parallel
execution of Component Pascal code on the shared heap.

The core extension (tasks, monitors, `[exclusive]`, `ACTIVE.AWAIT`, the
`^` key) works on Linux AMD64 and i386. The optional `Async` add-on
library (channels, worker threads, computons) is built for AMD64 only.

## Tasks

A task is an extensible record with a `Run` method and its own stack.

```
TYPE Worker = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

PROCEDURE (w: Worker) Run;
BEGIN Tasks.Sleep(10); INC(w.n) END Run;

NEW(w); Tasks.SetName(w, "worker"); Tasks.Start(w)
```

Main operations:

```
Tasks.Start(t);                    Tasks.StartIn(t, group);
Tasks.Yield;                       Tasks.Sleep(ms);
Tasks.Join(t);                     Tasks.Cancel(t);
Tasks.Park;                        Tasks.Unpark(t);
Tasks.Current();                   Tasks.InTask();
Tasks.State(t); Tasks.Failed(t);   Tasks.Deadlocked();
Tasks.SetStackSize(t, bytes);      Tasks.SetTimeSlice(t, ms);
Tasks.NewGroup(); Tasks.StartIn(t, g);
Tasks.Count(); Tasks.List();
```

States: new, ready, running, sleeping, waiting, parked, finished,
cancelled. `Cancel` only sets a flag; it is checked in wait points
(`Yield`, `Sleep`, `Join`, `Park`, monitor wait). A cancelled task
receives a normal trap and unwinds, so finalisers and `Unlock` run.
A trap in one task does not affect the others.

Stack memory is allocated by the kernel (`Kernel.AllocStack`, `mmap`
with a guard page on Linux x64 and i386) and released after the task
finishes.
The default size is 256 KB; pages are committed on touch.

## Monitors

A monitor is a side-table entry in the ordinary module `Monitors`
(moved out of the kernel in s18) keyed by the **address of an object**. Any object can be a monitor; no field is reserved in the
object header. The table entry exists while the object is held or
awaited and disappears afterwards.

```
Kernel.Lock(obj);                  (* enter the monitor *)
Kernel.Unlock(obj);                (* leave it *)
Kernel.Wait(obj);                  (* manual wait *)
Kernel.Await(cond, frame, obj);    (* conditional wait, see below *)
```

Rules, as implemented:

- **Recursive entry is allowed** and counted. The same task may take
  the same monitor several times; each `Lock` needs an `Unlock`.
- **Unlocks are LIFO.** A task must release its monitors in reverse
  order of acquisition. `Kernel.Unlock` of a monitor that is not the
  innermost one traps.
- `Lock` on a monitor held by another task parks the caller until the
  monitor is handed over. A condition waiter whose condition is true
  gets the monitor **before** newly arriving tasks, so the condition
  cannot be falsified between the test and the entry.
- `Unlock` of the last nesting level hands the monitor to the first
  waiter with a true condition, otherwise to a manual `Wait` waiter,
  otherwise to the first arriving task, otherwise frees the entry.
- **Traps release monitors.** Every `Lock` is recorded in a chain
  keyed by the task token; `Kernel.Try` asks the `Monitors` hook for the
  chain depth on entry and releases everything taken inside it when the
  body exits or traps. This works for nested `Try`, and an outer
  acquisition survives a trap caught by an inner `Try`.
- The monitor table, the lock chains and their free lists are ordinary
  globals of `Monitors`; `MarkGlobals` scans them, so the kernel no
  longer marks monitors itself. The monitored object is stored in an
  `ANYPTR` field, which keeps it alive even without other references.
- `Monitors.IsLocked(obj)`, `Monitors.MonitorCount()`,
  `Monitors.WaitersCount()` and `Monitors.DumpMonitors` are available
  for tests and diagnostics.
- The kernel keeps only the four entry points called by generated code
  (`Kernel.Lock`, `Unlock`, `Wait`, `Await`) as stubs over one
  `Kernel.MonitorHook` object. Without the installed hook they trap with
  "monitors module is not loaded"; the standard `Lin`/`Win` init modules
  import `Monitors`, so it is present in a normal world.

Without `Tasks` loaded the kernel still takes and releases free
monitors, but waiting is a trap: **there is nobody to wake the task**.

`Kernel.Wait` releases the monitor and parks the caller. The program
must re-check its condition in a loop:

```
Kernel.Lock(b);
WHILE b.count = 0 DO Kernel.Wait(b) END;
x := b.item; DEC(b.count);
Kernel.Unlock(b)
```

### Conditional wait: `Kernel.Await`

`Kernel.Await(cond, frame, obj)` implements the Muller/Active Oberon
scheme. `cond` is a procedure of type
`PROCEDURE (frame: SYSTEM.ADDRESS): BOOLEAN`; `frame` is the frame of
the waiting procedure, so the condition can read locals and fields
through the static link. The waiting task registers `(cond, frame)` in
the monitor and sleeps.

When a task leaves the monitor (`Unlock`, or its own `Await`), it
re-evaluates the conditions of the waiters **in the waiters' frames**.
The first waiter whose condition is true receives the monitor together
with ownership and is woken; newly arriving tasks wait behind it.

Details:

- The compiler evaluates the condition first and calls the kernel only
  when it is `FALSE`; a manual call may evaluate it directly (a
  module-level condition procedure needs no frame).
- `Await` without owning the monitor traps, as does `Wait` outside a
  task.
- A trap raised while evaluating the condition in another task is
  **forwarded** to the waiting task: its `Await` prints
  `Kernel: Await: trap N in the condition` and raises trap 121.
- `Await` releases only the monitor named in `obj`. Other monitors
  held by the task stay held; this can deadlock, and the deadlock
  journal shows the chain.
- Recursive entry is restored across the wait (`count` is saved and
  restored).

## Waiting rules

Waiting is a trap in these contexts:

- outside a task (`Tasks.Current() = NIL`);
- inside an action handler (`Services.Action.Do`) other than the task
  scheduler itself;
- inside a domain broadcast (`Models.InDomainCast`).

These checks are done by `Tasks.CheckWait`; `Kernel.Wait`/`Await`
report "wait outside a task" by themselves.

The rule of thumb: **a task may wait only in its own code**, not in a
framework callback. Framework code runs atomically with respect to
other tasks; a task talks to the framework through actions and the
event loop.

### Deadlock journal

When the scheduler has no ready task, no sleeping task with a deadline,
and live tasks remain, `Tasks.Step` prints once:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  monitor 7F...: held by "t1" count=1
    enters "t2"
  ...
```

`Tasks.Deadlocked()` reports the state to tests. The journal is a
diagnostic, not a fatal error: a later `Tasks.Unpark` or `Cancel` can
still make progress. A task that never yields is not detected.

## Compiler extension (stage 6)

The following syntax and checks are part of the language extension in
this branch; the runtime they rely on is `Kernel`:

```
MODULE CtlPump;
	IMPORT ACTIVE;
	VAR level: INTEGER; running: BOOLEAN;

	PROCEDURE [exclusive] SetLevel* (x: INTEGER);
	BEGIN level := x END SetLevel;

	PROCEDURE [exclusive] WaitLow* (limit: INTEGER);
	BEGIN ACTIVE.AWAIT(level < limit) END WaitLow;
END CtlPump.
```

`ACTIVE` is a pseudo-module built into the compiler, like `SYSTEM`.
`IMPORT ACTIVE` marks a module that uses tasks; the import is easy to
find by text. `SYSTEM` stays clean.

### `[exclusive]`

- Allowed on methods (`PROCEDURE [exclusive] (r: T) M`) and on
  top-level procedures of a module (module monitor). Forbidden on local
  procedures and together with `[code]`/`[ccall]`, and without
  `IMPORT ACTIVE`.
- The prologue calls `Kernel.Lock` with the receiver (a method) or
  with a hidden module-monitor object (a top-level procedure). The
  module monitor is a hidden global created before the module body
  runs; all `[exclusive]` top-level procedures of the module mutually
  exclude each other.
- All exits — every `RETURN`, falling off the end, function results of
  every type — go through one epilogue that calls `Kernel.Unlock`.
  A trap releases the monitor through the `Kernel.Try` chain.
- Repeated entry into one's own monitor is allowed and counted.
- The flag is not written to the symbol file: the caller does not need
  it.
- Unloading a module whose monitor is held or awaited is refused, like
  unloading a module with frames on a task stack.

### `ACTIVE.AWAIT`

- Only inside an `[exclusive]` procedure or method; elsewhere it is a
  compile-time error.
- The argument is a `BOOLEAN` expression. The compiler checks it and
  calls `Kernel.Await` only when it is `FALSE`.
- The expression is **read-only**: it may read variables and fields
  but must not call procedures or have side effects (enforced by the
  compiler). The condition is evaluated later in the frame of the
  waiting task, possibly while another task is leaving the monitor.
- Expansion outline: the condition becomes a hidden nested procedure
  `@awaitN (frame: ADDRESS): BOOLEAN` with the body `RETURN cond`; the
  statement becomes

  ```
  SYSTEM.GETREG(BP, @frm);          (* the waiter's frame *)
  IF ~cond THEN Kernel.Await(@awaitN, @frm, monitor) END
  ```

  The condition reads the waiter's variables through the CP static
  link: `Kernel.CallCond` calls `@awaitN` with `@frm` in `BX`, and the
  frame stays alive on the waiter's stack while it sleeps. A condition
  that only reads module globals needs no frame, but the frame is
  passed in the same way.
- The compiler evaluates the condition once before the call; the
  kernel evaluates it again in `Await` before parking and the leaving
  task evaluates it in `Unlock` (`FindCondition`), as in Active
  Oberon. Only the waiter whose condition is true gets the monitor,
  and it gets it before new arrivals.

### Yield points

An optional module compilation key inserts a call to `Kernel.Poll` on
the back edge of `WHILE`, `REPEAT`, `LOOP` and `FOR`. The key is the
character `^` after the module name:

```sh
echo 'DevCompiler64.CompileThis DevTestsYield ^' | ./dev0Linux   # amd64
echo 'DevCompiler.CompileThis DevTestsYield ^' | ./dev0Linux     # i386
# or: echo "DevCompiler.CompileOpt('^')" ...
```

The key is a module option; it is not written to the symbol file, so a
caller need not know whether the callee has yield points.

- `Poll` yields only when the scheduler raised its flag, the current
  stack is a task, and the task holds no monitor. Inside an
  `[exclusive]` region only an explicit `ACTIVE.AWAIT` may switch
  tasks, so a monitor is a boundary of an atomic update.
- The scheduler (`Tasks.Step`) raises the flag at most every
  `pollPeriod` (5 ms); a `Poll` that yields clears it. A cancelled task
  traps at a `Poll` that actually yields, so the key adds cancellation
  points as well.
- Framework modules (System, Std, Text, Form, Host, Dev, platform
  subsystems Lin/Win) must not be compiled with the key: the compiler
  rejects the option with an error. A framework call from a task is
  atomic with respect to other tasks.
- The key is implemented for AMD64 and i386 targets; other targets
  reject it. On i386 the compiler inserts `Kernel.Poll` on backward
  loop jumps in the same way (`CPC486`/`CPV486`).
- There is no signal-driven preemption; the key is the only implicit
  yield point, and code compiled with it must protect shared data.

## Examples

### Bounded buffer, manual kernel API

```
TYPE Buffer = POINTER TO RECORD
	item: ARRAY 8 OF INTEGER; head, tail, count: INTEGER
END;

PROCEDURE Put (b: Buffer; x: INTEGER);
BEGIN
	Kernel.Lock(b);
	WHILE b.count = 8 DO Kernel.Wait(b) END;
	b.item[b.tail] := x; b.tail := (b.tail + 1) MOD 8; INC(b.count);
	Kernel.Unlock(b)
END Put;

PROCEDURE Get (b: Buffer; VAR x: INTEGER);
BEGIN
	Kernel.Lock(b);
	WHILE b.count = 0 DO Kernel.Wait(b) END;
	x := b.item[b.head]; b.head := (b.head + 1) MOD 8; DEC(b.count);
	Kernel.Unlock(b)
END Get;
```

`Put` and `Get` are ordinary procedures; several producers and
consumers can call them from tasks. The monitor is the `Buffer` object
itself.

### Bounded buffer with `ACTIVE`

```
PROCEDURE [exclusive] (b: Buffer) Put* (x: INTEGER);
BEGIN
	ACTIVE.AWAIT(b.count < 8);
	b.item[b.tail] := x; b.tail := (b.tail + 1) MOD 8; INC(b.count)
END Put;

PROCEDURE [exclusive] (b: Buffer) Get* (VAR x: INTEGER);
BEGIN
	ACTIVE.AWAIT(b.count > 0);
	x := b.item[b.head]; b.head := (b.head + 1) MOD 8; DEC(b.count)
END Get;
```

The prologue and epilogue take and release `b`; `AWAIT` releases it
while waiting and re-enters with the monitor already held.

### Module monitor: a pump controller

`CtlPump` above is a singleton: all `[exclusive]` procedures of the
module share one monitor, and `AWAIT` reads module globals. Two tasks
may call `SetLevel` and `WaitLow` without any explicit `Lock`.

## Channels and select (stage 7)

The optional `Async` subsystem is written on the same
`[exclusive]`/`ACTIVE.AWAIT` extension. Module `AsyncOnce` is a
single-assignment variable:

```
o := AsyncOnce.New();
o.FillInt(42);          (* once only; a second Fill traps *)
x := o.ReadInt();       (* waits until filled *)
```

`AsyncChan` provides a rendezvous channel `Chan`, a bounded `Buffer` and
an unbounded `Mailbox`:

```
c := AsyncChan.NewChan();  b := AsyncChan.NewBuffer(8);
m := AsyncChan.NewMailbox();
c.Send(msg); c.Recv(msg);          (* rendezvous *)
b.Send(msg); b.Recv(msg);          (* bounded *)
m.Send(msg); m.Recv(msg);          (* unbounded *)
```

A message is `AsyncSelect.Msg`, a pointer to an extensible record; the
receiver dispatches with `WITH`. After sending, a message must not be
modified (the Clojure/Erlang discipline).

`Select`/`Alt` follows Hopac/CML. Alternatives are descriptions in an
array; `Select` returns the index of the one that fired:

```
VAR ops: ARRAY 3 OF AsyncSelect.Op;
AsyncChan.RecvAlt(a, ops[0]);
AsyncChan.RecvAlt(b, ops[1]);
AsyncChan.TimeoutAlt(100, ops[2]);
i := AsyncChan.Select(ops);
```

Each alternative leaves an offer in the channel with a shared decision
flag. The first channel that can execute an offer sets the flag under
its monitor and wakes the task with `Tasks.Unpark`; the owner withdraws
the losing offers when it wakes (nack). A deposited send offer is rolled
back if another alternative won. `Select` may only be called from a
task.

`Poller` (`System/Mod/Poller.cp`) registers descriptors; the console
host polls stdin and the registered descriptors in one `poll`. `AsyncIO`
builds "read or wait" and "write or wait" on top of `CommStreams`:

```
AsyncIO.Read(s, buf, beg, len, read);
AsyncIO.Write(s, buf, beg, len, written);
AsyncIO.Accept(l, s, timeout);
```

`CommStreams.Stream` and `Listener` have `Handle()`; `CommTCP__Lin`
returns the socket and has been switched from `select` to `poll`
(`fd_set` is limited to 1024 descriptors), with a larger accept
backlog. With a thousand open connections at rest, the test process
uses 7–50 ms of CPU per second.

A channel may carry an optional Zonnon-style dialogue protocol, see
"Channel protocol (stage 10)" below. `AsyncTask` from Ivan's framework
is not present in this snapshot; `Tasks.Task` plays the role of the
automaton, and `Select`/`Park`/`Join` bridge tasks and channels.

## OS worker threads (stage 8)

`AsyncWork` runs blocking OS calls in a pool of worker threads. The
client API, the `[untagged]` request records and the `[ccall]` job
procedures live in the portable facade (`Async/Mod64/Work.cp`); the
platform delegate is installed by the platform init:

- `AsyncWorkLin` (Linux x64): four pthreads, static slots under a
  pthread mutex, a worker writes one byte into a pipe, the main loop
  sees it through `Poller` and wakes the waiting task. SIGINT/SIGTERM
  are blocked in the workers.
- `AsyncWorkWin` (Windows x64): four `CreateThread` threads, static
  `[untagged]` slots under a `CRITICAL_SECTION`, the queue waits on a
  manual-reset event, completion is a `done` flag plus an auto-reset
  event. The main loop picks results up with a `Services` action
  (re-posted every 5 ms while slots are busy), so windows and the
  message loop are never blocked. Files use `CreateFileA`/`ReadFile`/
  `WriteFile`, names use `getaddrinfo` from `AsyncWinSock` (a WS2_32
  interface module); `WSAStartup(2.2)` runs at install.

```
id := AsyncWork.Execute(S.ADR(SomeJob), S.ADDRESS(arg));  (* from a task *)
status := AsyncWork.Result(id);
```

The delegate is installed lazily: the first `AsyncWork.Execute` calls
`Dialog.Call("<Module>.Install", ...)` from `AsyncWork.Ensure`, so the
platform init does not start the pool. `ReadFileJob`, `WriteFileJob`
and `ResolveJob` (`getaddrinfo`) are provided. A blocking call made
directly from a task still freezes the framework — move it to a worker.

The worst case is covered by `tools/test-resolver.sh`: inside
`unshare -rmn` with a dummy interface and a private resolv.conf whose
name server is unreachable (packets are dropped silently, default
timeout and attempts), `ResolveJob` for a nonexistent name waits
10,010 ms, while the main loop makes 9,449 steps. Without
`unshare`/`ip`/`mount` the scenario prints `SKIP` with the reason.
`tools/test.sh` runs it as a separate stage. `AsyncTestsWinWorkers` repeats
the slow-call, file and `getaddrinfo` checks under Wine (4/4).

## Computons (stage 9)

`AsyncComputon` reproduces the `RobustParallel` interface:

```
TYPE Computon* = POINTER TO ABSTRACT RECORD (Stores.Store) END;
     PROCEDURE (c: Computon) Do*, NEW, ABSTRACT;

AsyncComputon.Start(n);
AsyncComputon.Push(c, rank, tag);
r := AsyncComputon.Pull(rank, tag);    (* a wait point *)
AsyncComputon.Try(rank, tag, r);
AsyncComputon.Stop;
```

A computon is serialized with `Stores` and executed in a separate
BlackBox process (owned heap and collector), so `Do` may use `NEW`.
The computon type must implement `Externalize`/`Internalize`.

The default transport is `fork` + `exec` of the same
`blackbox-console` with three pipes per executor. A TCP transport over
localhost is available as well (see "Computon transports and code
update" below).

`Pull` from a task is a wait point (`Tasks.Sleep` with a deadline),
and `AsyncComputon.AltPull` is a `Select` alternative: a `Select` can
wait for a computon result together with channels and a timeout.
When an executor dies, `Poller` sees EOF and the request is retried on
a live executor; a `Do` trap is caught in the worker and returned as an
error code (`Pull` returns `NIL`, `LastError()` gives the code).
Measured: 12 computons on 1, 4 and 16 local processes, same result; a
`SIGKILL`ed executor is retried in 414 ms.

### `RobustParallel` facade

`Robust/Mod64/Parallel.cp` exposes the published definition from
I. Denisov's talk (2017, p. 19) on top of `AsyncComputon`:

```
CONST tag* = 1;
TYPE Computon* = AsyncComputon.Computon;   (* Do, NEW, ABSTRACT *)
     Hook* = AsyncComputon.Hook;           (* Failed(rank, tag, err) *)
VAR rank-, size-: INTEGER;
LogMsg, Push, Pull, Try, SetHook, Wait, Stop, Unload
```

The slide does not show `Hook`; `AsyncComputon.Hook` was chosen. Ranks
follow MPI: 0 is the controller (the client), 1..size−1 are executor
processes; `Push`/`Pull`/`Try` shift the rank by −1. The world size is
the external variable `ROBUST_SIZE` (default 1, controller only); the
facade does not start a nested pool inside an executor
(`ASYNC_IN`/`ASYNC_OUT`/`ASYNC_TCP`). `Unload` has no result in the
definition; a refusal is logged only.

A client written only against this definition in MPI style (rank 0
splits the work, `Push`es it, `Pull`s the results; the computon's `Do`
is the "others compute" branch) returns 500500 = 1+...+1000 on 1, 4
and 16 processes (`tools/test-robust.sh`). Compatibility is proven
against the published interface; running Ivan's actual code requires
that code.

### MPI transport

MPICH 5.0.2 is installed into user space (`uv pip install mpich` in
`.work/venv`); `tools/bbmpi.c` hides MPI behind seven C functions and
`AsyncMpi` binds them with `LinDl` only when the transport is selected,
so ordinary runs need no MPI library. World layout: rank 0 is the
BlackBox controller, ranks 1..n run `AsyncMpi.WorkerLoop`. Frames use
the same 12-byte header and body over `MPI_Send`/`MPI_Recv`; the main
loop polls `MPI_Iprobe` from a `Services` action. `Start(n)` requires
an n+1 world. No retry after a rank dies (mpiexec usually kills the
job), `Unload` is refused and `WorkerPid` is unknown. Test:
12 squares on 4 executors under `mpiexec -n 5`, 252 ms.

## Channel protocol (stage 10)

`AsyncProto` builds a deterministic state machine from a compact text
description; a channel may carry one as an optional dialogue protocol:

```
start: RequestMsg > wait; wait: ResponseMsg > wait;
wait: EndMsg > done; done:
```

```
p := AsyncProto.New(desc);      (* NIL on a syntax error *)
AsyncChan.SetProtocol(c, p);    (* debug mode: check every direct Send *)
```

A clause is `state: Message > state`; `>` and `->` are equivalent,
clauses are separated by `;` or a newline, and `state:` without an
arrow declares a state with no outgoing transition (final). The message
label is the dynamic record type name of `AsyncSelect.Msg`
(`Kernel.TypeOf`), e.g. `RequestMsg`. A send that is not allowed in the
current state prints a readable text to the console and traps (code
21); the text is kept in `AsyncProto.LastViolation`. Without a protocol
`AsyncSelect.CheckSend` returns after one `NIL` test, so there is no
overhead. Sends made through `Select` (`SendAlt`) are checked at the
moment the offer is actually executed — immediately in `Register`, or
when a deposited offer is taken in `TakeRendezvous`/`CompleteSend`. A
losing offer does not advance the protocol; a violation traps the task
performing the operation.

## Computon transports and code update

```
ok := AsyncComputon.SetTransport("tcp");   (* or "pipe"; before Start *)
AsyncComputon.Unload("SomeModule");        (* TRUE — reload on next Push *)
```

The transport is also selected by the external variable
`ASYNC_TRANSPORT` (`tcp` or `pipe`). In TCP mode the controller listens
on `127.0.0.1:0`, the executor connects to the address passed as
`--ASYNC_TCP=...`; frame layout, EOF detection and retry after executor
death are unchanged. The executor switches its socket back to blocking
mode (only it reads frames there); the controller keeps it
non-blocking for `Poller`.

`Unload(module)` asks every live executor to drop the module code so
that the next `Push` loads the freshly compiled `.ocf`. It returns
`FALSE` and `LastError() = 20` when a module is busy (the executor's
own transport module, or a module still imported or present on a
stack); a module that was not loaded counts as success.

## Relation to Active Oberon and limits

- The condition is a procedure plus a frame (Muller's scheme), not a
  closure; waiting tasks re-check in their own frames, so no garbage
  collector support for closures is needed.
- Unlike the native A2 kernel, a trap inside an `[exclusive]` region
  releases the monitor (through the task's lock chain).
- Unlike A2, repeated entry into one's own monitor is allowed and
  counted.
- `Unlock` and `Await` evaluate the waiters' conditions; a trap there
  is forwarded to the waiter instead of hitting the leaving task.
- One OS thread executes Component Pascal; parallel CP code does not
  exist. Blocking OS calls can be moved to the worker-thread pool of
  stage 8; a blocking call made directly from a task still freezes all
  tasks and the framework.
- `Select`/`Alt`, channels, `Once` and I/O waits are in the optional
  `Async` subsystem (stage 7), built on `Tasks.Park`/`Unpark` and
  monitors.

## Status

Implemented in this branch: tasks, stacks, scheduler, cancellation,
groups, monitors (`Lock`, `Unlock`, `Wait`, `Await`), trap release,
GC roots, deadlock journal, event-loop integration.

Implemented in this branch (stage 6): the `ACTIVE` pseudo-module,
`ACTIVE.AWAIT` with the Müller condition procedure, `[exclusive]` on
methods and top-level procedures, module monitors, the `^` yield-point
key, and the compiler-side trap release through the `Kernel.Try` chain.

Implemented in this branch (stage 7): `AsyncOnce`, `AsyncChan` (`Chan`,
`Buffer`, `Mailbox`), `AsyncSelect` (`Alt`/`Select` with offers, nack
and timeout), `Poller` and `AsyncIO`.

Implemented in this branch (stage 8): the `AsyncWork` facade with the
`AsyncWorkLin` (pthread pool, result pipe, `Poller`) and `AsyncWorkWin`
(`CreateThread`, critical section, events, `Services` action,
`CreateFile`/`ReadFile`/`WriteFile`, `getaddrinfo` through WS2_32)
delegates, `LinThreads`, `LinNative`, and the `Kernel` worker allocation
counter.

Implemented in this branch (stage 9): `AsyncComputon` (local BlackBox
processes over pipes and TCP, `Push`/`Pull`/`Try`, retry after executor
death, error results), the `RobustParallel` facade, and an MPI transport
over MPICH installed into user space (`SetTransport("mpi")`,
`mpiexec -n 5` test).

Implemented in this branch (stage 10): the Zonnon-style channel
protocol (`AsyncProto`) with checks for direct sends and `SendAlt`, the
TCP computon transport, `Unload` for code update on executors, and
deterministic compile timestamps for the bootstrap.

Also implemented: Windows x64 task stacks (`VirtualAlloc` +
`PAGE_NOACCESS`), per-stack `Try` context, the `CreateThread` worker
pool with readiness delivered by a `Services` action, and console
Unit/Tasks/Workers tests under wine (`tools/test-win64.sh`). The GUI
scenario is checked headlessly under `gtk4-broadwayd`
(`tools/test-gui.sh`).

### Compiler implementation notes

- The kernel entry points for generated code (`Kernel.Lock`, `Unlock`,
  `Await`, `Poll`) live in a hidden import module `"$$"`; the loader
  binds it to `Kernel`, so no object-file format or loader change was
  needed. Since s18 `Kernel.Lock/Unlock/Wait/Await` are stubs forwarding
  to `Monitors`; `Kernel.Poll` asks the scheduler hook object
  (`Kernel.SetScheduler(s)`), which replaced eight procedure variables.
- `[exclusive]` is an ordinary positive procedure flag (32), accepted
  only with `IMPORT ACTIVE` and only as the sole flag. Misuse is a
  compile error (compiler error 225, "illegal sys flag"); a non-BOOLEAN
  condition is error 120.
- The condition of `AWAIT` may read variables, fields and array
  elements but must not call procedures or assign; the compiler rejects
  `Ncall`/`Nassign` inside it.
- `tools/bootstrap.sh` rebuilds `Dev` three times. Passes B and C use
  `BB_COMPILE_TIME="2000-01-01 00:00:00"`: `DevCPE` writes that fixed
  timestamp into every `.ocf` instead of the current time, so B and C
  must be byte-identical without masking. Pass A uses the packed
  compiler and is only used to build B's compiler.
- Compile-time rejection cases are checked by `tools/check-reject.sh`.
