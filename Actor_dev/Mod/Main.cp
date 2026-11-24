MODULE Actor_devMain;
(** Тесты подсистемы Actor: ящик, надзор, жизненный цикл, исполнители.

	Акторы работают в пуле ActorRun; главный стек крутит
	Services.actionHook.Step (Until). Ask ждёт, поэтому сценарии с Ask
	идут в задаче Driver. *)

	IMPORT Kernel, Services, Tasks, ActorCore, ActorSup, ActorRun, UnitAssert;

	CONST
		evStarted = 1; evStopping = 2; evStopped = 3; evRestarting = 4;
		evTimeout = 5; evMark = 6;
		logLen = 64;

	TYPE
		Msg = ActorCore.Msg;
		Inc = POINTER TO RECORD (Msg) n: INTEGER END;
		Get = POINTER TO RECORD (Msg) END;
		Boom = POINTER TO RECORD (Msg) END;
		Nap = POINTER TO RECORD (Msg) ms: INTEGER END;
		Num = POINTER TO RECORD (Msg) n: INTEGER END;
		Swap = POINTER TO RECORD (Msg) END;
		Boom2 = POINTER TO RECORD (Msg) END;
		SetRt = POINTER TO RECORD (Msg) ms: INTEGER END;
		Mark = POINTER TO RECORD (Msg) END;
		AskMe = POINTER TO RECORD (Msg) END;

		Probe = POINTER TO RECORD (ActorCore.Actor) id, n, last: INTEGER END;
		Sup = POINTER TO RECORD (ActorCore.Actor) END;
		Top = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		SelfAsker = POINTER TO RECORD (ActorCore.Actor) END;
		TestStrategy = POINTER TO RECORD (ActorCore.Strategy)
			napMs, d42, dOther: INTEGER
		END;
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
		get: Get; boom: Boom; swap: Swap; boom2: Boom2; mark: Mark;
		grand: BOOLEAN;	(* проба в Started рождает ребёнка-пробу *)
		napStopping, factoryNap: INTEGER;	(* сон в Stopping и в фабрике, мс *)
		factoryTrap: BOOLEAN;	(* фабрика в задаче трапует 43 *)
		watchTwice, unwatch: BOOLEAN;	(* режим наблюдателя *)
		terms: INTEGER;	(* сколько Terminated получил наблюдатель *)
		napTask: Tasks.Task;	(* задача, исполнявшая последний Nap *)
		midProps: ActorCore.Props;

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

	PROCEDURE NewSetRt (ms: INTEGER): SetRt;
		VAR m: SetRt;
	BEGIN
		NEW(m); m.ms := ms; RETURN m
	END NewSetRt;

	PROCEDURE Stopped (r: ActorCore.Ref): BOOLEAN;
	(* Пост: r остановлен — его имени нет в каталоге. *)
	BEGIN
		RETURN ActorCore.dir.Lookup(r.node, r.name) # r
	END Stopped;

	PROCEDURE UntilStopped (r: ActorCore.Ref; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE ~Stopped(r) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN Stopped(r)
	END UntilStopped;

	PROCEDURE UntilInt (VAR x: INTEGER; want: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (x # want) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN x = want
	END UntilInt;

	(* --- акторы ----------------------------------------------------------- *)

	PROCEDURE (b: Negate) Receive (a: ActorCore.Actor; c: ActorCore.Context; m: Msg);
		VAR r: Num;
	BEGIN
		WITH m: Get DO NEW(r); r.n := -a(Probe).n; c.Respond(r)
		| m: Swap DO c.Become(NIL)
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE^ NewProbe (): ActorCore.Actor;

	PROCEDURE (p: Probe) Receive (c: ActorCore.Context; m: Msg);
		VAR r: Num; neg: Negate;
	BEGIN
		WITH m: ActorCore.Started DO
			INC(starts); Log(p.id, evStarted);
			IF grand & (c.Parent() # NIL) & (c.name[LEN(c.name$) - 1] # "g") THEN	(* внук: имя "…/g" *)
				IF ActorCore.Spawn(c, ActorCore.NewProps(NewProbe), "g") = NIL THEN END
			END
		| m: ActorCore.Stopping DO
			Log(p.id, evStopping);
			IF napStopping > 0 THEN Tasks.Sleep(napStopping) END
		| m: ActorCore.Stopped DO Log(p.id, evStopped)
		| m: ActorCore.Restarting DO Log(p.id, evRestarting)
		| m: ActorCore.ReceiveTimeout DO INC(timeouts); Log(p.id, evTimeout)
		| m: SetRt DO c.SetReceiveTimeout(m.ms)
		| m: Mark DO Log(p.id, evMark)
		| m: Boom2 DO HALT(43)
		| m: Inc DO
			IF (m.n > 0) & (m.n # p.last + 1) THEN ordered := FALSE END;
			p.last := m.n; INC(p.n)
		| m: Get DO NEW(r); r.n := p.n; c.Respond(r)
		| m: Boom DO HALT(42)
		| m: Nap DO
			INC(inside); IF inside > maxInside THEN maxInside := inside END;
			napTask := Tasks.Current();
			Tasks.Sleep(m.ms DIV 2); Tasks.Sleep(m.ms - m.ms DIV 2);	(* две точки переключения *)
			DEC(inside); INC(naps); napEnd := Kernel.Time()
		| m: Swap DO NEW(neg); c.Become(neg)
		ELSE
		END
	END Receive;

	PROCEDURE NewProbe (): ActorCore.Actor;
		VAR p: Probe;
	BEGIN
		IF Tasks.InTask() THEN
			IF factoryNap > 0 THEN Tasks.Sleep(factoryNap) END;
			IF factoryTrap THEN HALT(43) END
		END;
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

	PROCEDURE (t: Top) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorCore.Started DO IF ActorCore.Spawn(c, midProps, "mid") = NIL THEN END
		ELSE
		END
	END Receive;

	PROCEDURE NewTop (): ActorCore.Actor;
		VAR t: Top;
	BEGIN
		NEW(t); RETURN t
	END NewTop;

	PROCEDURE (s: TestStrategy) Decide (child: ActorCore.Ref; reason: INTEGER; VAR st: ActorCore.Stats): INTEGER;
	BEGIN
		IF s.napMs > 0 THEN Tasks.Sleep(s.napMs) END;
		IF reason = 42 THEN RETURN s.d42 ELSE RETURN s.dOther END
	END Decide;

	PROCEDURE NewStrategy (napMs, d42, dOther: INTEGER): ActorCore.Strategy;
		VAR s: TestStrategy;
	BEGIN
		NEW(s); s.napMs := napMs; s.d42 := d42; s.dOther := dOther; RETURN s
	END NewStrategy;

	PROCEDURE (w: Watcher) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorCore.Started DO
			c.Watch(watched);
			IF watchTwice THEN c.Watch(watched) END;
			IF unwatch THEN c.Unwatch(watched) END
		| m: ActorCore.Terminated DO IF m.who = watched THEN terminated := TRUE; INC(terms) END
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

	PROCEDURE (a: SelfAsker) Receive (c: ActorCore.Context; m: Msg);
		VAR r: Msg;
	BEGIN
		IF m IS AskMe THEN askRes := ActorCore.Ask(c, get, 300, r) END
	END Receive;

	PROCEDURE NewSelfAsker (): ActorCore.Actor;
		VAR a: SelfAsker;
	BEGIN
		NEW(a); RETURN a
	END NewSelfAsker;

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
		grand := FALSE; napStopping := 0; factoryNap := 0; factoryTrap := FALSE;
		watchTwice := FALSE; unwatch := FALSE; terms := 0; napTask := NIL;
		NEW(get); NEW(boom); NEW(swap); NEW(boom2); NEW(mark);
		ActorRun.Start(4)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		napStopping := 0; factoryNap := 0; factoryTrap := FALSE;
		ActorRun.Shutdown; Idle(5)
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

	(* --- 2. Restart ------------------------------------------------------- *)

	PROCEDURE RestartBody;
	BEGIN
		ActorCore.Tell(target, NewInc(0)); ActorCore.Tell(target, NewInc(0)); ActorCore.Tell(target, NewInc(0));
		ActorCore.Tell(target, boom);
		ActorCore.Tell(target, NewInc(0));
		AskInt(target, 2000)
	END RestartBody;

	PROCEDURE TestTrapRestartsWithFreshState*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000), 1) = NIL THEN END;
		target := kids[0];
		UnitAssert.True(Drive(RestartBody, 3000), "driver finished");
		UnitAssert.EqualInt(2, starts, "Started again after trap");
		UnitAssert.True(At(1, evRestarting) >= 0, "old instance got Restarting");
		UnitAssert.EqualInt(1, askVal, "state reset, next message handled by new instance")
	END TestTrapRestartsWithFreshState;

	(* --- 3. предел перезапусков ------------------------------------------- *)

	PROCEDURE TestTooManyRestartsStopsAndNotifiesWatcher*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 2, 10000), 1) = NIL THEN END;
		WatchIt(kids[0]);
		ActorCore.Tell(kids[0], boom); ActorCore.Tell(kids[0], boom); ActorCore.Tell(kids[0], boom);
		UnitAssert.True(Until(terminated, 2000), "watcher got Terminated");
		UnitAssert.EqualInt(3, starts, "two restarts, then Stop");
		UnitAssert.True(Stopped(kids[0]), "child stopped")
	END TestTooManyRestartsStopsAndNotifiesWatcher;

	(* --- 4. Resume и Escalate --------------------------------------------- *)

	PROCEDURE ResumeBody;
	BEGIN
		ActorCore.Tell(target, NewInc(0)); ActorCore.Tell(target, NewInc(0));
		ActorCore.Tell(target, boom);
		ActorCore.Tell(target, NewInc(0));
		AskInt(target, 2000)
	END ResumeBody;

	PROCEDURE TestResumeKeepsStateSkipsCulprit*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Resume, 0, 1000), 1) = NIL THEN END;
		target := kids[0];
		UnitAssert.True(Drive(ResumeBody, 3000), "driver finished");
		UnitAssert.EqualInt(1, starts, "no restart");
		UnitAssert.EqualInt(3, askVal, "state kept, culprit skipped")
	END TestResumeKeepsStateSkipsCulprit;

	PROCEDURE TestEscalateReachesRootWhichStops*;
		VAR top: ActorCore.Ref;
	BEGIN
		top := SpawnSup(ActorSup.NewOneForOne(ActorCore.Escalate, 0, 1000), 1);
		WatchIt(top);
		ActorCore.Tell(kids[0], boom);
		UnitAssert.True(Until(terminated, 2000), "root stopped the top actor");
		UnitAssert.True(At(1, evStopped) >= 0, "child stopped with its parent");
		UnitAssert.EqualInt(1, starts, "nobody restarted")
	END TestEscalateReachesRootWhichStops;

	(* --- 5. остановка родителя -------------------------------------------- *)

	PROCEDURE TestStopParentStopsChildrenFirst*;
		VAR sup: ActorCore.Ref; p: INTEGER;
	BEGIN
		sup := SpawnSup(NIL, 2);
		ActorCore.StopActor(sup);
		IF UntilStopped(sup, 5000) THEN END;
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
		VAR d0: LONGINT;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		ActorCore.StopActor(target);
		IF UntilStopped(target, 5000) THEN END;
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
		ActorCore.Tell(target, NewSetRt(100));
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

	(* --- отмена исполнителя в шагах жизненного цикла и надзора ------------ *)

	PROCEDURE RestartPool;
	(* Пост: задачи пула отменены и запущен новый пул из одного исполнителя. *)
	BEGIN
		Idle(30); ActorRun.Stop; Idle(10); ActorRun.Start(1)
	END RestartPool;

	PROCEDURE TestCancelInStoppingFinishesStop*;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		napStopping := 300;
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		WatchIt(target);
		ActorCore.StopActor(target);
		RestartPool;
		UnitAssert.True(Until(terminated, 2000), "watcher got Terminated");
		UnitAssert.True(Stopped(target), "actor stopped, not stuck in stopping")
	END TestCancelInStoppingFinishesStop;

	PROCEDURE TestCancelInDecideKeepsFailure*;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		IF SpawnSup(NewStrategy(300, ActorCore.Restart, ActorCore.Restart), 1) = NIL THEN END;
		ActorCore.Tell(kids[0], boom);
		RestartPool;
		UnitAssert.True(UntilInt(starts, 2, 2000), "decision made after cancel, child restarted")
	END TestCancelInDecideKeepsFailure;

	PROCEDURE TestCancelInFactoryRetries*;
	BEGIN
		ActorRun.Stop; ActorRun.Start(1);
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000), 1) = NIL THEN END;
		factoryNap := 300;
		ActorCore.Tell(kids[0], boom);
		RestartPool;
		UnitAssert.True(UntilInt(starts, 2, 2000), "factory repeated after cancel")
	END TestCancelInFactoryRetries;

	(* --- отмена своей задачи актора ---------------------------------------- *)

	PROCEDURE IncAskBody;
	BEGIN
		ActorCore.Tell(target, NewInc(0)); AskInt(target, 1000)
	END IncAskBody;

	PROCEDURE TestCancelledDedicatedTaskStillAnswers*;
		VAR p: ActorCore.Props;
	BEGIN
		p := ActorCore.NewProps(NewProbe); p.dedicated := TRUE;
		target := ActorCore.Spawn(NIL, p, "");
		ActorCore.Tell(target, NewNap(500)); ActorCore.Tell(target, NewInc(0));	(* письмо ждёт за сном *)
		Idle(30);
		UnitAssert.True(napTask # NIL, "handler waits in its own task");
		Tasks.Cancel(napTask); Idle(10);
		UnitAssert.True(Drive(IncAskBody, 2000), "driver finished");
		UnitAssert.EqualInt(ActorCore.ok, askRes, "actor answers after its task was cancelled");
		UnitAssert.EqualInt(2, askVal, "waiting and next messages handled")
	END TestCancelledDedicatedTaskStillAnswers;

	(* --- общая остановка --------------------------------------------------- *)

	PROCEDURE TestShutdownLeavesNoTasks*;
		VAR p: ActorCore.Props; own, sup: ActorCore.Ref; t: LONGINT;
	BEGIN
		p := ActorCore.NewProps(NewProbe); p.dedicated := TRUE;
		own := ActorCore.Spawn(NIL, p, "");
		sup := SpawnSup(NIL, 2);
		ActorRun.Shutdown;
		t := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t <= 1000) DO Services.actionHook.Step END;
		UnitAssert.True(Stopped(own) & Stopped(sup) & Stopped(kids[0]), "all actors stopped");
		UnitAssert.EqualInt(0, ActorRun.n, "pool stopped");
		UnitAssert.EqualInt(0, Tasks.Count(), "no tasks left: no deadlock at exit");
		UnitAssert.True(~Tasks.Deadlocked(), "scheduler saw no deadlock")
	END TestShutdownLeavesNoTasks;

	(* --- фабрика упала при перезапуске, стратегия ответила Resume ---------- *)

	PROCEDURE TestFactoryFailAfterRestartStopsOnResume*;
	BEGIN
		IF SpawnSup(NewStrategy(0, ActorCore.Restart, ActorCore.Resume), 1) = NIL THEN END;
		WatchIt(kids[0]);
		factoryTrap := TRUE;
		ActorCore.Tell(kids[0], boom); ActorCore.Tell(kids[0], NewInc(0));
		UnitAssert.True(Until(terminated, 2000), "child without instance stopped, not resumed")
	END TestFactoryFailAfterRestartStopsOnResume;

	(* --- тайм-аут простоя: приостановка и ждущие письма -------------------- *)

	PROCEDURE TestTimeoutNotLostWhileSuspended*;
	BEGIN
		IF SpawnSup(NewStrategy(150, ActorCore.Resume, ActorCore.Resume), 1) = NIL THEN END;
		ActorCore.Tell(kids[0], NewSetRt(50)); ActorCore.Tell(kids[0], boom);
		UnitAssert.True(UntilInt(timeouts, 1, 1000), "timeout re-armed after Resume")
	END TestTimeoutNotLostWhileSuspended;

	PROCEDURE TestTimeoutDoesNotOvertakeMail*;
		VAR mk, tm: INTEGER;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewProbe), "");
		ActorCore.Tell(target, NewSetRt(30)); ActorCore.Tell(target, NewNap(100)); ActorCore.Tell(target, mark);
		Idle(300);
		mk := At(1, evMark); tm := At(1, evTimeout);
		UnitAssert.True(mk >= 0, "mark handled");
		UnitAssert.True((tm < 0) OR (tm > mk), "timeout does not overtake a waiting message")
	END TestTimeoutDoesNotOvertakeMail;

	(* --- наблюдение -------------------------------------------------------- *)

	PROCEDURE TestWatchTwiceGivesOneTerminated*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		watchTwice := TRUE; WatchIt(target);
		ActorCore.StopActor(target);
		UnitAssert.True(Until(terminated, 1000), "Terminated came");
		Idle(30);
		UnitAssert.EqualInt(1, terms, "one Terminated for two Watch")
	END TestWatchTwiceGivesOneTerminated;

	PROCEDURE TestUnwatchGivesNoTerminated*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		unwatch := TRUE; WatchIt(target);
		ActorCore.StopActor(target);
		IF UntilStopped(target, 1000) THEN END;
		Idle(30);
		UnitAssert.EqualInt(0, terms, "no Terminated after Unwatch")
	END TestUnwatchGivesNoTerminated;

	PROCEDURE TestWatchStoppedGetsTerminatedAtOnce*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(target);
		IF UntilStopped(target, 1000) THEN END;
		WatchIt(target);
		UnitAssert.True(Until(terminated, 200), "Terminated at once for a stopped actor")
	END TestWatchStoppedGetsTerminatedAtOnce;

	(* --- перезапуск -------------------------------------------------------- *)

	PROCEDURE TestRestartStopsGrandchildrenFirst*;
		VAR gs, ns: INTEGER;
	BEGIN
		grand := TRUE;
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000), 1) = NIL THEN END;
		IF UntilInt(starts, 2, 1000) THEN END;	(* ребёнок 1 и внук 2 *)
		ActorCore.Tell(kids[0], boom);
		IF UntilInt(starts, 4, 1000) THEN END;	(* новый ребёнок 3 и его внук 4 *)
		gs := At(2, evStopped); ns := At(3, evStarted);
		UnitAssert.True(gs >= 0, "grandchild stopped");
		UnitAssert.True(ns >= 0, "new instance started");
		UnitAssert.True(gs < ns, "grandchild Stopped before new instance Started")
	END TestRestartStopsGrandchildrenFirst;

	PROCEDURE TestRestartWindowResets*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 1, 100), 1) = NIL THEN END;
		ActorCore.Tell(kids[0], boom);
		IF UntilInt(starts, 2, 1000) THEN END;
		Idle(200);
		ActorCore.Tell(kids[0], boom);
		UnitAssert.True(UntilInt(starts, 3, 1000), "failure after the window restarts again");
		UnitAssert.True(~Stopped(kids[0]), "child not stopped")
	END TestRestartWindowResets;

	PROCEDURE RestartBecomeBody;
	BEGIN
		ActorCore.Tell(target, swap); ActorCore.Tell(target, boom); ActorCore.Tell(target, NewInc(0));
		AskInt(target, 1000)
	END RestartBecomeBody;

	PROCEDURE TestRestartDropsBecome*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000), 1) = NIL THEN END;
		target := kids[0];
		UnitAssert.True(Drive(RestartBecomeBody, 2000), "driver finished");
		UnitAssert.EqualInt(1, askVal, "new instance answers with Receive, not old Become")
	END TestRestartDropsBecome;

	PROCEDURE TestRestartDropsReceiveTimeout*;
	BEGIN
		IF SpawnSup(ActorSup.NewOneForOne(ActorCore.Restart, 5, 10000), 1) = NIL THEN END;
		ActorCore.Tell(kids[0], NewSetRt(30)); ActorCore.Tell(kids[0], boom);
		IF UntilInt(starts, 2, 1000) THEN END;
		ActorCore.Tell(kids[0], NewInc(0));
		Idle(200);
		UnitAssert.EqualInt(0, timeouts, "no timeout after restart")
	END TestRestartDropsReceiveTimeout;

	(* --- приостановленный родитель откладывает решения --------------------- *)

	PROCEDURE TestSuspendedParentDefersDecisions*;
		VAR p: ActorCore.Props; top: ActorCore.Ref; mid: ActorCore.Context;
	BEGIN
		kidProps := ActorCore.NewProps(NewProbe); nKids := 2;
		midProps := ActorCore.NewProps(NewSup);
		midProps.strategy := NewStrategy(0, ActorCore.Escalate, ActorCore.Restart);
		p := ActorCore.NewProps(NewTop); p.strategy := NewStrategy(200, ActorCore.Resume, ActorCore.Resume);
		top := ActorCore.Spawn(NIL, p, "top");
		IF UntilInt(starts, 2, 1000) THEN END;
		ActorCore.Tell(kids[0], boom);	(* 42: mid эскалирует, top решает 200 мс *)
		Idle(20);
		ActorCore.Tell(kids[1], boom2);	(* 43: решение mid — Restart *)
		Idle(80);
		UnitAssert.EqualInt(2, starts, "suspended parent made no decision");
		UnitAssert.True(UntilInt(starts, 3, 1000), "decision made after resume")
	END TestSuspendedParentDefersDecisions;

	(* --- Ask: себе и к остановившемуся без ответа -------------------------- *)

	PROCEDURE TestAskSelfTraps*;
		VAR m: AskMe;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSelfAsker), "");
		WatchIt(target);
		NEW(m); ActorCore.Tell(target, m);
		UnitAssert.True(Until(terminated, 150), "Ask to itself traps at once, root stops it")
	END TestAskSelfTraps;

	PROCEDURE AskSilentBody;
	BEGIN
		t0 := Kernel.Time(); AskInt(target, 2000); t0 := Kernel.Time() - t0
	END AskSilentBody;

	PROCEDURE TestAskDeadWhenTargetStopsWithoutReply*;
		VAR d: Driver;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		driverDone := FALSE; NEW(d); d.body := AskSilentBody; Tasks.Start(d);
		Idle(50);
		ActorCore.StopActor(target);
		UnitAssert.True(Until(driverDone, 1500), "Ask returned before its timeout");
		UnitAssert.EqualInt(ActorCore.dead, askRes, "dead");
		UnitAssert.True(t0 < 1000, "without waiting the timeout")
	END TestAskDeadWhenTargetStopsWithoutReply;

	(* --- предусловия ------------------------------------------------------- *)

	PROCEDURE SetTimeoutOutside;
	BEGIN
		target(ActorCore.Context).SetReceiveTimeout(10)
	END SetTimeoutOutside;

	PROCEDURE BecomeOutside;
	BEGIN
		target(ActorCore.Context).Become(NIL)
	END BecomeOutside;

	PROCEDURE TestHandlerOnlyCallsTrapOutside*;
	BEGIN
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		UnitAssert.Traps(SetTimeoutOutside, "SetReceiveTimeout outside the handler traps");
		UnitAssert.Traps(BecomeOutside, "Become outside the handler traps")
	END TestHandlerOnlyCallsTrapOutside;

END Actor_devMain.
