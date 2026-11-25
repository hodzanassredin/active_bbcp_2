MODULE ObxBank1;
	IMPORT Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Clerk = POINTER TO RECORD (Tasks.Task) from, to, sum: INTEGER END;
		Clock = POINTER TO RECORD (Tasks.Task) END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; refused: INTEGER; clock: Clock;

	PROCEDURE Total (): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0; FOR i := 0 TO n - 1 DO s := s + balance[i] END;
		RETURN s
	END Total;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Перевод: проверка и обе записи без точки переключения между ними *)
	PROCEDURE Transfer (from, to, sum: INTEGER);
	BEGIN
		IF balance[from] >= sum THEN
			balance[from] := balance[from] - sum;
			balance[to] := balance[to] + sum
		ELSE
			INC(refused)
		END
	END Transfer;

	PROCEDURE (c: Clerk) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO Transfer(c.from, c.to, c.sum); Tasks.Yield END
	END Run;

	PROCEDURE (c: Clock) Run;
	BEGIN
		LOOP Tasks.Sleep(1000) END	(* «часы на стене»: работают, пока их не отменят *)
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR g: Tasks.Group; c: ARRAY n OF Clerk; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].from := i; c[i].to := (i + 1) MOD n; c[i].sum := 40 + 30 * i;
			Tasks.StartIn(c[i], g)
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]) END;
		Say("refused ", refused);
		Say("total ", Total());
		Tasks.Cancel(clock)
	END Run;

	PROCEDURE Do*;
		VAR m: Manager;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100; refused := 0;
		NEW(clock); Tasks.Start(clock);
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("clock state ", Tasks.State(clock))
	END Do;

END ObxBank1.
