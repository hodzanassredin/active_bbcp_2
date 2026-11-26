MODULE ObxBankCtx;
(** Счёт на мониторе-объекте: перевод повторным входом.

	Перевод — операция Transfer над счётом A. Её Exclusive уже идёт под
	монитором A и зовёт A.Do со списанием: это повторный вход, глубина
	становится 2. Затем под тем же внешним монитором берётся монитор B.
	Снятие ждёт остатка, как обычная операция; на глубине 1 монитор
	отпускается при выходе. *)

	IMPORT Tasks, Console, Strings;

	TYPE
		WithdrawOp = RECORD (Tasks.Op) amount: INTEGER; ok: BOOLEAN END;
		DepositOp = RECORD (Tasks.Op) amount: INTEGER END;
		TransferOp = RECORD (Tasks.Op) to: Account; amount: INTEGER; ok: BOOLEAN END;

		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;

		Buyer = POINTER TO RECORD (Tasks.Job) a: Account; amount: INTEGER END;
		Payroll = POINTER TO RECORD (Tasks.Job) a: Account; first, second: INTEGER END;
		Transfer = POINTER TO RECORD (Tasks.Job) from, to: Account; amount: INTEGER END;

	VAR
		transferDone: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (a: Account) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF a.balance > 0 THEN INCL(s, 0) END;
		RETURN s
	END Ready;

	PROCEDURE (a: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR w: WithdrawOp; d: DepositOp;
	BEGIN
		WITH op: WithdrawOp DO
			(* Цикл нужен: флаг 0 значит «есть хоть что-то», а не «хватает суммы». *)
			WHILE a.balance < op.amount DO
				IF ~a.Await(ctx, {0}) THEN op.ok := FALSE; RETURN END
			END;
			DEC(a.balance, op.amount); op.ok := TRUE
		| op: DepositOp DO
			INC(a.balance, op.amount)
		| op: TransferOp DO
			(* Повторный вход в тот же монитор: глубина 2. *)
			w.amount := op.amount; w.ok := FALSE;
			a.Do(ctx, w);
			IF ~w.ok THEN RETURN END;
			(* Монитор A остаётся у задачи, пока берётся монитор B. *)
			d.amount := op.amount;
			op.to.Do(ctx, d);
			op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (b: Buyer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WithdrawOp;
	BEGIN
		Tasks.Sleep(5);
		op.amount := b.amount; op.ok := FALSE;
		b.a.Do(ctx, op)
	END Body;

	PROCEDURE (p: Payroll) Body- (VAR ctx: Tasks.Ctx);
		VAR op: DepositOp;
	BEGIN
		op.amount := p.first; p.a.Do(ctx, op);
		Tasks.Sleep(10);
		op.amount := p.second; p.a.Do(ctx, op)
	END Body;

	PROCEDURE (t: Transfer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: TransferOp;
	BEGIN
		Tasks.Sleep(15);
		op.to := t.to; op.amount := t.amount; op.ok := FALSE;
		t.from.Do(ctx, op);
		transferDone := op.ok
	END Body;

	PROCEDURE Do*;
		VAR a, b: Account; x, y: Buyer; p: Payroll; t: Transfer;
	BEGIN
		NEW(a); a.balance := 200;
		NEW(b); b.balance := 0;
		NEW(x); x.a := a; x.amount := 100;
		NEW(y); y.a := a; y.amount := 100;
		NEW(p); p.a := a; p.first := 50; p.second := 50;
		NEW(t); t.from := a; t.to := b; t.amount := 50;
		transferDone := FALSE;
		Tasks.Start(t); Tasks.Start(x); Tasks.Start(y); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("A = ", a.balance);
		Say("B = ", b.balance);
		IF transferDone THEN
			Console.WriteStr("transfer ok"); Console.WriteLn
		ELSE
			Console.WriteStr("transfer failed"); Console.WriteLn
		END
	END Do;

END ObxBankCtx.
