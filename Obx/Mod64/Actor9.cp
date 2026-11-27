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
