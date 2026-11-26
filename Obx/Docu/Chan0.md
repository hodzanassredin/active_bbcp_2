# Overview by Example: ObxChan0

The example shows a rendezvous — the channel `AsyncChan.Chan`, which holds no messages. The sender passes five numbers, the receiver adds them up. A rendezvous is needed when the sender must not run ahead of the receiver: each message passes from hand to hand when both tasks are ready.

Source of the module `ObxChan0` (`Obx/Mod64/Chan0.cp`):

```
MODULE ObxChan0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Sender = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan END;
		Receiver = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Body;

	PROCEDURE (r: Receiver) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan0: sum="); Console.WriteStr(d); Console.WriteLn
	END Do;

END ObxChan0.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxChan0.Do
```

```sh
echo 'ObxChan0.Do' | tools/run64.sh
```

The log shows:

```
ObxChan0: sum=15
```

## What happened

1. `Do` creates the channel and two tasks and starts them with `Tasks.Start`.
2. The sender reaches `s.c.Send(m)` and waits: the channel has no receiver yet.
3. The receiver calls `r.c.Recv(m)`, takes the message of the waiting sender and wakes it.
4. This repeats five times; then the sender sends `NIL`, and the receiver leaves its loop and sets `done`.
5. `Do` runs on the main stack and cannot wait, so it spins `Tasks.Step` until `done` becomes true.

More in `Docu/Async.md`.

In this example we have seen how two tasks pass values over a rendezvous, how a stream of messages is ended with the agreed message `NIL`, and why a command on the main stack spins the scheduler instead of waiting itself.
