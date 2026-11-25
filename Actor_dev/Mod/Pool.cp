MODULE Actor_devPool;
(** Рост пула исполнителей ActorRun.

	Обработчик актора может ждать канал и держать исполнителя. Пока
	ждущих не больше числа из Start, пул не меняется; дальше Schedule
	запускает нового исполнителя на каждый готовый ящик, пока не достигнут
	предел. Лишние исполнители после простоя уходят, n возвращается к
	исходному. Ящик dedicated обслуживает своя задача и на пул не влияет.

	Ожидание канала — встреча AsyncChan: освобождает ждущего отправитель
	из задачи Driver. *)

	IMPORT Kernel, Services, Tasks, ActorCore, ActorSup, ActorRun, AsyncChan, AsyncSelect,
		UnitAssert;

	CONST
		shrinkWait = 5000;	(* ожидание усадки пула, мс *)

	TYPE
		Msg = ActorCore.Msg;
		Wait = POINTER TO RECORD (Msg) END;
		Ping = POINTER TO RECORD (Msg) END;
		Boom = POINTER TO RECORD (Msg) END;

		Probe = POINTER TO RECORD (ActorCore.Actor) END;
		Sup = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		ch: AsyncChan.Chan;
		waitMsg: Wait; pingMsg: Ping; boomMsg: Boom;
		relMsg: Ping;
		inside, peak, waited, pings, starts, relCount: INTEGER;
		probeProps, kidProps: ActorCore.Props;
		nKids: INTEGER;
		kids: ARRAY 4 OF ActorCore.Ref;

	(* --- обвязка ---------------------------------------------------------- *)

	PROCEDURE Idle (ms: LONGINT);
		VAR t: LONGINT; never: BOOLEAN;
	BEGIN
		never := FALSE; t := Kernel.Time();
		WHILE ~never & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END
	END Idle;

	PROCEDURE UntilInt (VAR x: INTEGER; want: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (x # want) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN x = want
	END UntilInt;

	PROCEDURE UntilExecs (atLeast: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (ActorRun.n < atLeast) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN ActorRun.n >= atLeast
	END UntilExecs;

	PROCEDURE UntilShrunk (to: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (ActorRun.n > to) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN ActorRun.n = to
	END UntilShrunk;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body
	END Run;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
		VAR d: Driver; t: LONGINT;
	BEGIN
		NEW(d); d.body := body; Tasks.Start(d);
		t := Kernel.Time();
		WHILE (Tasks.State(d) # Tasks.Finished) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN Tasks.State(d) = Tasks.Finished
	END Drive;

	PROCEDURE ReleaseBody;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO relCount DO ch.Send(relMsg) END
	END ReleaseBody;

	PROCEDURE Release (k: INTEGER): BOOLEAN;
	(* Пост: k отправителей встречи отпустили k ждущих обработчиков. *)
	BEGIN
		relCount := k;
		RETURN Drive(ReleaseBody, 5000)
	END Release;

	PROCEDURE NewProbe (): ActorCore.Actor;
		VAR p: Probe;
	BEGIN
		NEW(p); RETURN p
	END NewProbe;

	PROCEDURE SpawnProbe (): ActorCore.Ref;
	BEGIN
		RETURN ActorCore.Spawn(NIL, probeProps, "")
	END SpawnProbe;

	(* --- акторы ----------------------------------------------------------- *)

	PROCEDURE (p: Probe) Receive (c: ActorCore.Context; m: Msg);
		VAR mm: AsyncSelect.Msg;
	BEGIN
		WITH m: ActorCore.Started DO INC(starts)
		| m: Wait DO
			INC(inside); IF inside > peak THEN peak := inside END;
			ch.Recv(mm);
			DEC(inside); INC(waited)
		| m: Ping DO INC(pings)
		| m: Boom DO HALT(42)
		END
	END Receive;

	PROCEDURE (s: Sup) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorCore.Started DO
			kids[0] := ActorCore.Spawn(c, kidProps, "")
		END
	END Receive;

	PROCEDURE NewSup (): ActorCore.Actor;
		VAR s: Sup;
	BEGIN
		NEW(s); RETURN s
	END NewSup;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		ActorRun.SetLimit(1024);
		ch := AsyncChan.NewChan();
		NEW(waitMsg); NEW(pingMsg); NEW(boomMsg); NEW(relMsg);
		inside := 0; peak := 0; waited := 0; pings := 0; starts := 0; relCount := 0;
		nKids := 1; kids[0] := NIL;
		probeProps := ActorCore.NewProps(NewProbe);
		kidProps := ActorCore.NewProps(NewProbe);
		ActorRun.Start(2)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown; Idle(5); ch := NIL
	END Teardown;

	PROCEDURE SpawnWaiters (k: INTEGER; VAR refs: ARRAY OF ActorCore.Ref);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO k - 1 DO refs[i] := SpawnProbe() END
	END SpawnWaiters;

	(* --- (а) ждущие не держат остальных ----------------------------------- *)

	PROCEDURE TestWaitersDoNotBlockPool*;
		VAR refs: ARRAY 6 OF ActorCore.Ref; pingers: ARRAY 4 OF ActorCore.Ref; i: INTEGER;
	BEGIN
		SpawnWaiters(6, refs);
		UnitAssert.True(UntilInt(starts, 6, 3000), "six actors started");
		FOR i := 0 TO 5 DO ActorCore.Tell(refs[i], waitMsg) END;
		UnitAssert.True(UntilInt(peak, 6, 5000), "six handlers wait in a channel");
		UnitAssert.True(UntilExecs(6, 3000), "pool grew to the waiters");
		FOR i := 0 TO 3 DO pingers[i] := SpawnProbe() END;
		FOR i := 0 TO 3 DO ActorCore.Tell(pingers[i], pingMsg) END;
		UnitAssert.True(UntilInt(pings, 4, 3000), "other actors are served while six wait");
		UnitAssert.EqualInt(6, inside, "waiters still hold the channel");
		UnitAssert.True(Release(6), "six sends met six receivers");
		UnitAssert.True(UntilInt(waited, 6, 3000), "all six handlers woke");
		UnitAssert.EqualInt(0, inside, "no handler is waiting")
	END TestWaitersDoNotBlockPool;

	(* --- (б) предел: рост стоит, счётчик растёт --------------------------- *)

	PROCEDURE TestLimitStopsGrowth*;
		VAR refs: ARRAY 6 OF ActorCore.Ref; pingers: ARRAY 2 OF ActorCore.Ref; i: INTEGER;
	BEGIN
		ActorRun.Stop; ActorRun.SetLimit(3); ActorRun.Start(2);
		SpawnWaiters(6, refs);
		UnitAssert.True(UntilInt(starts, 6, 3000), "six actors started");
		FOR i := 0 TO 5 DO ActorCore.Tell(refs[i], waitMsg) END;
		UnitAssert.True(UntilInt(inside, 3, 5000), "three handlers wait, the pool is at the limit");
		UnitAssert.EqualInt(3, ActorRun.n, "no executor above the limit");
		UnitAssert.True(ActorRun.limitHits > 0, "ready mailboxes waited at the limit");
		UnitAssert.True(Release(6), "all six waiters released despite the limit");
		UnitAssert.True(UntilInt(waited, 6, 5000), "all six handlers woke");
		FOR i := 0 TO 1 DO pingers[i] := SpawnProbe() END;
		FOR i := 0 TO 1 DO ActorCore.Tell(pingers[i], pingMsg) END;
		UnitAssert.True(UntilInt(pings, 2, 3000), "work continues after the limit was reached")
	END TestLimitStopsGrowth;

	(* --- (в) усадка после пика -------------------------------------------- *)

	PROCEDURE TestPoolShrinksAfterPeak*;
		VAR refs: ARRAY 6 OF ActorCore.Ref; i: INTEGER;
	BEGIN
		SpawnWaiters(6, refs);
		UnitAssert.True(UntilInt(starts, 6, 3000), "six actors started");
		FOR i := 0 TO 5 DO ActorCore.Tell(refs[i], waitMsg) END;
		UnitAssert.True(UntilInt(peak, 6, 5000), "six handlers wait");
		UnitAssert.True(UntilExecs(6, 3000), "pool grew to six");
		UnitAssert.True(Release(6), "six waiters released");
		UnitAssert.True(UntilInt(waited, 6, 3000), "all six woke");
		UnitAssert.True(UntilShrunk(2, shrinkWait), "pool returned to the initial two");
		UnitAssert.True(ActorRun.shrunk > 0, "idle extra executors ended")
	END TestPoolShrinksAfterPeak;

	(* --- (г) dedicated и растущий пул ------------------------------------- *)

	PROCEDURE TestDedicatedAndGrowingPool*;
		VAR p: ActorCore.Props; own: ActorCore.Ref; pool: ARRAY 2 OF ActorCore.Ref; i: INTEGER;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		p := ActorCore.NewProps(NewProbe); p.dedicated := TRUE;
		own := ActorCore.Spawn(NIL, p, "");
		ActorCore.Tell(own, waitMsg);
		UnitAssert.True(UntilInt(inside, 1, 3000), "dedicated handler waits in its own task");
		UnitAssert.EqualInt(1, ActorRun.n, "dedicated wait did not grow the pool");
		FOR i := 0 TO 1 DO pool[i] := SpawnProbe() END;
		FOR i := 0 TO 1 DO ActorCore.Tell(pool[i], pingMsg) END;
		UnitAssert.True(UntilInt(pings, 2, 3000), "pool actors answered while dedicated waits");
		UnitAssert.EqualInt(1, inside, "dedicated handler still waits");
		UnitAssert.True(Release(1), "dedicated waiter released");
		UnitAssert.True(UntilInt(waited, 1, 3000), "dedicated handler woke")
	END TestDedicatedAndGrowingPool;

	(* --- (д) трап на выросшем исполнителе --------------------------------- *)

	PROCEDURE TestTrapOnGrownExecutor*;
		VAR refs: ARRAY 4 OF ActorCore.Ref; p: ActorCore.Props; sup, target, later: ActorCore.Ref; i: INTEGER;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		SpawnWaiters(4, refs);
		UnitAssert.True(UntilInt(starts, 4, 3000), "four waiters started");
		FOR i := 0 TO 3 DO ActorCore.Tell(refs[i], waitMsg) END;
		UnitAssert.True(UntilInt(peak, 4, 5000), "four handlers wait");
		UnitAssert.True(UntilExecs(4, 3000), "pool grew to the waiters");
		p := ActorCore.NewProps(NewSup); p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000);
		sup := ActorCore.Spawn(NIL, p, "");
		UnitAssert.True(UntilInt(starts, 5, 3000), "supervised child started");
		target := kids[0];
		ActorCore.Tell(target, boomMsg);
		UnitAssert.True(UntilInt(starts, 6, 3000), "trap on a grown executor restarted the child");
		UnitAssert.EqualInt(4, inside, "waiters on other grown executors are untouched");
		ActorCore.Tell(target, pingMsg);
		UnitAssert.True(UntilInt(pings, 1, 3000), "restarted child answers");
		UnitAssert.True(Release(4), "four waiters released");
		UnitAssert.True(UntilInt(waited, 4, 3000), "all four handlers woke");
		later := SpawnProbe();
		ActorCore.Tell(later, pingMsg);
		UnitAssert.True(UntilInt(pings, 2, 3000), "no executor was lost: new work is served")
	END TestTrapOnGrownExecutor;

END Actor_devPool.
