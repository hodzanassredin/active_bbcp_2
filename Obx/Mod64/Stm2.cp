MODULE ObxStm2;
	IMPORT Tasks, AsyncStm, Console, Strings;

	TYPE
		Pay = POINTER TO RECORD (AsyncStm.Tx) card: AsyncStm.IntVar; n: INTEGER END;
		Either = POINTER TO RECORD (AsyncStm.Tx) first, second: Pay END;
		Customer = POINTER TO RECORD (Tasks.Job) n: INTEGER END;
		Bank = POINTER TO RECORD (Tasks.Job) END;

	VAR card1, card2: AsyncStm.IntVar;

	PROCEDURE Show (IN what: ARRAY OF CHAR);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(what);
		Console.WriteStr(": card1 = "); Strings.IntToString(card1.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", card2 = "); Strings.IntToString(card2.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE (t: Pay) Do;
	BEGIN
		IF t.card.Get() < t.n THEN AsyncStm.Retry END;
		t.card.Set(t.card.Get() - t.n)
	END Do;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.first, t.second)
	END Do;

	PROCEDURE (c: Customer) Body- (VAR ctx: Tasks.Ctx);
		VAR t: Either;
	BEGIN
		NEW(t); NEW(t.first); NEW(t.second);
		t.first.card := card1; t.first.n := c.n;
		t.second.card := card2; t.second.n := c.n;
		AsyncStm.Atomically(t);
		Show("paid")
	END Body;

	PROCEDURE (b: Bank) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(10);
		card1.Set(card1.Get() + 100);
		Show("deposit")
	END Body;

	PROCEDURE Do*;
		VAR c: Customer; b: Bank;
	BEGIN
		card1 := AsyncStm.NewInt(10); card2 := AsyncStm.NewInt(80);
		NEW(c); c.n := 50; Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		NEW(c); c.n := 50; Tasks.Start(c);
		NEW(b); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm2.
