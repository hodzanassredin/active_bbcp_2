MODULE ObxTasks3;
(** Задачи нижнего уровня: пример показывает группу и ожидание её в TaskBoot. *)
	IMPORT Tasks, Console, Strings;

	TYPE
		Child = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Task) g: Tasks.Group END;

	VAR kids: ARRAY 3 OF Child;

	PROCEDURE (c: Child) Run;
	BEGIN
		IF c.n = 1 THEN Tasks.Sleep(10); HALT(30) END;
		LOOP Tasks.Sleep(1000) END
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR i: INTEGER;
	BEGIN
		p.g := Tasks.NewGroup();
		FOR i := 0 TO 2 DO NEW(kids[i]); kids[i].n := i; Tasks.StartIn(kids[i], p.g) END
	END Run;

	PROCEDURE Do*;
		VAR p: Parent; i: INTEGER; s: ARRAY 8 OF CHAR;
	BEGIN
		NEW(p); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.GroupFailed(p.g) THEN Console.WriteStr("group failed") ELSE Console.WriteStr("group ok") END;
		Console.WriteLn;
		FOR i := 0 TO 2 DO
			Strings.IntToString(Tasks.State(kids[i]), s);
			Console.WriteStr("child state "); Console.WriteStr(s); Console.WriteLn
		END
	END Do;

END ObxTasks3.
