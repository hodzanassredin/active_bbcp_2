[← Chapter 3](Tut-Active-3.md) · [Contents](Tut-Active-TOC.md) · [Chapter 5 →](Tut-Active-5.md)

# 4. Transactions

## 4.1. Money that disappeared

A clerk transfers 50 from account 0 to account 2. He has already withdrawn the money from account 0
and then finds that account 2 is frozen: nothing may be deposited to it. The program traps. The
clerk's task dies, and the 50 is withdrawn and deposited nowhere. The sum over the accounts has
decreased — the first rule of the bank is broken.

The monitor of chapter 3 is powerless here: it keeps other tasks out, and the trouble happened in
the task's own code. One could, of course, move the check before the withdrawal. In this
transfer one could. But in a real program there is a lot of code between the withdrawal and the
deposit, and a trap can happen in any line of it: a division by zero, an array index out of range,
`NIL`.

We need a way to say: "these changes are either all made or none". In databases this is called a
*transaction*. In this branch the module `AsyncStm` gives transactions for data in memory (STM —
software transactional memory).

## 4.2. How a transaction is written

A transaction is an object, an extension of `AsyncStm.Tx` with a method `Do`. The data that a
transaction can roll back lie in *transactional variables*: `AsyncStm.IntVar`, `BoolVar`,
`RealVar`, `RefVar`. A value is read with the method `Get` and written with `Set`. A call
`AsyncStm.Atomically(t)` executes `t.Do`. The first write to a variable stores its previous value
in a journal; if `Do` traps, `Atomically` returns all the variables to their previous values and
passes the trap on with the same number.

The accounts of `ObxBank4` are three `IntVar`, and the frozen flag is a `BoolVar`:

```
MODULE ObxBank4;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST n = 3;

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx) from, to, sum: INTEGER END;
		Either = POINTER TO RECORD (AsyncStm.Tx) first, second: Transfer END;
		Clerk = POINTER TO RECORD (Tasks.Task) END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;

	VAR
		balance: ARRAY n OF AsyncStm.IntVar;
		frozen: AsyncStm.BoolVar;	(* счёт 2 арестован: зачислять на него нельзя *)

	PROCEDURE Show (IN what: ARRAY OF CHAR);
		VAR i, sum: INTEGER; s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(what); Console.WriteStr(":"); sum := 0;
		FOR i := 0 TO n - 1 DO
			Strings.IntToString(balance[i].Get(), s); Console.WriteStr(" "); Console.WriteStr(s);
			sum := sum + balance[i].Get()
		END;
		Strings.IntToString(sum, s); Console.WriteStr(", total "); Console.WriteStr(s); Console.WriteLn
	END Show;

	PROCEDURE (t: Transfer) Do;
		VAR from, to: AsyncStm.IntVar;
	BEGIN
		from := balance[t.from]; to := balance[t.to];
		IF from.Get() < t.sum THEN AsyncStm.Retry END;	(* ждать денег *)
		from.Set(from.Get() - t.sum);
		IF (t.to = 2) & frozen.Get() THEN HALT(30) END;	(* трап посреди перевода *)
		to.Set(to.Get() + t.sum)
	END Do;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.first, t.second)	(* с первого счёта, а нет — со второго *)
	END Do;

	PROCEDURE NewTransfer (from, to, sum: INTEGER): Transfer;
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := from; t.to := to; t.sum := sum;
		RETURN t
	END NewTransfer;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		AsyncStm.Atomically(NewTransfer(0, 2, 50))
	END Run;

	PROCEDURE (b: Buyer) Run;
		VAR t: Either;
	BEGIN
		NEW(t); t.first := NewTransfer(1, 0, 120); t.second := NewTransfer(2, 0, 120);
		AsyncStm.Atomically(t);
		Show("buyer paid 120")
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		balance[2].Set(balance[2].Get() + 100);
		Show("salary 100 to account 2")
	END Run;

	PROCEDURE Do*;
		VAR c: Clerk; b: Buyer; p: Payroll;
	BEGIN
		balance[0] := AsyncStm.NewInt(100); balance[1] := AsyncStm.NewInt(100);
		balance[2] := AsyncStm.NewInt(50); frozen := AsyncStm.NewBool(TRUE);
		Show("start");
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Show("clerk trapped") END;
		NEW(b); NEW(p); Tasks.Start(b); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxBank4.
```

