MODULE Actor_devBounded;
(** Стойкий актор с ограниченным ящиком (Props.capacity > 0) и журналом,
	который отвечает позже. Ответ журнала не должен теряться на полном
	ящике: иначе актор навсегда остаётся «жду запись». Отложенные письма
	(Stash) считаются в предел ящика: пока актор ждёт ответа, держит не
	больше capacity писем, а возвращённые — без потерь и по порядку.
	Ящики обслуживает ActorSim, прогон — функция зерна. *)

	IMPORT Services, Tasks, Strings, Stores,
		ActorCore, ActorRun, ActorJournal, ActorPersist, ActorSim, UnitAssert;

	CONST
		cap = 3;
		maxIds = 4096;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) id: INTEGER END;

		Added = POINTER TO RECORD (ActorPersist.Event) id: INTEGER END;

		Counter = POINTER TO RECORD (ActorPersist.Actor) END;

		(* ответ «записано» позже: запросы ждут Flush *)
		Pending = POINTER TO RECORD
			stream: ActorCore.Name; expected: LONGINT; m: ActorJournal.Meta;
			body: ActorJournal.Bytes; len: INTEGER; r: ActorJournal.Request;
			next: Pending
		END;

		Late = POINTER TO RECORD (ActorJournal.Journal)
			in: ActorJournal.Journal;
			head, tail: Pending
		END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		the: Counter;
		j: Late;
		applied: ARRAY maxIds OF INTEGER; nApplied, acked: INTEGER;
		runs: INTEGER;
		driverDone: BOOLEAN;
		rnd: LONGINT;
		seed: LONGINT; runOk: BOOLEAN; runMsg: ARRAY 256 OF CHAR;
		sent, dead: INTEGER;

	(* --- журнал с ответом позже ----------------------------------------------- *)

	PROCEDURE (l: Late) Last (IN stream: ARRAY OF CHAR): LONGINT;
	BEGIN RETURN l.in.Last(stream) END Last;

	PROCEDURE (l: Late) Append (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
	BEGIN l.in.Append(stream, expected, m, body, len, res) END Append;

	PROCEDURE (l: Late) Read (IN stream: ARRAY OF CHAR; from: LONGINT; s: ActorJournal.Sink; OUT res: INTEGER);
	BEGIN l.in.Read(stream, from, s, res) END Read;

	PROCEDURE (l: Late) SaveSnapshot (IN stream: ARRAY OF CHAR; seq: LONGINT;
		IN data: ARRAY OF BYTE; len: INTEGER; OUT res: INTEGER);
	BEGIN l.in.SaveSnapshot(stream, seq, data, len, res) END SaveSnapshot;

	PROCEDURE (l: Late) LoadSnapshot (IN stream: ARRAY OF CHAR; OUT seq: LONGINT;
		OUT data: ActorJournal.Bytes; OUT len: INTEGER; OUT res: INTEGER);
	BEGIN l.in.LoadSnapshot(stream, seq, data, len, res) END LoadSnapshot;

	PROCEDURE (l: Late) Submit (IN stream: ARRAY OF CHAR; expected: LONGINT; IN m: ActorJournal.Meta;
		IN body: ARRAY OF BYTE; len: INTEGER; r: ActorJournal.Request);
		VAR p: Pending; i: INTEGER;
	BEGIN
		NEW(p); p.stream := stream$; p.expected := expected; p.m := m; p.len := len; p.r := r;
		NEW(p.body, MAX(len, 1)); FOR i := 0 TO len - 1 DO p.body[i] := body[i] END;
		IF l.tail = NIL THEN l.head := p ELSE l.tail.next := p END;
		l.tail := p
	END Submit;

	PROCEDURE (l: Late) Flush (k: INTEGER), NEW;
	(* Пост: на первые k ждущих записей (все при k < 0) ответ дан. *)
		VAR p: Pending; res: INTEGER;
	BEGIN
		WHILE (l.head # NIL) & (k # 0) DO
			p := l.head; l.head := p.next; IF l.head = NIL THEN l.tail := NIL END;
			l.in.Append(p.stream, p.expected, p.m, p.body^, p.len, res); p.r.Done(res);
			DEC(k)
		END
	END Flush;

	(* --- актор ------------------------------------------------------------------ *)

	PROCEDURE (e: Added) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.id)
	END Externalize;

	PROCEDURE (e: Added) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.id)
	END Internalize;

	PROCEDURE (a: Counter) Apply (e: ActorPersist.Event);
	BEGIN
		IF nApplied < maxIds THEN applied[nApplied] := e(Added).id END;
		INC(nApplied)
	END Apply;

	PROCEDURE (a: Counter) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Added;
	BEGIN
		WITH m: Inc DO NEW(e); e.id := m.id; a.Persist(c, e, 0) ELSE END
	END Handle;

	PROCEDURE (a: Counter) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
	BEGIN
		INC(acked)
	END Persisted;

	PROCEDURE NewCounter (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); a.journal := j; the := a; RETURN a
	END NewCounter;

	(* --- обвязка ------------------------------------------------------------------ *)

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~flag & (Services.Ticks() - t <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END Until;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		RETURN Until(driverDone, ms)
	END Drive;

	PROCEDURE Rand (n: INTEGER): INTEGER;
	BEGIN
		rnd := (rnd * 1103515245 + 12345) MOD 2147483648;
		RETURN SHORT((rnd DIV 65536) MOD n)
	END Rand;

	PROCEDURE Start (): ActorCore.Ref;
	(* Пост: новый счётчик с ящиком на cap писем и пустым журналом восстановлен. *)
		VAR p: ActorCore.Props; s: ActorCore.Name; d: ARRAY 16 OF CHAR; r: ActorCore.Ref;
	BEGIN
		IF ~ActorSim.Idle() THEN IF ActorSim.Drain(100000) THEN END END;
		ActorSim.Install;
		NEW(j); j.in := ActorJournal.NewMem();
		nApplied := 0; acked := 0; sent := 0; dead := 0; the := NIL;
		INC(runs); Strings.IntToString(runs, d); s := "bounded" + d;
		p := ActorCore.NewProps(NewCounter); p.capacity := cap;
		r := ActorCore.Spawn(NIL, p, s);
		IF ActorSim.Drain(1000) THEN END;
		RETURN r
	END Start;

	PROCEDURE Send (r: ActorCore.Ref);
		VAR m: Inc; d0: LONGINT;
	BEGIN
		INC(sent); NEW(m); m.id := sent;
		d0 := ActorCore.deadLetters; r.Post(m, NIL);
		IF ActorCore.deadLetters # d0 THEN INC(dead) END
	END Send;

	PROCEDURE Fail (IN what: ARRAY OF CHAR);
		VAR d: ARRAY 24 OF CHAR;
	BEGIN
		IF runOk THEN
			runOk := FALSE; Strings.IntToString(seed, d);
			runMsg := "seed " + d + ": " + what
		END
	END Fail;

	PROCEDURE Check (r: ActorCore.Ref);
	(* Инварианты после того, как журнал ответил на всё и работы нет. *)
		VAR i: INTEGER;
	BEGIN
		IF (the = NIL) OR the.Busy() THEN Fail("актор остался ждать запись: ответ журнала потерян") END;
		IF nApplied + dead # sent THEN Fail("письмо не применено и не мёртвое: потеряно молча") END;
		IF acked # nApplied THEN Fail("Persisted не на каждое событие") END;
		IF j.in.Last(r.name) # nApplied THEN Fail("в журнале не столько событий, сколько применено") END;
		FOR i := 1 TO MIN(nApplied, maxIds) - 1 DO
			IF applied[i] <= applied[i - 1] THEN Fail("порядок писем нарушен") END
		END
	END Check;

	PROCEDURE Settle;
	BEGIN
		REPEAT j.Flush(-1); IF ActorSim.Drain(100000) THEN END UNTIL (j.head = NIL) & ActorSim.Idle()
	END Settle;

	PROCEDURE RunSeed (steps: INTEGER);
	(* Поток писем вперемешку с шагами ящика и ответами журнала по зерну. *)
		VAR r: ActorCore.Ref; i, k: INTEGER;
	BEGIN
		rnd := seed; runOk := TRUE; runMsg := "";
		r := Start();
		FOR i := 1 TO steps DO
			CASE Rand(4) OF
			| 0: FOR k := 0 TO Rand(5) DO Send(r) END
			| 1: IF ActorSim.Step() THEN END
			| 2: j.Flush(Rand(3))
			| 3: IF ActorSim.Drain(Rand(4)) THEN END
			END
		END;
		Settle; Check(r);
		r.Stop; IF ActorSim.Drain(1000) THEN END
	END RunSeed;

	(* --- тесты ---------------------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		seed := 1
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown
	END Teardown;

	PROCEDURE ReplyBody;
	(* Ящик полон, когда журнал отвечает: ответ всё равно доходит. *)
		VAR r: ActorCore.Ref; k: INTEGER;
	BEGIN
		runOk := TRUE; seed := 0;
		r := Start();
		Send(r); IF ActorSim.Drain(10) THEN END;	(* запись 1 ждёт журнала *)
		FOR k := 1 TO cap DO Send(r) END;	(* ящик полон *)
		j.Flush(-1);	(* ответ «записано» приходит в полный ящик *)
		Settle; Check(r)
	END ReplyBody;

	PROCEDURE TestReplyNotLostOnFullMailbox*;
	BEGIN
		UnitAssert.True(Drive(ReplyBody, 5000), "run");
		UnitAssert.True(runOk, runMsg);
		UnitAssert.EqualInt(1 + cap, nApplied, "все принятые письма записаны")
	END TestReplyNotLostOnFullMailbox;

	PROCEDURE StashBody;
	(* Журнал не отвечает; письма идут по одному, ящик успевает их отложить. *)
		VAR r: ActorCore.Ref; k: INTEGER;
	BEGIN
		runOk := TRUE; seed := 0;
		r := Start();
		Send(r); IF ActorSim.Drain(10) THEN END;
		FOR k := 1 TO 10 DO Send(r); IF ActorSim.Drain(10) THEN END END;
		Settle; Check(r)
	END StashBody;

	PROCEDURE TestStashCountsToCapacity*;
	BEGIN
		UnitAssert.True(Drive(StashBody, 5000), "run");
		UnitAssert.True(runOk, runMsg);
		UnitAssert.EqualInt(1 + cap, nApplied, "пока ждёт запись, держит не больше cap писем");
		UnitAssert.EqualInt(10 - cap, dead, "лишние — мёртвые письма")
	END TestStashCountsToCapacity;

	PROCEDURE SeedsBody;
		VAR s: LONGINT;
	BEGIN
		s := 1;
		WHILE (s <= 300) & runOk DO seed := s; RunSeed(200); INC(s) END
	END SeedsBody;

	PROCEDURE TestSeeds*;
	BEGIN
		runOk := TRUE;
		UnitAssert.True(Drive(SeedsBody, 20000), "run");
		UnitAssert.True(runOk, runMsg)
	END TestSeeds;

END Actor_devBounded.
