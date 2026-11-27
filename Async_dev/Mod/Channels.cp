MODULE Async_devChannels;
(** Тесты библиотеки ожиданий: Once, Chan, Buffer, Mailbox, Select.

	Сценарии идут в задачах Tasks; главный стек крутит Tasks.Step через
	Pump. Прямые Recv без ожидания (данные уже есть) допустимы и на
	главном стеке: операция очереди без ожидания возвращается сразу. *)

	IMPORT Kernel, Services, Tasks, AsyncSelect, AsyncChan, AsyncOnce, UnitAssert;

	CONST
		n = 100;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		StrMsg = POINTER TO RECORD (AsyncSelect.Msg) s: ARRAY 32 OF CHAR END;

		Filler = POINTER TO RECORD (Tasks.Job) o: AsyncOnce.Once; n: INTEGER END;
		Reader = POINTER TO RECORD (Tasks.Job) o: AsyncOnce.Once END;
		LateReader = POINTER TO RECORD (Tasks.Job)
			o: AsyncOnce.Once; x: INTEGER; read, expired, done: BOOLEAN
		END;
		SecondFill = POINTER TO RECORD (Tasks.Job) o: AsyncOnce.Once END;

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

		Echo = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;
		Filler64 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		Drainer64 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		PlainSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;
		Picker = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;
		EchoBack = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;
		SelfSelect = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
		AltSender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;
		WakeKeeper = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; idx: INTEGER; parkedBack: BOOLEAN END;

	VAR
		onceDone: BOOLEAN;

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

		echoAllocs: LONGINT;
		drained, picked: INTEGER;	(* выделений за вторую сотню обменов Echo *)
		selfIdx: INTEGER; selfGot: AsyncSelect.Msg;

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

	PROCEDURE (t: Filler) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(10);
		t.o.FillInt(ctx, t.n)
	END Body;

	PROCEDURE (t: Reader) Body- (VAR ctx: Tasks.Ctx);
		VAR x: INTEGER;
	BEGIN
		onceDone := t.o.ReadInt(ctx, x) & (x = 42)
	END Body;

	PROCEDURE (t: LateReader) Body- (VAR ctx: Tasks.Ctx);
	(* Срок пропуска раньше заполнения: чтение возвращает FALSE и 0. *)
		VAR x: INTEGER;
	BEGIN
		Tasks.SetDeadline(ctx, 10);
		x := 7;
		t.read := t.o.ReadInt(ctx, x);
		t.x := x; t.expired := ctx.expired; t.done := TRUE
	END Body;

	PROCEDURE (t: SecondFill) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		t.o.FillInt(ctx, 1);
		t.o.FillInt(ctx, 2)	(* повторное заполнение — трап 21 *)
	END Body;

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

	PROCEDURE TestOnceReadStopped*;
	(* Срок пропуска наступил раньше заполнения: Read вернул FALSE и 0,
	   причина — срок; позднее заполнение проходит. *)
		VAR o: AsyncOnce.Once; r: LateReader; f: Filler;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); r.o := o; Tasks.Start(r);
		UnitAssert.True(Pump(2000), "reader ends by deadline");
		UnitAssert.True(r.done & ~r.read, "Read returned FALSE");
		UnitAssert.EqualInt(0, r.x, "value is 0");
		UnitAssert.True(r.expired, "reason is the deadline");
		NEW(f); f.o := o; f.n := 5; Tasks.Start(f);
		UnitAssert.True(Pump(2000), "late fill ends");
		UnitAssert.True(o.done, "once is filled")
	END TestOnceReadStopped;

	PROCEDURE TestOnceSecondFillTrap*;
		VAR o: AsyncOnce.Once; t: SecondFill;
	BEGIN
		o := AsyncOnce.New();
		NEW(t); t.o := o; Tasks.Start(t);
		UnitAssert.True(Pump(2000), "fill task ends");
		UnitAssert.True(Tasks.Failed(t), "second Fill traps");
		UnitAssert.True(o.done, "first Fill stayed")
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
		c.Recv(m);	(* данных достаточно: ожидания нет, главный стек допустим *)
		UnitAssert.EqualInt(0, c.count, "buffer empty after one recv");
		WITH m: NumMsg DO UnitAssert.EqualInt(1, m.n, "original message kept")
		ELSE UnitAssert.Fail("wrong message type")
		END
	END TestSelectSendFull;

	(* --- обмен без выделений памяти --------------------------------------- *)

	PROCEDURE (t: Echo) Run;
		VAR i: INTEGER; m: NumMsg; x: AsyncSelect.Msg; c0: LONGINT;
	BEGIN
		NEW(m); x := m; c0 := 0;
		FOR i := 1 TO t.n DO
			IF i = t.n DIV 2 THEN c0 := Kernel.AllocCount() END;
			t.a.Send(x); t.b.Recv(x)
		END;
		echoAllocs := Kernel.AllocCount() - c0
	END Run;

	PROCEDURE (t: EchoBack) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.a.Recv(x); t.b.Send(x) END
	END Run;

	PROCEDURE TestProcRendezvousNoAlloc*;
	(* Блокирующий обмен через встречу берёт записи ожидания из свободных
	   списков: после разогрева сотня обменов не выделяет памяти. *)
		VAR p: Echo; q: EchoBack;
	BEGIN
		NEW(p); NEW(q);
		p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan(); p.n := 200;
		q.a := p.a; q.b := p.b; q.n := 200;
		echoAllocs := -1;
		Tasks.Start(p); Tasks.Start(q);
		UnitAssert.True(Pump(5000), "exchange finished");
		UnitAssert.EqualInt(0, SHORT(echoAllocs), "no allocations per blocking exchange")
	END TestProcRendezvousNoAlloc;

	(* --- буфер: пачки без переключений и без выделений ------------------- *)

	PROCEDURE (t: Filler64) Run;
		VAR i: INTEGER; m: NumMsg;
	BEGIN
		NEW(m); m.n := 1;
		FOR i := 1 TO t.n DO t.c.Send(m) END
	END Run;

	PROCEDURE (t: Drainer64) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); INC(drained, x(NumMsg).n) END
	END Run;

	PROCEDURE TestProcBufferBatches*;
	(* Отправитель кладёт в буфер, пока есть место, и не ждёт монитора,
	   отданного разбуженному получателю: на 6400 сообщений через буфер 64
	   приходится порядка 2 * 6400 / 64 переключений, а не по два на
	   сообщение. Сообщения лежат в кольце: обмен не выделяет памяти. *)
		CONST total = 6400;
		VAR p: Filler64; q: Drainer64; sw, a: LONGINT;
	BEGIN
		NEW(p); NEW(q); p.c := AsyncChan.NewBuffer(64); q.c := p.c; p.n := total; q.n := total;
		drained := 0;
		sw := Tasks.Switches(); a := Kernel.AllocCount();
		Tasks.Start(p); Tasks.Start(q);
		UnitAssert.True(Pump(5000), "buffer exchange finished");
		UnitAssert.EqualInt(total, drained, "every message received");
		UnitAssert.True(Tasks.Switches() - sw < total DIV 16, "switches in batches, not per message");
		(* кольцо и первые записи ожидания; на сообщение — ничего *)
		UnitAssert.True(Kernel.AllocCount() - a < 16, "no allocations per message")
	END TestProcBufferBatches;

	(* --- встреча: ждущий отправитель не держит монитор ------------------- *)

	PROCEDURE (t: PlainSender) Run;
		VAR i: INTEGER; m: NumMsg;
	BEGIN
		NEW(m); m.n := 1;
		FOR i := 1 TO t.n DO t.c.Send(m) END
	END Run;

	PROCEDURE (t: Picker) Run;
		VAR i: INTEGER; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		FOR i := 1 TO t.n DO
			AsyncChan.RecvAlt(t.a, ops[0]); AsyncChan.RecvAlt(t.b, ops[1]);
			IF AsyncChan.Select(ops) >= 0 THEN INC(picked) END
		END
	END Run;

	PROCEDURE TestProcSelectRendezvousSwitches*;
	(* Отправитель встречи ждёт предложением в очереди канала, а не на
	   мониторе: Select забирает его сообщение без ожидания монитора, на
	   выбор приходится два переключения (отправитель и Select), а не восемь. *)
		CONST total = 2000;
		VAR a, b: PlainSender; p: Picker; sw: LONGINT;
	BEGIN
		NEW(a); NEW(b); NEW(p);
		a.c := AsyncChan.NewChan(); b.c := AsyncChan.NewChan(); a.n := total DIV 2; b.n := total DIV 2;
		p.a := a.c; p.b := b.c; p.n := total;
		picked := 0; sw := Tasks.Switches();
		Tasks.Start(a); Tasks.Start(b); Tasks.Start(p);
		UnitAssert.True(Pump(5000), "select exchange finished");
		UnitAssert.EqualInt(total, picked, "every message selected");
		UnitAssert.True(Tasks.Switches() - sw < 3 * total, "about two switches per select")
	END TestProcSelectRendezvousSwitches;

	(* --- встреча: задача не встречается сама с собой ---------------------- *)

	PROCEDURE (t: SelfSelect) Run;
		VAR ops: ARRAY 3 OF AsyncSelect.Op; m: NumMsg;
	BEGIN
		NEW(m); m.n := 5;
		AsyncChan.SendAlt(t.c, m, ops[0]);
		AsyncChan.RecvAlt(t.c, ops[1]);
		AsyncChan.TimeoutAlt(50, ops[2]);
		selfIdx := AsyncChan.Select(ops);
		selfGot := ops[1].got
	END Run;

	PROCEDURE TestSelectSendRecvSameChanTimesOut*;
	(* Отправка и приём одного Select на одной встрече не исполняют друг
	   друга: других задач нет, побеждает тайм-аут. *)
		VAR t: SelfSelect;
	BEGIN
		selfIdx := -1; selfGot := NIL;
		NEW(t); t.c := AsyncChan.NewChan(); Tasks.Start(t);
		UnitAssert.True(Pump(3000), "select finishes");
		UnitAssert.True(~Tasks.Failed(t), "select did not trap");
		UnitAssert.EqualInt(2, selfIdx, "timeout alternative wins");
		UnitAssert.True(selfGot = NIL, "nothing received from itself")
	END TestSelectSendRecvSameChanTimesOut;

	(* --- встреча: отправители в порядке прихода -------------------------- *)

	PROCEDURE (t: AltSender) Run;
		VAR ops: ARRAY 1 OF AsyncSelect.Op; m: NumMsg;
	BEGIN
		NEW(m); m.n := t.n;
		AsyncChan.SendAlt(t.c, m, ops[0]);
		IF AsyncChan.Select(ops) # 0 THEN HALT(60) END
	END Run;

	PROCEDURE NextNum (c: AsyncChan.Chan): INTEGER;
		VAR m: AsyncSelect.Msg;
	BEGIN
		c.Recv(m);	(* отправитель ждёт: ожидания нет, главный стек допустим *)
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END NextNum;

	PROCEDURE TestRendezvousSendersFifo*;
	(* Ждущие отправители встречи — прямые и через Select — обслуживаются
	   в порядке прихода. *)
		VAR c: AsyncChan.Chan; s1, s3: LateSender; s2: AltSender;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s1); s1.c := c; s1.n := 1; Tasks.Start(s1); Tasks.Step;
		NEW(s2); s2.c := c; s2.n := 2; Tasks.Start(s2); Tasks.Step;
		NEW(s3); s3.c := c; s3.n := 3; Tasks.Start(s3); Tasks.Step;
		UnitAssert.True(Tasks.State(s3) = Tasks.Parked, "senders wait");
		UnitAssert.EqualInt(1, NextNum(c), "first sender first");
		UnitAssert.EqualInt(2, NextNum(c), "select sender second");
		UnitAssert.EqualInt(3, NextNum(c), "third sender third");
		UnitAssert.True(Pump(3000), "senders finish")
	END TestRendezvousSendersFifo;

	PROCEDURE (t: WakeKeeper) Run;
		VAR ops: ARRAY 1 OF AsyncSelect.Op;
	BEGIN
		AsyncChan.RecvAlt(t.c, ops[0]);
		t.idx := AsyncChan.Select(ops);
		Tasks.Park;	(* посторонний Unpark во время Select не потерян *)
		t.parkedBack := TRUE
	END Run;

	PROCEDURE TestProcSelectKeepsForeignUnpark*;
	(** Unpark задачи, ждущей в Select, будит её ложно: Select ждёт дальше,
		а пробуждение остаётся следующему Park. *)
		VAR k: WakeKeeper; s: Sender;
	BEGIN
		NEW(k); k.c := AsyncChan.NewChan(); k.idx := -1; Tasks.Start(k);
		Tasks.Step;
		UnitAssert.True(Tasks.State(k) = Tasks.Parked, "select waits");
		Tasks.Unpark(k); Tasks.Step;
		UnitAssert.True(Tasks.State(k) = Tasks.Parked, "select still waits after a foreign wake");
		NEW(s); s.c := k.c; s.delay := 10; s.n := 1; Tasks.Start(s);
		UnitAssert.True(Pump(2000), "tasks finished");
		UnitAssert.EqualInt(0, k.idx, "select received");
		UnitAssert.True(k.parkedBack, "foreign wake was kept for Park");
		Tasks.Cancel(k); IF Pump(1000) THEN END	(* уборка после провала *)
	END TestProcSelectKeepsForeignUnpark;

END Async_devChannels.
