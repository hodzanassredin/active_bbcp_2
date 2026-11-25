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

		(* --- буфер на флагах --- *)

		PutOp = RECORD (Tasks.Op) v: INTEGER END;
		GetOp = RECORD (Tasks.Op) v: INTEGER END;
		FlagBuf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER
		END;
		FlagProd = POINTER TO RECORD (Tasks.Job) b: FlagBuf; n: INTEGER END;
		FlagCons = POINTER TO RECORD (Tasks.Job) b: FlagBuf; n: INTEGER END;

		(* --- буфер на Cond --- *)

		NotEmpty = POINTER TO RECORD (Tasks.Cond) END;
		NotFull = POINTER TO RECORD (Tasks.Cond) END;
		CondBuf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER;
			ne: NotEmpty; nf: NotFull
		END;
		CondProd = POINTER TO RECORD (Tasks.Job) b: CondBuf; n: INTEGER END;
		CondCons = POINTER TO RECORD (Tasks.Job) b: CondBuf; n: INTEGER END;

		(* --- ждущие с ложным условием: новый монитор --- *)

		WaitOp = RECORD (Tasks.Op) c: FalseCond END;
		FalseCond = POINTER TO RECORD (Tasks.Cond) END;
		WaitObj = POINTER TO RECORD (Tasks.Object) END;
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

	PROCEDURE (b: FlagBuf) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < cap THEN INCL(s, 0) END;
		IF b.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (b: FlagBuf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO
				IF ~b.Wait(ctx, {0}) THEN RETURN END
			END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Wait(ctx, {1}) THEN RETURN END
			END;
			op.v := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		END
	END Exclusive;

	PROCEDURE (j: FlagProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO j.n DO op.v := i; j.b.Do(ctx, op) END
	END Body;

	PROCEDURE (j: FlagCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO j.n DO j.b.Do(ctx, op); sum := op.v END
	END Body;

	PROCEDURE BufferFlag*;
		VAR b: FlagBuf; p: FlagProd; c: FlagCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		NEW(p); p.b := b; p.n := bufN; NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Start(p); Start(c); Finish("buffer-flag", bufN, t0)
	END BufferFlag;

	(* -------------------- буфер на Cond ----------------------------------- *)

	PROCEDURE (c: NotEmpty) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf); RETURN b.count > 0
	END Ready;

	PROCEDURE (c: NotFull) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf); RETURN b.count < cap
	END Ready;

	PROCEDURE (b: CondBuf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE ~b.nf.Ready(b) DO
				IF ~b.WaitFor(ctx, b.nf) THEN RETURN END
			END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE ~b.ne.Ready(b) DO
				IF ~b.WaitFor(ctx, b.ne) THEN RETURN END
			END;
			op.v := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		END
	END Exclusive;

	PROCEDURE (j: CondProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO j.n DO op.v := i; j.b.Do(ctx, op) END
	END Body;

	PROCEDURE (j: CondCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO j.n DO j.b.Do(ctx, op); sum := op.v END
	END Body;

	PROCEDURE BufferCond*;
		VAR b: CondBuf; p: CondProd; c: CondCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		NEW(b.ne); NEW(b.nf);
		NEW(p); p.b := b; p.n := bufN; NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Start(p); Start(c); Finish("buffer-cond", bufN, t0)
	END BufferCond;

	(* -------------------- ждущие с ложным условием: новый ----------------- *)

	PROCEDURE (c: FalseCond) Ready (o: Tasks.Object): BOOLEAN;
	BEGIN
		RETURN FALSE
	END Ready;

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

	PROCEDURE ExitWait (useCond: BOOLEAN; waiters, exits: INTEGER; IN name: ARRAY OF CHAR);
		VAR o: WaitObj; w: WaitJob; d: DriveJob; i: INTEGER; t0: LONGINT; ok: BOOLEAN;
	BEGIN
		NEW(o);
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
	BEGIN ExitWait(FALSE, 10, 100000, "exit-flag-10") END ExitFlag10;
	PROCEDURE ExitFlag100*;
	BEGIN ExitWait(FALSE, 100, 20000, "exit-flag-100") END ExitFlag100;
	PROCEDURE ExitFlag1000*;
	BEGIN ExitWait(FALSE, 1000, 2000, "exit-flag-1000") END ExitFlag1000;
	PROCEDURE ExitCond10*;
	BEGIN ExitWait(TRUE, 10, 100000, "exit-cond-10") END ExitCond10;
	PROCEDURE ExitCond100*;
	BEGIN ExitWait(TRUE, 100, 20000, "exit-cond-100") END ExitCond100;
	PROCEDURE ExitCond1000*;
	BEGIN ExitWait(TRUE, 1000, 2000, "exit-cond-1000") END ExitCond1000;

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
