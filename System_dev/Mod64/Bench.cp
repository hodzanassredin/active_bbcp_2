MODULE System_devBench;
(** Замеры задач и монитора-объекта Tasks.Object (не входят в tools/test.sh).

	Строка BENCH: ops, totalNs, nsPerOp, allocsPer1000, switchesPer1000.
	Главный стек крутит Tasks.Step.

	Команды:
	- DoFree       — свободный Do на объекте-мониторе, 1 млн раз;
	- BufferFlag   — буфер на 64 на флагах Ready, без передачи монитора;
	- BufferFlagPass — тот же буфер с TransferOnRelease;
	- NewTasks10k  — создание, запуск и завершение 10 тыс. задач;
	- RestartTasks10k — те же 10 тыс. прогонов одной и той же задачи;
	- Yield        — чистое переключение двух задач, 1 млн уступок.

	Сборка мусора при 10/50/100 тыс. задач — System_devGcStacks. *)

	IMPORT Kernel, Tasks, System_devClock, Console, Strings;

	CONST
		freeN = 1000000;
		bufN = 200000;
		cap = 64;
		million = 1000000;
		stack = 128 * 1024;

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

		(* --- переключение --- *)

		Yielder = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

	VAR
		sum: INTEGER;
		a0, s0: LONGINT;
		driveNs, driveAllocs, driveSwitches: LONGINT;

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
		Tasks.SetStackSize(t, stack); Tasks.Start(t)
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

	PROCEDURE Drain;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Drain;

	(* -------------------- свободный объект -------------------------------- *)

	PROCEDURE (b: Box) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: NoOp DO INC(b.n) END
	END Exclusive;

	PROCEDURE (t: SpinTask) Run;
	BEGIN
		INC(t.n)
	END Run;

	PROCEDURE NewTasks10k*;
	(* Создание, запуск и завершение 10 тыс. задач: каждый раз NEW. *)
		VAR i: INTEGER; t: SpinTask; t0: LONGINT; a0, s0: LONGINT;
	BEGIN
		Drain;
		a0 := Kernel.AllocCount(); s0 := Tasks.Switches(); t0 := System_devClock.now();
		FOR i := 1 TO 10000 DO
			NEW(t); Tasks.SetStackSize(t, stack); Tasks.Start(t); Drain
		END;
		Report("restart-new", 10000, System_devClock.now() - t0,
			Kernel.AllocCount() - a0, Tasks.Switches() - s0)
	END NewTasks10k;

	PROCEDURE RestartTasks10k*;
	(* Те же 10 тыс. прогонов, но задача создана один раз. *)
		VAR i: INTEGER; t: SpinTask; t0: LONGINT; a0, s0: LONGINT;
	BEGIN
		NEW(t); Tasks.SetStackSize(t, stack);
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
				IF ~b.Await(ctx, {0}) THEN RETURN END
			END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD b.capacity; INC(b.count);
			IF op.id = 1 THEN INC(b.puts1) ELSE INC(b.puts0) END
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Await(ctx, {1}) THEN RETURN END
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

	PROCEDURE FlagMode (pass: BOOLEAN; IN name: ARRAY OF CHAR);
		VAR b: FlagBuf; p: FlagProd; c: FlagCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		b.capacity := cap; b.pass := pass; b.puts0 := 0; b.puts1 := 0;
		NEW(p); p.b := b; p.id := 0; p.n := bufN; NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Start(p); Start(c); Finish(name, bufN, t0)
	END FlagMode;

	PROCEDURE BufferFlag*;
	BEGIN FlagMode(FALSE, "buffer-flag") END BufferFlag;

	PROCEDURE BufferFlagPass*;
	BEGIN FlagMode(TRUE, "buffer-flag-pass") END BufferFlagPass;

	(* -------------------- чистое переключение ------------------------------ *)

	PROCEDURE (t: Yielder) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO Tasks.Yield END;
		INC(sum, t.n)
	END Run;

	PROCEDURE Yield*;
		VAR a, b: Yielder; t0: LONGINT;
	BEGIN
		sum := 0;
		NEW(a); NEW(b); a.n := million DIV 2; b.n := million DIV 2;
		t0 := Now();
		Start(a); Start(b);
		Finish("yield", million, t0)
	END Yield;

END System_devBench.
