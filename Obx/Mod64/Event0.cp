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
