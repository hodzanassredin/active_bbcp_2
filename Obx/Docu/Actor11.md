# Overview by Example: ObxActor11

A stream of events: two subscribers, a filter, and a dead letter published to the stream.

The stream carries orders and information. One subscriber takes every letter, the other only
orders. Then a letter to a stopped actor becomes a `DeadLetter` event, and the subscriber sees the
original order inside it. The example belongs to the guide `Actor/Docu/Actors.md` (section 4).

```
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
```

Run it in a component:

```
   ObxActor11.Do
```

or in the console:

```
echo 'ObxActor11.Do' | tools/run64.sh
```

The log shows:

```
all letters: 3
orders only: 2
dead letter carries order 9
```

## What happened

`ActorEvent.New` creates a stream actor. Both subscribers are subscribed to it: the first without a
predicate, the second with `OrdersOnly`. Two orders and one information letter are published: the
first subscriber receives all three, the second only the two orders, because `Match` returns `TRUE`
only for `Order`.

Then `SetDefault(stream)` makes the stream the receiver of dead letters. A silent actor is created
and stopped, and the order numbered 9 is sent to its address. It cannot be delivered, so
`ActorCore` counts it as a dead letter and calls the hook; the hook publishes a `DeadLetter` to the
stream. The first subscriber receives it, and the handler reads the original order number from
`m.m`. The second subscriber does not receive it: a `DeadLetter` is not an `Order`.

## In this example we saw

A stream separates the publisher from the list of subscribers, a filter decides which letters a
subscriber gets, and a dead letter becomes an event of the same stream with the original letter
inside.
