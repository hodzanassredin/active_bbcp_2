MODULE Test64WinWorkers;
(** Тесты рабочих нитей этапа 8 на Windows x64 (под wine).

	Отдельный модуль, а не Test64Workers: тот использует usleep и
	файловый путь /tmp, а здесь те же проверки на Win32: Sleep(300) в
	рабочей нити (задача ждёт, главный цикл делает шаги), запись и
	чтение файла, getaddrinfo для localhost и число нитей пула.

	Готовность забирает действие Services (AsyncWorkWin), поэтому шаги
	цикла здесь — Services.actionHook.Step, как и в Linux-тестах. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, AsyncWork, A := WinApi,
		UnitAssert, Console, Strings;

	TYPE
		SlowTask = POINTER TO RECORD (Tasks.Task) END;
		FileTask = POINTER TO RECORD (Tasks.Task) END;
		AddrTask = POINTER TO RECORD (Tasks.Task) END;

	VAR
		slowDone, slowRes, steps, fileDone, addrCount: INTEGER;
		slowElapsed: LONGINT;
		fileReq: AsyncWork.FileReq;
		addrReq: AsyncWork.AddrReq;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE [ccall] SlowJob (arg: S.ADDRESS);
	BEGIN
		A.Sleep(300)
	END SlowJob;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			A.Sleep(1);
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
		UnitAssert.True(steps > 50, "main loop kept running");
		Console.WriteStr("Test64WinWorkers: slow ms="); Strings.IntToString(slowElapsed, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" steps="); Strings.IntToString(steps, dlg);
		Console.WriteStr(dlg); Console.WriteLn
	END TestProcNoFreeze;

	(* --- файл ------------------------------------------------------------- *)

	PROCEDURE (t: FileTask) Run;
		VAR res, i: INTEGER; expected: ARRAY 32 OF SHORTCHAR;
	BEGIN
		fileReq.path := "active2-asyncwork.txt";
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

	PROCEDURE TestProcFileIo*;
		VAR t: FileTask;
	BEGIN
		NEW(t); fileReq.status := -1; fileDone := 0; Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, FileReady), "file jobs finish");
		UnitAssert.EqualInt(0, fileReq.status, "write+read round trip");
		UnitAssert.EqualInt(12, fileReq.len, "12 bytes read")
	END TestProcFileIo;

	(* --- разрешение имён -------------------------------------------------- *)

	PROCEDURE (t: AddrTask) Run;
		VAR res: INTEGER;
	BEGIN
		addrReq.host := "localhost";
		addrReq.count := 0; addrReq.status := -1;
		addrCount := 0;
		res := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(addrReq));
		addrCount := 1
	END Run;

	PROCEDURE TestProcResolve*;
		VAR t: AddrTask;
	BEGIN
		addrCount := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(5000, AddrReady), "resolve job finishes");
		UnitAssert.EqualInt(0, addrReq.status, "getaddrinfo succeeded");
		UnitAssert.True(addrReq.count > 0, "at least one address")
	END TestProcResolve;

	(* --- пул -------------------------------------------------------------- *)

	PROCEDURE TestProcWorkers*;
	BEGIN
		UnitAssert.EqualInt(4, AsyncWork.Workers(), "four worker threads")
	END TestProcWorkers;

END Test64WinWorkers.
