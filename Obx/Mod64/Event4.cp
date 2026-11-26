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
