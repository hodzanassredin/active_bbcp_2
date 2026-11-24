# Tasks

A guide to the `Tasks` module for the Component Pascal programmer. It describes a task, its states,
the module interface, groups, the scheduler and the cost. The language extension — exclusive
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
3. a yield point, if the module was compiled with the `^` key (sections 7 and 9.2 of the language report).

Between two switch points a task executes indivisibly with respect to the other tasks. It follows
that a sequence of statements without switch points needs no protection.

Code outside a task — a module body, a command, an event handler — behaves like a task that never
waits. Waiting in it is an abnormal termination (section 6).

## 2. States

`State(t)` returns the state of a task; the state constants are exported by `Tasks`.

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
| `Running` | `Cancelled` | cancellation detected at a switch point or at task entry |
| `Sleeping` | `Ready` | the deadline expired; `Unpark`; `Cancel` |
| `Waiting` | `Ready` | the target finished; `Unpark`; `Cancel` |
| `Parked` | `Ready` | `Unpark`; `Cancel`; a monitor was handed over |
| `Finished`, `Cancelled` | — | terminal states |

`Cancel` on a task in `New`, `Ready` or `Running` does not change the state: it only sets the flag.
A task in `New` goes directly to `Cancelled` when started, and its body does not run.

## 3. Interface

```
DEFINITION Tasks;

	CONST
		New = 0; Ready = 1; Running = 2; Sleeping = 3;
		Waiting = 4; Parked = 5; Finished = 6; Cancelled = 7;

	TYPE
		Task = POINTER TO ABSTRACT RECORD
			(t: Task) Run, NEW, ABSTRACT
		END;

		Group = POINTER TO RECORD END;

	PROCEDURE Cancel (t: Task);
	PROCEDURE Count (): INTEGER;
	PROCEDURE Current (): Task;
	PROCEDURE Deadlocked (): BOOLEAN;
	PROCEDURE Failed (t: Task): BOOLEAN;
	PROCEDURE GetName (t: Task; OUT s: ARRAY OF CHAR);
	PROCEDURE GroupCount (g: Group): INTEGER;
	PROCEDURE GroupFailed (g: Group): BOOLEAN;
	PROCEDURE InTask (): BOOLEAN;
	PROCEDURE Join (t: Task);
	PROCEDURE List;
	PROCEDURE ListSize (): INTEGER;
	PROCEDURE NewGroup (): Group;
	PROCEDURE NextDeadline (): LONGINT;
	PROCEDURE OwnGroup (t: Task): Group;
	PROCEDURE Park;
	PROCEDURE ResetStats;
	PROCEDURE SetName (t: Task; IN s: ARRAY OF CHAR);
	PROCEDURE SetStackSize (t: Task; size: INTEGER);
	PROCEDURE Sleep (ms: LONGINT);
	PROCEDURE Start (t: Task);
	PROCEDURE StartIn (t: Task; g: Group);
	PROCEDURE State (t: Task): INTEGER;
	PROCEDURE Step;
	PROCEDURE Switches (): LONGINT;
	PROCEDURE Unpark (t: Task);
	PROCEDURE Yield;

END Tasks.
```

Before each trap `Tasks` writes the line `Tasks: <text>` to the console. The procedures that suspend
a task (`Yield`, `Sleep`, `Join`, `Park`) start with the same check `CheckWait`; below it is written
as "the wait check":

