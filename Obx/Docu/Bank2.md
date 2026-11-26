# Overview by Example: ObxBank2

Two customers wait for money on an account: without a monitor and with a monitor.

Two customers want to withdraw 100 each from an empty account and wait until money arrives; before
handing out money the clerk checks the signature, and this is a wait. The example plays the naive
version, where a wait stands between the check and the withdrawal, and the version with an
the monitor-object operation and `AwaitCond`. The example belongs to the guide
`Docu/Tasks.md`.

```
MODULE ObxBank2;
	IMPORT Tasks, Console, Strings;

	TYPE
		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;
		Enough = POINTER TO RECORD (Tasks.Cond) n: INTEGER END;

		WithdrawOp = RECORD (Tasks.Op) n: INTEGER; cond: Enough END;
		DepositOp = RECORD (Tasks.Op) n: INTEGER END;

		Buyer = POINTER TO RECORD (Tasks.Job) acc: Account; safe: BOOLEAN END;
		Payroll = POINTER TO RECORD (Tasks.Job) acc: Account END;

	VAR acc: Account;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Check;
	BEGIN
		Tasks.Sleep(5)	(* кассир сверяет подпись: это ожидание *)
	END Check;

	(* Без монитора: между проверкой и списанием стоит ожидание *)
	PROCEDURE NaiveWithdraw (n: INTEGER);
	BEGIN
		WHILE acc.balance < n DO Tasks.Sleep(1) END;
		Check;
		acc.balance := acc.balance - n
	END NaiveWithdraw;

	PROCEDURE (e: Enough) Ready (o: Tasks.Object): BOOLEAN;
		VAR a: Account;
	BEGIN
		a := o(Account);
		RETURN a.balance >= e.n
	END Ready;

	(* С монитором: проверка и списание в одной исключающей операции *)
	PROCEDURE (a: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: WithdrawOp DO
			op.cond.n := op.n;
			IF ~a.AwaitCond(ctx, op.cond) THEN RETURN END;
			DEC(a.balance, op.n)
		| op: DepositOp DO
			INC(a.balance, op.n)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (b: Buyer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WithdrawOp; cond: Enough;
	BEGIN
		IF b.safe THEN
			NEW(cond); op.n := 100; op.cond := cond;
			b.acc.Do(ctx, op)
		ELSE
			NaiveWithdraw(100)
		END;
		Say("paid 100, balance ", acc.balance)
	END Body;

	PROCEDURE (p: Payroll) Body- (VAR ctx: Tasks.Ctx);
		VAR op: DepositOp;
	BEGIN
		Tasks.Sleep(10); op.n := 150; p.acc.Do(ctx, op); Say("deposit 150, balance ", acc.balance);
		Tasks.Sleep(30); op.n := 50; p.acc.Do(ctx, op); Say("deposit 50, balance ", acc.balance)
	END Body;

	PROCEDURE Run (safe: BOOLEAN);
		VAR b1, b2: Buyer; p: Payroll;
	BEGIN
		NEW(acc);
		NEW(b1); b1.acc := acc; b1.safe := safe;
		NEW(b2); b2.acc := acc; b2.safe := safe; NEW(p); p.acc := acc;
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", acc.balance)
	END Run;

	PROCEDURE Do*;
	BEGIN
		Console.WriteStr("without monitor:"); Console.WriteLn; Run(FALSE);
		Console.WriteStr("with monitor:"); Console.WriteLn; Run(TRUE)
	END Do;

END ObxBank2.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank2.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBank2.Do
```

In the console:

```sh
echo 'ObxBank2.Do' | tools/run64.sh
```

The log shows:

```
without monitor:
deposit 150, balance 150
paid 100, balance 50
paid 100, balance -50
deposit 50, balance 0
final balance 0
with monitor:
deposit 150, balance 150
paid 100, balance 50
deposit 50, balance 100
paid 100, balance 0
final balance 0
```

## What happened

In the naive version the customers poll the balance with `Tasks.Sleep(1)`. After 10 ms 150 arrives.
Both see that there is enough money, and both go to check the signature in `Check`. The check is a
switch point, and while the first sleeps in it, the second also passes the check. Both withdraw 100,
the balance is −50.

In the version with the monitor a customer first checks the signature, then calls the operation
`WithdrawOp` on the account monitor-object. `AwaitCond` with the `Enough` condition releases the
monitor and puts the customer to sleep while the condition is false. The deposit `DepositOp` is an
operation of the same monitor too; when it leaves the monitor, the waiters recheck their conditions. After the deposit
of 150 the first customer passes, the condition of the second is false again (50), and it waits for
the second deposit.

In this example we have seen why a wait between a check and a change breaks indivisibility, and how
one monitor-object operation with `AwaitCond` makes the check and the change one step.
