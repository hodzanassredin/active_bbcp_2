# Tasks

A guide to the `Tasks` module for the Component Pascal programmer. It describes a task, its states,
the operations on it, groups, the scheduler and the cost. The language extension — exclusive
procedures, `ACTIVE.AWAIT` and yield points — is defined in `Docu/Active-CP-Lang.md`; the channel
and worker-thread library is described in `Docu/Async.md`.

A task is an extensible record with a `Run` method and its own stack. Tasks are cooperative: one
operating-system thread executes Component Pascal code, and a task yields it only at an explicit
switch point.

## 1. Task and Switch Point

A *task* is the sequential execution of a program on its own stack. The module `Tasks` creates and
starts a task; at most one task runs at any moment.

A *switch point* is a place where the running task may yield the processor. There are three kinds
of switch point:

1. a call to a `Tasks` operation that suspends the task: `Yield`, `Sleep`, `Join`, `Park`;
2. entering an occupied monitor and `ACTIVE.AWAIT` with a false condition;
3. a yield point, if the module was compiled with the `^` key (section 7 of the language report).

Between two switch points a task executes indivisibly with respect to the other tasks. It follows
that a sequence of statements without switch points needs no protection.

Code outside a task — a module body, a command, an event handler — behaves like a task that never
waits. Waiting in it is an abnormal termination (section 6).

## 2. States

A task stores its state in the field `state`; the state constants are exported by `Tasks`.

| State | Value | Meaning |
|---|---:|---|
| `New` | 0 | created but not started |
| `Ready` | 1 | ready to run, in the queue |
| `Running` | 2 | running |
| `Sleeping` | 3 | sleeping until the `Sleep` deadline |
| `Waiting` | 4 | waiting for another task to finish in `Join` |
| `Parked` | 5 | waiting for `Unpark` or a monitor handover |
| `Finished` | 6 | the body of `Run` has returned |
| `Cancelled` | 7 | the task is cancelled |

Transitions:

| From | To | Cause |
|---|---|---|
| `New` | `Ready` | `Start` or `StartIn` |
| `Ready` | `Running` | the scheduler took the task from the queue |
| `Running` | `Ready` | `Yield`; `Sleep` with a non-positive delay |
| `Running` | `Sleeping` | `Sleep` with a positive delay |
| `Running` | `Waiting` | `Join`, the target has not finished |
| `Running` | `Parked` | `Park`; waiting for one's group to empty |
| `Running` | `Finished` | the body of `Run` has returned |
| `Running` | `Cancelled` | cancellation detected at a wait point or at task entry |
| `Sleeping` | `Ready` | the deadline expired; `Unpark`; `Cancel` |
| `Waiting` | `Ready` | the target finished; `Unpark`; `Cancel` |
| `Parked` | `Ready` | `Unpark`; `Cancel`; a monitor was handed over |
| `Finished`, `Cancelled` | — | terminal states |

`Cancel` on a task in `New`, `Ready` or `Running` does not change the state: it only sets the flag.
A task in `New` goes directly to `Cancelled` when started, and its body does not run.

## 3. Operations

| Operation | Effect | Suspends | Traps |
|---|---|---|---|
| `Start(t)`, `StartIn(t, g)` | allocates the stack and puts the task in the ready queue | no | `t = NIL`; the task was already started; the task object lies on a stack; no stack memory |
| `Yield` | puts the task at the tail of the ready queue | yes | outside a task; in an action handler; in a domain broadcast; the task is cancelled |
| `Sleep(ms)` | sleeps at least `ms` milliseconds | yes, if `ms > 0` | the same as `Yield` |
| `Join(t)` | waits for `t` to finish or be cancelled | yes, while `t` is not done | the same; `t = NIL` |
| `Park` | sleeps until `Unpark` | yes, unless a wake-up arrived earlier | the same |
| `Unpark(t)` | wakes `t`; a wake-up before `Park` is not lost | no | `t = NIL` |
| `Cancel(t)` | sets the cancellation flag and wakes a sleeping task | no | `t = NIL` |
| `Step` | runs ready tasks for at most one quantum | no | called from a task or re-entered |
| `NewGroup` | creates a group owned by the current task | no | — |
| `GroupFailed(g)`, `GroupCount(g)` | group state | no | — |
| `OwnGroup(t)` | the group owned by `t` | no | — |
| `State(t)`, `Failed(t)`, `GetName(t, s)` | state, trap flag, name | no | — |
| `SetName(t, s)` | sets the name | no | — |
| `SetStackSize(t, bytes)` | sets the stack size before start | no | — |
| `Count()` | number of unfinished tasks | no | — |
| `Current()`, `InTask()` | the current task; whether a task is running | no | — |
| `Deadlocked()` | whether the deadlock journal has been printed | no | — |
| `Switches()`, `ResetStats()` | switch counter and its reset | no | — |
| `ListSize()`, `List` | length of the task list and its printing | no | — |
| `NextDeadline()` | the nearest sleeping deadline, or −1 | no | — |

