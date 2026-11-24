MODULE AsyncTestsChannels;
(** Тесты библиотеки ожиданий этапа 7: Once, Chan, Buffer, Mailbox, Select.

	Сценарии идут в задачах Tasks; главный стек крутит Tasks.Step через
	Pump. Прямые Recv без ожидания (данные уже есть) допустимы и на
	главном стеке: AWAIT не вызывает Kernel.Await, если условие истинно. *)

	IMPORT Kernel, Monitors, Services, Tasks, AsyncSelect, AsyncChan, AsyncOnce, UnitAssert;

	CONST
		n = 100;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		StrMsg = POINTER TO RECORD (AsyncSelect.Msg) s: ARRAY 32 OF CHAR END;

		Filler = POINTER TO RECORD (Tasks.Task) o: AsyncOnce.Once; n: INTEGER END;
		Reader = POINTER TO RECORD (Tasks.Task) o: AsyncOnce.Once END;

		Producer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;

		BProducer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		BConsumer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;

		MSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Mailbox END;
		BFill = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;

		Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; delay, n: INTEGER END;
		SelectRecv = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; timeout: LONGINT END;
		SelectTimeout = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; timeout: LONGINT END;
		SelectSend = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer END;
		LateSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;
		DirectRecv = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;

		MonitorHolder = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
		WaitMux = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan END;
		RaceSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;

	VAR
		onceDone: BOOLEAN;
		trapOnce: AsyncOnce.Once;

		prodSum, consSum, consCount: INTEGER;
		ordered: BOOLEAN;
		last: INTEGER;
		seen: ARRAY n OF INTEGER;
		bufSum, bufCount: INTEGER;

		mailInt, mailStr, mailCount: INTEGER;
		mbox: AsyncChan.Mailbox;

		selIdx, selVal: INTEGER;
		toIdx: INTEGER; toElapsed: LONGINT;
		ssIdx: INTEGER;
		lateVal: INTEGER;

		holderLocked, muxDone: BOOLEAN;
		muxIdx, muxVal: INTEGER;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		(** Главный цикл без окна: действия Services (планировщик и
			таймеры Select) плюс опрос готовности дескрипторов. *)
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	(* --- Once ------------------------------------------------------------- *)

	PROCEDURE (t: Filler) Run;
	BEGIN
		Tasks.Sleep(10);
		t.o.FillInt(t.n)
	END Run;

	PROCEDURE (t: Reader) Run;
	BEGIN
		onceDone := t.o.ReadInt() = 42
	END Run;

	PROCEDURE FillThis;
	BEGIN
		trapOnce.FillInt(2)
	END FillThis;

	PROCEDURE TestOnce*;
		VAR o: AsyncOnce.Once; f: Filler; r: Reader;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); r.o := o; NEW(f); f.o := o; f.n := 42;
		Tasks.Start(r); Tasks.Start(f);
		UnitAssert.True(Pump(2000), "once tasks finish");
		UnitAssert.True(onceDone, "Read waits for Fill and sees 42");
		UnitAssert.True(o.done, "once is filled")
	END TestOnce;

	PROCEDURE TestOnceSecondFillTrap*;
	BEGIN
		trapOnce := AsyncOnce.New();
		trapOnce.FillInt(1);
		UnitAssert.Traps(FillThis, "second Fill traps")
	END TestOnceSecondFillTrap;

	(* --- встреча ---------------------------------------------------------- *)

	PROCEDURE (t: Producer) Run;
		VAR i: INTEGER; m: NumMsg;
	BEGIN
		FOR i := 0 TO t.n - 1 DO
			NEW(m); m.n := i;
			t.c.Send(m)
		END
	END Run;

	PROCEDURE (t: Consumer) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		FOR i := 0 TO t.n - 1 DO
			t.c.Recv(m);
			WITH m: NumMsg DO
				consSum := consSum + m.n;
				IF (last >= 0) & (m.n # last + 1) THEN ordered := FALSE END;
				last := m.n;
				INC(consCount)
			ELSE ordered := FALSE
			END
		END
	END Run;

	PROCEDURE TestRendezvous*;
		VAR c: AsyncChan.Chan; p: Producer; q: Consumer;
	BEGIN
		c := AsyncChan.NewChan();
		consSum := 0; consCount := 0; last := -1; ordered := TRUE;
		NEW(p); p.c := c; p.n := n;
		NEW(q); q.c := c; q.n := n;
		Tasks.Start(p); Tasks.Start(q);
		UnitAssert.True(Pump(5000), "rendezvous tasks finish");
		UnitAssert.EqualInt(n, consCount, "all items received");
		UnitAssert.EqualInt(n * (n - 1) DIV 2, consSum, "sum of items");
		UnitAssert.True(ordered, "order preserved")
	END TestRendezvous;

	(* --- ограниченный канал ----------------------------------------------- *)

	PROCEDURE (t: BProducer) Run;
		VAR i: INTEGER; m: NumMsg;
	BEGIN
		FOR i := 0 TO t.n - 1 DO
			NEW(m); m.n := i;
			t.c.Send(m)
		END
	END Run;

	PROCEDURE (t: BConsumer) Run;
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		FOR i := 0 TO t.n - 1 DO
			t.c.Recv(m);
			WITH m: NumMsg DO
				bufSum := bufSum + m.n;
				IF (m.n >= 0) & (m.n < LEN(seen)) & (seen[m.n] = 0) THEN
					seen[m.n] := 1; INC(bufCount)
				END
			END
		END
	END Run;

	PROCEDURE TestBounded*;
		VAR c: AsyncChan.Buffer; p: BProducer; q: BConsumer; i: INTEGER;
	BEGIN
		c := AsyncChan.NewBuffer(3);
		bufSum := 0; bufCount := 0;
		FOR i := 0 TO LEN(seen) - 1 DO seen[i] := 0 END;
		NEW(p); p.c := c; p.n := n;
		NEW(q); q.c := c; q.n := n;
		Tasks.Start(p); Tasks.Start(q);
		UnitAssert.True(Pump(5000), "bounded tasks finish");
		UnitAssert.EqualInt(n, bufCount, "each item once");
		UnitAssert.EqualInt(n * (n - 1) DIV 2, bufSum, "sum of items")
	END TestBounded;

	(* --- почтовый ящик ---------------------------------------------------- *)

	PROCEDURE (t: MSender) Run;
		VAR a: NumMsg; b: StrMsg;
	BEGIN
		NEW(a); a.n := 5; t.c.Send(a);
		NEW(b); b.s := "hello"; t.c.Send(b)
	END Run;

	PROCEDURE TestMailbox*;
		VAR s: MSender; m: AsyncSelect.Msg; i: INTEGER;
	BEGIN
		mailInt := 0; mailStr := 0; mailCount := 0;
		mbox := AsyncChan.NewMailbox();
		NEW(s); s.c := mbox; Tasks.Start(s);
		UnitAssert.True(Pump(2000), "mailbox sender finishes");
		FOR i := 0 TO 1 DO
			mbox.Recv(m);
			WITH m: NumMsg DO mailInt := mailInt + m.n
			| m: StrMsg DO
				IF m.s = "hello" THEN INC(mailStr) END
			END;
			INC(mailCount)
		END;
		UnitAssert.EqualInt(2, mailCount, "two messages");
		UnitAssert.EqualInt(5, mailInt, "integer message");
		UnitAssert.EqualInt(1, mailStr, "text message")
	END TestMailbox;

	(* --- Select по двум каналам ------------------------------------------- *)

	PROCEDURE (t: Sender) Run;
		VAR m: NumMsg;
	BEGIN
		Tasks.Sleep(t.delay);
		NEW(m); m.n := t.n;
		t.c.Send(m)
	END Run;

	PROCEDURE (t: SelectRecv) Run;
		VAR ops: ARRAY 3 OF AsyncSelect.Op; m: AsyncSelect.Msg;
	BEGIN
		AsyncChan.RecvAlt(t.a, ops[0]);
		AsyncChan.RecvAlt(t.b, ops[1]);
		AsyncChan.TimeoutAlt(t.timeout, ops[2]);
		selIdx := AsyncChan.Select(ops);
		m := ops[selIdx].got;
		WITH m: NumMsg DO selVal := m.n ELSE selVal := -1 END
	END Run;

	PROCEDURE TestSelect*;
		VAR a, b: AsyncChan.Chan; s1: Sender; r: SelectRecv;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		selIdx := -1; selVal := -1;
		NEW(s1); s1.c := b; s1.delay := 5; s1.n := 77;
		NEW(r); r.a := a; r.b := b; r.timeout := 2000;
		Tasks.Start(r); Tasks.Start(s1);
		UnitAssert.True(Pump(5000), "select tasks finish");
		UnitAssert.EqualInt(1, selIdx, "second alternative wins");
		UnitAssert.EqualInt(77, selVal, "value received")
	END TestSelect;

	PROCEDURE (t: SelectTimeout) Run;
		VAR ops: ARRAY 3 OF AsyncSelect.Op;
	BEGIN
		AsyncChan.RecvAlt(t.a, ops[0]);
		AsyncChan.RecvAlt(t.b, ops[1]);
		AsyncChan.TimeoutAlt(t.timeout, ops[2]);
		toIdx := AsyncChan.Select(ops)
	END Run;

	PROCEDURE TestSelectTimeout*;
		VAR a, b: AsyncChan.Chan; r: SelectTimeout; t0: LONGINT;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		toIdx := -1;
		NEW(r); r.a := a; r.b := b; r.timeout := 60;
		t0 := Kernel.Time();
		Tasks.Start(r);
		UnitAssert.True(Pump(3000), "timeout select finishes");
		toElapsed := Kernel.Time() - t0;
		UnitAssert.EqualInt(2, toIdx, "timeout alternative wins");
		UnitAssert.True(toElapsed >= 50, "timeout waited")
	END TestSelectTimeout;

	PROCEDURE (t: LateSender) Run;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := t.n;
		t.c.Send(m)
	END Run;

	PROCEDURE (t: DirectRecv) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		t.c.Recv(m);
		WITH m: NumMsg DO lateVal := m.n END
	END Run;

	PROCEDURE TestNoStaleAfterTimeout*;
		VAR a, b: AsyncChan.Chan; r: SelectTimeout; s: LateSender; d: DirectRecv;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		toIdx := -1;
		NEW(r); r.a := a; r.b := b; r.timeout := 30;
		Tasks.Start(r);
		UnitAssert.True(Pump(3000), "timeout finishes");
		(* прямой обмен по a после снятия предложений *)
		lateVal := -1;
		NEW(s); s.c := a; s.n := 123;
		NEW(d); d.c := a;
		Tasks.Start(s); Tasks.Start(d);
		UnitAssert.True(Pump(3000), "late exchange finishes");
		UnitAssert.EqualInt(123, lateVal, "message delivered after timeout")
	END TestNoStaleAfterTimeout;

	(* --- Select с отправкой в полный буфер -------------------------------- *)

	PROCEDURE (t: SelectSend) Run;
		VAR ops: ARRAY 2 OF AsyncSelect.Op; m: NumMsg;
	BEGIN
		NEW(m); m.n := 999;
		AsyncChan.SendAlt(t.c, m, ops[0]);
		AsyncChan.TimeoutAlt(40, ops[1]);
		ssIdx := AsyncChan.Select(ops)
	END Run;

	PROCEDURE (t: BFill) Run;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := t.n; t.c.Send(m)
	END Run;

	PROCEDURE TestSelectSendFull*;
		VAR c: AsyncChan.Buffer; s: SelectSend; f: BFill; m: AsyncSelect.Msg;
	BEGIN
		c := AsyncChan.NewBuffer(1);
		NEW(f); f.c := c; f.n := 1; Tasks.Start(f);
		UnitAssert.True(Pump(2000), "fill task finishes");
		UnitAssert.EqualInt(1, c.count, "buffer full");
		ssIdx := -1;
		NEW(s); s.c := c; Tasks.Start(s);
		UnitAssert.True(Pump(3000), "select send finishes");
		UnitAssert.EqualInt(1, ssIdx, "timeout wins on full buffer");
		mbox := NIL;
		c.Recv(m);	(* данных достаточно: AWAIT не ждёт, главный стек допустим *)
		UnitAssert.EqualInt(0, c.count, "buffer empty after one recv");
		WITH m: NumMsg DO UnitAssert.EqualInt(1, m.n, "original message kept")
		ELSE UnitAssert.Fail("wrong message type")
		END
	END TestSelectSendFull;

	(* --- Select, пока задача ждёт монитор другого канала ------------------- *)
	(** Дефект этапа 7: Select регистрирует альтернативы по одной. Если
		первый канал готов, а второй занят чужим монитором, задача паркуется
		на Kernel.Lock. Отправитель первого канала решает предложение и
		будит задачу через Tasks.Unpark; задача просыпается в Kernel.Lock,
		не получив монитор, и трапает «woken without the monitor». *)

	PROCEDURE (h: MonitorHolder) Run;
	BEGIN
		Kernel.Lock(h.c);	(* держим монитор второго канала *)
		holderLocked := TRUE;
		Tasks.Sleep(50);
		Kernel.Unlock(h.c)
	END Run;

	PROCEDURE (m: WaitMux) Run;
		VAR ops: ARRAY 2 OF AsyncSelect.Op; msg: AsyncSelect.Msg;
	BEGIN
		AsyncChan.RecvAlt(m.a, ops[0]);
		AsyncChan.RecvAlt(m.b, ops[1]);
		muxIdx := AsyncChan.Select(ops);
		muxVal := -1;
		IF muxIdx = 0 THEN
			msg := ops[0].got;
			WITH msg: NumMsg DO muxVal := msg.n END
		END;
		muxDone := TRUE
	END Run;

	PROCEDURE (s: RaceSender) Run;
		VAR m: NumMsg;
	BEGIN
		Tasks.Sleep(10);
		NEW(m); m.n := 77;
		s.c.Send(m)
	END Run;

	PROCEDURE TestProcSelectWhileMonitorWait*;
		VAR a, b: AsyncChan.Chan; h: MonitorHolder; m: WaitMux; s: RaceSender;
	BEGIN
		a := AsyncChan.NewChan(); b := AsyncChan.NewChan();
		holderLocked := FALSE; muxDone := FALSE; muxIdx := -1; muxVal := -1;
		NEW(h); h.c := b; Tasks.Start(h);
		WHILE ~holderLocked & (Tasks.Count() > 0) DO Tasks.Step END;
		UnitAssert.True(holderLocked, "holder took the monitor");
		NEW(m); m.a := a; m.b := b; Tasks.Start(m);
		Tasks.Step;	(* mux зарегистрировал a и встал на монитор b *)
		UnitAssert.True(Tasks.State(m) = Tasks.Parked, "mux waits for the monitor");
		NEW(s); s.c := a; Tasks.Start(s);
		UnitAssert.True(Pump(5000), "select finished after the monitor was released");
		UnitAssert.True(~Tasks.Failed(m), "mux survived the monitor wait");
		UnitAssert.EqualInt(0, muxIdx, "first alternative won");
		UnitAssert.EqualInt(77, muxVal, "message was received");
		UnitAssert.True(~Monitors.IsLocked(b), "second monitor released")
	END TestProcSelectWhileMonitorWait;

END AsyncTestsChannels.
