MODULE ObxBank0;
	IMPORT Services, Tasks, Console, Strings;

	CONST n = 3;	(* число счетов *)

	TYPE
		Statement = POINTER TO RECORD (Services.Action) i: INTEGER END;
		StatementTask = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; done: BOOLEAN;

	PROCEDURE Line (IN who: ARRAY OF CHAR; i: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(who); Console.WriteStr(": account ");
		Strings.IntToString(i, s); Console.WriteStr(s);
		Console.WriteStr(" balance "); Strings.IntToString(balance[i], s); Console.WriteStr(s);
		Console.WriteLn
	END Line;

	(* Действие: один счёт за вызов, место в работе хранит поле i *)
	PROCEDURE (a: Statement) Do;
	BEGIN
		Line("action", a.i);
		INC(a.i);
		IF a.i < n THEN Services.DoLater(a, Services.now) ELSE done := TRUE END
	END Do;

	(* Задача: обычный цикл, место в работе лежит на её стеке *)
	PROCEDURE (t: StatementTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO Line("task", i); Tasks.Yield END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Statement; t: StatementTask;
	BEGIN
		balance[0] := 100; balance[1] := 250; balance[2] := 40;
		NEW(a); a.i := 0; done := FALSE; Services.DoLater(a, Services.now);
		WHILE ~done DO Services.actionHook.Step END;
		NEW(t); done := FALSE; Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END
	END Do;

END ObxBank0.
