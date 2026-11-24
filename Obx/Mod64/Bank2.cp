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
