# Overview by Example: ObxWork0

The example shows a worker thread. Resolving a host name blocks an OS thread, so the task gives it to the pool `AsyncWork` and waits for the result without stopping the main loop.

Source of the module `ObxWork0` (`Obx/Mod64/Work0.cp`):

```
MODULE ObxWork0;
	IMPORT S := SYSTEM, AsyncWork, Tasks, Services, AsyncIO, Console, Strings;

	TYPE Asker = POINTER TO RECORD (Tasks.Task) END;

	VAR req: AsyncWork.AddrReq; status: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Asker) Run;
	BEGIN
		req.host := "localhost"; req.count := 0; req.status := -1;
		status := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Asker; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(a); done := FALSE; Tasks.Start(a);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxWork0: status="); Strings.IntToString(req.status, s); Console.WriteStr(s);
		Console.WriteLn
	END Do;

END ObxWork0.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxWork0.Do
```

```sh
echo 'ObxWork0.Do' | tools/run64.sh
```

The log shows:

```
ObxWork0: status=0
```

## What happened

1. The task fills the global record `req` and calls `AsyncWork.Execute` with the address of the job `ResolveJob`.
2. On the first call the pool installs the delegate `AsyncWorkLin` and gives the job to a free thread; the task falls asleep in `Tasks.Park`.
3. The task waits for an event from outside (`Tasks.ParkExternal`): the wake-up will come from the worker thread, not from another task. So the scheduler does not take this wait for a deadlock and prints no report.
4. The thread finishes and writes a byte into an OS pipe; `AsyncIO.Poll(1)` in the loop of `Do` sees it, and the pool wakes the task.
5. Status 0 means that the name `localhost` was resolved.
6. The record `req` is global rather than allocated with `NEW`: a worker thread must not touch the heap.

More in `Docu/Async.md`.

In this example we have seen how to move a blocking call into a worker thread, why its argument lies outside the heap, and why the scheduler does not take the wait for a worker thread for a deadlock.
