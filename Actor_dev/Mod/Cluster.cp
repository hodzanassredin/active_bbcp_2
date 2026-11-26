MODULE Actor_devCluster;
(** Узлы ActorNet и Raft в отдельных процессах blackbox-console.

	Сценарий этапа 1 плана кластера. Каждый процесс получает роль и
	каталог прогона внешними переменными (--SCENARIO, --ROLE, --DIR,
	--N, --IDX); адреса всех узлов лежат в файле nodes каталога, пропуски
	и результаты роли пишут и читают файлами там же. Процесс поднимает
	узел ActorNetTcp на своём адресе из файла, заводит акторов и крутит
	главный цикл (Services.actionHook.Step и AsyncIO.Poll) до файла stop.

	Сценарии:
	- ask — Ask и Watch между процессами; kill -9 узла даёт Terminated с
	  причиной byNodeLoss;
	- raft — кластер RaftNode из N процессов плюс клиент; выборы, запись,
	  kill -9 лидера, перевыборы, перезапуск узла с тем же хранилищем,
	  согласие журналов;
	- saga — сага ObxActor7 в трёх процессах: счета A и B, координатор C;
	  kill -9 координатора посреди перевода, перезапуск, повтор шага из
	  журнала;
	- grain — виртуальные акторы ObxActor8 в трёх процессах: у каждого
	  свой узел Raft (журнал RaftJournal) и свой узел ActorGrain; kill -9
	  владельца ключа, ключ переезжает, состояние читается из журнала.

	Номер воплощения узла процесс хранит в файле <узел>.inc: New всегда
	даёт 1, и окно повторов соседей отбросило бы кадры нового процесса
	как дубли (ActorNet.SetIncarnation). *)

	IMPORT Services, Tasks, Files, Dialog, Strings, Console, Stores,
		CommStreams,
		ActorCore, ActorRun, ActorNet, ActorNetTcp, ActorJournal, ActorFileJournal,
		ActorPersist, ActorGrain,
		CasStore, KvLin, KvStore, KvDb,
		Core := RaftCore, RaftSm, RaftStorage, RaftKvStorage, RaftNode, RaftJournal,
		RaftMembership, RaftGrainMembership;

	CONST
		maxRoles = 16;
		maxText = 4096;
		askMs = 5000;
		clientAskMs = 1500;
		maxNodes = Core.maxNodes;
		serveMs = 120000;	(* предел жизни процесса-службы в прогоне *)

	TYPE
		Name = ActorCore.Name;

		Addr = RECORD name: Name; adr: ARRAY 80 OF CHAR END;

		Ticker = POINTER TO RECORD (Services.Action) ref: ActorCore.Ref; ms: INTEGER; dead: BOOLEAN END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

		(* --- ask --------------------------------------------------------------- *)
		Ping = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		Pong = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		WatchMe = POINTER TO RECORD (ActorCore.Msg) r: ActorCore.Ref END;

		Echo = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		Sink = POINTER TO RECORD (ActorCore.Actor) END;

		(* --- raft -------------------------------------------------------------- *)
		Counter = POINTER TO RECORD (RaftSm.StateMachine) n: INTEGER END;

		(* --- saga -------------------------------------------------------------- *)
		Move = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		SagaReply = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; ok: BOOLEAN END;
		GetBalance = POINTER TO RECORD (ActorNet.Msg) END;
		Balance = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		Begin = POINTER TO RECORD (ActorCore.Msg) sum: INTEGER END;
		SagaTick = POINTER TO RECORD (ActorCore.Msg) END;

		Moved = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER; ok: BOOLEAN END;
		Step = POINTER TO RECORD (ActorPersist.Event) sum, state: INTEGER END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance: INTEGER; ops: ARRAY 16 OF LONGINT; oks: ARRAY 16 OF BOOLEAN; n: INTEGER
		END;

		Coord = POINTER TO RECORD (ActorPersist.Actor)
			started: BOOLEAN; sum, state: INTEGER; from, to: ActorCore.Ref
		END;

		(* --- grain ------------------------------------------------------------- *)
		Deposit = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		GrainBalance = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		Deposited = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER END;
		GrainAcct = POINTER TO RECORD (ActorGrain.Actor)
			balance: INTEGER; ops: ARRAY 16 OF LONGINT; n: INTEGER
		END;

	VAR
		dir: ARRAY 1024 OF CHAR;
		role, scenario: Name;
		idx, n, tickMs: INTEGER;
		stop, finished: BOOLEAN;
		adrs: ARRAY maxRoles OF Addr; nadr: INTEGER;
		t0: LONGINT;
		pong, watchReason: INTEGER;
		watchOk: BOOLEAN;
		clock: Ticker;
		rootRef: ActorCore.Ref;
		ndQ: ActorNet.Node;
		burst, sinkCount: INTEGER;
		raftCfg: RaftNode.Config;
		raftRef: ActorCore.Ref;
		nodes: INTEGER;
		clientRefs: ARRAY maxNodes OF ActorCore.Ref;
		clientId: INTEGER;
		sagaJournal: ActorJournal.Journal;
		sagaFrom, sagaTo: ActorCore.Ref;
		sagaState: INTEGER;
		sagaReady: BOOLEAN;
		sagaDup: INTEGER;
		stateLog: ARRAY 128 OF CHAR; nStates: INTEGER;
		capA, capB: ARRAY 256 OF CHAR;
		statusName: Name;
		grainCl: ActorGrain.Cluster;
		grainJournal: RaftJournal.Client;
		raftCaps: RaftNode.Caps;
		(* --- членство в процессах --- *)
		memCl: ActorGrain.Cluster;
		memJournal: RaftJournal.Client;
		memClient: RaftMembership.Client;
		memCfg: RaftGrainMembership.Config;
		memServiceCaps, memJournalCaps: RaftNode.Caps;
		memKey: ARRAY 2 OF BYTE;
		memOps, memOks, memBad: INTEGER;
		memBalance, memExpect: INTEGER;	(* сумма по всем счетам и ожидаемый баланс текущего *)
		memDone: BOOLEAN;

	(* --- параметры ------------------------------------------------------------ *)

	PROCEDURE GetPar (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR);
		VAR res: INTEGER;
	BEGIN
		Dialog.GetPar(key, val, res);
		IF res # 0 THEN val := "" END
	END GetPar;

	PROCEDURE GetInt (IN key: ARRAY OF CHAR; def: INTEGER): INTEGER;
		VAR s: ARRAY 32 OF CHAR; res, x, i: INTEGER; neg: BOOLEAN;
	BEGIN
		GetPar(key, s);
		IF s = "" THEN RETURN def END;
		i := 0; neg := FALSE;
		IF s[0] = "-" THEN neg := TRUE; INC(i) END;
		x := 0;
		WHILE (i < LEN(s)) & (s[i] >= "0") & (s[i] <= "9") DO x := x * 10 + ORD(s[i]) - ORD("0"); INC(i) END;
		IF neg THEN x := -x END;
		RETURN x
	END GetInt;

	(* --- файлы каталога прогона ------------------------------------------------ *)

	PROCEDURE Loc (): Files.Locator;
	BEGIN
		RETURN Files.dir.This(dir)
	END Loc;

	PROCEDURE CopyName (IN s: ARRAY OF CHAR; OUT nm: Files.Name);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(nm) - 1) & (i < LEN(s)) & (s[i] # 0X) DO nm[i] := s[i]; INC(i) END;
		nm[i] := 0X
	END CopyName;

	PROCEDURE Exists (IN name: ARRAY OF CHAR): BOOLEAN;
		VAR loc: Files.Locator; f: Files.File; nm: Files.Name;
	BEGIN
		loc := Loc();
		IF loc = NIL THEN RETURN FALSE END;
		CopyName(name, nm);
		f := Files.dir.Old(loc, nm, FALSE);
		IF f = NIL THEN RETURN FALSE END;
		f.Close; RETURN TRUE
	END Exists;

	PROCEDURE CopyNameSuf (IN s: ARRAY OF CHAR; OUT nm: Files.Name; IN suf: ARRAY OF CHAR);
		VAR i, k: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(nm) - 1) & (i < LEN(s)) & (s[i] # 0X) DO nm[i] := s[i]; INC(i) END;
		k := 0;
		WHILE (i < LEN(nm) - 1) & (k < LEN(suf)) & (suf[k] # 0X) DO nm[i] := suf[k]; INC(i); INC(k) END;
		nm[i] := 0X
	END CopyNameSuf;

	PROCEDURE PutText (IN name, text: ARRAY OF CHAR);
		(* Файл появляется целиком: текст пишется в <имя>.tmp и
			переименовывается — иначе сосед увидит пустой файл сразу
			после создания и прочтёт пустоту. *)
		VAR loc: Files.Locator; f: Files.File; w: Files.Writer; i, len, res: INTEGER; b: ARRAY 1 OF BYTE;
			nm, tmp: Files.Name;
	BEGIN
		loc := Loc();
		IF loc = NIL THEN RETURN END;
		CopyName(name, nm);
		CopyNameSuf(name, tmp, ".tmp");
		Files.dir.Delete(loc, tmp);
		f := Files.dir.New(loc, Files.dontAsk);
		IF f = NIL THEN RETURN END;
		f.Register(tmp, "", Files.dontAsk, res);
		f := Files.dir.Old(loc, tmp, Files.exclusive);
		IF f = NIL THEN RETURN END;
		w := f.NewWriter(NIL);
		len := LEN(text$);
		FOR i := 0 TO len - 1 DO b[0] := SHORT(SHORT(ORD(text[i]) MOD 256)); w.WriteBytes(b, 0, 1) END;
		f.Flush; f.Close;
		Files.dir.Delete(loc, nm);
		Files.dir.Rename(loc, tmp, nm, Files.dontAsk)
	END PutText;

	PROCEDURE GetText (IN name: ARRAY OF CHAR; OUT text: ARRAY OF CHAR);
		VAR loc: Files.Locator; f: Files.File; r: Files.Reader; i, len: INTEGER; b: ARRAY maxText OF BYTE;
			nm: Files.Name;
	BEGIN
		text := "";
		loc := Loc();
		IF loc = NIL THEN RETURN END;
		CopyName(name, nm);
		f := Files.dir.Old(loc, nm, FALSE);
		IF f = NIL THEN RETURN END;
		len := f.Length(); IF len > maxText - 1 THEN len := maxText - 1 END;
		r := f.NewReader(NIL);
		IF len > 0 THEN r.ReadBytes(b, 0, len) END;
		f.Close;
		FOR i := 0 TO len - 1 DO
			IF b[i] # 0 THEN text[i] := CHR(b[i] MOD 256) END
		END;
		text[len] := 0X
	END GetText;

	PROCEDURE IntToText (x: LONGINT; OUT s: ARRAY OF CHAR);
	BEGIN
		Strings.IntToString(x, s)
	END IntToText;

	PROCEDURE FileInt (IN name: ARRAY OF CHAR): LONGINT;
		VAR s: ARRAY 64 OF CHAR; i, x: INTEGER;
	BEGIN
		GetText(name, s); i := 0; x := 0;
		WHILE (i < LEN(s)) & (s[i] >= "0") & (s[i] <= "9") DO x := x * 10 + ORD(s[i]) - ORD("0"); INC(i) END;
		RETURN x
	END FileInt;

	PROCEDURE NextInc (IN node: ARRAY OF CHAR): LONGINT;
	(* Пост: счётчик запусков процесса для узла node: прочитан, увеличен
		и записан; первым запуском считается 1. *)
		VAR s: ARRAY 64 OF CHAR; inc: LONGINT;
	BEGIN
		IF Exists(node + ".inc") THEN inc := FileInt(node + ".inc") + 1 ELSE inc := 1 END;
		IntToText(inc, s); PutText(node + ".inc", s);
		RETURN inc
	END NextInc;

	(* --- адреса --------------------------------------------------------------- *)

	PROCEDURE LoadNodes;
		VAR s, line, name: ARRAY 512 OF CHAR; adr: ARRAY 80 OF CHAR; i, j, k: INTEGER;
	BEGIN
		nadr := 0;
		GetText("nodes", s);
		i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) & (nadr < maxRoles) DO
			j := 0;
			WHILE (i < LEN(s)) & (s[i] # 0X) & (s[i] # 0AX) & (s[i] # " ") DO name[j] := s[i]; INC(i); INC(j) END;
			name[j] := 0X;
			WHILE (i < LEN(s)) & ((s[i] = " ") OR (s[i] = 0AX) OR (s[i] = 0DX)) DO INC(i) END;
			k := 0;
			WHILE (i < LEN(s)) & (s[i] # 0X) & (s[i] # 0AX) & (s[i] # 0DX) DO adr[k] := s[i]; INC(i); INC(k) END;
			adr[k] := 0X;
			IF (name # "") & (adr # "") THEN
				adrs[nadr].name := name$; adrs[nadr].adr := adr$; INC(nadr)
			END;
			WHILE (i < LEN(s)) & ((s[i] = 0AX) OR (s[i] = 0DX)) DO INC(i) END
		END
	END LoadNodes;

	PROCEDURE AddrOf (IN node: ARRAY OF CHAR; OUT adr: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		adr := ""; i := 0;
		WHILE (i < nadr) & (adrs[i].name # node) DO INC(i) END;
		IF i < nadr THEN adr := adrs[i].adr$ END
	END AddrOf;

	PROCEDURE SecretOf (IN node: ARRAY OF CHAR): LONGINT;
	(* Секрет узла переживает перезапуск процесса: метки прежних пропусков
		остаются верными. *)
		VAR i: INTEGER; h: LONGINT;
	BEGIN
		h := 17; i := 0;
		WHILE node[i] # 0X DO h := (h * 131 + ORD(node[i])) MOD 1000000007; INC(i) END;
		RETURN h + 1
	END SecretOf;

	(* --- главный цикл --------------------------------------------------------- *)

	PROCEDURE Pump;
	(* Шаг главного цикла консоли: действия (в том числе планировщик задач)
		и опрос stdin вместе с дескрипторами, зарегистрированными Poller. *)
	BEGIN
		Services.actionHook.Step;
		IF Console.WaitInput(2) THEN END	(* ввод командой не читается *)
	END Pump;

	PROCEDURE CheckStop;
	BEGIN
		IF Exists("stop") THEN stop := TRUE END
	END CheckStop;

	PROCEDURE WaitFileTask (IN name: ARRAY OF CHAR; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~Exists(name) & ~stop & (Services.Ticks() - t < ms) DO Tasks.Sleep(20); CheckStop END;
		RETURN Exists(name)
	END WaitFileTask;

	PROCEDURE (t: Ticker) Do;
		VAR m: RaftNode.Tick;
	BEGIN
		IF ~t.dead & (t.ref # NIL) THEN
			NEW(m); ActorCore.Tell(t.ref, m);
			Services.DoLater(t, Services.Ticks() + t.ms)
		END
	END Do;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body
	END Run;

	PROCEDURE WaitFile (IN name: ARRAY OF CHAR; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~Exists(name) & ~stop & (Services.Ticks() - t < ms) DO Pump; CheckStop END;
		RETURN Exists(name)
	END WaitFile;

	PROCEDURE Serve;
		VAR last: LONGINT;
	BEGIN
		last := Services.Ticks();
		WHILE ~stop & ~finished & (Services.Ticks() - t0 < serveMs) DO
			Pump;
			IF Services.Ticks() - last >= 50 THEN CheckStop; last := Services.Ticks() END
		END
	END Serve;

	(* --- сообщения ask -------------------------------------------------------- *)

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

	PROCEDURE (a: Echo) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR p: Pong;
	BEGIN
		WITH m: Ping DO NEW(p); p.n := m.n; c.Respond(p)
		ELSE
		END
	END Receive;

	PROCEDURE (a: Watcher) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR s: ARRAY 128 OF CHAR;
	BEGIN
		WITH m: WatchMe DO c.Watch(m.r); PutText("Q.watching", "1")
		| m: ActorCore.Terminated DO
			watchReason := m.reason; watchOk := TRUE;
			IntToText(m.reason, s); PutText("Q.result", s)
		ELSE
		END
	END Receive;

	PROCEDURE NewEcho (): ActorCore.Actor;
		VAR a: Echo;
	BEGIN
		NEW(a); RETURN a
	END NewEcho;

	PROCEDURE NewWatcher (): ActorCore.Actor;
		VAR a: Watcher;
	BEGIN
		NEW(a); RETURN a
	END NewWatcher;

	PROCEDURE (a: Sink) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Ping DO
			INC(sinkCount);
			IF sinkCount = burst THEN PutText("Q.burst", "1") END;
			IF sinkCount = burst + 1 THEN PutText("Q.after", "1") END
		ELSE
		END
	END Receive;

	PROCEDURE NewSink (): ActorCore.Actor;
		VAR a: Sink;
	BEGIN
		NEW(a); RETURN a
	END NewSink;

	(* --- транспорт и узел ------------------------------------------------------ *)

	PROCEDURE OpenNode (IN node: ARRAY OF CHAR; OUT t: ActorNetTcp.Net; OUT nd: ActorNet.Node);
		VAR adr: ARRAY 80 OF CHAR; res: INTEGER; inc: LONGINT;
	BEGIN
		AddrOf(node, adr);
		ASSERT(adr # "", 100);
		ActorNetTcp.New(adr, t, res);
		ASSERT((res = CommStreams.done) & (t # NIL), 101);
		inc := NextInc(node);
		nd := ActorNet.New(node, t, SecretOf(node));
		ASSERT(nd # NIL, 102);
		ActorNetTcp.Attach(t, nd);
		IF inc > 1 THEN ActorNet.SetIncarnation(nd, inc) END
	END OpenNode;

	PROCEDURE PeerAll (t: ActorNetTcp.Net; IN self: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nadr - 1 DO
			IF adrs[i].name # self THEN ActorNetTcp.Peer(t, adrs[i].name, adrs[i].adr) END
		END
	END PeerAll;

	(* --- роль ask: узел E ------------------------------------------------------ *)

	PROCEDURE RunEcho;
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; cap, qcap: ARRAY 256 OF CHAR;
			remote: ActorCore.Ref; p: Ping; i: INTEGER;
	BEGIN
		OpenNode("E", t, nd); PeerAll(t, "E");
		rootRef := ActorCore.Spawn(NIL, ActorCore.NewProps(NewEcho), "E");
		ActorNet.Export(nd, rootRef, cap);
		PutText("E.cap", cap);
		WHILE ~Exists("Q.cap") & ~stop DO Pump; CheckStop END;
		GetText("Q.cap", qcap);
		remote := ActorNet.Import(nd, qcap);
		IF Exists("E.burstSent") THEN
			(* Узел уже работал: окно повторов соседа помнит прежние номера.
				Один кадр нового процесса дойдёт только после смены
				воплощения (ActorNet.SetIncarnation в OpenNode). *)
			NEW(p); p.n := burst + 1; ActorCore.Tell(remote, p);
			PutText("E.afterSent", "1")
		ELSE
			FOR i := 1 TO burst DO NEW(p); p.n := i; ActorCore.Tell(remote, p) END;
			PutText("E.burstSent", "1")
		END;
		PutText("E.ready", "1");
		Serve;
		ActorNetTcp.Close(t)
	END RunEcho;

	(* --- роль ask: узел Q ------------------------------------------------------ *)

	PROCEDURE AskBody;
		VAR cap: ARRAY 256 OF CHAR; s: ARRAY 128 OF CHAR; r: ActorCore.Msg; p: Ping; w: WatchMe;
			remote, watcher: ActorCore.Ref;
	BEGIN
		WHILE ~Exists("E.cap") & ~stop DO Tasks.Sleep(10) END;
		GetText("E.cap", cap);
		remote := ActorNet.Import(ndQ, cap);
		NEW(p); p.n := 42;
		IF ActorCore.Ask(remote, p, askMs, r) = ActorCore.ok THEN
			WITH r: Pong DO pong := r.n ELSE pong := -1 END;
			IntToText(pong, s); PutText("Q.ask", "ok " + s)
		ELSE PutText("Q.ask", "failed")
		END;
		watcher := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWatcher), "watcher");
		NEW(w); w.r := remote; ActorCore.Tell(watcher, w);
		WHILE ~watchOk & ~stop DO Tasks.Sleep(10) END
	END AskBody;

	PROCEDURE RunAsker;
		VAR t: ActorNetTcp.Net; d: Driver; sink: ActorCore.Ref; cap: ARRAY 256 OF CHAR;
	BEGIN
		OpenNode("Q", t, ndQ); PeerAll(t, "Q");
		sink := ActorCore.Spawn(NIL, ActorCore.NewProps(NewSink), "sink");
		ActorNet.Export(ndQ, sink, cap);
		PutText("Q.cap", cap);
		PutText("Q.ready", "1");
		NEW(d); d.body := AskBody; Tasks.Start(d);
		Serve;
		ActorNetTcp.Close(t)
	END RunAsker;

	(* --- raft: автомат счётчика ------------------------------------------------- *)

	PROCEDURE PutInt (VAR b: ARRAY OF BYTE; i, x: INTEGER);
	BEGIN
		b[i] := SHORT(SHORT(x MOD 256)); b[i + 1] := SHORT(SHORT(x DIV 256 MOD 256));
		b[i + 2] := SHORT(SHORT(x DIV 10000H MOD 256)); b[i + 3] := SHORT(SHORT(ASH(x, -24) MOD 256))
	END PutInt;

	PROCEDURE GetI32 (IN b: ARRAY OF BYTE; i: INTEGER): INTEGER;
	BEGIN
		RETURN b[i] MOD 256 + b[i + 1] MOD 256 * 100H + b[i + 2] MOD 256 * 10000H + ASH(LONG(b[i + 3]), 24)
	END GetI32;

	PROCEDURE (c: Counter) Apply (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): RaftSm.Bytes;
		VAR b: RaftSm.Bytes;
	BEGIN
		INC(c.n); NEW(b, 4); PutInt(b, 0, c.n); RETURN b
	END Apply;

	PROCEDURE (c: Counter) Snapshot (): RaftSm.Bytes;
		VAR b: RaftSm.Bytes;
	BEGIN
		NEW(b, 4); PutInt(b, 0, c.n); RETURN b
	END Snapshot;

	PROCEDURE (c: Counter) Restore (b: RaftSm.Bytes);
	BEGIN
		IF (b # NIL) & (LEN(b^) >= 4) THEN c.n := GetI32(b^, 0) ELSE c.n := 0 END
	END Restore;

	PROCEDURE NewCounterSm (): RaftSm.StateMachine;
		VAR c: Counter;
	BEGIN
		NEW(c); RETURN c
	END NewCounterSm;

	(* --- raft: узел ------------------------------------------------------------ *)

	PROCEDURE WaitCaps (count: INTEGER; ms: LONGINT): BOOLEAN;
		VAR i: INTEGER; s: ARRAY 64 OF CHAR; ok: BOOLEAN;
	BEGIN
		ok := FALSE;
		WHILE ~ok & ~stop & (Services.Ticks() - t0 < ms) DO
			ok := TRUE; i := 0;
			WHILE (i < count) & ok DO
				IntToText(i, s); ok := Exists("R" + s + ".cap"); INC(i)
			END;
			IF ~ok THEN Pump END
		END;
		RETURN ok
	END WaitCaps;

	PROCEDURE FillPeers;
		VAR i: INTEGER; s: ARRAY 64 OF CHAR; cap: ARRAY RaftNode.capLen OF CHAR;
	BEGIN
		FOR i := 0 TO n - 1 DO
			IntToText(i, s); GetText("R" + s + ".cap", cap);
			ASSERT(cap # "", 110);
			raftCfg.peers[i] := cap$
		END
	END FillPeers;

	PROCEDURE StartTicker (ref: ActorCore.Ref);
	BEGIN
		NEW(clock); clock.ref := ref; clock.ms := tickMs; clock.dead := FALSE;
		Services.DoLater(clock, Services.Ticks() + tickMs)
	END StartTicker;

	PROCEDURE WriteStatus (rl, term, leader, last, lastTerm, commit, applied: INTEGER; broken: BOOLEAN);
		VAR s, x: ARRAY 512 OF CHAR;
	BEGIN
		s := "";
		IntToText(rl, x); s := s + "role=" + x;
		IntToText(term, x); s := s + " term=" + x;
		IntToText(leader, x); s := s + " leader=" + x;
		IntToText(last, x); s := s + " last=" + x;
		IntToText(lastTerm, x); s := s + " lastTerm=" + x;
		IntToText(commit, x); s := s + " commit=" + x;
		IntToText(applied, x); s := s + " applied=" + x;
		IF broken THEN s := s + " broken=1" ELSE s := s + " broken=0" END;
		PutText("status-" + statusName, s)
	END WriteStatus;

	PROCEDURE OpenRaftStorage (IN node: ARRAY OF CHAR; OUT store: RaftStorage.Storage);
		VAR path: ARRAY 1200 OF CHAR; st: KvStore.Store; db: KvDb.Db; res: INTEGER;
	BEGIN
		path := dir + "/" + node + ".kv";
		KvLin.Open(path, st, res); ASSERT((res = KvStore.ok) & (st # NIL), 111);
		KvDb.Open(st, db, res); ASSERT(res = KvDb.ok, 112);
		store := RaftKvStorage.New(db)
	END OpenRaftStorage;

	PROCEDURE StatusLoop;
		VAR r, term, leader, last, lastTerm, commit, applied: INTEGER; broken: BOOLEAN;
			pr, pl, plast, pcommit, pa: INTEGER; t: LONGINT;
	BEGIN
		pr := -99; pl := -99; plast := -1; pcommit := -1; pa := -1; t := 0;
		WHILE ~stop & ~finished & (Services.Ticks() - t0 < serveMs) DO
			Pump; CheckStop;
			IF Services.Ticks() - t >= 50 THEN
				t := Services.Ticks();
				RaftNode.Status(raftCfg, r, term, leader, last, lastTerm, commit, applied, broken);
				IF (r # pr) OR (leader # pl) OR (last # plast) OR (commit # pcommit) OR (applied # pa) THEN
					WriteStatus(r, term, leader, last, lastTerm, commit, applied, broken);
					pr := r; pl := leader; plast := last; pcommit := commit; pa := applied
				END
			END
		END
	END StatusLoop;

	PROCEDURE RunRaft;
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; cap: ARRAY RaftNode.capLen OF CHAR;
			store: RaftStorage.Storage;
	BEGIN
		OpenNode(role, t, nd); PeerAll(t, role);
		IF GetInt("MEM", 0) = 1 THEN store := RaftStorage.NewMem()
		ELSE OpenRaftStorage(role, store)
		END;
		raftCfg := RaftNode.NewConfig(idx, n, nd, store, NewCounterSm);
		raftCfg.electionTicks := 10; raftCfg.seed := 1000 + idx * 37; raftCfg.tickMs := 0;
		raftRef := RaftNode.Start(raftCfg);
		statusName := role;
		ActorNet.Export(nd, raftRef, cap); PutText(role + ".cap", cap);
		ASSERT(WaitCaps(n, 20000), 113);
		FillPeers;
		StartTicker(raftRef);
		PutText(role + ".ready", "1");
		StatusLoop;
		clock.dead := TRUE;
		ActorRun.Shutdown;
		ActorNetTcp.Close(t)
	END RunRaft;

	(* --- raft: клиент ---------------------------------------------------------- *)

	PROCEDURE^ ClientBody;

	PROCEDURE RunClient;
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; s: ARRAY 64 OF CHAR; cap: ARRAY RaftNode.capLen OF CHAR;
			i: INTEGER; d: Driver;
	BEGIN
		OpenNode("C", t, nd); PeerAll(t, "C");
		clientId := SHORT(1000 + FileInt("C.inc"));
		ASSERT(WaitCaps(n, 20000), 114);
		nodes := n;
		FOR i := 0 TO n - 1 DO
			IntToText(i, s); GetText("R" + s + ".cap", cap);
			ASSERT(cap # "", 115);
			clientRefs[i] := ActorNet.Import(nd, cap)
		END;
		PutText("C.ready", "1");
		NEW(d); d.body := ClientBody; Tasks.Start(d);
		WHILE ~finished & ~stop & (Services.Ticks() - t0 < serveMs) DO Pump; CheckStop END;
		ActorNetTcp.Close(t)
	END RunClient;

	PROCEDURE ClientTry (seq, hint: INTEGER; OUT res, leaderHint: INTEGER; OUT data: Core.Bytes): BOOLEAN;
		(* Пост: TRUE — ответ получен (res — Reply.res, leaderHint — Reply.leader,
			data — результат автомата); FALSE — все узлы не ответили за clientAskMs. *)
		VAR p: RaftNode.Propose; r: ActorCore.Msg; i, k: INTEGER; b: Core.Bytes;
	BEGIN
		res := -1; leaderHint := -1; data := NIL;
		NEW(p); p.client := clientId; p.seq := seq; NEW(b, 1); b[0] := SHORT(SHORT(seq MOD 256)); p.body := b;
		IF (hint >= 0) & (hint < nodes) THEN
			IF ActorCore.Ask(clientRefs[hint], p, clientAskMs, r) = ActorCore.ok THEN
				WITH r: RaftNode.Reply DO res := r.res; leaderHint := r.leader; data := r.data END;
				RETURN TRUE
			END;
			RETURN FALSE
		END;
		FOR k := 0 TO nodes - 1 DO
			i := (k + seq) MOD nodes;
			IF ActorCore.Ask(clientRefs[i], p, clientAskMs, r) = ActorCore.ok THEN
				WITH r: RaftNode.Reply DO res := r.res; leaderHint := r.leader; data := r.data END;
				RETURN TRUE
			END
		END;
		RETURN FALSE
	END ClientTry;

	PROCEDURE ClientBody;
		VAR seq, ok, ops, hint, res, lh, pauseAt, lastLeader, v: INTEGER; now, firstFail: LONGINT;
			recovery, changes: LONGINT; s, x: ARRAY 512 OF CHAR; data: Core.Bytes; bad: BOOLEAN;
	BEGIN
		ops := GetInt("OPS", 30);
		pauseAt := 0;
		IF Exists("C.pauseAfter") THEN pauseAt := SHORT(FileInt("C.pauseAfter")) END;
		seq := 1; ok := 0; hint := -1; lastLeader := -1; changes := 0; recovery := -1; firstFail := -1;
		bad := FALSE;
		WHILE (seq <= ops) & ~stop DO
			now := Services.Ticks();
			IF ClientTry(seq, hint, res, lh, data) THEN
				IF res = RaftNode.ok THEN
					INC(ok); seq := seq + 1;
					(* Применённая команда возвращает счётчик автомата; он равен
						числу подтверждённых команд. Ответ ok без применения
						(ответ до фиксации) даёт NIL или чужое число. *)
					IF (data = NIL) OR (LEN(data^) < 4) THEN bad := TRUE
					ELSE
						v := GetI32(data^, 0);
						IF v # ok THEN
							bad := TRUE;
							IntToText(ok, x); s := "ok=" + x;
							IntToText(v, x); s := s + " v=" + x;
							PutText("C.bad", s)
						END
					END;
					IF lastLeader < 0 THEN lastLeader := lh
					ELSIF lastLeader # lh THEN
						INC(changes);
						IF firstFail >= 0 THEN recovery := now - firstFail; firstFail := -1 END;
						lastLeader := lh
					END;
					hint := lh;
					IntToText(lh, x); PutText("C.leader", x);
					IntToText(ok, x); s := "ok=" + x; IntToText(Services.Ticks() - t0, x); s := s + " ms=" + x;
					PutText("C.progress", s);
					IF (pauseAt > 0) & (ok = pauseAt) & ~Exists("C.resume") THEN
						(* пауза для сценария: снаружи убивают лидера и пишут C.resume *)
						PutText("C.paused", "1");
						WHILE ~Exists("C.resume") & ~stop DO Tasks.Sleep(10) END
					END
				ELSE
					IF firstFail < 0 THEN firstFail := now END;
					IF lh >= 0 THEN hint := lh ELSE hint := -1 END;
					Tasks.Sleep(10)
				END
			ELSE
				IF firstFail < 0 THEN firstFail := now END;
				hint := -1; Tasks.Sleep(10)
			END
		END;
		IntToText(ok, x); s := "oks=" + x;
		IntToText(Services.Ticks() - t0, x); s := s + " ms=" + x;
		IntToText(changes, x); s := s + " changes=" + x;
		IntToText(recovery, x); s := s + " recoveryMs=" + x;
		IF bad THEN s := s + " bad=1" ELSE s := s + " bad=0" END;
		PutText("C.result", s);
		finished := TRUE
	END ClientBody;

	(* --- saga: сообщения и события --------------------------------------------- *)

	PROCEDURE (m: Move) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Move) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: SagaReply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteBool(m.ok)
	END Write;

	PROCEDURE (m: SagaReply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadBool(m.ok)
	END Read;

	PROCEDURE (m: GetBalance) Write- (VAR wr: Stores.Writer);
	END Write;

	PROCEDURE (m: GetBalance) Read- (VAR rd: Stores.Reader);
	END Read;

	PROCEDURE (m: Balance) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: Balance) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
	END Read;

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
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum); wr.WriteInt(e.state)
	END Externalize;

	PROCEDURE (e: Step) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.sum); rd.ReadInt(e.state)
	END Internalize;

	(* --- saga: счёт ------------------------------------------------------------ *)

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Moved DO
			IF e.ok THEN INC(a.balance, e.sum) END;
			a.ops[a.n] := e.op; a.oks[a.n] := e.ok; INC(a.n)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Moved; r: SagaReply; b: Balance; i: INTEGER;
	BEGIN
		WITH m: Move DO
			i := 0; WHILE (i < a.n) & (a.ops[i] # m.op) DO INC(i) END;
			IF i = a.n THEN
				NEW(e); e.op := m.op; e.sum := m.sum; e.ok := a.balance + m.sum >= 0;
				a.Persist(c, e, m.op)
			END;
			NEW(r); r.op := m.op; r.ok := a.oks[i]; c.Respond(r)
		| m: GetBalance DO
			NEW(b); b.n := a.balance; c.Respond(b)
		ELSE
		END
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := 1000; a.journal := sagaJournal; RETURN a
	END NewAccount;

	(* --- saga: координатор ----------------------------------------------------- *)

	PROCEDURE (k: Coord) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Step DO k.started := TRUE; k.sum := e.sum; k.state := e.state; sagaState := e.state
		ELSE
		END
	END Apply;

	PROCEDURE (k: Coord) Command (c: ActorCore.Context), NEW;
		VAR m: Move;
	BEGIN
		NEW(m);
		CASE k.state OF
		| 0: m.op := 1; m.sum := -k.sum; c.Tell(k.from, m)	(* begun: снять *)
		| 1: m.op := 2; m.sum := k.sum; c.Tell(k.to, m)	(* withdrawn: зачислить *)
		| 3: m.op := 3; m.sum := k.sum; c.Tell(k.from, m)	(* refunding: вернуть *)
		ELSE
		END
	END Command;

	PROCEDURE (k: Coord) Go (c: ActorCore.Context; s: INTEGER), NEW;
		(* Записать шаг; команду шага шлёт ближайший Tick — так пауза
			между шагами видна водителю, и обрыв процесса попадает в шаг. *)
		VAR e: Step;
	BEGIN
		NEW(e); e.sum := k.sum; e.state := s; k.Persist(c, e, s)
	END Go;

	PROCEDURE (k: Coord) Handle (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Begin DO IF ~k.started THEN k.sum := m.sum; k.Go(c, 0) END
		| m: ActorPersist.ReplayComplete DO sagaReady := TRUE
		| m: SagaTick DO k.Command(c)
		| m: SagaReply DO
			IF (m.op = 1) & (k.state = 0) THEN
				IF m.ok THEN k.Go(c, 1) ELSE k.Go(c, 5) END
			ELSIF (m.op = 2) & (k.state = 1) THEN
				IF m.ok THEN k.Go(c, 2) ELSE k.Go(c, 3) END
			ELSIF (m.op = 3) & (k.state = 3) & m.ok THEN k.Go(c, 4)
			END
		ELSE
		END
	END Handle;

	PROCEDURE NewCoord (): ActorCore.Actor;
		VAR k: Coord;
	BEGIN
		NEW(k); k.journal := sagaJournal; k.from := sagaFrom; k.to := sagaTo; RETURN k
	END NewCoord;

	(* --- saga: процессы -------------------------------------------------------- *)

	PROCEDURE OpenSagaJournal (IN node: ARRAY OF CHAR);
		VAR loc: Files.Locator; nm: Files.Name; res: INTEGER;
	BEGIN
		loc := Loc(); ASSERT(loc # NIL, 116);
		CopyName(node + ".jrn", nm);
		sagaJournal := ActorFileJournal.Open(loc, nm, CasStore.NewMem(), res);
		ASSERT((res = ActorJournal.ok) & (sagaJournal # NIL), 117)
	END OpenSagaJournal;

	PROCEDURE RunAccount (IN node: ARRAY OF CHAR);
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; cap: ARRAY 256 OF CHAR;
	BEGIN
		OpenSagaJournal(node);
		OpenNode(node, t, nd); PeerAll(t, node);
		rootRef := ActorCore.Spawn(NIL, ActorCore.NewProps(NewAccount), node);
		ActorNet.Export(nd, rootRef, cap);
		PutText(node + ".cap", cap);
		PutText(node + ".ready", "1");
		Serve;
		sagaJournal.Close;
		ActorNetTcp.Close(t)
	END RunAccount;

	PROCEDURE SagaResult;
		VAR g: GetBalance; r: ActorCore.Msg; a, b, sum: INTEGER; s, x: ARRAY 512 OF CHAR;
	BEGIN
		a := -1; b := -1;
		NEW(g);
		IF ActorCore.Ask(sagaFrom, g, askMs, r) = ActorCore.ok THEN
			WITH r: Balance DO a := r.n ELSE END
		END;
		NEW(g);
		IF ActorCore.Ask(sagaTo, g, askMs, r) = ActorCore.ok THEN
			WITH r: Balance DO b := r.n ELSE END
		END;
		sum := a + b;
		IntToText(a, x); s := "A=" + x;
		IntToText(b, x); s := s + " B=" + x;
		IntToText(sum, x); s := s + " sum=" + x;
		IntToText(sagaDup, x); s := s + " dup=" + x;
		PutText("C.result", s)
	END SagaResult;

	PROCEDURE SagaDuplicate (OUT bal: INTEGER): BOOLEAN;
		(* Повтор шага 1 (списание 300) после конца саги: счёт узнаёт
			операцию и отвечает прежним итогом, остаток не меняется. *)
		VAR m: Move; g: GetBalance; r: ActorCore.Msg;
	BEGIN
		bal := -1;
		NEW(m); m.op := 1; m.sum := -300;
		IF ActorCore.Ask(sagaFrom, m, askMs, r) = ActorCore.ok THEN
			NEW(g);
			IF ActorCore.Ask(sagaFrom, g, askMs, r) = ActorCore.ok THEN
				WITH r: Balance DO bal := r.n ELSE END
			END
		END;
		RETURN bal >= 0
	END SagaDuplicate;

	PROCEDURE LogState (st: INTEGER);
		VAR x: ARRAY 16 OF CHAR; i: INTEGER;
	BEGIN
		IntToText(st, x); i := 0;
		WHILE (x[i] # 0X) & (nStates < LEN(stateLog) - 1) DO
			stateLog[nStates] := x[i]; INC(nStates); INC(i)
		END;
		stateLog[nStates] := 0X;
		PutText("C.states", stateLog)
	END LogState;

	PROCEDURE SagaDriver;
		VAR b: Begin; tick: SagaTick; i, last: INTEGER;
	BEGIN
		(* Ждём конец перечитывания журнала: состояние до него — не то,
			с которым координатор работает. *)
		WHILE ~sagaReady & ~stop DO Tasks.Sleep(10) END;
		last := -99;
		NEW(b); b.sum := 300; ActorCore.Tell(rootRef, b);
		i := 0;
		WHILE ~stop & (sagaState < 2) & (sagaState # 5) DO
			IF sagaState # last THEN LogState(sagaState); last := sagaState END;
			INC(i);
			IF (sagaState = 1) & ~Exists("C.resume") THEN
				(* процесс не убит: ждём обрыва снаружи, команд не шлём *)
				IF ~Exists("C.withdrawn") THEN PutText("C.withdrawn", "1") END;
				WHILE ~Exists("C.resume") & ~stop DO Tasks.Sleep(10) END
			END;
			IF i MOD 4 = 0 THEN NEW(tick); ActorCore.Tell(rootRef, tick) END;
			Tasks.Sleep(10)
		END;
		IF sagaState # last THEN LogState(sagaState) END;
		IF ~SagaDuplicate(sagaDup) THEN sagaDup := -1 END;
		SagaResult;
		finished := TRUE
	END SagaDriver;

	PROCEDURE RunSagaCoord;
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; d: Driver;
	BEGIN
		OpenSagaJournal("C");
		OpenNode("C", t, nd); PeerAll(t, "C");
		WHILE ~(Exists("A.cap") & Exists("B.cap")) & ~stop DO Pump; CheckStop END;
		GetText("A.cap", capA); GetText("B.cap", capB);
		sagaFrom := ActorNet.Import(nd, capA); sagaTo := ActorNet.Import(nd, capB);
		ASSERT((sagaFrom # NIL) & (sagaTo # NIL), 118);
		sagaState := -1; sagaReady := FALSE; sagaDup := -1;
		stateLog := ""; nStates := 0;
		rootRef := ActorCore.Spawn(NIL, ActorCore.NewProps(NewCoord), "C");
		PutText("C.ready", "1");
		NEW(d); d.body := SagaDriver; Tasks.Start(d);
		Serve;
		sagaJournal.Close;
		ActorNetTcp.Close(t)
	END RunSagaCoord;

	(* --- grain: сообщения и счёт ------------------------------------------------- *)

	PROCEDURE (m: Deposit) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Deposit) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: GrainBalance) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: GrainBalance) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
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

	PROCEDURE (a: GrainAcct) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO
			INC(a.balance, e.sum);
			IF a.n < LEN(a.ops) THEN a.ops[a.n] := e.op; INC(a.n) END
		ELSE
		END
	END Apply;

	PROCEDURE (a: GrainAcct) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Deposited; r: GrainBalance; i: INTEGER;
	BEGIN
		WITH m: Deposit DO
			i := 0; WHILE (i < a.n) & (a.ops[i] # m.op) DO INC(i) END;
			IF i = a.n THEN
				NEW(e); e.op := m.op; e.sum := m.sum; a.Persist(c, e, m.op)	(* ответ — в Persisted *)
			ELSE
				NEW(r); r.n := a.balance; c.Respond(r)
			END
		ELSE
		END
	END Handle;

	PROCEDURE (a: GrainAcct) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
		VAR r: GrainBalance;
	BEGIN
		NEW(r); r.n := a.balance; c.Respond(r)
	END Persisted;

	PROCEDURE NewGrainAcct (): ActorCore.Actor;
		VAR a: GrainAcct;
	BEGIN
		NEW(a); RETURN a
	END NewGrainAcct;

	(* --- grain: процесс ---------------------------------------------------------- *)

	PROCEDURE GrainDeposit (gr: ActorCore.Ref; op, sum: INTEGER; OUT bal: INTEGER; ms: LONGINT): BOOLEAN;
		VAR d: Deposit; r: ActorCore.Msg; t: LONGINT;
	BEGIN
		bal := -1; t := Services.Ticks();
		REPEAT
			NEW(d); d.op := op; d.sum := sum;
			IF ActorCore.Ask(gr, d, askMs, r) = ActorCore.ok THEN
				WITH r: GrainBalance DO bal := r.n; RETURN TRUE ELSE END
			END;
			Tasks.Sleep(50)
		UNTIL stop OR (Services.Ticks() - t >= ms);
		RETURN FALSE
	END GrainDeposit;

	PROCEDURE GrainDriver;
		VAR key, owner, nowner: Name; gr: ActorCore.Ref; i, bal: INTEGER; ok, found: BOOLEAN;
			s, x: ARRAY 1024 OF CHAR;
	BEGIN
		owner := "";
		i := 0; found := FALSE;
		WHILE (i <= 49) & ~found DO
			IntToText(42 + i, key);
			ActorGrain.Owner(grainCl, "acct", key, owner);
			found := owner # role;
			IF ~found THEN INC(i) END
		END;
		ASSERT((owner # "") & (owner # role), 120);
		gr := ActorGrain.Ref(grainCl, "acct", key);
		ok := GrainDeposit(gr, 1, 100, bal, 30000);
		IF ok THEN ok := GrainDeposit(gr, 2, 50, bal, 30000) END;
		IntToText(bal, x); s := "key=" + key + " old=" + owner + " after2=" + x;
		PutText("G0.deposited", s);
		IF ~ok THEN PutText("G0.result", "failed deposits"); finished := TRUE; RETURN END;
		(* владелец убит снаружи: ждём, пока кластер признает его мёртвым *)
		WHILE ActorGrain.Alive(grainCl, owner) & ~stop & (Services.Ticks() - t0 < serveMs) DO Tasks.Sleep(20) END;
		ok := GrainDeposit(gr, 3, 25, bal, 40000);
		ActorGrain.Owner(grainCl, "acct", key, nowner);
		IntToText(bal, x);
		s := "key=" + key + " old=" + owner + " new=" + nowner + " balance=" + x;
		IF ~ok THEN s := s + " failed" END;
		PutText("G0.result", s);
		finished := TRUE
	END GrainDriver;

	PROCEDURE RunGrain;
		VAR raftT, grainT: ActorNetTcp.Net; raftNd, grainNd: ActorNet.Node;
			store: RaftStorage.Storage; s: ARRAY 64 OF CHAR; cap: ARRAY 256 OF CHAR;
			i, inc: INTEGER; d: Driver;
	BEGIN
		IntToText(idx, s);
		statusName := "R" + s;
		OpenNode(role, grainT, grainNd); PeerAll(grainT, role);
		OpenNode(statusName, raftT, raftNd); PeerAll(raftT, statusName);
		OpenRaftStorage(statusName, store);
		raftCfg := RaftNode.NewConfig(idx, n, raftNd, store, RaftJournal.NewMachine);
		raftCfg.electionTicks := 10; raftCfg.seed := 1000 + idx * 37; raftCfg.tickMs := 0;
		raftRef := RaftNode.Start(raftCfg);
		ActorNet.Export(raftNd, raftRef, cap); PutText(statusName + ".cap", cap);
		ASSERT(WaitCaps(n, 20000), 121);
		FillPeers;
		StartTicker(raftRef);
		(* пропуски узлов Raft для журнала на узле гранул *)
		FOR i := 0 TO n - 1 DO
			IntToText(i, s); GetText("R" + s + ".cap", cap);
			ASSERT(cap # "", 122);
			raftCaps[i] := cap$
		END;
		inc := SHORT(FileInt(role + ".inc"));
		clientId := (idx + 1) * 100000 + inc;
		grainJournal := RaftJournal.New(grainNd, raftCaps, n, clientId);
		grainCl := ActorGrain.New(grainNd, grainJournal);
		ActorGrain.HostCap(grainCl, cap);
		PutText(role + ".gcap", cap);
		FOR i := 0 TO n - 1 DO
			IF i # idx THEN
				IntToText(i, s);
				ASSERT(WaitFile("G" + s + ".gcap", 20000), 123)
			END
		END;
		FOR i := 0 TO n - 1 DO
			IF i # idx THEN
				IntToText(i, s); GetText("G" + s + ".gcap", cap);
				ActorGrain.Member(grainCl, cap)
			END
		END;
		ActorGrain.Register(grainCl, "acct", NewGrainAcct);
		PutText(role + ".ready", "1");
		IF idx = 0 THEN NEW(d); d.body := GrainDriver; Tasks.Start(d) END;
		StatusLoop;
		clock.dead := TRUE;
		grainJournal.Close;
		ActorRun.Shutdown;
		ActorNetTcp.Close(grainT); ActorNetTcp.Close(raftT)
	END RunGrain;


	(* --- служба состава в процессах ------------------------------------------- *)

	PROCEDURE SetCount (s: SET): INTEGER;
		VAR i, k: INTEGER;
	BEGIN
		k := 0; FOR i := 0 TO Core.maxNodes - 1 DO IF i IN s THEN INC(k) END END;
		RETURN k
	END SetCount;

	PROCEDURE WaitRoleCaps (IN prefix: ARRAY OF CHAR; count: INTEGER; ms: LONGINT): BOOLEAN;
		VAR i: INTEGER; s: ARRAY 64 OF CHAR; ok: BOOLEAN;
	BEGIN
		ok := FALSE;
		WHILE ~ok & ~stop & (Services.Ticks() - t0 < ms) DO
			ok := TRUE; i := 0;
			WHILE (i < count) & ok DO
				IntToText(i, s); ok := Exists(prefix + s + ".cap"); INC(i)
			END;
			IF ~ok THEN Pump END
		END;
		RETURN ok
	END WaitRoleCaps;

	PROCEDURE ReadCaps (IN prefix: ARRAY OF CHAR; count: INTEGER; OUT caps: RaftNode.Caps);
		VAR i: INTEGER; s: ARRAY 64 OF CHAR;
	BEGIN
		FOR i := 0 TO count - 1 DO
			IntToText(i, s); GetText(prefix + s + ".cap", caps[i]); ASSERT(caps[i] # "", 124)
		END
	END ReadCaps;

	PROCEDURE StatusInt (IN name, key: ARRAY OF CHAR; def: INTEGER): INTEGER;
		VAR t: ARRAY 512 OF CHAR; i, j, x: INTEGER; found: BOOLEAN;
	BEGIN
		GetText(name, t); i := 0; found := FALSE;
		WHILE (i < LEN(t)) & (t[i] # 0X) & ~found DO
			j := 0;
			WHILE (key[j] # 0X) & (t[i + j] = key[j]) DO INC(j) END;
			IF key[j] = 0X THEN found := TRUE; INC(i, j) ELSE INC(i) END
		END;
		IF ~found THEN RETURN def END;
		IF t[i] = "=" THEN INC(i) END;
		x := 0;
		WHILE (i < LEN(t)) & (t[i] >= "0") & (t[i] <= "9") DO x := x * 10 + ORD(t[i]) - ORD("0"); INC(i) END;
		RETURN x
	END StatusInt;

	PROCEDURE WriteSvcStatus;
		VAR voters, learners: SET; rl, term, leader, last, lastTerm, commit, applied: INTEGER;
			broken, dirty, isV, isL: BOOLEAN; s, x: ARRAY 256 OF CHAR;
	BEGIN
		RaftNode.Status(raftCfg, rl, term, leader, last, lastTerm, commit, applied, broken);
		RaftNode.ConfStatus(raftCfg, voters, learners, dirty, isV, isL);
		s := "role=" + role; IntToText(rl, x); s := s + " role=" + x;
		IntToText(term, x); s := s + " term=" + x;
		IntToText(leader, x); s := s + " leader=" + x;
		IntToText(SetCount(voters), x); s := s + " voters=" + x;
		IF isV THEN s := s + " isVoter=1" ELSE s := s + " isVoter=0" END;
		IF dirty THEN s := s + " dirty=1" ELSE s := s + " dirty=0" END;
		PutText(role + ".status", s)
	END WriteSvcStatus;

	PROCEDURE RunMembershipService;
		VAR t: ActorNetTcp.Net; nd: ActorNet.Node; store: RaftStorage.Storage;
			cap: ARRAY RaftNode.capLen OF CHAR; i: INTEGER; last: LONGINT; xs: ARRAY 16 OF CHAR;
			vs, ls: SET; dirty, isV, isL: BOOLEAN;
	BEGIN
		OpenNode(role, t, nd); PeerAll(t, role);
		OpenRaftStorage(role, store);
		raftCfg := RaftNode.NewConfig(idx, 3, nd, store, RaftMembership.NewMachine);
		raftCfg.electionTicks := 10; raftCfg.seed := 5000 + idx * 37; raftCfg.tickMs := 0;
		raftRef := RaftNode.Start(raftCfg);
		statusName := role;
		ActorNet.Export(nd, raftRef, cap); PutText(role + ".cap", cap);
		ASSERT(WaitRoleCaps("S", 5, 20000), 120);
		FOR i := 0 TO 4 DO
			IF i # idx THEN IntToText(i, xs); GetText("S" + xs + ".cap", cap); RaftNode.SetPeer(raftCfg, i, cap) END
		END;
		StartTicker(raftRef);
		PutText(role + ".ready", "1");
		last := 0;
		WHILE ~stop & (Services.Ticks() - t0 < serveMs) DO
			Pump; CheckStop;
			IF Services.Ticks() - last >= 50 THEN
				last := Services.Ticks();
				IF Exists("svc.add3") & Exists("S3.cap") THEN
					GetText("S3.cap", cap); RaftNode.SetPeer(raftCfg, 3, cap); RaftNode.AddServer(raftCfg, 3)
				END;
				IF Exists("svc.add4") & Exists("S4.cap") THEN
					RaftNode.ConfStatus(raftCfg, vs, ls, dirty, isV, isL);
					IF SetCount(vs) = 4 THEN
						GetText("S4.cap", cap); RaftNode.SetPeer(raftCfg, 4, cap); RaftNode.AddServer(raftCfg, 4)
					END
				END;
				IF Exists("svc.remove4") THEN RaftNode.RemoveServer(raftCfg, 4) END;
				WriteSvcStatus
			END
		END;
		clock.dead := TRUE; ActorRun.Shutdown; ActorNetTcp.Close(t)
	END RunMembershipService;


	PROCEDURE WriteGrainStatus;
		VAR joined: BOOLEAN; now, members, lists: INTEGER; s, x: ARRAY 256 OF CHAR;
	BEGIN
		RaftGrainMembership.Status(memCfg, joined, now, members, lists);
		s := "role=" + role; IntToText(ActorGrain.AliveCount(memCl), x); s := s + " alive=" + x;
		IntToText(ActorGrain.Members(memCl), x); s := s + " members=" + x;
		IF joined THEN s := s + " joined=1" ELSE s := s + " joined=0" END;
		IntToText(RaftGrainMembership.Errors(memCfg), x); s := s + " errors=" + x;
		IntToText(lists, x); s := s + " lists=" + x;
		PutText(role + ".status", s)
	END WriteGrainStatus;

	PROCEDURE MembershipGrainLoop;
		VAR last, leftAt: LONGINT; t: RaftNode.Tick; s: ARRAY 128 OF CHAR;
	BEGIN
		last := 0; leftAt := -1;
		WHILE ~stop & (Services.Ticks() - t0 < serveMs) DO
			Pump; CheckStop;
			IF Services.Ticks() - last >= tickMs THEN
				last := Services.Ticks();
				RaftGrainMembership.time := Services.Ticks();
				RaftGrainMembership.Tick(memCfg);
				RaftJournal.Tick(memJournal);
				IF idx < 3 THEN NEW(t); ActorCore.Tell(raftRef, t) END;
				IF (leftAt < 0) & Exists("leave") THEN
					GetText("leave", s);
					IF s = role THEN RaftGrainMembership.Leave(memCfg); leftAt := Services.Ticks() END
				END;
				WriteGrainStatus
			END
		END
	END MembershipGrainLoop;

	PROCEDURE^ MembershipDriver;

	PROCEDURE RunMembershipGrain;
		VAR grainT, raftT: ActorNetTcp.Net; grainNd, raftNd: ActorNet.Node;
			store: RaftStorage.Storage; cap: ARRAY RaftNode.capLen OF CHAR;
			s: ARRAY 64 OF CHAR; i, inc, id: INTEGER; d: Driver;
	BEGIN
		IntToText(idx, s);
		OpenNode(role, grainT, grainNd); PeerAll(grainT, role);
		IF idx < 3 THEN
			statusName := "R" + s;
			OpenNode(statusName, raftT, raftNd); PeerAll(raftT, statusName);
			OpenRaftStorage(statusName, store);
			raftCfg := RaftNode.NewConfig(idx, 3, raftNd, store, RaftJournal.NewMachine);
			raftCfg.electionTicks := 10; raftCfg.seed := 7000 + idx * 37; raftCfg.tickMs := 0;
			raftRef := RaftNode.Start(raftCfg);
			ActorNet.Export(raftNd, raftRef, cap); PutText(statusName + ".cap", cap)
		END;
		ASSERT(WaitRoleCaps("S", 5, 30000), 121);
		ReadCaps("S", 5, memServiceCaps);
		ASSERT(WaitRoleCaps("R", 3, 60000), 122);
		ReadCaps("R", 3, memJournalCaps);
		IF idx < 3 THEN
			FOR i := 0 TO 2 DO IF i # idx THEN RaftNode.SetPeer(raftCfg, i, memJournalCaps[i]) END END
		END;
		inc := SHORT(FileInt(role + ".inc")); IF inc < 1 THEN inc := 1 END;
		id := (idx + 5) * 100000 + inc;
		memJournal := RaftJournal.New(grainNd, memJournalCaps, 3, id);
		memJournal.tickMs := 0;
		memClient := RaftMembership.New(grainNd, memServiceCaps, 5, (idx + 5) * 1000000 + inc);
		memClient.tickMs := 0;
		memCl := ActorGrain.New(grainNd, memJournal); ActorGrain.Register(memCl, "acct", NewGrainAcct);
		memKey[0] := SHORT(SHORT(idx + 1)); memKey[1] := 3;
		memCfg := RaftGrainMembership.NewConfig(idx, memCl, memClient, memKey, 2, 5000, 10, 10, 0);
		ASSERT(RaftGrainMembership.Start(memCfg) # NIL, 123);
		PutText(role + ".ready", "1");
		IF idx = 0 THEN NEW(d); d.body := MembershipDriver; Tasks.Start(d) END;
		MembershipGrainLoop;
		clock.dead := TRUE; memJournal.Close; RaftMembership.Close(memClient);
		ActorRun.Shutdown;
		ActorNetTcp.Close(grainT);
		IF idx < 3 THEN ActorNetTcp.Close(raftT) END
	END RunMembershipGrain;

	PROCEDURE WaitGrainAlive (k: INTEGER; skip: SET; ms: LONGINT): BOOLEAN;
		VAR i: INTEGER; t: LONGINT; ok: BOOLEAN; s: ARRAY 16 OF CHAR;
	BEGIN
		t := Services.Ticks(); ok := FALSE;
		WHILE ~ok & ~stop & (Services.Ticks() - t < ms) DO
			ok := TRUE; i := 0;
			WHILE (i <= 3) & ok DO
				IntToText(i, s);
				IF ~(i IN skip) & Exists("G" + s + ".status") THEN
					IF StatusInt("G" + s + ".status", "alive", -1) # k THEN ok := FALSE END
				END;
				INC(i)
			END;
			IF ok THEN ok := StatusInt("G0.status", "alive", -1) = k END;
			IF ~ok THEN Tasks.Sleep(20) END
		END;
		RETURN ok
	END WaitGrainAlive;

	PROCEDURE WaitSvcVoters (k, ms: LONGINT): BOOLEAN;
		VAR i: INTEGER; t: LONGINT; ok: BOOLEAN; s: ARRAY 16 OF CHAR;
	BEGIN
		t := Services.Ticks(); ok := FALSE;
		WHILE ~ok & ~stop & (Services.Ticks() - t < ms) DO
			ok := FALSE; i := 0;
			WHILE (i < 5) & ~ok DO
				IntToText(i, s);
				IF Exists("S" + s + ".status") & (StatusInt("S" + s + ".status", "voters", -1) = k) THEN ok := TRUE END;
				INC(i)
			END;
			IF ~ok THEN Tasks.Sleep(20) END
		END;
		RETURN ok
	END WaitSvcVoters;

	PROCEDURE KeyOwnedByRole (IN want: ARRAY OF CHAR; OUT key: ActorCore.Name): BOOLEAN;
		VAR k: INTEGER; d: ARRAY 16 OF CHAR; o: ActorCore.Name; found: BOOLEAN;
	BEGIN
		found := FALSE; k := 1; key := "";
		WHILE (k < 1000) & ~found DO
			IntToText(k, d); key := d$;
			ActorGrain.Owner(memCl, "acct", key, o);
			found := o = want;
			IF ~found THEN INC(k) END
		END;
		IF ~found THEN key := "" END;
		RETURN found
	END KeyOwnedByRole;

	PROCEDURE MemDeposit (gr: ActorCore.Ref; op, sum: INTEGER; ms: LONGINT): BOOLEAN;
	(* Вклад в текущий ключ: ответ — баланс этого ключа, не меньше суммы вкладов. *)
		VAR bal: INTEGER;
	BEGIN
		IF ~GrainDeposit(gr, op, sum, bal, ms) THEN RETURN FALSE END;
		INC(memExpect, sum);
		IF bal # memExpect THEN PutText("driver.fail", "balance"); RETURN FALSE END;
		INC(memBalance, sum);
		RETURN TRUE
	END MemDeposit;

	PROCEDURE MembershipDriver;
		VAR key, owner, roleName: ActorCore.Name; ref: ActorCore.Ref; i: INTEGER; ok, good: BOOLEAN;
			s: ARRAY 512 OF CHAR; n16: ARRAY 16 OF CHAR; skipKill: SET;
	BEGIN
		good := TRUE;
		IF ~WaitGrainAlive(3, {}, 60000) THEN good := FALSE; PutText("driver.fail", "grains3") END;
		IF good & ~KeyOwnedByRole("G1", key) THEN good := FALSE; PutText("driver.fail", "key-g1") END;
		IF good THEN
			ref := ActorGrain.Ref(memCl, "acct", key);
			ok := MemDeposit(ref, 1, 100, 30000); good := ok;
			IF ok THEN INC(memOks) ELSE INC(memBad) END;
			IF ok THEN ok := MemDeposit(ref, 2, 50, 30000); good := good & ok; IF ok THEN INC(memOks) ELSE INC(memBad) END END;
			IF ~ok THEN PutText("driver.fail", "deposit1") END
		END;
		IF good THEN
			PutText("svc.add3", "1");
			IF ~WaitSvcVoters(4, 30000) THEN good := FALSE; PutText("driver.fail", "voters4") END
		END;
		IF good THEN
			PutText("svc.add4", "1");
			IF ~WaitSvcVoters(5, 30000) THEN good := FALSE; PutText("driver.fail", "voters5") END
		END;
		IF good THEN
			PutText("startG3", "1");
			IF ~WaitGrainAlive(4, {}, 60000) THEN good := FALSE; PutText("driver.fail", "grains4") END
		END;
		IF good THEN
			PutText("leave", "G1");
			IF ~WaitGrainAlive(3, {1}, 30000) THEN good := FALSE; PutText("driver.fail", "leave") END
		END;
		IF good THEN
			ActorGrain.Owner(memCl, "acct", key, owner);
			IF owner = "G1" THEN good := FALSE; PutText("driver.fail", "owner-g1") END
		END;
		IF good THEN
			ok := MemDeposit(ref, 3, 25, 40000); good := ok;
			IF ok THEN INC(memOks) ELSE INC(memBad) END;
			IF ~ok THEN PutText("driver.fail", "deposit3") END
		END;
		IF good THEN
			IF owner = role THEN	(* владелец — сам водитель: взять ключ, которым владеет G2, и новый адрес *)
				IF ~KeyOwnedByRole("G2", key) THEN good := FALSE; PutText("driver.fail", "key-g2")
				ELSE
					ref := ActorGrain.Ref(memCl, "acct", key); memExpect := 0;
					PutText("driver.newkey", key);
					ActorGrain.Owner(memCl, "acct", key, owner)
				END
			END;
			IF good & (owner = role) THEN good := FALSE; PutText("driver.fail", "owner-self") END;
			IF good THEN
				PutText("kill-owner", owner);
				IF ~WaitFileTask("killed", 30000) THEN good := FALSE; PutText("driver.fail", "kill-owner") END
			END;
			IF good THEN
				skipKill := {1};
				IF owner = "G2" THEN INCL(skipKill, 2)
				ELSIF owner = "G3" THEN INCL(skipKill, 3)
				END
			END
		END;
		IF good THEN
			IF ~WaitGrainAlive(2, skipKill, 60000) THEN good := FALSE; PutText("driver.fail", "grains2") END
		END;
		IF good THEN
			ok := MemDeposit(ref, 4, 5, 60000); good := ok;
			IF ok THEN INC(memOks) ELSE INC(memBad) END;
			IF ~ok THEN PutText("driver.fail", "deposit4") END
		END;
		IF good THEN
			i := StatusInt("S0.status", "leader", -1);
			IF i < 0 THEN i := StatusInt("S1.status", "leader", -1) END;
			IF i < 0 THEN i := StatusInt("S2.status", "leader", -1) END;
			IF i < 0 THEN i := StatusInt("S3.status", "leader", -1) END;
			IF i < 0 THEN i := StatusInt("S4.status", "leader", -1) END;
			IF i < 0 THEN good := FALSE; PutText("driver.fail", "no-leader")
			ELSE
				IntToText(i, n16); roleName := "S" + n16$;
				PutText("kill-svc", roleName);
				IF ~WaitFileTask("killed-svc", 30000) THEN good := FALSE; PutText("driver.fail", "kill-svc") END
			END
		END;
		IF good THEN
			ok := MemDeposit(ref, 5, 5, 60000); good := ok;
			IF ok THEN INC(memOks) ELSE INC(memBad) END;
			IF ~ok THEN PutText("driver.fail", "deposit5") END
		END;
		IF good THEN
			PutText("svc.remove4", "1");
			IF ~WaitSvcVoters(4, 30000) THEN good := FALSE; PutText("driver.fail", "remove4") END
		END;
		s := "oks="; IntToText(memOks, n16); s := s + n16$;
		IntToText(memBad, n16); s := s + " bad=" + n16$;
		IntToText(memBalance, n16); s := s + " balance=" + n16$;
		IntToText(ActorGrain.AliveCount(memCl), n16); s := s + " alive=" + n16$;
		IF good THEN s := s + " good=1" ELSE s := s + " good=0" END;
		PutText("G0.result", s);
		memDone := TRUE; finished := TRUE
	END MembershipDriver;

	(* --- диспетчер ------------------------------------------------------------- *)

	PROCEDURE Run*;
	BEGIN
		stop := FALSE; finished := FALSE;
		GetPar("DIR", dir); GetPar("ROLE", role); GetPar("SCENARIO", scenario);
		idx := GetInt("IDX", 0); n := GetInt("N", 0); tickMs := GetInt("TICK", 20);
		burst := GetInt("BURST", 300);
		ASSERT(dir # "", 20); ASSERT(role # "", 21); ASSERT(scenario # "", 22);
		t0 := Services.Ticks();
		LoadNodes();
		ActorRun.Start(4);
		IF scenario = "ask" THEN
			IF role = "E" THEN RunEcho ELSIF role = "Q" THEN RunAsker END
		ELSIF scenario = "raft" THEN
			nodes := n;
			IF role = "C" THEN RunClient
			ELSE
				idx := GetInt("IDX", 0);
				RunRaft
			END
		ELSIF scenario = "saga" THEN
			IF role = "C" THEN RunSagaCoord ELSE RunAccount(role) END
		ELSIF scenario = "grain" THEN
			nodes := n; idx := GetInt("IDX", 0);
			RunGrain
		ELSIF scenario = "membership" THEN
			idx := GetInt("IDX", 0);
			IF role[0] = "S" THEN RunMembershipService ELSE RunMembershipGrain END
		END
	END Run;

END Actor_devCluster.
