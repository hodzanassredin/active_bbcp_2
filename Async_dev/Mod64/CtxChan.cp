MODULE Async_devCtxChan;
(** Каналы и выбор с пропуском.

	Проверяется: AsyncChan.SendCtx/RecvCtx (встреча, полный буфер),
	AsyncChan.SelectCtx и AsyncSelect.SelectCtx (два канала и таймер),
	отмена и срок во время ожидания, −1 при остановке, снятие предложений
	(остановленная задача не съедает сообщение), возврат после остановки
	без переключений, что раньше — TimeoutAlt или срок пропуска, и трап 26
	на новых вызовах под монитором-объектом. *)

	IMPORT Kernel, Services, Tasks, AsyncSelect, AsyncChan, UnitAssert;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Holder = POINTER TO RECORD (Tasks.Object) allow: BOOLEAN END;

		RecvJob = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan; deadline: LONGINT;
			rec: BOOLEAN; got: INTEGER;
			cancelled, expired, fast, repeat: BOOLEAN;
			hold: LONGINT; firstDone, done: BOOLEAN;
			s0, s1: LONGINT
		END;

		SendJob = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan; deadline: LONGINT; n: INTEGER;
			sent: BOOLEAN; cancelled, expired, fast, repeat: BOOLEAN;
			hold: LONGINT; firstDone, done: BOOLEAN;
			s0, s1: LONGINT
		END;

		SendBufJob = POINTER TO RECORD (Tasks.Job)
			b: AsyncChan.Buffer; deadline: LONGINT; n: INTEGER;
			sent, expired, fast, repeat: BOOLEAN;
			hold: LONGINT; firstDone, done: BOOLEAN;
			s0, s1: LONGINT
		END;

		SelJob = POINTER TO RECORD (Tasks.Job)
			a, b: AsyncChan.Chan; deadline, timeout: LONGINT;
			idx, got: INTEGER;
			cancelled, expired, fast, repeat: BOOLEAN;
			hold: LONGINT; firstDone, done: BOOLEAN;
			s0, s1: LONGINT
		END;

		PlainRecvJob = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan; got: INTEGER; done: BOOLEAN
		END;

		Sender = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; b: AsyncChan.Buffer; useBuf: BOOLEAN;
			ms: LONGINT; n: INTEGER
		END;

		Locker = POINTER TO RECORD (Tasks.Task)
			a, b: AsyncChan.Chan; sent: BOOLEAN
		END;

		HoldRecvOp = RECORD (Tasks.Op) c: AsyncChan.Chan; rec: BOOLEAN; got: INTEGER END;
		HoldSelOp = RECORD (Tasks.Op) a, b: AsyncChan.Chan; idx: INTEGER END;
		HoldSendOp = RECORD (Tasks.Op) c: AsyncChan.Chan; sent: BOOLEAN END;

		CtxRecvJob = POINTER TO RECORD (Tasks.Job) h: Holder; c: AsyncChan.Chan; op: HoldRecvOp END;
		CtxSelJob = POINTER TO RECORD (Tasks.Job) h: Holder; a, b: AsyncChan.Chan; op: HoldSelOp END;
		CtxSendJob = POINTER TO RECORD (Tasks.Job) h: Holder; c: AsyncChan.Chan; op: HoldSendOp END;

	VAR
		errSeen: INTEGER;

	(* -------------------- вспомогательное -------------------------------- *)

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

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE StepUntil (t: Tasks.Task; st: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.State(t) # st) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.State(t) = st
	END StepUntil;

	PROCEDURE StepWhile (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~flag & (Kernel.Time() - t0 <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END StepWhile;

	PROCEDURE Start (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, 128 * 1024); Tasks.Start(t)
	END Start;

	(* -------------------- задачи с пропуском ------------------------------ *)

	PROCEDURE (j: RecvJob) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		j.rec := j.c.RecvCtx(ctx, m);
		j.got := GetNum(m);
		j.cancelled := ctx.cancelled; j.expired := ctx.expired;
		j.firstDone := TRUE;
		IF ~j.rec & (ctx.cancelled OR ctx.expired) THEN
			j.s0 := Tasks.Switches();
			j.repeat := ~j.c.RecvCtx(ctx, m);
			j.s1 := Tasks.Switches();
			j.fast := j.s1 = j.s0
		END;
		IF (j.hold > 0) & ~ctx.cancelled THEN Tasks.Sleep(j.hold) END;
		j.done := TRUE
	END Body;

	PROCEDURE (j: SendJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		j.sent := j.c.SendCtx(ctx, NewNum(j.n));
		j.cancelled := ctx.cancelled; j.expired := ctx.expired;
		j.firstDone := TRUE;
		IF ~j.sent & (ctx.cancelled OR ctx.expired) THEN
			j.s0 := Tasks.Switches();
			j.repeat := ~j.c.SendCtx(ctx, NewNum(j.n));
			j.s1 := Tasks.Switches();
			j.fast := j.s1 = j.s0
		END;
		IF (j.hold > 0) & ~ctx.cancelled THEN Tasks.Sleep(j.hold) END;
		j.done := TRUE
	END Body;

	PROCEDURE (j: SendBufJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		j.sent := j.b.SendCtx(ctx, NewNum(j.n));
		j.expired := ctx.expired;
		j.firstDone := TRUE;
		IF ~j.sent & ctx.expired THEN
			j.s0 := Tasks.Switches();
			j.repeat := ~j.b.SendCtx(ctx, NewNum(j.n));
			j.s1 := Tasks.Switches();
			j.fast := j.s1 = j.s0
		END;
		IF j.hold > 0 THEN Tasks.Sleep(j.hold) END;
		j.done := TRUE
	END Body;

	PROCEDURE (j: SelJob) Body- (VAR ctx: Tasks.Ctx);
		VAR ops: ARRAY 3 OF AsyncSelect.Op;
	BEGIN
		IF j.deadline > 0 THEN Tasks.SetDeadline(ctx, j.deadline) END;
		AsyncChan.RecvAlt(j.a, ops[0]);
		AsyncChan.RecvAlt(j.b, ops[1]);
		AsyncChan.TimeoutAlt(j.timeout, ops[2]);
		j.idx := AsyncChan.SelectCtx(ctx, ops);
		IF (j.idx = 0) OR (j.idx = 1) THEN j.got := GetNum(ops[j.idx].got) END;
		j.cancelled := ctx.cancelled; j.expired := ctx.expired;
		j.firstDone := TRUE;
		IF j.idx = -1 THEN
			j.s0 := Tasks.Switches();
			j.repeat := AsyncChan.SelectCtx(ctx, ops) = -1;
			j.s1 := Tasks.Switches();
			j.fast := j.s1 = j.s0
		END;
		IF (j.hold > 0) & ~ctx.cancelled THEN Tasks.Sleep(j.hold) END;
		j.done := TRUE
	END Body;

	PROCEDURE (j: PlainRecvJob) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		j.c.Recv(m);
		j.got := GetNum(m);
		j.done := TRUE
	END Body;

	PROCEDURE (s: Sender) Run;
		VAR m: NumMsg;
	BEGIN
		Tasks.Sleep(s.ms);
		m := NewNum(s.n);
		IF s.useBuf THEN s.b.Send(m) ELSE s.c.Send(m) END
	END Run;

	PROCEDURE (l: Locker) Run;
	(* Держит монитор канала a дольше срока получателя и, пока тот ждёт
	   монитор, чтобы снять своё предложение с a, отдаёт сообщение в его ещё
	   не снятое предложение на b. Остановка — сроком: отменённую задачу вход
	   в занятый монитор прерывает трапом 127, как и в Select без пропуска. *)
	BEGIN
		Kernel.Lock(l.a);
		Tasks.Sleep(80);
		l.b.Send(NewNum(7)); l.sent := TRUE;
		Kernel.Unlock(l.a)
	END Run;

	(* -------------------- объект-монитор ---------------------------------- *)

	PROCEDURE (h: Holder) HoldsWhileWaiting (): BOOLEAN;
	BEGIN
		RETURN h.allow
	END HoldsWhileWaiting;

	PROCEDURE (h: Holder) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR m: AsyncSelect.Msg; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		WITH op: HoldRecvOp DO
			op.rec := op.c.RecvCtx(ctx, m);
			op.got := GetNum(m)
		| op: HoldSelOp DO
			AsyncChan.RecvAlt(op.a, ops[0]);
			AsyncChan.RecvAlt(op.b, ops[1]);
			op.idx := AsyncChan.SelectCtx(ctx, ops)
		| op: HoldSendOp DO
			op.sent := op.c.SendCtx(ctx, NewNum(5))
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (j: CtxRecvJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.op.c := j.c;
		j.h.Do(ctx, j.op)
	END Body;

	PROCEDURE (j: CtxSelJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.op.a := j.a; j.op.b := j.b;
		j.h.Do(ctx, j.op)
	END Body;

	PROCEDURE (j: CtxSendJob) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		j.op.c := j.c;
		j.h.Do(ctx, j.op)
	END Body;

	(* -------------------- проверка канала после остановки ----------------- *)

	PROCEDURE ChannelStillWorks (c: AsyncChan.Chan);
		VAR r: PlainRecvJob; s: Sender;
	BEGIN
		NEW(r); r.c := c; Start(r);
		NEW(s); s.c := c; s.ms := 2; s.n := 77; Start(s);
		UnitAssert.True(Pump(2000), "канал работает после остановки");
		UnitAssert.EqualInt(77, r.got, "другая задача получила своё сообщение");
		Tasks.Cancel(r); Tasks.Cancel(s)	(* не оставить зависшую задачу *)
	END ChannelStillWorks;

	(* -------------------- RecvCtx ----------------------------------------- *)

	PROCEDURE TestRecvCtxCancel*;
		VAR j: RecvJob; c: AsyncChan.Chan;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(j); j.c := c; j.hold := 0;
		Start(j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "приём ждёт");
		Tasks.Cancel(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.True(~j.rec, "RecvCtx вернул FALSE");
		UnitAssert.EqualInt(-1, j.got, "сообщение не получено");
		UnitAssert.True(j.cancelled, "причина — отмена");
		UnitAssert.True(~j.expired, "срок не истёк");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		UnitAssert.True(~Tasks.Failed(j), "без трапа");
		ChannelStillWorks(c)
	END TestRecvCtxCancel;

	PROCEDURE TestRecvCtxDeadline*;
		VAR j: RecvJob; c: AsyncChan.Chan;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(j); j.c := c; j.deadline := 10; j.hold := 40;
		Start(j);
		UnitAssert.True(StepWhile(j.firstDone, 2000), "срок вернул приём");
		UnitAssert.True(~j.rec, "RecvCtx вернул FALSE");
		UnitAssert.EqualInt(-1, j.got, "сообщение не получено");
		UnitAssert.True(j.expired, "причина — срок");
		UnitAssert.True(~j.cancelled, "отмены не было");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		(* Остановленная задача ещё жива и спит; её предложение не должно
		   перехватить сообщение другой пары. *)
		ChannelStillWorks(c);
		UnitAssert.True(Pump(1000), "остановленная задача завершилась");
		UnitAssert.True(j.done, "тело дошло до конца")
	END TestRecvCtxDeadline;

	(* -------------------- SendCtx: встреча -------------------------------- *)

	PROCEDURE TestSendCtxCancel*;
		VAR j: SendJob; c: AsyncChan.Chan;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(j); j.c := c; j.n := 42; j.hold := 0;
		Start(j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "отправка ждёт получателя");
		Tasks.Cancel(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.True(~j.sent, "SendCtx вернул FALSE");
		UnitAssert.True(j.cancelled, "причина — отмена");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		UnitAssert.True(~Tasks.Failed(j), "без трапа");
		ChannelStillWorks(c)
	END TestSendCtxCancel;

	PROCEDURE TestSendCtxDeadline*;
		(* Сообщение остановленного отправителя не должно достаться
		   следующему получателю. *)
		VAR j: SendJob; c: AsyncChan.Chan;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(j); j.c := c; j.n := 42; j.deadline := 10; j.hold := 40;
		Start(j);
		UnitAssert.True(StepWhile(j.firstDone, 2000), "срок вернул отправку");
		UnitAssert.True(~j.sent, "SendCtx вернул FALSE");
		UnitAssert.True(j.expired, "причина — срок");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		ChannelStillWorks(c);
		UnitAssert.True(Pump(1000), "остановленная задача завершилась")
	END TestSendCtxDeadline;

	(* -------------------- SendCtx: полный буфер --------------------------- *)

	PROCEDURE TestSendBufDeadline*;
		VAR b: AsyncChan.Buffer; j: SendBufJob; m: AsyncSelect.Msg;
			s: Sender;
	BEGIN
		b := AsyncChan.NewBuffer(1);
		b.Send(NewNum(1));	(* буфер полон; главный стек годится *)
		NEW(j); j.b := b; j.n := 42; j.deadline := 10; j.hold := 40;
		Start(j);
		UnitAssert.True(StepWhile(j.firstDone, 2000), "срок вернул отправку");
		UnitAssert.True(~j.sent, "SendCtx вернул FALSE");
		UnitAssert.True(j.expired, "причина — срок");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		(* Освободить место: если предложение не снято, в буфер попадёт 42. *)
		b.Recv(m);
		UnitAssert.EqualInt(1, GetNum(m), "первым лежал 1");
		NEW(s); s.b := b; s.useBuf := TRUE; s.ms := 1; s.n := 99; Start(s);
		UnitAssert.True(Pump(2000), "отправитель завершился");
		b.Recv(m);
		UnitAssert.EqualInt(99, GetNum(m), "место заняло новое сообщение, а не 42");
		Tasks.Cancel(s);
		UnitAssert.True(j.done, "остановленная задача завершилась")
	END TestSendBufDeadline;

	(* -------------------- SelectCtx --------------------------------------- *)

	PROCEDURE TestSelectCtxWinner*;
		VAR j: SelJob; a, b: AsyncChan.Chan; s: Sender;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 500;
		NEW(s); s.c := b; s.ms := 10; s.n := 7;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задачи завершились");
		UnitAssert.EqualInt(1, j.idx, "победил второй канал");
		UnitAssert.EqualInt(7, j.got, "сообщение получено");
		UnitAssert.True(~j.cancelled & ~j.expired, "остановки не было");
		UnitAssert.True(~Tasks.Failed(j), "без трапа")
	END TestSelectCtxWinner;

	PROCEDURE TestSelectCtxTimeout*;
		VAR j: SelJob; a, b: AsyncChan.Chan;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 5;
		Start(j);
		UnitAssert.True(Pump(2000), "задача завершилась");
		UnitAssert.EqualInt(2, j.idx, "победил тайм-аут");
		UnitAssert.True(~j.cancelled & ~j.expired, "остановки не было")
	END TestSelectCtxTimeout;

	PROCEDURE TestSelectCtxCancel*;
		VAR j: SelJob; a, b: AsyncChan.Chan;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 500;
		Start(j);
		UnitAssert.True(StepUntil(j, Tasks.Parked, 1000), "выбор ждёт");
		Tasks.Cancel(j);
		UnitAssert.True(Pump(1000), "задача завершилась");
		UnitAssert.EqualInt(-1, j.idx, "SelectCtx вернул −1");
		UnitAssert.True(j.cancelled, "причина — отмена");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		ChannelStillWorks(a)
	END TestSelectCtxCancel;

	PROCEDURE TestSelectCtxDecidedWhileWithdrawing*;
		(* Решение, принятое, пока остановленная задача снимает предложения
		   (монитор канала a занят), важнее остановки: сообщение, которое
		   отправитель уже отдал, не теряется. *)
		VAR j: SelJob; l: Locker; a, b: AsyncChan.Chan;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 5000; j.deadline := 20;
		Start(j);
		NEW(l); l.a := a; l.b := b;
		Start(l);
		UnitAssert.True(Pump(2000), "обе задачи завершились");
		UnitAssert.True(l.sent, "отправка состоялась");
		UnitAssert.True(j.expired, "срок получателя наступил");
		UnitAssert.EqualInt(1, j.idx, "победил приём из b, а не остановка");
		UnitAssert.EqualInt(7, j.got, "сообщение получено")
	END TestSelectCtxDecidedWhileWithdrawing;

	PROCEDURE TestSelectCtxDeadlineFirst*;
		(* Срок пропуска 10 мс раньше тайм-аута 500 мс: −1. *)
		VAR j: SelJob; a, b: AsyncChan.Chan;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 500; j.deadline := 10;
		j.hold := 40;
		Start(j);
		UnitAssert.True(StepWhile(j.firstDone, 2000), "срок вернул выбор");
		UnitAssert.EqualInt(-1, j.idx, "SelectCtx вернул −1");
		UnitAssert.True(j.expired, "причина — срок");
		UnitAssert.True(j.repeat & j.fast, "повтор после остановки — сразу");
		(* Предложения обоих каналов сняты: сообщение достанется другой задаче. *)
		ChannelStillWorks(a);
		UnitAssert.True(Pump(1000), "остановленная задача завершилась")
	END TestSelectCtxDeadlineFirst;

	PROCEDURE TestSelectCtxTimeoutFirst*;
		(* Тайм-аут 5 мс раньше срока пропуска 500 мс: побеждает тайм-аут. *)
		VAR j: SelJob; a, b: AsyncChan.Chan;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		NEW(j); j.a := a; j.b := b; j.timeout := 5; j.deadline := 500;
		Start(j);
		UnitAssert.True(Pump(2000), "задача завершилась");
		UnitAssert.EqualInt(2, j.idx, "победил тайм-аут");
		UnitAssert.True(~j.expired, "срок ещё не наступил")
	END TestSelectCtxTimeoutFirst;

	(* -------------------- трап 26 на новых вызовах ------------------------ *)

	PROCEDURE TestRecvCtxHoldDenied*;
		VAR h: Holder; j: CtxRecvJob;
	BEGIN
		errSeen := 0;
		NEW(h); h.allow := FALSE;
		NEW(j); j.h := h; j.c := AsyncChan.NewChan();
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "RecvCtx под монитором без разрешения трапует");
		errSeen := Kernel.err;
		UnitAssert.EqualInt(26, errSeen, "RecvCtx под монитором — трап 26");
		UnitAssert.True(~Tasks.Held(h), "монитор отпущен")
	END TestRecvCtxHoldDenied;

	PROCEDURE TestRecvCtxHoldAllowed*;
		VAR h: Holder; j: CtxRecvJob; s: Sender;
	BEGIN
		NEW(h); h.allow := TRUE;
		NEW(j); j.h := h; j.c := AsyncChan.NewChan();
		NEW(s); s.c := j.c; s.ms := 10; s.n := 9;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "RecvCtx с разрешением не трапует");
		UnitAssert.True(j.op.rec, "приём состоялся");
		UnitAssert.EqualInt(9, j.op.got, "сообщение получено");
		UnitAssert.True(~Tasks.Held(h), "монитор свободен")
	END TestRecvCtxHoldAllowed;

	PROCEDURE TestSelectCtxHoldDenied*;
		VAR h: Holder; j: CtxSelJob;
	BEGIN
		errSeen := 0;
		NEW(h); h.allow := FALSE;
		NEW(j); j.h := h; j.a := AsyncChan.NewChan(); j.b := AsyncChan.NewChan();
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "SelectCtx под монитором без разрешения трапует");
		errSeen := Kernel.err;
		UnitAssert.EqualInt(26, errSeen, "SelectCtx под монитором — трап 26");
		UnitAssert.True(~Tasks.Held(h), "монитор отпущен")
	END TestSelectCtxHoldDenied;

	PROCEDURE TestSelectCtxHoldAllowed*;
		VAR h: Holder; j: CtxSelJob; s: Sender;
	BEGIN
		NEW(h); h.allow := TRUE;
		NEW(j); j.h := h; j.a := AsyncChan.NewChan(); j.b := AsyncChan.NewChan();
		NEW(s); s.c := j.b; s.ms := 10; s.n := 7;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "SelectCtx с разрешением не трапует");
		UnitAssert.EqualInt(1, j.op.idx, "второй канал победил");
		UnitAssert.True(~Tasks.Held(h), "монитор свободен")
	END TestSelectCtxHoldAllowed;

	PROCEDURE TestSendCtxHoldDenied*;
		VAR h: Holder; j: CtxSendJob;
	BEGIN
		errSeen := 0;
		NEW(h); h.allow := FALSE;
		NEW(j); j.h := h; j.c := AsyncChan.NewChan();
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "SendCtx под монитором без разрешения трапует");
		errSeen := Kernel.err;
		UnitAssert.EqualInt(26, errSeen, "SendCtx под монитором — трап 26");
		UnitAssert.True(~Tasks.Held(h), "монитор отпущен")
	END TestSendCtxHoldDenied;

END Async_devCtxChan.
