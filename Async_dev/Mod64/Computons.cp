MODULE Async_devComputons;
(** Тесты компутонов: локальные процессы BlackBox.

	Square — сериализуемый компутон (Stores): x -> y=x*x. Пул из 1, 4 и
	16 процессов считает одни и те же 12 компутонов. Проверяются повтор
	после убитого исполнителя (Sleeper) и возврат ошибки Do (Boomer). *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, Stores, Files, AsyncComputon,
		AsyncIO, AsyncChan, AsyncSelect, LinNative, UnitAssert;

	CONST
		m = 12;
		tagBase = 100;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Sleeper = POINTER TO RECORD (AsyncComputon.Computon) ms, y: INTEGER END;
		Boomer = POINTER TO RECORD (AsyncComputon.Computon) END;
		Plain = POINTER TO RECORD (Stores.Store) END;
		LateTask = POINTER TO RECORD (Tasks.Task) first: BOOLEAN; y: INTEGER END;

		SquareTask = POINTER TO RECORD (Tasks.Task) n, done, bad: INTEGER END;
		ResultTask = POINTER TO RECORD (Tasks.Task) rank, tag: INTEGER; ok: BOOLEAN; y: INTEGER END;
		ChanSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
		AltTask = POINTER TO RECORD (Tasks.Task) tag, idx1, idx2, y: INTEGER END;

	VAR
		badCount, goodCount, doneFlag: INTEGER;
		resultOk: BOOLEAN;
		resultY: INTEGER;
		resultErr: INTEGER;
		boomTask: ResultTask; altTask: AltTask; altDone, altIdx1, altIdx2, altY: INTEGER; altChan: AsyncChan.Chan;

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

	PROCEDURE RunSquares (n: INTEGER; tcp: BOOLEAN);
		VAR t: SquareTask; ok: BOOLEAN;
	BEGIN
		IF tcp THEN ok := AsyncComputon.SetTransport("tcp")
		ELSE ok := AsyncComputon.SetTransport("pipe")
		END;
		UnitAssert.True(ok, "transport selected");
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
		RunSquares(1, FALSE)
	END TestSquares1;

	PROCEDURE TestSquares4*;
	BEGIN
		RunSquares(4, FALSE)
	END TestSquares4;

	PROCEDURE TestSquares16*;
	BEGIN
		RunSquares(16, FALSE)
	END TestSquares16;

	PROCEDURE TestSquaresTcp4*;
	(** Те же 12 квадратов на 4 исполнителях по TCP (localhost). *)
	BEGIN
		RunSquares(4, TRUE)
	END TestSquaresTcp4;

	PROCEDURE TestTransportSetting*;
	(** Неизвестное имя транспорта отвергается, прежний выбор сохраняется. *)
		VAR ok: BOOLEAN;
	BEGIN
		ok := AsyncComputon.SetTransport("pipe");
		IF ~ok THEN ASSERT(FALSE, 20) END;
		UnitAssert.True(~AsyncComputon.SetTransport("shm"), "unknown transport rejected");
		UnitAssert.True(AsyncComputon.SetTransport("mpi"), "mpi transport name accepted");
		ok := AsyncComputon.SetTransport("tcp");
		UnitAssert.True(ok, "tcp accepted")
	END TestTransportSetting;

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

	PROCEDURE KillRetry (tcp: BOOLEAN);
		VAR c: Sleeper; pid, res: INTEGER; t: ResultTask; ok: BOOLEAN;
	BEGIN
		IF tcp THEN ok := AsyncComputon.SetTransport("tcp")
		ELSE ok := AsyncComputon.SetTransport("pipe")
		END;
		UnitAssert.True(ok, "transport selected");
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
	END KillRetry;

	PROCEDURE TestKillRetry*;
	BEGIN
		KillRetry(FALSE)
	END TestKillRetry;

	PROCEDURE TestKillRetryTcp*;
	(** Гибель исполнителя по TCP: повтор на живом. *)
	BEGIN
		KillRetry(TRUE)
	END TestKillRetryTcp;

	(* --- ошибка Do -------------------------------------------------------- *)

	PROCEDURE TestDoError*;
		VAR c: Boomer; r: AsyncComputon.Computon;
	BEGIN
		IF ~AsyncComputon.SetTransport("pipe") THEN ASSERT(FALSE, 20) END;
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

	(* --- Alt ждёт Pull ---------------------------------------------------- *)

	PROCEDURE (t: ChanSender) Run;
	BEGIN
		Tasks.Sleep(10);
		t.c.Send(NIL)
	END Run;

	PROCEDURE (t: AltTask) Run;
		VAR ops: ARRAY 2 OF AsyncSelect.Op; c: AsyncComputon.Computon;
	BEGIN
		AsyncChan.RecvAlt(altChan, ops[0]);
		AsyncSelect.AltSignal(AsyncComputon.AltPull(0, t.tag), ops[1]);
		altIdx1 := AsyncChan.Select(ops);

		AsyncSelect.AltSignal(AsyncComputon.AltPull(0, t.tag), ops[0]);
		AsyncChan.TimeoutAlt(5000, ops[1]);
		altIdx2 := AsyncChan.Select(ops);

		altY := -1;
		IF AsyncComputon.Try(0, t.tag, c) THEN
			WITH c: Sleeper DO altY := c.y END
		END;
		altDone := 1
	END Run;

	PROCEDURE AltDoneCond (): BOOLEAN;
	BEGIN RETURN altDone = 1 END AltDoneCond;

	PROCEDURE TestAltPull*;
		VAR c: Sleeper; t: AltTask; s: ChanSender;
	BEGIN
		IF ~AsyncComputon.SetTransport("pipe") THEN ASSERT(FALSE, 20) END;
		AsyncComputon.Start(1);
		NEW(c); c.ms := 200; c.y := -1;
		AsyncComputon.Push(c, 0, 42);
		altChan := AsyncChan.NewChan();
		NEW(s); s.c := altChan; Tasks.Start(s);
		altDone := 0; altIdx1 := -1; altIdx2 := -1; altY := -1;
		NEW(t); t.tag := 42; Tasks.Start(t);
		UnitAssert.True(PumpUntil(20000, AltDoneCond), "alt+pull finishes");
		UnitAssert.EqualInt(0, altIdx1, "channel wins the first select");
		UnitAssert.EqualInt(0, altIdx2, "pull signal wins the second select");
		UnitAssert.EqualInt(200, altY, "computon result after alt")
	END TestAltPull;

	(* --- не компутон ------------------------------------------------------ *)

	PROCEDURE (p: Plain) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0)
	END Externalize;

	PROCEDURE (p: Plain) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, version)
	END Internalize;

	PROCEDURE TestDeserializeNotComputon*;
	(** Хранимый объект другого типа даёт NIL, а не трап. *)
		VAR p: Plain; f: Files.File; wr: Stores.Writer; rd: Files.Reader;
			b: POINTER TO ARRAY OF SHORTCHAR; by: BYTE; i, len: INTEGER;
	BEGIN
		NEW(p); f := Files.dir.Temp();
		wr.ConnectTo(f); wr.WriteStore(p); f.Flush();
		len := f.Length(); NEW(b, len);
		rd := f.NewReader(NIL);
		FOR i := 0 TO len - 1 DO rd.ReadByte(by); b[i] := S.VAL(SHORTCHAR, by) END;
		UnitAssert.True(AsyncComputon.Deserialize(b^, len) = NIL, "not a computon gives NIL")
	END TestDeserializeNotComputon;

	(* --- поздний ответ после тайм-аута ------------------------------------ *)

	PROCEDURE (t: LateTask) Run;
		VAR c: AsyncComputon.Computon; s: Sleeper; q: Square;
	BEGIN
		NEW(s); s.ms := 300; s.y := -1;
		AsyncComputon.Push(s, 0, 77);
		c := AsyncComputon.PullTimeout(0, 77, 50);
		t.first := c = NIL;
		NEW(q); q.x := 3; q.y := -1;
		AsyncComputon.Push(q, 0, 77);
		c := AsyncComputon.Pull(0, 77);
		t.y := -1;
		IF c # NIL THEN WITH c: Square DO t.y := c.y ELSE t.y := -2 END END;
		doneFlag := 1
	END Run;

	PROCEDURE TestLateReplyAfterTimeout*;
	(** Ответ на запрос, брошенный по тайм-ауту, не достаётся новому
		запросу с тем же tag. *)
		VAR t: LateTask;
	BEGIN
		IF ~AsyncComputon.SetTransport("pipe") THEN ASSERT(FALSE, 20) END;
		AsyncComputon.Start(1);
		doneFlag := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpUntil(20000, DoneCond), "both pulls finished");
		UnitAssert.True(t.first, "first pull timed out");
		UnitAssert.EqualInt(9, t.y, "second pull got its own result")
	END TestLateReplyAfterTimeout;

END Async_devComputons.
