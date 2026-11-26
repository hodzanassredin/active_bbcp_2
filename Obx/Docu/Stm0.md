# Overview by Example: ObxStm0

A transfer between accounts: a trap between "withdraw" and "deposit" loses no money.

A transfer is two actions: withdraw money from one account and deposit it into another. There is no
switch point between them, and other tasks do not see half a transfer. But a trap can happen between
them, and then the withdrawn money would be lost. The example shows how the transaction
`AsyncStm.Atomically` rolls back the withdrawal. The example belongs to the guide `Docu/Async.md`,
section "Transactions".

```
MODULE ObxStm0;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST limit = 150;	(* больше этого счёт b не принимает *)

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER
		END;
		Clerk = POINTER TO RECORD (Tasks.Job) END;

	VAR a, b: AsyncStm.IntVar;

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);	(* снять *)
		ASSERT(t.to.Get() + t.n <= limit, 30);	(* трап между «снять» и «зачислить» *)
		t.to.Set(t.to.Get() + t.n)	(* зачислить *)
	END Do;

	PROCEDURE Move (n: INTEGER);
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := a; t.to := b; t.n := n;
		AsyncStm.Atomically(t)
	END Move;

	PROCEDURE (c: Clerk) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Move(100); Move(100)
	END Body;

	PROCEDURE Show;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr("a = "); Strings.IntToString(a.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", b = "); Strings.IntToString(b.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", sum = "); Strings.IntToString(a.Get() + b.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR c: Clerk;
	BEGIN
		a := AsyncStm.NewInt(300); b := AsyncStm.NewInt(0);
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Console.WriteStr("clerk trapped") ELSE Console.WriteStr("clerk done") END;
		Console.WriteLn;
		Show
	END Do;

END ObxStm0.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Stm0.cp`). In the BlackBox
window a commander executes the command:

```
   ObxStm0.Do
```

In the console:

```sh
echo 'ObxStm0.Do' | tools/run64.sh
```

The log shows:

```
clerk trapped
a = 200, b = 100, sum = 300
```

## What happened

Command `Do` creates two accounts — transactional variables `a = 300` and `b = 0` — and starts the
task `Clerk`. The task makes two transfers of 100. Each transfer is a `Transfer` object; its method
`Do` withdraws, checks the limit of account `b` and deposits; `AsyncStm.Atomically` executes `Do`
as one transaction.

The first transfer succeeds: `a = 200`, `b = 100`. The second withdraws another 100 from `a` and
hits the limit: `b + 100 = 200` is greater than 150, and `ASSERT` traps with code 30. At this moment
`a` already equals 100. Before the first write to `a` the method `Set` put the old value into the
undo log of the transaction; having caught the trap, `Atomically` restores `a = 200` from the log and
passes trap 30 on. The task ends with a trap, `Tasks.Failed(c)` is true, and the command prints
`clerk trapped`.

The sum stays 300: the second transfer is not made at all. Without `Atomically` the method `Do`
would be called directly, and after the trap there would be `a = 100`, `b = 100`, sum 200.

In this example we have seen how to describe a transaction as an object with a method `Do`, how to
create transactional variables, and how a trap inside a transaction rolls back its writes and
reaches the caller with the same number.
