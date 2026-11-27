# Overview by Example: ObxActor10

A router sends one letter to a route of its choice or to every route: `NewGroup` with round-robin,
broadcast and consistent hashing.

Three workers count their letters. One group router with `ActorRouter.roundRobin` gives each worker
two of six letters. A second router with `ActorRouter.broadcast` sends one letter to all three. A
third router with `ActorRouter.consistentHash` sends four letters with the same key to one route.
The example belongs to the guide `Actor/Docu/Actors.md` (section 4).

```
MODULE ObxActor10;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun, ActorRouter;

	TYPE
		Put = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) id: INTEGER END;
		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		refs: ARRAY 3 OF ActorCore.Ref;
		counts: ARRAY 3 OF INTEGER;
		served, nextId: INTEGER;
		put: Put;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Put DO INC(counts[a.id]); INC(served)
		| m: ActorRouter.Keyed DO INC(counts[a.id]); INC(served)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); a.id := nextId; INC(nextId); RETURN a
	END NewWorker;

	PROCEDURE SpawnWorkers;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 2 DO
			refs[i] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWorker), "")
		END
	END SpawnWorkers;

	PROCEDURE Wait (n: INTEGER);
	BEGIN
		WHILE served < n DO Tasks.Sleep(1) END
	END Wait;

	PROCEDURE Print (IN s: ARRAY OF CHAR; a, b, c: INTEGER);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(s);
		Strings.IntToString(a, d); Console.WriteStr(d);
		Strings.IntToString(b, d); Console.WriteStr(" "); Console.WriteStr(d);
		Strings.IntToString(c, d); Console.WriteStr(" "); Console.WriteStr(d); Console.WriteLn
	END Print;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR r: ActorCore.Ref; i, b0, b1, b2: INTEGER; m: ActorRouter.Keyed;
	BEGIN
		r := ActorRouter.NewGroup(refs, ActorRouter.roundRobin, 1);
		FOR i := 1 TO 6 DO ActorCore.Tell(r, put) END;
		Wait(6);
		Print("round-robin: ", counts[0], counts[1], counts[2]);
		r := ActorRouter.NewGroup(refs, ActorRouter.broadcast, 1);
		ActorCore.Tell(r, put);
		Wait(9);
		Print("after broadcast: ", counts[0] - 2, counts[1] - 2, counts[2] - 2);
		r := ActorRouter.NewGroup(refs, ActorRouter.consistentHash, 1);
		b0 := counts[0]; b1 := counts[1]; b2 := counts[2];
		FOR i := 1 TO 4 DO
			NEW(m); m.key := "order-7"; ActorCore.Tell(r, m)
		END;
		Wait(13);
		IF (counts[0] - b0 = 4) OR (counts[1] - b1 = 4) OR (counts[2] - b2 = 4) THEN
			Console.WriteStr("hash: one route got 4 letters"); Console.WriteLn
		ELSE Console.WriteStr("hash: letters spread"); Console.WriteLn
		END;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		nextId := 0; served := 0;
		SpawnWorkers; NEW(put); NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor10.
```

Run it in a component:

```
   ObxActor10.Do
```

or in the console:

```
echo 'ObxActor10.Do' | tools/run64.sh
```

The log shows:

```
round-robin: 2 2 2
after broadcast: 1 1 1
hash: one route got 4 letters
```

## What happened

`SpawnWorkers` creates three root actors; each counts the letters it receives. The first router is a
group of these three with `roundRobin`: six letters go round the routes in turn, two to each.

The second router broadcasts one letter; every route receives it, so each worker gets one more.

The third router hashes the key of the letter. The four letters carry the same key `order-7`, so
`ActorHash` gives them the same owner and all four reach one worker. A different key would give
another owner; a new route changes the owner only of the keys it wins.

## In this example we saw

A router chooses a route by a rule and forwards the letter with its original sender. Round-robin
shares the load, broadcast reaches every route, and consistent hashing keeps one key on one route.
