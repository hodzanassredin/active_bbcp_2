# Tasks

A guide to module `Tasks`. It is for the programmer who writes in Component Pascal and wants one
program to do several things at once. The language stays plain Component Pascal: tasks and
monitors are library types `Task`, `Job` and `Object`. The monitor object and the pass are
described in more detail in `docs/monitor-object.md`. The examples of this guide are inline;
there is no separate example subsystem.

## 1. Why a task

A program serves a hundred clients. Each client sends a request and waits for the answer; the
server takes requests one at a time and answers them. Written as a single sequential procedure,
the program must itself remember where each client stopped: the programmer keeps a table of
states and writes a loop that walks it. The client's code — "send a request, wait, add up the
result" — is scattered across the branches of that loop.

A *task* gives such code back its natural form. A task is the sequential execution of a
procedure on its own stack. While a task waits, its local variables and its place in the code
stay on its stack, and the programmer need not save them. A hundred clients are a hundred tasks,
and each is written as straight text of three lines.

A task is declared by extending record `Tasks.Task` and implementing method `Run`;
`Tasks.Start` starts it. All tasks are executed by one operating system thread, and at any moment
at most one task executes. Nobody interrupts a task: it gives up the processor itself, and only
at known places. These places are called *switch points*:

1. a call of `Yield`, `Sleep`, `Park` or `Join`;
2. entering a monitor object held by another task;
3. waiting for a condition in `Await` or `AwaitCond`.

The main property of tasks follows from this. Two tasks increment a shared counter: each reads
it, adds one and writes it back. In a system with OS threads one increment is sometimes lost,
because a thread can be interrupted between the read and the write. Here there is no switch point
between the read and the write, and everything a task does between two switch points is seen by
other tasks as one indivisible action. That is why the counter needs no monitor:

```
MODULE TasksSample0;
	IMPORT Tasks, Console, Strings;

	TYPE Counter = POINTER TO RECORD (Tasks.Task) END;

	VAR count: INTEGER;

	PROCEDURE (c: Counter) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 1000 DO
			x := count; count := x + 1;	(* чтение и запись без точки переключения между ними *)
			IF i MOD 10 = 0 THEN Tasks.Yield END
		END
	END Run;

	PROCEDURE Do*;
		VAR a, b: Counter; s: ARRAY 16 OF CHAR;
	BEGIN
		count := 0;
		NEW(a); NEW(b); Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(count, s);
		Console.WriteStr("count = "); Console.WriteStr(s); Console.WriteLn
	END Do;

END TasksSample0.
```

Command `TasksSample0.Do` prints `count = 2000` on every run. The tasks alternate after every ten
increments, but no increment is torn apart.

The property holds as long as there is no switch point between the read and the write. Put
`Tasks.Yield` between `x := count` and `count := x + 1`, and the second task will read the old
value in time, and the result will be less than 2000. The compiler does not check this. When
shared state is changed across a wait, it is protected by a monitor: an exclusive procedure does
not admit a second task until the first has left.

The server with a hundred clients is built exactly so. The request queue is a monitor object:
a client enters it through `Do` and waits until the queue has room; the server takes a request
with the same `Do` and computes the square.

