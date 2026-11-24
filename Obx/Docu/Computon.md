# Overview by Example: ObxComputon

The example shows a computon — an object that is executed in a separate BlackBox process with its own heap. This is how Component Pascal code with `NEW` computes on several cores.

Source of the module `ObxComputon` (`Obx/Mod64/Computon.cp`):

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Services, AsyncIO, Console, Strings;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (cl: Client) Run;
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		IF r # NIL THEN
			WITH r: Square DO Strings.IntToString(r.y, d) END
		ELSE
			Strings.IntToString(AsyncComputon.LastError(), d)
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Do;

END ObxComputon.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxComputon.Do
```

```sh
echo 'ObxComputon.Do' | tools/run64.sh
```

The log shows:

```
ObxComputon: 144
```

## What happened

1. The type `Square` extends `AsyncComputon.Computon`, implements `Do` and stores its fields in `Externalize` and `Internalize`.
2. The module's `Do` starts one worker with `AsyncComputon.Start(1)` and a client task.
3. The client serializes the computon with `x = 12` and sends it with `Push(c, 0, 1)` to worker 0 under request number 1.
4. `Pull(0, 1)` waits for the reply; the worker calls `Do` and returns the object with `y = 144`.
5. The reply is read from an OS pipe through `Poller`, so the module's `Do` spins `AsyncIO.Poll(1)` together with the action step.
6. `Stop` shuts the worker down.

More in `Docu/Async.md`.

In this example we have seen how to describe a computon, send it to a worker and get the result.
