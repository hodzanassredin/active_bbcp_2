[← Chapter 1](Tut-Active-1.md) · [Contents](Tut-Active-TOC.md) · [Chapter 3 →](Tut-Active-3.md)

# 2. The first task

## 2.1. Clerks and a manager

A bank has three accounts, 100 on each. Three clerks move money round a circle: the first from
account 0 to account 1, the second from 1 to 2, the third from 2 to 0; each makes five transfers of
their own sum. A transfer for which there is not enough money is refused by the clerk: an account
must not go below zero. The manager waits until all the clerks finish and checks the cash: the sum
over the accounts must stay 300. There is a clock on the wall — a task that works until it is
stopped; the manager stops it at the end of the day.

Each participant is a task. A task is described by an extension of the record `Tasks.Task` with a
method `Run`:

```
MODULE ObxBank1;
	IMPORT Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Clerk = POINTER TO RECORD (Tasks.Task) from, to, sum: INTEGER END;
		Clock = POINTER TO RECORD (Tasks.Task) END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; refused: INTEGER; clock: Clock;

	PROCEDURE Total (): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0; FOR i := 0 TO n - 1 DO s := s + balance[i] END;
		RETURN s
	END Total;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Перевод: проверка и обе записи без точки переключения между ними *)
	PROCEDURE Transfer (from, to, sum: INTEGER);
	BEGIN
		IF balance[from] >= sum THEN
			balance[from] := balance[from] - sum;
			balance[to] := balance[to] + sum
		ELSE
			INC(refused)
		END
	END Transfer;

	PROCEDURE (c: Clerk) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO Transfer(c.from, c.to, c.sum); Tasks.Yield END
	END Run;

	PROCEDURE (c: Clock) Run;
	BEGIN
		LOOP Tasks.Sleep(1000) END	(* «часы на стене»: работают, пока их не отменят *)
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR g: Tasks.Group; c: ARRAY n OF Clerk; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].from := i; c[i].to := (i + 1) MOD n; c[i].sum := 40 + 30 * i;
			Tasks.StartIn(c[i], g)
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]) END;
		Say("refused ", refused);
		Say("total ", Total());
		Tasks.Cancel(clock)
	END Run;

	PROCEDURE Do*;
		VAR m: Manager;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100; refused := 0;
		NEW(clock); Tasks.Start(clock);
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("clock state ", Tasks.State(clock))
	END Do;

END ObxBank1.
```

The command prints:

```
balance 200
balance 20
balance 80
refused 3
total 300
Tasks: cancelled
clock state 7
```

## 2.2. What happens here

`Tasks.Start(t)` allocates a stack for the task and puts it into the ready queue. The task itself
starts working when the scheduler reaches it. The command `Do` executes outside a task, so it
turns the scheduler itself: `Tasks.Step` is one step of it, and the loop `WHILE Tasks.Count() > 0`
runs while tasks are alive. In a BlackBox window this loop is not needed (chapter 1).

The manager creates a *group* (`Tasks.NewGroup`) and starts the clerks into it (`Tasks.StartIn`).
A group ties fates together: if a clerk traps, the other clerks of the group are cancelled, and the
manager itself does not finish while the group has live members. The rules of a group are
described in detail in section 4 of the guide `Docu/Tasks.md` and in the example `ObxTasks3`.

`Tasks.Join(c[i])` means "wait until task `c[i]` finishes". The manager falls asleep in `Join` and
wakes up when the clerk has left `Run`. While it sleeps, other tasks work.

`Tasks.Sleep(1000)` puts the clock to sleep for a second. A sleeping task does not use the
processor: the scheduler wakes it when the time comes. The clock sleeps in an endless loop, and
without intervention the loop in `Do` would never end. The manager calls `Tasks.Cancel(clock)`. A
cancellation is a request: it sets a flag on the task and wakes it; the task notices the flag in
its nearest wait and ends with trap 127. The line `Tasks: cancelled` is printed by this
cancellation, and the state of the clock becomes 7 (`Tasks.Cancelled`). A task that computes and
does not wait anywhere does not notice the cancellation until it reaches a wait.

## 2.3. Why a transfer needs no lock

Look at the procedure `Transfer`. It reads the balance, compares it with the sum, decreases one
account and increases another. Three clerks work "at the same time", and in an ordinary
multithreaded program this is dangerous: a thread can be interrupted between the check and the
withdrawal, another thread withdraws from the same account, and the account goes below zero. That
is why in Java or C# such a transfer is closed with a lock.

Here no lock is needed. One thread executes all the tasks, and nobody interrupts a task: it gives
up the processor itself and only at known places. These places are called *switch points*:

1. a call of `Tasks.Yield`, `Sleep`, `Park` or `Join`;
2. entry into a monitor held by another task, and `ACTIVE.AWAIT` with a false condition (chapter 3);
3. a wait in a channel, in a choice, in a single-assignment variable (chapters 5 and 6);
4. a yield point in a module compiled with option `^` (chapter 7).

Everything a task does between two switch points is seen by the other tasks as one indivisible
action. There are no switch points in `Transfer`, so the check and both writes happen "at once".
The result `total 300` and the absence of negative balances are not luck but a consequence of this
rule, for any number of runs.

The clerk calls `Tasks.Yield` after each transfer — it gives the turn to the other clerks. Without
it the first clerk would make all five transfers in a row; the result would stay correct, but the
clerks would work one after another, not interleaved.

## 2.4. How to break it

Move `Tasks.Yield` into the transfer — between the check and the withdrawal:

```
IF balance[from] >= sum THEN
	Tasks.Yield;	(* a switch point in the middle of a transfer *)
	balance[from] := balance[from] - sum;
```

Now between the check "there is enough money" and the withdrawal another clerk has time to take
their sum from the account, and the balance can become negative. The compiler will not notice
this. In practice `Yield` is rarely put into the middle of a transfer on purpose; more often the
wait hides inside a called procedure: writing to a log on disk, asking for confirmation, sending a
message. The next chapter is about this.

## 2.5. Cost

A task is cheap, but not free. The default stack is 256 KB of address space; physical memory is
allocated as it is touched, so thousands of tasks fit in memory without trouble. Creating and
finishing a task, when the stack is taken from the pool, costs 822–911 ns, a `Yield` switch
between two tasks costs 84–113 ns (Linux x86-64, one thread, a loaded machine;
`docs/measurements.md`, section 2). So a task is started for each conversation with a client or
each request, and not for each addition.

One thread executes all the tasks, so tasks do not speed up a computation on a multicore machine.
They make a program responsive and let you write a wait in plain text. How to compute on several
cores is told in chapter 7.
