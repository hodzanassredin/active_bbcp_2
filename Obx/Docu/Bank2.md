# Overview by Example: ObxBank2

Two customers wait for money on an account: without a monitor and with a monitor.

Two customers want to withdraw 100 each from an empty account and wait until money arrives; before
handing out money the clerk checks the signature, and this is a wait. The example plays the naive
version, where a wait stands between the check and the withdrawal, and the version with an
exclusive procedure and `ACTIVE.AWAIT`. The example belongs to chapter 3 of the tutorial
`Docu/Tut-Active-3.md`.

```
MODULE ObxBank2;
	IMPORT ACTIVE, Tasks, Console, Strings;

	TYPE
		Buyer = POINTER TO RECORD (Tasks.Task) safe: BOOLEAN END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: INTEGER;

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
		WHILE balance < n DO Tasks.Sleep(1) END;
		Check;
		balance := balance - n
	END NaiveWithdraw;

	(* С монитором: проверка и списание в одной исключающей процедуре *)
	PROCEDURE [exclusive] Withdraw (n: INTEGER);
	BEGIN
		ACTIVE.AWAIT(balance >= n);
		balance := balance - n
	END Withdraw;

	PROCEDURE [exclusive] Deposit (n: INTEGER);
	BEGIN
		balance := balance + n
	END Deposit;

	PROCEDURE (b: Buyer) Run;
	BEGIN
		IF b.safe THEN Check; Withdraw(100) ELSE NaiveWithdraw(100) END;
		Say("paid 100, balance ", balance)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10); Deposit(150); Say("deposit 150, balance ", balance);
		Tasks.Sleep(30); Deposit(50); Say("deposit 50, balance ", balance)
	END Run;

	PROCEDURE Run (safe: BOOLEAN);
		VAR b1, b2: Buyer; p: Payroll;
	BEGIN
		balance := 0;
		NEW(b1); b1.safe := safe; NEW(b2); b2.safe := safe; NEW(p);
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", balance)
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

In the version with the monitor a customer first checks the signature, then calls the exclusive
procedure `Withdraw`. `ACTIVE.AWAIT(balance >= n)` releases the monitor of the module and puts the
customer to sleep while the condition is false. The deposit `Deposit` is an exclusive procedure
too; when it leaves the monitor, it rechecks the conditions of the waiting tasks. After the deposit
of 150 the first customer passes, the condition of the second is false again (50), and it waits for
the second deposit.

In this example we have seen why a wait between a check and a change breaks indivisibility, and how
an exclusive procedure with `AWAIT` makes the check and the change one step.
