MODULE ObxEvent1;
(** Буферизованный канал из встреч: сервер с выбором и действиями.

	Идея из Concurrent ML (Reppy): буфер — это процесс, который держит
	очередь и ждёт двух видов встреч, «положить» и «взять». Выбор решает,
	какую встречу обслужить; действие Wrap после выбора кладёт значение в
	очередь и подтверждает приём либо выдаёт первое значение. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console, Strings;

	CONST
		capacity = 64;	(* очередь сервера *)
		total = 5;		(* сколько значений проходит через буфер *)

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		PutReq = POINTER TO RECORD (AsyncSelect.Msg)
			n: INTEGER;
			ack: AsyncChan.Chan
		END;

		GetReq = POINTER TO RECORD (AsyncSelect.Msg)
			reply: AsyncChan.Chan
		END;

		Queue = RECORD
			items: ARRAY capacity OF INTEGER;
			head, tail, count: INTEGER
		END;

		Table = POINTER TO RECORD
			q: Queue;
			putCh, getCh: AsyncChan.Chan;
			stopCh: AsyncChan.Mailbox
		END;

		ServerPut = POINTER TO RECORD (AsyncEvent.Action) t: Table END;
		ServerGet = POINTER TO RECORD (AsyncEvent.Action) t: Table END;
		ServerStop = POINTER TO RECORD (AsyncEvent.Action) s: Server END;

		PutAck = POINTER TO RECORD (AsyncEvent.Action) ack: AsyncChan.Chan END;
		GetReply = POINTER TO RECORD (AsyncEvent.Action) reply: AsyncChan.Chan END;

		Server = POINTER TO RECORD (Tasks.Task)
			t: Table; running, done: BOOLEAN
		END;

		Producer = POINTER TO RECORD (Tasks.Task) t: Table; done: BOOLEAN END;
		Consumer = POINTER TO RECORD (Tasks.Task)
			t: Table; got: ARRAY total OF INTEGER; sum: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Что получил потребитель и сумма. *)
		GotN*: ARRAY total OF INTEGER; GotSum*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE Enqueue (VAR q: Queue; n: INTEGER);
	BEGIN
		q.items[q.tail] := n;
		q.tail := (q.tail + 1) MOD capacity;
		INC(q.count)
	END Enqueue;

	PROCEDURE Dequeue (VAR q: Queue): INTEGER;
		VAR n: INTEGER;
	BEGIN
		n := q.items[q.head];
		q.head := (q.head + 1) MOD capacity;
		DEC(q.count);
		RETURN n
	END Dequeue;

	(* --- действия сервера -------------------------------------------------- *)

	PROCEDURE (a: ServerPut) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		WITH m: PutReq DO
			Enqueue(a.t.q, m.n);
			m.ack.Send(Num(m.n))
		END;
		y := NIL
	END Do;

	PROCEDURE (a: ServerGet) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		WITH m: GetReq DO m.reply.Send(Num(Dequeue(a.t.q))) END;
		y := NIL
	END Do;

	PROCEDURE (a: ServerStop) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
	BEGIN
		a.s.running := FALSE; y := NIL
	END Do;

	(* --- действия клиентов ------------------------------------------------- *)

	PROCEDURE (a: PutAck) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: AsyncSelect.Msg;
	BEGIN
		a.ack.Recv(r); y := NIL
	END Do;

	PROCEDURE (a: GetReply) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: AsyncSelect.Msg;
	BEGIN
		a.reply.Recv(r); y := r
	END Do;

	(* --- сервер ------------------------------------------------------------ *)

	PROCEDURE (s: Server) Run;
		VAR e: AsyncEvent.Event; m: AsyncSelect.Msg; tick: AsyncEvent.Event;
			put: ServerPut; get: ServerGet; stop: ServerStop;
			evs4: ARRAY 4 OF AsyncEvent.Event;
			evs3: ARRAY 3 OF AsyncEvent.Event;
	BEGIN
		NEW(put); put.t := s.t;
		NEW(get); get.t := s.t;
		NEW(stop); stop.s := s;
		tick := AsyncEvent.Timeout(100);	(* сторож: планировщик видит срок *)
		s.running := TRUE;
		WHILE s.running DO
			evs3[0] := AsyncEvent.Wrap(AsyncEvent.Recv(s.t.putCh), put);
			evs3[1] := AsyncEvent.Wrap(AsyncEvent.Recv(s.t.stopCh), stop);
			evs3[2] := tick;
			IF s.t.q.count > 0 THEN
				evs4[0] := evs3[0];
				evs4[1] := AsyncEvent.Wrap(AsyncEvent.Recv(s.t.getCh), get);
				evs4[2] := evs3[1];
				evs4[3] := tick;
				e := AsyncEvent.Choose(evs4)
			ELSE
				e := AsyncEvent.Choose(evs3)
			END;
			m := AsyncEvent.Sync(e)
		END;
		s.done := TRUE
	END Run;

	PROCEDURE Put (t: Table; n: INTEGER);
		VAR req: PutReq; a: PutAck;
	BEGIN
		NEW(req); req.n := n; req.ack := AsyncChan.NewChan();
		NEW(a); a.ack := req.ack;
		IF AsyncEvent.Sync(AsyncEvent.Wrap(AsyncEvent.Send(t.putCh, req), a)) = NIL THEN END
	END Put;

	PROCEDURE Get (t: Table): INTEGER;
		VAR req: GetReq; a: GetReply; m: AsyncSelect.Msg;
	BEGIN
		NEW(req); req.reply := AsyncChan.NewChan();
		NEW(a); a.reply := req.reply;
		m := AsyncEvent.Sync(AsyncEvent.Wrap(AsyncEvent.Send(t.getCh, req), a));
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END Get;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO total DO Put(p.t, i) END;
		p.done := TRUE
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i: INTEGER;
	BEGIN
		c.sum := 0;
		FOR i := 0 TO total - 1 DO
			c.got[i] := Get(c.t); c.sum := c.sum + c.got[i]
		END;
		c.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR t: Table; s: Server; p: Producer; c: Consumer; i: INTEGER;
			m: AsyncSelect.Msg; buf: ARRAY 16 OF CHAR;
	BEGIN
		NEW(t); t.q.head := 0; t.q.tail := 0; t.q.count := 0;
		t.putCh := AsyncChan.NewChan(); t.getCh := AsyncChan.NewChan();
		t.stopCh := AsyncChan.NewMailbox();
		NEW(s); s.t := t; s.done := FALSE;
		NEW(p); p.t := t; p.done := FALSE;
		NEW(c); c.t := t; c.done := FALSE;
		Tasks.Start(s); Tasks.Start(c); Tasks.Start(p);
		WHILE ~(p.done & c.done) DO Services.actionHook.Step END;
		t.stopCh.Send(NIL);	(* ящик: отправка с главного стека допустима *)
		WHILE ~s.done DO Services.actionHook.Step END;
		GotSum := c.sum;
		FOR i := 0 TO total - 1 DO GotN[i] := c.got[i] END;
		Console.WriteStr("ObxEvent1: got");
		FOR i := 0 TO total - 1 DO
			Strings.IntToString(GotN[i], buf);
			Console.WriteStr(" "); Console.WriteStr(buf)
		END;
		Strings.IntToString(GotSum, buf);
		Console.WriteStr(" sum="); Console.WriteStr(buf); Console.WriteLn
	END Do;

END ObxEvent1.
