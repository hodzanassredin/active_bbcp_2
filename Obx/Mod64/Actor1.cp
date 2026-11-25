MODULE ObxActor1;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR worker: ActorCore.Ref; starts, last: INTEGER;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO INC(starts)
		| m: Inc DO INC(a.n); last := a.n
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
	BEGIN
		IF m IS ActorCore.Started THEN
			worker := ActorCore.Spawn(c, ActorCore.NewProps(NewWorker), "worker")
		END
	END Receive;

	PROCEDURE NewParent (): ActorCore.Actor;
		VAR a: Parent;
	BEGIN
		NEW(a); RETURN a
	END NewParent;

	PROCEDURE (t: Client) Run;
		VAR inc: Inc; boom: Boom; s: ARRAY 16 OF CHAR;
	BEGIN
		WHILE worker = NIL DO Tasks.Sleep(1) END;
		NEW(inc); NEW(boom);
		ActorCore.Tell(worker, inc); ActorCore.Tell(worker, inc);
		ActorCore.Tell(worker, boom);
		ActorCore.Tell(worker, inc);
		Tasks.Sleep(100);
		Strings.IntToString(starts, s); Console.WriteStr("worker: Started "); Console.WriteStr(s);
		Strings.IntToString(last, s); Console.WriteStr(", count after restart "); Console.WriteStr(s);
		Console.WriteLn;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		IF ActorCore.Spawn(NIL, p, "parent") = NIL THEN END;
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor1.
