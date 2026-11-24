MODULE Actor_devMain;
(** Тесты подсистемы Actor: ящик, надзор, жизненный цикл, исполнители.

	Акторы работают в пуле ActorRun; главный стек крутит
	Services.actionHook.Step (Until). Ask ждёт, поэтому сценарии с Ask
	идут в задаче Driver. *)

	IMPORT Kernel, Services, Tasks, ActorCore, ActorRun, UnitAssert;

	CONST
		evStarted = 1; evStopping = 2; evStopped = 3; evRestarting = 4;
		logLen = 64;

	TYPE
		Msg = ActorCore.Msg;
		Inc = POINTER TO RECORD (Msg) n: INTEGER END;
		Get = POINTER TO RECORD (Msg) END;
		Boom = POINTER TO RECORD (Msg) END;
		Nap = POINTER TO RECORD (Msg) ms: INTEGER END;
		Num = POINTER TO RECORD (Msg) n: INTEGER END;
		Swap = POINTER TO RECORD (Msg) END;

		Probe = POINTER TO RECORD (ActorCore.Actor) id, n, last: INTEGER END;
		Sup = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		Silent = POINTER TO RECORD (ActorCore.Actor) END;
		Negate = POINTER TO RECORD (ActorCore.Behavior) END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		nextId, starts, naps, inside, maxInside, timeouts: INTEGER;
		ordered, terminated, driverDone: BOOLEAN;
		log: ARRAY logLen OF INTEGER; nLog: INTEGER;
		kidProps: ActorCore.Props;
		nKids: INTEGER;
		kids: ARRAY 4 OF ActorCore.Ref;
		watched, target: ActorCore.Ref;
		askRes, askVal: INTEGER;
		t0, napEnd, otherAt: LONGINT;
		get: Get; boom: Boom; swap: Swap;

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

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		RETURN Until(driverDone, ms)
	END Drive;

	PROCEDURE Log (id, ev: INTEGER);
	BEGIN
		IF nLog < logLen THEN log[nLog] := id * 10 + ev; INC(nLog) END
	END Log;

	PROCEDURE At (id, ev: INTEGER): INTEGER;
	(* Пост: номер записи (id, ev) в журнале; -1 — нет. *)
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < nLog) & (log[i] # id * 10 + ev) DO INC(i) END;
		IF i = nLog THEN RETURN -1 ELSE RETURN i END
	END At;

	PROCEDURE NewInc (n: INTEGER): Inc;
		VAR m: Inc;
	BEGIN
		NEW(m); m.n := n; RETURN m
	END NewInc;

	PROCEDURE NewNap (ms: INTEGER): Nap;
		VAR m: Nap;
	BEGIN
		NEW(m); m.ms := ms; RETURN m
	END NewNap;

	(* --- акторы ----------------------------------------------------------- *)

	PROCEDURE (b: Negate) Receive (a: ActorCore.Actor; c: ActorCore.Context; m: Msg);
		VAR r: Num;
	BEGIN
		WITH m: Get DO NEW(r); r.n := -a(Probe).n; c.Respond(r)
		| m: Swap DO c.Become(NIL)
		ELSE
		END
	END Receive;

	PROCEDURE (p: Probe) Receive (c: ActorCore.Context; m: Msg);
		VAR r: Num; neg: Negate;
	BEGIN
		WITH m: ActorCore.Started DO INC(starts); Log(p.id, evStarted)
		| m: ActorCore.Stopping DO Log(p.id, evStopping)
		| m: ActorCore.Stopped DO Log(p.id, evStopped)
		| m: ActorCore.Restarting DO Log(p.id, evRestarting)
		| m: ActorCore.ReceiveTimeout DO INC(timeouts)
		| m: Inc DO
			IF (m.n > 0) & (m.n # p.last + 1) THEN ordered := FALSE END;
			p.last := m.n; INC(p.n)
		| m: Get DO NEW(r); r.n := p.n; c.Respond(r)
		| m: Boom DO HALT(42)
		| m: Nap DO
			INC(inside); IF inside > maxInside THEN maxInside := inside END;
			Tasks.Sleep(m.ms DIV 2); Tasks.Sleep(m.ms - m.ms DIV 2);	(* две точки переключения *)
			DEC(inside); INC(naps); napEnd := Kernel.Time()
		| m: Swap DO NEW(neg); c.Become(neg)
		ELSE
		END
	END Receive;

	PROCEDURE NewProbe (): ActorCore.Actor;
		VAR p: Probe;
	BEGIN
		NEW(p); INC(nextId); p.id := nextId; RETURN p
	END NewProbe;

	PROCEDURE (s: Sup) Receive (c: ActorCore.Context; m: Msg);
		VAR i: INTEGER;
	BEGIN
		WITH m: ActorCore.Started DO
			FOR i := 0 TO nKids - 1 DO kids[i] := ActorCore.Spawn(c, kidProps, "") END
		| m: ActorCore.Stopping DO Log(0, evStopping)
		| m: ActorCore.Stopped DO Log(0, evStopped)
		ELSE
		END
	END Receive;

	PROCEDURE NewSup (): ActorCore.Actor;
		VAR s: Sup;
	BEGIN
		NEW(s); RETURN s
	END NewSup;

	PROCEDURE (w: Watcher) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorCore.Started DO c.Watch(watched)
		| m: ActorCore.Terminated DO IF m.who = watched THEN terminated := TRUE END
		ELSE
		END
	END Receive;

	PROCEDURE NewWatcher (): ActorCore.Actor;
		VAR w: Watcher;
	BEGIN
		NEW(w); RETURN w
	END NewWatcher;

	PROCEDURE (s: Silent) Receive (c: ActorCore.Context; m: Msg);
	END Receive;

	PROCEDURE NewSilent (): ActorCore.Actor;
		VAR s: Silent;
	BEGIN
		NEW(s); RETURN s
	END NewSilent;

	PROCEDURE SpawnSup (strategy: ActorCore.Strategy; kids: INTEGER): ActorCore.Ref;
	(* Пост: супервизор со стратегией strategy и kids детьми-пробами
	   запущен, дети созданы. *)
		VAR p: ActorCore.Props; r: ActorCore.Ref; ready: BOOLEAN;
	BEGIN
		kidProps := ActorCore.NewProps(NewProbe); nKids := kids;
		p := ActorCore.NewProps(NewSup); p.strategy := strategy;
		r := ActorCore.Spawn(NIL, p, "");
		ready := FALSE;
		WHILE ~ready DO Services.actionHook.Step; ready := starts >= kids END;
		RETURN r
	END SpawnSup;

	PROCEDURE WatchIt (r: ActorCore.Ref);
		VAR w: ActorCore.Ref;
	BEGIN
		watched := r; w := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWatcher), ""); Idle(20)
	END WatchIt;

	PROCEDURE AskInt (to: ActorCore.Ref; ms: LONGINT);
	(* В задаче. Пост: askRes — итог Ask(Get), askVal — число ответа. *)
		VAR r: Msg;
	BEGIN
		askRes := ActorCore.Ask(to, get, ms, r); askVal := MIN(INTEGER);
		IF (r # NIL) & (r IS Num) THEN askVal := r(Num).n END
	END AskInt;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		nextId := 0; starts := 0; naps := 0; inside := 0; maxInside := 0; timeouts := 0;
		ordered := TRUE; terminated := FALSE; nLog := 0; askRes := -1; askVal := 0;
		watched := NIL; target := NIL; napEnd := 0; otherAt := 0;
		NEW(get); NEW(boom); NEW(swap);
		ActorRun.Start(4)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Stop; Idle(5)
	END Teardown;

	(* --- 1. счётчик ------------------------------------------------------- *)

	PROCEDURE CounterBody;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 1000 DO ActorCore.Tell(target, NewInc(i)) END;
		AskInt(target, 2000)
	END CounterBody;

	PROCEDURE TestCounterOrderAndAsk*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		UnitAssert.True(Drive(CounterBody, 3000), "driver finished");
		UnitAssert.EqualInt(ActorCore.ok, askRes, "Ask answered");
		UnitAssert.EqualInt(1000, askVal, "1000 Tell counted before Ask");
		UnitAssert.True(ordered, "FIFO from one sender")
	END TestCounterOrderAndAsk;

	(* --- 5. остановка родителя -------------------------------------------- *)

	PROCEDURE TestStopParentStopsChildrenFirst*;
		VAR sup: ActorCore.Ref; done: BOOLEAN; p: INTEGER;
	BEGIN
		sup := SpawnSup(NIL, 2);
		ActorCore.StopActor(sup);
		done := FALSE;
		WHILE ~done DO Services.actionHook.Step; done := sup(ActorCore.Context).IsStopped() END;
		p := At(0, evStopped);
		UnitAssert.True(p >= 0, "parent got Stopped");
		UnitAssert.True((At(1, evStopping) >= 0) & (At(1, evStopping) < p), "child 1 Stopping before parent Stopped");
		UnitAssert.True((At(1, evStopped) >= 0) & (At(1, evStopped) < p), "child 1 Stopped before parent Stopped");
		UnitAssert.True((At(2, evStopped) >= 0) & (At(2, evStopped) < p), "child 2 Stopped before parent Stopped")
	END TestStopParentStopsChildrenFirst;

	(* --- 6. Ask: тайм-аут и мёртвый адресат -------------------------------- *)

	PROCEDURE SilentBody;
	BEGIN
		t0 := Kernel.Time(); AskInt(target, 50); t0 := Kernel.Time() - t0
	END SilentBody;

	PROCEDURE TestAskTimesOut*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		UnitAssert.True(Drive(SilentBody, 2000), "driver finished");
		UnitAssert.EqualInt(ActorCore.timedOut, askRes, "no answer");
		UnitAssert.True(t0 >= 50, "waited the timeout")
	END TestAskTimesOut;

	PROCEDURE DeadBody;
	BEGIN
		ActorCore.Tell(target, NewInc(0));
		t0 := Kernel.Time(); AskInt(target, 1000); t0 := Kernel.Time() - t0
	END DeadBody;

	PROCEDURE TestStoppedActorGivesDeadLetters*;
		VAR d0: LONGINT; done: BOOLEAN;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		ActorCore.StopActor(target);
		done := FALSE;
		WHILE ~done DO Services.actionHook.Step; done := target(ActorCore.Context).IsStopped() END;
		d0 := ActorCore.deadLetters;
		UnitAssert.True(Drive(DeadBody, 2000), "sender did not trap");
		UnitAssert.EqualInt(ActorCore.dead, askRes, "Ask learns of dead letter");
		UnitAssert.True(t0 < 500, "without waiting the timeout");
		UnitAssert.EqualInt(2, ActorCore.deadLetters - d0, "Tell and Ask counted")
	END TestStoppedActorGivesDeadLetters;

	(* --- 7. предел ящика -------------------------------------------------- *)

	PROCEDURE TestCapacityOverflowGoesToDeadLetters*;
		VAR p: ActorCore.Props; d0: LONGINT; i: INTEGER;
	BEGIN
		p := ActorCore.NewProps(NewProbe); p.capacity := 5;
		target := ActorCore.Spawn(NIL, p, "");
		d0 := ActorCore.deadLetters;
		FOR i := 1 TO 20 DO ActorCore.Tell(target, NewInc(i)) END;
		UnitAssert.EqualInt(15, ActorCore.deadLetters - d0, "15 over capacity");
		UnitAssert.True(Drive(SilentBody, 2000), "driver finished");
		UnitAssert.EqualInt(5, askVal, "5 accepted")
	END TestCapacityOverflowGoesToDeadLetters;

	(* --- 8. тайм-аут простоя ---------------------------------------------- *)

	PROCEDURE TimeoutBody;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO ActorCore.Tell(target, NewInc(0)); Tasks.Sleep(20) END;
		askVal := timeouts;	(* за 200 мс с письмами каждые 20 мс *)
		Tasks.Sleep(250);
		askRes := timeouts;	(* после простоя *)
		Tasks.Sleep(250)
	END TimeoutBody;

	PROCEDURE TestReceiveTimeoutOnceAfterIdle*;
		VAR done: BOOLEAN;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		done := FALSE;
		WHILE ~done DO Services.actionHook.Step; done := starts = 1 END;
		target(ActorCore.Context).SetReceiveTimeout(100);
		UnitAssert.True(Drive(TimeoutBody, 3000), "driver finished");
		UnitAssert.EqualInt(0, askVal, "messages reset the timeout");
		UnitAssert.EqualInt(1, askRes, "one timeout after idle");
		UnitAssert.EqualInt(1, timeouts, "not repeated without a message")
	END TestReceiveTimeoutOnceAfterIdle;

	(* --- 9. ожидание внутри Receive --------------------------------------- *)

	PROCEDURE OtherBody;
	BEGIN
		AskInt(kids[1], 1000); otherAt := Kernel.Time()
	END OtherBody;

	PROCEDURE NapScenario (dedicated: BOOLEAN);
		VAR p: ActorCore.Props;
	BEGIN
		p := ActorCore.NewProps(NewProbe); p.dedicated := dedicated;
		kids[0] := ActorCore.Spawn(NIL, p, "");
		kids[1] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		ActorCore.Tell(kids[0], NewNap(100)); ActorCore.Tell(kids[0], NewNap(100));
		Idle(10);
		UnitAssert.True(Drive(OtherBody, 1000), "other actor answered");
		UnitAssert.True(Until(driverDone, 0) & (naps = 0), "answered while the first nap goes on");
		WHILE naps < 2 DO Services.actionHook.Step END;
		UnitAssert.EqualInt(1, maxInside, "one handler at a time");
		UnitAssert.True(otherAt < napEnd, "other actor served during the naps")
	END NapScenario;

	PROCEDURE TestBusyActorGetsNoSecondMessage*;
	BEGIN
		NapScenario(FALSE)
	END TestBusyActorGetsNoSecondMessage;

	PROCEDURE TestDedicatedLeavesPoolFree*;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		NapScenario(TRUE)
	END TestDedicatedLeavesPoolFree;

	(* --- смена поведения, отмена исполнителя, каталог ---------------------- *)

	PROCEDURE BecomeBody;
		VAR a: INTEGER;
	BEGIN
		ActorCore.Tell(target, NewInc(0)); ActorCore.Tell(target, NewInc(0));
		ActorCore.Tell(target, swap);
		AskInt(target, 1000); a := askVal;
		ActorCore.Tell(target, swap);
		AskInt(target, 1000); askRes := a
	END BecomeBody;

	PROCEDURE TestBecomeSwitchesHandler*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		UnitAssert.True(Drive(BecomeBody, 2000), "driver finished");
		UnitAssert.EqualInt(-2, askRes, "new behavior answers");
		UnitAssert.EqualInt(2, askVal, "Become(NIL) restores Receive")
	END TestBecomeSwitchesHandler;

	PROCEDURE TestCancelledExecutorIsNotFailure*;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		ActorCore.Tell(target, NewNap(500)); ActorCore.Tell(target, NewInc(0));
		Idle(20);
		UnitAssert.EqualInt(1, inside, "handler waits inside Receive");
		ActorRun.Stop; Idle(20);
		ActorRun.Start(1);
		UnitAssert.True(Drive(SilentBody, 2000), "driver finished");
		UnitAssert.EqualInt(1, starts, "no restart: cancel is not a failure");
		UnitAssert.EqualInt(0, naps, "interrupted message is lost");
		UnitAssert.EqualInt(1, askVal, "next message handled")
	END TestCancelledExecutorIsNotFailure;

	PROCEDURE TestLookupLocalName*;
		VAR r: ActorCore.Ref;
	BEGIN
		r := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "lookup-me");
		UnitAssert.Same(r, ActorCore.dir.Lookup(ActorCore.node, "lookup-me"), "local name found");
		UnitAssert.Same(NIL, ActorCore.dir.Lookup("other", "lookup-me"), "other node is not local");
		ActorCore.StopActor(r); Idle(20);
		UnitAssert.Same(NIL, ActorCore.dir.Lookup(ActorCore.node, "lookup-me"), "stopped actor leaves the directory")
	END TestLookupLocalName;

END Actor_devMain.
