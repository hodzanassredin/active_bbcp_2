# Overview by Example: ObxEvent4

The example shows `Guard` used for a deadline, as in Concurrent ML (Reppy). A `Timeout` event remembers milliseconds at construction time. When one deadline event is reused, the time left until an absolute deadline must be computed anew. `Guard` calls `Builder.Make` on every synchronization; here `Make` returns a `Timeout` with the time left.

Source of the module `ObxEvent4` (`Obx/Mod64/Event4.cp`):

```
MODULE ObxEvent4;
(** Срок, построенный при синхронизации: Guard для тайм-аута.

	Событие Timeout запоминает миллисекунды в момент построения. Если
	событие срока переиспользуется, остаток до абсолютного срока надо
	считать заново. Guard зовёт Builder.Make при каждой синхронизации;
	здесь Make возвращает Timeout с остатком до общего срока. *)

	IMPORT Kernel, Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console,
		Strings;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		TagAction = POINTER TO RECORD (AsyncEvent.Action) n: INTEGER END;

		DeadlineBuilder = POINTER TO RECORD (AsyncEvent.Builder)
			due: LONGINT; builds: INTEGER
		END;

		Server = POINTER TO RECORD (Tasks.Job)
			delay: LONGINT; out: AsyncChan.Mailbox
		END;

		Client = POINTER TO RECORD (Tasks.Job) deadline: AsyncEvent.Event END;

	VAR
		(** Итог последнего прогона. *)
		Fast*, Slow*: BOOLEAN; Builds*: INTEGER;

	PROCEDURE (a: TagAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: NumMsg;
	BEGIN
		NEW(r); r.n := a.n; y := r
	END Do;

	PROCEDURE (b: DeadlineBuilder) Make (OUT e: AsyncEvent.Event);
		VAR ms: LONGINT;
	BEGIN
		INC(b.builds);
		ms := b.due - Kernel.Time();
		e := AsyncEvent.Timeout(ms)
	END Make;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR m: NumMsg;
	BEGIN
		Tasks.Sleep(s.delay); NEW(m); m.n := 0; s.out.Send(m)
	END Body;

	PROCEDURE Ask (delay: LONGINT; deadline: AsyncEvent.Event): BOOLEAN;
		VAR s: Server; ta: TagAction; evs: ARRAY 2 OF AsyncEvent.Event;
			m: AsyncSelect.Msg;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox();
		Tasks.Start(s);
		NEW(ta); ta.n := 1;
		evs[0] := AsyncEvent.Recv(s.out);
		evs[1] := AsyncEvent.Wrap(deadline, ta);
		m := AsyncEvent.Sync(AsyncEvent.Choose(evs));
		WITH m: NumMsg DO RETURN m.n = 0 ELSE RETURN FALSE END
	END Ask;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Fast := Ask(10, c.deadline);
		Slow := Ask(200, c.deadline)
	END Body;

	PROCEDURE Do*;
		VAR b: DeadlineBuilder; d: AsyncEvent.Event; c: Client; buf: ARRAY 16 OF CHAR;
	BEGIN
		NEW(b); b.due := Kernel.Time() + 50; b.builds := 0;
		d := AsyncEvent.Guard(b);	(* событие срока — одно на оба запроса *)
		NEW(c); c.deadline := d;
		Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END;
		Builds := b.builds;
		Console.WriteStr("ObxEvent4: fast=");
		IF Fast THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" slow=");
		IF Slow THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" builds=");
		Strings.IntToString(Builds, buf); Console.WriteStr(buf); Console.WriteLn
	END Do;

END ObxEvent4.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxEvent4.Do
```

```sh
echo 'ObxEvent4.Do' | tools/run64.sh
```

The log shows:

```
ObxEvent4: fast=reply slow=timeout builds=2
```

## What happened

1. The command computes an absolute deadline 50 ms ahead and builds one guarded deadline event from it.
2. The client task calls `Ask` twice. Each call starts a server that replies after its delay into a mailbox, and synchronizes on a choice: receive the reply, or the deadline event with an action that marks a timeout.
3. The first call asks for a reply after 10 ms. The guarded deadline is built at this synchronization with about 50 ms left; the reply wins.
4. The second call starts about 10 ms later and asks for a reply after 200 ms. The same deadline event is synchronized again, and `Make` computes about 40 ms left. The timeout wins before the server replies.
5. The late server still puts its reply into the mailbox and finishes; the command waits for all tasks, so the log has no cancellation of a sleeping task.
6. The builder ran twice, once per synchronization, and the log shows `builds=2`.

In this example we have seen a guard evaluate its builder once per synchronization, an event value reused with a fresh timeout each time, and a deadline that bounds a wait.

More in [the Async guide](../../Docu/Async.md), section 5.1.