```
MODULE TasksSample2;
	IMPORT Tasks, Console, Strings;

	CONST clients = 100; cap = 8;

	TYPE
		Request = POINTER TO RECORD arg, res: INTEGER END;
		SubmitOp = RECORD (Tasks.Op) r: Request END;
		TakeOp = RECORD (Tasks.Op) r: Request END;
		Queue = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF Request; head, tail, count: INTEGER
		END;
		Client = POINTER TO RECORD (Tasks.Job) n: INTEGER; q: Queue END;
		Server = POINTER TO RECORD (Tasks.Job) q: Queue END;

	VAR sum: INTEGER;

	PROCEDURE (q: Queue) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF q.count < cap THEN INCL(s, 0) END;
		IF q.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (q: Queue) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: SubmitOp DO
			WHILE q.count = cap DO
				IF ~q.Await(ctx, {0}) THEN RETURN END
			END;
			q.ring[q.tail] := op.r;
			q.tail := (q.tail + 1) MOD cap; INC(q.count)
		| op: TakeOp DO
			WHILE q.count = 0 DO
				IF ~q.Await(ctx, {1}) THEN RETURN END
			END;
			op.r := q.ring[q.head];
			q.head := (q.head + 1) MOD cap; DEC(q.count)
		END
	END Exclusive;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Request; op: SubmitOp;
	BEGIN
		NEW(r); r.arg := c.n;
		op.r := r; c.q.Do(ctx, op)
	END Body;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: TakeOp;
	BEGIN
		FOR i := 1 TO clients DO
			s.q.Do(ctx, op);
			INC(sum, op.r.arg * op.r.arg)
		END
	END Body;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; q: Queue; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; NEW(q);
		NEW(s); s.q := q; Tasks.Start(s);
		FOR i := 1 TO clients DO NEW(c); c.n := i; c.q := q; Tasks.Start(c) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(sum, str);
		Console.WriteStr("sum = "); Console.WriteStr(str); Console.WriteLn
	END Do;

END TasksSample2.
```

The command prints `sum = 338350`, the sum of the squares from 1 to 100. The client's text stays
straight: put the request and return. Monitor `Queue` holds the queue; `Await` waits for the
flags "has room" and "has a request", and the FIFO order of the queues preserves the order of
requests.

Command `Do` executes outside a task and drives the scheduler itself by calling `Tasks.Step`
while tasks are alive. In the BlackBox window this is not needed: the scheduler is built into
the main loop (section 5).

## 2. The life of a task

`NEW` creates a task, and it is in state `New`. Until the task is started, it may be given a name
(`SetName`) and a stack size (`SetStackSize`). `Start` takes a stack for the task, puts it at the
end of the ready queue and makes it `Ready`. The scheduler takes the task from the head of the
queue, switches to its stack, and the task becomes `Running`.

A running task leaves this state in three ways. It may yield and queue up at once (`Yield`) — then
it is `Ready` again. It may wait: `Sleep` makes it `Sleeping`, `Join` makes it `Waiting`, `Park`
and waiting for a monitor make it `Parked`. Finally, the body `Run` may end: by a normal return, by
a trap or by cancellation. A waiting task is woken by the event it waits for, by `Unpark` or by
`Cancel`; the woken task enters the ready queue again.

There are eight states; the module exports their values, and `State(t)` returns the current one:

| From | To | What happens |
|---|---|---|
| `New` (0) | `Ready` (1) | `Start` or `StartIn` |
| `Ready` | `Running` (2) | the scheduler took the task from the queue |
| `Running` | `Ready` | `Yield`; `Sleep` with a time not above zero |
| `Running` | `Sleeping` (3) | `Sleep` with a positive time |
| `Running` | `Waiting` (4) | `Join` while the target is alive |
| `Running` | `Parked` (5) | `Park`; a busy monitor; waiting in `Await`; waiting for its group |
| `Sleeping` | `Ready` | the time is up; `Unpark`; `Cancel` |
| `Waiting` | `Ready` | the target ended; `Unpark`; `Cancel` |
| `Parked` | `Ready` | `Unpark`; `Cancel`; a monitor was handed to the task |
| `Running` | `Finished` (6) | body `Run` returned or trapped |
| `Running` | `Cancelled` (7) | the task was cancelled |

`Finished` and `Cancelled` are final states. A task whose body ended in a trap is also
`Finished`; `Failed(t)` tells it apart. Cancelling a task in state `New`, `Ready` or `Running` does
not change its state but only sets a flag: a running task notices it at its next switch point, a
task not yet started notices it when started, and then its body does not execute at all.

