MODULE AsyncTestsWorkers;
(** Тесты рабочих нитей этапа 8.

	Медленный вызов в рабочей нити не замораживает главный цикл: задача
	ждёт Execute, главный стек продолжает крутить действия. Проверяются
	разрешение имён и файловые задания. Отладочный счётчик обращений к
	куче из рабочей нити удалён из ядра: рабочие задания не выделяют
	память по построению, а счётчик был общим флагом без атомарности. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, AsyncWork, AsyncIO,
		LinNative, UnitAssert, Console, Strings;

	TYPE
		SlowTask = POINTER TO RECORD (Tasks.Task) END;
		FileTask = POINTER TO RECORD (Tasks.Task) END;
		AddrTask = POINTER TO RECORD (Tasks.Task) END;

	VAR
		slowDone, slowRes, steps: INTEGER; slowElapsed: LONGINT;
		fileDone: INTEGER;
		fileReq: AsyncWork.FileReq;
		addrReq: AsyncWork.AddrReq;
		addrStatus, addrCount: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE [ccall] SlowJob (arg: S.ADDRESS);
		VAR res: INTEGER;
	BEGIN
		res := LinNative.usleep(300000)
	END SlowJob;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1);
			INC(steps)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE SlowReady (): BOOLEAN;
	BEGIN RETURN slowDone = 1 END SlowReady;

	PROCEDURE FileReady (): BOOLEAN;
	BEGIN RETURN fileDone = 1 END FileReady;

	PROCEDURE AddrReady (): BOOLEAN;
	BEGIN RETURN addrCount = 1 END AddrReady;

	(* --- медленный вызов -------------------------------------------------- *)

	PROCEDURE (t: SlowTask) Run;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		slowRes := AsyncWork.Execute(S.ADR(SlowJob), 0);
		slowElapsed := Kernel.Time() - t0;
		slowDone := 1
	END Run;

	PROCEDURE TestProcNoFreeze*;
		VAR t: SlowTask;
	BEGIN
		slowDone := 0; slowRes := -2; slowElapsed := 0; steps := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, SlowReady), "slow job finishes");
		UnitAssert.True(slowElapsed >= 250, "slow call really took time");
		UnitAssert.True(steps > 100, "main loop kept running");
		Console.WriteStr("AsyncTestsWorkers: slow ms="); Strings.IntToString(slowElapsed, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" steps="); Strings.IntToString(steps, dlg);
		Console.WriteStr(dlg); Console.WriteLn
	END TestProcNoFreeze;

	(* --- файл ------------------------------------------------------------- *)

	PROCEDURE (t: FileTask) Run;
		VAR res, i: INTEGER; expected: ARRAY 32 OF SHORTCHAR;
	BEGIN
		fileReq.path := "/tmp/active2-asyncwork.txt";
		fileReq.buf := "hello worker";
		fileReq.len := 12; fileReq.status := -1;
		res := AsyncWork.Execute(S.ADR(AsyncWork.WriteFileJob), S.ADR(fileReq));
		IF fileReq.status = 0 THEN
			fileReq.len := 0;
			res := AsyncWork.Execute(S.ADR(AsyncWork.ReadFileJob), S.ADR(fileReq));
			IF fileReq.status = 0 THEN
				expected := "hello worker";
				IF fileReq.len = 12 THEN
					FOR i := 0 TO 11 DO
						IF fileReq.buf[i] # expected[i] THEN fileReq.status := -2 END
					END
				ELSE fileReq.status := -3
				END
			END
		END;
		fileDone := 1
	END Run;

	PROCEDURE TestFileIo*;
		VAR t: FileTask;
	BEGIN
		NEW(t); fileReq.status := -1; fileDone := 0; Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, FileReady), "file jobs finish");
		UnitAssert.EqualInt(0, fileReq.status, "write+read round trip");
		UnitAssert.EqualInt(12, fileReq.len, "12 bytes read")
	END TestFileIo;

	(* --- разрешение имён -------------------------------------------------- *)

	PROCEDURE (t: AddrTask) Run;
	BEGIN
		addrReq.host := "localhost";
		addrReq.count := 0; addrReq.status := -1;
		addrStatus := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(addrReq));
		addrCount := 1
	END Run;

	PROCEDURE TestResolve*;
		VAR t: AddrTask;
	BEGIN
		addrCount := 0; addrStatus := -2;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, AddrReady), "resolve job finishes");
		UnitAssert.EqualInt(0, addrReq.status, "getaddrinfo succeeded");
		UnitAssert.True(addrReq.count > 0, "at least one address")
	END TestResolve;

END AsyncTestsWorkers.
