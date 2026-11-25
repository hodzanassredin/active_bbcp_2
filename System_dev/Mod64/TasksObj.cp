MODULE System_devTasksObj;
(** Тесты монитора-объекта и пропуска (Tasks.Object, Tasks.Job, Tasks.Ctx).

	Проверяется: взаимное исключение и FIFO очередей; буфер на флагах
	Ready и на условии-объекте Cond; повторный вход и отпускание на
	глубине 1; трап Wait на глубине 2; чужие мониторы в руках (запрет и
	разрешение); непойманный трап отпускает все мониторы, пойманный через
	Kernel.Try — только внутренние; отмена и срок при ожидании входа и в
	Wait; Spawn/JoinAll; выделения на операцию. *)

	IMPORT S := SYSTEM, Kernel, Tasks, UnitAssert;

	CONST
		producers = 3;
		consumers = 2;
		per = 20;
		cap = 3;

	TYPE
		(* --- запросы ------------------------------------------------------ *)

		EnterOp = RECORD (Tasks.Op) END;
		SleepOp = RECORD (Tasks.Op) ms: LONGINT END;
		SetOp = RECORD (Tasks.Op) v: BOOLEAN END;
		WaitOp = RECORD (Tasks.Op)
			id, note: INTEGER;
			result, second, fast, cancelled, expired: BOOLEAN;
			depth, queued: INTEGER
		END;
		NoteOp = RECORD (Tasks.Op) id: INTEGER END;
		CondWaitOp = RECORD (Tasks.Op) c: Tasks.Cond; id: INTEGER END;
		NeedOp = RECORD (Tasks.Op) need: SET; id: INTEGER END;
		NestOp = RECORD (Tasks.Op) depth1, depth3, innerDepth: INTEGER END;
		InnerOp = RECORD (Tasks.Op) depth: INTEGER; seen: BOOLEAN END;
		HoldOp = RECORD (Tasks.Op) result: BOOLEAN END;
		OuterOp = RECORD (Tasks.Op) END;
		CaughtOp = RECORD (Tasks.Op) tryIt: BOOLEAN; heldAfter: INTEGER; result: BOOLEAN END;
		CrashOp = RECORD (Tasks.Op) END;
		ProbeOp = RECORD (Tasks.Op) END;
		JoinOp = RECORD (Tasks.Op) child: Tasks.Task END;
		CrossOp = RECORD (Tasks.Op) y: Lock END;

		(* --- объект-монитор с флагами ------------------------------------- *)

		Lock = POINTER TO RECORD (Tasks.Object)
			inside, maxInside, entries: INTEGER;
			flag, flag1, allow, useY, deepWait: BOOLEAN;
			y: Lock
		END;

		YesCond = POINTER TO RECORD (Tasks.Cond) yes: BOOLEAN END;

		(* --- буфер на флагах ---------------------------------------------- *)

		PutOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) item: INTEGER END;

		Buf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		(* --- буфер на Cond ------------------------------------------------ *)

		NotEmpty = POINTER TO RECORD (Tasks.Cond) END;
		NotFull = POINTER TO RECORD (Tasks.Cond) END;

		CondBuf = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER;
			notEmpty: NotEmpty;
			notFull: NotFull
		END;

		CondPutOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		CondGetOp = RECORD (Tasks.Op) item: INTEGER END;

		(* --- задачи ------------------------------------------------------- *)

		EnterJob = POINTER TO RECORD (Tasks.Job) x: Lock; done: BOOLEAN END;
		SleepHoldJob = POINTER TO RECORD (Tasks.Job) x: Lock; ms: LONGINT; done: BOOLEAN END;
		JoinChild = POINTER TO RECORD (Tasks.Task) ms: LONGINT END;
		JoinHoldJob = POINTER TO RECORD (Tasks.Job) x: Lock; child: Tasks.Task; done: BOOLEAN END;
		CrossJob = POINTER TO RECORD (Tasks.Job) x, y: Lock; done: BOOLEAN END;
		SetJob = POINTER TO RECORD (Tasks.Job) x: Lock; v: BOOLEAN END;
		WaitJob = POINTER TO RECORD (Tasks.Job) x: Lock; op: WaitOp END;
		CondWaitJob = POINTER TO RECORD (Tasks.Job) x: Lock; op: CondWaitOp END;
		NeedJob = POINTER TO RECORD (Tasks.Job) x: Lock; op: NeedOp END;
		OrderJob = POINTER TO RECORD (Tasks.Job) x: Lock; id: INTEGER END;
		NestJob = POINTER TO RECORD (Tasks.Job) x: Lock; n: NestOp END;
		OuterJob = POINTER TO RECORD (Tasks.Job) x: Lock END;
		CaughtJob = POINTER TO RECORD (Tasks.Job) x: Lock; op: CaughtOp END;
		StopEntryJob = POINTER TO RECORD (Tasks.Job)
			x: Lock; deadline: LONGINT; done, cancelled, expired: BOOLEAN
		END;
		DeadlineJob = POINTER TO RECORD (Tasks.Job) x: Lock; op: WaitOp; expired: BOOLEAN END;
		AllocJob = POINTER TO RECORD (Tasks.Job) x: Lock END;
		ProbeJob = POINTER TO RECORD (Tasks.Job) x: Lock END;
		SpawnJob = POINTER TO RECORD (Tasks.Job) n: INTEGER END;
		ChildJob = POINTER TO RECORD (Tasks.Job) END;
		BufProd = POINTER TO RECORD (Tasks.Job) b: Buf; id, n: INTEGER END;
		BufCons = POINTER TO RECORD (Tasks.Job) b: Buf; n: INTEGER END;
		CondProd = POINTER TO RECORD (Tasks.Job) b: CondBuf; id, n: INTEGER END;
		CondCons = POINTER TO RECORD (Tasks.Job) b: CondBuf; n: INTEGER END;

		(* --- задачи без пропуска (управление) ------------------------------ *)

		Unparker = POINTER TO RECORD (Tasks.Task) t: Tasks.Task END;
		CancelTask = POINTER TO RECORD (Tasks.Task) t: Tasks.Task; ms: LONGINT END;

		CtxPtr = POINTER TO Tasks.Ctx;

	VAR
		waits, orderN, spawnDone, childDone: INTEGER;
		order: ARRAY 16 OF INTEGER;
		maxInside: INTEGER;
		failed, bodyDone: BOOLEAN;
		caughtSeen: BOOLEAN;
		bufBroken, condBroken: BOOLEAN;
		bufCount, condCount: INTEGER;
		consStopped: BOOLEAN;
		nextSeq, got: ARRAY producers OF INTEGER;
		readyCalls: INTEGER;
		allocDelta: LONGINT;
		(* новые пробы: сон, Join и вход в чужой монитор под монитором *)
		sleepDone, joinDone: INTEGER;
		(* трап внутри Exclusive: обработчик Kernel.Try получает адрес Ctx *)
		trapY: Lock; trapCtx: S.ADDRESS;

	(* -------------------- вспомогательное -------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Start (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, 128 * 1024);
		Tasks.Start(t)
	END Start;

	PROCEDURE NilCrash;
		VAR p: Lock;
	BEGIN
		p := NIL; p.entries := 1
	END NilCrash;

	PROCEDURE ResetCase;
		VAR i: INTEGER;
	BEGIN
		waits := 0; orderN := 0; spawnDone := 0; childDone := 0;
		maxInside := 0; readyCalls := 0;
		failed := FALSE; bodyDone := FALSE; caughtSeen := FALSE;
		bufBroken := FALSE; condBroken := FALSE; bufCount := 0; condCount := 0;
		allocDelta := -1; trapY := NIL; trapCtx := 0;
		sleepDone := 0; joinDone := 0;
		consStopped := FALSE;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END;
		FOR i := 0 TO LEN(order) - 1 DO order[i] := 0 END
	END ResetCase;

	PROCEDURE Setup*;
	BEGIN
		ResetCase
	END Setup;

	PROCEDURE Note (id: INTEGER);
	BEGIN
		IF orderN < LEN(order) THEN order[orderN] := id; INC(orderN) END
	END Note;

	(* -------------------- Lock -------------------------------------------- *)

	PROCEDURE (x: Lock) Ready (): SET;
		VAR s: SET;
	BEGIN
		INC(readyCalls);
		s := {};
		IF x.flag THEN INCL(s, 0) END;
		IF x.flag1 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (x: Lock) HoldsWhileWaiting (): BOOLEAN;
	BEGIN
		RETURN x.allow
	END HoldsWhileWaiting;

	PROCEDURE (c: YesCond) Ready (o: Tasks.Object): BOOLEAN;
	BEGIN
		RETURN c.yes
	END Ready;

	PROCEDURE InnerTrap (adr, b, c: S.ADDRESS);
	(* Обработчик Kernel.Try: внешний объект берётся в InnerTrap. *)
		VAR p: CtxPtr; y: Lock; crash: CrashOp;
	BEGIN
		p := S.VAL(CtxPtr, adr);
		y := trapY;
		y.Do(p^, crash)
	END InnerTrap;

	PROCEDURE (x: Lock) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR inner: InnerOp; hold: HoldOp; visit: EnterOp; deep: BOOLEAN; s0, s1: LONGINT;
	BEGIN
		WITH op: EnterOp DO
			INC(x.entries); INC(x.inside);
			IF x.inside > maxInside THEN maxInside := x.inside END;
			Tasks.Sleep(4);
			DEC(x.inside)
		| op: SleepOp DO
			INC(x.entries); INC(x.inside); Tasks.Sleep(op.ms); DEC(x.inside)
		| op: SetOp DO
			x.flag := op.v
		| op: WaitOp DO
			INC(waits);
			op.result := x.Wait(ctx, {0});
			op.cancelled := ctx.cancelled; op.expired := ctx.expired;
			op.depth := Tasks.Depth(x); op.queued := Tasks.Queued(x);
			IF op.result & (op.id # 0) THEN Note(op.id) END;
			IF ~op.result THEN
				s0 := Tasks.Switches();
				op.second := x.Wait(ctx, {0});
				s1 := Tasks.Switches();
				op.fast := s1 = s0
			END
		| op: NeedOp DO
			LOOP
				IF op.need * x.Ready() # {} THEN
					IF op.id # 0 THEN Note(op.id) END;
					EXIT
				END;
				IF ~x.Wait(ctx, op.need) THEN RETURN END
			END
		| op: CondWaitOp DO
			LOOP
				IF op.c.Ready(x) THEN
					IF op.id # 0 THEN Note(op.id) END;
					EXIT
				END;
				IF ~x.WaitFor(ctx, op.c) THEN RETURN END
			END
		| op: NoteOp DO
			Note(op.id)
		| op: NestOp DO
			op.depth1 := Tasks.Depth(x);
			x.Do(ctx, inner);
			op.innerDepth := inner.depth;
			op.depth3 := Tasks.Depth(x)
		| op: InnerOp DO
			op.depth := Tasks.Depth(x); op.seen := TRUE;
			deep := x.deepWait;
			IF deep THEN
				IF ~x.Wait(ctx, {0}) THEN RETURN END
			END
		| op: OuterOp DO
			hold.result := FALSE;
			trapY := x.y;
			x.y.Do(ctx, hold)
		| op: HoldOp DO
			INC(x.entries);
			op.result := x.Wait(ctx, {0})
		| op: CaughtOp DO
			trapY := x.y; trapCtx := S.ADR(ctx);
			IF op.tryIt THEN
				Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(InnerTrap)), trapCtx, 0, 0)
			ELSE
				InnerTrap(trapCtx, 0, 0)
			END;
			op.heldAfter := ctx.HeldCount();
			op.result := ~Tasks.Held(x.y) & Tasks.Held(x);
			caughtSeen := op.result
		| op: CrashOp DO
			NilCrash
		| op: JoinOp DO
			Tasks.Join(op.child)
		| op: CrossOp DO
			op.y.Do(ctx, visit)
		| op: ProbeOp DO
		ELSE
			HALT(20)
		END
	END Exclusive;

	(* -------------------- буфер на флагах --------------------------------- *)

	PROCEDURE (b: Buf) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < cap THEN INCL(s, 0) END;
		IF b.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (b: Buf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO
				IF ~b.Wait(ctx, {0}) THEN RETURN END
			END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Wait(ctx, {1}) THEN RETURN END
			END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: BufProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(ctx, op)
		END
	END Body;

	PROCEDURE (c: BufCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(ctx, op);
			IF op.item < 0 THEN bufBroken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN bufBroken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(bufCount)
		END
	END Body;

	(* -------------------- буфер на Cond ----------------------------------- *)

	PROCEDURE (c: NotEmpty) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf);
		RETURN b.count > 0
	END Ready;

	PROCEDURE (c: NotFull) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: CondBuf;
	BEGIN
		b := o(CondBuf);
		RETURN b.count < cap
	END Ready;

	PROCEDURE (b: CondBuf) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: CondPutOp DO
			WHILE ~b.notFull.Ready(b) DO
				IF ~b.WaitFor(ctx, b.notFull) THEN RETURN END
			END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: CondGetOp DO
			WHILE ~b.notEmpty.Ready(b) DO
				IF ~b.WaitFor(ctx, b.notEmpty) THEN RETURN END
			END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: CondProd) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: CondPutOp;
	BEGIN
		FOR i := 1 TO p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(ctx, op)
		END
	END Body;

	PROCEDURE (c: CondCons) Body- (VAR ctx: Tasks.Ctx);
		VAR i, id, seq: INTEGER; op: CondGetOp;
	BEGIN
		i := 1;
		WHILE (i <= c.n) & ~ctx.Stopped() DO
			op.item := -1;
			c.b.Do(ctx, op);
			IF op.item < 0 THEN condBroken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN condBroken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(condCount);
			INC(i)
		END;
		IF ctx.Stopped() THEN consStopped := TRUE END
	END Body;

	(* -------------------- тела задач -------------------------------------- *)

	PROCEDURE (j: EnterJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: EnterOp;
	BEGIN
		j.x.Do(ctx, op);
		j.done := TRUE
	END Body;

	PROCEDURE (j: SleepHoldJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: SleepOp;
	BEGIN
		op.ms := j.ms;
		j.x.Do(ctx, op);
		j.done := TRUE
	END Body;

	PROCEDURE (t: JoinChild) Run;
	BEGIN
		Tasks.Sleep(t.ms)
	END Run;

	PROCEDURE (j: JoinHoldJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: JoinOp;
	BEGIN
		op.child := j.child;
		j.x.Do(ctx, op);
		j.done := TRUE
	END Body;

	PROCEDURE (j: CrossJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: CrossOp;
	BEGIN
		op.y := j.y;
		j.x.Do(ctx, op);
		j.done := TRUE
	END Body;

	PROCEDURE (j: SetJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: SetOp;
	BEGIN
		op.v := j.v;
		j.x.Do(ctx, op)
	END Body;

	PROCEDURE (j: WaitJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.x.Do(ctx, j.op)
	END Body;

	PROCEDURE (j: CondWaitJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.x.Do(ctx, j.op)
	END Body;

	PROCEDURE (j: NeedJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.x.Do(ctx, j.op)
	END Body;

	PROCEDURE (j: OrderJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: NoteOp;
	BEGIN
		op.id := j.id;
		j.x.Do(ctx, op)
	END Body;

	PROCEDURE (j: NestJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.x.Do(ctx, j.n)
	END Body;

	PROCEDURE (j: OuterJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: OuterOp;
	BEGIN
		j.x.Do(ctx, op);
		bodyDone := TRUE
	END Body;

	PROCEDURE (j: CaughtJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.x.Do(ctx, j.op);
		bodyDone := TRUE
	END Body;

	PROCEDURE (j: StopEntryJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: EnterOp; before: INTEGER;
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		before := j.x.entries;
		j.x.Do(ctx, op);
		j.done := j.x.entries > before;
		j.cancelled := ctx.cancelled;
		j.expired := ctx.expired;
		bodyDone := TRUE
	END Body;

	PROCEDURE (j: DeadlineJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.SetDeadline(ctx, 5);
		j.x.Do(ctx, j.op);
		j.expired := ctx.expired;
		bodyDone := TRUE
	END Body;

	PROCEDURE (j: ProbeJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: ProbeOp;
	BEGIN
		j.x.Do(ctx, op)
	END Body;

	PROCEDURE (j: AllocJob) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: ProbeOp; a0, a1: LONGINT;
	BEGIN
		FOR i := 1 TO 8 DO j.x.Do(ctx, op) END;	(* прогрев записей *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO 1000 DO j.x.Do(ctx, op) END;
		a1 := Kernel.AllocCount();
		allocDelta := a1 - a0
	END Body;

	PROCEDURE (j: SpawnJob) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; c: ChildJob;
	BEGIN
		FOR i := 1 TO j.n DO
			NEW(c); Tasks.Spawn(ctx, c); INC(spawnDone)
		END;
		Tasks.JoinAll(ctx);
		bodyDone := TRUE
	END Body;

	PROCEDURE (j: ChildJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(2);
		INC(childDone)
	END Body;

	PROCEDURE (t: Unparker) Run;
	BEGIN
		Tasks.Sleep(2);
		Tasks.Unpark(t.t)
	END Run;

	PROCEDURE (t: CancelTask) Run;
	BEGIN
		Tasks.Sleep(t.ms);
		Tasks.Cancel(t.t)
	END Run;

	(* -------------------- тесты ------------------------------------------- *)

	PROCEDURE CaseMutualExclusion;
		VAR x: Lock; a, b: EnterJob;
	BEGIN
		NEW(x); x.allow := TRUE; NEW(a); a.x := x; NEW(b); b.x := x;
		Start(a); Start(b);
		UnitAssert.True(Pump(2000), "задачи вошли и вышли");
		UnitAssert.EqualInt(1, maxInside, "внутри монитора не больше одной задачи");
		UnitAssert.EqualInt(2, x.entries, "обе задачи выполнили операцию");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseMutualExclusion;

	PROCEDURE CaseFifo;
	(* A спит в мониторе; B и C встают в очередь входа по порядку. *)
		VAR x: Lock; a: SleepHoldJob; b, c: OrderJob;
	BEGIN
		NEW(x); x.allow := TRUE; NEW(a); a.x := x; a.ms := 20;
		Start(a); Tasks.Step;
		UnitAssert.EqualInt(1, x.inside, "A вошла и спит");
		NEW(b); b.x := x; b.id := 2; NEW(c); c.x := x; c.id := 3;
		Start(b); Start(c); Tasks.Step;
		UnitAssert.EqualInt(2, Tasks.Queued(x), "B и C ждут входа");
		UnitAssert.True(Pump(2000), "все вышли");
		UnitAssert.EqualInt(2, orderN, "вошли двое");
		UnitAssert.EqualInt(2, order[0], "первым вошёл B");
		UnitAssert.EqualInt(3, order[1], "вторым вошёл C");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseFifo;

	PROCEDURE CaseFlagFifo;
	(* Несколько ждущих по флагам: выход будит первого готового по порядку. *)
		VAR x: Lock; b, c, d: WaitJob; n: ProbeJob;
	BEGIN
		NEW(x); x.flag := FALSE;
		NEW(b); b.x := x; b.op.id := 11;
		NEW(c); c.x := x; c.op.id := 12;
		NEW(d); d.x := x; d.op.id := 13;
		Start(b); Tasks.Step; Start(c); Tasks.Step; Start(d); Tasks.Step;
		UnitAssert.EqualInt(3, Tasks.Queued(x), "три ждущих по флагу");
		x.flag := TRUE;	(* так делает только тест: главный стек между шагами *)
		NEW(n); n.x := x; Start(n);	(* вход и выход будят первого готового *)
		UnitAssert.True(Pump(2000), "все вышли");
		UnitAssert.EqualInt(3, orderN, "трое прошли ожидание");
		UnitAssert.EqualInt(11, order[0], "первым разбужен B");
		UnitAssert.EqualInt(12, order[1], "вторым C");
		UnitAssert.EqualInt(13, order[2], "третьим D");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseFlagFifo;

	PROCEDURE CaseFlagPriority;
	(* Двое ждут разных флагов; готов только второй — будится он, а не первый в очереди. *)
		VAR x: Lock; a, b: NeedJob; n: ProbeJob; t0: LONGINT;
	BEGIN
		NEW(x); x.flag := FALSE; x.flag1 := FALSE;
		NEW(a); a.x := x; a.op.need := {1}; a.op.id := 1; Start(a); Tasks.Step;
		NEW(b); b.x := x; b.op.need := {0}; b.op.id := 2; Start(b); Tasks.Step;
		UnitAssert.EqualInt(2, Tasks.Queued(x), "два ждущих по флагу");
		x.flag := TRUE;		(* готов флаг 0, нужный B; A ждёт флаг 1 *)
		NEW(n); n.x := x; Start(n);
		t0 := Kernel.Time();
		WHILE (orderN < 1) & (Kernel.Time() - t0 <= 500) DO Tasks.Step END;
		UnitAssert.EqualInt(1, orderN, "разбужен ровно один");
		UnitAssert.EqualInt(2, order[0], "разбужен первый готовый");
		UnitAssert.EqualInt(1, Tasks.Queued(x), "A ещё ждёт");
		x.flag1 := TRUE; Tasks.Unpark(a);
		UnitAssert.True(Pump(1000), "A отпущен");
		UnitAssert.EqualInt(2, orderN, "оба прошли");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseFlagPriority;

	PROCEDURE CaseCondPriority;
	(* Двое ждут разных условий-объектов; готово второе — будится он. *)
		VAR x: Lock; c1, c2: YesCond; a, b: CondWaitJob; n: ProbeJob; t0: LONGINT;
	BEGIN
		NEW(x); x.flag := FALSE; x.flag1 := FALSE;
		NEW(c1); c1.yes := FALSE; NEW(c2); c2.yes := FALSE;
		NEW(a); a.x := x; a.op.c := c1; a.op.id := 1; Start(a); Tasks.Step;
		NEW(b); b.x := x; b.op.c := c2; b.op.id := 2; Start(b); Tasks.Step;
		UnitAssert.EqualInt(2, Tasks.Queued(x), "два ждущих по условию");
		c2.yes := TRUE;		(* готово условие B, а не первое в очереди *)
		NEW(n); n.x := x; Start(n);
		t0 := Kernel.Time();
		WHILE (orderN < 1) & (Kernel.Time() - t0 <= 500) DO Tasks.Step END;
		UnitAssert.EqualInt(1, orderN, "разбужен ровно один");
		UnitAssert.EqualInt(2, order[0], "разбужен первый готовый");
		UnitAssert.EqualInt(1, Tasks.Queued(x), "A ещё ждёт");
		c1.yes := TRUE; Tasks.Unpark(a);
		UnitAssert.True(Pump(1000), "A отпущен");
		UnitAssert.EqualInt(2, orderN, "оба прошли");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseCondPriority;

	PROCEDURE CaseBufferFlags;
		VAR b: Buf; p: BufProd; c: BufCons; i, total: INTEGER;
	BEGIN
		total := producers * per;
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		FOR i := 0 TO consumers - 1 DO
			NEW(c); c.b := b; c.n := total DIV consumers; Start(c)
		END;
		FOR i := 0 TO producers - 1 DO
			NEW(p); p.b := b; p.id := i; p.n := per; Start(p)
		END;
		UnitAssert.True(Pump(3000), "буфер отработал");
		UnitAssert.EqualInt(total, bufCount, "принято предметов");
		UnitAssert.True(~bufBroken, "порядок производителей сохранён");
		FOR i := 0 TO producers - 1 DO UnitAssert.EqualInt(per, got[i], "все предметы производителя") END;
		UnitAssert.EqualInt(0, b.count, "буфер пуст");
		UnitAssert.True(~Tasks.Held(b), "монитор свободен")
	END CaseBufferFlags;

	PROCEDURE CaseBufferCond;
		VAR b: CondBuf; p: CondProd; c: CondCons; i, total: INTEGER;
	BEGIN
		total := producers * per;
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		NEW(b.notEmpty); NEW(b.notFull);
		FOR i := 0 TO consumers - 1 DO
			NEW(c); c.b := b; c.n := total DIV consumers; Start(c)
		END;
		FOR i := 0 TO producers - 1 DO
			NEW(p); p.b := b; p.id := i; p.n := per; Start(p)
		END;
		UnitAssert.True(Pump(3000), "буфер на Cond отработал");
		UnitAssert.EqualInt(total, condCount, "принято предметов");
		UnitAssert.True(~condBroken, "порядок производителей сохранён");
		UnitAssert.True(~consStopped, "потребитель не остановлен");
		UnitAssert.EqualInt(0, b.count, "буфер пуст");
		UnitAssert.True(~Tasks.Held(b), "монитор свободен")
	END CaseBufferCond;

	PROCEDURE CaseReentrant;
		VAR x: Lock; j: NestJob;
	BEGIN
		NEW(x); NEW(j); j.x := x; x.deepWait := FALSE;
		Start(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.EqualInt(1, j.n.depth1, "внешний вход: глубина 1");
		UnitAssert.EqualInt(2, j.n.innerDepth, "в повторном входе глубина 2");
		UnitAssert.EqualInt(1, j.n.depth3, "после внутреннего выхода глубина 1");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseReentrant;

	PROCEDURE CaseWaitDepth2Traps;
		(* InnerOp ждёт на глубине 2 своего объекта — трап 23. *)
		VAR x: Lock; j: NestJob;
	BEGIN
		NEW(x); NEW(j); j.x := x; x.deepWait := TRUE;
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "задача трапнула");
		UnitAssert.True(~Tasks.Held(x), "монитор отпущен");
		UnitAssert.EqualInt(0, Tasks.Queued(x), "очереди пусты")
	END CaseWaitDepth2Traps;

	PROCEDURE CaseForeignMonitorDenied;
		(* X взят, Y.Wait ждёт — X не разрешает ждать: трап 26. *)
		VAR x, y: Lock; j: OuterJob;
	BEGIN
		NEW(x); NEW(y); x.allow := FALSE; y.flag := FALSE; x.y := y;
		NEW(j); j.x := x;
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "ожидание с чужим монитором трапует");
		UnitAssert.True(~Tasks.Held(x), "X отпущен");
		UnitAssert.True(~Tasks.Held(y), "Y отпущен")
	END CaseForeignMonitorDenied;

	PROCEDURE CaseForeignMonitorAllowed;
		(* X разрешает ждать, держа его; Y освобождает условие. *)
		VAR x, y: Lock; j: OuterJob; set: SetJob;
	BEGIN
		NEW(x); NEW(y); x.allow := TRUE; y.flag := FALSE; x.y := y;
		NEW(j); j.x := x;
		Start(j); Tasks.Step;
		UnitAssert.True(~Tasks.Failed(j), "пока без трапа");
		NEW(set); set.x := y; set.v := TRUE; Start(set);
		UnitAssert.True(Pump(2000), "задача завершилась");
		UnitAssert.True(~Tasks.Failed(j), "ожидание с разрешением не трапует");
		UnitAssert.True(~Tasks.Held(x), "X отпущен");
		UnitAssert.True(~Tasks.Held(y), "Y отпущен")
	END CaseForeignMonitorAllowed;

	PROCEDURE CaseUncaughtTrapReleasesAll;
		VAR x, y: Lock; j: CaughtJob;
	BEGIN
		NEW(x); NEW(y); x.y := y;
		NEW(j); j.x := x; j.op.tryIt := FALSE;
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "задача трапнула");
		UnitAssert.True(~Tasks.Held(x), "внешний монитор отпущен");
		UnitAssert.True(~Tasks.Held(y), "внутренний монитор отпущен");
		UnitAssert.EqualInt(0, Tasks.Queued(x) + Tasks.Queued(y), "очереди пусты")
	END CaseUncaughtTrapReleasesAll;

	PROCEDURE CaseCaughtTrapReleasesInner;
		VAR x, y: Lock; j: CaughtJob;
	BEGIN
		NEW(x); NEW(y); x.y := y;
		NEW(j); j.x := x; j.op.tryIt := TRUE;
		Start(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.True(~Tasks.Failed(j), "трап пойман внутри Exclusive");
		UnitAssert.EqualInt(1, j.op.heldAfter, "внешний монитор остался у задачи");
		UnitAssert.True(j.op.result, "внутренний отпущен, внешний ещё взят");
		UnitAssert.True(~Tasks.Held(x), "после выхода внешний отпущен");
		UnitAssert.True(~Tasks.Held(y), "внутренний свободен")
	END CaseCaughtTrapReleasesInner;

	PROCEDURE CaseCancelEntry;
		(* Ожидание входа: отмена не пускает в Exclusive, следующий входит. *)
		VAR x: Lock; a: SleepHoldJob; b: StopEntryJob; c: OrderJob;
	BEGIN
		NEW(x); x.allow := TRUE; NEW(a); a.x := x; a.ms := 40;
		Start(a); Tasks.Step;
		UnitAssert.EqualInt(1, x.inside, "A держит монитор");
		NEW(b); b.x := x; b.deadline := 0;
		Start(b); Tasks.Step;
		UnitAssert.EqualInt(1, Tasks.Queued(x), "B ждёт входа");
		Tasks.Cancel(b);
		UnitAssert.True(Pump(2000), "все завершились");
		UnitAssert.True(~b.done, "Exclusive отменённой задачи не выполнен");
		UnitAssert.True(b.cancelled, "причина — отмена");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(b), "задача отменена");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен");
		NEW(c); c.x := x; c.id := 7; Start(c);
		UnitAssert.True(Pump(1000), "следующий вошёл");
		UnitAssert.EqualInt(1, orderN, "следующий waiter работает")
	END CaseCancelEntry;

	PROCEDURE CaseDeadlineEntry;
		(* Ожидание входа: срок не пускает в Exclusive. *)
		VAR x: Lock; a: SleepHoldJob; b: StopEntryJob;
	BEGIN
		NEW(x); x.allow := TRUE; NEW(a); a.x := x; a.ms := 40;
		Start(a); Tasks.Step;
		UnitAssert.EqualInt(1, x.inside, "A держит монитор");
		NEW(b); b.x := x; b.deadline := 5;
		Start(b); Tasks.Step;
		UnitAssert.EqualInt(1, Tasks.Queued(x), "B ждёт входа");
		UnitAssert.True(Pump(2000), "все завершились");
		UnitAssert.True(~b.done, "Exclusive не выполнен");
		UnitAssert.True(b.expired, "причина — срок");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseDeadlineEntry;

	PROCEDURE CaseCancelWait;
		(* Отмена в Wait: FALSE, причина, монитор у задачи, второй Wait сразу. *)
		VAR x: Lock; b: WaitJob; c: OrderJob;
	BEGIN
		NEW(x); x.flag := FALSE;
		NEW(b); b.x := x; b.op.id := 0; b.op.result := TRUE;
		Start(b); Tasks.Step;
		UnitAssert.True(waits > 0, "B ждёт условия");
		Tasks.Cancel(b);
		UnitAssert.True(Pump(1000), "B завершилась");
		UnitAssert.True(~b.op.result, "Wait вернул FALSE");
		UnitAssert.True(b.op.cancelled, "причина — отмена");
		UnitAssert.True(~b.op.expired, "срок не истёк");
		UnitAssert.EqualInt(1, b.op.depth, "монитор снова у задачи");
		UnitAssert.EqualInt(0, b.op.queued, "очередь пуста");
		UnitAssert.True(~b.op.second, "второй Wait тоже FALSE");
		UnitAssert.True(b.op.fast, "второй Wait не спал");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(b), "задача отменена");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен");
		NEW(c); c.x := x; c.id := 9; Start(c);
		UnitAssert.True(Pump(1000), "следующий вошёл");
		UnitAssert.EqualInt(1, orderN, "следующий waiter работает")
	END CaseCancelWait;

	PROCEDURE CaseDeadlineWait;
		(* Срок в Wait: FALSE, причина — срок, монитор у задачи. *)
		VAR x: Lock; j: DeadlineJob;
	BEGIN
		NEW(x); x.flag := FALSE;
		NEW(j); j.x := x; j.op.result := TRUE;
		Start(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.True(~Tasks.Failed(j), "без трапа");
		UnitAssert.True(~j.op.result, "Wait вернул FALSE");
		UnitAssert.True(j.expired, "причина — срок");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен");
		UnitAssert.EqualInt(0, Tasks.Queued(x), "очередь пуста")
	END CaseDeadlineWait;

	PROCEDURE CaseReadyOnce;
		(* Пустые очереди не зовут Ready; один выход при ждущем — ровно раз. *)
		VAR x: Lock; a: AllocJob; w: WaitJob; n: ProbeJob; t0: LONGINT;
	BEGIN
		NEW(x); NEW(a); a.x := x; readyCalls := 0;
		Start(a);
		UnitAssert.True(Pump(1000), "вход-выход без ждущих");
		UnitAssert.EqualInt(0, readyCalls, "пустые очереди Ready не зовут");
		NEW(w); w.x := x; w.op.id := 0; w.op.result := TRUE;
		x.flag := FALSE; x.flag1 := TRUE;	(* Ready даёт {1}, ждущий хочет {0} *)
		Start(w); Tasks.Step;
		UnitAssert.EqualInt(1, Tasks.Queued(x), "один ждущий по флагу");
		readyCalls := 0;
		NEW(n); n.x := x; Start(n);
		(* Ждём только n: w должен остаться спать. *)
		t0 := Kernel.Time();
		WHILE (Tasks.State(n) # Tasks.Finished) & (Tasks.State(n) # Tasks.Cancelled)
				& (Kernel.Time() - t0 <= 1000) DO
			Tasks.Step
		END;
		UnitAssert.True(Tasks.State(n) IN {Tasks.Finished, Tasks.Cancelled}, "выход случился");
		UnitAssert.EqualInt(1, readyCalls, "на выходе Ready зовут один раз");
		x.flag := TRUE; Tasks.Unpark(w);
		UnitAssert.True(Pump(1000), "ждущий отпущен");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseReadyOnce;

	PROCEDURE CaseNoAllocation;
		VAR x: Lock; j: AllocJob;
	BEGIN
		NEW(x); NEW(j); j.x := x;
		Start(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.EqualInt(0, allocDelta, "1000 пар вход-выход без выделений")
	END CaseNoAllocation;

	PROCEDURE CaseSpawnJoinAll;
		VAR j: SpawnJob;
	BEGIN
		NEW(j); j.n := 3;
		Start(j);
		UnitAssert.True(Pump(3000), "родитель и дети завершились");
		UnitAssert.EqualInt(3, spawnDone, "запущено детей");
		UnitAssert.EqualInt(3, childDone, "дети дошли до конца");
		UnitAssert.EqualInt(0, Tasks.Count(), "задач не осталось")
	END CaseSpawnJoinAll;

	(* -------------------- сон под монитором (трап 26) --------------------- *)

	PROCEDURE CaseSleepHoldDenied;
		VAR x: Lock; j: SleepHoldJob;
	BEGIN
		NEW(x); x.allow := FALSE;
		NEW(j); j.x := x; j.ms := 10;
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "сон под монитором без разрешения трапует");
		UnitAssert.EqualInt(26, Kernel.err, "сон под монитором — трап 26");
		UnitAssert.True(~j.done, "тело после трапа не продолжилось");
		UnitAssert.True(~Tasks.Held(x), "монитор отпущен");
		UnitAssert.EqualInt(0, Tasks.Queued(x), "очереди пусты")
	END CaseSleepHoldDenied;

	PROCEDURE CaseSleepHoldAllowed;
		VAR x: Lock; j: SleepHoldJob;
	BEGIN
		NEW(x); x.allow := TRUE;
		NEW(j); j.x := x; j.ms := 5;
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "сон с разрешением не трапует");
		UnitAssert.True(j.done, "тело дошло до конца");
		UnitAssert.EqualInt(1, x.entries, "операция выполнена");
		UnitAssert.True(~Tasks.Held(x), "монитор свободен")
	END CaseSleepHoldAllowed;

	PROCEDURE CaseJoinHoldDenied;
		VAR x: Lock; c: JoinChild; j: JoinHoldJob;
	BEGIN
		NEW(x); x.allow := FALSE;
		NEW(c); c.ms := 60; Start(c);
		NEW(j); j.x := x; j.child := c; Start(j);
		UnitAssert.True(Pump(2000), "задачи закончились");
		UnitAssert.True(Tasks.Failed(j), "Join под монитором без разрешения трапует");
		UnitAssert.EqualInt(26, Kernel.err, "Join под монитором — трап 26");
		UnitAssert.True(~j.done, "тело после трапа не продолжилось");
		UnitAssert.True(~Tasks.Held(x), "монитор отпущен")
	END CaseJoinHoldDenied;

	PROCEDURE CaseJoinHoldAllowed;
		VAR x: Lock; c: JoinChild; j: JoinHoldJob;
	BEGIN
		NEW(x); x.allow := TRUE;
		NEW(c); c.ms := 10; Start(c);
		NEW(j); j.x := x; j.child := c; Start(j);
		UnitAssert.True(Pump(2000), "задачи закончились");
		UnitAssert.True(~Tasks.Failed(j), "Join с разрешением не трапует");
		UnitAssert.True(j.done, "Join вернулся");
		UnitAssert.EqualInt(0, Tasks.Count(), "задач не осталось")
	END CaseJoinHoldAllowed;

	PROCEDURE CaseEntryHoldDenied;
		(* Ждать входа в чужой монитор, держа свой, — тоже сон под монитором. *)
		VAR x, y: Lock; h: SleepHoldJob; j: CrossJob;
	BEGIN
		NEW(x); x.allow := FALSE;
		NEW(y); y.allow := TRUE;
		NEW(h); h.x := y; h.ms := 60; Start(h);
		Tasks.Step;
		UnitAssert.True(Tasks.Held(y), "держатель взял Y");
		NEW(j); j.x := x; j.y := y; Start(j);
		UnitAssert.True(Pump(2000), "задачи закончились");
		UnitAssert.True(Tasks.Failed(j), "ожидание входа под монитором трапует");
		UnitAssert.EqualInt(26, Kernel.err, "ожидание входа — трап 26");
		UnitAssert.True(~j.done, "тело после трапа не продолжилось");
		UnitAssert.True(~Tasks.Held(x), "X отпущен");
		UnitAssert.True(~Tasks.Held(y), "Y отпущен")
	END CaseEntryHoldDenied;

	PROCEDURE CaseEntryHoldAllowed;
		VAR x, y: Lock; h: SleepHoldJob; j: CrossJob;
	BEGIN
		NEW(x); x.allow := TRUE;
		NEW(y); y.allow := TRUE;
		NEW(h); h.x := y; h.ms := 20; Start(h);
		Tasks.Step;
		UnitAssert.True(Tasks.Held(y), "держатель взял Y");
		NEW(j); j.x := x; j.y := y; Start(j);
		UnitAssert.True(Pump(2000), "задачи закончились");
		UnitAssert.True(~Tasks.Failed(j), "ожидание входа с разрешением не трапует");
		UnitAssert.True(j.done, "операция выполнена");
		UnitAssert.EqualInt(2, y.entries, "вошли в Y: держатель и ожидавший")
	END CaseEntryHoldAllowed;

	(* -------------------- пробуждение без передачи ------------------------ *)

	(* -------------------- запуск обоих режимов ---------------------------- *)

	PROCEDURE TestMutualExclusion*;
	BEGIN CaseMutualExclusion END TestMutualExclusion;
	PROCEDURE TestFifo*;
	BEGIN CaseFifo END TestFifo;
	PROCEDURE TestFlagFifo*;
	BEGIN CaseFlagFifo END TestFlagFifo;
	PROCEDURE TestFlagPriority*;
	BEGIN CaseFlagPriority END TestFlagPriority;
	PROCEDURE TestCondPriority*;
	BEGIN CaseCondPriority END TestCondPriority;
	PROCEDURE TestBufferFlags*;
	BEGIN CaseBufferFlags END TestBufferFlags;
	PROCEDURE TestBufferCond*;
	BEGIN CaseBufferCond END TestBufferCond;
	PROCEDURE TestReentrant*;
	BEGIN CaseReentrant END TestReentrant;
	PROCEDURE TestWaitDepth2Traps*;
	BEGIN CaseWaitDepth2Traps END TestWaitDepth2Traps;
	PROCEDURE TestForeignMonitorDenied*;
	BEGIN CaseForeignMonitorDenied END TestForeignMonitorDenied;
	PROCEDURE TestForeignMonitorAllowed*;
	BEGIN CaseForeignMonitorAllowed END TestForeignMonitorAllowed;
	PROCEDURE TestUncaughtTrapReleasesAll*;
	BEGIN CaseUncaughtTrapReleasesAll END TestUncaughtTrapReleasesAll;
	PROCEDURE TestCaughtTrapReleasesInner*;
	BEGIN CaseCaughtTrapReleasesInner END TestCaughtTrapReleasesInner;
	PROCEDURE TestCancelEntry*;
	BEGIN CaseCancelEntry END TestCancelEntry;
	PROCEDURE TestDeadlineEntry*;
	BEGIN CaseDeadlineEntry END TestDeadlineEntry;
	PROCEDURE TestCancelWait*;
	BEGIN CaseCancelWait END TestCancelWait;
	PROCEDURE TestDeadlineWait*;
	BEGIN CaseDeadlineWait END TestDeadlineWait;
	PROCEDURE TestReadyOnce*;
	BEGIN CaseReadyOnce END TestReadyOnce;
	PROCEDURE TestNoAllocation*;
	BEGIN CaseNoAllocation END TestNoAllocation;
	PROCEDURE TestSpawnJoinAll*;
	BEGIN CaseSpawnJoinAll END TestSpawnJoinAll;
	PROCEDURE TestSleepHoldDenied*;
	BEGIN CaseSleepHoldDenied END TestSleepHoldDenied;
	PROCEDURE TestSleepHoldAllowed*;
	BEGIN CaseSleepHoldAllowed END TestSleepHoldAllowed;
	PROCEDURE TestJoinHoldDenied*;
	BEGIN CaseJoinHoldDenied END TestJoinHoldDenied;
	PROCEDURE TestJoinHoldAllowed*;
	BEGIN CaseJoinHoldAllowed END TestJoinHoldAllowed;
	PROCEDURE TestEntryHoldDenied*;
	BEGIN CaseEntryHoldDenied END TestEntryHoldDenied;
	PROCEDURE TestEntryHoldAllowed*;
	BEGIN CaseEntryHoldAllowed END TestEntryHoldAllowed;

END System_devTasksObj.