A finished task returns to the main stack, and the scheduler removes it: unregisters it from the
kernel's stack registry, gives the stack to the pool, wakes the tasks waiting for it in `Join` and
decrements the count of live tasks. `Count()` no longer counts the task after that. The task
object stays with the program, and `State`, `Failed` and `GetName` keep working on it.

## 3. Waiting

A task waits when it has nothing to do: a pause runs out, the result of another task is needed, a
signal is needed. The module has an operation for each of these reasons.

`Yield` waits for nothing. The task goes to the end of the ready queue, and the tasks that were
ready earlier get to execute. It is called in a long computation so as not to hold the
processor: without `Yield` a task that computes for a minute stops all other tasks and the
BlackBox window for a minute.

`Sleep(ms)` puts the task to sleep for at least `ms` milliseconds by the clock `Kernel.Time`. The
task wakes when the scheduler sees in one of its steps that the time is up, so a sleep may last
longer than ordered by the length of a step. `Sleep` with a non-positive time equals `Yield`.

`Park` and `Unpark` are a pair for signalling. A task calls `Park` and sleeps until another task
calls `Unpark` for it. A signal is not lost: if `Unpark` came before `Park`, the wake-up flag is
already set, and `Park` returns at once, clearing it. There is one flag, so two `Unpark` in a row
give one wake-up. `Join(t)` waits until task `t` ends or is cancelled; if it has already ended,
`Join` returns at once.

The example combines all three operations. The main task starts a sleeper and a waker and waits
for both:

```
MODULE TasksSample1;
	IMPORT Tasks, Console;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) who: Tasks.Task END;
		Main = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Console.WriteStr("sleeper: park"); Console.WriteLn;
		Tasks.Park;
		Console.WriteStr("sleeper: woken"); Console.WriteLn
	END Run;

	PROCEDURE (w: Waker) Run;
	BEGIN
		Tasks.Sleep(20);
		Console.WriteStr("waker: unpark"); Console.WriteLn;
		Tasks.Unpark(w.who)
	END Run;

	PROCEDURE (m: Main) Run;
		VAR s: Sleeper; w: Waker;
	BEGIN
		NEW(s); Tasks.Start(s);
		NEW(w); w.who := s; Tasks.Start(w);
		Tasks.Join(s); Tasks.Join(w);
		Console.WriteStr("main: both finished"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR m: Main;
	BEGIN
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END TasksSample1.
```

Output:

```
sleeper: park
waker: unpark
sleeper: woken
main: both finished
```

Wake-ups of different kinds do not mix. `Unpark` wakes a task in any wait, but `Join` and waiting
for a monitor treat such a wake-up as spurious and wait on, and the flag stays for the next
`Park`. That is why a stray `Unpark` neither brings a task into a monitor too early nor breaks a
`Join`. `Sleep` consumes the wake-up: woken by `Unpark` before its time, the task clears the flag,
and its next `Park` will wait.

Waiting is allowed only in a task. `Join` called from a command traps 20 and prints
`Tasks: wait outside a task` to the console: a command has no stack of its own and nowhere to give
the processor. That is why in the example the main task waits, while the command only starts it
and drives `Step`. `Join` on itself traps 23 — the task would wait for its own end forever — and
`Join` on a task not started traps 24.

## 4. Cancellation and groups

A task started three helpers, and one of them failed. The other two keep working on a result
nobody needs any more. The parent task finished `Run`, while the helpers are still alive and
hold resources the parent believes it released. One rule cures both troubles: children do not
outlive their parent, and the failure of one child cancels the others. This rule is called
structured concurrency.

Cancellation in this module is a request, not a blow. `Cancel(t)` sets a flag in the task and
wakes it if it sleeps or waits. The task notices the flag at its next switch point: every waiting
operation checks it before and after the wait and interrupts the task with trap 127. The trap
unwinds the stack the usual way, so the monitors the task held are released and its entries in
monitor queues are removed. The task becomes `Cancelled`. A task that computes without switch
points does not see the flag until it reaches one.

