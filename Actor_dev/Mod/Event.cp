MODULE Actor_devEvent;
(** Тесты потока событий: подписка, предикат, отписка, остановленный
	подписчик, мёртвые письма как события потока. *)

	IMPORT Kernel, Services, Tasks, ActorCore, ActorRun, ActorEvent, UnitAssert;

	CONST
		maxSubs = 16;

	TYPE
		Msg = ActorCore.Msg;
		Put = POINTER TO RECORD (Msg) n: INTEGER END;
		Other = POINTER TO RECORD (Msg) END;
		OnlyPut = POINTER TO RECORD (ActorEvent.Filter) END;
		OnlyDead = POINTER TO RECORD (ActorEvent.Filter) END;
		Sub = POINTER TO RECORD (ActorCore.Actor) id: INTEGER END;
		Silent = POINTER TO RECORD (ActorCore.Actor) END;
		Forwarder = POINTER TO RECORD (ActorCore.Actor) to: ActorCore.Ref END;

	VAR
		counts: ARRAY maxSubs OF INTEGER;
		served, nextId, deadCount, lastDeadN: INTEGER;
		lastTo, lastSender: ActorCore.Ref;
		fwdTo: ActorCore.Ref;
		stream: ActorCore.Ref;
		onlyPut: OnlyPut; onlyDead: OnlyDead;
		put: Put; other: Other;

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

	PROCEDURE Stopped (r: ActorCore.Ref): BOOLEAN;
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

	(* --- предикаты -------------------------------------------------------- *)

	PROCEDURE (f: OnlyPut) Match (m: Msg): BOOLEAN;
	BEGIN
		RETURN m IS Put
	END Match;

	PROCEDURE (f: OnlyDead) Match (m: Msg): BOOLEAN;
	BEGIN
		RETURN m IS ActorEvent.DeadLetter
	END Match;

	(* --- подписчик -------------------------------------------------------- *)

	PROCEDURE (a: Sub) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Put DO INC(counts[a.id]); INC(served); lastDeadN := m.n
		| m: Other DO INC(counts[a.id]); INC(served)
		| m: ActorEvent.DeadLetter DO
			INC(deadCount);
			IF m.m IS Put THEN lastDeadN := m.m(Put).n END;
			lastTo := m.to; lastSender := m.sender
		ELSE
		END
	END Receive;

	PROCEDURE NewSub (): ActorCore.Actor;
		VAR a: Sub;
	BEGIN
		NEW(a); a.id := nextId; INC(nextId); RETURN a
	END NewSub;

	PROCEDURE SpawnSub (): ActorCore.Ref;
	BEGIN
		RETURN ActorCore.Spawn(NIL, ActorCore.NewProps(NewSub), "")
	END SpawnSub;

	PROCEDURE (a: Silent) Receive (c: ActorCore.Context; m: Msg);
	END Receive;

	PROCEDURE NewSilent (): ActorCore.Actor;
		VAR a: Silent;
	BEGIN
		NEW(a); RETURN a
	END NewSilent;

	PROCEDURE (a: Forwarder) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorEvent.DeadLetter DO
			INC(deadCount); ActorCore.Tell(a.to, m)
		ELSE
		END
	END Receive;

	PROCEDURE NewForwarder (): ActorCore.Actor;
		VAR a: Forwarder;
	BEGIN
		NEW(a); a.to := fwdTo; RETURN a
	END NewForwarder;

	PROCEDURE Fixture;
	BEGIN
		served := 0; nextId := 0; deadCount := 0; lastDeadN := 0;
		lastTo := NIL; lastSender := NIL;
		NEW(put); put.n := 7; NEW(other);
		NEW(onlyPut); NEW(onlyDead);
		stream := ActorEvent.New(); Idle(20)
	END Fixture;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO maxSubs - 1 DO counts[i] := 0 END;
		ActorEvent.SetDefault(NIL);
		ActorRun.Start(4)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorEvent.SetDefault(NIL); ActorRun.Shutdown; Idle(5)
	END Teardown;

	(* --- тесты ------------------------------------------------------------ *)

	PROCEDURE TestPublishReachesSubscribers*;
		VAR a, b: ActorCore.Ref;
	BEGIN
		Fixture;
		a := SpawnSub(); b := SpawnSub();
		ActorEvent.Subscribe(stream, a, NIL); ActorEvent.Subscribe(stream, b, NIL);
		ActorEvent.Publish(stream, put); ActorEvent.Publish(stream, put);
		UnitAssert.True(UntilInt(served, 4, 2000), "both subscribers got two letters");
		UnitAssert.EqualInt(2, counts[0], "first subscriber");
		UnitAssert.EqualInt(2, counts[1], "second subscriber")
	END TestPublishReachesSubscribers;

	PROCEDURE TestFilterSelectsLetters*;
		VAR a, b: ActorCore.Ref;
	BEGIN
		Fixture;
		UnitAssert.True(onlyPut.Match(put), "filter passes Put");
		UnitAssert.True(~onlyPut.Match(other), "filter rejects Other");
		a := SpawnSub(); b := SpawnSub();
		ActorEvent.Subscribe(stream, a, NIL);
		ActorEvent.Subscribe(stream, b, onlyPut);
		ActorEvent.Publish(stream, put); ActorEvent.Publish(stream, other);
		UnitAssert.True(UntilInt(served, 3, 2000), "unfiltered and filtered letters");
		UnitAssert.EqualInt(2, counts[0], "all letters");
		UnitAssert.EqualInt(1, counts[1], "only Put passes the filter")
	END TestFilterSelectsLetters;

	PROCEDURE TestUnsubscribeStopsDelivery*;
		VAR a: ActorCore.Ref;
	BEGIN
		Fixture;
		a := SpawnSub();
		ActorEvent.Subscribe(stream, a, NIL);
		ActorEvent.Publish(stream, put);
		UnitAssert.True(UntilInt(served, 1, 2000), "one letter before unsubscribe");
		ActorEvent.Unsubscribe(stream, a);
		Idle(30);	(* письмо отписки разобрано *)
		ActorEvent.Publish(stream, put);
		Idle(50);
		UnitAssert.EqualInt(1, counts[0], "no letter after unsubscribe")
	END TestUnsubscribeStopsDelivery;

	PROCEDURE TestStoppedSubscriberIsRemoved*;
		VAR a, d: ActorCore.Ref;
	BEGIN
		Fixture;
		a := SpawnSub(); d := SpawnSub();
		ActorEvent.Subscribe(stream, a, NIL);
		ActorEvent.Subscribe(stream, d, onlyDead);
		ActorEvent.SetDefault(stream);
		ActorEvent.Publish(stream, put);
		UnitAssert.True(UntilInt(served, 1, 2000), "subscriber was alive");
		ActorCore.StopActor(a);
		IF UntilStopped(a, 1000) THEN END;
		Idle(100);	(* поток получил Terminated и снял подписчика *)
		ActorEvent.Publish(stream, put);
		Idle(100);
		UnitAssert.EqualInt(0, deadCount, "stopped subscriber left the stream")
	END TestStoppedSubscriberIsRemoved;

	PROCEDURE TestDeadLetterPublished*;
		VAR d, target: ActorCore.Ref;
	BEGIN
		Fixture;
		ActorEvent.SetDefault(stream);
		d := SpawnSub(); ActorEvent.Subscribe(stream, d, onlyDead);
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(target);
		IF UntilStopped(target, 1000) THEN END;
		ActorCore.Tell(target, put);
		UnitAssert.True(UntilInt(deadCount, 1, 2000), "dead letter became an event");
		UnitAssert.EqualInt(7, lastDeadN, "the original letter is inside");
		UnitAssert.Same(target, lastTo, "addressee is recorded");
		UnitAssert.Same(NIL, lastSender, "sender is recorded")
	END TestDeadLetterPublished;

	PROCEDURE TestDeadLetterIsNotRepublished*;
		VAR d, target: ActorCore.Ref; before: LONGINT;
	BEGIN
		Fixture;
		ActorEvent.SetDefault(stream);
		d := SpawnSub(); ActorEvent.Subscribe(stream, d, onlyDead);
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(target);
		IF UntilStopped(target, 1000) THEN END;
		before := ActorCore.deadLetters;
		ActorCore.Tell(target, put);
		UnitAssert.True(UntilInt(deadCount, 1, 2000), "one dead letter event");
		Idle(50);
		UnitAssert.EqualInt(1, SHORT(ActorCore.deadLetters - before), "the event is not counted as another dead letter");
		UnitAssert.EqualInt(1, deadCount, "no event about the event")
	END TestDeadLetterIsNotRepublished;

	PROCEDURE TestDeadLetterAboutDeadLetterIsNotPublished*;
		VAR fwd, t1, t2: ActorCore.Ref;
	BEGIN
		Fixture;
		ActorEvent.SetDefault(stream);
		t1 := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(t1); IF UntilStopped(t1, 1000) THEN END;
		t2 := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(t2); IF UntilStopped(t2, 1000) THEN END;
		fwdTo := t2;
		fwd := ActorCore.Spawn(NIL, ActorCore.NewProps(NewForwarder), "");
		ActorEvent.Subscribe(stream, fwd, onlyDead);
		ActorCore.Tell(t1, put);	(* мёртвое письмо; подписчик шлёт его второму мёртвому *)
		UnitAssert.True(UntilInt(deadCount, 1, 2000), "the forwarding subscriber got the event");
		Idle(100);
		UnitAssert.EqualInt(1, deadCount, "no event about the event")
	END TestDeadLetterAboutDeadLetterIsNotPublished;

END Actor_devEvent.
