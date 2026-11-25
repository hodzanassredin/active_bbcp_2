MODULE Async_devBenchScale;
(** Замер масштаба: сколько задач и мониторов можно создать.

	Каждый N — отдельный запуск процесса: RSS не наследуется. Команда
	принимает два числа через DevCommanders.par: N и размер стека
	(0 — Tasks.defaultStack, 256 КБ). Строка SCALE ... печатает время
	создания, VmRSS, VmSize, число строк /proc/self/maps, цену пары
	«задача + монитор», время одной Kernel.Collect при стоящих задачах,
	время пробуждения и число завершившихся.

	Варианты:
		Ctx   — Job входит в СВОЙ Tasks.Object и ждёт в Await (условие ложно);
		Old   — то же на AsyncObject из этой ветки;
		Excl  — задача ждёт в [exclusive]-методе через ACTIVE.AWAIT;
		Chan  — задача ждёт приёма из своего AsyncChan.Chan;
		Park  — задача спит в Tasks.Park без мониторов;
		Mon   — только объекты-мониторы, без задач (цена монитора).

	Стеки — Kernel.AllocStack (LinKernel: mmap + сторожевая страница
	PROT_NONE). Отказ стека (Tasks.Start — трап 22) ловится Kernel.Try:
	процесс не падает и печатает stopped=stack. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Tasks, AsyncObject, AsyncChan, AsyncSelect,
		System_devClock, Console, Strings, DevCommanders, TextModels, Libc := LinLibc;

	CONST
		maxN = 1000000;
		rssLimitKb = 16 * 1024 * 1024;	(* 16 ГБ *)
		pageKb = 4;
		checkEvery = 4096;
		settleMs = 60000;
		wakeMs = 600000;
		gcMaxN = 100000;	(* дольше одной сборки при большем N не ждём *)

	TYPE
		(* --- вариант Ctx: свой объект-монитор и Await с ложным условием --- *)

		WaitOp = RECORD (Tasks.Op) END;
		CtxObj = POINTER TO RECORD (Tasks.Object) flag: BOOLEAN END;
		CtxJob = POINTER TO RECORD (Tasks.Job) o: CtxObj END;

		(* --- вариант Old: AsyncObject --- *)

		OldObj = POINTER TO RECORD (AsyncObject.Object) flag: BOOLEAN END;
		OldWaitOp = RECORD (AsyncObject.Op) END;
		OldSetOp = RECORD (AsyncObject.Op) END;
		OldJob = POINTER TO RECORD (Tasks.Task) o: OldObj END;

		(* --- вариант Excl: [exclusive] и ACTIVE.AWAIT --- *)

		ExBox = POINTER TO RECORD flag: BOOLEAN END;
		ExJob = POINTER TO RECORD (Tasks.Task) b: ExBox END;
		ExDriver = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

		(* --- вариант Chan --- *)

		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		ChanJob = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;

		(* --- вариант Park --- *)

		ParkJob = POINTER TO RECORD (Tasks.Task) END;

	VAR
		ctxObjs: POINTER TO ARRAY OF CtxObj;
		ctxJobs: POINTER TO ARRAY OF CtxJob;
		oldObjs: POINTER TO ARRAY OF OldObj;
		oldJobs: POINTER TO ARRAY OF OldJob;
		exBoxes: POINTER TO ARRAY OF ExBox;
		exJobs: POINTER TO ARRAY OF ExJob;
		chans: POINTER TO ARRAY OF AsyncChan.Chan;
		chanJobs: POINTER TO ARRAY OF ChanJob;
		parkJobs: POINTER TO ARRAY OF ParkJob;

		(* запуск под Kernel.Try: трап 22 при отказе стека *)
		startTask: Tasks.Task; startOk: BOOLEAN;
		gcMode: INTEGER;	(* -1 — сам: до gcMaxN; 0 — не мерить; 1 — мерить *)

	(* -------------------- вывод ------------------------------------------- *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Ln;
	BEGIN Console.WriteLn
	END Ln;

	(* -------------------- параметры --------------------------------------- *)

	PROCEDURE ReadPars (OUT n, stack, gc: INTEGER);
		VAR p: DevCommanders.Par; rd: TextModels.Reader; field: INTEGER;
			have: BOOLEAN; ch: CHAR; d: INTEGER;
	BEGIN
		n := 0; stack := 0; gc := 0; field := 0; have := FALSE;
		p := DevCommanders.par;
		IF p = NIL THEN RETURN END;
		DevCommanders.par := NIL;
		rd := p.text.NewReader(NIL);
		rd.SetPos(p.beg); rd.Read;
		WHILE ~rd.eot & (rd.Pos() <= p.end) DO
			ch := rd.char;
			IF (ch >= "0") & (ch <= "9") THEN
				d := ORD(ch) - ORD("0"); have := TRUE;
				IF field = 0 THEN n := n * 10 + d
				ELSIF field = 1 THEN stack := stack * 10 + d
				ELSE gc := gc * 10 + d
				END
			ELSIF have THEN
				INC(field); have := FALSE
			END;
			rd.Read
		END;
		IF field < 2 THEN gc := -1 END;	(* третьего числа нет — режим сам *)
		IF n > maxN THEN n := maxN END
	END ReadPars;

	PROCEDURE SetStack (t: Tasks.Task; stack: INTEGER);
	BEGIN
		IF stack > 0 THEN Tasks.SetStackSize(t, stack) END
	END SetStack;

	(* -------------------- память ------------------------------------------ *)

	PROCEDURE ReadMem (OUT vmKb, rssKb: LONGINT);
		(* /proc/self/statm: size resident shared text lib data dt, страницы. *)
		VAR f: Libc.PtrFILE; buf: ARRAY 256 OF SHORTCHAR; i: INTEGER;
			v1, v2: LONGINT; ch: SHORTCHAR;
	BEGIN
		vmKb := -1; rssKb := -1;
		f := Libc.fopen("/proc/self/statm", "r");
		IF f = Libc.NULL THEN RETURN END;
		IF Libc.fgets(buf, LEN(buf), f) # NIL THEN
			i := 0; v1 := 0; v2 := 0;
			WHILE (buf[i] >= "0") & (buf[i] <= "9") DO
				v1 := v1 * 10 + (ORD(buf[i]) - ORD("0")); INC(i)
			END;
			WHILE (buf[i] = " ") OR (buf[i] = 9X) DO INC(i) END;
			WHILE (buf[i] >= "0") & (buf[i] <= "9") DO
				v2 := v2 * 10 + (ORD(buf[i]) - ORD("0")); INC(i)
			END;
			vmKb := v1 * pageKb; rssKb := v2 * pageKb
		END;
		IF Libc.fclose(f) # 0 THEN END
	END ReadMem;

	PROCEDURE CountMaps (): LONGINT;
		VAR f: Libc.PtrFILE; buf: ARRAY 512 OF SHORTCHAR; n: LONGINT;
	BEGIN
		n := 0;
		f := Libc.fopen("/proc/self/maps", "r");
		IF f = Libc.NULL THEN RETURN -1 END;
		WHILE Libc.fgets(buf, LEN(buf), f) # NIL DO INC(n) END;
		IF Libc.fclose(f) # 0 THEN END;
		RETURN n
	END CountMaps;

	PROCEDURE StartBody (a, b, c: S.ADDRESS);
	BEGIN
		Tasks.Start(startTask);
		startOk := TRUE
	END StartBody;

	PROCEDURE DoCollect (created: INTEGER): LONGINT;
		VAR t0: LONGINT;
	BEGIN
		IF (gcMode = 0) OR ((gcMode < 0) & (created > gcMaxN)) THEN RETURN -1 END;
		Kernel.blockCollect := FALSE;
		t0 := System_devClock.now();
		Kernel.Collect;
		t0 := System_devClock.now() - t0;
		Kernel.blockCollect := TRUE;
		RETURN t0
	END DoCollect;

	PROCEDURE Start (t: Tasks.Task; stack: INTEGER): BOOLEAN;
	BEGIN
		SetStack(t, stack);
		startTask := t; startOk := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(StartBody)), 0, 0, 0);
		RETURN startOk
	END Start;

	PROCEDURE Settle;
	(* Дать всем задачам дойти до ожидания: шаги, пока меняются переключения. *)
		VAR last, s: LONGINT; t0: LONGINT; quiet: INTEGER;
	BEGIN
		t0 := Kernel.Time(); last := -1; quiet := 0;
		REPEAT
			s := Tasks.Switches();
			IF s = last THEN INC(quiet) ELSE quiet := 0; last := s END;
			IF quiet >= 3 THEN RETURN END;
			Tasks.Step
		UNTIL Kernel.Time() - t0 > settleMs
	END Settle;

	PROCEDURE Pump (ms: LONGINT);
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END
	END Pump;

	PROCEDURE Report (IN what: ARRAY OF CHAR; n, stack, created: INTEGER;
		IN stop: ARRAY OF CHAR; createNs, settleNs, gcNs, wakeNs: LONGINT;
		vm0, rss0, vm, rss: LONGINT; maps, finished: LONGINT);
	BEGIN
		W("SCALE "); W(what);
		W(" n="); N(n); W(" stack="); N(stack);
		W(" created="); N(created); W(" stopped="); W(stop);
		W(" createMs="); N(createNs DIV 1000000);
		W(" settleMs="); N(settleNs DIV 1000000);
		W(" vmRssKb="); N(rss); W(" rssPerPair=");
		IF created > 0 THEN N((rss - rss0) * 1024 DIV created) ELSE N(-1) END;
		W(" vmSizeKb="); N(vm); W(" vmSizeDeltaKb="); N(vm - vm0);
		W(" maps="); N(maps);
		W(" gcMs=");
		IF gcNs >= 0 THEN N(gcNs DIV 1000000) ELSE W("skipped") END;
		W(" wakeMs="); N(wakeNs DIV 1000000);
		W(" finished="); N(finished);
		Ln
	END Report;

	(* -------------------- Ctx: Tasks.Object и Await ------------------------ *)

	PROCEDURE (o: CtxObj) Ready (): SET;
	BEGIN
		IF o.flag THEN RETURN {0} ELSE RETURN {} END
	END Ready;

	PROCEDURE (o: CtxObj) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: WaitOp DO
			WHILE ~o.flag DO
				IF ~o.Await(ctx, {0}) THEN RETURN END
			END
		END
	END Exclusive;

	PROCEDURE (j: CtxJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WaitOp;
	BEGIN
		j.o.Do(ctx, op)
	END Body;

	PROCEDURE CtxRun (n, stack: INTEGER);
		VAR i, created, finished: INTEGER; stop: ARRAY 16 OF CHAR;
			vm0, rss0, vm, rss: LONGINT; maps: LONGINT;
			t0, t1, t2, gc0: LONGINT; settle0: LONGINT;
	BEGIN
		created := 0; stop := "none"; finished := 0;
		ReadMem(vm0, rss0);
		NEW(ctxObjs, n); NEW(ctxJobs, n);
		Kernel.blockCollect := TRUE;
		t0 := System_devClock.now();
		i := 0;
		LOOP
			IF i >= n THEN EXIT END;
			NEW(ctxObjs[i]); NEW(ctxJobs[i]); ctxJobs[i].o := ctxObjs[i];
			IF ~Start(ctxJobs[i], stack) THEN stop := "stack"; EXIT END;
			INC(created); INC(i);
			IF created MOD checkEvery = 0 THEN
				ReadMem(vm, rss);
				IF rss > rssLimitKb THEN stop := "rss"; EXIT END
			END
		END;
		t1 := System_devClock.now();
		settle0 := System_devClock.now();
		Settle;
		t1 := t1 - t0;
		settle0 := System_devClock.now() - settle0;
		ReadMem(vm, rss); maps := CountMaps();
		gc0 := DoCollect(created);
		t2 := System_devClock.now();
		IF created > 0 THEN
			FOR i := 0 TO created - 1 DO ctxObjs[i].flag := TRUE; Tasks.Unpark(ctxJobs[i]) END
		END;
		Pump(wakeMs);
		FOR i := 0 TO created - 1 DO
			IF Tasks.State(ctxJobs[i]) IN {Tasks.Finished, Tasks.Cancelled} THEN INC(finished) END
		END;
		t2 := System_devClock.now() - t2;
		Kernel.blockCollect := FALSE;
		Report("ctx", n, stack, created, stop, t1, settle0, gc0, t2, vm0, rss0, vm, rss, maps, finished)
	END CtxRun;

	(* -------------------- Old: AsyncObject --------------------------------- *)

	PROCEDURE (o: OldObj) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: OldWaitOp DO
			WHILE ~o.flag DO o.Wait END
		| op: OldSetOp DO
			o.flag := TRUE
		END
	END Exclusive;

	PROCEDURE (j: OldJob) Run;
		VAR op: OldWaitOp;
	BEGIN
		j.o.Do(op)
	END Run;

	PROCEDURE OldRun (n, stack: INTEGER);
		VAR i, created, finished: INTEGER; stop: ARRAY 16 OF CHAR;
			vm0, rss0, vm, rss: LONGINT; maps: LONGINT;
			t0, t1, t2, gc0: LONGINT; settle0: LONGINT; set: OldSetOp;
	BEGIN
		created := 0; stop := "none"; finished := 0;
		ReadMem(vm0, rss0);
		NEW(oldObjs, n); NEW(oldJobs, n);
		Kernel.blockCollect := TRUE;
		t0 := System_devClock.now();
		i := 0;
		LOOP
			IF i >= n THEN EXIT END;
			NEW(oldObjs[i]); NEW(oldJobs[i]); oldJobs[i].o := oldObjs[i];
			IF ~Start(oldJobs[i], stack) THEN stop := "stack"; EXIT END;
			INC(created); INC(i);
			IF created MOD checkEvery = 0 THEN
				ReadMem(vm, rss);
				IF rss > rssLimitKb THEN stop := "rss"; EXIT END
			END
		END;
		t1 := System_devClock.now();
		settle0 := System_devClock.now();
		Settle;
		t1 := t1 - t0;
		settle0 := System_devClock.now() - settle0;
		ReadMem(vm, rss); maps := CountMaps();
		gc0 := DoCollect(created);
		t2 := System_devClock.now();
		Kernel.blockCollect := TRUE;
		IF created > 0 THEN
			FOR i := 0 TO created - 1 DO oldObjs[i].Do(set) END
		END;
		Pump(wakeMs);
		FOR i := 0 TO created - 1 DO
			IF Tasks.State(oldJobs[i]) IN {Tasks.Finished, Tasks.Cancelled} THEN INC(finished) END
		END;
		t2 := System_devClock.now() - t2;
		Kernel.blockCollect := FALSE;
		Report("old", n, stack, created, stop, t1, settle0, gc0, t2, vm0, rss0, vm, rss, maps, finished)
	END OldRun;

	(* -------------------- Excl: [exclusive] и AWAIT ----------------------- *)

	PROCEDURE [exclusive] (b: ExBox) Wait, NEW;
	BEGIN
		WHILE ~b.flag DO ACTIVE.AWAIT(b.flag) END
	END Wait;

	PROCEDURE [exclusive] (b: ExBox) Set, NEW;
	BEGIN
		b.flag := TRUE
	END Set;

	PROCEDURE (j: ExJob) Run;
	BEGIN
		j.b.Wait
	END Run;

	PROCEDURE (d: ExDriver) Run;
		VAR i: INTEGER;
	BEGIN
		Tasks.Park;	(* ждём сигнала с главного стека *)
		FOR i := 0 TO d.n - 1 DO exBoxes[i].Set END
	END Run;

	PROCEDURE ExclRun (n, stack: INTEGER);
		VAR i, created, finished: INTEGER; stop: ARRAY 16 OF CHAR;
			vm0, rss0, vm, rss: LONGINT; maps: LONGINT;
			t0, t1, t2, gc0: LONGINT; settle0: LONGINT; d: ExDriver;
	BEGIN
		created := 0; stop := "none"; finished := 0;
		ReadMem(vm0, rss0);
		NEW(exBoxes, n); NEW(exJobs, n);
		NEW(d); d.n := 0;	(* стек драйвера занимаем до отказа карт *)
		IF ~Start(d, stack) THEN stop := "stack" END;
		Kernel.blockCollect := TRUE;
		t0 := System_devClock.now();
		i := 0;
		LOOP
			IF i >= n THEN EXIT END;
			NEW(exBoxes[i]); NEW(exJobs[i]); exJobs[i].b := exBoxes[i];
			IF ~Start(exJobs[i], stack) THEN stop := "stack"; EXIT END;
			INC(created); INC(i);
			IF created MOD checkEvery = 0 THEN
				ReadMem(vm, rss);
				IF rss > rssLimitKb THEN stop := "rss"; EXIT END
			END
		END;
		t1 := System_devClock.now();
		settle0 := System_devClock.now();
		Settle;
		t1 := t1 - t0;
		settle0 := System_devClock.now() - settle0;
		ReadMem(vm, rss); maps := CountMaps();
		gc0 := DoCollect(created);
		t2 := System_devClock.now();
		Kernel.blockCollect := TRUE;
		IF created > 0 THEN
			d.n := created;
			Tasks.Unpark(d)
		END;
		Pump(wakeMs);
		FOR i := 0 TO created - 1 DO
			IF Tasks.State(exJobs[i]) IN {Tasks.Finished, Tasks.Cancelled} THEN INC(finished) END
		END;
		t2 := System_devClock.now() - t2;
		Kernel.blockCollect := FALSE;
		Report("excl", n, stack, created, stop, t1, settle0, gc0, t2, vm0, rss0, vm, rss, maps, finished)
	END ExclRun;

	(* -------------------- Chan: приём из AsyncChan ------------------------ *)

	PROCEDURE (j: ChanJob) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		j.c.Recv(m)
	END Run;

	PROCEDURE ChanRun (n, stack: INTEGER);
		VAR i, created, finished: INTEGER; stop: ARRAY 16 OF CHAR;
			vm0, rss0, vm, rss: LONGINT; maps: LONGINT;
			t0, t1, t2, gc0: LONGINT; settle0: LONGINT; m: NumMsg;
	BEGIN
		created := 0; stop := "none"; finished := 0;
		ReadMem(vm0, rss0);
		NEW(chans, n); NEW(chanJobs, n);
		Kernel.blockCollect := TRUE;
		t0 := System_devClock.now();
		i := 0;
		LOOP
			IF i >= n THEN EXIT END;
			chans[i] := AsyncChan.NewChan(); NEW(chanJobs[i]); chanJobs[i].c := chans[i];
			IF ~Start(chanJobs[i], stack) THEN stop := "stack"; EXIT END;
			INC(created); INC(i);
			IF created MOD checkEvery = 0 THEN
				ReadMem(vm, rss);
				IF rss > rssLimitKb THEN stop := "rss"; EXIT END
			END
		END;
		t1 := System_devClock.now();
		settle0 := System_devClock.now();
		Settle;
		t1 := t1 - t0;
		settle0 := System_devClock.now() - settle0;
		ReadMem(vm, rss); maps := CountMaps();
		gc0 := DoCollect(created);
		t2 := System_devClock.now();
		Kernel.blockCollect := TRUE;	(* сообщения не должны звать сборку на ходу *)
		IF created > 0 THEN
			FOR i := 0 TO created - 1 DO NEW(m); m.n := i; chans[i].Send(m) END
		END;
		Pump(wakeMs);
		FOR i := 0 TO created - 1 DO
			IF Tasks.State(chanJobs[i]) IN {Tasks.Finished, Tasks.Cancelled} THEN INC(finished) END
		END;
		t2 := System_devClock.now() - t2;
		Kernel.blockCollect := FALSE;
		Report("chan", n, stack, created, stop, t1, settle0, gc0, t2, vm0, rss0, vm, rss, maps, finished)
	END ChanRun;

	(* -------------------- Park: Tasks.Park -------------------------------- *)

	PROCEDURE (j: ParkJob) Run;
	BEGIN
		Tasks.Park
	END Run;

	PROCEDURE ParkRun (n, stack: INTEGER);
		VAR i, created, finished: INTEGER; stop: ARRAY 16 OF CHAR;
			vm0, rss0, vm, rss: LONGINT; maps: LONGINT;
			t0, t1, t2, gc0: LONGINT; settle0: LONGINT;
	BEGIN
		created := 0; stop := "none"; finished := 0;
		ReadMem(vm0, rss0);
		NEW(parkJobs, n);
		Kernel.blockCollect := TRUE;
		t0 := System_devClock.now();
		i := 0;
		LOOP
			IF i >= n THEN EXIT END;
			NEW(parkJobs[i]);
			IF ~Start(parkJobs[i], stack) THEN stop := "stack"; EXIT END;
			INC(created); INC(i);
			IF created MOD checkEvery = 0 THEN
				ReadMem(vm, rss);
				IF rss > rssLimitKb THEN stop := "rss"; EXIT END
			END
		END;
		t1 := System_devClock.now();
		settle0 := System_devClock.now();
		Settle;
		t1 := t1 - t0;
		settle0 := System_devClock.now() - settle0;
		ReadMem(vm, rss); maps := CountMaps();
		gc0 := DoCollect(created);
		t2 := System_devClock.now();
		IF created > 0 THEN
			FOR i := 0 TO created - 1 DO Tasks.Unpark(parkJobs[i]) END
		END;
		Pump(wakeMs);
		FOR i := 0 TO created - 1 DO
			IF Tasks.State(parkJobs[i]) IN {Tasks.Finished, Tasks.Cancelled} THEN INC(finished) END
		END;
		t2 := System_devClock.now() - t2;
		Kernel.blockCollect := FALSE;
		Report("park", n, stack, created, stop, t1, settle0, gc0, t2, vm0, rss0, vm, rss, maps, finished)
	END ParkRun;

	(* -------------------- Mon: только объекты ----------------------------- *)

	PROCEDURE MonRun (n: INTEGER);
		VAR i, created: INTEGER; stop: ARRAY 16 OF CHAR;
			a0, a1: LONGINT; vm0, rss0, vm, rss: LONGINT;
	BEGIN
		created := 0; stop := "none";
		ReadMem(vm0, rss0);
		NEW(ctxObjs, n);
		a0 := Kernel.Allocated();
		FOR i := 0 TO n - 1 DO NEW(ctxObjs[i]); INC(created) END;
		a1 := Kernel.Allocated();
		ReadMem(vm, rss);
		W("SCALE mon n="); N(n); W(" created="); N(created); W(" stopped="); W(stop);
		W(" allocDelta="); N(a1 - a0);
		W(" bytesPer=");
		IF created > 0 THEN N((a1 - a0) DIV created) ELSE N(-1) END;
		W(" vmRssKb="); N(rss); W(" rssDeltaKb="); N(rss - rss0);
		W(" vmSizeKb="); N(vm); Ln
	END MonRun;

	(* -------------------- команды ----------------------------------------- *)

	PROCEDURE Ctx*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; CtxRun(n, stack) END Ctx;

	PROCEDURE Old*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; OldRun(n, stack) END Old;

	PROCEDURE Excl*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; ExclRun(n, stack) END Excl;

	PROCEDURE Chan*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; ChanRun(n, stack) END Chan;

	PROCEDURE Park*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; ParkRun(n, stack) END Park;

	PROCEDURE Mon*;
		VAR n, stack, gc: INTEGER;
	BEGIN ReadPars(n, stack, gc); gcMode := gc; MonRun(n) END Mon;

END Async_devBenchScale.