Pre: the procedure is called from a task — trap 20.
Pre: the task is not cancelled — trap 127.
Pre: no `Services` action handler (other than the scheduler's action) and no domain broadcast is
running — trap 21.

**CONST New, Ready, Running, Sleeping, Waiting, Parked, Finished, Cancelled**
Task states (section 2).

**TYPE Task**
ABSTRACT
A task. The program extends the record and implements `Run`.

**PROCEDURE (t: Task) Run**
NEW, ABSTRACT
The task body. `TaskBoot` executes it on the task's stack under `Kernel.Try`.

**TYPE Group**
A group of tasks (section 4). The fields are not exported; `GroupCount` and `GroupFailed` read the
state.

**PROCEDURE Start (t: Task)**
Equivalent to `StartIn(t, NIL)`.

**PROCEDURE StartIn (t: Task; g: Group)**
Allocates a stack for the task and puts it at the tail of the ready queue. With `g # NIL` it records
the task in group `g`.

Pre: `t # NIL` — trap 20.
Pre: `State(t) = New` — trap 21.
Pre: the stack is allocated — trap 22, the console shows `no stack memory (platform does not support
it)`. The stack is not allocated if the size exceeds 64 MB or the platform gave no memory
(section 9).
Post: `State(t) = Ready`; with `g # NIL` `GroupCount(g)` is incremented by 1.

**PROCEDURE Yield**
Puts the current task at the tail of the ready queue and switches to the scheduler.

Pre: the wait check.
Post: the task runs again; the tasks that were ready earlier ran before it.

**PROCEDURE Sleep (ms: LONGINT)**
Suspends the task for at least `ms` milliseconds. With `ms <= 0` it is equivalent to `Yield`.

Pre: the wait check; after waking the check is repeated (cancelling a sleeping task — trap 127).
Post: the deadline expired, or `Unpark` woke the task; the `Unpark` wake-up is consumed.

**PROCEDURE Join (t: Task)**
Waits until `t` finishes or is cancelled. If `t` has already finished, it returns at once.

Pre: the wait check (traps 20, 127, 21); after waking the check is repeated.
Pre: `t # NIL` — trap 22.
Pre: `t # Current()` — trap 23.
Pre: `State(t) # New` — trap 24.
Post: `State(t) IN {Finished, Cancelled}`. `Unpark` of the waiting task does not end the wait and
stays for its next `Park`.

**PROCEDURE Park**
Suspends the task until `Unpark`. A wake-up that arrived before the call is not lost: `Park` returns
at once and consumes it.

Pre: the wait check; after waking the check is repeated.
Post: the wake-up is consumed.

**PROCEDURE Unpark (t: Task)**
Records the wake-up for the next `Park` of task `t` and wakes `t` if it sleeps, waits in `Join`, is
suspended in `Park` or waits for a monitor. `Sleep` and `Park` consume the wake-up. `Join`, a monitor
wait and `AsyncSelect.Select` do not consume it: they keep waiting, and the wake-up goes to the next
`Park`.

Pre: `t # NIL` — trap 20.
Post: `t` is ready, or its next `Park` returns at once.

**PROCEDURE Cancel (t: Task)**
Marks an unfinished task as cancelled and wakes it if it sleeps or waits. A cancelled task traps 127
at its nearest switch point.

Pre: `t # NIL` — trap 20.
Post: `t` is marked cancelled; the state of `t` does not change until its switch point.

**PROCEDURE NewGroup (): Group**
In a task returns its own group: the first call creates an empty group owned by the current task,
a repeated call returns the same group. Outside a task creates a new group without an owner.

**PROCEDURE GroupCount (g: Group): INTEGER**
The number of live members of the group; `0` for `g = NIL`.

**PROCEDURE GroupFailed (g: Group): BOOLEAN**
`TRUE` if at least one member trapped; `FALSE` for `g = NIL`.

**PROCEDURE OwnGroup (t: Task): Group**
The own group of task `t` (`NewGroup` in `t`); `NIL` if `t` has not created it, and for `t = NIL`.

**PROCEDURE Step**
Runs ready tasks until the 5 ms quantum expires or the queue is empty (section 5).

Pre: called outside a task — trap 20.
Pre: `Step` is not executing at this moment — trap 21.

**PROCEDURE NextDeadline (): LONGINT**
The nearest deadline of a sleeping task by the clock `Kernel.Time`; `-1` if no task sleeps.

**PROCEDURE Current (): Task**
The current task; `NIL` outside a task.

**PROCEDURE InTask (): BOOLEAN**
`Current() # NIL`.

**PROCEDURE State (t: Task): INTEGER**
The state of the task; `New` for `t = NIL`.

**PROCEDURE Failed (t: Task): BOOLEAN**
`TRUE` if the body of `t` ended with a trap; `FALSE` for `t = NIL`.

**PROCEDURE GetName (t: Task; OUT s: ARRAY OF CHAR)**
The name of the task; the empty string for `t = NIL`.

**PROCEDURE SetName (t: Task; IN s: ARRAY OF CHAR)**
Sets the name of the task, up to 63 characters. For `t = NIL` it does nothing. The procedure does
not check the length of `s`: a longer name traps on copying.

**PROCEDURE SetStackSize (t: Task; size: INTEGER)**
Sets the stack size in bytes; `0` means 256 KB.

Pre: `t # NIL` — trap 20.
Pre: `State(t) = New` — trap 21.
Pre: `size >= 0` — trap 22. The upper limit is not checked here; `StartIn` rejects a size above
64 MB (trap 22).

**PROCEDURE Count (): INTEGER**
The number of tasks started and not yet removed in `Reap`.

**PROCEDURE Deadlocked (): BOOLEAN**
`TRUE` if `Step` detected a deadlock and printed the journal (section 7).

**PROCEDURE Switches (): LONGINT**
The number of stack switches since the last `ResetStats`.

**PROCEDURE ResetStats**
Resets the counter `Switches` to zero.

**PROCEDURE ListSize (): INTEGER**
The length of the list of all tasks. A task removed in `Reap` is not in the list.

**PROCEDURE List**
Prints a line per task to the console: name, state, stack bounds, sleep deadline.

## 4. Groups and Cancellation

A *group* keeps the number of live members (`GroupCount`), the flag of a trap in at least one
member (`GroupFailed`) and the owner — the task that created the group. `NewGroup` remembers the
current task as the owner; outside a task the owner is `NIL`.

Group rules:

1. `StartIn(t, g)` registers a member; `GroupCount(g)` is incremented.
2. After returning from `Run` the owner does not finish while `GroupCount(g) > 0`. It sets itself to
   `Parked` and waits until the last member is removed in `Reap`.
3. A trap in a member marks the group `failed` and cancels the other members. A member's
   cancellation takes effect at its nearest switch point.
4. Cancellation of the owner while it waits for the group cancels all members; the owner gets the
   state `Cancelled`.
5. A trap or cancellation of the owner in `Run` cancels all members of its group: members do not
   outlive the owner. The owner does not wait for them; each finishes at its nearest switch point.
6. A task owns one group: a repeated `NewGroup` returns the same group (`OwnGroup`).

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
4. It removes a finished task in `Reap`: takes it off the all-task list, returns its stack to the
   pool, wakes `Join` waiters, decrements the live counter and accounts for the group.
5. It returns control to the main loop when the 5 ms quantum has expired or the queue is empty.
   Before returning it gives pool stacks above 1024 back to the kernel, at most 16 per step
   (section 10).

`Step` is called only from the bottom of the main loop: from the scheduler action or from a test.
Calling it from a task and re-entering it are traps. A task is not preempted by the quantum: the
quantum is checked only after the task has returned control itself.

The main loop (`System/Mod/Loop.cp`) calls `Services.actionHook.Step`; the console loop
(`Cons/Mod/Interp.cp`) calls it between commands and while waiting for input. Tasks therefore run
both in a window and in the console.

## 6. Where Waiting Is Forbidden

The wait check (`CheckWait`, section 3) traps in three cases:

1. there is no current task — `wait outside a task`, trap 20;
2. the task is cancelled — trap 127 ("interrupted"): it differs from precondition traps, including
   trap 21 of a channel protocol violation;
3. an action handler is running (other than the scheduler's own action) or a domain broadcast is
   running — `wait inside an action handler or domain broadcast`, trap 21.

`Yield`, `Sleep`, `Join` and `Park` pass this check. Waiting in a monitor outside a task
(`Kernel.Await`) traps 24 in the module `Monitors` (language report, section 8).

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

**Stack.** The default size is 256 KB. `SetStackSize` sets another size before start. The kernel sets
the limit: `Kernel.AllocStack` refuses a size above 64 MB (`maxStackScan`). Then `StartIn` traps 22,
and the task stays in the state `New`. The platform allocates memory with `mmap` and turns the lowest
page into `PROT_NONE`: a stack overflow hits the guard page. Physical pages are allocated on touch.
Therefore 10 thousand tasks with 64 KB stacks take 640 MB of virtual memory and tens of megabytes of
physical memory.

After a task finishes, `Reap` returns its stack to the pool. The next task of the same size takes a
stack from the pool without a system call. The pool keeps up to 1024 free stacks; the scheduler
returns the surplus to the kernel (`munmap`), at most 16 stacks per step.

**Cost.** Numbers, conditions and reproduction commands are collected in `docs/measurements.md`,
section 2. At load average 15.2–15.9 (24.11.2026), 64 KB stacks:

| Measurement | BlackBox | Go, `GOMAXPROCS=1` |
|---|---:|---:|
| creating 10 thousand tasks | 60–70 ms | 18.6–20.2 ms |
| creating 10 thousand tasks, stacks from the pool | 2.3–3.1 ms | — |
| running 10 thousand tasks (`Yield` and exit) | 6.9–9.3 ms | 1.8–2.5 ms |
| `Yield` of two tasks | 84–113 ns | 35–60 ns |
| creating and finishing a task, stacks from the pool | 822–911 ns | 868–1,032 ns |

```sh
printf 'UnitRunner.Run System_devTasks proc slow quit\n' | tools/run64.sh
printf 'Async_devBench.All\n' | tools/run64.sh
```

**Limits.**

1. One operating-system thread, no preemption. A task yields the processor only at a switch point
   (section 1). Procedures with the wait check and yield points detect cancellation. A task in a
   module without the key `^` that calls no suspending procedures is not interrupted.
2. A yield point does not switch a task that owns a monitor, nor code outside a task.
3. If the platform did not allocate a stack, the task does not start, and `StartIn` traps 22 with
   the text `no stack memory (platform does not support it)`.
4. Operating-system worker threads (`AsyncWork`) do not access the heap or the module `Tasks`; the
   exception is jobs that allocate no memory themselves. See `Docu/Async.md`.
5. Without `Tasks` loaded, the kernel takes and releases free monitors, but waiting in them traps:
   nobody can wake the task.

## 10. Implementation Scheme

This section does not define behaviour; it describes how the behaviour is obtained. Details are in
`docs/implementation.md`, sections 2–4.

`Taskarch.Switch` is a `[code]` procedure inlined into the caller (module `Taskarch` has one file per target; `Tasks` is a single file). On amd64 it saves 13 registers
(`rax`, `rbx`, `rbp`, `rsi`, `rdi`, `r8`–`r15`) and `rsp`, on i386 five (`eax`, `ebx`, `ebp`,
`esi`, `edi`) and `esp`. The synthetic return address points to the continuation right after the
bytes of `Switch`; for a fresh task the return slot holds the address of `TaskBoot`. Around the raw
switch stand `Kernel.SwitchSave` and `Kernel.SwitchRestore`: they move the `Kernel.Try` context,
`trapStack` and `tryCount` into the stack record and restore them on return.

`Kernel.RegisterStack` enters a stack into the registry — a doubly linked list of records.
`MarkLocals` scans the current stack from the frame pointer, sleeping stacks from the saved stack
pointer. `SwitchSave` records the stack pointer with a slack of 256 bytes on amd64 (64 on i386), so
that the registers saved by the switcher fall into the scan. `Kernel.StackOf` finds a record by a
linear walk of the list. A switch does not call it: a task remembers its record, and `Step` looks up
its own once per step.

`TaskBoot` is the entry point of a new stack. It calls `Run` under `Kernel.Try`; a normal return
gives `Finished`, a trap gives `Failed(t)` and `Finished`, cancellation gives `Cancelled`. After the
body `TaskBoot` waits for its group to empty. Then `Finish` switches to the main stack, where `Step`
removes the task in `Reap`.

The scheduler keeps ready tasks in a FIFO queue and sleeping tasks in a 4-ary heap ordered by
deadline: the top of the heap is the nearest deadline. The pool of free stacks is a singly linked
list threaded through the top words of the stacks themselves (`TakeStack`, `GiveStack`); `TrimPool`
returns the surplus above `poolMax` = 1024 to the kernel in portions of up to `trimStep` = 16 stacks
per step. `Reap` unlinks the task from the doubly linked list of all tasks in O(1): otherwise
finished tasks would keep their objects until the end of the process. The module's `CLOSE` removes
the scheduler (`Kernel.SetScheduler(NIL)`), removes the action from `Services` and returns the stacks
of live tasks and of the pool to the kernel.
