MODULE AsyncTestsMpi;
(** Тест MPI-транспорта компутонов (этап 9) под mpiexec.

	Мир: ранг 0 — этот тест, ранги 1..4 — AsyncMpi.WorkerLoop.
	12 квадратов раздаются на 4 исполнителя; результат проверяется.
	Запускается только из tools/test-mpi.sh (нужны mpiexec и libmpi
	из .work/venv, собранная libbbmpi.so и LD_LIBRARY_PATH). *)

	IMPORT Kernel, Services, Tasks, Stores, AsyncComputon, AsyncMpi,
		AsyncIO, UnitAssert, Console, Strings;

	CONST
		m = 12;
		tagBase = 200;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		SqTask = POINTER TO RECORD (Tasks.Task) done: INTEGER END;

	VAR
		good, bad, doneFlag, steps: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE (c: Square) Do;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0);
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version);
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

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

	PROCEDURE (t: SqTask) Run;
		VAR i, size: INTEGER; c: Square; r: AsyncComputon.Computon;
	BEGIN
		size := AsyncComputon.Size();
		FOR i := 0 TO m - 1 DO
			NEW(c); c.x := i; c.y := -1;
			AsyncComputon.Push(c, i MOD size, tagBase + i)
		END;
		FOR i := 0 TO m - 1 DO
			r := AsyncComputon.Pull(i MOD size, tagBase + i);
			WITH r: Square DO
				IF (r.x = i) & (r.y = i * i) THEN INC(good) ELSE INC(bad) END
			ELSE INC(bad)
			END
		END;
		doneFlag := 1
	END Run;

	PROCEDURE DoneCond (): BOOLEAN;
	BEGIN
		RETURN doneFlag = 1
	END DoneCond;

	PROCEDURE TestProcMpiSquares*;
		VAR t: SqTask;
	BEGIN
		UnitAssert.True(AsyncMpi.Available(), "libbbmpi and libmpi bound");
		UnitAssert.True(AsyncMpi.Install(), "mpi transport installed");
		UnitAssert.True(AsyncComputon.SetTransport("mpi"), "mpi transport selected");
		AsyncComputon.Start(4);
		UnitAssert.EqualInt(4, AsyncComputon.Size(), "four executors started");
		UnitAssert.EqualInt(5, AsyncMpi.WorldSize(), "mpiexec world of five ranks");
		good := 0; bad := 0; doneFlag := 0; steps := 0;
		NEW(t); t.done := 0; Tasks.Start(t);
		UnitAssert.True(PumpUntil(60000, DoneCond), "squares computed over MPI");
		Console.WriteStr("AsyncTestsMpi: good="); Strings.IntToString(good, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" bad="); Strings.IntToString(bad, dlg);
		Console.WriteStr(dlg); Console.WriteStr(" steps="); Strings.IntToString(steps, dlg);
		Console.WriteStr(dlg); Console.WriteLn;
		UnitAssert.EqualInt(m, good, "all 12 results correct");
		UnitAssert.EqualInt(0, bad, "no wrong results");
		AsyncComputon.Stop
	END TestProcMpiSquares;

END AsyncTestsMpi.
