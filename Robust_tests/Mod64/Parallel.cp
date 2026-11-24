MODULE Robust_testsParallel;
(** Тесты фасада RobustParallel (этап 9).

	Клиент Robust_testsClient написан в стиле MPI против опубликованного
	определения; тест проверяет сумму 1..1000 при размере мира из
	внешней переменной ROBUST_SIZE. tools/test-robust.sh гоняет его на
	1, 4 и 16 процессах (управляющий + исполнители). *)

	IMPORT Kernel, Services, Tasks, AsyncIO, UnitAssert,
		Robust_testsClient, RobustParallel, Console, Strings;

	CONST
		n = 1000;

	TYPE
		ClientTask = POINTER TO RECORD (Tasks.Task) result, done: INTEGER END;

	VAR
		nsteps: INTEGER;
		clientTask: ClientTask;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1);
			INC(nsteps)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE (t: ClientTask) Run;
	BEGIN
		t.result := Robust_testsClient.Sum(n);
		t.done := 1
	END Run;

	PROCEDURE DoneCond (): BOOLEAN;
		VAR st: INTEGER;
	BEGIN
		IF clientTask = NIL THEN RETURN TRUE END;
		st := Tasks.State(clientTask);
		RETURN (clientTask.done = 1) OR (st = Tasks.Finished) OR (st = Tasks.Cancelled)
	END DoneCond;

	PROCEDURE TestSum*;
	(** Сумма считается верно на любом размере мира. *)
		VAR t: ClientTask; expected: INTEGER;
	BEGIN
		UnitAssert.EqualInt(0, RobustParallel.rank, "controller rank is 0");
		UnitAssert.True(RobustParallel.size >= 1, "world size is at least 1");
		expected := n * (n + 1) DIV 2;
		nsteps := 0;
		NEW(t); t.result := 0; t.done := 0; clientTask := t; Tasks.Start(t);
		UnitAssert.True(PumpUntil(20000, DoneCond), "client finishes");
		Console.WriteStr("Robust_testsParallel: size=");
		Strings.IntToString(RobustParallel.size, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" done=");
		Strings.IntToString(t.done, dlg); Console.WriteStr(dlg);
		Console.WriteStr(" state="); Strings.IntToString(Tasks.State(t), dlg);
		Console.WriteStr(dlg); Console.WriteStr(" failed=");
		IF Tasks.Failed(t) THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END;
		Console.WriteStr(" sum=");
		Strings.IntToString(t.result, dlg); Console.WriteStr(dlg);
		Console.WriteStr(" steps="); Strings.IntToString(nsteps, dlg);
		Console.WriteStr(dlg); Console.WriteLn;
		UnitAssert.EqualInt(expected, t.result, "sum 1..1000 is correct");
		RobustParallel.Stop
	END TestSum;

END Robust_testsParallel.
