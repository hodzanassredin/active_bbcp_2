MODULE Raft_devJournal;
(** Тесты журнала стойких акторов поверх Raft (RaftJournal).

	Три узла RaftNode со своими часами на сети ActorNetMem без сбоев
	(доставляет главный цикл), исполнитель — пул ActorRun, у клиента журнала
	свои часы: сценарии в реальном времени. Прогоны по зёрнам под ActorSim —
	Raft_devPersist. Стойкий актор — счётчик (ActorPersist) на отдельном
	узле P; журнал — RaftJournal.Client.

	Проверяется: запись идёт в журнал Raft и после фиксации есть у
	большинства; новое воплощение счётчика восстанавливается из журнала
	Raft; гибель лидера не теряет подтверждённого и не останавливает
	запись; лидер, отрезанный от большинства, не подтверждает запись
	(мутация RaftJournal.mutNoWait ловится). *)

	IMPORT Services, Tasks, Strings, Stores, AsyncIO,
		ActorCore, ActorRun, ActorJournal, ActorPersist, ActorNet, ActorNetMem,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, RaftJournal, UnitAssert;

	CONST
		n = 3;
		kP = n;	(* номер узла P в таблице разрывов *)

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Value = POINTER TO RECORD (ActorCore.Msg) v: INTEGER END;

		Added = POINTER TO RECORD (ActorPersist.Event) END;

		Counter = POINTER TO RECORD (ActorPersist.Actor) v: INTEGER END;

		Cut = POINTER TO RECORD (ActorNet.Transport) END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		net: ActorNetMem.Net;
		nodes: ARRAY n + 1 OF ActorNet.Node;
		cfgs: ARRAY n OF RaftNode.Config;
		disks: ARRAY n OF RaftStorage.Storage;
		caps: RaftNode.Caps;
		blocked: ARRAY n + 1, n + 1 OF BOOLEAN;
		journal: RaftJournal.Client;
		counter: ActorCore.Ref;
		gen, clientId, got, askRes, majority: INTEGER;
		driverDone: BOOLEAN;

	(* --- обвязка ----------------------------------------------------------- *)

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~flag & (Services.Ticks() - t <= ms) DO Services.actionHook.Step; AsyncIO.Poll(0) END;
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

	PROCEDURE Idle (ms: LONGINT);
		VAR never: BOOLEAN;
	BEGIN
		never := FALSE; IF Until(never, ms) THEN END
	END Idle;

	PROCEDURE Index (IN name: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < n) & (nodes[i].name # name) DO INC(i) END;
		RETURN i
	END Index;

	PROCEDURE (t: Cut) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		IF ~blocked[Index(from), Index(to)] THEN net.Send(from, to, frame, len) END
	END Send;

	(* --- счётчик ------------------------------------------------------------- *)

	PROCEDURE (e: Added) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0)
	END Externalize;

	PROCEDURE (e: Added) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v)
	END Internalize;

	PROCEDURE (a: Counter) Apply (e: ActorPersist.Event);
	BEGIN
		INC(a.v)
	END Apply;

	PROCEDURE (a: Counter) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Added; r: Value;
	BEGIN
		IF m IS Inc THEN NEW(e); a.Persist(c, e, 0)	(* ответ — в Persisted, после фиксации *)
		ELSIF m IS Get THEN NEW(r); r.v := a.v; c.Respond(r)
		END
	END Handle;

	PROCEDURE (a: Counter) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
		VAR r: Value;
	BEGIN
		NEW(r); r.v := a.v; c.Respond(r)
	END Persisted;

	PROCEDURE NewCounter (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); a.journal := journal; a.stream := "counter"; RETURN a
	END NewCounter;

	(* --- кластер --------------------------------------------------------------- *)

	PROCEDURE Name (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d);
		IF i = kP THEN s := "P" + d$ ELSE s := "J" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i) END
	END Name;

	PROCEDURE StartCounter;
	(* новое воплощение счётчика на узле P с новым клиентом журнала *)
	BEGIN
		INC(clientId); journal := RaftJournal.New(nodes[kP], caps, n, clientId);
		journal.timeoutMs := 300; journal.giveUpMs := 15000;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(NewCounter), nodes[kP].name)
	END StartCounter;

	PROCEDURE Cluster;
		VAR i, j: INTEGER; s: ActorCore.Name; cut: Cut; r: ActorCore.Ref;
	BEGIN
		INC(gen); ActorRun.Start(2); net := ActorNetMem.NewAuto(); NEW(cut);
		FOR i := 0 TO n DO FOR j := 0 TO n DO blocked[i, j] := FALSE END END;
		FOR i := 0 TO n DO Name(i, s); nodes[i] := ActorNet.New(s, cut, 500 + i); ActorNetMem.Add(net, nodes[i]) END;
		FOR i := 0 TO n - 1 DO
			disks[i] := RaftStorage.NewMem();
			cfgs[i] := RaftNode.NewConfig(i, n, nodes[i], disks[i], RaftJournal.NewMachine);
			cfgs[i].tickMs := 5; cfgs[i].seed := gen;
			r := RaftNode.Start(cfgs[i]); ActorNet.Export(nodes[i], r, caps[i])
		END;
		FOR i := 0 TO n - 1 DO FOR j := 0 TO n - 1 DO cfgs[j].peers[i] := caps[i]$ END END
	END Cluster;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER; a: RaftNode.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			a := cfgs[i].actor;
			IF (a # NIL) & (a.nd # NIL) & ~a.broken & nodes[i].up & (a.nd.role = Core.leader) THEN RETURN i END
		END;
		RETURN -1
	END Leader;

	PROCEDURE WaitLeader (): INTEGER;
		VAR t: LONGINT; l: INTEGER;
	BEGIN
		t := Services.Ticks(); l := Leader();
		WHILE (l < 0) & (Services.Ticks() - t < 5000) DO Idle(5); l := Leader() END;
		RETURN l
	END WaitLeader;

	PROCEDURE Stored (i: INTEGER): BOOLEAN;
	(* хранилище узла i содержит последнюю команду клиента журнала *)
		VAR t, v, si, st, last, res, k, c, q: INTEGER; snap: Core.Bytes; log: Core.Entries;
	BEGIN
		disks[i].Load(t, v, si, st, snap, log, last, res);
		FOR k := 1 TO last - si DO
			RaftSm.Decode(log[k].data, c, q);
			IF (c = journal.id) & (q = journal.seq) THEN RETURN TRUE END
		END;
		RETURN FALSE
	END Stored;

	PROCEDURE Majority (): INTEGER;
		VAR i, k: INTEGER;
	BEGIN
		k := 0; FOR i := 0 TO n - 1 DO IF Stored(i) THEN INC(k) END END;
		RETURN k
	END Majority;

	PROCEDURE Applied (i: INTEGER): LONGINT;
	(* событий потока в копии журнала узла i; -1 — узел не поднят *)
		VAR a: RaftNode.Node;
	BEGIN
		a := cfgs[i].actor;
		IF (a = NIL) OR (a.sm = NIL) OR a.broken OR ~nodes[i].up THEN RETURN -1 END;
		RETURN a.sm.sm(RaftJournal.Machine).mem.Last("counter")
	END Applied;

	PROCEDURE Teardown*;
	BEGIN
		RaftJournal.mutations := {};
		ActorRun.Shutdown; Idle(5)
	END Teardown;

	(* --- тела задач ----------------------------------------------------------- *)

	PROCEDURE IncBody;
		VAR reply: ActorCore.Msg; m: Inc;
	BEGIN
		NEW(m); askRes := ActorCore.Ask(counter, m, 30000, reply);
		IF (askRes = ActorCore.ok) & (reply IS Value) THEN got := reply(Value).v ELSE got := -1 END;
		majority := Majority()
	END IncBody;

	PROCEDURE GetBody;
		VAR reply: ActorCore.Msg; m: Get;
	BEGIN
		NEW(m); askRes := ActorCore.Ask(counter, m, 30000, reply);
		IF (askRes = ActorCore.ok) & (reply IS Value) THEN got := reply(Value).v ELSE got := -1 END
	END GetBody;

	PROCEDURE Inc1 (want: INTEGER; IN what: ARRAY OF CHAR);
	BEGIN
		UnitAssert.True(Drive(IncBody, 40000), "Inc завис: " + what$);
		UnitAssert.EqualInt(want, got, what);
		UnitAssert.True(majority >= n DIV 2 + 1, "событие у большинства, когда Append вернулся: " + what$)
	END Inc1;

	PROCEDURE WaitApplied (i: INTEGER; want: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE (Applied(i) # want) & (Services.Ticks() - t < 5000) DO Idle(5) END;
		RETURN Applied(i) = want
	END WaitApplied;

	PROCEDURE Restart (i: INTEGER);
		VAR t: LONGINT; r: ActorCore.Ref;
	BEGIN
		t := Services.Ticks();
		WHILE (ActorCore.dir.Lookup(ActorCore.node, nodes[i].name) # NIL) & (Services.Ticks() - t < 5000) DO Idle(5) END;
		ActorNet.Up(nodes[i]); r := RaftNode.Start(cfgs[i])
	END Restart;

	(* --- тесты ------------------------------------------------------------------ *)

	PROCEDURE TestPersistAndRecover*;
	(* события счётчика — в журнале Raft на всех узлах; новое воплощение
		счётчика с новым клиентом журнала восстанавливается из него *)
		VAR i, k: INTEGER;
	BEGIN
		Cluster; UnitAssert.True(WaitLeader() >= 0, "лидер выбран");
		StartCounter;
		FOR k := 1 TO 5 DO Inc1(k, "приращение") END;
		FOR i := 0 TO n - 1 DO UnitAssert.True(WaitApplied(i, 5), "копия журнала на узле") END;
		ActorCore.StopActor(counter); Idle(20);
		StartCounter;
		UnitAssert.True(Drive(GetBody, 40000), "Get завис");
		UnitAssert.EqualInt(5, got, "восстановлено из журнала Raft");
		Inc1(6, "после восстановления")
	END TestPersistAndRecover;

	PROCEDURE TestLeaderCrash*;
	(* гибель лидера: подтверждённое не теряется, запись продолжается у
		нового лидера, поднятый узел догоняет журнал *)
		VAR l, i, k: INTEGER;
	BEGIN
		Cluster; l := WaitLeader(); UnitAssert.True(l >= 0, "лидер выбран");
		StartCounter;
		FOR k := 1 TO 3 DO Inc1(k, "до падения") END;
		ActorNetMem.Crash(net, nodes[l].name);
		FOR k := 4 TO 6 DO Inc1(k, "после падения лидера") END;
		UnitAssert.True(Leader() # l, "лидер новый");
		Restart(l);
		UnitAssert.True(WaitApplied(l, 6), "поднятый узел догнал журнал");
		FOR i := 0 TO n - 1 DO UnitAssert.True(WaitApplied(i, 6), "копии совпадают") END
	END TestLeaderCrash;

	PROCEDURE CutOff (l: INTEGER);
	(* лидер l отрезан от остальных узлов Raft; узел P видит всех *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF i # l THEN blocked[i, l] := TRUE; blocked[l, i] := TRUE END END
	END CutOff;

	PROCEDURE CutLeaderRun (mut: SET; OUT gotOk: BOOLEAN; OUT maj: INTEGER);
		VAR l: INTEGER;
	BEGIN
		Cluster; l := WaitLeader(); UnitAssert.True(l >= 0, "лидер выбран");
		StartCounter;
		Inc1(1, "до разрыва");	(* клиент знает лидера *)
		CutOff(l); RaftJournal.mutations := mut;
		UnitAssert.True(Drive(IncBody, 40000), "Inc завис");
		RaftJournal.mutations := {};
		gotOk := got = 2; maj := majority
	END CutLeaderRun;

	PROCEDURE TestCutLeaderDoesNotConfirm*;
	(* лидер за разрывом не подтверждает: Append ждёт, пока запись не
		зафиксирует новый лидер большинства, и только тогда возвращается *)
		VAR ok: BOOLEAN; maj: INTEGER;
	BEGIN
		CutLeaderRun({}, ok, maj);
		UnitAssert.True(ok, "запись прошла через нового лидера");
		UnitAssert.True(journal.retries > 0, "клиент ушёл от отрезанного лидера");
		UnitAssert.True(maj >= 2, "когда Append вернулся, запись у большинства")
	END TestCutLeaderDoesNotConfirm;

	PROCEDURE TestMutNoWait*;
	(* мутация: Append отвечает «записано», не дождавшись фиксации — запись
		есть только у отрезанного лидера *)
		VAR ok: BOOLEAN; maj: INTEGER;
	BEGIN
		CutLeaderRun({RaftJournal.mutNoWait}, ok, maj);
		UnitAssert.True(ok & (maj < 2), "мутация mutNoWait поймана: подтверждено без большинства")
	END TestMutNoWait;

END Raft_devJournal.
