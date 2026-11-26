MODULE ObxStm0;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST limit = 150;	(* больше этого счёт b не принимает *)

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER
		END;
		Clerk = POINTER TO RECORD (Tasks.Job) END;

	VAR a, b: AsyncStm.IntVar;

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);	(* снять *)
		ASSERT(t.to.Get() + t.n <= limit, 30);	(* трап между «снять» и «зачислить» *)
		t.to.Set(t.to.Get() + t.n)	(* зачислить *)
	END Do;

	PROCEDURE Move (n: INTEGER);
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := a; t.to := b; t.n := n;
		AsyncStm.Atomically(t)
	END Move;

	PROCEDURE (c: Clerk) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Move(100); Move(100)
	END Body;

	PROCEDURE Show;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr("a = "); Strings.IntToString(a.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", b = "); Strings.IntToString(b.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", sum = "); Strings.IntToString(a.Get() + b.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR c: Clerk;
	BEGIN
		a := AsyncStm.NewInt(300); b := AsyncStm.NewInt(0);
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Console.WriteStr("clerk trapped") ELSE Console.WriteStr("clerk done") END;
		Console.WriteLn;
		Show
	END Do;

END ObxStm0.
