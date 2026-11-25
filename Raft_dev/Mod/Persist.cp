MODULE Raft_devPersist;
(** Тесты стойких акторов на журнале Raft (RaftJournal) по зёрнам.

	Три узла RaftNode с автоматом журнала (RaftJournal.NewMachine) на сети
	ActorNetMem с потерей, дублем, задержкой и перестановкой кадров;
	исполнитель — ActorSim, поэтому прогон — функция зерна. Стойкий актор
	— счётчик Acct (ActorPersist) на узле P, ребёнок корневого актора
	узла: падает вместе с узлом. Изредка поднимается узел Q со вторым
	воплощением того же потока — два воплощения пишут наперегонки, как
	при разделении сети у ActorGrain. Клиенты — акторы узла K: шлют
	приращения через сеть и ждут подтверждения. У каждого воплощения свой
	клиент журнала RaftJournal.Client без своих часов: тики шлёт водитель.
	Сбои: потери и разрывы сети, падение узла Raft (чаще — лидера) и
	подъём из стойкого хранилища, отказ хранилища посреди Ready, падение и
	подъём узла P, второе воплощение на Q.

	Эталон журнала — тот же автомат, применённый к решённым записям Raft по
	порядку индексов (ref). Инварианты:
	- Raft: один лидер на срок, совпадение журналов, полнота нового
	  лидера, одна запись под индексом;
	- подтверждённое актору событие записано: в Persisted событие стоит в
	  эталоне под своим номером; приращение, на которое клиент получил
	  ответ, есть в эталоне в конце прогона;
	- после восстановления состояние актора — свёртка первых seq событий
	  эталона;
	- воплощения не пишут вперемешку: каждое событие эталона записано по
	  состоянию сразу перед ним (поле prev = номер - 1);
	- живость: после лечения сети запись снова подтверждается.

	Мутации (RaftJournal.mutNoWait — «записано» до фиксации,
	ActorPersist.mutNoStash — следующее письмо до ответа журнала,
	RaftJournal.mutNoExpected — автомат не сверяет expected) обязаны
	ловиться. *)

	IMPORT Services, Tasks, Strings, Log, Stores,
		ActorCore, ActorJournal, ActorCodec, ActorPersist, ActorNet, ActorNetMem, ActorSim,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, RaftJournal, UnitAssert;

	CONST
		n = 3;
		nClients = 3;
		maxTerm = 100000;
		maxJ = 512;	(* клиентов журнала за прогон *)
		kP = n; kQ = n + 1; kK = n + 2; nIdx = n + 3;	(* номера узлов в таблице разрывов *)
		clientTimeout = 300;	(* ходов до повтора приращения *)

	TYPE
		Flaky = POINTER TO RECORD (RaftStorage.Storage) inner: RaftStorage.Storage END;
		Cut = POINTER TO RECORD (ActorNet.Transport) END;
		Trace = POINTER TO RECORD (RaftNode.Tracer) END;

		(* приращение: op — номер операции клиента, inc — на сколько *)
		Add = POINTER TO RECORD (ActorNet.Msg) op, inc: INTEGER END;
		Ack = POINTER TO RECORD (ActorNet.Msg) op: INTEGER END;

		(* событие: writer — номер воплощения, prev — номер последнего события, который видел обработчик *)
		Added = POINTER TO RECORD (ActorPersist.Event) op, inc, writer: INTEGER; prev: LONGINT END;

		Acct = POINTER TO RECORD (ActorPersist.Actor) v: LONGINT; me: INTEGER END;
		Host = POINTER TO RECORD (ActorCore.Actor) h: INTEGER END;
		Respawn = POINTER TO RECORD (ActorCore.Msg) END;

		Client = POINTER TO RECORD (ActorCore.Actor) c: INTEGER END;
		KRoot = POINTER TO RECORD (ActorCore.Actor) END;
		CTick = POINTER TO RECORD (ActorCore.Msg) END;

		Cl = RECORD busy: BOOLEAN; op, deadline: INTEGER END;

		(* событие эталона *)
		Ev = RECORD op, inc, writer: INTEGER; prev, sum: LONGINT END;
		Collect = POINTER TO RECORD (ActorJournal.Sink) END;

		Driver = POINTER TO RECORD (Tasks.Task) END;

		(* адрес по имени — только для ActorNet.Export *)
		Named = POINTER TO RECORD (ActorCore.Ref) END;

	VAR
		gen, now, runSteps: INTEGER;
		seed, x: LONGINT;
		net: ActorNetMem.Net;
		nodes: ARRAY n OF ActorNet.Node;
		hn: ARRAY 2 OF ActorNet.Node;	(* узлы P и Q *)
		kn: ActorNet.Node;
		cfgs: ARRAY n OF RaftNode.Config;
		up: ARRAY n OF BOOLEAN;
		hostUp: ARRAY 2 OF BOOLEAN;
		seen: ARRAY n OF RaftNode.Node;
		checked: ARRAY n OF INTEGER;
		wasLeader: ARRAY n OF BOOLEAN;
		blocked: ARRAY nIdx, nIdx OF BOOLEAN;
		nref: ARRAY n OF ActorCore.Ref;
		caps: RaftNode.Caps;
		tgt: ARRAY 2 OF ActorCore.Ref;	(* воплощения P и Q с узла K *)
		journals: ARRAY maxJ OF RaftJournal.Client; nj, nextId: INTEGER;
		pendingHost: INTEGER; pendingJ: RaftJournal.Client;
		cref: ARRAY nClients OF ActorCore.Ref;
		cl: ARRAY nClients OF Cl;
		nextClient, nextOp: INTEGER;
		decided: Core.Entries; dlast, maxCommit: INTEGER;
		leaderOf: POINTER TO ARRAY OF INTEGER;
		ref: RaftSm.Sessions; refApplied: INTEGER;
		evs: POINTER TO ARRAY OF Ev; nev: INTEGER;
		acked: POINTER TO ARRAY OF INTEGER; nacked: INTEGER;
		lossy: BOOLEAN;
		failRate: INTEGER;	(* отказ хранилища на Save, в десятитысячных *)
		ok, done: BOOLEAN; why: ARRAY 256 OF CHAR;
		acks*, recovers*, crashes*, hostCrashes*, twins*, deaths*, commits*: INTEGER;

	(* --- служебное --------------------------------------------------------- *)

	PROCEDURE Rand (k: INTEGER): INTEGER;
	BEGIN
		x := x * 6364136223846793005 + 1442695040888963407;
		RETURN SHORT(ASH(x, -33) MOD k)
	END Rand;

	PROCEDURE Violation (IN what: ARRAY OF CHAR; i: LONGINT);
		VAR d, e: ARRAY 32 OF CHAR;
	BEGIN
		IF ok THEN
			ok := FALSE; Strings.IntToString(seed, d); Strings.IntToString(i, e);
			why := "seed " + d$ + ", номер " + e$ + ": " + what$
		END
	END Violation;

	PROCEDURE Same (IN a, b: Core.Entry): BOOLEAN;
		VAR k: INTEGER;
	BEGIN
		IF a.term # b.term THEN RETURN FALSE END;
		IF (a.data = NIL) OR (b.data = NIL) THEN RETURN (a.data = NIL) & (b.data = NIL) END;
		IF LEN(a.data^) # LEN(b.data^) THEN RETURN FALSE END;
		k := 0; WHILE (k < LEN(a.data^)) & (a.data[k] = b.data[k]) DO INC(k) END;
		RETURN k = LEN(a.data^)
	END Same;

	(* --- эталон журнала ------------------------------------------------------ *)

	PROCEDURE (s: Collect) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR st: Stores.Store; res: INTEGER; a: POINTER TO ARRAY OF Ev; k: INTEGER;
	BEGIN
		ActorCodec.dir.Decode(body, len, st, res);
		IF (res # ActorCodec.ok) OR ~(st IS Added) THEN Violation("событие эталона не читается", m.seq); RETURN FALSE END;
		IF nev + 1 >= LEN(evs^) THEN NEW(a, 2 * LEN(evs^)); FOR k := 0 TO nev DO a[k] := evs[k] END; evs := a END;
		INC(nev);
		evs[nev].op := st(Added).op; evs[nev].inc := st(Added).inc; evs[nev].writer := st(Added).writer;
		evs[nev].prev := st(Added).prev; evs[nev].sum := evs[nev - 1].sum + st(Added).inc;
		IF evs[nev].prev # nev - 1 THEN Violation("событие записано не по состоянию перед ним (prev # номер - 1): запись мимо expected или до ответа журнала", nev) END;
		RETURN TRUE
	END Event;

	PROCEDURE Advance;
	(* применить к эталону решённые записи без пропусков *)
		VAR out: Core.Bytes; dup: BOOLEAN; s: Collect; res: INTEGER;
	BEGIN
		WHILE (refApplied < dlast) & (decided[refApplied + 1].term # -1) DO
			INC(refApplied); ref.Apply(refApplied, decided[refApplied].data, out, dup)
		END;
		NEW(s); ref.sm(RaftJournal.Machine).mem.Read("acct", nev + 1, s, res)
	END Advance;

	PROCEDURE Decide (k: INTEGER; IN e: Core.Entry);
		VAR a: Core.Entries; j: INTEGER;
	BEGIN
		IF k > dlast THEN
			IF k >= LEN(decided^) THEN
				NEW(a, 2 * k + 16); FOR j := 0 TO dlast DO a[j] := decided[j] END; decided := a
			END;
			FOR j := dlast + 1 TO k - 1 DO decided[j].term := -1 END;
			dlast := k; decided[k] := e
		ELSIF decided[k].term = -1 THEN decided[k] := e
		ELSIF ~Same(decided[k], e) THEN Violation("под индексом решены две разные записи", k)
		END
	END Decide;

	PROCEDURE (t: Trace) Applied (id, index: INTEGER; IN e: Core.Entry);
	BEGIN
		Decide(index, e); Advance
	END Applied;

	PROCEDURE CheckFold (v, seq: LONGINT; IN what: ARRAY OF CHAR);
	BEGIN
		IF seq > nev THEN Violation(what + ": у актора событий больше, чем в журнале", seq)
		ELSIF evs[seq].sum # v THEN Violation(what + ": состояние актора — не свёртка журнала", seq)
		END
	END CheckFold;

	PROCEDURE (r: Named) Post (m: ActorCore.Msg; sender: ActorCore.Ref);
	END Post;

	PROCEDURE (r: Named) AddWatcher (w: ActorCore.Ref);
	END AddWatcher;

	PROCEDURE (r: Named) RemoveWatcher (w: ActorCore.Ref);
	END RemoveWatcher;

	PROCEDURE (r: Named) Stop;
	END Stop;

	(* --- сообщения и событие ---------------------------------------------------- *)

	PROCEDURE (m: Add) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.op); wr.WriteInt(m.inc)
	END Write;

	PROCEDURE (m: Add) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.op); rd.ReadInt(m.inc)
	END Read;

	PROCEDURE (m: Ack) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.op)
	END Write;

	PROCEDURE (m: Ack) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.op)
	END Read;

	PROCEDURE (e: Added) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0);
		wr.WriteInt(e.op); wr.WriteInt(e.inc); wr.WriteInt(e.writer); wr.WriteLong(e.prev)
	END Externalize;

	PROCEDURE (e: Added) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.op); rd.ReadInt(e.inc); rd.ReadInt(e.writer); rd.ReadLong(e.prev)
	END Internalize;

	(* --- стойкий актор ------------------------------------------------------------ *)

	PROCEDURE (a: Acct) Apply (e: ActorPersist.Event);
	BEGIN
		INC(a.v, e(Added).inc)
	END Apply;

	PROCEDURE (a: Acct) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Added;
	BEGIN
		IF m IS ActorPersist.ReplayComplete THEN INC(recovers); CheckFold(a.v, a.seq, "после восстановления")
		ELSIF m IS Add THEN
			NEW(e); e.op := m(Add).op; e.inc := m(Add).inc; e.writer := a.me; e.prev := a.seq;
			a.Persist(c, e, m(Add).op)
		END
	END Handle;

	PROCEDURE (a: Acct) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
		VAR k: Ack;
	BEGIN
		CheckFold(a.v, a.seq, "после записи");
		IF (a.seq <= nev) & ((evs[a.seq].op # e(Added).op) OR (evs[a.seq].writer # a.me)) THEN
			Violation("подтверждённое актору событие не стоит в журнале под своим номером", a.seq)
		END;
		NEW(k); k.op := e(Added).op; c.Respond(k)
	END Persisted;

	PROCEDURE NewAcct (): ActorCore.Actor;
		VAR a: Acct;
	BEGIN
		NEW(a); a.journal := pendingJ; a.stream := "acct"; a.me := pendingJ.id; RETURN a
	END NewAcct;

	PROCEDURE (h: Host) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR j: RaftJournal.Client; nm: ActorCore.Name;
	BEGIN
		IF (m IS ActorCore.Started) OR (m IS Respawn) THEN
			nm := c.name + "/acct";
			IF (ActorCore.dir.Lookup(ActorCore.node, nm) = NIL) & (nj < maxJ) THEN
				INC(nextId); j := RaftJournal.New(hn[h.h], caps, n, nextId);
				j.tickMs := 0; j.timeoutTicks := 8; j.giveUpTicks := 120;
				journals[nj] := j; INC(nj);
				pendingJ := j;
				IF ActorCore.Spawn(c, ActorCore.NewProps(NewAcct), "acct") = NIL THEN END
			END
		END
	END Receive;

	PROCEDURE NewHost (): ActorCore.Actor;
		VAR h: Host;
	BEGIN
		NEW(h); h.h := pendingHost; RETURN h
	END NewHost;

	(* --- клиенты ------------------------------------------------------------------ *)

	PROCEDURE Send (ctx: ActorCore.Context; c: INTEGER);
		VAR m: Add; h: INTEGER;
	BEGIN
		h := 0; IF hostUp[1] & (Rand(3) = 0) THEN h := 1 END;
		NEW(m); m.op := cl[c].op; m.inc := 1 + cl[c].op MOD 5;
		cl[c].deadline := now + clientTimeout;
		ctx.Tell(tgt[h], m)
	END Send;

	PROCEDURE (k: Client) Receive (ctx: ActorCore.Context; m: ActorCore.Msg);
		VAR c, i: INTEGER; a: POINTER TO ARRAY OF INTEGER;
	BEGIN
		c := k.c;
		IF m IS CTick THEN
			IF ~cl[c].busy THEN INC(nextOp); cl[c].op := nextOp; cl[c].busy := TRUE; Send(ctx, c)
			ELSIF now >= cl[c].deadline THEN Send(ctx, c)	(* повтор той же операции *)
			END
		ELSIF m IS Ack THEN
			IF cl[c].busy & (m(Ack).op = cl[c].op) THEN
				cl[c].busy := FALSE; INC(acks);
				IF nacked = LEN(acked^) THEN NEW(a, 2 * nacked); FOR i := 0 TO nacked - 1 DO a[i] := acked[i] END; acked := a END;
				acked[nacked] := cl[c].op; INC(nacked)
			END
		END
	END Receive;

	PROCEDURE NewClient (): ActorCore.Actor;
		VAR k: Client;
	BEGIN
		NEW(k); k.c := nextClient; RETURN k
	END NewClient;

	PROCEDURE (k: KRoot) Receive (ctx: ActorCore.Context; m: ActorCore.Msg);
		VAR c: INTEGER; nm: ARRAY 4 OF CHAR;
	BEGIN
		IF m IS ActorCore.Started THEN
			FOR c := 0 TO nClients - 1 DO
				nextClient := c; nm := "c0"; nm[1] := CHR(ORD("0") + c);
				cref[c] := ActorCore.Spawn(ctx, ActorCore.NewProps(NewClient), nm)
			END
		END
	END Receive;

	PROCEDURE NewKRoot (): ActorCore.Actor;
		VAR k: KRoot;
	BEGIN
		NEW(k); RETURN k
	END NewKRoot;

	(* --- хранилище с отказами и транспорт с разрывами ---------------------- *)

	PROCEDURE (s: Flaky) Save (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
	BEGIN
		IF lossy & (failRate > 0) & (Rand(10000) < failRate) THEN res := RaftStorage.failed
		ELSE s.inner.Save(nd, r, res)
		END
	END Save;

	PROCEDURE (s: Flaky) Compact (index, term: INTEGER; snap: Core.Bytes; OUT res: INTEGER);
	BEGIN
		s.inner.Compact(index, term, snap, res)
	END Compact;

	PROCEDURE (s: Flaky) Load (OUT term, vote, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER);
	BEGIN
		s.inner.Load(term, vote, snapIndex, snapTerm, snap, log, last, res)
	END Load;

	PROCEDURE Index (IN name: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF nodes[i].name = name THEN RETURN i END END;
		IF hn[0].name = name THEN RETURN kP ELSIF hn[1].name = name THEN RETURN kQ END;
		RETURN kK
	END Index;

	PROCEDURE (t: Cut) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		IF ~blocked[Index(from), Index(to)] THEN net.Send(from, to, frame, len) END
	END Send;

	(* --- инварианты Raft ------------------------------------------------------- *)

	PROCEDURE Live (i: INTEGER): Core.Node;
		VAR a: RaftNode.Node;
	BEGIN
		IF ~up[i] THEN RETURN NIL END;
		a := cfgs[i].actor;
		IF (a = NIL) OR (a.nd = NIL) OR a.broken THEN RETURN NIL END;
		IF a # seen[i] THEN seen[i] := a; checked[i] := a.nd.snapIndex; wasLeader[i] := FALSE END;
		RETURN a.nd
	END Live;

	PROCEDURE CheckLeader (i: INTEGER; nd: Core.Node);
		VAR k: INTEGER; e: Core.Entry;
	BEGIN
		IF nd.role = Core.leader THEN
			IF nd.term < maxTerm THEN
				IF leaderOf[nd.term] = Core.none THEN leaderOf[nd.term] := i
				ELSIF leaderOf[nd.term] # i THEN Violation("два лидера в одном сроке", i)
				END
			END;
			IF ~wasLeader[i] THEN
				wasLeader[i] := TRUE;
				IF nd.last < maxCommit THEN Violation("новый лидер без зафиксированных записей", i)
				ELSE
					FOR k := nd.snapIndex + 1 TO maxCommit DO
						Core.EntryAt(nd, k, e);
						IF (decided[k].term # -1) & ~Same(decided[k], e) THEN
							Violation("новый лидер без зафиксированной записи", k)
						END
					END
				END
			END
		ELSE wasLeader[i] := FALSE
		END
	END CheckLeader;

	PROCEDURE CheckCommit (i: INTEGER; nd: Core.Node);
		VAR k: INTEGER; e: Core.Entry;
	BEGIN
		IF nd.commit > nd.last THEN Violation("commit за концом журнала", i) END;
		FOR k := checked[i] + 1 TO nd.commit DO
			IF (k <= nd.last) & (k > nd.snapIndex) THEN Core.EntryAt(nd, k, e); Decide(k, e) END
		END;
		IF nd.commit > checked[i] THEN checked[i] := nd.commit END;
		IF nd.commit > maxCommit THEN INC(commits, nd.commit - maxCommit); maxCommit := nd.commit END
	END CheckCommit;

	PROCEDURE CheckMatching (a, b: Core.Node);
		VAR i, k, lo: INTEGER; ea, eb: Core.Entry;
	BEGIN
		lo := a.snapIndex; IF b.snapIndex > lo THEN lo := b.snapIndex END;
		i := a.last; IF b.last < i THEN i := b.last END;
		WHILE (i > lo) & (Core.TermAt(a, i) # Core.TermAt(b, i)) DO DEC(i) END;
		FOR k := lo + 1 TO i DO
			Core.EntryAt(a, k, ea); Core.EntryAt(b, k, eb);
			IF ~Same(ea, eb) THEN Violation("журналы расходятся до общей записи", k); RETURN END
		END
	END CheckMatching;

	PROCEDURE CheckAll;
		VAR i, j: INTEGER; nds: ARRAY n OF Core.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			nds[i] := Live(i);
			IF nds[i] # NIL THEN CheckLeader(i, nds[i]); CheckCommit(i, nds[i]) END
		END;
		FOR i := 0 TO n - 1 DO
			FOR j := i + 1 TO n - 1 DO
				IF (nds[i] # NIL) & (nds[j] # NIL) THEN CheckMatching(nds[i], nds[j]) END
			END
		END;
		Advance
	END CheckAll;

	PROCEDURE CheckAcked;
	(* каждое приращение, на которое клиент получил ответ, есть в журнале *)
		VAR a, k: INTEGER; found: BOOLEAN;
	BEGIN
		FOR a := 0 TO nacked - 1 DO
			found := FALSE; k := 1;
			WHILE ~found & (k <= nev) DO found := evs[k].op = acked[a]; INC(k) END;
			IF ~found THEN Violation("подтверждённое клиенту приращение потеряно", acked[a]) END
		END
	END CheckAcked;

	(* --- ходы ------------------------------------------------------------- *)

	PROCEDURE NodeName (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d);
		IF i = kP THEN s := "P" + d$
		ELSIF i = kQ THEN s := "Q" + d$
		ELSIF i = kK THEN s := "K" + d$
		ELSE s := "R" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i)
		END
	END NodeName;

	PROCEDURE Crash (i: INTEGER);
	BEGIN
		ActorNetMem.Crash(net, nodes[i].name); up[i] := FALSE; INC(crashes)
	END Crash;

	PROCEDURE TryRestart (i: INTEGER);
	BEGIN
		IF ~up[i] & (ActorCore.dir.Lookup(ActorCore.node, nodes[i].name) = NIL) THEN
			ActorNet.Up(nodes[i]); nref[i] := RaftNode.Start(cfgs[i]); up[i] := TRUE
		END
	END TryRestart;

	PROCEDURE HostDown (h: INTEGER);
	BEGIN
		IF hostUp[h] THEN ActorNetMem.Crash(net, hn[h].name); hostUp[h] := FALSE; INC(hostCrashes) END
	END HostDown;

	PROCEDURE HostUp (h: INTEGER);
	BEGIN
		IF ~hostUp[h] & (ActorCore.dir.Lookup(ActorCore.node, hn[h].name) = NIL) THEN
			IF ~hn[h].up THEN ActorNet.Up(hn[h]) END;
			pendingHost := h;
			IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewHost), hn[h].name) = NIL THEN END;
			hostUp[h] := TRUE;
			IF h = 1 THEN INC(twins) END
		END
	END HostUp;

	PROCEDURE Respawned (h: INTEGER);
	(* воплощение на узле h остановлено трапом: хозяин поднимет новое *)
		VAR nm: ActorCore.Name; r: ActorCore.Ref; m: Respawn;
	BEGIN
		IF hostUp[h] THEN
			nm := hn[h].name + "/acct"; r := ActorCore.dir.Lookup(ActorCore.node, hn[h].name);
			IF (r # NIL) & (ActorCore.dir.Lookup(ActorCore.node, nm) = NIL) THEN INC(deaths); NEW(m); ActorCore.Tell(r, m) END
		END
	END Respawned;

	PROCEDURE Heal;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO nIdx - 1 DO FOR j := 0 TO nIdx - 1 DO blocked[i, j] := FALSE END END
	END Heal;

	PROCEDURE Partition;
		VAR i, j, a, b, k: INTEGER; side: ARRAY nIdx OF BOOLEAN;
	BEGIN
		Heal; k := Rand(5);
		IF k = 0 THEN
		ELSIF k = 3 THEN a := Rand(n); b := Rand(n); IF a # b THEN blocked[b, a] := TRUE END
		ELSIF k = 4 THEN	(* узел P отрезан от Raft *)
			FOR i := 0 TO n - 1 DO blocked[kP, i] := TRUE; blocked[i, kP] := TRUE END
		ELSE
			FOR i := 0 TO nIdx - 1 DO side[i] := Rand(2) = 0 END;
			FOR i := 0 TO nIdx - 1 DO
				FOR j := 0 TO nIdx - 1 DO IF side[i] # side[j] THEN blocked[i, j] := TRUE END END
			END
		END
	END Partition;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER; nd: Core.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			nd := Live(i); IF (nd # NIL) & (nd.role = Core.leader) THEN RETURN i END
		END;
		RETURN -1
	END Leader;

	PROCEDURE Move;
		VAR k, i, j: INTEGER; t: RaftNode.Tick; ct: CTick;
	BEGIN
		INC(now); k := Rand(1000);
		IF k < 600 THEN	(* сеть и ящики быстрее часов *)
			FOR j := 0 TO 2 DO IF ActorNetMem.Pump(net) THEN END; IF ActorSim.Step() THEN END; IF ActorSim.Step() THEN END END
		ELSIF k < 720 THEN
			i := Rand(n); IF up[i] THEN NEW(t); ActorCore.Tell(nref[i], t) END
		ELSIF k < 800 THEN
			i := Rand(nClients); NEW(ct); ActorCore.Tell(cref[i], ct)
		ELSIF k < 880 THEN
			FOR i := 0 TO nj - 1 DO RaftJournal.Tick(journals[i]) END
		ELSIF k < 900 THEN Respawned(0); Respawned(1)
		ELSIF ~lossy THEN
			FOR j := 0 TO 2 DO IF ActorSim.Step() THEN END END
		ELSIF k < 908 THEN i := Rand(n); IF up[i] THEN Crash(i) END
		ELSIF k < 914 THEN i := Leader(); IF i >= 0 THEN Crash(i) END	(* лидер падает чаще *)
		ELSIF k < 945 THEN i := Rand(n); TryRestart(i)
		ELSIF k < 960 THEN Partition
		ELSIF k < 966 THEN HostDown(0)
		ELSIF k < 985 THEN HostUp(0)
		ELSIF k < 990 THEN HostUp(1)	(* второе воплощение *)
		ELSE HostDown(1)
		END;
		FOR i := 0 TO n - 1 DO	(* сломанный хранилищем узел — как упавший *)
			IF up[i] & (cfgs[i].actor # NIL) & cfgs[i].actor.broken THEN Crash(i) END
		END
	END Move;

	(* --- прогон -------------------------------------------------------------- *)

	PROCEDURE Init;
		VAR i, j: INTEGER; s: ActorCore.Name; cut: Cut; st: Flaky; cap: ARRAY RaftNode.capLen OF CHAR;
			trace: Trace; nm: Named;
	BEGIN
		x := seed * 2654435761 + 88172645463325252; INC(gen);
		ok := TRUE; why := ""; dlast := 0; maxCommit := 0; now := 0; nj := 0; nextOp := 0; nacked := 0;
		NEW(acked, 64); NEW(evs, 64); nev := 0; evs[0].sum := 0;
		NEW(decided, 64); decided[0].term := 0;
		NEW(leaderOf, maxTerm); FOR i := 0 TO maxTerm - 1 DO leaderOf[i] := Core.none END;
		ref := RaftSm.New(RaftJournal.NewMachine()); refApplied := 0;
		Heal; NEW(trace); RaftNode.tracer := trace;
		ActorSim.Install; net := ActorNetMem.New(seed); NEW(cut);
		FOR i := 0 TO n - 1 DO
			NodeName(i, s); nodes[i] := ActorNet.New(s, cut, 1000 + i); ActorNetMem.Add(net, nodes[i]);
			NEW(st); st.inner := RaftStorage.NewMem();
			cfgs[i] := RaftNode.NewConfig(i, n, nodes[i], st, RaftJournal.NewMachine);
			cfgs[i].seed := seed; cfgs[i].compactEvery := 0;
			IF Rand(2) = 0 THEN cfgs[i].compactEvery := 5 + Rand(20) END;
			up[i] := TRUE; seen[i] := NIL; checked[i] := 0; wasLeader[i] := FALSE;
			nref[i] := RaftNode.Start(cfgs[i])
		END;
		FOR i := 0 TO n - 1 DO
			ActorNet.Export(nodes[i], nref[i], cap); caps[i] := cap$;
			FOR j := 0 TO n - 1 DO cfgs[j].peers[i] := cap$ END
		END;
		NodeName(kK, s); kn := ActorNet.New(s, cut, 77); ActorNetMem.Add(net, kn);
		FOR i := 0 TO 1 DO
			NodeName(kP + i, s); hn[i] := ActorNet.New(s, cut, 90 + i); ActorNetMem.Add(net, hn[i]);
			hostUp[i] := FALSE;
			(* пропуск воплощения: метка — от имени, переживает падения узла и смену воплощений *)
			s := hn[i].name + "/acct"; NEW(nm); ActorCore.InitRef(nm, ActorCore.node, s);
			ActorNet.Export(hn[i], nm, cap);
			tgt[i] := ActorNet.Import(kn, cap)
		END;
		FOR i := 0 TO nClients - 1 DO cl[i].busy := FALSE END;
		IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewKRoot), kn.name) = NIL THEN END;
		HostUp(0);
		IF ActorSim.Drain(1000) THEN END
	END Init;

	PROCEDURE Finish;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF up[i] THEN ActorNet.Down(nodes[i]) END END;
		FOR i := 0 TO 1 DO IF hostUp[i] THEN ActorNet.Down(hn[i]) END END;
		ActorNet.Down(kn);
		FOR i := 0 TO nj - 1 DO journals[i].Close; journals[i] := NIL END;
		IF ActorSim.Drain(100000) THEN END;
		RaftNode.tracer := NIL
	END Finish;

	PROCEDURE Body;
		VAR k, i, before: INTEGER;
	BEGIN
		Init; lossy := TRUE;
		net.plan.loss := 30; net.plan.dup := 30; net.plan.delay := 3; net.plan.reorder := TRUE;
		k := 0;
		WHILE ok & (k < runSteps) DO Move; CheckAll; INC(k) END;
		IF ok THEN	(* живость: сеть цела, всё поднято, одно воплощение *)
			lossy := FALSE; Heal; net.plan.loss := 0; net.plan.dup := 0;
			HostDown(1); before := acks; k := 0;
			WHILE ok & (k < 30000) & (acks < before + 3) DO
				FOR i := 0 TO n - 1 DO TryRestart(i) END;
				HostUp(0); Move; CheckAll; INC(k)
			END;
			IF ok & (acks < before + 3) THEN Violation("запись не подтверждается после лечения", 0) END
		END;
		IF ok THEN CheckAcked END;
		Finish
	END Body;

	PROCEDURE (d: Driver) Run;
	BEGIN
		Body; done := TRUE
	END Run;

	PROCEDURE Run* (s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(** Один прогон по зерну s. Пост: res — инварианты держались; msg — что нарушено. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := s; runSteps := steps; done := FALSE; ok := TRUE; why := "";
		NEW(d); Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~done & (Services.Ticks() - t <= 120000) DO Services.actionHook.Step END;
		IF ~done THEN Violation("прогон завис", 0) END;
		res := ok; msg := why$
	END Run;

	PROCEDURE Seeds (from, count, steps: INTEGER; OUT found: INTEGER; OUT msg: ARRAY OF CHAR);
		VAR s: INTEGER; r: BOOLEAN;
	BEGIN
		found := -1; msg := ""; s := from;
		WHILE (found < 0) & (s < from + count) DO
			Run(s, steps, r, msg);
			IF ~r THEN found := s END;
			INC(s)
		END
	END Seeds;

	PROCEDURE Setup*;
	BEGIN
		acks := 0; recovers := 0; crashes := 0; hostCrashes := 0; twins := 0; deaths := 0; commits := 0;
		failRate := 20; RaftJournal.mutations := {}; ActorPersist.mutations := {}
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		RaftJournal.mutations := {}; ActorPersist.mutations := {}; RaftNode.tracer := NIL; failRate := 20
	END Teardown;

	(* --- тесты --------------------------------------------------------------- *)

	PROCEDURE TestSeeds*;
	(* зёрна без мутаций: все инварианты держатся, сбои и второе воплощение случались *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(1, 300, 1500, f, msg);
		UnitAssert.True(f < 0, msg);
		UnitAssert.True(acks > 200, "клиенты получали подтверждения");
		UnitAssert.True(recovers > 200, "воплощения восстанавливались");
		UnitAssert.True(hostCrashes > 20, "узел актора падал");
		UnitAssert.True(twins > 20, "второе воплощение поднималось");
		UnitAssert.True(deaths > 10, "воплощения падали на conflict или неизвестном итоге");
		UnitAssert.True(crashes > 50, "узлы Raft падали")
	END TestSeeds;

	PROCEDURE TestReplayIsExact*;
	(* тот же прогон по тому же зерну *)
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR; a1, c1, k1, d1: INTEGER;
	BEGIN
		Setup; Run(2, 1500, r, msg); a1 := acks; c1 := commits; k1 := crashes; d1 := deaths;
		Setup; Run(2, 1500, r, msg);
		UnitAssert.True(r, msg); UnitAssert.True(a1 > 0, "прогон что-то подтверждает");
		UnitAssert.EqualInt(a1, acks, "те же подтверждения");
		UnitAssert.EqualInt(c1, commits, "те же фиксации");
		UnitAssert.EqualInt(k1, crashes, "те же падения");
		UnitAssert.EqualInt(d1, deaths, "те же гибели воплощений")
	END TestReplayIsExact;

	PROCEDURE Caught (IN name: ARRAY OF CHAR; count, at: INTEGER; OUT seed: INTEGER);
	(* мутация ловится, и первым — на зерне at (без мутации это зерно чистое: TestSeeds) *)
		VAR msg: ARRAY 256 OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		Seeds(1, count, 1500, seed, msg);
		Teardown;
		Strings.IntToString(at, d);
		UnitAssert.True(seed >= 0, "мутация " + name$ + " не поймана");
		UnitAssert.EqualInt(at, seed, "мутация " + name$ + " ловится на зерне " + d$);
		IF seed >= 0 THEN Log.String("мутация " + name$ + " поймана: " + msg$); Log.Ln END
	END Caught;

	PROCEDURE TestMutNoWait*;
	(* «записано» до фиксации: событие подтверждено актору, а в журнале его нет *)
		VAR f: INTEGER;
	BEGIN
		RaftJournal.mutations := {RaftJournal.mutNoWait}; Caught("mutNoWait", 50, 1, f)
	END TestMutNoWait;

	PROCEDURE TestMutNoStash*;
	(* следующее письмо до ответа журнала: обработчик решает по устаревшему состоянию *)
		VAR f: INTEGER;
	BEGIN
		ActorPersist.mutations := {ActorPersist.mutNoStash}; Caught("mutNoStash", 100, 1, f)
	END TestMutNoStash;

	PROCEDURE TestMutNoExpected*;
	(* автомат не сверяет expected: два воплощения пишут вперемешку *)
		VAR f: INTEGER;
	BEGIN
		RaftJournal.mutations := {RaftJournal.mutNoExpected}; Caught("mutNoExpected", 200, 9, f)
	END TestMutNoExpected;

	PROCEDURE Report (count, steps: INTEGER; mut, pmut: SET);
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR; d: ARRAY 32 OF CHAR;
	BEGIN
		Setup; RaftJournal.mutations := mut; ActorPersist.mutations := pmut;
		Seeds(1, count, steps, f, msg); Teardown;
		Strings.IntToString(f, d); Log.String("first failing seed " + d$ + ": " + msg$);
		Strings.IntToString(acks, d); Log.String(", acks " + d$);
		Strings.IntToString(recovers, d); Log.String(", recovers " + d$);
		Strings.IntToString(deaths, d); Log.String(", deaths " + d$);
		Strings.IntToString(twins, d); Log.String(", twins " + d$); Log.Ln
	END Report;

	PROCEDURE Long*;
	(** Долгий прогон: 2000 зёрен по 3000 ходов, затем поиск мутаций на 300 зёрнах. *)
	BEGIN
		Report(2000, 3000, {}, {});
		Report(300, 1500, {RaftJournal.mutNoWait}, {});
		Report(300, 1500, {}, {ActorPersist.mutNoStash});
		Report(300, 1500, {RaftJournal.mutNoExpected}, {})
	END Long;

END Raft_devPersist.