`StartIn` additionally registers the task in group `g` and increments `g.alive`. A task belongs to
at most one group. `Start` is equivalent to `StartIn(t, NIL)`.

`Sleep(ms)` with `ms <= 0` calls `Yield`. After waking, `Sleep` does not re-check cancellation:
cancellation is detected at the next wait point.

`Join` puts the waiting task in the target's waiter list. When the target finishes, `Reap` wakes all
waiters. If the target is already finished, `Join` returns at once.

`Park` consumes a wake-up that arrived before the call: `Unpark` sets the flag `wake`, and the next
`Park` returns without sleeping. The flag is also consumed on waking from a monitor, so the next
`Park` does not return spuriously.

`Cancel` does not put the task into `Cancelled` and does not interrupt a running task. A cancelled
task traps at the nearest wait point; `TaskBoot` catches the trap, the task gets the state
`Cancelled`, and the others continue.

`SetTimeSlice(t, ms)` stores the value in the task, but the scheduler does not read it: all tasks run
with the common quantum of 5 ms. The operation remains in the interface and does not affect
scheduling.

## 4. Groups and Cancellation

A *group* is a record with three fields: `alive` (number of live members), `failed` (a trap occurred
in at least one member) and `owner` (the task that created the group). `NewGroup` remembers the
current task as the owner; outside a task the owner is `NIL`.

Group rules:

1. `StartIn(t, g)` registers a member; `g.alive` is incremented.
2. After returning from `Run` the owner does not finish while `g.alive > 0`. It sets itself to
   `Parked` and waits until the last member is removed in `Reap`.
3. A trap in a member marks the group `failed` and cancels the other members. A member's
   cancellation takes effect at its nearest wait point.
4. Cancellation of the owner while it waits for the group cancels all members; the owner gets the
   state `Cancelled`.
5. A task has one field `own`: a second `NewGroup` replaces the previous group, and the owner waits
   only for the last one.

`Cancel(t)` cancels one task. Group cancellation is performed by the internal `CancelGroup`; it is
not exported.

## 5. Scheduler and Main Loop

The scheduler is an ordinary `Services.Action`. `Start`, `Yield`, `Sleep`, `Unpark` and a monitor
handover request it through `EnsureScheduled`: if the action is not yet registered, it is scheduled
for the nearest `Services` step (`Services.DoLater`). If there are no ready tasks, the action is
scheduled for the nearest sleeping deadline.

`Step` runs ready tasks:

1. Every 5 ms it raises the flag `Kernel.RequestPoll`; the yield point `Kernel.Poll` yields on that
   flag.
2. It scans sleepers at most once per changed value of `Kernel.Time` and moves those whose deadline
   has arrived to ready.
3. It takes a task from the head of the ready queue, switches to its stack and waits for the return.
4. It removes a finished task in `Reap`: takes it off the all-task list, frees its stack, wakes
   `Join` waiters, decrements the live counter and accounts for the group.
5. It returns control to the main loop when the 5 ms quantum has expired or the queue is empty.

`Step` is called only from the bottom of the main loop: from the scheduler action or from a test.
Calling it from a task and re-entering it are traps. A task is not preempted by the quantum: the
quantum is checked only after the task has returned control itself.

The main loop (`System/Mod/Loop.cp`) calls `Services.actionHook.Step`; the console loop
(`Cons/Mod/Interp.cp`) calls it between commands and while waiting for input. Tasks therefore run
both in a window and in the console.

## 6. Where Waiting Is Forbidden

`Tasks.CheckWait` checks three conditions and traps:

1. there is no current task — `wait outside a task`, trap 20;
2. the task is cancelled — trap 127 ("interrupted"): it differs from precondition traps, including
   trap 21 of a channel protocol violation;
