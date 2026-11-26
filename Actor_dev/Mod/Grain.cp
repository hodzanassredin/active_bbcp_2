MODULE Actor_devGrain;
(** Тесты виртуальных акторов: активация по первому письму, пассивация и
	подъём из журнала, переезд ключа при потере узла, повтор операции,
	два воплощения при разделении сети — пишет одно, прогоны со сбоями от
	зерна. Узлы A, B, C на сети в памяти, ящики обслуживает ActorSim,
	журнал общий. *)

	IMPORT Kernel, Services, Tasks, Strings, Stores, Log := StdLog,
		ActorCore, ActorRun, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorSim, ActorGrain,
		UnitAssert;

	CONST
		maxOps = 64;
		nodes = 3;
		benchN = 2000; benchEv = 20;

	TYPE
		Msg = ActorCore.Msg;
		Name = ActorCore.Name;

		Deposit = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		Reply = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; balance: INTEGER END;
		Send = POINTER TO RECORD (Msg) to: ActorCore.Ref; m: ActorNet.Msg END;

		Deposited = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER END;

		Account = POINTER TO RECORD (ActorGrain.Actor)
			balance: INTEGER; ops: ARRAY maxOps OF LONGINT; n: INTEGER
		END;

		Client = POINTER TO RECORD (ActorCore.Actor) END;

		Sum = POINTER TO RECORD (ActorPersist.Visitor) total, n: INTEGER END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		net: ActorNetMem.Net;
		nd: ARRAY nodes OF ActorNet.Node;
		cl: ARRAY nodes OF ActorGrain.Cluster;
		client: ActorCore.Ref; key: Name;
		benchT: ARRAY 6 OF LONGINT;
		j: ActorJournal.Journal;
		replies, lastBalance, executed, restores: INTEGER; lastOp: LONGINT;
		gen: INTEGER; driverDone: BOOLEAN;
		seed: LONGINT; runOk: BOOLEAN; runMsg: ARRAY 256 OF CHAR;
		acked: ARRAY 32 OF BOOLEAN;
		traceSteps, traceDelivered: LONGINT; traceBal: INTEGER;

	(* --- обвязка ----------------------------------------------------------- *)

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

	PROCEDURE Settle;
	(* Пост: ни ящиков с работой, ни кадров в пути. *)
	BEGIN
		REPEAT IF ActorSim.Drain(100000) THEN END UNTIL ~ActorNetMem.Pump(net) & ActorSim.Idle()
	END Settle;

	PROCEDURE NodeName (i: INTEGER; OUT s: Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		s := "Ag"; s[0] := CHR(ORD("A") + i)	(* имена постоянны: размещение — функция зерна *)
	END NodeName;

	(* --- письма и события ---------------------------------------------------- *)

	PROCEDURE (m: Deposit) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Deposit) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.balance)
	END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.balance)
	END Read;

	PROCEDURE (e: Deposited) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum)
	END Externalize;

	PROCEDURE (e: Deposited) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum)
	END Internalize;

	PROCEDURE (s: Sum) Visit (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(s.total, e.sum); INC(s.n) ELSE END
	END Visit;

	PROCEDURE NewDeposit (op: LONGINT; sum: INTEGER): Deposit;
		VAR d: Deposit;
	BEGIN
		NEW(d); d.op := op; d.sum := sum; RETURN d
	END NewDeposit;

	(* --- счёт — виртуальный актор -------------------------------------------- *)

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(a.balance, e.sum); a.ops[a.n MOD maxOps] := e.op; INC(a.n) ELSE END
	END Apply;

	PROCEDURE (a: Account) Seen (op: LONGINT): BOOLEAN, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < MIN(a.n, maxOps)) & (a.ops[i] # op) DO INC(i) END;
		RETURN i < MIN(a.n, maxOps)
	END Seen;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR e: Deposited; r: Reply;
	BEGIN
		WITH m: Deposit DO
			IF (m.op # 0) & ~a.Seen(m.op) THEN	(* op = 0 — только узнать остаток *)
				NEW(e); e.op := m.op; e.sum := m.sum; a.Persist(c, e, m.op); INC(executed)
			END;
			NEW(r); r.op := m.op; r.balance := a.balance; c.Respond(r)
		ELSE
		END
	END Handle;

	PROCEDURE (a: Account) Snapshot (): Stores.Store;
	BEGIN
		RETURN a
	END Snapshot;

	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
		VAR i: INTEGER;
	BEGIN
		wr.WriteVersion(0); wr.WriteInt(a.balance); wr.WriteInt(a.n);
		FOR i := 0 TO maxOps - 1 DO wr.WriteLong(a.ops[i]) END
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v, i: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(a.balance); rd.ReadInt(a.n);
		FOR i := 0 TO maxOps - 1 DO rd.ReadLong(a.ops[i]) END
	END Internalize;

	PROCEDURE (a: Account) Restore (s: Stores.Store);
		VAR i: INTEGER;
	BEGIN
		WITH s: Account DO
			a.balance := s.balance; a.n := s.n; FOR i := 0 TO maxOps - 1 DO a.ops[i] := s.ops[i] END;
			INC(restores)
		ELSE
		END
	END Restore;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); RETURN a
	END NewAccount;

	PROCEDURE (a: Client) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Send DO c.Tell(m.to, m.m)
		| m: Reply DO
			INC(replies); lastBalance := m.balance; lastOp := m.op;
			IF (m.op > 0) & (m.op < LEN(acked)) THEN acked[m.op] := TRUE END
		ELSE
		END
	END Receive;

	PROCEDURE NewClient (): ActorCore.Actor;
		VAR a: Client;
	BEGIN
		NEW(a); RETURN a
	END NewClient;

	(* --- кластер ---------------------------------------------------------------- *)

	PROCEDURE Cluster (k: INTEGER; withMembers: BOOLEAN);
	(* Пост: k узлов на новой сети, у каждого кластер с видом "acct"; при
		withMembers все знают всех; клиент — на узле 0 (имя "<A>/c" вне хозяина). *)
		VAR i, q: INTEGER; s: Name; cap: ARRAY 256 OF CHAR;
	BEGIN
		FOR i := 0 TO nodes - 1 DO	(* узлы прошлого прогона — вниз, имена свободны *)
			IF (nd[i] # NIL) & nd[i].up THEN ActorNet.Down(nd[i]) END
		END;
		IF ~ActorSim.Idle() THEN IF ActorSim.Drain(100000) THEN END END;
		ActorSim.Install; net := ActorNetMem.New(seed); j := ActorJournal.NewMem();
		FOR i := 0 TO k - 1 DO
			NodeName(i, s); nd[i] := ActorNet.New(s, net, 1000 + i); ActorNetMem.Add(net, nd[i]);
			cl[i] := ActorGrain.New(nd[i], j); ActorGrain.Register(cl[i], "acct", NewAccount)
		END;
		IF withMembers THEN
			FOR i := 0 TO k - 1 DO
				ActorGrain.HostCap(cl[i], cap);
				FOR q := 0 TO k - 1 DO IF q # i THEN ActorGrain.Member(cl[q], cap) END END
			END
		END;
		client := ActorCore.Spawn(ActorCore.dir.Lookup(ActorCore.node, nd[0].name)(ActorCore.Context),
			ActorCore.NewProps(NewClient), "c")
	END Cluster;

	PROCEDURE Ask (to: ActorCore.Ref; op: LONGINT; sum: INTEGER);
	(* Клиент шлёт Deposit адресу to. *)
		VAR s: Send;
	BEGIN
		NEW(s); s.to := to; s.m := NewDeposit(op, sum); ActorCore.Tell(client, s)
	END Ask;

	PROCEDURE Journal (IN key: ARRAY OF CHAR; OUT sum: Sum);
		VAR last: LONGINT; res: INTEGER;
	BEGIN
		NEW(sum); ActorPersist.Read(j, "acct." + key, 1, sum, last, res)
	END Journal;

	PROCEDURE OwnerIs (IN key: ARRAY OF CHAR; i: INTEGER): BOOLEAN;
		VAR o: Name;
	BEGIN
		ActorGrain.Owner(cl[0], "acct", key, o); RETURN o = nd[i].name
	END OwnerIs;

	PROCEDURE KeyOwnedBy (i: INTEGER; OUT key: Name);
	(* Пост: ключ, которым по мнению cl[0] владеет узел i. *)
		VAR k: INTEGER; d: ARRAY 16 OF CHAR;
	BEGIN
		k := 0;
		REPEAT INC(k); Strings.IntToString(k, d); key := d$ UNTIL OwnerIs(key, i)
	END KeyOwnedBy;

	PROCEDURE Setup*;
	BEGIN
		replies := 0; lastBalance := 0; executed := 0; restores := 0; lastOp := -1; seed := 1
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown
	END Teardown;

	(* --- тесты ---------------------------------------------------------------------- *)

	PROCEDURE FirstBody;
		VAR r: ActorCore.Ref;
	BEGIN
		Cluster(3, TRUE); Settle;
		KeyOwnedBy(1, key); r := ActorGrain.Ref(cl[0], "acct", key);
		Ask(r, 1, 100); Settle; Ask(r, 2, 50); Settle
	END FirstBody;

	PROCEDURE TestActivateOnFirstMessage*;
	(* Письмо ключу с узла A поднимает воплощение на владельце B, ответ идёт клиенту. *)
		VAR s: Sum;
	BEGIN
		UnitAssert.True(Drive(FirstBody, 5000), "run");
		UnitAssert.EqualInt(2, replies, "two replies");
		UnitAssert.EqualInt(150, lastBalance, "balance");
		UnitAssert.EqualInt(1, cl[1].activations, "activated on owner B");
		UnitAssert.EqualInt(0, cl[0].activations + cl[2].activations, "not elsewhere");
		Journal(key, s); UnitAssert.EqualInt(2, s.n, "two events in stream acct.key")
	END TestActivateOnFirstMessage;

	PROCEDURE PassiveBody;
		VAR r: ActorCore.Ref; i: INTEGER;
	BEGIN
		Cluster(2, TRUE); Settle;
		FOR i := 0 TO 1 DO cl[i].idle := 2 END;
		KeyOwnedBy(1, key); r := ActorGrain.Ref(cl[0], "acct", key);
		Ask(r, 1, 100); Settle;
		ActorGrain.Tick(cl[1]); Settle; ActorGrain.Tick(cl[1]); Settle;
		Ask(r, 2, 10); Settle
	END PassiveBody;

	PROCEDURE TestPassivateAndReactivate*;
	(* После idle тиков воплощение сохраняет снимок и уходит; следующее письмо
		поднимает его из снимка с прежним состоянием. *)
	BEGIN
		UnitAssert.True(Drive(PassiveBody, 5000), "run");
		UnitAssert.EqualInt(1, cl[1].passivations, "passivated");
		UnitAssert.EqualInt(2, cl[1].activations, "activated again");
		UnitAssert.EqualInt(1, restores, "restored from snapshot");
		UnitAssert.EqualInt(110, lastBalance, "state kept")
	END TestPassivateAndReactivate;

	PROCEDURE MoveBody;
		VAR r: ActorCore.Ref;
	BEGIN
		Cluster(3, TRUE); Settle;
		KeyOwnedBy(1, key); r := ActorGrain.Ref(cl[0], "acct", key);
		Ask(r, 1, 100); Settle;
		ActorNetMem.Crash(net, nd[1].name); Settle;
		Ask(r, 2, 5); Settle
	END MoveBody;

	PROCEDURE TestMoveOnNodeLoss*;
	(* Узел-владелец пропал: ключ поднимается на другом живом узле с тем же состоянием. *)
	BEGIN
		UnitAssert.True(Drive(MoveBody, 5000), "run");
		UnitAssert.True(~ActorGrain.Alive(cl[0], nd[1].name), "B dead for A");
		UnitAssert.True(~OwnerIs(key, 1), "owner moved");
		UnitAssert.EqualInt(1, cl[0].activations + cl[2].activations, "activated on the new owner");
		UnitAssert.EqualInt(105, lastBalance, "state from the shared journal")
	END TestMoveOnNodeLoss;

	PROCEDURE RetryBody;
		VAR r: ActorCore.Ref;
	BEGIN
		Cluster(2, TRUE); Settle;
		KeyOwnedBy(1, key); r := ActorGrain.Ref(cl[0], "acct", key);
		Ask(r, 7, 100); Settle; Ask(r, 7, 100); Settle
	END RetryBody;

	PROCEDURE TestRetryIdempotent*;
	(* Повтор операции с тем же номером не исполняется второй раз. *)
		VAR s: Sum;
	BEGIN
		UnitAssert.True(Drive(RetryBody, 5000), "run");
		UnitAssert.EqualInt(2, replies, "both answered");
		UnitAssert.EqualInt(100, lastBalance, "once");
		Journal(key, s); UnitAssert.EqualInt(1, s.n, "one event")
	END TestRetryIdempotent;

	PROCEDURE SplitBody;
		VAR ra, rb: ActorCore.Ref; s: Send;
	BEGIN
		Cluster(2, FALSE); Settle;	(* членов нет: каждый узел считает владельцем себя *)
		key := "77";
		ra := ActorGrain.Ref(cl[0], "acct", key); rb := ActorGrain.Ref(cl[1], "acct", key);
		Ask(ra, 0, 0); Ask(rb, 0, 0); Settle;	(* оба воплощения подняты *)
		Ask(ra, 1, 100); Settle;
		Ask(rb, 2, 10); Settle;	(* воплощение на B не видело события A: conflict *)
		Ask(rb, 2, 10); Settle	(* повтор поднимает B заново, уже с событием A *)
	END SplitBody;

	PROCEDURE TestSplitBrainOneWriter*;
	(* Разделение: оба узла подняли ключ. Перед письмом воплощение сверяет
		seq с последним номером потока (Last) и догоняет первое событие, так
		что два воплощения не пишут мимо друг друга: события идут по
		порядку, сумма общая. *)
		VAR s: Sum;
	BEGIN
		UnitAssert.True(Drive(SplitBody, 5000), "run");
		UnitAssert.EqualInt(1, cl[0].activations, "A activated");
		UnitAssert.EqualInt(1, cl[1].activations, "B caught up before writing: no second activation");
		Journal(key, s);
		UnitAssert.EqualInt(2, s.n, "two events, none lost or doubled");
		UnitAssert.EqualInt(110, s.total, "sum");
		UnitAssert.EqualInt(110, lastBalance, "B sees A's event")
	END TestSplitBrainOneWriter;

	PROCEDURE TestLetterDuringPassivation*;
	(* Пул исполнителей: хозяин обрабатывает тик (просит пассивацию) и сразу
		следом конверт тому же ключу, пока воплощение ещё не остановилось.
		Письмо не теряется: хозяин держит его и отдаёт новому воплощению. *)
		VAR s: Sum; r: ActorCore.Ref; c: ActorGrain.Cluster; n: ActorNet.Node; t: LONGINT;
	BEGIN
		ActorRun.Start(1); net := ActorNetMem.NewAuto(); j := ActorJournal.NewMem();
		n := ActorNet.New("P", net, 7); ActorNetMem.Add(net, n);
		c := ActorGrain.New(n, j); ActorGrain.Register(c, "acct", NewAccount); c.idle := 1;
		key := "5"; r := ActorGrain.Ref(c, "acct", key);
		ActorCore.Tell(r, NewDeposit(1, 100));
		t := Services.Ticks(); WHILE (c.activations < 1) & (Services.Ticks() - t < 2000) DO Services.actionHook.Step END;
		Journal(key, s); t := Services.Ticks();
		WHILE (s.n < 1) & (Services.Ticks() - t < 2000) DO Services.actionHook.Step; Journal(key, s) END;
		ActorGrain.Tick(c); ActorCore.Tell(r, NewDeposit(2, 10));	(* оба письма — хозяину подряд *)
		t := Services.Ticks();
		WHILE (s.n < 2) & (Services.Ticks() - t < 2000) DO Services.actionHook.Step; Journal(key, s) END;
		UnitAssert.EqualInt(2, s.n, "second deposit executed");
		UnitAssert.EqualInt(1, c.passivations, "passivated once");
		UnitAssert.EqualInt(2, c.activations, "activated again after passivation");
		ActorNet.Down(n)
	END TestLetterDuringPassivation;

	(* --- прогоны от зерна --------------------------------------------------------- *)

	PROCEDURE Fail (IN what: ARRAY OF CHAR);
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		IF runOk THEN runOk := FALSE; Strings.IntToString(seed, d); runMsg := "seed " + d$ + ": " + what$ END
	END Fail;

	PROCEDURE SeedBody;
	(* 3 узла, 5 ключей, 20 операций по 1; сеть теряет и дублирует кадры, узлы
		падают и поднимаются; клиент повторяет неподтверждённые операции. *)
		CONST ops = 20; keys = 5;
		VAR i, round, down, upAt, q: INTEGER; refs: ARRAY keys OF ActorCore.Ref; k: ARRAY 4 OF CHAR;
			s: Sum; total: INTEGER; all: BOOLEAN; cap: ARRAY 256 OF CHAR; hostOk: BOOLEAN;
	BEGIN
		Cluster(3, TRUE); Settle;
		net.plan.loss := 100; net.plan.dup := 100; net.plan.delay := 2; net.plan.reorder := TRUE;
		FOR i := 0 TO keys - 1 DO k := "k0"; k[1] := CHR(ORD("0") + i); refs[i] := ActorGrain.Ref(cl[0], "acct", k) END;
		FOR i := 1 TO ops DO acked[i] := FALSE END;
		down := -1; upAt := 0; round := 0;
		REPEAT
			INC(round);
			IF round MOD 12 = 1 THEN	(* неподтверждённые — заново *)
				FOR i := 1 TO ops DO IF ~acked[i] THEN Ask(refs[i MOD keys], i, 1) END END
			END;
			IF (round < 600) & (down < 0) & (ActorNetMem.Rand(net, 1000) < 10) THEN
				down := 1 + ActorNetMem.Rand(net, 2); upAt := round + 10 + ActorNetMem.Rand(net, 30);
				ActorNetMem.Crash(net, nd[down].name)
			ELSIF (down > 0) & (round >= upAt) THEN	(* подъём: новый хозяин, все снова знают всех *)
				IF ActorSim.Drain(10000) THEN END;
				ActorNet.Up(nd[down]);
				cl[down] := ActorGrain.New(nd[down], j); ActorGrain.Register(cl[down], "acct", NewAccount);
				FOR q := 0 TO 2 DO
					IF q # down THEN
						ActorGrain.HostCap(cl[q], cap); ActorGrain.Member(cl[down], cap);
						ActorGrain.Rejoin(cl[q], nd[down].name)
					END
				END;
				down := -1
			END;
			IF ActorSim.Step() THEN END; IF ActorSim.Step() THEN END; IF ActorNetMem.Pump(net) THEN END;
			all := TRUE; FOR i := 1 TO ops DO all := all & acked[i] END
		UNTIL all & (down < 0) OR (round > 20000);
		IF ~all THEN Fail("operations not acknowledged") END;
		total := 0;
		FOR i := 0 TO keys - 1 DO
			k := "k0"; k[1] := CHR(ORD("0") + i); Journal(k, s); INC(total, s.total);
			IF s.n # ops DIV keys THEN Fail("operation executed twice or lost") END
		END;
		IF total # ops THEN Fail("sum") END;
		traceSteps := ActorSim.steps; traceDelivered := net.delivered; traceBal := total;
		FOR i := 0 TO 2 DO ActorNet.Down(nd[i]) END; IF ActorSim.Drain(100000) THEN END
	END SeedBody;

	PROCEDURE RunSeed (sd: LONGINT; OUT steps, delivered: LONGINT): BOOLEAN;
	BEGIN
		seed := sd; runOk := TRUE; runMsg := "";
		IF ~Drive(SeedBody, 20000) THEN Fail("run hung") END;
		steps := traceSteps; delivered := traceDelivered;
		RETURN runOk
	END RunSeed;

	PROCEDURE Seeds (from, to: LONGINT; again: BOOLEAN): BOOLEAN;
		VAR sd, s1, d1, s2, d2: LONGINT;
	BEGIN
		sd := from;
		WHILE sd <= to DO
			IF ~RunSeed(sd, s1, d1) THEN RETURN FALSE END;
			IF again THEN
				IF ~RunSeed(sd, s2, d2) THEN RETURN FALSE END;
				IF (s1 # s2) OR (d1 # d2) THEN Fail("rerun differs"); RETURN FALSE END
			END;
			INC(sd)
		END;
		RETURN TRUE
	END Seeds;

	PROCEDURE TestSeeds*;
	(* 100 зёрен со сбоями сети и падениями узлов: каждая операция исполнена
		ровно один раз; повтор зерна даёт тот же прогон. *)
	BEGIN
		UnitAssert.True(Seeds(1, 100, TRUE), runMsg)
	END TestSeeds;

	(* --- замер ---------------------------------------------------------------------- *)

	PROCEDURE BenchKey (prefix: CHAR; i: INTEGER; OUT k: Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(i, d); k := "x" + d$; k[0] := prefix
	END BenchKey;

	PROCEDURE SendAll (prefix: CHAR; op: LONGINT);
		VAR i: INTEGER; k: Name;
	BEGIN
		FOR i := 0 TO benchN - 1 DO BenchKey(prefix, i, k); Ask(ActorGrain.Ref(cl[0], "acct", k), op, 1) END;
		Settle
	END SendAll;

	PROCEDURE BenchBody;
	(* Один узел (без сети): цена активации пустого ключа, обращения к
		активному, пассивации, подъёма из снимка и из журнала без снимка. *)
		VAR t: LONGINT; e: INTEGER;
	BEGIN
		Cluster(1, FALSE); Settle; cl[0].idle := 1;
		t := Kernel.Time(); SendAll("s", 1); benchT[0] := Kernel.Time() - t;	(* активация пустого *)
		t := Kernel.Time(); SendAll("s", 0); benchT[1] := Kernel.Time() - t;	(* к активному *)
		FOR e := 2 TO benchEv DO SendAll("s", e) END;
		t := Kernel.Time(); ActorGrain.Tick(cl[0]); Settle; benchT[2] := Kernel.Time() - t;	(* пассивация со снимком *)
		t := Kernel.Time(); SendAll("s", 0); benchT[3] := Kernel.Time() - t;	(* подъём из снимка *)
		FOR e := 1 TO benchEv DO SendAll("n", e) END;
		ActorNet.Down(nd[0]); Settle; ActorNet.Up(nd[0]);	(* остановка без снимка *)
		cl[0] := ActorGrain.New(nd[0], j); ActorGrain.Register(cl[0], "acct", NewAccount);
		client := ActorCore.Spawn(ActorCore.dir.Lookup(ActorCore.node, nd[0].name)(ActorCore.Context),
			ActorCore.NewProps(NewClient), "c");
		Settle;
		t := Kernel.Time(); SendAll("n", 0); benchT[4] := Kernel.Time() - t	(* подъём из 20 событий *)
	END BenchBody;

	PROCEDURE Line (IN what: ARRAY OF CHAR; ms: LONGINT);
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(ms * 1000 DIV benchN, d);
		Log.String("BENCH grain-" + what + " " + d$ + " us/op"); Log.Ln
	END Line;

	PROCEDURE Bench*;
	(** Строки BENCH: активация, обращение к активному, пассивация, подъём из
		снимка и из журнала без снимка (2000 ключей, журнал в памяти). *)
	BEGIN
		Setup;
		IF Drive(BenchBody, 600000) THEN
			Line("activate-empty", benchT[0]); Line("call-active", benchT[1]);
			Line("passivate", benchT[2]); Line("activate-snapshot", benchT[3]);
			Line("activate-20-events", benchT[4])
		ELSE Log.String("bench hung"); Log.Ln
		END
	END Bench;

	PROCEDURE Long*;
	(** Долгий прогон: 2000 зёрен. *)
	BEGIN
		Setup;
		IF Seeds(1, 2000, FALSE) THEN Log.String("Actor_devGrain.Long ok") ELSE Log.String(runMsg) END;
		Log.Ln
	END Long;

END Actor_devGrain.
