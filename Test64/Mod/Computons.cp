MODULE Test64Computons;
(** Тесты компутонов этапа 9: локальные процессы BlackBox.

	Square — сериализуемый компутон (Stores): x -> y=x*x. Пул из 1, 4 и
	16 процессов считает одни и те же 12 компутонов. Проверяются повтор
	после убитого исполнителя (Sleeper) и возврат ошибки Do (Boomer). *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, Stores, AsyncComputon,
		AsyncIO, LinNative, UnitAssert;

	CONST
		m = 12;
		tagBase = 100;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Sleeper = POINTER TO RECORD (AsyncComputon.Computon) ms, y: INTEGER END;
		Boomer = POINTER TO RECORD (AsyncComputon.Computon) END;

		SquareTask = POINTER TO RECORD (Tasks.Task) n, done, bad: INTEGER END;
		ResultTask = POINTER TO RECORD (Tasks.Task) rank, tag: INTEGER; ok: BOOLEAN; y: INTEGER END;

	VAR
		badCount, goodCount, doneFlag: INTEGER;
		resultOk: BOOLEAN;
		resultY: INTEGER;
		resultErr: INTEGER;
		boomTask: ResultTask;

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

	PROCEDURE (c: Sleeper) Do;
		VAR res: INTEGER;
	BEGIN
		res := LinNative.usleep(c.ms * 1000);
		c.y := c.ms
	END Do;

	PROCEDURE (c: Sleeper) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0);
		wr.WriteInt(c.ms); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Sleeper) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version);
		rd.ReadInt(c.ms); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (c: Boomer) Do;
	BEGIN
		ASSERT(FALSE, 20)
	END Do;

	PROCEDURE (c: Boomer) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0)
	END Externalize;

	PROCEDURE (c: Boomer) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version)
	END Internalize;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE DoneCond (): BOOLEAN;
	BEGIN RETURN doneFlag = 1 END DoneCond;

	PROCEDURE (t: SquareTask) Run;
		VAR i, size: INTEGER; c: Square; r: AsyncComputon.Computon; d: Square;
	BEGIN
		size := AsyncComputon.Size();
		FOR i := 0 TO m - 1 DO
			NEW(c); c.x := i; c.y := -1;
			AsyncComputon.Push(c, i MOD size, tagBase + i)
		END;
		FOR i := 0 TO m - 1 DO
			r := AsyncComputon.Pull(i MOD size, tagBase + i);
			WITH r: Square DO
				IF (r.x = i) & (r.y = i * i) THEN INC(goodCount) ELSE INC(badCount) END
			ELSE INC(badCount)
			END
		END;
		doneFlag := 1
	END Run;

	PROCEDURE RunSquares (n: INTEGER);
		VAR t: SquareTask;
	BEGIN
		AsyncComputon.Start(n);
		UnitAssert.EqualInt(n, AsyncComputon.Size(), "all workers started");
		goodCount := 0; badCount := 0; doneFlag := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(120000, DoneCond), "squares computed");
		UnitAssert.EqualInt(m, goodCount, "all 12 results correct");
		UnitAssert.EqualInt(0, badCount, "no wrong results")
	END RunSquares;

	PROCEDURE TestSquares1*;
	BEGIN
		RunSquares(1)
	END TestSquares1;

	PROCEDURE TestSquares4*;
	BEGIN
		RunSquares(4)
	END TestSquares4;

	PROCEDURE TestSquares16*;
	BEGIN
		RunSquares(16)
	END TestSquares16;

	(* --- гибель исполнителя ----------------------------------------------- *)

	PROCEDURE (t: ResultTask) Run;
		VAR c: AsyncComputon.Computon; res: INTEGER;
	BEGIN
		c := AsyncComputon.PullTimeout(t.rank, t.tag, 20000);
		resultErr := AsyncComputon.LastError();
		resultOk := c # NIL;
		resultY := -1;
		IF c # NIL THEN WITH c: Sleeper DO resultY := c.y END END;
		resultErr := AsyncComputon.LastError();
		doneFlag := 1
	END Run;

	PROCEDURE TestKillRetry*;
		VAR c: Sleeper; pid, res: INTEGER; t: ResultTask;
	BEGIN
		AsyncComputon.Start(2);
		UnitAssert.EqualInt(2, AsyncComputon.Size(), "two executors");
		NEW(c); c.ms := 400; c.y := -1;
		AsyncComputon.Push(c, 1, 7);
		pid := AsyncComputon.WorkerPid(1);
		UnitAssert.True(pid > 0, "second executor has pid");
		res := LinNative.kill(pid, LinNative.SIGKILL);
		(* дать главному циклу заметить EOF и повторить на живом *)
		doneFlag := 0; resultOk := FALSE; resultY := -1;
		NEW(t); t.rank := 1; t.tag := 7; Tasks.Start(t);
		UnitAssert.True(PumpUntil(30000, DoneCond), "retry completes");
		UnitAssert.True(resultOk, "result returned after executor died");
		UnitAssert.EqualInt(400, resultY, "retried computon computed")
	END TestKillRetry;

	(* --- ошибка Do -------------------------------------------------------- *)

	PROCEDURE TestDoError*;
		VAR c: Boomer; r: AsyncComputon.Computon;
	BEGIN
		AsyncComputon.Start(1);
		NEW(c);
		AsyncComputon.Push(c, 0, 9);
		(* Pull из задачи *)
		doneFlag := 0; resultOk := FALSE; resultErr := 0;
		NEW(boomTask); boomTask.rank := 0; boomTask.tag := 9; Tasks.Start(boomTask);
		UnitAssert.True(PumpUntil(20000, DoneCond), "error result arrives");
		UnitAssert.True(~resultOk, "Do error gives NIL");
		UnitAssert.True(resultErr # 0, "error code returned")
	END TestDoError;

END Test64Computons.
