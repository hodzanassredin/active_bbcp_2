# Overview by Example: ObxEvent0

The example shows a swap channel: two tasks exchange values in one synchronization. The idea is from Concurrent ML (Reppy). The channel carries a pair "value and reply channel". A synchronization chooses between accepting another offer — reply into that channel and take the value — and sending its own offer and waiting for the reply. A fresh reply channel is created for every synchronization, so the event is built by `Guard`.

Source of the module `ObxEvent0` (`Obx/Mod64/Event0.cp`):

```
MODULE ObxEvent0;
(** Канал обмена: два процесса отдают друг другу значения одним событием.

	Идея из Concurrent ML (Reppy): канал передаёт пару «значение и канал
	ответа». Синхронизация — выбор из двух ветвей: принять чужое
	предложение и ответить в чужой канал либо послать своё и дождаться
	ответа. Канал ответа создаётся заново на каждую синхронизацию, поэтому
	событие строит Guard. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		SwapMsg = POINTER TO RECORD (AsyncSelect.Msg)
			n: INTEGER;
			reply: AsyncChan.Chan
		END;

		TakeAction = POINTER TO RECORD (AsyncEvent.Action)
			n: INTEGER
		END;

		AwaitAction = POINTER TO RECORD (AsyncEvent.Action)
			reply: AsyncChan.Chan
		END;

		SwapBuilder = POINTER TO RECORD (AsyncEvent.Builder)
			c: AsyncChan.Chan; n: INTEGER
		END;

		Swapper = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; n, got: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Результаты последнего обмена: кто что получил. *)
		First*, Second*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: TakeAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: NumMsg;
	BEGIN
		WITH m: SwapMsg DO
			m.reply.Send(Num(a.n));	(* ответ в канал чужого предложения *)
			NEW(r); r.n := m.n; y := r
		ELSE y := NIL
		END
	END Do;

	PROCEDURE (a: AwaitAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: AsyncSelect.Msg;
	BEGIN
		a.reply.Recv(r); y := r
	END Do;

	PROCEDURE (b: SwapBuilder) Make (OUT e: AsyncEvent.Event);
		VAR reply: AsyncChan.Chan; offer: SwapMsg;
			take: TakeAction; await: AwaitAction;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		reply := AsyncChan.NewChan();
		NEW(offer); offer.n := b.n; offer.reply := reply;
		NEW(take); take.n := b.n;
		NEW(await); await.reply := reply;
		evs[0] := AsyncEvent.Wrap(AsyncEvent.Recv(b.c), take);
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Send(b.c, offer), await);
		e := AsyncEvent.Choose(evs)
	END Make;

	PROCEDURE Swap (c: AsyncChan.Chan; n: INTEGER): INTEGER;
		VAR b: SwapBuilder; m: AsyncSelect.Msg;
	BEGIN
		NEW(b); b.c := c; b.n := n;
		m := AsyncEvent.Sync(AsyncEvent.Guard(b));
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END Swap;

	PROCEDURE (s: Swapper) Run;
	BEGIN
		s.got := Swap(s.c, s.n); s.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; a, b: Swapper;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(a); a.c := c; a.n := 1; a.done := FALSE;
		NEW(b); b.c := c; b.n := 2; b.done := FALSE;
		Tasks.Start(a); Tasks.Start(b);
		WHILE ~(a.done & b.done) DO Services.actionHook.Step END;
		First := a.got; Second := b.got;
		Console.WriteStr("ObxEvent0: a=");
		IF First = 2 THEN Console.WriteStr("2") ELSE Console.WriteStr("?") END;
		Console.WriteStr(" b=");
		IF Second = 1 THEN Console.WriteStr("1") ELSE Console.WriteStr("?") END;
		Console.WriteLn
	END Do;

END ObxEvent0.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxEvent0.Do
```

```sh
echo 'ObxEvent0.Do' | tools/run64.sh
```

The log shows:

```
ObxEvent0: a=2 b=1
```

## What happened

1. Two swapper tasks start. Each builds a `Guard` event: the guard creates a reply channel and two branches — `Recv` on the shared channel with an action that replies, and `Send` of its own offer with an action that waits for the reply.
2. The first task finds no partner: its receive offer and its send offer stay in the channel queues, and the task sleeps.
3. The second task builds the same event and its receive can take the first task's waiting send. The receive branch wins; its action sends the second value into the first task's reply channel and returns the first value.
4. The first task wakes on its send branch: the send was performed by the second task. Its action receives from its own reply channel and gets the second value.
5. Both synchronizations return: the first task got 2, the second got 1.

In this example we have seen how `Choose`, `Wrap` and `Guard` compose into a reusable operation, and that an action after the choice may block: the rendezvous send inside the action is a switch point, and the synchronization ends only after the action returns.

More in [the Async guide](../../Docu/Async.md), section 5.1.