The command prints:

```
start: 100 100 50, total 250
clerk trapped: 100 100 50, total 250
salary 100 to account 2: 100 100 150, total 350
buyer paid 120: 220 100 30, total 350
```

The second line shows the rollback. The clerk withdrew 50 from account 0 and trapped on `HALT(30)`;
the task died (`Tasks.Failed(c)` is true), but account 0 is 100 again, and the sum is the same.

## 4.3. Retry: wait for money

In chapter 3 the wait for money was written `ACTIVE.AWAIT(balance >= n)` inside a monitor. In a
transaction the same is written `AsyncStm.Retry`: "it is too early to go on". `Retry` rolls back
everything the transaction has done and puts the task to sleep until one of the variables the
transaction has read changes. Then `Do` is executed again, from the start.

The difference from `AWAIT` is that the condition can cover any variables, not only the data of one
monitor. The transaction itself remembers what it has read; the programmer does not declare whom to
wait for.

## 4.4. OrElse: from the first account, or else from the second

A customer wants to pay 120: from account 1, or, if there is not enough, from account 2. Each
attempt is a separate transaction `Transfer`, and both can `Retry`. `AsyncStm.OrElse(first, second)`
executes the first; if it did a `Retry`, it rolls back its writes and executes the second. If both
did `Retry`, the whole transaction waits — until a variable read in either branch changes.

In the example account 1 has 100, account 2 has 50, both branches do `Retry`, and the customer
falls asleep. The task `Payroll` deposits 100 to account 2 by an ordinary write outside a
transaction; the write wakes the customer. He tries again: the first branch does `Retry` again, the
second passes. The customer paid from account 2, and 30 is left on it.

Transactions compose. An `Atomically` called inside another transaction becomes a part of it: two
transfers in one `Do` are executed as a whole, and a trap in the second rolls back the first as
well. With monitors this does not work: two exclusive procedures called one after another are two
separate steps.

## 4.5. What is not allowed inside a transaction

Inside `Do` you cannot wait: `Tasks.Sleep`, `Yield`, a wait in a channel, entry into a busy
monitor. The reason is how indivisibility is built. A transaction is indivisible because there are
no switch points inside it; if the task fell asleep in the middle of `Do`, the others would see
half a transfer. So a wait inside a transaction traps with 25 and the text
`Tasks: wait inside an atomic section`, and the writes are rolled back. The only way to wait is
`Retry`.

The second rule: only transactional variables are rolled back. Output to the console, a write to a
file, sending a message, ordinary fields of objects stay done, and with `Retry` they are repeated
at each new execution of `Do`. So in a transaction only transactional variables are read and
written, and external actions are done after `Atomically`, on the result. In the example `Show`
stands after `Atomically`, not inside `Do`. The compiler does not check this. The other violations
and their numbers are in section 8 of the guide `Docu/Async.md`.

## 4.6. Monitor or transaction

| Situation | Take |
|---|---|
| The data of one object, short operations | monitor |
| A wait in the middle of work (a check, an answer, input) with the data held | monitor; a transaction cannot wait |
| Changing several objects at once, and a trap must not leave half of it | transaction |
| Waiting for a condition over several objects | transaction with `Retry` |
| "Do this, and if it cannot be done, do the other" | transaction with `OrElse` |
| Every nanosecond counts | ordinary fields between switch points (chapter 2) |

Cost. A transfer of "two reads, two writes" costs 76–82 ns as a transaction, 62–72 ns as an
exclusive procedure, and at most 1 ns as a direct write to fields (AMD64, one thread, a million
transfers, a loaded machine; `docs/measurements.md`, section 11). A transaction is more expensive
than a monitor by about 10 ns; it is taken for the rollback and for waiting on several objects, not
for speed.

Limit. A transaction is indivisible only inside one BlackBox process: it is held by one thread
without preemption. If the accounts lie in different processes or on different machines, there is
no transaction between them; how money is transferred there is told in chapter 8.
