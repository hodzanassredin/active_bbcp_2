MODULE Actor_devNet;
(** Тесты акторов между узлами: Ask и тайм-аут через узел, адрес-пропуск,
	Watch удалённого актора, потеря узла, дубли сети, повтор команды после
	потерянного ответа, сага перевода между узлами на сети со сбоями от
	зерна.

	Сага: счета на узлах A и B, координатор на узле C — персистентные
	акторы; журнал каждого узла в памяти переживает падение узла, как
	диск. Шаг перевода — событие координатора; текущий шаг повторяется по
	тику; счёт отвечает на повтор операции тем же итогом. Прогон саги —
	функция зерна: провал печатает зерно, Rerun повторяет последнее. *)

	IMPORT Kernel, Services, Tasks, Strings, Stores, Log := StdLog, CommStreams, AsyncIO,
		ActorCore, ActorRun, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorNetTcp, ActorSim, UnitAssert;

	CONST
		initial = 1000;	(* начальный остаток счетов *)
		limitB = 1500;	(* счёт B не принимает больше: зачисление сверх — отказ *)
		maxT = 8;	(* переводов в прогоне саги *)
		maxOps = 64;
		quickSeeds = 200;	(* зёрен в обычном тесте *)
		longSeeds = 3000;	(* зёрен в долгом прогоне Long *)

		(* состояния перевода *)
		begun = 0; withdrawn = 1; done = 2; refunding = 3; refunded = 4; failed = 5;

	TYPE
		Msg = ActorCore.Msg;
		Name = ActorCore.Name;

		(* письма между узлами *)
		Ping = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		Pong = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		Move = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;	(* sum > 0 — зачисление *)
		Reply = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; ok: BOOLEAN END;

		(* локальные *)
		Tick = POINTER TO RECORD (Msg) END;
		Start = POINTER TO RECORD (Msg) id, sum: INTEGER; reverse: BOOLEAN END;
		Send = POINTER TO RECORD (Msg) to: ActorCore.Ref; m: ActorNet.Msg END;

		(* события *)
		Moved = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER; ok: BOOLEAN END;
		Step = POINTER TO RECORD (ActorPersist.Event) id, sum, state: INTEGER; reverse: BOOLEAN END;

		Echo = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		Root = POINTER TO RECORD (ActorCore.Actor) kind: INTEGER END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance, limit: INTEGER;
			ops: ARRAY maxOps OF LONGINT; oks: ARRAY maxOps OF BOOLEAN; nOps: INTEGER
		END;

		Transfer = RECORD sum, state: INTEGER; reverse: BOOLEAN END;
		Coord = POINTER TO RECORD (ActorPersist.Actor)
			t: ARRAY maxT OF Transfer; n: INTEGER;
			a, b: ActorCore.Ref
		END;

		Sum = POINTER TO RECORD (ActorPersist.Visitor) total: INTEGER END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

		(* транспорт-перехватчик: помнит последний кадр и передаёт его сети *)
		Tap = POINTER TO RECORD (ActorNet.Transport)
			inner: ActorNetMem.Net; last: ActorJournal.Bytes; len: INTEGER
		END;

		Trace = RECORD steps, delivered, lost, dup: LONGINT; a, b, crashes: INTEGER END;

	VAR
		terms, executed, replies, lastReason: INTEGER; lastOk: BOOLEAN;
		driverDone: BOOLEAN;
		askRes, lastPong: INTEGER; askMs: LONGINT;
		rootKind, gen: INTEGER;
		target, watched, coordRef: ActorCore.Ref;
		net: ActorNetMem.Net;
		na, nb, nc: ActorNet.Node;
		ja, jb, jc: ActorJournal.Journal;
		capA, capB: ARRAY 256 OF CHAR;
		states: ARRAY maxT OF INTEGER; nStates: INTEGER;
		seed: LONGINT; trace: Trace; sagaOk: BOOLEAN; sagaMsg: ARRAY 256 OF CHAR;

	(* --- обвязка ---------------------------------------------------------- *)

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~flag & (Services.Ticks() - t <= ms) DO Services.actionHook.Step; AsyncIO.Poll(0) END;
		RETURN flag
	END Until;

	PROCEDURE UntilInt (VAR x: INTEGER; want: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE (x < want) & (Services.Ticks() - t <= ms) DO Services.actionHook.Step; AsyncIO.Poll(0) END;
		RETURN x >= want
	END UntilInt;

	PROCEDURE Idle (ms: LONGINT);
		VAR never: BOOLEAN;
	BEGIN
		never := FALSE; IF Until(never, ms) THEN END
	END Idle;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
	(* Пост: body выполнена в своей задаче за ms мс. *)
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		RETURN Until(driverDone, ms)
	END Drive;

	PROCEDURE NodeName (c: CHAR; OUT s: Name);
	(* Пост: s — имя узла вида "A17" для текущего gen. *)
		VAR d: ARRAY 16 OF CHAR; i: INTEGER;
	BEGIN
		Strings.IntToString(gen, d); s[0] := c;
		i := 0; WHILE d[i] # 0X DO s[i + 1] := d[i]; INC(i) END;
		s[i + 1] := 0X
	END NodeName;

	PROCEDURE Child (n: ActorNet.Node; OUT s: Name);
	(* Пост: s — имя ребёнка "x" корня узла n. *)
	BEGIN
		s := n.name$ + "/x"
	END Child;

	(* --- письма ------------------------------------------------------------ *)

	PROCEDURE (m: Ping) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: Ping) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
	END Read;

	PROCEDURE (m: Pong) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: Pong) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
	END Read;

	PROCEDURE (m: Move) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Move) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteBool(m.ok)
	END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadBool(m.ok)
	END Read;

	PROCEDURE NewPing (n: INTEGER): Ping;
		VAR p: Ping;
	BEGIN
		NEW(p); p.n := n; RETURN p
	END NewPing;

	PROCEDURE NewMove (op: LONGINT; sum: INTEGER): Move;
		VAR m: Move;
	BEGIN
		NEW(m); m.op := op; m.sum := sum; RETURN m
	END NewMove;

	(* --- события ------------------------------------------------------------ *)

	PROCEDURE (e: Moved) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum); wr.WriteBool(e.ok)
	END Externalize;

	PROCEDURE (e: Moved) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum); rd.ReadBool(e.ok)
	END Internalize;

	PROCEDURE (e: Step) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0);
		wr.WriteInt(e.id); wr.WriteInt(e.sum); wr.WriteInt(e.state); wr.WriteBool(e.reverse)
	END Externalize;

	PROCEDURE (e: Step) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.id); rd.ReadInt(e.sum); rd.ReadInt(e.state); rd.ReadBool(e.reverse)
	END Internalize;

	PROCEDURE (s: Sum) Visit (e: ActorPersist.Event);
	BEGIN
		WITH e: Moved DO IF e.ok THEN INC(s.total, e.sum) END ELSE END
	END Visit;

	(* --- эхо, клиент, наблюдатель ----------------------------------------------- *)

	PROCEDURE (a: Echo) Receive (c: ActorCore.Context; m: Msg);
		VAR p: Pong;
	BEGIN
		WITH m: Ping DO
			IF m.n < 0 THEN HALT(77) END;	(* отказ по просьбе теста *)
			INC(executed); NEW(p); p.n := m.n + 1; c.Respond(p)
		ELSE
		END
	END Receive;

	PROCEDURE (a: Client) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Send DO c.Tell(m.to, m.m)
		| m: Reply DO INC(replies); lastOk := m.ok
		ELSE
		END
	END Receive;

	PROCEDURE (w: Watcher) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: ActorCore.Started DO c.Watch(watched)
		| m: ActorCore.Terminated DO IF m.who = watched THEN INC(terms); lastReason := m.reason END
		ELSE
		END
	END Receive;

	(* --- счёт -------------------------------------------------------------------- *)

	PROCEDURE (a: Account) Find (op: LONGINT): INTEGER, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < MIN(a.nOps, maxOps)) & (a.ops[i] # op) DO INC(i) END;
		IF i < MIN(a.nOps, maxOps) THEN RETURN i ELSE RETURN -1 END
	END Find;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Moved DO
			IF e.ok THEN INC(a.balance, e.sum) END;
			a.ops[a.nOps MOD maxOps] := e.op; a.oks[a.nOps MOD maxOps] := e.ok; INC(a.nOps)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR e: Moved; r: Reply; i: INTEGER;
	BEGIN
		WITH m: Move DO
			i := a.Find(m.op);
			IF i < 0 THEN	(* новая операция: итог записан событием, повтор получит тот же *)
				NEW(e); e.op := m.op; e.sum := m.sum;
				e.ok := (a.balance + m.sum >= 0) & (a.balance + m.sum <= a.limit);
				a.Persist(c, e, m.op); INC(executed);
				i := a.Find(m.op)
			END;
			NEW(r); r.op := m.op; r.ok := a.oks[i]; c.Respond(r)
		ELSE
		END
	END Handle;

	PROCEDURE NewAccount (j: ActorJournal.Journal; limit: INTEGER): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := initial; a.limit := limit; a.journal := j; RETURN a
	END NewAccount;

	(* --- координатор саги --------------------------------------------------------- *)

	PROCEDURE (k: Coord) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Step DO
			IF e.id >= k.n THEN k.n := e.id + 1 END;
			k.t[e.id].sum := e.sum; k.t[e.id].state := e.state; k.t[e.id].reverse := e.reverse;
			states[e.id] := e.state; IF k.n > nStates THEN nStates := k.n END
		ELSE
		END
	END Apply;

	PROCEDURE (k: Coord) Command (c: ActorCore.Context; id: INTEGER), NEW;
	(* Послать команду текущего шага перевода id; повтор безопасен. *)
		VAR from, to: ActorCore.Ref; t: Transfer;
	BEGIN
		t := k.t[id];
		IF t.reverse THEN from := k.b; to := k.a ELSE from := k.a; to := k.b END;
		CASE t.state OF
		| begun: c.Tell(from, NewMove(id * 10 + 1, -t.sum))
		| withdrawn: c.Tell(to, NewMove(id * 10 + 2, t.sum))
		| refunding: c.Tell(from, NewMove(id * 10 + 3, t.sum))
		ELSE
		END
	END Command;

	PROCEDURE (k: Coord) Go (c: ActorCore.Context; id, state: INTEGER), NEW;
		VAR e: Step;
	BEGIN
		NEW(e); e.id := id; e.sum := k.t[id].sum; e.reverse := k.t[id].reverse; e.state := state;
		k.Persist(c, e, id);
		k.Command(c, id)
	END Go;

	PROCEDURE (k: Coord) Handle (c: ActorCore.Context; m: Msg);
		VAR i, id, stp: INTEGER; e: Step;
	BEGIN
		WITH m: Start DO
			IF m.id = k.n THEN	(* повтор Start не начинает второй перевод *)
				NEW(e); e.id := m.id; e.sum := m.sum; e.reverse := m.reverse; e.state := begun;
				k.Persist(c, e, m.id); k.Command(c, m.id)
			END
		| m: Tick DO
			FOR i := 0 TO k.n - 1 DO k.Command(c, i) END
		| m: Reply DO
			id := SHORT(m.op DIV 10); stp := SHORT(m.op MOD 10);
			IF (id >= 0) & (id < k.n) THEN
				IF (stp = 1) & (k.t[id].state = begun) THEN
					IF m.ok THEN k.Go(c, id, withdrawn) ELSE k.Go(c, id, failed) END
				ELSIF (stp = 2) & (k.t[id].state = withdrawn) THEN
					IF m.ok THEN k.Go(c, id, done) ELSE k.Go(c, id, refunding) END
				ELSIF (stp = 3) & (k.t[id].state = refunding) & m.ok THEN k.Go(c, id, refunded)
				END
			END
		ELSE
		END
	END Handle;

	(* --- корень узла ---------------------------------------------------------------- *)

	PROCEDURE NewEcho (): ActorCore.Actor;
		VAR a: Echo;
	BEGIN
		NEW(a); RETURN a
	END NewEcho;

	PROCEDURE NewClient (): ActorCore.Actor;
		VAR a: Client;
	BEGIN
		NEW(a); RETURN a
	END NewClient;

	PROCEDURE NewWatcher (): ActorCore.Actor;
		VAR w: Watcher;
	BEGIN
		NEW(w); RETURN w
	END NewWatcher;

	PROCEDURE NewAccountA (): ActorCore.Actor;
	BEGIN
		RETURN NewAccount(ja, MAX(INTEGER))
	END NewAccountA;

	PROCEDURE NewAccountB (): ActorCore.Actor;
	BEGIN
		RETURN NewAccount(jb, limitB)
	END NewAccountB;

	PROCEDURE NewCoord (): ActorCore.Actor;
		VAR k: Coord;
	BEGIN
		NEW(k); k.journal := jc;
		k.a := ActorNet.Import(nc, capA); k.b := ActorNet.Import(nc, capB);
		RETURN k
	END NewCoord;

	(* kind: 0 — счёт A, 1 — счёт B, 2 — координатор, 3 — эхо, 4 — клиент, 5 — наблюдатель *)
	PROCEDURE (r: Root) Receive (c: ActorCore.Context; m: Msg);
		VAR f: ActorCore.Factory;
	BEGIN
		IF m IS ActorCore.Started THEN
			CASE r.kind OF
			| 0: f := NewAccountA | 1: f := NewAccountB | 2: f := NewCoord
			| 3: f := NewEcho | 4: f := NewClient | 5: f := NewWatcher
			END;
			target := ActorCore.Spawn(c, ActorCore.NewProps(f), "x")
		END
	END Receive;

	PROCEDURE NewRoot (): ActorCore.Actor;
		VAR r: Root;
	BEGIN
		NEW(r); r.kind := rootKind; RETURN r
	END NewRoot;

	PROCEDURE StartRoot (kind: INTEGER; IN name: ARRAY OF CHAR);
	(* Корень узла name с ребёнком "x" вида kind; адрес ребёнка — target
		после Started корня. *)
	BEGIN
		rootKind := kind; target := NIL;
		IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewRoot), name) = NIL THEN END
	END StartRoot;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		terms := 0; lastReason := -1; executed := 0; replies := 0; lastOk := FALSE;
		askRes := -1; lastPong := 0; INC(gen); target := NIL; watched := NIL
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown; Idle(5)
	END Teardown;

	PROCEDURE Pair (kindB: INTEGER; auto: BOOLEAN);
	(* Узлы na и nb на сети net; на nb корень с ребёнком вида kindB.
		auto — пул акторов и сеть, которую качает главный цикл; иначе ActorSim. *)
		VAR s: Name;
	BEGIN
		IF auto THEN ActorRun.Start(2); net := ActorNetMem.NewAuto() ELSE ActorSim.Install; net := ActorNetMem.New(gen) END;
		NodeName("A", s); na := ActorNet.New(s, net, 11); ActorNetMem.Add(net, na);
		NodeName("B", s); nb := ActorNet.New(s, net, 22); ActorNetMem.Add(net, nb);
		StartRoot(kindB, nb.name)
	END Pair;

	PROCEDURE WaitTarget;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE (target = NIL) & (Services.Ticks() - t <= 2000) DO Services.actionHook.Step; AsyncIO.Poll(0) END
	END WaitTarget;

	(* --- Ask через узел ------------------------------------------------------- *)

	PROCEDURE AskBody;
		VAR r: ActorCore.Ref; reply: Msg;
	BEGIN
		r := ActorNet.Import(na, capB);
		askRes := ActorCore.Ask(r, NewPing(41), askMs, reply);
		IF (reply # NIL) & (reply IS Pong) THEN lastPong := reply(Pong).n END
	END AskBody;

	PROCEDURE TestAskAcrossNodes*;
	(* Ask к актору на другом узле: ответ приходит через транспорт. *)
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB);
		askMs := 2000;
		UnitAssert.True(Drive(AskBody, 3000), "ask finished");
		UnitAssert.EqualInt(ActorCore.ok, askRes, "ask ok");
		UnitAssert.EqualInt(42, lastPong, "reply decoded");
		UnitAssert.EqualInt(1, executed, "executed once");
		UnitAssert.True(nb.received >= 1, "frame crossed the transport")
	END TestAskAcrossNodes;

	PROCEDURE TestAskTimeout*;
	(* Письмо потеряно сетью: Ask кончается тайм-аутом, не зависает. *)
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB);
		net.plan.loss := 1000; askMs := 100;
		UnitAssert.True(Drive(AskBody, 3000), "ask finished");
		UnitAssert.EqualInt(ActorCore.timedOut, askRes, "timed out");
		UnitAssert.EqualInt(0, executed, "nothing executed");
		UnitAssert.True(net.lost >= 1, "frame was lost")
	END TestAskTimeout;

	(* --- адрес-пропуск --------------------------------------------------------- *)

	PROCEDURE TestForgedAddressRefused*;
	(* Пропуск с подделанной меткой и пропуск с другим именем при старой
		метке отклоняются; настоящий проходит. *)
		VAR r: ActorCore.Ref; forged: ARRAY 256 OF CHAR; i: INTEGER;
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB);
		forged := capB$; i := LEN(forged$) - 1;
		IF forged[i] = "0" THEN forged[i] := "1" ELSE forged[i] := "0" END;
		r := ActorNet.Import(na, forged); UnitAssert.True(r # NIL, "forged parsed");
		ActorCore.Tell(r, NewPing(1));
		Idle(50);
		UnitAssert.EqualInt(1, nb.refused, "wrong mac refused");
		forged := capB$; i := 0; WHILE forged[i] # "|" DO INC(i) END;
		forged[i + LEN(nb.name$) + 2] := "y";	(* "B7/x" -> "B7/y": имя угадано, метка от другого *)
		r := ActorNet.Import(na, forged); ActorCore.Tell(r, NewPing(1));
		Idle(50);
		UnitAssert.EqualInt(2, nb.refused, "guessed name refused");
		UnitAssert.EqualInt(0, executed, "nothing executed");
		r := ActorNet.Import(na, capB); ActorCore.Tell(r, NewPing(1));
		UnitAssert.True(UntilInt(executed, 1, 2000), "real capability works")
	END TestForgedAddressRefused;

	(* --- наблюдение и потеря узла ----------------------------------------------- *)

	PROCEDURE WatchFromA;
	BEGIN
		watched := ActorNet.Import(na, capB);
		StartRoot(5, na.name); WaitTarget; Idle(50)
	END WatchFromA;

	PROCEDURE TestRemoteWatchStop*;
	(* Наблюдатель на A получает Terminated, когда актор на B остановлен. *)
		VAR echo: ActorCore.Ref;
	BEGIN
		Pair(3, TRUE); WaitTarget; echo := target; ActorNet.Export(nb, echo, capB);
		WatchFromA;
		UnitAssert.EqualInt(0, terms, "alive");
		ActorCore.StopActor(echo);
		UnitAssert.True(UntilInt(terms, 1, 2000), "terminated across nodes")
	END TestRemoteWatchStop;

	PROCEDURE TestNodeLost*;
	(* Падение узла B: наблюдатель на A получает Terminated адреса на B. *)
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB);
		WatchFromA;
		UnitAssert.EqualInt(0, terms, "alive");
		ActorNetMem.Crash(net, nb.name);
		UnitAssert.True(UntilInt(terms, 1, 2000), "lost node gives Terminated");
		UnitAssert.True(~nb.up, "node down")
	END TestNodeLost;

	PROCEDURE TestDuplicatesExecuteOnce*;
	(* Сеть дублирует каждый кадр: каждое письмо исполнено один раз. *)
		VAR r: ActorCore.Ref; i: INTEGER;
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB);
		net.plan.dup := 1000;
		r := ActorNet.Import(na, capB);
		FOR i := 1 TO 5 DO ActorCore.Tell(r, NewPing(i)) END;
		UnitAssert.True(UntilInt(executed, 5, 2000), "all delivered");
		Idle(50);
		UnitAssert.EqualInt(5, executed, "each once");
		UnitAssert.EqualInt(5, net.duplicated, "network duplicated each");
		UnitAssert.True(nb.duplicates >= 5, "duplicates dropped")
	END TestDuplicatesExecuteOnce;

	(* --- TCP ------------------------------------------------------------------- *)

	PROCEDURE TcpPair (OUT ta, tb: ActorNetTcp.Net);
	(* Узлы na и nb на TCP localhost; на nb эхо, пропуск — capB. *)
		VAR s: Name; adr: ARRAY 64 OF CHAR; res: INTEGER;
	BEGIN
		ActorRun.Start(2);
		ActorNetTcp.New("127.0.0.1:0", ta, res); UnitAssert.EqualInt(CommStreams.done, res, "listen a");
		ActorNetTcp.New("127.0.0.1:0", tb, res); UnitAssert.EqualInt(CommStreams.done, res, "listen b");
		NodeName("A", s); na := ActorNet.New(s, ta, 11); ActorNetTcp.Attach(ta, na);
		NodeName("B", s); nb := ActorNet.New(s, tb, 22); ActorNetTcp.Attach(tb, nb);
		ActorNetTcp.Address(tb, adr); ActorNetTcp.Peer(ta, nb.name, adr);
		ActorNetTcp.Address(ta, adr); ActorNetTcp.Peer(tb, na.name, adr);
		StartRoot(3, nb.name); WaitTarget; ActorNet.Export(nb, target, capB)
	END TcpPair;

	PROCEDURE TestTcpAsk*;
	(* Ask через настоящий сокет: кадр и ответ проходят TCP. *)
		VAR ta, tb: ActorNetTcp.Net;
	BEGIN
		TcpPair(ta, tb); askMs := 3000;
		UnitAssert.True(Drive(AskBody, 4000), "ask finished");
		UnitAssert.EqualInt(ActorCore.ok, askRes, "ask ok");
		UnitAssert.EqualInt(42, lastPong, "reply decoded");
		UnitAssert.True((ta.sentFrames >= 1) & (tb.readFrames >= 1), "frames over tcp");
		ActorNetTcp.Close(ta); ActorNetTcp.Close(tb)
	END TestTcpAsk;

	PROCEDURE TestTcpLost*;
	(* Сеть узла B закрыта: наблюдатель на A получает Terminated (обрыв = потеря узла). *)
		VAR ta, tb: ActorNetTcp.Net;
	BEGIN
		TcpPair(ta, tb);
		WatchFromA; Idle(100);
		UnitAssert.EqualInt(0, terms, "alive");
		ActorNetTcp.Close(tb);
		UnitAssert.True(UntilInt(terms, 1, 3000), "connection loss gives Terminated");
		ActorNetTcp.Close(ta)
	END TestTcpLost;

	(* --- причина в Terminated ---------------------------------------------------- *)

	PROCEDURE TestReasonLocal*;
	(* Локально: остановка — byStop, отказ (трап, супервизор остановил) — byFailure. *)
		VAR s: Name;
	BEGIN
		ActorRun.Start(2);
		NodeName("L", s); StartRoot(3, s); WaitTarget; watched := target;
		NodeName("M", s); StartRoot(5, s); WaitTarget; Idle(20);
		ActorCore.StopActor(watched);
		UnitAssert.True(UntilInt(terms, 1, 2000), "stopped");
		UnitAssert.EqualInt(ActorCore.byStop, lastReason, "reason stop");
		NodeName("N", s); StartRoot(3, s); WaitTarget; watched := target;
		NodeName("O", s); StartRoot(5, s); WaitTarget; Idle(20);
		ActorCore.Tell(watched, NewPing(-1));
		UnitAssert.True(UntilInt(terms, 2, 2000), "failed");
		UnitAssert.EqualInt(ActorCore.byFailure, lastReason, "reason failure")
	END TestReasonLocal;

	PROCEDURE TestReasonRemoteStop*;
	(* Через узел: остановка актора — byStop. *)
		VAR echo: ActorCore.Ref;
	BEGIN
		Pair(3, TRUE); WaitTarget; echo := target; ActorNet.Export(nb, echo, capB);
		WatchFromA;
		ActorCore.StopActor(echo);
		UnitAssert.True(UntilInt(terms, 1, 2000), "stopped");
		UnitAssert.EqualInt(ActorCore.byStop, lastReason, "remote stop")
	END TestReasonRemoteStop;

	PROCEDURE TestReasonRemoteFailure*;
	(* Через узел: отказ актора — byFailure. *)
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB); WatchFromA;
		ActorCore.Tell(ActorNet.Import(na, capB), NewPing(-1));
		UnitAssert.True(UntilInt(terms, 1, 2000), "failed");
		UnitAssert.EqualInt(ActorCore.byFailure, lastReason, "remote failure")
	END TestReasonRemoteFailure;

	PROCEDURE TestReasonNodeLoss*;
	(* Потеря узла — byNodeLoss. *)
	BEGIN
		Pair(3, TRUE); WaitTarget; ActorNet.Export(nb, target, capB); WatchFromA;
		ActorNetMem.Crash(net, nb.name);
		UnitAssert.True(UntilInt(terms, 1, 2000), "lost");
		UnitAssert.EqualInt(ActorCore.byNodeLoss, lastReason, "node loss")
	END TestReasonNodeLoss;

	(* --- повтор после потерянного ответа (детерминированно) ------------------------ *)

	PROCEDURE Settle;
	(* Пост: ни ящиков с работой, ни кадров в пути. *)
	BEGIN
		REPEAT IF ActorSim.Drain(100000) THEN END UNTIL ~ActorNetMem.Pump(net) & ActorSim.Idle()
	END Settle;

	PROCEDURE RetryBody;
		VAR client, acct: ActorCore.Ref; s: Send;
	BEGIN
		Settle; ActorNet.Export(nb, target, capB); acct := ActorNet.Import(na, capB);
		StartRoot(4, na.name); Settle; client := target;
		NEW(s); s.to := acct; s.m := NewMove(7, -100);
		ActorCore.Tell(client, s);
		IF ActorSim.Drain(1000) THEN END;	(* клиент послал команду *)
		IF ActorNetMem.Pump(net) THEN END;	(* команда дошла до B *)
		net.plan.loss := 1000;
		IF ActorSim.Drain(1000) THEN END;	(* B исполнил и ответил; ответ потерян *)
		net.plan.loss := 0; Settle;
		IF replies = 0 THEN	(* повтор той же операции *)
			NEW(s); s.to := acct; s.m := NewMove(7, -100);
			ActorCore.Tell(client, s); Settle
		END
	END RetryBody;

	PROCEDURE TestRetryAfterLostReply*;
	(* Ответ на команду потерян, клиент повторяет её с тем же номером:
		счёт не исполняет операцию второй раз и отвечает прежним итогом. *)
		VAR sum: Sum; last: LONGINT; res: INTEGER; s: Name;
	BEGIN
		ja := ActorJournal.NewMem(); jb := ActorJournal.NewMem();
		Pair(1, FALSE);
		UnitAssert.True(Drive(RetryBody, 5000), "finished");
		Child(nb, s); NEW(sum); ActorPersist.Read(jb, s, 1, sum, last, res);
		UnitAssert.EqualInt(1, replies, "reply after retry");
		UnitAssert.True(lastOk, "same outcome");
		UnitAssert.EqualInt(1, executed, "executed once");
		UnitAssert.EqualInt(1, last, "one event");
		UnitAssert.EqualInt(-100, sum.total, "withdrawn once");
		UnitAssert.True(net.lost >= 1, "reply was lost")
	END TestRetryAfterLostReply;

	(* --- защита: подделанные кадры и пропуска ------------------------------------ *)

	PROCEDURE (t: Tap) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
		VAR i: INTEGER;
	BEGIN
		NEW(t.last, MAX(len, 1)); t.len := len; FOR i := 0 TO len - 1 DO t.last[i] := frame[i] END;
		t.inner.Send(from, to, frame, len)
	END Send;

	PROCEDURE TapPair (OUT tap: Tap);
	(* Как Pair(3, FALSE), но кадры узлов идут через перехватчик tap. *)
		VAR s: Name;
	BEGIN
		ActorSim.Install; net := ActorNetMem.New(gen);
		NEW(tap); tap.inner := net;
		NodeName("A", s); na := ActorNet.New(s, tap, 11); ActorNetMem.Add(net, na);
		NodeName("B", s); nb := ActorNet.New(s, tap, 22); ActorNetMem.Add(net, nb);
		StartRoot(3, nb.name)
	END TapPair;

	PROCEDURE Copy (t: Tap; OUT b: ActorJournal.Bytes);
		VAR i: INTEGER;
	BEGIN
		NEW(b, LEN(t.last^)); FOR i := 0 TO t.len - 1 DO b[i] := t.last[i] END
	END Copy;

	PROCEDURE NameEnd (IN b: ARRAY OF BYTE; pos: INTEGER): INTEGER;
	(* Пост: позиция после имени кадра (2 байта длины и по 2 байта на знак) с позиции pos. *)
	BEGIN
		RETURN pos + 2 + 2 * (b[pos] MOD 256 + 256 * (b[pos + 1] MOD 256))
	END NameEnd;

	PROCEDURE MacPos (IN b: ARRAY OF BYTE): INTEGER;
	(* Пост: позиция метки адресата: вид, from, воплощение, номер, имя. *)
	BEGIN
		RETURN NameEnd(b, NameEnd(b, 1) + 16)
	END MacPos;

	PROCEDURE PutLong (VAR b: ARRAY OF BYTE; pos: INTEGER; x: LONGINT);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 7 DO b[pos + i] := SHORT(SHORT(SHORT(x MOD 256))); x := x DIV 256 END
	END PutLong;

	PROCEDURE SettleBody;
	BEGIN
		Settle; ActorNet.Export(nb, target, capB)
	END SettleBody;

	PROCEDURE Step2Body;
	BEGIN
		Settle
	END Step2Body;

	PROCEDURE TestUnloadedModuleRefused*;
	(* Кадр, где тип письма — из незагруженного модуля, отклонён; модуль не загружен. *)
		VAR tap: Tap; b: ActorJournal.Bytes; i, k: INTEGER; pat, rep: ARRAY 16 OF CHAR; hit: BOOLEAN;
	BEGIN
		TapPair(tap); UnitAssert.True(Drive(SettleBody, 3000), "started");
		UnitAssert.True(Kernel.ThisLoadedMod("ObxFact") = NIL, "precondition: ObxFact not loaded");
		ActorCore.Tell(ActorNet.Import(na, capB), NewPing(1)); Copy(tap, b); ActorNetMem.Drop(net);
		pat := "Actor_devNet"; rep := "ObxFact"; hit := FALSE;
		i := 0;
		WHILE ~hit & (i + 2 * 12 <= tap.len) DO
			k := 0; WHILE (k < 12) & (b[i + 2 * k] = ORD(pat[k])) & (b[i + 2 * k + 1] = 0) DO INC(k) END;
			IF k = 12 THEN
				hit := TRUE; FOR k := 0 TO 11 DO b[i + 2 * k] := SHORT(SHORT(ORD(rep[k]) MOD 128)); b[i + 2 * k + 1] := 0 END
			ELSE INC(i)
			END
		END;
		UnitAssert.True(hit, "module name found in frame");
		ActorNet.Deliver(nb, b, tap.len);
		UnitAssert.True(Kernel.ThisLoadedMod("ObxFact") = NIL, "module not loaded by a frame");
		UnitAssert.EqualInt(1, nb.refused, "frame refused")
	END TestUnloadedModuleRefused;

	PROCEDURE TestBadCapabilityGivesNil*;
	(* Длинный, обрезанный и с лишним хвостом пропуск — NIL, без трапа. *)
		VAR cap: ARRAY 1024 OF CHAR; i: INTEGER; ok: ARRAY 256 OF CHAR;
	BEGIN
		Pair(3, FALSE);
		FOR i := 0 TO 400 DO cap[i] := "x" END; cap[401] := "|"; cap[402] := "y"; cap[403] := "|"; cap[404] := 0X;
		UnitAssert.True(ActorNet.Import(na, cap) = NIL, "long node name");
		cap := "n|"; FOR i := 2 TO 400 DO cap[i] := "y" END; cap[401] := "|"; cap[402] := 0X;
		UnitAssert.True(ActorNet.Import(na, cap) = NIL, "long actor name");
		cap := "n|x|0123";
		UnitAssert.True(ActorNet.Import(na, cap) = NIL, "truncated mark");
		ok := "n|x|00112233445566778899aabbccddeeff";
		UnitAssert.True(ActorNet.Import(na, ok) # NIL, "good capability");
		cap := ok$ + "00";
		UnitAssert.True(ActorNet.Import(na, cap) = NIL, "mark too long")
	END TestBadCapabilityGivesNil;

	PROCEDURE TestForgedFrameKeepsWindow*;
	(* Кадр от имени A с неверной меткой и воплощением 1000 не сдвигает окно
		повторов: следующий настоящий кадр A доставлен. *)
		VAR tap: Tap; b: ActorJournal.Bytes; p: INTEGER;
	BEGIN
		TapPair(tap); UnitAssert.True(Drive(SettleBody, 3000), "started");
		ActorCore.Tell(ActorNet.Import(na, capB), NewPing(1)); Copy(tap, b); ActorNetMem.Drop(net);
		PutLong(b, NameEnd(b, 1), 1000);
		p := MacPos(b); b[p] := SHORT(SHORT((b[p] + 1) MOD 128));
		ActorNet.Deliver(nb, b, tap.len);
		UnitAssert.EqualInt(1, nb.refused, "forged refused");
		ActorCore.Tell(ActorNet.Import(na, capB), NewPing(2));
		UnitAssert.True(Drive(Step2Body, 3000), "settled");
		UnitAssert.EqualInt(1, executed, "real frame delivered")
	END TestForgedFrameKeepsWindow;

	PROCEDURE TestForgedTermRefused*;
	(* Кадр term с неверной меткой не будит наблюдателя; с верной — будит. *)
		VAR tap: Tap; b: ActorJournal.Bytes; p, k: INTEGER;
	BEGIN
		TapPair(tap); UnitAssert.True(Drive(SettleBody, 3000), "started");
		watched := ActorNet.Import(na, capB); StartRoot(5, na.name);
		UnitAssert.True(Drive(Step2Body, 3000), "watching");
		(* последний кадр — просьба watch от A; из неё — term от имени B *)
		Copy(tap, b); UnitAssert.EqualInt(1, b[0], "watch frame");
		b[0] := 3; b[3] := 66;	(* вид term, from "A…" -> "B…" *)
		PutLong(b, NameEnd(b, 1) + 8, 100000);	(* свой номер кадра *)
		p := MacPos(b); k := b[p]; b[p] := SHORT(SHORT((k + 1) MOD 128));
		ActorNet.Deliver(na, b, tap.len);
		UnitAssert.True(Drive(Step2Body, 3000), "settled");
		UnitAssert.EqualInt(0, terms, "forged term ignored");
		UnitAssert.EqualInt(1, na.refused, "forged term refused");
		b[p] := SHORT(SHORT(k)); PutLong(b, NameEnd(b, 1) + 8, 100001);
		ActorNet.Deliver(na, b, tap.len);
		UnitAssert.True(Drive(Step2Body, 3000), "settled");
		UnitAssert.EqualInt(1, terms, "real term wakes watcher")
	END TestForgedTermRefused;

	(* --- сага --------------------------------------------------------------------- *)

	PROCEDURE Balance (j: ActorJournal.Journal; n: ActorNet.Node): INTEGER;
		VAR sum: Sum; last: LONGINT; res: INTEGER; s: Name;
	BEGIN
		Child(n, s); NEW(sum); ActorPersist.Read(j, s, 1, sum, last, res);
		ASSERT(res = ActorJournal.ok, 60);
		RETURN initial + sum.total
	END Balance;

	PROCEDURE Restart (n: ActorNet.Node);
	(* Узел n поднят, корень создан заново (координатор — с адресами счетов). *)
	BEGIN
		ActorNet.Up(n);
		IF n = na THEN StartRoot(0, n.name) ELSIF n = nb THEN StartRoot(1, n.name) ELSE StartRoot(2, n.name) END;
		IF ActorSim.Drain(1000) THEN END;
		IF n = nc THEN coordRef := target END
	END Restart;

	PROCEDURE Terminal (s: INTEGER): BOOLEAN;
	BEGIN
		RETURN (s = done) OR (s = refunded) OR (s = failed)
	END Terminal;

	PROCEDURE AllTerminal (): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		IF nStates < maxT THEN RETURN FALSE END;
		i := 0; WHILE (i < maxT) & Terminal(states[i]) DO INC(i) END;
		RETURN i = maxT
	END AllTerminal;

	PROCEDURE Fail (IN what: ARRAY OF CHAR);
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		IF sagaOk THEN
			sagaOk := FALSE; Strings.IntToString(seed, d);
			sagaMsg := "seed " + d$ + ": " + what$
		END
	END Fail;

	PROCEDURE CrashNode (k: INTEGER);
		VAR n: ActorNet.Node;
	BEGIN
		IF k = 0 THEN n := na ELSIF k = 1 THEN n := nb ELSE n := nc END;
		ActorNetMem.Crash(net, n.name); INC(trace.crashes);
		IF ActorSim.Drain(1000) THEN END
	END CrashNode;

	PROCEDURE SagaBody;
	(* Один прогон саги от зерна seed; итог — trace, sagaOk и sagaMsg.
		Узел падает в случайный шаг (жребий от зерна) и поднимается через
		случайное число шагов: падение приходится на любую точку саги. *)
		VAR s: Name; i, round, down, upAt: INTEGER; st: Start; tick: Tick;
			sums: ARRAY maxT OF INTEGER; rev: ARRAY maxT OF BOOLEAN;
	BEGIN
		ActorSim.Install; net := ActorNetMem.New(seed);
		net.plan.loss := 100; net.plan.dup := 100; net.plan.delay := 3; net.plan.reorder := TRUE;
		ja := ActorJournal.NewMem(); jb := ActorJournal.NewMem(); jc := ActorJournal.NewMem();
		INC(gen);
		NodeName("A", s); na := ActorNet.New(s, net, 101); ActorNetMem.Add(net, na);
		NodeName("B", s); nb := ActorNet.New(s, net, 202); ActorNetMem.Add(net, nb);
		NodeName("C", s); nc := ActorNet.New(s, net, 303); ActorNetMem.Add(net, nc);
		StartRoot(0, na.name); IF ActorSim.Drain(1000) THEN END; ActorNet.Export(na, target, capA);
		StartRoot(1, nb.name); IF ActorSim.Drain(1000) THEN END; ActorNet.Export(nb, target, capB);
		StartRoot(2, nc.name); IF ActorSim.Drain(1000) THEN END; coordRef := target;
		nStates := 0; FOR i := 0 TO maxT - 1 DO states[i] := -1 END;
		FOR i := 0 TO maxT - 1 DO sums[i] := 1 + ActorNetMem.Rand(net, 600); rev[i] := ActorNetMem.Rand(net, 2) = 1 END;
		trace.crashes := 0; down := -1; upAt := 0; round := 0; NEW(tick);
		WHILE (round < 6000) & ~AllTerminal() DO
			INC(round);
			IF (round MOD 16 = 1) & (down # 2) THEN	(* неначатые переводы — по порядку номеров *)
				FOR i := nStates TO maxT - 1 DO
					NEW(st); st.id := i; st.sum := sums[i]; st.reverse := rev[i]; ActorCore.Tell(coordRef, st)
				END
			END;
			IF (round MOD 8 = 0) & (down # 2) THEN ActorCore.Tell(coordRef, tick) END;
			IF (round < 2000) & (down < 0) & (ActorNetMem.Rand(net, 1000) < 15) THEN
				down := ActorNetMem.Rand(net, 3); upAt := round + 1 + ActorNetMem.Rand(net, 40);
				CrashNode(down)
			ELSIF (down >= 0) & (round >= upAt) THEN
				IF down = 0 THEN Restart(na) ELSIF down = 1 THEN Restart(nb) ELSE Restart(nc) END;
				down := -1
			END;
			IF ActorSim.Step() THEN END; IF ActorSim.Step() THEN END;
			IF ActorNetMem.Pump(net) THEN END
		END;
		IF ActorSim.Drain(100000) THEN END;
		IF ~AllTerminal() THEN Fail("transfers not finished") END;
		trace.a := Balance(ja, na); trace.b := Balance(jb, nb);
		IF trace.a + trace.b # 2 * initial THEN Fail("sum not conserved") END;
		IF (trace.a < 0) OR (trace.b < 0) THEN Fail("negative balance") END;
		trace.steps := ActorSim.steps; trace.delivered := net.delivered; trace.lost := net.lost; trace.dup := net.duplicated;
		ActorNet.Down(na); ActorNet.Down(nb); ActorNet.Down(nc); IF ActorSim.Drain(100000) THEN END
	END SagaBody;

	PROCEDURE RunSeed (sd: LONGINT; OUT t: Trace): BOOLEAN;
	BEGIN
		seed := sd; sagaOk := TRUE; sagaMsg := "";
		IF ~Drive(SagaBody, 20000) THEN Fail("saga run hung") END;
		t := trace;
		RETURN sagaOk
	END RunSeed;

	PROCEDURE Same (IN x, y: Trace): BOOLEAN;
	BEGIN
		RETURN (x.steps = y.steps) & (x.delivered = y.delivered) & (x.lost = y.lost) & (x.dup = y.dup)
			& (x.a = y.a) & (x.b = y.b) & (x.crashes = y.crashes)
	END Same;

	PROCEDURE Seeds (from, to: LONGINT; again: BOOLEAN; OUT crashes: INTEGER): BOOLEAN;
	(* Прогоны саги для зёрен [from, to]; again — каждый дважды, сверка шаг в шаг.
		Пост: FALSE — провал, sagaMsg начинается с зерна. *)
		VAR sd: LONGINT; t1, t2: Trace;
	BEGIN
		crashes := 0; sd := from;
		WHILE sd <= to DO
			IF ~RunSeed(sd, t1) THEN RETURN FALSE END;
			INC(crashes, t1.crashes);
			IF again THEN
				IF ~RunSeed(sd, t2) THEN RETURN FALSE END;
				IF ~Same(t1, t2) THEN Fail("rerun differs"); RETURN FALSE END
			END;
			INC(sd)
		END;
		RETURN TRUE
	END Seeds;

	PROCEDURE TestSagaSeeds*;
	(* Сага на quickSeeds зёрнах с потерями, дублями, задержкой, перестановкой
		и падениями узлов: все переводы закончены, сумма сохранена, повтор
		зерна даёт тот же прогон. *)
		VAR crashes: INTEGER; ok: BOOLEAN;
	BEGIN
		ok := Seeds(1, quickSeeds, TRUE, crashes);
		UnitAssert.True(ok, sagaMsg);
		IF ok THEN UnitAssert.True(crashes >= quickSeeds DIV 2, "crashes happened") END
	END TestSagaSeeds;

	PROCEDURE TestSagaRunsFreeMemory*;
	(* Прогоны саги не копят память: узлы и сети прошлых прогонов собираются
		сборщиком (раньше их держал список узлов ActorNet, и время на зерно росло). *)
		VAR crashes: INTEGER; u0, u1: LONGINT;
	BEGIN
		IF Seeds(1, 20, FALSE, crashes) THEN END;
		Kernel.Collect; u0 := Kernel.Used();
		UnitAssert.True(Seeds(21, 220, FALSE, crashes), sagaMsg);
		Kernel.Collect; u1 := Kernel.Used();
		UnitAssert.True(u1 - u0 < 1000000, "memory does not grow with runs")
	END TestSagaRunsFreeMemory;

	PROCEDURE Long*;
	(** Долгий прогон саги на longSeeds зёрнах (вне обычного test.sh). *)
		VAR crashes: INTEGER; d: ARRAY 32 OF CHAR;
	BEGIN
		Setup;
		IF Seeds(1, longSeeds, FALSE, crashes) THEN
			Strings.IntToString(crashes, d); Log.String("Actor_devNet.Long ok, crashes " + d$)
		ELSE Log.String(sagaMsg)
		END;
		Log.Ln
	END Long;

	PROCEDURE Rerun*;
	(** Повторить прогон саги с последним зерном (после провала — с зерном провала). *)
		VAR t: Trace;
	BEGIN
		IF RunSeed(seed, t) THEN Log.String("seed ok") ELSE Log.String(sagaMsg) END; Log.Ln
	END Rerun;

END Actor_devNet.
