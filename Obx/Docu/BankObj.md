# Overview by Example: ObxBankObj

An account on a monitor that lives in the base class: two customers wait for a balance.

Two customers want to withdraw 100 each from an empty account; the payroll deposits 150 and then
50. Before handing out money the clerk checks the signature, and this is a wait. The account is
an object; `Withdraw` and `Deposit` are operation records, and the condition is written as
`WHILE balance < amount DO account.Wait END`. The module uses no language extension. The same
scene in the old form, with an exclusive procedure and `ACTIVE.AWAIT`, is `ObxBank2`; the study
is `docs/object-form-study.md`.

```
MODULE ObxBankObj;
(** Счёт в банке на мониторе в базовом классе.

	Тот же сценарий, что ObxBank2: два покупателя ждут остатка на счёте,
	кассир проверяет подпись (ожидание), зарплата приходит двумя частями.
	Здесь монитор — объект Account: операции Withdraw и Deposit —
	записи-наследники Op, ожидание условия — WHILE balance < n DO Wait.
	Для сравнения со старым способом см. ObxBank2 ([exclusive] + AWAIT). *)

	IMPORT AsyncObject, Tasks, Console, Strings;

	TYPE
		WithdrawOp = RECORD (AsyncObject.Op)
			amount: INTEGER;
			ok: BOOLEAN
		END;
		DepositOp = RECORD (AsyncObject.Op)
			amount: INTEGER
		END;

		Account = POINTER TO RECORD (AsyncObject.Object)
			balance: INTEGER
		END;

		Buyer = POINTER TO RECORD (Tasks.Task) a: Account END;
		Payroll = POINTER TO RECORD (Tasks.Task) a: Account END;

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

	PROCEDURE (a: Account) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: WithdrawOp DO
			(* Проверка и списание — под одним монитором *)
			WHILE a.balance < op.amount DO a.Wait END;
			DEC(a.balance, op.amount); op.ok := TRUE
		| op: DepositOp DO
			INC(a.balance, op.amount)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE Withdraw (a: Account; n: INTEGER);
		VAR op: WithdrawOp;
	BEGIN
		op.amount := n; op.ok := FALSE; a.Do(op)
	END Withdraw;

	PROCEDURE Deposit (a: Account; n: INTEGER);
		VAR op: DepositOp;
	BEGIN
		op.amount := n; a.Do(op)
	END Deposit;

	PROCEDURE (b: Buyer) Run;
	BEGIN
		Check;
		Withdraw(b.a, 100);
		Say("paid 100, balance ", b.a.balance)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10); Deposit(p.a, 150);
		Say("deposit 150, balance ", p.a.balance);
		Tasks.Sleep(30); Deposit(p.a, 50);
		Say("deposit 50, balance ", p.a.balance)
	END Run;

	PROCEDURE Do*;
		VAR a: Account; b1, b2: Buyer; p: Payroll;
	BEGIN
		NEW(a); a.balance := 0;
		NEW(b1); b1.a := a; NEW(b2); b2.a := a; NEW(p); p.a := a;
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", a.balance)
	END Do;

END ObxBankObj.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/BankObj.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBankObj.Do
```

In the console:

```sh
echo 'ObxBankObj.Do' | tools/run64.sh
```

The log shows:

```
deposit 150, balance 150
paid 100, balance 50
deposit 50, balance 100
paid 100, balance 0
final balance 0
```

## What happened

1. Two buyers and the payroll start. A buyer sleeps for 5 ms (the signature check) and then calls
   `Withdraw(account, 100)`; the payroll sleeps for 10 ms and deposits 150.
2. `Withdraw` builds a `WithdrawOp` on its stack and calls `account.Do`. The monitor is entered,
   `Exclusive` sees `balance < amount` and calls `account.Wait`; the monitor is released and the
   buyer sleeps.
3. `Deposit` enters the same monitor and adds 150. Its exit wakes both buyers; they recheck the
   condition themselves.
4. One buyer takes 100. The other still sees too little and waits again; the second deposit of 50
   wakes it, and it takes the rest.
5. The final balance is zero; the output is the `with monitor` part of `ObxBank2`.

In this example we saw the same bank scene without a language extension: the monitor is a field
of the base class, an operation is a stack record, and the wait condition is a loop.
