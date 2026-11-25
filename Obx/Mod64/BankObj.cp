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
