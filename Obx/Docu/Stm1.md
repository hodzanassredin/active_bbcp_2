# Overview by Example: ObxStm1

Waiting for a condition over two variables: `AsyncStm.Retry`.

A buyer waits until his account holds 250 and the shop opens. The account and the "open" flag are
different objects; a monitor's `Await` condition works inside one object only and does not fit
here. The example shows how a transaction waits for a condition over two variables. The example
belongs to the guide `Docu/Async.md`, section "Transactions".

```
MODULE ObxStm1;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST price = 250;

	TYPE
		Buy = POINTER TO RECORD (AsyncStm.Tx) tries: INTEGER END;
		Buyer = POINTER TO RECORD (Tasks.Task) END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;
		Opener = POINTER TO RECORD (Tasks.Task) END;

	VAR
		money: AsyncStm.IntVar;	(* счёт покупателя *)
		open: AsyncStm.BoolVar;	(* магазин открыт *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (t: Buy) Do;
	BEGIN
		INC(t.tries);	(* обычное поле: откат его не трогает *)
		IF (money.Get() < price) OR ~open.Get() THEN AsyncStm.Retry END;
		money.Set(money.Get() - price)
	END Do;

	PROCEDURE (b: Buyer) Run;
		VAR t: Buy;
	BEGIN
		NEW(t); t.tries := 0;
		AsyncStm.Atomically(t);
		Say("bought, money = ", money.Get());
		Say("transaction ran times: ", t.tries)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10);
		money.Set(money.Get() + 200);
		Say("salary paid, money = ", money.Get())
	END Run;

	PROCEDURE (o: Opener) Run;
	BEGIN
		Tasks.Sleep(20);
		open.Set(TRUE);
		Console.WriteStr("shop opened"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR b: Buyer; p: Payroll; o: Opener;
	BEGIN
		money := AsyncStm.NewInt(100); open := AsyncStm.NewBool(FALSE);
		NEW(b); NEW(p); NEW(o);
		Tasks.Start(b); Tasks.Start(p); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm1.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Stm1.cp`). In the BlackBox
window a commander executes the command:

```
   ObxStm1.Do
```

In the console:

```sh
echo 'ObxStm1.Do' | tools/run64.sh
```

The log shows:

```
salary paid, money = 300
shop opened
bought, money = 50
transaction ran times: 3
```

## What happened

Command `Do` creates the account `money = 100` and the flag `open = FALSE` and starts three tasks.
The task `Buyer` executes the transaction `Buy`. Its method `Do` reads both variables; there is 100,
the shop is closed, and `Do` calls `AsyncStm.Retry`. `Retry` rolls the transaction back, and
`Atomically` puts the task to sleep until one of the variables read changes — `money` or `open`.

After 10 ms the task `Payroll` adds 200 to the account outside a transaction. `Set` wakes the buyer.
`Atomically` executes `Do` again: there is 300, but the shop is closed, and the task waits again.
After 20 ms the task `Opener` opens the shop; the third execution of `Do` withdraws 250, and the
transaction completes.

The counter `tries` is an ordinary field of the object, not a transactional variable, so rollback
does not touch it, and it shows that `Do` ran three times.

In this example we have seen how `Retry` waits for a condition over several variables and how a
change of any variable read wakes the waiting transaction.
