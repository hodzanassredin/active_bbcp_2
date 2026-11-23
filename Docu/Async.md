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
with a guard page on Linux x64) and released after the task finishes.
The default size is 256 KB; pages are committed on touch.

## Monitors

A monitor is a side-table entry in the kernel keyed by the **address of
an object**. Any object can be a monitor; no field is reserved in the
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
- **Traps release monitors.** Every `Lock` is recorded in a chain on
  the current task; `Kernel.Try` remembers the chain depth on entry and
  releases everything taken inside it when the body exits or traps.
  This works for nested `Try`, and an outer acquisition survives a trap
  caught by an inner `Try`.
- The garbage collector treats objects in the monitor table and the
  records of the lock chains as roots.
- `Kernel.IsLocked(obj)`, `Kernel.MonitorCount()`,
  `Kernel.WaitersCount()` and `Kernel.DumpMonitors` are available for
  tests and diagnostics.

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
- Expansion outline: the condition becomes a hidden local procedure
  `PROCEDURE $CondN (frame: ADDRESS): BOOLEAN`; the statement becomes
  `IF ~cond THEN Kernel.Await($CondN, frame, receiver) END`, where
  `frame` is the waiting procedure's static link.

### Yield points

An optional module compilation key inserts a call to `Kernel.Poll` on
the back edge of `WHILE`, `REPEAT`, `LOOP` and `FOR`.

- `Poll` yields only when the scheduler raised its flag, the current
  stack is a task, and the task holds no monitor. Inside an
  `[exclusive]` region only an explicit `ACTIVE.AWAIT` may switch
  tasks, so a monitor is a boundary of an atomic update.
- Framework modules (System, Std, Text, Form, Host, Dev, platform
  subsystems) must not be compiled with the key: a framework call from
  a task is atomic with respect to other tasks.
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
- Only one OS thread; no parallel Component Pascal code. A blocking OS
  call must be moved to an OS worker thread (not implemented yet) or it
  freezes all tasks and the framework.
- There is no `Select`/`Alt` yet; it is built on `Tasks.Park`/`Unpark`
  in the library stage.

## Status

Implemented in this branch: tasks, stacks, scheduler, cancellation,
groups, monitors (`Lock`, `Unlock`, `Wait`, `Await`), trap release,
GC roots, deadlock journal, event-loop integration.

Compiler features described above (`ACTIVE`, `ACTIVE.AWAIT`,
`[exclusive]`, yield points, module monitors) are stage 6; check
`docs/` for the current state before relying on the syntax.
