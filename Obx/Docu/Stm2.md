# Overview by Example: ObxStm2

A choice between two transactions: `AsyncStm.OrElse`.

A customer pays with the first card, and if it does not hold enough money, with the second. Each
payment is a ready transaction `Pay` that waits in `Retry` while money is short. The example shows
how `OrElse` composes two such transactions into one without looking inside them. The example
belongs to the guide `Docu/Async.md`, section "Transactions".

```
MODULE ObxStm2;
	IMPORT Tasks, AsyncStm, Console, Strings;

	TYPE
		Pay = POINTER TO RECORD (AsyncStm.Tx) card: AsyncStm.IntVar; n: INTEGER END;
		Either = POINTER TO RECORD (AsyncStm.Tx) first, second: Pay END;
		Customer = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Bank = POINTER TO RECORD (Tasks.Task) END;

	VAR card1, card2: AsyncStm.IntVar;

	PROCEDURE Show (IN what: ARRAY OF CHAR);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(what);
		Console.WriteStr(": card1 = "); Strings.IntToString(card1.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", card2 = "); Strings.IntToString(card2.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE (t: Pay) Do;
	BEGIN
		IF t.card.Get() < t.n THEN AsyncStm.Retry END;
		t.card.Set(t.card.Get() - t.n)
	END Do;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.first, t.second)
	END Do;

	PROCEDURE (c: Customer) Run;
		VAR t: Either;
	BEGIN
		NEW(t); NEW(t.first); NEW(t.second);
		t.first.card := card1; t.first.n := c.n;
		t.second.card := card2; t.second.n := c.n;
		AsyncStm.Atomically(t);
		Show("paid")
	END Run;

	PROCEDURE (b: Bank) Run;
	BEGIN
		Tasks.Sleep(10);
		card1.Set(card1.Get() + 100);
		Show("deposit")
	END Run;

	PROCEDURE Do*;
		VAR c: Customer; b: Bank;
	BEGIN
		card1 := AsyncStm.NewInt(10); card2 := AsyncStm.NewInt(80);
		NEW(c); c.n := 50; Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		NEW(c); c.n := 50; Tasks.Start(c);
		NEW(b); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm2.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Stm2.cp`). In the BlackBox
window a commander executes the command:

```
   ObxStm2.Do
```

In the console:

```sh
echo 'ObxStm2.Do' | tools/run64.sh
```

The log shows:

```
paid: card1 = 10, card2 = 30
deposit: card1 = 110, card2 = 30
paid: card1 = 60, card2 = 30
```

## What happened

The first card holds 10, the second 80. The first customer pays 50. `OrElse` executes the first
branch: money is short, the branch calls `Retry`. `OrElse` rolls back the writes of the first branch
and executes the second; it withdraws 50 from the second card, and the transaction completes:
`card2 = 30`.

The second customer also pays 50. Now both cards are short: both branches call `Retry`, and the
whole transaction waits until any variable read in any branch changes. After 10 ms the task `Bank`
deposits 100 to the first card. The customer wakes up, the first branch finds 110 and withdraws 50.

In this example we have seen how `OrElse` picks the first of two transactions that does not wait,
and how the waiting of `OrElse` covers the variables of both branches.
