MODULE ObxTasks0;
(** Задача нижнего уровня: Run без пропуска — это и есть предмет примера. *)
	IMPORT Tasks, Console, Strings;

	TYPE Counter = POINTER TO RECORD (Tasks.Task) END;

	VAR count: INTEGER;

	PROCEDURE (c: Counter) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 1000 DO
			x := count; count := x + 1;	(* чтение и запись без точки переключения между ними *)
			IF i MOD 10 = 0 THEN Tasks.Yield END
		END
	END Run;

	PROCEDURE Do*;
		VAR a, b: Counter; s: ARRAY 16 OF CHAR;
	BEGIN
		count := 0;
		NEW(a); NEW(b); Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(count, s);
		Console.WriteStr("count = "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxTasks0.
