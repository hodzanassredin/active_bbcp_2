# Overview by Example: ObxActor9

Two group strategies: a failure restarts all children or only the failed one with the children
started after it.

A parent starts three workers. With `ActorSup.NewAllForOne` a trap in one worker restarts all
three; with `ActorSup.NewRestForOne` it restarts the failed worker and the one born after it, and
the older worker keeps its state. The example belongs to the guide `Actor/Docu/Actors.md`
(section 6).

```
MODULE ObxActor9;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (Tasks.Job) END;

	VAR
		workers: ARRAY 3 OF ActorCore.Ref;
		nWorkers, nRestarts: INTEGER;
		boom: Boom;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO INC(nWorkers)
		| m: ActorCore.Restarting DO INC(nRestarts)
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); RETURN a
	END NewWorker;

	PROCEDURE (a: Parent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR i: INTEGER; name: ARRAY 4 OF CHAR;
	BEGIN
		IF m IS ActorCore.Started THEN
			FOR i := 0 TO 2 DO
				name[0] := "w"; name[1] := CHR(ORD("0") + i); name[2] := 0X;
				workers[i] := ActorCore.Spawn(c, ActorCore.NewProps(NewWorker), name)
			END
		END
	END Receive;

	PROCEDURE NewParent (): ActorCore.Actor;
		VAR a: Parent;
	BEGIN
		NEW(a); RETURN a
	END NewParent;

	PROCEDURE Say (IN s: ARRAY OF CHAR; n: INTEGER);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(n, d); Console.WriteStr(s); Console.WriteStr(d); Console.WriteLn
	END Say;

	PROCEDURE Begin (strategy: ActorCore.Strategy;
		IN name: ARRAY OF CHAR);
	(* Пост: родитель с тремя работниками; nWorkers — сколько стартовало. *)
		VAR p: ActorCore.Props;
	BEGIN
		nWorkers := 0; nRestarts := 0;
		p := ActorCore.NewProps(NewParent); p.strategy := strategy;
		IF ActorCore.Spawn(NIL, p, name) = NIL THEN END;
		WHILE nWorkers < 3 DO Tasks.Sleep(1) END
	END Begin;

	PROCEDURE Wait (n: INTEGER);
	BEGIN
		WHILE nWorkers < n DO Tasks.Sleep(1) END
	END Wait;

	PROCEDURE (t: Client) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Begin(ActorSup.NewAllForOne(ActorCore.Restart, 5, 10000), "all");
		ActorCore.Tell(workers[1], boom);
		Wait(6);
		Say("all-for-one: restarted ", nRestarts);
		Begin(ActorSup.NewRestForOne(ActorCore.Restart, 5, 10000), "rest");
		ActorCore.Tell(workers[1], boom);
		Wait(5); Tasks.Sleep(50);
		Say("rest-for-one: restarted ", nRestarts);
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		NEW(boom); NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor9.
```

Run it in a component:

```
   ObxActor9.Do
```

or in the console:

```
echo 'ObxActor9.Do' | tools/run64.sh
```

The log shows:

```
all-for-one: restarted 3
rest-for-one: restarted 2
```

## What happened

`Begin` spawns a parent with three workers and waits until all three report `Started`. The client
then sends `Boom` to the middle worker. The first parent has strategy `NewAllForOne(Restart, 5,
10000)`: its strategy answers the failure with `RestartAll`, and `ActorCore` sends `Restart` to
every child. Each old worker reports `Restarting` to its handler, and a new instance reports
`Started`; the counter shows three restarts.

The second parent uses `NewRestForOne`. The list of children keeps the newest first, so the middle
worker and the worker started after it form the affected group. `ActorCore` sends `Restart` to
those two only. The older worker stays alive, so the counter shows two restarts while the third
child continues its work.

## In this example we saw

A supervisor with dependent children chooses the group strategy: `AllForOne` restarts all of them,
`RestForOne` restarts the failed child and those started after it, and the failure window counts
the failures of the whole group.
