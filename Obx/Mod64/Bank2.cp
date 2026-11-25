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
			WHILE a.balance < op.n DO
				IF ~a.AwaitCond(ctx, op.cond) THEN RETURN END
			END;
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
