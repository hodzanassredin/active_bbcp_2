MODULE ObxPipeCtx;
(** Конвейер из трёх стадий на каналах с пропуском.

	Источник шлёт числа во встречу a, обработчик берёт их из a и шлёт
	удвоенными во встречу b, приёмник считает. Все три — задачи с
	пропуском; каждая операция SendCtx/RecvCtx возвращает FALSE, когда
	пропуск остановлен, и стадия сворачивается, не трапуя.

	Родитель запускает стадии через Spawn и ставит срок SetDeadline.
	Срок кончается раньше, чем поток чисел: JoinAll отменяет группу, и все
	стадии выходят по остановке. Output — счётчики и признаки. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, Console, Strings;

	CONST
		total = 1000000;	(* заведомо больше, чем успеет пройти *)
		deadlineMs = 30;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Source = POINTER TO RECORD (Tasks.Job)
			out: AsyncChan.Chan; sent: INTEGER; stopped: BOOLEAN
		END;

		Worker = POINTER TO RECORD (Tasks.Job)
			in, out: AsyncChan.Chan; done: INTEGER; stopped: BOOLEAN
		END;

		Sink = POINTER TO RECORD (Tasks.Job)
			in: AsyncChan.Chan; got: INTEGER; stopped: BOOLEAN
		END;

		Parent = POINTER TO RECORD (Tasks.Job)
			a, b: AsyncChan.Chan; src: Source; wrk: Worker; snk: Sink;
			expired: BOOLEAN
		END;

	VAR
		sent, done, got: INTEGER;
		stoppedSrc, stoppedWrk, stoppedSnk, parentExpired: BOOLEAN;

	PROCEDURE NewNum (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewNum;

	PROCEDURE GetNum (m: AsyncSelect.Msg): INTEGER;
	BEGIN
		IF m = NIL THEN RETURN -1 END;
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END GetNum;

	PROCEDURE SayInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END SayInt;

	PROCEDURE SayBool (b: BOOLEAN);
	BEGIN
		IF b THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END
	END SayBool;

	PROCEDURE (s: Source) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		LOOP
			IF (i >= total) OR ctx.Stopped() THEN EXIT END;
			INC(i);
			IF ~s.out.SendCtx(ctx, NewNum(i)) THEN EXIT END;
			s.sent := i
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (w: Worker) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			IF ~w.in.RecvCtx(ctx, m) THEN EXIT END;
			n := GetNum(m);
			IF n < 0 THEN EXIT END;
			IF ~w.out.SendCtx(ctx, NewNum(2 * n)) THEN EXIT END;
			INC(w.done)
		END;
		w.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (s: Sink) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			IF ~s.in.RecvCtx(ctx, m) THEN EXIT END;
			IF GetNum(m) < 0 THEN EXIT END;
			INC(s.got)
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (p: Parent) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, p.src);
		Tasks.Spawn(ctx, p.wrk);
		Tasks.Spawn(ctx, p.snk);
		Tasks.JoinAll(ctx);	(* срок кончился — группа отменена и доведена *)
		p.expired := ctx.expired
	END Body;

	PROCEDURE Do*;
		VAR p: Parent;
	BEGIN
		NEW(p);
		p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan();
		NEW(p.src); p.src.out := p.a;
		NEW(p.wrk); p.wrk.in := p.a; p.wrk.out := p.b;
		NEW(p.snk); p.snk.in := p.b;
		Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		sent := p.src.sent; done := p.wrk.done; got := p.snk.got;
		stoppedSrc := p.src.stopped; stoppedWrk := p.wrk.stopped;
		stoppedSnk := p.snk.stopped; parentExpired := p.expired;

		Console.WriteStr("pipeline sent="); SayInt(sent);
		Console.WriteStr(" processed="); SayInt(done);
		Console.WriteStr(" received="); SayInt(got); Console.WriteLn;
		Console.WriteStr("pipeline moved: "); SayBool(sent > 0); Console.WriteLn;
		Console.WriteStr("pipeline balanced: "); SayBool((done = got) OR (done = got + 1)); Console.WriteLn;
		Console.WriteStr("pipeline stopped: "); SayBool(stoppedSrc); Console.WriteStr(" ");
		SayBool(stoppedWrk); Console.WriteStr(" "); SayBool(stoppedSnk); Console.WriteLn;
		Console.WriteStr("pipeline deadline: "); SayBool(parentExpired); Console.WriteLn
	END Do;

END ObxPipeCtx.
