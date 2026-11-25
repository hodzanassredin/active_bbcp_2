MODULE Async_devCtxSleep;
(** Сон под монитором-объектом в ожиданиях Async.

	Задача входит в Tasks.Object и внутри Exclusive ждёт выбора
	AsyncSelect.Select или приёма из AsyncChan. Оба пути кончаются
	Tasks.Park/ParkExternal, где общая проверка мониторов (трап 26)
	требует HoldsWhileWaiting = TRUE. Проверяются: трап без разрешения,
	работа с разрешением и приём без монитора — как раньше. *)

	IMPORT Kernel, Services, Tasks, AsyncSelect, AsyncChan, UnitAssert;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Holder = POINTER TO RECORD (Tasks.Object) allow: BOOLEAN END;

		SelectOp = RECORD (Tasks.Op)
			a, b: AsyncChan.Chan; idx: INTEGER
		END;
		RecvOp = RECORD (Tasks.Op) c: AsyncChan.Chan; msg: AsyncSelect.Msg END;

		SelJob = POINTER TO RECORD (Tasks.Job)
			h: Holder; a, b: AsyncChan.Chan; idx: INTEGER; done: BOOLEAN
		END;
		RecvJob = POINTER TO RECORD (Tasks.Job)
			h: Holder; c: AsyncChan.Chan; got: INTEGER; done: BOOLEAN
		END;
		PlainRecvJob = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan; got: INTEGER; done: BOOLEAN
		END;

		Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; ms: LONGINT; n: INTEGER END;

	VAR
		errSeen: INTEGER;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Start (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, 128 * 1024); Tasks.Start(t)
	END Start;

	(* -------------------- объект-монитор -------------------------------- *)

	PROCEDURE (h: Holder) HoldsWhileWaiting (): BOOLEAN;
	BEGIN
		RETURN h.allow
	END HoldsWhileWaiting;

	PROCEDURE (h: Holder) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR sel: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		WITH op: SelectOp DO
			AsyncChan.RecvAlt(op.a, sel[0]);
			AsyncChan.RecvAlt(op.b, sel[1]);
			op.idx := AsyncChan.Select(sel)
		| op: RecvOp DO
			op.c.Recv(op.msg)
		ELSE
			HALT(20)
		END
	END Exclusive;

	(* -------------------- задачи ---------------------------------------- *)

	PROCEDURE (j: SelJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: SelectOp;
	BEGIN
		op.a := j.a; op.b := j.b; op.idx := -1;
		j.h.Do(ctx, op);
		j.idx := op.idx; j.done := TRUE
	END Body;

	PROCEDURE (j: RecvJob) Body- (VAR ctx: Tasks.Ctx);
		VAR op: RecvOp; m: AsyncSelect.Msg;
	BEGIN
		op.c := j.c; op.msg := NIL;
		j.h.Do(ctx, op);
		m := op.msg;
		IF m # NIL THEN
			WITH m: NumMsg DO j.got := m.n ELSE j.got := -1 END
		END;
		j.done := TRUE
	END Body;

	PROCEDURE (j: PlainRecvJob) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		j.c.Recv(m);
		IF m # NIL THEN
			WITH m: NumMsg DO j.got := m.n ELSE j.got := -1 END
		END;
		j.done := TRUE
	END Body;

	PROCEDURE (s: Sender) Run;
		VAR m: NumMsg;
	BEGIN
		Tasks.Sleep(s.ms);
		NEW(m); m.n := s.n;
		s.c.Send(m)
	END Run;

	(* -------------------- тесты ----------------------------------------- *)

	PROCEDURE TestSelectHoldDenied*;
		(* Select на пустых каналах под монитором без разрешения — трап 26. *)
		VAR h: Holder; j: SelJob;
	BEGIN
		errSeen := 0;
		NEW(h); h.allow := FALSE;
		NEW(j); j.h := h; j.a := AsyncChan.NewChan(); j.b := AsyncChan.NewChan();
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "Select под монитором без разрешения трапует");
		errSeen := Kernel.err;
		UnitAssert.EqualInt(26, errSeen, "Select под монитором — трап 26");
		UnitAssert.True(~j.done, "тело после трапа не продолжилось");
		UnitAssert.True(~Tasks.Held(h), "монитор отпущен")
	END TestSelectHoldDenied;

	PROCEDURE TestSelectHoldAllowed*;
		VAR h: Holder; j: SelJob; s: Sender;
	BEGIN
		NEW(h); h.allow := TRUE;
		NEW(j); j.h := h; j.a := AsyncChan.NewChan(); j.b := AsyncChan.NewChan();
		NEW(s); s.c := j.a; s.ms := 10; s.n := 7;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "Select с разрешением не трапует");
		UnitAssert.True(j.done, "тело дошло до конца");
		UnitAssert.EqualInt(0, j.idx, "первая альтернатива победила");
		UnitAssert.True(~Tasks.Held(h), "монитор свободен")
	END TestSelectHoldAllowed;

	PROCEDURE TestRecvHoldDenied*;
		(* Приём из пустого канала под монитором без разрешения — трап 26. *)
		VAR h: Holder; j: RecvJob;
	BEGIN
		errSeen := 0;
		NEW(h); h.allow := FALSE;
		NEW(j); j.h := h; j.c := AsyncChan.NewChan();
		Start(j);
		UnitAssert.True(Pump(1000), "задача закончилась");
		UnitAssert.True(Tasks.Failed(j), "приём под монитором без разрешения трапует");
		errSeen := Kernel.err;
		UnitAssert.EqualInt(26, errSeen, "приём под монитором — трап 26");
		UnitAssert.True(~j.done, "тело после трапа не продолжилось");
		UnitAssert.True(~Tasks.Held(h), "монитор отпущен")
	END TestRecvHoldDenied;

	PROCEDURE TestRecvHoldAllowed*;
		VAR h: Holder; j: RecvJob; s: Sender;
	BEGIN
		NEW(h); h.allow := TRUE;
		NEW(j); j.h := h; j.c := AsyncChan.NewChan();
		NEW(s); s.c := j.c; s.ms := 10; s.n := 9;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "приём с разрешением не трапует");
		UnitAssert.True(j.done, "тело дошло до конца");
		UnitAssert.EqualInt(9, j.got, "сообщение получено");
		UnitAssert.True(~Tasks.Held(h), "монитор свободен")
	END TestRecvHoldAllowed;

	PROCEDURE TestRecvWithoutMonitor*;
		(* Без монитора приём ведёт себя как раньше. *)
		VAR j: PlainRecvJob; s: Sender;
	BEGIN
		NEW(j); j.c := AsyncChan.NewChan();
		NEW(s); s.c := j.c; s.ms := 10; s.n := 5;
		Start(j); Start(s);
		UnitAssert.True(Pump(2000), "задача закончилась");
		UnitAssert.True(~Tasks.Failed(j), "без монитора трапа нет");
		UnitAssert.True(j.done, "тело дошло до конца");
		UnitAssert.EqualInt(5, j.got, "сообщение получено")
	END TestRecvWithoutMonitor;

END Async_devCtxSleep.