Cancelling one task does not touch others. To make cancellation travel down a tree, tasks are
joined into a *group*. `NewGroup` called in a task creates the task's own group (a repeated call
returns the same one; a task has one group), and `StartIn(t, g)` starts `t` as a member of group
`g`. The owner of a group is the task that created it. Four rules apply to a group:

1. An owner that returned from `Run` does not end while the group has live members: it becomes
   `Parked` and waits for the last one.
2. A trap in a member marks the group failed (`GroupFailed`) and cancels the other members.
3. A trap or cancellation of the owner cancels all members of its group. The owner does not wait
   for their end; each child ends at its next switch point.
4. Cancelling the owner while it waits for the group cancels the members, and the owner becomes
   `Cancelled`.

The example shows the second rule. The parent starts three children and leaves `Run`; the second
child traps after 10 ms, the two others sleep in an endless loop:

```
MODULE TasksSample3;
	IMPORT Tasks, Console, Strings;

	TYPE
		Child = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Task) g: Tasks.Group END;

	VAR kids: ARRAY 3 OF Child;

	PROCEDURE (c: Child) Run;
	BEGIN
		IF c.n = 1 THEN Tasks.Sleep(10); HALT(30) END;
		LOOP Tasks.Sleep(1000) END
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR i: INTEGER;
	BEGIN
		p.g := Tasks.NewGroup();
		FOR i := 0 TO 2 DO NEW(kids[i]); kids[i].n := i; Tasks.StartIn(kids[i], p.g) END
	END Run;

	PROCEDURE Do*;
		VAR p: Parent; i: INTEGER; s: ARRAY 8 OF CHAR;
	BEGIN
		NEW(p); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.GroupFailed(p.g) THEN Console.WriteStr("group failed") ELSE Console.WriteStr("group ok") END;
		Console.WriteLn;
		FOR i := 0 TO 2 DO
			Strings.IntToString(Tasks.State(kids[i]), s);
			Console.WriteStr("child state "); Console.WriteStr(s); Console.WriteLn
		END
	END Do;

END TasksSample3.
```

Output:

```
Tasks: cancelled
Tasks: cancelled
group failed
child state 7
child state 6
child state 7
```

Without the group the two sleeping children would live forever, and loop
`WHILE Tasks.Count() > 0` would never end. With the group the trap of the second child (state 6,
`Finished` with `Failed`) wakes its siblings, they catch trap 127 in `Sleep` and become `Cancelled`
(7); the lines `Tasks: cancelled` are printed by their cancellation. All this time the parent was
waiting for the group by rule 1 and ended last.

A trap in a task does not bring the program down. Every body `Run` executes under `Kernel.Try`: a
trap ends only this task, the others go on. So a failure is learned from `Failed(t)` and
`GroupFailed(g)`, not from BlackBox stopping.

## 5. The scheduler and the main loop

Tasks are executed by the *scheduler*, a `Services.Action` that the BlackBox main loop calls
between window events. Each call is one `Step`. A step wakes the sleepers whose time has come and
executes ready tasks in turn until the queue is empty or a quantum of 5 ms has passed, then
returns control to the main loop. So the window stays alive while tasks work. Console BlackBox
calls the same action between commands and while waiting for input.

The quantum is checked only when a task has given control back by itself. There is no
preemption: a task computing without switch points holds both the scheduler and the window until
it is done. Therefore a long computation is split by explicit `Yield` (or `Sleep`): the language
has no other switch points.

