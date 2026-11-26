# Overview by Example: ObxEvent2

The example shows a call with a deadline and cancellation of the loser, as in Concurrent ML (Reppy) and Hopac. The client wraps the reply receive in `WithNack`; the request carries the notification event to the server. The client chooses between the reply and a deadline. The server chooses between its own work and the notification. If the deadline wins, the client fires the notification and the server cancels the work instead of replying.

Source of the module `ObxEvent2` (`Obx/Mod64/Event2.cp`):

```
MODULE ObxEvent2;
(** Сервер и отмена клиента через WithNack.

	Идея из Concurrent ML (Reppy) и Hopac: клиент ждёт ответ не дольше
	срока. Ожидание ответа обёрнуто в WithNack; если выбор выиграл
	тайм-аут, клиент получает событие-уведомление и зажигает его. Сервер
	ждёт ответ своей работы и это уведомление одним выбором: уведомление
	отменяет работу. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Request = POINTER TO RECORD (AsyncSelect.Msg)
			delay: LONGINT;			(* сколько сервер работает *)
			reply: AsyncChan.Mailbox;
			nack: AsyncEvent.Event	(* уведомление «клиент ушёл» *)
		END;

		Server = POINTER TO RECORD (Tasks.Task)
			ch: AsyncChan.Chan; n: INTEGER;
			replies, cancelled: INTEGER;
			done: BOOLEAN
		END;

		Client = POINTER TO RECORD (Tasks.Job)
			ch: AsyncChan.Chan; fast, slow, done: BOOLEAN
		END;

		ReplyAction = POINTER TO RECORD (AsyncEvent.Action)
			s: Server; req: Request
		END;

		CancelAction = POINTER TO RECORD (AsyncEvent.Action)
			s: Server
		END;

		TimeoutAction = POINTER TO RECORD (AsyncEvent.Action) END;

	VAR
		(** Итог последнего прогона. *)
		Fast*, Slow*: BOOLEAN; Cancelled*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: ReplyAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		a.req.reply.Send(Num(0)); INC(a.s.replies); y := NIL
	END Do;

	PROCEDURE (a: CancelAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		INC(a.s.cancelled); y := NIL
	END Do;

	PROCEDURE (a: TimeoutAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		y := Num(1)
	END Do;

	PROCEDURE (s: Server) Run;
		VAR req: Request; m: AsyncSelect.Msg; ra: ReplyAction; ca: CancelAction;
			evs: ARRAY 2 OF AsyncEvent.Event; i: INTEGER;
	BEGIN
		FOR i := 1 TO s.n DO
			s.ch.Recv(m);
			WITH m: Request DO
				req := m;
				NEW(ra); ra.s := s; ra.req := req;
				NEW(ca); ca.s := s;
				evs[0] := AsyncEvent.Wrap(AsyncEvent.Timeout(req.delay), ra);
				evs[1] := AsyncEvent.Wrap(req.nack, ca);
				IF AsyncEvent.Sync(AsyncEvent.Choose(evs)) = NIL THEN END
			END
		END;
		s.done := TRUE
	END Run;

	PROCEDURE Call (ch: AsyncChan.Chan; delay, deadline: LONGINT): BOOLEAN;
		(* TRUE — ответ пришёл раньше срока; FALSE — срок, nack зажжён. *)
		VAR reply: AsyncChan.Mailbox; nack: AsyncEvent.Event; req: Request;
			ev: AsyncEvent.Event; evs: ARRAY 2 OF AsyncEvent.Event;
			m: AsyncSelect.Msg; ta: TimeoutAction;
	BEGIN
		reply := AsyncChan.NewMailbox();
		ev := AsyncEvent.WithNack(AsyncEvent.Recv(reply), nack);
		NEW(req); req.delay := delay; req.reply := reply; req.nack := nack;
		ch.Send(req);	(* встреча: сервер взял запрос *)
		NEW(ta);
		evs[0] := ev;
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Timeout(deadline), ta);
		m := AsyncEvent.Sync(AsyncEvent.Choose(evs));
		IF m = NIL THEN RETURN FALSE END;
		RETURN m(NumMsg).n = 0
	END Call;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		c.fast := Call(c.ch, 10, 500);
		c.slow := Call(c.ch, 500, 30);
		c.done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR ch: AsyncChan.Chan; s: Server; c: Client; buf: ARRAY 16 OF CHAR;
	BEGIN
		ch := AsyncChan.NewChan();
		NEW(s); s.ch := ch; s.n := 2; s.replies := 0; s.cancelled := 0;
		s.done := FALSE;
		NEW(c); c.ch := ch; c.fast := FALSE; c.slow := FALSE; c.done := FALSE;
		Tasks.Start(s); Tasks.Start(c);
		WHILE ~(c.done & s.done) DO Services.actionHook.Step END;
		Fast := c.fast; Slow := c.slow; Cancelled := s.cancelled;
		Console.WriteStr("ObxEvent2: fast=");
		IF Fast THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" slow=");
		IF Slow THEN Console.WriteStr("reply") ELSE Console.WriteStr("timeout") END;
		Console.WriteStr(" cancelled=");
		Strings.IntToString(Cancelled, buf);
		Console.WriteStr(buf); Console.WriteLn
	END Do;

END ObxEvent2.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxEvent2.Do
```

```sh
echo 'ObxEvent2.Do' | tools/run64.sh
```

The log shows:

```
ObxEvent2: fast=reply slow=timeout cancelled=1
```

## What happened

1. A call creates the reply mailbox, then `WithNack(Recv(reply))`, which yields the receive event and the notification event, and sends a request with both into the request channel.
2. The server takes the request and offers a choice: `Timeout(work)` wrapped by an action that replies, or the request's notification wrapped by an action that counts a cancellation.
3. In the fast call the work takes 10 ms and the client's deadline is 500 ms. The server's timeout wins, the reply lands in the mailbox, and the client's receive wins. The notification is not fired because the receive branch was chosen.
4. In the slow call the work would take 500 ms and the client's deadline is 30 ms. The client's timeout branch wins; the receive branch loses, so its notification fires.
5. The server is waiting on the same notification as an event. The notification wins its choice, the cancellation action runs, and no reply is sent. The log shows `cancelled=1`.
6. The client and the server both finish; the late work is not performed. The example waits until all tasks end, so no cancellation of a sleeping task appears in the log.

In this example we have seen `WithNack` notify the loser's partner, the notification used as an ordinary alternative on the server side, and that one choice can span the client and the server.

More in [the Async guide](../../Docu/Async.md), section 5.1.
