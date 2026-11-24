MODULE Actor_devBench;
(** Замеры подсистемы Actor. Строка BENCH <имя> ops=… totalNs=… nsPerOp=…
	allocsPer1000=… switchesPer1000=…; тесты TestSlow… проверяют итог, а
	не время. Тело замера идёт в задаче: Unpark из задачи будит
	исполнителя без задержки планировщика. *)

	IMPORT Kernel, Services, Tasks, Console, Strings, ActorCore, ActorRun, UnitAssert, System_devClock;

	CONST
		actors = 10000;
		exchanges = 100000;

	TYPE
		Msg = ActorCore.Msg;
		Ball = POINTER TO RECORD (Msg) END;
		Counter = POINTER TO RECORD (ActorCore.Actor) END;
		Pinger = POINTER TO RECORD (ActorCore.Actor) kick: ActorCore.Ref END;
		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		got, starts, hops, limit: INTEGER;
		ball: Ball;
		kickTo: ActorCore.Ref;
		allocs0, switches0: LONGINT;
		driverDone: BOOLEAN;
		pool: INTEGER;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Now (): LONGINT;
	BEGIN
		allocs0 := Kernel.AllocCount(); switches0 := Tasks.Switches();
		RETURN System_devClock.now()
	END Now;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ops: INTEGER; t0: LONGINT);
		VAR ns: LONGINT;
	BEGIN
		ns := System_devClock.now() - t0;
		W("BENCH "); W(name); W(" ops="); N(ops); W(" totalNs="); N(ns);
		W(" nsPerOp="); N(ns DIV ops);
		W(" allocsPer1000="); N((Kernel.AllocCount() - allocs0) * 1000 DIV ops);
		W(" switchesPer1000="); N((Tasks.Switches() - switches0) * 1000 DIV ops); Console.WriteLn
	END Report;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
		VAR d: Driver; t: LONGINT;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		t := Kernel.Time();
		WHILE ~driverDone & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN driverDone
	END Drive;

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS Ball THEN INC(got) ELSIF m IS ActorCore.Started THEN INC(starts) END
	END Receive;

	PROCEDURE NewCounter (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END NewCounter;

	PROCEDURE (a: Pinger) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS ActorCore.Started THEN
			IF a.kick # NIL THEN c.Tell(a.kick, ball) END
		ELSIF m IS Ball THEN
			INC(hops);
			IF hops < limit THEN c.Respond(m) END
		END
	END Receive;

	PROCEDURE NewPinger (): ActorCore.Actor;
		VAR a: Pinger;
	BEGIN
		NEW(a); a.kick := kickTo; RETURN a
	END NewPinger;

	PROCEDURE TenThousand;
	(* Пост: actors акторов созданы и получили по письму; строки BENCH. *)
		VAR refs: POINTER TO ARRAY OF ActorCore.Ref; p: ActorCore.Props; i: INTEGER; t0: LONGINT;
	BEGIN
		NEW(refs, actors); p := ActorCore.NewProps(NewCounter);
		t0 := Now();
		FOR i := 0 TO actors - 1 DO refs[i] := ActorCore.Spawn(NIL, p, "") END;
		WHILE starts < actors DO Tasks.Yield END;
		Report("actor-spawn", actors, t0);
		t0 := Now();
		FOR i := 0 TO actors - 1 DO ActorCore.Tell(refs[i], ball) END;
		WHILE got < actors DO Tasks.Yield END;
		Report("actor-tell", actors, t0);
		FOR i := 0 TO actors - 1 DO ActorCore.StopActor(refs[i]) END
	END TenThousand;

	PROCEDURE PingPong;
	(* Пост: два актора обменялись 2 * exchanges письмами; строка BENCH. *)
		VAR a, b: ActorCore.Ref; t0: LONGINT; name: ARRAY 32 OF CHAR; s: ARRAY 8 OF CHAR;
	BEGIN
		hops := 0; limit := 2 * exchanges;
		kickTo := NIL; a := ActorCore.Spawn(NIL, ActorCore.NewProps(NewPinger), "");
		t0 := Now();
		kickTo := a; b := ActorCore.Spawn(NIL, ActorCore.NewProps(NewPinger), "");
		WHILE hops < limit DO Tasks.Yield END;
		Strings.IntToString(pool, s); name := "actor-pingpong-" + s + "exec";
		Report(name, limit, t0);
		ActorCore.StopActor(a); ActorCore.StopActor(b)
	END PingPong;

	PROCEDURE TestSlowTenThousandActors*;
	BEGIN
		NEW(ball); got := 0; starts := 0;
		ActorRun.Start(4);
		UnitAssert.True(Drive(TenThousand, 60000), "bench finished");
		UnitAssert.EqualInt(actors, got, "every actor got its message");
		ActorRun.Stop
	END TestSlowTenThousandActors;

	PROCEDURE TestSlowPingPong*;
	BEGIN
		NEW(ball);
		pool := 1; ActorRun.Start(pool);
		UnitAssert.True(Drive(PingPong, 60000), "1 executor finished");
		ActorRun.Stop;
		pool := 4; ActorRun.Start(pool);
		UnitAssert.True(Drive(PingPong, 60000), "4 executors finished");
		UnitAssert.EqualInt(2 * exchanges, hops, "all exchanges done");
		ActorRun.Stop
	END TestSlowPingPong;

END Actor_devBench.
