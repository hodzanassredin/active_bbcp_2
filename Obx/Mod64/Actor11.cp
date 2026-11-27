MODULE ObxActor11;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun, ActorEvent;

	TYPE
		Order = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Info = POINTER TO RECORD (ActorCore.Msg) END;
		OrdersOnly = POINTER TO RECORD (ActorEvent.Filter) END;
		Sub = POINTER TO RECORD (ActorCore.Actor) id: INTEGER END;
		Silent = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		stream: ActorCore.Ref;
		counts: ARRAY 2 OF INTEGER;
		served, dead, deadOrder, nextId: INTEGER;
		onlyOrders: OrdersOnly;

	PROCEDURE (f: OrdersOnly) Match (m: ActorCore.Msg): BOOLEAN;
	BEGIN
		RETURN m IS Order
	END Match;

	PROCEDURE (a: Sub) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Order DO INC(counts[a.id]); INC(served)
		| m: Info DO INC(counts[a.id]); INC(served)
		| m: ActorEvent.DeadLetter DO
			INC(dead);
			IF m.m IS Order THEN deadOrder := m.m(Order).n END
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

	PROCEDURE (a: Silent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	END Receive;

	PROCEDURE NewSilent (): ActorCore.Actor;
		VAR a: Silent;
	BEGIN
		NEW(a); RETURN a
	END NewSilent;

	PROCEDURE Wait (n: INTEGER);
	BEGIN
		WHILE served < n DO Tasks.Sleep(1) END
	END Wait;

	PROCEDURE Say (IN s: ARRAY OF CHAR; n: INTEGER);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(s); Strings.IntToString(n, d); Console.WriteStr(d); Console.WriteLn
	END Say;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR a, b, target: ActorCore.Ref; o: Order; info: Info; i: INTEGER;
	BEGIN
		stream := ActorEvent.New();
		a := SpawnSub(); b := SpawnSub();
		ActorEvent.Subscribe(stream, a, NIL);
		NEW(onlyOrders); ActorEvent.Subscribe(stream, b, onlyOrders);
		FOR i := 1 TO 2 DO NEW(o); o.n := i; ActorEvent.Publish(stream, o) END;
		NEW(info); ActorEvent.Publish(stream, info);
		Wait(5);
		Say("all letters: ", counts[0]);
		Say("orders only: ", counts[1]);
		ActorEvent.SetDefault(stream);
		target := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSilent), "");
		ActorCore.StopActor(target);
		WHILE ActorCore.dir.Lookup(target.node, target.name) = target DO Tasks.Sleep(1) END;
		NEW(o); o.n := 9; ActorCore.Tell(target, o);
		WHILE dead < 1 DO Tasks.Sleep(1) END;
		Say("dead letter carries order ", deadOrder);
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counts[0] := 0; counts[1] := 0; served := 0; dead := 0; deadOrder := 0; nextId := 0;
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor11.