Where waiting is forbidden. An event handler, a broadcast over a document's domain, a menu
command are code of the BlackBox framework. That code expects to run to the end before the next
event starts, and all tasks see it as one action. If a task called `Sleep` inside such a handler,
the framework would be stopped halfway. Therefore waiting inside a `Services` action (other than
the scheduler's own) or inside a `Models.Domain` broadcast traps 21 with the text
`Tasks: wait inside an action handler or domain broadcast`. A task talks to the framework through
actions and the event loop: it did its work, posted an action, and waits again at home.

`Step` must not be called from a task (trap 20), nor again from inside a step (trap 21). Commands
and tests that want to wait for tasks without the main loop call `Step` in a loop, as the
examples of this guide do.

Waiting is also forbidden inside an *atomic section* — between `BeginAtomic` and the matching
`EndAtomic`. The section protects writes that no other task may see until it has ended. A wait
inside the section traps 25 with the text `Tasks: wait inside an atomic section`, `Step` traps 22.

## 6. Deadlock

Tasks wait for each other in a circle, or the only task waits for an `Unpark` nobody will send.
An OS thread in such a position would just hang. The scheduler sees it: there are live tasks, no
ready ones, and no sleeper waits for a time. Then a step prints once a log of "who waits for
what" and raises flag `Deadlocked()`:

```
MODULE TasksSample4;
	IMPORT Tasks;

	TYPE Lonely = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (l: Lonely) Run;
	BEGIN
		Tasks.Park	(* разбудить некому *)
	END Run;

	PROCEDURE Do*;
		VAR l: Lonely;
	BEGIN
		NEW(l); Tasks.SetName(l, "lonely"); Tasks.Start(l);
		Tasks.Step;
		IF Tasks.Deadlocked() THEN Tasks.Cancel(l); Tasks.Step END
	END Do;

END TasksSample4.
```

Output:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Tasks: cancelled
```

The log is a diagnosis, not a crash. The program can get out of the deadlock: `Unpark` or `Cancel` make a task ready again, and the
flag is cleared. In the example the command cancels the lonely task, and it ends with trap 127.

The scheduler detects only a complete deadlock. If even one task sleeps with a time or spins in a
loop with `Yield`, some tasks may stand forever and no log appears. A task that never yields is
not detected either: the scheduler simply does not get control.

A wait for an event from outside the tasks is not a deadlock: a timer, a ready descriptor, a
signal from another process. A task woken by such events waits in `ParkExternal`, not in
`Park`. While even one task waits in `ParkExternal`, the scheduler declares no deadlock and prints
no log. A primitive of one's own that is woken from outside the tasks must wait in `ParkExternal`: in `Park`
the scheduler prints the log although the wake-up will come. The opposite mistake is worse:
`ParkExternal` where nobody will wake the task hides a real deadlock as long as at least one such
task waits. Neither the compiler nor `Tasks` can tell the two cases apart.

## 7. Cost

A task's stack is 256 KB by default; `SetStackSize` sets another size before the start. The
kernel allocates the stack with `mmap` and closes its lowest page: an overflow hits the guard page
and traps instead of spoiling neighbouring memory. Physical pages are allocated on touch, so 10
thousand tasks with 64 KB stacks take 640 MB of address space and tens of megabytes of memory. The
size limit is 64 MB: the kernel does not allocate a larger stack, and `StartIn` traps 22, leaving
the task in state `New`. A program gets the same trap and text
`no stack memory (platform does not support it)` when the platform gave no memory.

Creating a task the first time is expensive: a system call per stack and, most often, a garbage
collection that scans all live stacks. In the measurements of `docs/measurements.md` (section 2;
Linux x86-64, one thread, three runs at load average 15.2–15.9) 10 thousand tasks with 64 KB
stacks are created in 60–70 ms, while Go with `GOMAXPROCS=1` does it in 18.6–20.2 ms. Therefore
the stack of a finished task is not returned to the platform but goes to a pool, and the next
task of the same size takes it without a system call. The same 10 thousand tasks with stacks
from the pool are created in 2.3–3.1 ms, and creating and finishing one task costs 822–911 ns —
as much as in Go (868–1,032 ns). The pool keeps up to 1024 free stacks; the scheduler returns the
surplus to the kernel little by little, no more than 16 stacks per step, so that a mass end of
tasks does not cost a mass of system calls at once.

`System_devBench` gives the cost of one `Yield` switch — 29–30 ns — and of restarting a task —
196–207 ns against 507–523 ns when each run does a `NEW`. Garbage collection with standing tasks:
9–11 ms at 10 thousand, 72–77 ms at 50 thousand, 202–223 ms at 100 thousand
(`System_devGcStacks`). Creating 10 thousand tasks with 64 KB stacks takes 52 ms on fresh stacks
and 4.3 ms on stacks from the pool (`System_devTasks.TestSlowLoadTenThousand`).

```sh
printf 'UnitRunner.Run System_devTasks slow noquit\n' | tools/run64.sh
tools/iso.sh sh -c "echo 'System_devBench.Yield' | tools/run64.sh"
```

The limits of the model follow from the single thread: tasks give no parallel work on several
cores.

## 8. Interface

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

	PROCEDURE BeginAtomic;
	PROCEDURE Cancel (t: Task);
	PROCEDURE Count (): INTEGER;
	PROCEDURE Current (): Task;
	PROCEDURE Deadlocked (): BOOLEAN;
	PROCEDURE EndAtomic;
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
	PROCEDURE ParkExternal;
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

Besides classic tasks, the module exports the monitor object and the pass: `Ctx` (`LIMITED
RECORD`), `Job` with abstract `Body-` and ready `Run`, `Object` with `Do`, `Await`, `AwaitCond`,
`Ready`, `HoldsWhileWaiting` and `TransferOnRelease`, `Op`, `Cond`, operations `Spawn`,
`JoinAll`, `ParkCtx`, `SetDeadline`, diagnostics `Held`, `Depth`, `Queued`. Their signatures,
rules and examples are in `docs/monitor-object.md`.

Before every trap the module writes a line `Tasks: <text>` to the console. The operations that
suspend a task (`Yield`, `Sleep`, `Join`, `Park`) begin with one check; below it is called "the
wait check":

```
Pre: called from a task — 20
Pre: the task is not cancelled — 127
Pre: no Services action (other than the scheduler) and no domain broadcast is running — 21
Pre: no atomic section (BeginAtomic) is open — 25
```

Entry into a busy monitor object and waiting in `Await` pass the same check.

`Sleep`, `Join` and `Park` repeat the check after waking, so cancelling a sleeping or waiting task
interrupts it with trap 127.

**CONST New, Ready, Running, Sleeping, Waiting, Parked, Finished, Cancelled**
Task states (section 2).

**TYPE Task**
ABSTRACT
A task. The program extends the record and implements `Run`.

**PROCEDURE (t: Task) Run**
NEW, ABSTRACT
The task body. Executes on the task's stack under `Kernel.Try`.

**TYPE Group**
A group of tasks (section 4). Its state is read by `GroupCount` and `GroupFailed`.

**PROCEDURE StartIn (t: Task; g: Group)**
Allocates a stack for the task and puts it at the end of the ready queue; with `g # NIL` enters
it into group `g`.

```
Pre: t # NIL — 20
Pre: State(t) = New — 21
Pre: a stack was allocated (size at most 64 MB, the platform gave memory) — 22
Post: State(t) = Ready; with g # NIL GroupCount(g) increased by 1
```

**PROCEDURE Start (t: Task)**
`StartIn(t, NIL)`.

**PROCEDURE Yield**
Puts the current task at the end of the ready queue and gives up the processor.

```
Pre: the wait check
Post: the tasks ready earlier have executed
```

**PROCEDURE Sleep (ms: LONGINT)**
Puts the task to sleep for at least `ms` ms; with `ms <= 0` equals `Yield`.

```
Pre: the wait check, before and after the sleep
Post: the time is up or Unpark came; the wake-up flag is cleared
```

**PROCEDURE Join (t: Task)**
Waits for the end of task `t`.

```
Pre: the wait check, before and after every wake-up
Pre: t # NIL — 22
Pre: t # Current() — 23
Pre: State(t) # New — 24
Post: State(t) IN {Finished, Cancelled}; the wake-up flag is untouched
```

**PROCEDURE Park**
Puts the task to sleep until `Unpark`; if the wake-up flag is already set, returns at once.

```
Pre: the wait check, before and after the sleep
Post: the wake-up flag is cleared
```

**PROCEDURE ParkExternal**
The same as `Park`, but for a wait for an event from outside the tasks (section 6): while a task waits
this way, the scheduler declares no deadlock. `Unpark` wakes it as usual.

```
Pre: the wait check, before and after the sleep
Post: the wake-up flag is cleared
```

**PROCEDURE Unpark (t: Task)**
Sets the wake-up flag and wakes `t` if it sleeps or waits.

```
Pre: t # NIL — 20
Post: t is ready, or its next Park returns at once
```

**PROCEDURE Cancel (t: Task)**
Marks an unfinished task cancelled and wakes it if it sleeps or waits.

```
Pre: t # NIL — 20
Post: t is marked; at its next switch point it gets trap 127
```

**PROCEDURE NewGroup (): Group**
In a task, the task's own group (created on the first call); outside a task, a new group without
an owner.

**PROCEDURE GroupCount (g: Group): INTEGER**
The number of live members; 0 when `g = NIL`.

**PROCEDURE GroupFailed (g: Group): BOOLEAN**
`TRUE` if at least one member trapped; `FALSE` when `g = NIL`.

**PROCEDURE OwnGroup (t: Task): Group**
The own group of `t`; `NIL` if it has none or `t = NIL`.

**PROCEDURE Step**
One scheduler step: wakes sleepers, executes ready tasks for at most 5 ms.

```
Pre: called outside a task — 20
Pre: Step is not executing now — 21
Pre: no atomic section is open — 22
```

**PROCEDURE NextDeadline (): LONGINT**
The nearest time of a sleeping task by `Kernel.Time`; −1 if no task sleeps.

**PROCEDURE Current (): Task**
The running task; `NIL` outside a task.

**PROCEDURE InTask (): BOOLEAN**
`Current() # NIL`.

**PROCEDURE State (t: Task): INTEGER**
The state of the task; `New` when `t = NIL`.

**PROCEDURE Failed (t: Task): BOOLEAN**
`TRUE` if the body of `t` ended in a trap; `FALSE` when `t = NIL`.

**PROCEDURE GetName (t: Task; OUT s: ARRAY OF CHAR)**
The name of the task; the empty string when `t = NIL`.

**PROCEDURE SetName (t: Task; IN s: ARRAY OF CHAR)**
Sets a name of up to 63 characters; does nothing when `t = NIL`. The length of `s` is not
checked: a longer name traps when copied.

**PROCEDURE SetStackSize (t: Task; size: INTEGER)**
The stack size in bytes; 0 means 256 KB.

```
Pre: t # NIL — 20
Pre: State(t) = New — 21
Pre: size >= 0 — 22
```

The upper limit is not checked here; a stack above 64 MB is rejected by `StartIn`.

**PROCEDURE Count (): INTEGER**
The number of tasks started and not yet removed.

**PROCEDURE BeginAtomic**
Opens an atomic section; sections nest. A section inside which the task ended with a trap ends
together with the task.

```
Post: until the matching EndAtomic the wait check traps 25, Step traps 22
```

**PROCEDURE EndAtomic**
Closes an atomic section.

```
Pre: a section is open — 20
```

**PROCEDURE Deadlocked (): BOOLEAN**
`TRUE` if the last step found a deadlock and printed the log (section 6).

**PROCEDURE Switches (): LONGINT**
The number of stack switches since the last `ResetStats`.

**PROCEDURE ResetStats**
Resets the `Switches` counter.

**PROCEDURE ListSize (): INTEGER**
The length of the list of live tasks.

**PROCEDURE List**
Prints a line per task to the console: name, state, stack bounds, sleep time.

The inside of the module — switcher `Taskarch.Switch`, the stack registry, the sleepers' heap, the
pool — is described in `docs/implementation.md`.
