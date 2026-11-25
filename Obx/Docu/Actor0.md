# Overview by Example: ObxActor0

The first actor: `Spawn`, `Tell`, `Ask`, `Respond` and the common shutdown `ActorRun.Shutdown`.

A counter is an actor with one field. A client sends it a thousand `Inc` letters and then asks for
the total with a `Get` letter. Only the counter's handler changes its state, so no monitor is
needed. The example belongs to the guide `Actor/Docu/Actors.md` (sections 2 and 3).

```
MODULE ObxActor0;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Num = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR counter: ActorCore.Ref;

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: Num;
	BEGIN
		WITH m: Inc DO INC(a.n)
		| m: Get DO NEW(r); r.n := a.n; c.Respond(r)
		ELSE	(* Started и прочие сообщения жизненного цикла *)
		END
	END Receive;

	PROCEDURE New (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END New;

	PROCEDURE (t: Client) Run;
		VAR i: INTEGER; inc: Inc; get: Get; r: ActorCore.Msg; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(inc); NEW(get);
		FOR i := 1 TO 1000 DO ActorCore.Tell(counter, inc) END;
		IF ActorCore.Ask(counter, get, 1000, r) = ActorCore.ok THEN
			Strings.IntToString(r(Num).n, s); Console.WriteStr("counter: "); Console.WriteStr(s)
		ELSE Console.WriteStr("counter: no answer")
		END;
		Console.WriteLn;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(New), "counter");
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor0.
```

`tools/build64.sh` builds the module (the source is `Obx/Mod64/Actor0.cp`; it is for amd64 only).
In a BlackBox window a commander executes the command:

```
   ObxActor0.Do
```

In the console:

```sh
echo 'ObxActor0.Do' | tools/run64.sh
```

The log shows:

```
counter: 1000
```

## What happened

The command starts a pool of four executors (`ActorRun.Start`) unless one runs already, and creates
the actor: `Spawn` calls the factory `New`, makes a mailbox named `counter` and returns the address.
The handler gets `Started` as the first letter; the counter skips it in the `ELSE` branch.

The client is a task, because `Ask` waits for an answer, and waiting is allowed only in a task.
`Tell` puts a letter into the mailbox and returns at once; the thousand `Inc` letters are in the
mailbox before an executor takes the first of them. The client sends the same object `inc` a
thousand times: a message does not change after it is sent, so this is allowed.

`Ask` sends `Get` with a temporary reply address and waits for the first letter to that address for
at most 1000 ms. Letters of one sender to one receiver arrive in order, so `Get` is handled after
all `Inc` letters. The handler answers with `c.Respond`: the answer goes to the sender of the current
letter, that is, to the client's reply address. The client prints `counter: 1000`.

At the end the client calls `ActorRun.Shutdown`. It sends `Stop` to every actor without a parent,
waits until all actors have stopped, and ends the pool tasks without cancelling them. The command
runs the main loop while there are tasks (`Tasks.Count() > 0`) and returns. Without `Shutdown` the
executors would stay in `Tasks.Park`, the command loop would never end, and the scheduler would print
`Tasks: deadlock`.

In this example we have seen how to create an actor, send it letters, ask it, and stop everything
with one call.
