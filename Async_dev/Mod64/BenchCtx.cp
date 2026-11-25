MODULE Async_devBenchCtx;
(** Замеры монитора-объекта Tasks.Object.

	Сценарии: свободный Do; буфер на флагах и на Cond; цена выхода при
	10/100/1000 ждущих с ложным условием (флаги, Cond, старый
	AsyncObject). Старые сценарии (AsyncObject, [exclusive], AsyncChan) —
	в Async_devBenchObj; прогоны чередуются снаружи, медиана по пяти.

	Строка BENCH: ops, totalNs, nsPerOp, allocsPer1000, switchesPer1000. *)

	IMPORT Kernel, Tasks, AsyncObject, System_devClock, Console, Strings;

	CONST
		freeN = 1000000;
		bufN = 200000;
		cap = 64;
		condN = 1;

	TYPE
		(* --- свободный объект --- *)

		NoOp = RECORD (Tasks.Op) END;
		Box = POINTER TO RECORD (Tasks.Object) n: INTEGER END;
		BoxJob = POINTER TO RECORD (Tasks.Job) b: Box; n: INTEGER; ns: LONGINT END;
		SpinTask = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

		(* --- буфер на флагах --- *)

		PutOp = RECORD (Tasks.Op) v, id: INTEGER END;
		GetOp = RECORD (Tasks.Op) v: INTEGER END;
		FlagBuf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER;
			capacity: INTEGER;
			puts0, puts1: INTEGER;
			pass: BOOLEAN
		END;
		FlagProd = POINTER TO RECORD (Tasks.Job) b: FlagBuf; id, n: INTEGER END;
		FlagCons = POINTER TO RECORD (Tasks.Job) b: FlagBuf; n: INTEGER END;

		(* --- буфер на Cond --- *)

		NotEmpty = POINTER TO RECORD (Tasks.Cond) END;
		NotFull = POINTER TO RECORD (Tasks.Cond) END;
		CondBuf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER;
			capacity: INTEGER;
			puts0, puts1: INTEGER;
			pass: BOOLEAN;
			ne: NotEmpty; nf: NotFull
		END;
		CondProd = POINTER TO RECORD (Tasks.Job) b: CondBuf; id, n: INTEGER END;
		CondCons = POINTER TO RECORD (Tasks.Job) b: CondBuf; n: INTEGER END;

		(* --- ждущие с ложным условием: новый монитор --- *)

		WaitOp = RECORD (Tasks.Op) c: FalseCond END;
		FalseCond = POINTER TO RECORD (Tasks.Cond) END;
		WaitObj = POINTER TO RECORD (Tasks.Object) pass: BOOLEAN END;
		WaitJob = POINTER TO RECORD (Tasks.Job) o: WaitObj; c: FalseCond END;
		DriveJob = POINTER TO RECORD (Tasks.Job)
			o: WaitObj; n: INTEGER; ns, allocs, switches: LONGINT
		END;

		(* --- то же на старом AsyncObject --- *)

		OldWaitOp = RECORD (AsyncObject.Op) END;
		OldNoOp = RECORD (AsyncObject.Op) END;
		OldObj = POINTER TO RECORD (AsyncObject.Object) END;
		OldWaitJob = POINTER TO RECORD (Tasks.Task) o: OldObj END;
		OldDriveTask = POINTER TO RECORD (Tasks.Task)
			o: OldObj; n: INTEGER; ns, allocs, switches: LONGINT
		END;

	VAR
		sum: INTEGER;
		a0, s0: LONGINT;
		driveNs, driveAllocs, driveSwitches: LONGINT;
		driveN: INTEGER;

	(* -------------------- вывод ------------------------------------------- *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ops: INTEGER; ns: LONGINT; allocs, switches: LONGINT);
	BEGIN
		W("BENCH "); W(name); W(" ops="); N(ops); W(" totalNs="); N(ns);
		W(" nsPerOp="); N(ns DIV ops);
		W(" allocsPer1000="); N(allocs * 1000 DIV ops);
		W(" switchesPer1000="); N(switches * 1000 DIV ops);
		Console.WriteLn
	END Report;

	PROCEDURE Now (): LONGINT;
	BEGIN
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches();
		RETURN System_devClock.now()
	END Now;

	PROCEDURE Finish (IN name: ARRAY OF CHAR; ops: INTEGER; t0: LONGINT);
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Report(name, ops, System_devClock.now() - t0, Kernel.AllocCount() - a0,
			Tasks.Switches() - s0)
	END Finish;

	PROCEDURE Start (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, 128 * 1024); Tasks.Start(t)
	END Start;

	PROCEDURE PumpUntil (t: Tasks.Task; ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.State(t) # Tasks.Finished) & (Tasks.State(t) # Tasks.Cancelled)
				& (Kernel.Time() - t0 <= ms) DO
			Tasks.Step
		END;
		RETURN Tasks.State(t) IN {Tasks.Finished, Tasks.Cancelled}
	END PumpUntil;

	(* -------------------- свободный объект -------------------------------- *)

	PROCEDURE (b: Box) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: NoOp DO INC(b.n) END
	END Exclusive;

	PROCEDURE (t: SpinTask) Run;
	BEGIN
		INC(t.n)
	END Run;

	PROCEDURE Drain;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Drain;

	PROCEDURE NewTasks10k*;
	(* Создание, запуск и завершение 10 тыс. задач: каждый раз NEW. *)
		VAR i: INTEGER; t: SpinTask; t0: LONGINT; a0, s0: LONGINT;
	BEGIN
		Drain;
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches(); t0 := System_devClock.now();
		FOR i := 1 TO 10000 DO
			NEW(t); Tasks.SetStackSize(t, 128 * 1024); Tasks.Start(t); Drain
		END;
		Report("restart-new", 10000, System_devClock.now() - t0,
			Kernel.AllocCount() - a0, Tasks.Switches() - s0)
	END NewTasks10k;

	PROCEDURE RestartTasks10k*;
	(* Те же 10 тыс. прогонов, но задача создана один раз. *)
		VAR i: INTEGER; t: SpinTask; t0: LONGINT; a0, s0: LONGINT;
	BEGIN
		NEW(t); Tasks.SetStackSize(t, 128 * 1024);
		FOR i := 1 TO 8 DO Tasks.Start(t); Drain END;	(* прогрев стека и записи *)
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches(); t0 := System_devClock.now();
		FOR i := 1 TO 10000 DO Tasks.Start(t); Drain END;
		Report("restart-same", 10000, System_devClock.now() - t0,
			Kernel.AllocCount() - a0, Tasks.Switches() - s0);
		W("RUNS "); N(t.n); Console.WriteLn
	END RestartTasks10k;

	PROCEDURE (j: BoxJob) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: NoOp;
	BEGIN
		FOR i := 1 TO 8 DO j.b.Do(ctx, op) END;	(* прогрев записей *)
		driveNs := 0; driveAllocs := 0; driveSwitches := 0;
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches();
		driveNs := System_devClock.now();
		FOR i := 1 TO j.n DO j.b.Do(ctx, op) END;
		driveNs := System_devClock.now() - driveNs;
		driveAllocs := Kernel.AllocCount() - a0;
		driveSwitches := Tasks.Switches() - s0;
		sum := j.n
	END Body;

	PROCEDURE DoFree*;
		VAR b: Box; j: BoxJob;
	BEGIN
		NEW(b); NEW(j); j.b := b; j.n := freeN;
		Start(j);
		IF PumpUntil(j, 10000) THEN END;
		Report("do-free", freeN, driveNs, driveAllocs, driveSwitches)
	END DoFree;

	(* -------------------- буфер на флагах --------------------------------- *)

	PROCEDURE (b: FlagBuf) TransferOnRelease (): BOOLEAN;
	BEGIN
		RETURN b.pass
	END TransferOnRelease;

	PROCEDURE (b: FlagBuf) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < b.capacity THEN INCL(s, 0) END;
		IF b.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (b: FlagBuf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = b.capacity DO
				IF ~b.Wait(ctx, {0}) THEN RETURN END
			END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD b.capacity; INC(b.count);
			IF op.id = 1 THEN INC(b.puts1) ELSE INC(b.puts0) END
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Wait(ctx, {1}) THEN RETURN END
			END;
			op.v := b.ring[b.head];
			b.head := (b.head + 1) MOD b.capacity; DEC(b.count)
		END
	END Exclusive;

	PROCEDURE (j: FlagProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO j.n DO op.v := i; op.id := j.id; j.b.Do(ctx, op) END
	END Body;

	PROCEDURE (j: FlagCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO j.n DO j.b.Do(ctx, op); sum := op.v END
	END Body;

	PROCEDURE FlagMode (pass: BOOLEAN; capacity, n: INTEGER; IN name: ARRAY OF CHAR);
		VAR b: FlagBuf; p: FlagProd; c: FlagCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		b.capacity := capacity; b.pass := pass; b.puts0 := 0; b.puts1 := 0;
		NEW(p); p.b := b; p.id := 0; p.n := n; NEW(c); c.b := b; c.n := n;
		t0 := Now(); Start(p); Start(c); Finish(name, n, t0)
	END FlagMode;

	PROCEDURE BufferFlagMode (pass: BOOLEAN; IN name: ARRAY OF CHAR);
	BEGIN FlagMode(pass, cap, bufN, name) END BufferFlagMode;

	PROCEDURE BufferFlag*;
	BEGIN BufferFlagMode(FALSE, "buffer-flag") END BufferFlag;
	PROCEDURE BufferFlagPass*;
	BEGIN BufferFlagMode(TRUE, "buffer-flag-pass") END BufferFlagPass;
	PROCEDURE BufferFlagWake*;
	BEGIN BufferFlagMode(FALSE, "buffer-flag-wake") END BufferFlagWake;
	PROCEDURE Buffer1FlagPass*;
	BEGIN FlagMode(TRUE, 1, bufN, "buffer1-flag-pass") END Buffer1FlagPass;
	PROCEDURE Buffer1FlagWake*;
	BEGIN FlagMode(FALSE, 1, bufN, "buffer1-flag-wake") END Buffer1FlagWake;

	PROCEDURE FairFlag (pass: BOOLEAN; IN name: ARRAY OF CHAR);
		VAR b: FlagBuf; p0, p1: FlagProd; c: FlagCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		b.capacity := cap; b.pass := pass; b.puts0 := 0; b.puts1 := 0;
		NEW(p0); p0.b := b; p0.id := 0; p0.n := bufN DIV 2;
		NEW(p1); p1.b := b; p1.id := 1; p1.n := bufN DIV 2;
		NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Start(p0); Start(p1); Start(c);
		Finish(name, bufN, t0);
		W("FAIR "); W(name); W(" put0="); N(b.puts0); W(" put1="); N(b.puts1);
		Console.WriteLn
	END FairFlag;

	PROCEDURE FairFlagPass*;
	BEGIN FairFlag(TRUE, "fair-flag-pass") END FairFlagPass;
	PROCEDURE FairFlagWake*;
	BEGIN FairFlag(FALSE, "fair-flag-wake") END FairFlagWake;

	(* -------------------- буфер на Cond ----------------------------------- *)

	PROCEDURE (c: NotEmpty) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf); RETURN b.count > 0
	END Ready;

	PROCEDURE (c: NotFull) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf); RETURN b.count < b.capacity
	END Ready;

	PROCEDURE (b: CondBuf) TransferOnRelease (): BOOLEAN;
	BEGIN
		RETURN b.pass
	END TransferOnRelease;

	PROCEDURE (b: CondBuf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE ~b.nf.Ready(b) DO
				IF ~b.WaitFor(ctx, b.nf) THEN RETURN END
			END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD b.capacity; INC(b.count);
			IF op.id = 1 THEN INC(b.puts1) ELSE INC(b.puts0) END
		| op: GetOp DO
			WHILE ~b.ne.Ready(b) DO
				IF ~b.WaitFor(ctx, b.ne) THEN RETURN END
			END;
			op.v := b.ring[b.head];
			b.head := (b.head + 1) MOD b.capacity; DEC(b.count)
		END
	END Exclusive;

	PROCEDURE (j: CondProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO j.n DO op.v := i; op.id := j.id; j.b.Do(ctx, op) END
	END Body;

	PROCEDURE (j: CondCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO j.n DO j.b.Do(ctx, op); sum := op.v END
	END Body;

	PROCEDURE CondMode (pass: BOOLEAN; capacity, n: INTEGER; IN name: ARRAY OF CHAR);
		VAR b: CondBuf; p: CondProd; c: CondCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		b.capacity := capacity; b.pass := pass; b.puts0 := 0; b.puts1 := 0;
		NEW(b.ne); NEW(b.nf);
		NEW(p); p.b := b; p.id := 0; p.n := n; NEW(c); c.b := b; c.n := n;
		t0 := Now(); Start(p); Start(c); Finish(name, n, t0)
	END CondMode;

	PROCEDURE BufferCond*;
	BEGIN CondMode(FALSE, cap, bufN, "buffer-cond") END BufferCond;
	PROCEDURE BufferCondPass*;
	BEGIN CondMode(TRUE, cap, bufN, "buffer-cond-pass") END BufferCondPass;
	PROCEDURE BufferCondWake*;
	BEGIN CondMode(FALSE, cap, bufN, "buffer-cond-wake") END BufferCondWake;
	PROCEDURE Buffer1CondPass*;
	BEGIN CondMode(TRUE, 1, bufN, "buffer1-cond-pass") END Buffer1CondPass;
	PROCEDURE Buffer1CondWake*;
	BEGIN CondMode(FALSE, 1, bufN, "buffer1-cond-wake") END Buffer1CondWake;

	PROCEDURE FairCond (pass: BOOLEAN; IN name: ARRAY OF CHAR);
		VAR b: CondBuf; p0, p1: CondProd; c: CondCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		b.capacity := cap; b.pass := pass; b.puts0 := 0; b.puts1 := 0;
		NEW(b.ne); NEW(b.nf);
		NEW(p0); p0.b := b; p0.id := 0; p0.n := bufN DIV 2;
		NEW(p1); p1.b := b; p1.id := 1; p1.n := bufN DIV 2;
		NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Start(p0); Start(p1); Start(c);
		Finish(name, bufN, t0);
		W("FAIR "); W(name); W(" put0="); N(b.puts0); W(" put1="); N(b.puts1);
		Console.WriteLn
	END FairCond;

	PROCEDURE FairCondPass*;
	BEGIN FairCond(TRUE, "fair-cond-pass") END FairCondPass;
	PROCEDURE FairCondWake*;
	BEGIN FairCond(FALSE, "fair-cond-wake") END FairCondWake;

	(* -------------------- ждущие с ложным условием: новый ----------------- *)

	PROCEDURE (c: FalseCond) Ready (o: Tasks.Object): BOOLEAN;
	BEGIN
		RETURN FALSE
	END Ready;

	PROCEDURE (o: WaitObj) TransferOnRelease (): BOOLEAN;
	BEGIN
		RETURN o.pass
	END TransferOnRelease;

	PROCEDURE (o: WaitObj) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: WaitOp DO
			IF op.c = NIL THEN
				LOOP
					IF ~o.Wait(ctx, {0}) THEN RETURN END
				END
			ELSE
				LOOP
					IF ~o.WaitFor(ctx, op.c) THEN RETURN END
				END
			END
		| op: NoOp DO
		END
	END Exclusive;

	PROCEDURE (j: WaitJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WaitOp;
	BEGIN
		op.c := j.c;
		j.o.Do(ctx, op)
	END Body;

	PROCEDURE (j: DriveJob) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: NoOp;
	BEGIN
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches();
		driveNs := System_devClock.now();
		FOR i := 1 TO j.n DO j.o.Do(ctx, op) END;
		driveNs := System_devClock.now() - driveNs;
		driveAllocs := Kernel.AllocCount() - a0;
		driveSwitches := Tasks.Switches() - s0
	END Body;

	PROCEDURE ExitWait (pass, useCond: BOOLEAN; waiters, exits: INTEGER; IN name: ARRAY OF CHAR);
		VAR o: WaitObj; w: WaitJob; d: DriveJob; i: INTEGER; t0: LONGINT; ok: BOOLEAN;
	BEGIN
		NEW(o); o.pass := pass;
		FOR i := 1 TO waiters DO
			NEW(w); w.o := o;
			IF useCond THEN NEW(w.c) END;
			Start(w)
		END;
		(* все ждущие встали в очередь *)
		t0 := Kernel.Time();
		WHILE (Tasks.Queued(o) < waiters) & (Kernel.Time() - t0 <= 5000) DO Tasks.Step END;
		ok := Tasks.Queued(o) = waiters;
		NEW(d); d.o := o; d.n := exits;
		Start(d);
		WHILE (Tasks.State(d) # Tasks.Finished) & (Tasks.State(d) # Tasks.Cancelled) DO Tasks.Step END;
		Report(name, exits, driveNs, driveAllocs, driveSwitches);
		IF ~ok THEN W("BENCH setup-failed"); Console.WriteLn END
	END ExitWait;

	PROCEDURE ExitFlag10*;
	BEGIN ExitWait(TRUE, FALSE, 10, 100000, "exit-flag-10-pass") END ExitFlag10;
	PROCEDURE ExitFlag100*;
	BEGIN ExitWait(TRUE, FALSE, 100, 20000, "exit-flag-100-pass") END ExitFlag100;
	PROCEDURE ExitFlag1000*;
	BEGIN ExitWait(TRUE, FALSE, 1000, 2000, "exit-flag-1000-pass") END ExitFlag1000;
	PROCEDURE ExitCond10*;
	BEGIN ExitWait(TRUE, TRUE, 10, 100000, "exit-cond-10-pass") END ExitCond10;
	PROCEDURE ExitCond100*;
	BEGIN ExitWait(TRUE, TRUE, 100, 20000, "exit-cond-100-pass") END ExitCond100;
	PROCEDURE ExitCond1000*;
	BEGIN ExitWait(TRUE, TRUE, 1000, 2000, "exit-cond-1000-pass") END ExitCond1000;
	PROCEDURE ExitFlag10Wake*;
	BEGIN ExitWait(FALSE, FALSE, 10, 100000, "exit-flag-10-wake") END ExitFlag10Wake;
	PROCEDURE ExitFlag100Wake*;
	BEGIN ExitWait(FALSE, FALSE, 100, 20000, "exit-flag-100-wake") END ExitFlag100Wake;
	PROCEDURE ExitFlag1000Wake*;
	BEGIN ExitWait(FALSE, FALSE, 1000, 2000, "exit-flag-1000-wake") END ExitFlag1000Wake;
	PROCEDURE ExitCond10Wake*;
	BEGIN ExitWait(FALSE, TRUE, 10, 100000, "exit-cond-10-wake") END ExitCond10Wake;
	PROCEDURE ExitCond100Wake*;
	BEGIN ExitWait(FALSE, TRUE, 100, 20000, "exit-cond-100-wake") END ExitCond100Wake;
	PROCEDURE ExitCond1000Wake*;
	BEGIN ExitWait(FALSE, TRUE, 1000, 2000, "exit-cond-1000-wake") END ExitCond1000Wake;

	(* -------------------- то же на старом AsyncObject --------------------- *)

	PROCEDURE (o: OldObj) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: OldWaitOp DO
			LOOP
				o.Wait
			END
		| op: OldNoOp DO
		END
	END Exclusive;

	PROCEDURE (j: OldWaitJob) Run;
		VAR op: OldWaitOp;
	BEGIN
		j.o.Do(op)
	END Run;

	PROCEDURE (j: OldDriveTask) Run;
		VAR i: INTEGER; op: OldNoOp; warm: OldObj;
	BEGIN
		NEW(warm); warm.Do(op);	(* прогрев записи ожидания *)
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches();
		driveNs := System_devClock.now();
		FOR i := 1 TO j.n DO j.o.Do(op) END;
		driveNs := System_devClock.now() - driveNs;
		driveAllocs := Kernel.AllocCount() - a0;
		driveSwitches := Tasks.Switches() - s0
	END Run;

	PROCEDURE ExitOld (waiters: INTEGER; IN name: ARRAY OF CHAR);
	(* Старый монитор на выходе будит всех. После первого выхода очередь
	   пуста (разбуженные ещё не успели снова заснуть), поэтому меряем
	   ровно один выход при waiters ждущих в очереди. *)
		VAR o: OldObj; w: OldWaitJob; d: OldDriveTask; i: INTEGER; t0: LONGINT;
	BEGIN
		NEW(o);
		FOR i := 1 TO waiters DO NEW(w); w.o := o; Start(w) END;
		t0 := Kernel.Time();
		WHILE (AsyncObject.Queued(o) < waiters) & (Kernel.Time() - t0 <= 5000) DO Tasks.Step END;
		NEW(d); d.o := o; d.n := 1;
		Start(d);
		WHILE (Tasks.State(d) # Tasks.Finished) & (Tasks.State(d) # Tasks.Cancelled) DO Tasks.Step END;
		Report(name, 1, driveNs, driveAllocs, driveSwitches)
	END ExitOld;

	PROCEDURE ExitOld10*;
	BEGIN ExitOld(10, "exit-old-10") END ExitOld10;
	PROCEDURE ExitOld100*;
	BEGIN ExitOld(100, "exit-old-100") END ExitOld100;
	PROCEDURE ExitOld1000*;
	BEGIN ExitOld(1000, "exit-old-1000") END ExitOld1000;

END Async_devBenchCtx.
