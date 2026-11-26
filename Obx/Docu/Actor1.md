# Overview by Example: ObxActor1

Supervision: a trap in a handler, the `Restart` directive and the state lost on restart.

A worker actor counts letters and traps on a `Boom` letter. Its parent is a supervisor with the
`OneForOne` strategy and the `Restart` directive. The example shows that a restart creates an
instance with the initial state. The example belongs to the guide `Actor/Docu/Actors.md`
(section 6).

```
MODULE ObxActor1;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (Tasks.Job) END;

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

	PROCEDURE (t: Client) Body- (VAR ctx: Tasks.Ctx);
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
	END Body;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "parent") # NIL, 100);
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor1.
```

`tools/build64.sh` builds the module (the source is `Obx/Mod64/Actor1.cp`; it is for amd64 only).
In a BlackBox window a commander executes the command:

```
   ObxActor1.Do
```

In the console:

```sh
echo 'ObxActor1.Do' | tools/run64.sh
```

The log shows:

```
worker: Started 2, count after restart 1
```

## What happened

The command creates a parent named `parent` with the strategy `ActorSup.NewOneForOne(ActorCore.Restart,
3, 1000)`: a failing child is restarted, at most three times a second. On `Started` the parent
creates a child; its full name is `parent/worker`.

The client waits until the child exists and sends it `Inc`, `Inc`, `Boom`, `Inc`. The two `Inc`
letters bring the count to 2. On `Boom` the handler traps 42. The trap does not bring the executor
down: the handler runs under `Kernel.Try`, the child's mailbox is suspended, and the parent gets a
system message about the failure. The strategy answers `Restart`. The old instance gets
`Restarting`, the factory `NewWorker` creates a new one, and it gets `Started` — the second time in
the example. The culprit letter is skipped, the other letters of the mailbox are kept: the last
`Inc` is handled by the new instance, and its count is 1.

After 100 ms the client prints the result and calls `ActorRun.Shutdown`: the parent and the child
stop, the pool tasks end.

In this example we have seen that the parent decides about a failing actor, and that a restart
without a journal loses the instance's state.
