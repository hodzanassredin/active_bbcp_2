MODULE ObxBank3;
	IMPORT Tasks, Console;

	TYPE
		DepositOp = RECORD (Tasks.Op) n: INTEGER END;
		TransferOp = RECORD (Tasks.Op) to: Account; n: INTEGER END;

		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;

		Clerk = POINTER TO RECORD (Tasks.Job) from, to: Account END;

	VAR a, b: Account; c1, c2: Clerk;

	PROCEDURE (acc: Account) HoldsWhileWaiting (): BOOLEAN;
	(* Перевод держит монитор счёта, пока берёт монитор второго счёта. *)
	BEGIN
		RETURN TRUE
	END HoldsWhileWaiting;

	PROCEDURE (acc: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR dep: DepositOp;
	BEGIN
		WITH op: DepositOp DO
			INC(acc.balance, op.n)
		| op: TransferOp DO
			DEC(acc.balance, op.n);
			Tasks.Yield;	(* держит монитор acc и уступает *)
			dep.n := op.n;
			op.to.Do(ctx, dep)	(* вход во второй монитор под монитором acc *)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (c: Clerk) Body- (VAR ctx: Tasks.Ctx);
		VAR op: TransferOp;
	BEGIN
		op.to := c.to; op.n := 10;
		c.from.Do(ctx, op)
	END Body;

	PROCEDURE Do*;
	BEGIN
		NEW(a); a.balance := 100; NEW(b); b.balance := 100;
		NEW(c1); c1.from := a; c1.to := b; Tasks.SetName(c1, "clerk a->b");
		NEW(c2); c2.from := b; c2.to := a; Tasks.SetName(c2, "clerk b->a");
		Tasks.Start(c1); Tasks.Start(c2);
		WHILE (Tasks.Count() > 0) & ~Tasks.Deadlocked() DO Tasks.Step END;
		IF Tasks.Deadlocked() THEN
			Console.WriteStr("deadlock, cancelling"); Console.WriteLn;
			Tasks.Cancel(c1); Tasks.Cancel(c2);
			WHILE Tasks.Count() > 0 DO Tasks.Step END
		END
	END Do;

END ObxBank3.
