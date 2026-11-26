# Overview by Example: ObxChan2

The example shows the mailbox `AsyncChan.Mailbox`. Sending into a mailbox never waits, so one may send into it from the main stack — from a command, an action handler or a message broadcast, where waiting is not allowed.

Source of the module `ObxChan2` (`Obx/Mod64/Chan2.cp`):

```
MODULE ObxChan2;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Event = POINTER TO RECORD (AsyncSelect.Msg) code: INTEGER END;
		Handler = POINTER TO RECORD (Tasks.Job) n: INTEGER END;

	VAR box: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Post (code: INTEGER);
	(* Вызывается с главного стека: ящик не заставляет ждать. *)
		VAR e: Event;
	BEGIN
		NEW(e); e.code := code; box.Send(e)
	END Post;

	PROCEDURE (h: Handler) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			box.Recv(m);
			WITH m: Event DO
				IF m.code = 0 THEN EXIT END;
				INC(h.n)
			END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR h: Handler; s: ARRAY 16 OF CHAR;
	BEGIN
		box := AsyncChan.NewMailbox();
		Post(1); Post(2); Post(3); Post(0);
		NEW(h); done := FALSE; Tasks.Start(h);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(h.n, s);
		Console.WriteStr("ObxChan2: events="); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan2.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxChan2.Do
```

```sh
echo 'ObxChan2.Do' | tools/run64.sh
```

The log shows:

```
ObxChan2: events=3
```

## What happened

1. `Do` creates the mailbox and calls `Post` four times from the main stack. The mailbox accepts all messages at once.
2. Then `Do` starts the handler task.
3. The handler takes the events out one by one in sending order and counts them until it meets code 0.
4. Three events with a non-zero code give `events=3`.

More in `Docu/Async.md`.

In this example we have seen how the main stack hands events to a task through a mailbox without waiting even once.
