MODULE Actor_devRouter;
(** Тесты маршрутизаторов: пул и группа, четыре способа выбора, смена
	набора письмом, надзор за детьми пула.

	Маршруты — простые акторы, считающие полученные письма; счёт
	хранится по номеру экземпляра и переживает перезапуск. *)

	IMPORT Kernel, Services, Tasks, ActorCore, ActorSup, ActorRun, ActorHash, ActorRouter,
		UnitAssert;

	CONST
		maxSinks = 32;
		hashKeys = 8;

	TYPE
		Msg = ActorCore.Msg;
		Put = POINTER TO RECORD (Msg) END;
		Boom = POINTER TO RECORD (Msg) END;
		Ping = POINTER TO RECORD (ActorRouter.Keyed) END;
		Sink = POINTER TO RECORD (ActorCore.Actor) id: INTEGER END;

	VAR
		counts: ARRAY maxSinks OF INTEGER;
		served, starts, nextId: INTEGER;
		put: Put; boom: Boom;

	(* --- обвязка ---------------------------------------------------------- *)

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE ~flag & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END Until;

	PROCEDURE Idle (ms: LONGINT);
		VAR never: BOOLEAN;
	BEGIN
		never := FALSE; IF Until(never, ms) THEN END
	END Idle;

	PROCEDURE UntilInt (VAR x: INTEGER; want: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (x # want) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN x = want
	END UntilInt;

	PROCEDURE Reset;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO maxSinks - 1 DO counts[i] := 0 END;
		served := 0; starts := 0; nextId := 0
	END Reset;

	(* --- маршрут ---------------------------------------------------------- *)

	PROCEDURE (a: Sink) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Put DO INC(counts[a.id]); INC(served)
		| m: Ping DO INC(counts[a.id]); INC(served)
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE NewSink (): ActorCore.Actor;
		VAR a: Sink;
	BEGIN
		NEW(a); a.id := nextId; INC(nextId); INC(starts); RETURN a
	END NewSink;

	PROCEDURE SpawnSink (): ActorCore.Ref;
	BEGIN
		RETURN ActorCore.Spawn(NIL, ActorCore.NewProps(NewSink), "")
	END SpawnSink;

	PROCEDURE Group (n: INTEGER; OUT refs: ARRAY OF ActorCore.Ref);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO refs[i] := SpawnSink() END
	END Group;

	PROCEDURE Key (i: INTEGER; OUT key: ActorCore.Name);
	BEGIN
		key := ""; key[0] := "k"; key[1] := CHR(ORD("0") + i)
	END Key;

	PROCEDURE Owner (IN key: ActorCore.Name; IN refs: ARRAY OF ActorCore.Ref): INTEGER;
		VAR i, best: INTEGER; w, bw: LONGINT;
	BEGIN
		best := -1; bw := -1;
		FOR i := 0 TO LEN(refs) - 1 DO
			w := ActorHash.Weight(refs[i].name, "router", key);
			IF (w > bw) OR (w = bw) & (refs[i].name < refs[best].name) THEN best := i; bw := w END
		END;
		RETURN best
	END Owner;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		Reset; NEW(put); NEW(boom); ActorRun.Start(4)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown; Idle(5)
	END Teardown;

	(* --- 1. группа -------------------------------------------------------- *)

	PROCEDURE TestRoundRobinCyclesOverGroup*;
		VAR refs: ARRAY 3 OF ActorCore.Ref; r: ActorCore.Ref; i: INTEGER;
	BEGIN
		Group(3, refs);
		r := ActorRouter.NewGroup(refs, ActorRouter.roundRobin, 1);
		FOR i := 1 TO 6 DO ActorCore.Tell(r, put) END;
		UnitAssert.True(UntilInt(served, 6, 2000), "six letters served");
		UnitAssert.EqualInt(2, counts[0], "route 0 got two");
		UnitAssert.EqualInt(2, counts[1], "route 1 got two");
		UnitAssert.EqualInt(2, counts[2], "route 2 got two")
	END TestRoundRobinCyclesOverGroup;

	PROCEDURE TestBroadcastSendsToEveryRoute*;
		VAR refs: ARRAY 3 OF ActorCore.Ref; r: ActorCore.Ref;
	BEGIN
		Group(3, refs);
		r := ActorRouter.NewGroup(refs, ActorRouter.broadcast, 1);
		ActorCore.Tell(r, put);
		UnitAssert.True(UntilInt(served, 3, 2000), "every route served");
		UnitAssert.EqualInt(1, counts[0], "route 0");
		UnitAssert.EqualInt(1, counts[1], "route 1");
		UnitAssert.EqualInt(1, counts[2], "route 2")
	END TestBroadcastSendsToEveryRoute;

	PROCEDURE TestRandomIsDeterministicFromSeed*;
		VAR a, b: ARRAY 3 OF ActorCore.Ref; ra, rb: ActorCore.Ref; i, used: INTEGER;
	BEGIN
		Group(3, a); Group(3, b);
		ra := ActorRouter.NewGroup(a, ActorRouter.random, 424242);
		rb := ActorRouter.NewGroup(b, ActorRouter.random, 424242);
		FOR i := 1 TO 12 DO ActorCore.Tell(ra, put); ActorCore.Tell(rb, put) END;
		UnitAssert.True(UntilInt(served, 24, 3000), "both routers served");
		UnitAssert.EqualInt(counts[0], counts[3], "same seed, route 0");
		UnitAssert.EqualInt(counts[1], counts[4], "same seed, route 1");
		UnitAssert.EqualInt(counts[2], counts[5], "same seed, route 2");
		used := 0;
		FOR i := 0 TO 2 DO IF counts[i] > 0 THEN INC(used) END END;
		UnitAssert.True(used >= 2, "random used more than one route")
	END TestRandomIsDeterministicFromSeed;

	PROCEDURE TestConsistentHashKeepsKeyOnRoute*;
		VAR refs: ARRAY 4 OF ActorCore.Ref; r: ActorCore.Ref; i, k, o: INTEGER;
			key: ActorCore.Name; m: Ping; exp: ARRAY 4 OF INTEGER;
	BEGIN
		Group(4, refs);
		r := ActorRouter.NewGroup(refs, ActorRouter.consistentHash, 1);
		FOR i := 0 TO 3 DO exp[i] := 0 END;
		FOR i := 0 TO hashKeys - 1 DO
			Key(i, key);
			o := Owner(key, refs); INC(exp[o]);
			NEW(m); m.key := key; ActorCore.Tell(r, m);
			NEW(m); m.key := key; ActorCore.Tell(r, m)
		END;
		UnitAssert.True(UntilInt(served, 2 * hashKeys, 3000), "every key served twice");
		k := 0;
		FOR i := 0 TO 3 DO
			UnitAssert.EqualInt(2 * exp[i], counts[i], "route got exactly its keys");
			INC(k, 2 * exp[i])
		END;
		UnitAssert.EqualInt(2 * hashKeys, k, "the keys cover all routes")
	END TestConsistentHashKeepsKeyOnRoute;

	PROCEDURE TestSetGroupChangesRoutes*;
		VAR refs: ARRAY 2 OF ActorCore.Ref; one: ARRAY 1 OF ActorCore.Ref; r: ActorCore.Ref; i: INTEGER;
	BEGIN
		Group(2, refs);
		r := ActorRouter.NewGroup(refs, ActorRouter.roundRobin, 1);
		ActorCore.Tell(r, put); ActorCore.Tell(r, put);
		UnitAssert.True(UntilInt(served, 2, 2000), "old routes served");
		one[0] := SpawnSink();
		ActorRouter.SetGroup(r, one);
		FOR i := 1 TO 4 DO ActorCore.Tell(r, put) END;
		UnitAssert.True(UntilInt(served, 6, 2000), "new route served");
		UnitAssert.EqualInt(1, counts[0], "old route 0 untouched");
		UnitAssert.EqualInt(1, counts[1], "old route 1 untouched");
		UnitAssert.EqualInt(4, counts[2], "new route got four")
	END TestSetGroupChangesRoutes;

	(* --- 2. пул ------------------------------------------------------------ *)

	PROCEDURE TestPoolRestartsFailedRoute*;
		VAR r: ActorCore.Ref;
	BEGIN
		r := ActorRouter.NewPool(NewSink, 3, ActorRouter.roundRobin, 1,
			ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000));
		UnitAssert.True(UntilInt(starts, 3, 2000), "three routes born");
		ActorCore.Tell(r, boom);
		UnitAssert.True(UntilInt(starts, 4, 2000), "failed route restarted");
		ActorCore.Tell(r, put); ActorCore.Tell(r, put); ActorCore.Tell(r, put);
		UnitAssert.True(UntilInt(served, 3, 2000), "all routes serve after restart");
		UnitAssert.EqualInt(0, counts[0], "old instance got no letters");
		UnitAssert.EqualInt(1, counts[1], "route 1 served once");
		UnitAssert.EqualInt(1, counts[2], "route 2 served once");
		UnitAssert.EqualInt(1, counts[3], "restarted route served once")
	END TestPoolRestartsFailedRoute;

	PROCEDURE TestPoolDropsStoppedRoute*;
		VAR r: ActorCore.Ref;
	BEGIN
		r := ActorRouter.NewPool(NewSink, 3, ActorRouter.roundRobin, 1,
			ActorSup.NewOneForOne(ActorCore.Stop, 0, 1000));
		UnitAssert.True(UntilInt(starts, 3, 2000), "three routes born");
		ActorCore.Tell(r, boom);
		Idle(300);	(* ребёнок остановлен, Terminated снял маршрут *)
		ActorCore.Tell(r, put); ActorCore.Tell(r, put); ActorCore.Tell(r, put);
		ActorCore.Tell(r, put); ActorCore.Tell(r, put); ActorCore.Tell(r, put);
		UnitAssert.True(UntilInt(served, 6, 2000), "six letters served by two routes");
		UnitAssert.EqualInt(6, counts[0] + counts[1] + counts[2] + counts[3], "two routes served all six")
	END TestPoolDropsStoppedRoute;

	PROCEDURE TestPoolResizeChangesRouteCount*;
		VAR r: ActorCore.Ref; i: INTEGER;
	BEGIN
		r := ActorRouter.NewPool(NewSink, 2, ActorRouter.roundRobin, 1, NIL);
		UnitAssert.True(UntilInt(starts, 2, 2000), "two routes born");
		ActorRouter.SetCount(r, 4);
		UnitAssert.True(UntilInt(starts, 4, 2000), "two routes added");
		FOR i := 1 TO 4 DO ActorCore.Tell(r, put) END;
		UnitAssert.True(UntilInt(served, 4, 2000), "all four routes served");
		UnitAssert.EqualInt(1, counts[0], "route 0");
		UnitAssert.EqualInt(1, counts[1], "route 1");
		UnitAssert.EqualInt(1, counts[2], "route 2");
		UnitAssert.EqualInt(1, counts[3], "route 3")
	END TestPoolResizeChangesRouteCount;

END Actor_devRouter.