3. an action handler is running (other than the scheduler's own action) or a domain broadcast is
   running — `wait inside an action handler or domain broadcast`, trap 21.

`Yield`, `Sleep`, `Join` and `Park` pass this check. The kernel (`Kernel.Wait` and `Kernel.Await`)
itself reports `wait outside a task` if no task is loaded.

The rule: a task waits only in its own code, not in a framework callback. Framework code is atomic
with respect to other tasks; a task talks to the framework through actions and the event loop.

## 7. Deadlock

When there are no ready tasks, no sleeping tasks with a deadline, and live tasks remain, `Step`
prints the journal once:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  monitor 7F...: held by "t1" count=1
    enters "t2"
  ...
```

`Tasks.Deadlocked()` reports this state to tests. The journal is a diagnostic, not an abnormal
termination: a later `Unpark` or `Cancel` may make progress. A task that never yields is not
detected.

## 8. Example

```
MODULE ObxTasks;
	IMPORT ACTIVE, Tasks, Console, Strings;

	VAR count: INTEGER; s: ARRAY 16 OF CHAR;

	TYPE Worker = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE [exclusive] Tick*;
	BEGIN INC(count) END Tick;

	PROCEDURE (w: Worker) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO Tick; Tasks.Yield END;
		Tick
	END Run;

	PROCEDURE Run*;
		VAR a, b: Worker;
	BEGIN
		NEW(a); NEW(b); count := 0;
		Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(count, s);
		Console.WriteStr("ObxTasks: count="); Console.WriteStr(s); Console.WriteLn
	END Run;

END ObxTasks.
```

Two tasks call the module's exclusive procedure and yield after each call. The command `Run`
executes outside a task, so it calls `Tasks.Step` itself until the tasks finish. The result is
`ObxTasks: count=22`.

## 9. Cost and Limits

**Stack.** The default size is 256 KB (`Tasks.defaultStack`). `SetStackSize` sets another size
before start; values up to 64 MB are allowed. The platform allocates memory with `mmap` and makes the
lowest page `PROT_NONE`: a stack overflow hits the guard page. Physical pages are committed on
touch, so the virtual memory for 10,000 tasks with 64 KB stacks is 640 MB, while the resident part
is tens of megabytes. After a task finishes its stack is released (`munmap`).

**Switching.** Measurement (`UnitRunner.Run System_devTasks proc slow`, Linux x86-64, one thread,
64 KB stacks):

| Tasks | Setup | Steps | Switches | Per switch | Virtual for stacks |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 7.2 ms | 8.0 ms | 4,000 | 2.0 µs | 65.5 MB |
| 10,000 | 119 ms | 160 ms | 40,000 | 4.0 µs | 655 MB |

Each task yields and then finishes; one task accounts for two entries and four switches. The peak
RSS of the 10,000-task run is about 47 MB (measured with `/usr/bin/time -v` around the same
command). The test prints the numbers in `BENCH` lines; the reproduction command is:

```sh
printf 'UnitRunner.Run System_devTasks proc slow quit\n' | tools/run64.sh
```

**Limits.**

1. One operating-system thread, no preemption: cancellation and `Yield` work only at wait points.
   A running task without waits is not interrupted.
2. A task object must not be allocated on a stack: `Start` checks this with `Kernel.StackOf` and
   traps.
3. If the platform cannot allocate a stack, the task does not start and `Start` traps with
   `no stack memory (platform does not support it)`.
4. Operating-system worker threads (`AsyncWork`) must not touch the heap or the `Tasks` module; the
   exception is jobs that themselves allocate no memory. See `Docu/Async.md`.
5. Without `Tasks` loaded the kernel still takes and releases free monitors, but waiting in them
   traps: there is nobody to wake the task.

## 10. Implementation Scheme

This section does not define behaviour; it describes how the behaviour is obtained.

`Tasks.Switch` is a `[code]` procedure inlined into its caller. On amd64 it saves 13 registers
(`rax`, `rbx`, `rbp`, `rsi`, `rdi`, `r8`–`r15`) and `rsp`; on i386 it saves five (`eax`, `ebx`,
`ebp`, `esi`, `edi`) and `esp`. A synthetic return address points to the continuation immediately
after the bytes of `Switch`; for a fresh task the return slot holds the address of `TaskBoot`.
Around the raw switch stand `Kernel.SwitchSave` and `Kernel.SwitchRestore`: they move the
`Kernel.Try` context, `trapStack` and `tryCount` into the stack record and restore them on return.

`Kernel.RegisterStack` puts a stack into the registry; `MarkLocals` scans the current stack from the
frame pointer and sleeping stacks from the saved stack pointer. `SwitchSave` records the stack
pointer with a slack of 256 bytes on amd64 (64 on i386), so that the registers saved by the switch
fall into the scan. On amd64 `Kernel.StackOf` finds a record by a sorted index with binary search;
on i386 it walks the list linearly.

`TaskBoot` is the entry point of a new stack. It calls `Run` under `Kernel.Try`; a normal return
gives `Finished`, a trap gives `failed` and `Finished`, cancellation gives `Cancelled`. After the
body `TaskBoot` waits for its group to empty. Then `Finish` switches to the main stack, where `Step`
removes the task in `Reap`.

The scheduler keeps ready tasks in a FIFO queue and sleepers in a singly linked list. `Reap` removes
a task from the doubly linked all-task list in O(1): otherwise finished tasks would keep their
objects alive until the end of the process. `CLOSE` of the module removes the scheduler
(`Kernel.SetScheduler(NIL)`), removes the action from `Services` and frees the stacks of live tasks.
