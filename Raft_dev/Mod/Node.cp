MODULE Raft_devNode;
(** Тесты водителя узла RaftNode: узлы-акторы над ActorNetMem со сбоями от зерна.

	Кластер из 3 или 5 узлов RaftNode на сети ActorNetMem с потерей, дублем,
	задержкой и перестановкой кадров; клиенты — акторы на отдельном узле
	ActorNet. Исполнитель — ActorSim, поэтому прогон — функция зерна. Сбои:
	падение узла (ActorNetMem.Crash) и подъём из стойкого хранилища
	(RaftNode.Start после ActorNet.Up), разрыв сети между узлами (острова,
	в одну сторону) и отказ хранилища посреди Ready (узел ломается и
	падает: несохранённое пропало).

	После каждого хода проверяются те же инварианты, что в Raft_devSim:
	один лидер на срок, совпадение журналов, полнота лидера, одна запись
	под индексом на всех узлах (и среди применённых); в конце — живость
	после лечения, линеаризуемость истории счётчика и главное для клиента:
	команда, на которую клиент получил «да», есть среди зафиксированных.

	Мутации водителя (RaftNode.mutations) обязаны ловиться: отправка до
	сохранения и ответ клиенту до фиксации. *)

	IMPORT Services, Tasks, Strings, Log, AsyncIO,
		ActorCore, ActorNet, ActorNetMem, ActorSim,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, UnitAssert;

	CONST
		maxN = 5;
		nClients = 4;
		maxTerm = 100000;
		clientTimeout = 400;	(* ходов до повтора *)
		idle = 0; inc = 1; read = 2;
		kIdx = maxN;	(* номер узла клиентов в таблице разрывов *)

	TYPE
		Counter = POINTER TO RECORD (RaftSm.StateMachine) v: INTEGER END;

		(* хранилище, которое отказывает по жребию: ничего не сохранено, узел сломан *)
		Flaky = POINTER TO RECORD (RaftStorage.Storage)
			inner: RaftStorage.Storage; i: INTEGER
		END;

		(* транспорт с разрывами: кадр между разорванными узлами пропадает *)
		Cut = POINTER TO RECORD (ActorNet.Transport) END;

		Client = POINTER TO RECORD (ActorCore.Actor) c: INTEGER END;
		KRoot = POINTER TO RECORD (ActorCore.Actor) END;
		CTick = POINTER TO RECORD (ActorCore.Msg) END;

		Trace = POINTER TO RECORD (RaftNode.Tracer) END;

		Cl = RECORD
			state, seq, tag, op, inv, deadline, hint: INTEGER
		END;

		Op = RECORD kind, inv, resp, v, client, seq: INTEGER; done: BOOLEAN END;

		Driver = POINTER TO RECORD (Tasks.Task) END;

	VAR
		n, gen: INTEGER;
		net: ActorNetMem.Net;
		nodes: ARRAY maxN OF ActorNet.Node;
		kn: ActorNet.Node;
		cfgs: ARRAY maxN OF RaftNode.Config;
		up: ARRAY maxN OF BOOLEAN;	(* узел ActorNet поднят *)
		seen: ARRAY maxN OF RaftNode.Node;	(* воплощение, которое сверяем *)
		checked: ARRAY maxN OF INTEGER;
		wasLeader: ARRAY maxN OF BOOLEAN;
		blocked: ARRAY maxN + 1, maxN + 1 OF BOOLEAN;
		nref: ARRAY maxN OF ActorCore.Ref;	(* адреса акторов узлов, местные *)
		tgt: ARRAY maxN OF ActorCore.Ref;	(* адреса узлов с узла клиентов *)
		cref: ARRAY nClients OF ActorCore.Ref;
		cl: ARRAY nClients OF Cl;
		nextClient: INTEGER;
		ops: POINTER TO ARRAY OF Op; nops: INTEGER;
		leaderOf: POINTER TO ARRAY OF INTEGER;
		decided: Core.Entries; dlast, maxCommit: INTEGER;
		now: INTEGER;
		seed, x: LONGINT;
		lossy: BOOLEAN;
		failRate: INTEGER;	(* отказ хранилища на Save, в десятитысячных *)
		ok, done: BOOLEAN; why: ARRAY 256 OF CHAR;
		acks*, commits*, crashes*, breaks*: INTEGER;
		runSize, runSteps: INTEGER;

	(* --- служебное --------------------------------------------------------- *)

	PROCEDURE Rand (k: INTEGER): INTEGER;
	BEGIN
		x := x * 6364136223846793005 + 1442695040888963407;
		RETURN SHORT(ASH(x, -33) MOD k)
	END Rand;

	PROCEDURE Violation (IN what: ARRAY OF CHAR; i: INTEGER);
		VAR d, e: ARRAY 32 OF CHAR;
	BEGIN
		IF ok THEN
			ok := FALSE; Strings.IntToString(seed, d); Strings.IntToString(i, e);
			why := "seed " + d$ + ", узел/индекс " + e$ + ": " + what$
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
		Decide(index, e)
	END Applied;

	PROCEDURE Val (r: Core.Bytes): INTEGER;
	BEGIN
		IF (r = NIL) OR (LEN(r^) < 4) THEN RETURN -1 END;
		RETURN r[0] MOD 256 + r[1] MOD 256 * 100H + r[2] MOD 256 * 10000H
	END Val;

	PROCEDURE Bytes4 (v: INTEGER): Core.Bytes;
		VAR r: Core.Bytes;
	BEGIN
		NEW(r, 4); r[0] := SHORT(SHORT(v MOD 256)); r[1] := SHORT(SHORT(v DIV 256 MOD 256));
		r[2] := SHORT(SHORT(v DIV 10000H MOD 256)); r[3] := 0;
		RETURN r
	END Bytes4;

	(* --- автомат: счётчик ------------------------------------------------ *)

	PROCEDURE (c: Counter) Apply (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Core.Bytes;
	BEGIN
		INC(c.v); RETURN Bytes4(c.v)
	END Apply;

	PROCEDURE (c: Counter) Snapshot (): Core.Bytes;
	BEGIN
		RETURN Bytes4(c.v)
	END Snapshot;

	PROCEDURE (c: Counter) Restore (b: Core.Bytes);
	BEGIN
		IF b = NIL THEN c.v := 0 ELSE c.v := Val(b) END
	END Restore;

	PROCEDURE NewCounter (): RaftSm.StateMachine;
		VAR c: Counter;
	BEGIN
		NEW(c); RETURN c
	END NewCounter;

	(* --- хранилище с отказами и транспорт с разрывами ---------------------- *)

	PROCEDURE (s: Flaky) Save (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
	BEGIN
		IF lossy & (failRate > 0) & (Rand(10000) < failRate) THEN res := RaftStorage.failed
		ELSE s.inner.Save(nd, r, res)
		END
	END Save;

	PROCEDURE (s: Flaky) Compact (index, term: INTEGER; snap: Core.Bytes;
		voters, learners: SET; OUT res: INTEGER);
	BEGIN
		s.inner.Compact(index, term, snap, voters, learners, res)
	END Compact;

	PROCEDURE (s: Flaky) Load (OUT term, vote, commit, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER; OUT voters, learners: SET; OUT hasConf: BOOLEAN);
	BEGIN
		s.inner.Load(term, vote, commit, snapIndex, snapTerm, snap, log, last, res, voters, learners, hasConf)
	END Load;

	PROCEDURE Index (IN name: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF nodes[i].name = name THEN RETURN i END END;
		RETURN kIdx
	END Index;

	PROCEDURE (t: Cut) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
	BEGIN
		IF ~blocked[Index(from), Index(to)] THEN net.Send(from, to, frame, len) END
	END Send;

	(* --- история операций ------------------------------------------------- *)

	PROCEDURE AddOp (kind, inv, client, seq: INTEGER): INTEGER;
		VAR a: POINTER TO ARRAY OF Op; k: INTEGER;
	BEGIN
		IF nops = LEN(ops^) THEN NEW(a, 2 * nops); FOR k := 0 TO nops - 1 DO a[k] := ops[k] END; ops := a END;
		ops[nops].kind := kind; ops[nops].inv := inv; ops[nops].done := FALSE;
		ops[nops].client := client; ops[nops].seq := seq; INC(nops);
		RETURN nops - 1
	END AddOp;

	PROCEDURE Respond (c, v: INTEGER);
		VAR k: INTEGER;
	BEGIN
		k := cl[c].op;
		ops[k].resp := now; ops[k].v := v; ops[k].done := TRUE;
		IF ops[k].kind = inc THEN INC(acks) END;
		cl[c].state := idle
	END Respond;

	(* --- клиенты ------------------------------------------------------------ *)

	PROCEDURE Submit (k: Client; ctx: ActorCore.Context);
		VAR c, i: INTEGER; p: RaftNode.Propose; q: RaftNode.Query;
	BEGIN
		c := k.c;
		IF cl[c].hint >= 0 THEN i := cl[c].hint ELSE i := ActorNetMem.Rand(net, n) END;
		cl[c].hint := -1;
		cl[c].deadline := now + clientTimeout;
		IF cl[c].state = inc THEN
			NEW(p); p.client := c + 1; p.seq := cl[c].seq; NEW(p.body, 1); p.body[0] := ORD("i");
			ctx.Tell(tgt[i], p)
		ELSE
			INC(cl[c].tag); NEW(q); q.tag := cl[c].tag; ctx.Tell(tgt[i], q)
		END
	END Submit;

	PROCEDURE (k: Client) Receive (ctx: ActorCore.Context; m: ActorCore.Msg);
		VAR c: INTEGER; r: RaftNode.Reply;
	BEGIN
		c := k.c;
		IF m IS CTick THEN
			IF cl[c].state = idle THEN
				cl[c].inv := now;
				IF ActorNetMem.Rand(net, 10) < 6 THEN
					cl[c].state := inc; INC(cl[c].seq); cl[c].op := AddOp(inc, now, c + 1, cl[c].seq)
				ELSE cl[c].state := read; cl[c].op := AddOp(read, now, c + 1, 0)
				END;
				Submit(k, ctx)
			ELSIF now >= cl[c].deadline THEN Submit(k, ctx)	(* повтор: та же операция, тот же номер *)
			END
		ELSIF m IS RaftNode.Reply THEN
			r := m(RaftNode.Reply);
			IF (cl[c].state = inc) & (r.tag = cl[c].seq) OR (cl[c].state = read) & (r.tag = cl[c].tag) THEN
				IF r.res = RaftNode.ok THEN Respond(c, Val(r.data))
				ELSIF r.res = RaftNode.notLeader THEN
					IF (r.leader >= 0) & (r.leader < n) THEN cl[c].hint := r.leader END;
					cl[c].deadline := now + 5
				ELSE cl[c].deadline := now
				END
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

	(* --- инварианты ------------------------------------------------------- *)

	PROCEDURE Live (i: INTEGER): Core.Node;
	(* автомат живого узла i или NIL *)
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
		VAR i, j: INTEGER; nds: ARRAY maxN OF Core.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			nds[i] := Live(i);
			IF nds[i] # NIL THEN CheckLeader(i, nds[i]); CheckCommit(i, nds[i]) END
		END;
		FOR i := 0 TO n - 1 DO
			FOR j := i + 1 TO n - 1 DO
				IF (nds[i] # NIL) & (nds[j] # NIL) THEN CheckMatching(nds[i], nds[j]) END
			END
		END
	END CheckAll;

	PROCEDURE CheckHistory;
	(* линеаризуемость истории счётчика (как в Raft_devSim); операции без
		значения (ответ без результата) в ней не участвуют *)
		VAR a, b, k, cnt: INTEGER;
	BEGIN
		FOR a := 0 TO nops - 1 DO
			IF ops[a].done & (ops[a].v >= 0) THEN
				IF ops[a].kind = read THEN
					cnt := 0;
					FOR k := 0 TO nops - 1 DO IF (ops[k].kind = inc) & (ops[k].inv <= ops[a].resp) THEN INC(cnt) END END;
					IF ops[a].v > cnt THEN Violation("чтение видит приращений больше, чем начато", a) END
				END;
				FOR b := 0 TO nops - 1 DO
					IF (b # a) & ops[b].done & (ops[b].v >= 0) THEN
						IF (ops[a].kind = inc) & (ops[b].kind = inc) & (ops[a].v = ops[b].v) THEN
							Violation("два приращения вернули одно значение", a)
						END;
						IF ops[a].resp < ops[b].inv THEN
							IF (ops[b].kind = inc) & (ops[a].v >= ops[b].v) OR (ops[b].kind = read) & (ops[a].v > ops[b].v) THEN
								Violation("порядок значений спорит с реальным временем", a)
							END
						END
					END
				END
			END
		END
	END CheckHistory;

	PROCEDURE CheckAcked;
	(* каждая команда, на которую клиент получил «да», зафиксирована *)
		VAR a, k, c, q: INTEGER; found: BOOLEAN;
	BEGIN
		FOR a := 0 TO nops - 1 DO
			IF ops[a].done & (ops[a].kind = inc) THEN
				found := FALSE; k := 1;
				WHILE ~found & (k <= dlast) DO
					IF decided[k].term # -1 THEN
						RaftSm.Decode(decided[k].data, c, q);
						found := (c = ops[a].client) & (q = ops[a].seq)
					END;
					INC(k)
				END;
				IF ~found THEN Violation("подтверждённая клиенту команда потеряна", a) END
			END
		END
	END CheckAcked;

	(* --- ходы ------------------------------------------------------------- *)

	PROCEDURE NodeName (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d);
		IF i = kIdx THEN s := "K" + d$ ELSE s := "R" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i) END
	END NodeName;

	PROCEDURE Crash (i: INTEGER);
	BEGIN
		ActorNetMem.Crash(net, nodes[i].name); up[i] := FALSE; INC(crashes)
	END Crash;

	PROCEDURE TryRestart (i: INTEGER);
	(* подъём узла i, если его прежний актор уже остановлен *)
	BEGIN
		IF ~up[i] & (ActorCore.dir.Lookup(ActorCore.node, nodes[i].name) = NIL) THEN
			ActorNet.Up(nodes[i]); nref[i] := RaftNode.Start(cfgs[i]); up[i] := TRUE
		END
	END TryRestart;

	PROCEDURE Heal;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO maxN DO FOR j := 0 TO maxN DO blocked[i, j] := FALSE END END
	END Heal;

	PROCEDURE Partition;
		VAR i, j, a, b, k: INTEGER; side: ARRAY maxN OF BOOLEAN;
	BEGIN
		Heal; k := Rand(4);
		IF k = 0 THEN
		ELSIF k = 3 THEN a := Rand(n); b := Rand(n); IF a # b THEN blocked[b, a] := TRUE END
		ELSE
			FOR i := 0 TO n - 1 DO side[i] := Rand(2) = 0 END;
			FOR i := 0 TO n - 1 DO
				FOR j := 0 TO n - 1 DO IF side[i] # side[j] THEN blocked[i, j] := TRUE END END
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
		IF k < 650 THEN	(* сеть и ящики быстрее часов *)
			FOR j := 0 TO 2 DO IF ActorNetMem.Pump(net) THEN END; IF ActorSim.Step() THEN END; IF ActorSim.Step() THEN END END
		ELSIF k < 820 THEN
			i := Rand(n); IF up[i] THEN NEW(t); ActorCore.Tell(nref[i], t) END
		ELSIF k < 930 THEN
			i := Rand(nClients); NEW(ct); ActorCore.Tell(cref[i], ct)
		ELSIF ~lossy THEN
			FOR j := 0 TO 2 DO IF ActorSim.Step() THEN END END
		ELSIF k < 940 THEN i := Rand(n); IF up[i] THEN Crash(i) END
		ELSIF k < 948 THEN i := Leader(); IF i >= 0 THEN Crash(i) END	(* лидер падает чаще *)
		ELSIF k < 985 THEN i := Rand(n); TryRestart(i)
		ELSE Partition
		END;
		FOR i := 0 TO n - 1 DO	(* сломанный хранилищем узел — как упавший *)
			IF up[i] & (cfgs[i].actor # NIL) & cfgs[i].actor.broken THEN Crash(i); INC(breaks) END
		END
	END Move;

	(* --- прогон -------------------------------------------------------------- *)

	PROCEDURE Init (size: INTEGER);
		VAR i: INTEGER; s: ActorCore.Name; cut: Cut; st: Flaky; cap: ARRAY RaftNode.capLen OF CHAR; j: INTEGER;
			trace: Trace;
	BEGIN
		n := size; x := seed * 2654435761 + 88172645463325252; INC(gen);
		ok := TRUE; why := ""; dlast := 0; maxCommit := 0; now := 0; nops := 0; NEW(ops, 256);
		NEW(decided, 64); decided[0].term := 0;
		NEW(leaderOf, maxTerm); FOR i := 0 TO maxTerm - 1 DO leaderOf[i] := Core.none END;
		Heal; NEW(trace); RaftNode.tracer := trace;
		ActorSim.Install; net := ActorNetMem.New(seed); NEW(cut);
		FOR i := 0 TO n - 1 DO
			NodeName(i, s); nodes[i] := ActorNet.New(s, cut, 1000 + i); ActorNetMem.Add(net, nodes[i]);
			NEW(st); st.inner := RaftStorage.NewMem(); st.i := i;
			cfgs[i] := RaftNode.NewConfig(i, n, nodes[i], st, NewCounter);
			cfgs[i].seed := seed; cfgs[i].compactEvery := 0;
			IF Rand(2) = 0 THEN cfgs[i].compactEvery := 5 + Rand(20) END;
			up[i] := TRUE; seen[i] := NIL; checked[i] := 0; wasLeader[i] := FALSE;
			nref[i] := RaftNode.Start(cfgs[i])
		END;
		FOR i := 0 TO n - 1 DO
			ActorNet.Export(nodes[i], nref[i], cap);
			FOR j := 0 TO n - 1 DO cfgs[j].peers[i] := cap$ END
		END;
		NodeName(kIdx, s); kn := ActorNet.New(s, cut, 77); ActorNetMem.Add(net, kn);
		FOR i := 0 TO n - 1 DO ActorNet.Export(nodes[i], nref[i], cap); tgt[i] := ActorNet.Import(kn, cap) END;
		FOR i := 0 TO nClients - 1 DO cl[i].state := idle; cl[i].seq := 0; cl[i].tag := 0; cl[i].hint := -1 END;
		IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewKRoot), kn.name) = NIL THEN END;
		IF ActorSim.Drain(1000) THEN END
	END Init;

	PROCEDURE Finish;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF up[i] THEN ActorNet.Down(nodes[i]) END END;
		ActorNet.Down(kn); IF ActorSim.Drain(100000) THEN END;
		RaftNode.tracer := NIL
	END Finish;

	PROCEDURE Body (size, steps: INTEGER);
		VAR k, i, before: INTEGER;
	BEGIN
		Init(size); lossy := TRUE;
		net.plan.loss := 30; net.plan.dup := 30; net.plan.delay := 3; net.plan.reorder := TRUE;
		k := 0;
		WHILE ok & (k < steps) DO Move; CheckAll; INC(k) END;
		IF ok THEN	(* живость: сеть цела, все подняты, без потерь и отказов *)
			lossy := FALSE; Heal; net.plan.loss := 0; net.plan.dup := 0;
			before := maxCommit; k := 0;
			WHILE ok & (k < 30000) & (maxCommit <= before + 2) DO
				FOR i := 0 TO n - 1 DO TryRestart(i) END;
				Move; CheckAll; INC(k)
			END;
			IF ok & (maxCommit <= before + 2) THEN Violation("кластер не фиксирует после лечения", 0) END;
			k := 0;	(* догнать применение на всех узлах *)
			WHILE ok & (k < 3000) DO Move; CheckAll; INC(k) END
		END;
		IF ok THEN CheckHistory END;
		IF ok THEN CheckAcked END;
		Finish
	END Body;

	PROCEDURE RunBody;
	BEGIN
		Body(runSize, runSteps)
	END RunBody;

	PROCEDURE (d: Driver) Run;
	BEGIN
		RunBody; done := TRUE
	END Run;

	PROCEDURE Run* (size: INTEGER; s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(** Один прогон по зерну s. Пост: res — инварианты держались; msg — что нарушено. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := s; runSize := size; runSteps := steps; done := FALSE; ok := TRUE; why := "";
		NEW(d); Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~done & (Services.Ticks() - t <= 120000) DO Services.actionHook.Step; AsyncIO.Poll(0) END;
		IF ~done THEN Violation("прогон завис", 0) END;
		res := ok; msg := why$
	END Run;

	PROCEDURE Seeds (size, from, count, steps: INTEGER; mut: SET; OUT found: INTEGER; OUT msg: ARRAY OF CHAR);
		VAR s: INTEGER; r: BOOLEAN;
	BEGIN
		RaftNode.mutations := mut; found := -1; msg := ""; s := from;
		WHILE (found < 0) & (s < from + count) DO
			Run(size, s, steps, r, msg);
			IF ~r THEN found := s END;
			INC(s)
		END;
		RaftNode.mutations := {}
	END Seeds;

	PROCEDURE Setup*;
	BEGIN
		acks := 0; commits := 0; crashes := 0; breaks := 0; failRate := 20
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		RaftNode.mutations := {}; RaftNode.tracer := NIL; failRate := 20
	END Teardown;

	(* --- тесты --------------------------------------------------------------- *)

	PROCEDURE TestThreeNodes*;
	(* 3 узла, 200 зёрен: инварианты, живость, линеаризуемость, подтверждённое не теряется. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(3, 1, 200, 3000, {}, f, msg);
		UnitAssert.True(f < 0, msg);
		UnitAssert.True(acks > 100, "клиенты получали ответы");
		UnitAssert.True(crashes > 30, "узлы падали");
		UnitAssert.True(breaks > 0, "хранилище отказывало")
	END TestThreeNodes;

	PROCEDURE TestFiveNodes*;
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(5, 500, 60, 3000, {}, f, msg);
		UnitAssert.True(f < 0, msg);
		UnitAssert.True(acks > 50, "клиенты получали ответы")
	END TestFiveNodes;

	PROCEDURE TestReplayIsExact*;
	(* тот же прогон по тому же зерну *)
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR; a1, c1, k1: INTEGER;
	BEGIN
		Setup; Run(3, 7, 2000, r, msg); a1 := acks; c1 := commits; k1 := crashes;
		Setup; Run(3, 7, 2000, r, msg);
		UnitAssert.True(r, msg); UnitAssert.True(c1 > 5, "прогон что-то фиксирует");
		UnitAssert.EqualInt(c1, commits, "те же фиксации");
		UnitAssert.EqualInt(a1, acks, "те же ответы");
		UnitAssert.EqualInt(k1, crashes, "те же падения")
	END TestReplayIsExact;

	PROCEDURE Caught (mut, size, count: INTEGER);
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		Seeds(size, 1, count, 3000, {mut}, f, msg);
		Strings.IntToString(mut, d);
		UnitAssert.True(f >= 0, "мутация водителя " + d$ + " не поймана");
		IF f >= 0 THEN Log.String("мутация водителя " + d$ + " поймана: " + msg$); Log.Ln END
	END Caught;

	PROCEDURE TestMutSendFirst*;
	(* отправка до сохранения: голос или подтверждение записи уходят, узел
		падает, не сохранив, — второй голос в сроке или потеря зафиксированного *)
	BEGIN
		failRate := 200; Caught(RaftNode.mutSendFirst, 3, 300)
	END TestMutSendFirst;

	PROCEDURE TestSendFirstSafeAtSameRate*;
	(* те же зёрна и частота отказов хранилища без мутации — без нарушений *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		failRate := 200; Seeds(3, 1, 40, 3000, {}, f, msg);
		UnitAssert.True(f < 0, msg)
	END TestSendFirstSafeAtSameRate;

	PROCEDURE TestMutEarlyReply*;
	(* ответ клиенту до фиксации: лидер падает, запись стёрта — «да» без записи *)
	BEGIN
		Caught(RaftNode.mutEarlyReply, 3, 100)
	END TestMutEarlyReply;

	PROCEDURE Report (mut: SET; rate, size, count: INTEGER);
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR; d: ARRAY 32 OF CHAR;
	BEGIN
		Setup; failRate := rate; Seeds(size, 1, count, 3000, mut, f, msg);
		Strings.IntToString(f, d); Log.String("first failing seed " + d$ + ": " + msg$);
		Strings.IntToString(acks, d); Log.String(", acks " + d$);
		Strings.IntToString(breaks, d); Log.String(", breaks " + d$); Log.Ln
	END Report;

	PROCEDURE Long*;
	(** Долгий прогон: 1000 зёрен на 3 узлах и 300 на 5, затем поиск мутаций водителя. *)
	BEGIN
		Report({}, 20, 3, 1000); Report({}, 20, 5, 300); Report({}, 200, 3, 300);
		Report({RaftNode.mutSendFirst}, 200, 3, 300); Report({RaftNode.mutEarlyReply}, 20, 3, 300);
		Teardown
	END Long;

	PROCEDURE Replay*;
	(** Повтор одного прогона: печать итога для зерна seed (по умолчанию 1). *)
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR;
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		Setup; Run(3, 1, 3000, r, msg);
		IF r THEN Log.String("ok") ELSE Log.String(msg) END;
		Strings.IntToString(acks, d); Log.String(" acks " + d$);
		Strings.IntToString(commits, d); Log.String(", commits " + d$);
		Strings.IntToString(crashes, d); Log.String(", crashes " + d$);
		Strings.IntToString(breaks, d); Log.String(", breaks " + d$);
		Strings.IntToString(now, d); Log.String(", now " + d$);
		Log.Ln
	END Replay;

END Raft_devNode.
