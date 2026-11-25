# Overview by Example: ObxSelect0

The example shows a selection with a timeout. The client waits for the server reply no longer than 50 ms: `AsyncChan.Select` waits at once for a receive from the reply mailbox and for the deadline, and reports which happened first.

Source of the module `ObxSelect0` (`Obx/Mod64/Select0.cp`):

```
MODULE ObxSelect0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Services, AsyncIO, Console;

	TYPE
		Reply = POINTER TO RECORD (AsyncSelect.Msg) END;
		Server = POINTER TO RECORD (Tasks.Task) delay: INTEGER; out: AsyncChan.Mailbox END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR fast, slow: ARRAY 16 OF CHAR; done: BOOLEAN;

	PROCEDURE (s: Server) Run;
		VAR r: Reply;
	BEGIN
		Tasks.Sleep(s.delay); NEW(r); s.out.Send(r)
	END Run;

	PROCEDURE Ask (delay: INTEGER; OUT res: ARRAY OF CHAR);
		VAR s: Server; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox(); Tasks.Start(s);
		AsyncChan.RecvAlt(s.out, ops[0]);
		AsyncChan.TimeoutAlt(50, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN res := "reply" ELSE res := "timeout" END
	END Ask;

	PROCEDURE (c: Client) Run;
	BEGIN
		Ask(10, fast); Ask(200, slow); done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: Client;
	BEGIN
		NEW(c); done := FALSE; Tasks.Start(c);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxSelect0: fast="); Console.WriteStr(fast);
		Console.WriteStr(" slow="); Console.WriteStr(slow); Console.WriteLn
	END Do;

END ObxSelect0.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxSelect0.Do
```

```sh
echo 'ObxSelect0.Do' | tools/run64.sh
```

The log shows:

```
ObxSelect0: fast=reply slow=timeout
Tasks: cancelled
```

The second line is printed by the cancellation of the slow server: when the command ends it is still
asleep, and at program exit `Tasks` cancels the remaining tasks (trap 127 in `Sleep`).

## What happened

1. The client task calls `Ask` twice: with a server that replies after 10 ms and with a server that replies after 200 ms.
2. `Ask` starts the server and builds two alternatives: `RecvAlt` on the reply mailbox and `TimeoutAlt(50)`.
3. `Select` returns 0 if the reply came first and 1 if the deadline expired first.
4. The late server still puts its reply into the mailbox and finishes; had it replied into a rendezvous, it would have stayed waiting forever.
5. `Do` spins `Services.actionHook.Step` and `AsyncIO.Poll(1)`, like BlackBox's main loop: the timeout of a selection is a `Services` action, and `Tasks.Step` alone is not enough for it.

More in `Docu/Async.md`.

In this example we have seen how to wait for several events at once, how to bound a wait with a deadline, and why a reply that may be late is sent into a mailbox.
