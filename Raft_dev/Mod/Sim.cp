MODULE Raft_devSim;
(** Детерминированная симуляция ядра Raft и проверка инвариантов.

	Кластер из 3 или 5 автоматов RaftCore, пул писем с потерей, дублем и
	перестановкой, падения узлов с потерей несохранённого (в том числе
	между сохранением и отправкой), разделения сети (меньшинство,
	большинство, в одну сторону) и перекос часов (у каждого узла свой
	темп тиков). Всё случайное берётся от зерна, поэтому провал
	повторяется шаг в шаг: Replay с переменной RAFT_SEED.

	После каждого шага проверяются инварианты:
	- один лидер на срок;
	- совпадение журналов: если у двух узлов запись i одного срока, то
	  записи 1 .. i у них одинаковы;
	- полнота лидера: новый лидер хранит все зафиксированные записи;
	- безопасность автомата: зафиксированное и применённое на любом узле
	  под индексом i — одна и та же запись, и она не меняется (в том числе
	  не заменяется в журнале узла задним числом);
	- в конце, после лечения сети, кластер снова фиксирует команды.

	Режимы: PreVote и CheckQuorum (RandomRun), смена состава по одному
	узлу (MembershipRun: начальный состав меньше числа узлов, узел входит
	учеником и становится избирателем после догона). *)

	IMPORT Int64, Strings, Dialog, Log, KvPages, KvDb, KvFault, Core := RaftCore, RaftSm, RaftStorage, RaftKvStorage, UnitAssert;

	CONST
		maxN = Core.maxNodes;
		poolSize = 4000;
		maxTerm = 100000;
		electionTicks = 10;
		nClients = 4;
		clientTimeout = 300;	(* ходов до повтора *)
		idle = 0; inc = 1; read = 2;

	TYPE
		Counter = POINTER TO RECORD (RaftSm.StateMachine) v: INTEGER END;

		Client = RECORD
			state, seq, node, index, ctx, confirmed, op, inv, deadline: INTEGER
		END;

		Op = RECORD kind, inv, resp, v: INTEGER; done: BOOLEAN END;

	VAR
		n: INTEGER;	(* всего узлов в прогоне *)
		sms: ARRAY maxN OF RaftSm.Sessions;
		cl: ARRAY nClients OF Client;
		ops: POINTER TO ARRAY OF Op; nops*: INTEGER;
		now, nextCtx: INTEGER;
		nodes: ARRAY maxN OF Core.Node;
		disk: ARRAY maxN OF RaftStorage.Storage;
		kvs: ARRAY maxN OF KvFault.Store;	(* носители узлов в режиме Kv *)
		onKv: BOOLEAN;
		restarts: ARRAY maxN OF INTEGER;
		rate: ARRAY maxN OF INTEGER;	(* темп часов: тиков за ход *)
		checked: ARRAY maxN OF INTEGER;	(* до какого индекса сверена фиксация узла *)
		wasLeader: ARRAY maxN OF BOOLEAN;
		wasLearner: ARRAY maxN OF BOOLEAN;
		blocked: ARRAY maxN, maxN OF BOOLEAN;
		pool: ARRAY poolSize OF Core.Msg; np: INTEGER;
		leaderOf: POINTER TO ARRAY OF INTEGER;
		decided: Core.Entries; dlast, maxCommit: INTEGER;
		x, seed: LONGINT;
		lossy: BOOLEAN;
		compactEvery: INTEGER;	(* снимок, когда после прошлого применено столько записей; 0 — без снимков *)
		installs*: INTEGER;	(* снимков, принятых от лидера *)
		crashNext: INTEGER;	(* узел, который упадёт посреди ближайшего Ready; -1 — никто *)
		harsh: INTEGER;	(* жёсткость сбоев от зерна: 1 .. 3 *)
		batch: INTEGER;
		ok: BOOLEAN; why: ARRAY 256 OF CHAR;
		proposals, commits*: INTEGER;
		(* режимы *)
		initVoters, initLearners: SET;
		preVoteOn, checkQuorumOn, membershipOn: BOOLEAN;
		sessionExpiry: INTEGER;
		confChanges, removes: INTEGER;
		promoted: BOOLEAN;

	PROCEDURE Rand (k: INTEGER): INTEGER;
	BEGIN
		x := Int64.Mul(x, 6364136223846793005); INC(x, 1442695040888963407);
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
	(* одна и та же запись: срок, вид, узел и байты команды (после Kv указатели другие) *)
		VAR k: INTEGER;
	BEGIN
		IF (a.term # b.term) OR (a.kind # b.kind) OR (a.node # b.node) THEN RETURN FALSE END;
		IF (a.data = NIL) OR (b.data = NIL) THEN RETURN a.data = b.data END;
		IF LEN(a.data^) # LEN(b.data^) THEN RETURN FALSE END;
		k := 0; WHILE (k < LEN(a.data^)) & (a.data[k] = b.data[k]) DO INC(k) END;
		RETURN k = LEN(a.data^)
	END Same;

	(* --- решённое: зафиксированное и применённое ------------------------- *)

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

	(* --- счётчик и клиенты ---------------------------------------------- *)

	PROCEDURE (c: Counter) Apply (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): Core.Bytes;
		VAR r: Core.Bytes;
	BEGIN
		INC(c.v); NEW(r, 4); r[0] := SHORT(SHORT(c.v MOD 256)); r[1] := SHORT(SHORT(c.v DIV 256 MOD 256));
		r[2] := SHORT(SHORT(c.v DIV 10000H MOD 256)); r[3] := 0;
		RETURN r
	END Apply;

	PROCEDURE (c: Counter) Snapshot (): Core.Bytes;
		VAR r: Core.Bytes;
	BEGIN
		NEW(r, 4); r[0] := SHORT(SHORT(c.v MOD 256)); r[1] := SHORT(SHORT(c.v DIV 256 MOD 256));
		r[2] := SHORT(SHORT(c.v DIV 10000H MOD 256)); r[3] := 0;
		RETURN r
	END Snapshot;

	PROCEDURE (c: Counter) Restore (b: Core.Bytes);
	BEGIN
		IF b = NIL THEN c.v := 0 ELSE c.v := b[0] MOD 256 + b[1] MOD 256 * 100H + b[2] MOD 256 * 10000H END
	END Restore;

	PROCEDURE Val (r: Core.Bytes): INTEGER;
	BEGIN
		RETURN r[0] MOD 256 + r[1] MOD 256 * 100H + r[2] MOD 256 * 10000H
	END Val;

	PROCEDURE AddOp (kind, inv: INTEGER): INTEGER;
		VAR a: POINTER TO ARRAY OF Op; k: INTEGER;
	BEGIN
		IF nops = LEN(ops^) THEN NEW(a, 2 * nops); FOR k := 0 TO nops - 1 DO a[k] := ops[k] END; ops := a END;
		ops[nops].kind := kind; ops[nops].inv := inv; ops[nops].done := FALSE; INC(nops);
		RETURN nops - 1
	END AddOp;

	PROCEDURE Respond (c, v: INTEGER);
		VAR k: INTEGER;
	BEGIN
		IF cl[c].state = read THEN k := AddOp(read, cl[c].inv) ELSE k := cl[c].op END;
		ops[k].resp := now; ops[k].v := v; ops[k].done := TRUE;
		cl[c].state := idle
	END Respond;

	PROCEDURE Applied (i, k: INTEGER; data, res: Core.Bytes);
	(* узел i применил запись k: ответить клиенту, который её ждёт у этого узла *)
		VAR c, ec, eq: INTEGER;
	BEGIN
		RaftSm.Decode(data, ec, eq);
		FOR c := 0 TO nClients - 1 DO
			IF (cl[c].state = inc) & (cl[c].node = i) & (cl[c].index = k) THEN
				IF (ec = c + 1) & (eq = cl[c].seq) & (res # NIL) THEN Respond(c, Val(res))
				ELSE cl[c].deadline := now	(* запись затёрта: повторить *)
				END
			END
		END
	END Applied;

	PROCEDURE ServeReads (i: INTEGER);
		VAR c: INTEGER;
	BEGIN
		FOR c := 0 TO nClients - 1 DO
			IF (cl[c].state = read) & (cl[c].node = i) & (cl[c].confirmed >= 0) & (nodes[i] # NIL)
				& (nodes[i].applied >= cl[c].confirmed) THEN
				Respond(c, sms[i].sm(Counter).v)
			END
		END
	END ServeReads;

	(* --- узлы ------------------------------------------------------------ *)

	PROCEDURE Persist (i: INTEGER; IN r: Core.Ready): BOOLEAN;
	(* FALSE — питание пропало посреди фиксации Kv *)
		VAR res: INTEGER;
	BEGIN
		IF onKv & lossy & (Rand(1000) < 5 * harsh) THEN kvs[i].Arm(Rand(40), Rand(KvPages.size)) END;
		disk[i].Save(nodes[i], r, res);
		IF onKv THEN kvs[i].Arm(-1, 0) END;
		RETURN res = RaftStorage.ok
	END Persist;

	PROCEDURE Post (m: Core.Msg);
		VAR nx: Core.Msg;
	BEGIN
		WHILE m # NIL DO
			nx := m.next; m.next := NIL;
			IF np < poolSize THEN pool[np] := m; INC(np) END;
			m := nx
		END
	END Post;

	PROCEDURE Crash (i: INTEGER);
		VAR c: INTEGER;
	BEGIN
		nodes[i] := NIL; wasLeader[i] := FALSE; checked[i] := 0;
		IF onKv THEN kvs[i].PowerOff(Rand(1000)) END;	(* несинхронизированное пропало или дошло наполовину *)
		FOR c := 0 TO nClients - 1 DO IF (cl[c].state # idle) & (cl[c].node = i) THEN cl[c].deadline := now END END
	END Crash;

	PROCEDURE Restart (i: INTEGER);
		VAR cnt: Counter; db: KvDb.Db; res, t, v, cm, si, st, last: INTEGER; log: Core.Entries; snap: Core.Bytes;
			voters, learners: SET; hasConf: BOOLEAN;
	BEGIN
		INC(restarts[i]);
		IF onKv THEN KvDb.Open(kvs[i], db, res); ASSERT(res = KvDb.ok, 60); disk[i] := RaftKvStorage.New(db) END;
		disk[i].Load(t, v, cm, si, st, snap, log, last, res, voters, learners, hasConf); ASSERT(res = RaftStorage.ok, 61);
		nodes[i] := Core.New(i, n, seed * 7919 + restarts[i] * 31, electionTicks, t, v, si, st, snap, log, last);
		IF hasConf THEN Core.SetConfig(nodes[i], voters, learners)
		ELSE Core.SetConfig(nodes[i], initVoters, initLearners)
		END;
		Core.SetCommit(nodes[i], cm);
		Core.SetFeatures(nodes[i], preVoteOn, checkQuorumOn);
		NEW(cnt); sms[i] := RaftSm.New(cnt); sms[i].Restore(snap); sms[i].SetExpiry(sessionExpiry); checked[i] := si;
		Core.SetBatch(nodes[i], batch)
	END Restart;

	PROCEDURE Process (i: INTEGER);
	(* выполнить Ready узла; между сохранением и отправкой узел может упасть *)
		VAR r: Core.Ready; k, c, res: INTEGER; e: Core.Entry; nd: Core.Node; out, snap: Core.Bytes; dup: BOOLEAN;
	BEGIN
		nd := nodes[i];
		WHILE (nd # NIL) & Core.HasReady(nd) DO
			Core.GetReady(nd, r);
			IF r.syncFirst THEN
				IF ~Persist(i, r) THEN Crash(i); RETURN END
			ELSE Post(r.msgs)
			END;
			IF lossy & (Rand(10000) < 5 + 10 * (harsh - 1)) OR (crashNext = i) THEN crashNext := -1; Crash(i); RETURN END;
			IF r.syncFirst THEN Post(r.msgs)
			ELSIF ~Persist(i, r) THEN Crash(i); RETURN
			END;
			IF r.snap # NIL THEN sms[i].Restore(r.snap); INC(installs) END;
			FOR k := r.applyLo TO r.applyHi DO
				Core.EntryAt(nd, k, e); Decide(k, e);
				sms[i].Apply(k, e.term, e.data, out, dup); Applied(i, k, e.data, out)
			END;
			FOR k := 0 TO r.nreads - 1 DO
				FOR c := 0 TO nClients - 1 DO
					IF (cl[c].state = read) & (cl[c].node = i) & (cl[c].ctx = r.reads[k].ctx) THEN
						cl[c].confirmed := r.reads[k].index
					END
				END
			END;
			Core.Advance(nd, r);
			IF (compactEvery > 0) & (nd.applied = nd.commit) & (nd.applied - nd.snapIndex >= compactEvery) THEN
				(* снимок и усечение журнала: только применённое и зафиксированное *)
				snap := sms[i].Snapshot(); Core.Compact(nd, nd.applied, snap);
				disk[i].Compact(nd.snapIndex, nd.snapTerm, snap, nd.snapVoters, nd.snapLearners, res);
				IF res # RaftStorage.ok THEN Crash(i); RETURN END
			END;
			ServeReads(i)
		END
	END Process;

	(* --- инварианты ------------------------------------------------------ *)

	PROCEDURE StepAt (i: INTEGER; m: Core.Msg);
	(* принять письмо узлом i; зафиксированный хвост не должен измениться *)
		VAR old: INTEGER; e, e2: Core.Entry; had: BOOLEAN;
	BEGIN
		had := FALSE; old := 0;
		IF (nodes[i] # NIL) & (nodes[i].commit > nodes[i].snapIndex) & (nodes[i].commit <= nodes[i].last) THEN
			old := nodes[i].commit; Core.EntryAt(nodes[i], old, e); had := TRUE
		END;
		Core.Step(nodes[i], m);
		IF had & (nodes[i] # NIL) & (old > nodes[i].snapIndex) THEN
			IF nodes[i].last < old THEN Violation("усечён зафиксированный хвост", old)
			ELSE
				Core.EntryAt(nodes[i], old, e2);
				IF ~Same(e, e2) THEN Violation("зафиксированная запись заменена", old) END
			END
		END;
		Process(i)
	END StepAt;

	PROCEDURE CheckLeader (i: INTEGER);
		VAR nd: Core.Node; k: INTEGER; e: Core.Entry;
	BEGIN
		nd := nodes[i];
		IF nd.role = Core.leader THEN
			IF nd.term < maxTerm THEN
				IF leaderOf[nd.term] = Core.none THEN leaderOf[nd.term] := i
				ELSIF leaderOf[nd.term] # i THEN Violation("два лидера в одном сроке", i)
				END
			END;
			IF ~wasLeader[i] THEN	(* полнота лидера *)
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

	PROCEDURE CheckCommit (i: INTEGER);
		VAR nd: Core.Node; k: INTEGER; e: Core.Entry;
	BEGIN
		nd := nodes[i];
		IF nd.commit > nd.last THEN Violation("commit за концом журнала", i) END;
		FOR k := checked[i] + 1 TO nd.commit DO
			IF (k <= nd.last) & (k > nd.snapIndex) THEN Core.EntryAt(nd, k, e); Decide(k, e) END
		END;
		IF nd.commit > checked[i] THEN checked[i] := nd.commit END;
		(* зафиксированная запись не заменяется: сверяем запись узла под
		   старшим решённым индексом не выше commit — при подмене хвоста
		   (в том числе ошибкой смены состава) она другая *)
		k := nd.commit; IF k > nd.last THEN k := nd.last END;
		IF k > dlast THEN k := dlast END;
		WHILE (k > nd.snapIndex) & (decided[k].term = -1) DO DEC(k) END;
		IF k > nd.snapIndex THEN
			Core.EntryAt(nd, k, e);
			IF ~Same(decided[k], e) THEN Violation("зафиксированная запись заменена", k) END
		END;
		IF nd.commit > maxCommit THEN INC(commits, nd.commit - maxCommit); maxCommit := nd.commit END
	END CheckCommit;

	PROCEDURE CheckMatching (a, b: Core.Node);
		VAR i, k, lo: INTEGER; ea, eb: Core.Entry;
	BEGIN
		lo := a.snapIndex; IF b.snapIndex > lo THEN lo := b.snapIndex END;	(* до lo — снимки, т. е. зафиксированное *)
		i := a.last; IF b.last < i THEN i := b.last END;
		WHILE (i > lo) & (Core.TermAt(a, i) # Core.TermAt(b, i)) DO DEC(i) END;
		FOR k := lo + 1 TO i DO
			Core.EntryAt(a, k, ea); Core.EntryAt(b, k, eb);
			IF ~Same(ea, eb) THEN Violation("журналы расходятся до общей записи", k); RETURN END
		END
	END CheckMatching;

	PROCEDURE NoteMembership (i: INTEGER);
		VAR nd: Core.Node;
	BEGIN
		nd := nodes[i];
		IF i IN nd.learners THEN wasLearner[i] := TRUE END;
		IF wasLearner[i] & (i IN nd.voters) THEN promoted := TRUE END
	END NoteMembership;

	PROCEDURE CheckAll;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO
			IF nodes[i] # NIL THEN CheckLeader(i); CheckCommit(i); NoteMembership(i) END
		END;
		FOR i := 0 TO n - 1 DO
			FOR j := i + 1 TO n - 1 DO
				IF (nodes[i] # NIL) & (nodes[j] # NIL) THEN CheckMatching(nodes[i], nodes[j]) END
			END
		END
	END CheckAll;

	(* --- ходы ------------------------------------------------------------ *)

	PROCEDURE Deliver;
		VAR k, w, j: INTEGER; m, c: Core.Msg;
	BEGIN
		(* перестановка в окне: одно из восьми самых старых писем; задержка
		   ограничена, иначе очередь растёт быстрее тайм-аута выборов *)
		w := np; IF w > 8 THEN w := 8 END;
		k := Rand(w); m := pool[k];
		DEC(np); FOR j := k TO np - 1 DO pool[j] := pool[j + 1] END; pool[np] := NIL;
		IF lossy & (Rand(100) < 4) THEN	(* дубль: копия уходит в конец пула *)
			NEW(c); c^ := m^; c.next := NIL; pool[np] := c; INC(np)
		END;
		IF (nodes[m.to] # NIL) & ~blocked[m.from, m.to] & ~(lossy & (Rand(100) < 4)) THEN
			StepAt(m.to, m)
		END
	END Deliver;

	PROCEDURE TickOne;
		VAR i, k: INTEGER;
	BEGIN
		i := Rand(n);
		IF nodes[i] # NIL THEN
			FOR k := 1 TO rate[i] DO IF nodes[i] # NIL THEN Core.Tick(nodes[i]); Process(i) END END
		END
	END TickOne;

	PROCEDURE Submit (c: INTEGER);
	(* отправить операцию клиента c узлу, который считается лидером *)
		VAR i: INTEGER; body: ARRAY 1 OF BYTE;
	BEGIN
		i := Rand(n);
		IF (nodes[i] # NIL) & (nodes[i].role # Core.leader) & (nodes[i].leader # Core.none) THEN
			i := nodes[i].leader	(* клиента перенаправили на лидера *)
		END;
		cl[c].node := i; cl[c].deadline := now + 20; cl[c].confirmed := -1;
		IF nodes[i] # NIL THEN
			IF cl[c].state = inc THEN
				body[0] := ORD("i");
				cl[c].index := Core.Propose(nodes[i], RaftSm.Encode(c + 1, cl[c].seq, body, 1));
				IF cl[c].index > 0 THEN INC(proposals); cl[c].deadline := now + clientTimeout; Process(i) END
			ELSE
				INC(nextCtx); cl[c].ctx := nextCtx;
				IF Core.ReadIndex(nodes[i], nextCtx) THEN cl[c].deadline := now + clientTimeout; Process(i) END
			END
		END
	END Submit;

	PROCEDURE ClientMove;
		VAR c: INTEGER;
	BEGIN
		c := Rand(nClients);
		IF cl[c].state = idle THEN
			cl[c].inv := now;
			IF Rand(10) < 6 THEN
				cl[c].state := inc; INC(cl[c].seq); cl[c].op := AddOp(inc, now)
			ELSE cl[c].state := read
			END;
			Submit(c)
		ELSIF now >= cl[c].deadline THEN Submit(c)	(* повтор: та же операция, тот же номер *)
		END
	END ClientMove;

	PROCEDURE Heal;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO maxN - 1 DO FOR j := 0 TO maxN - 1 DO blocked[i, j] := FALSE END END
	END Heal;

	PROCEDURE Block (a, b: INTEGER);
	BEGIN
		blocked[a, b] := TRUE; blocked[b, a] := TRUE
	END Block;

	PROCEDURE Partition;
		VAR i, j, a, b, k: INTEGER; side: ARRAY maxN OF BOOLEAN;
	BEGIN
		Heal; k := Rand(4);
		IF k = 0 THEN	(* сеть цела *)
		ELSIF k = 3 THEN	(* в одну сторону: a не слышит b *)
			a := Rand(n); b := Rand(n); IF a # b THEN blocked[b, a] := TRUE END
		ELSE	(* два острова: случайное меньшинство или большинство *)
			FOR i := 0 TO n - 1 DO side[i] := Rand(2) = 0 END;
			FOR i := 0 TO n - 1 DO
				FOR j := 0 TO n - 1 DO IF side[i] # side[j] THEN blocked[i, j] := TRUE END END
			END
		END
	END Partition;

	PROCEDURE TryMembership;
	(* случайная смена состава: добавить не входящий узел или удалить входящий *)
		VAR l, id, k: INTEGER;
	BEGIN
		l := Rand(n);
		IF nodes[l] # NIL THEN
			IF (nodes[l].role # Core.leader) & (nodes[l].leader # Core.none) THEN l := nodes[l].leader END;
			IF (nodes[l] # NIL) & (nodes[l].role = Core.leader) THEN
				k := Rand(3); id := Rand(n);
				IF k = 0 THEN
					IF (id IN nodes[l].voters) OR (id IN nodes[l].learners) THEN
						IF Core.RemoveServer(nodes[l], id) > 0 THEN INC(removes); INC(confChanges); Process(l) END
					END
				ELSE
					IF ~(id IN nodes[l].voters) & ~(id IN nodes[l].learners) THEN
						IF Core.AddServer(nodes[l], id) > 0 THEN INC(confChanges); Process(l) END
					END
				END
			END
		END
	END TryMembership;

	PROCEDURE Move;
		VAR k, i: INTEGER;
	BEGIN
		INC(now); k := Rand(1000);
		IF (k < 700) & (np > 0) THEN	(* сеть быстрее часов: до трёх писем за ход *)
			Deliver; IF np > 0 THEN Deliver END; IF np > 0 THEN Deliver END
		ELSIF k < 850 THEN TickOne
		ELSIF k < 950 THEN
			IF membershipOn & (Rand(8) = 0) THEN TryMembership
			ELSIF lossy OR (Rand(10) = 0) THEN ClientMove
			END
		ELSIF ~lossy THEN TickOne
		ELSIF k < 950 + 5 * harsh THEN i := Rand(n); IF nodes[i] # NIL THEN Crash(i) END
		ELSIF k < 985 THEN i := Rand(n); IF nodes[i] = NIL THEN Restart(i) END
		ELSE Partition
		END
	END Move;

	PROCEDURE CheckHistory;
	(* Линеаризуемость истории счётчика: приращение возвращает новое
	   значение, чтение — текущее. Значения задают порядок операций; история
	   линеаризуема, если он не спорит с реальным временем (a кончилась до
	   начала b) и чтение не видит больше приращений, чем начато до его конца. *)
		VAR a, b, k, cnt: INTEGER;
	BEGIN
		FOR a := 0 TO nops - 1 DO
			IF ops[a].done THEN
				IF ops[a].kind = read THEN
					cnt := 0;
					FOR k := 0 TO nops - 1 DO IF (ops[k].kind = inc) & (ops[k].inv <= ops[a].resp) THEN INC(cnt) END END;
					IF ops[a].v > cnt THEN Violation("чтение видит приращений больше, чем начато", a) END
				END;
				FOR b := 0 TO nops - 1 DO
					IF (b # a) & ops[b].done THEN
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

	PROCEDURE InitConf (size, initial: INTEGER; s: LONGINT);
		VAR i: INTEGER;
	BEGIN
		ASSERT((initial > 0) & (initial <= size) & (size <= maxN), 70);
		n := size; seed := s; x := Int64.Mul(s, 2654435761); INC(x, 88172645463325252);
		ok := TRUE; why := ""; np := 0; dlast := 0; maxCommit := 0; proposals := 0; commits := 0;
		now := 0; nextCtx := 0; crashNext := -1; nops := 0; NEW(ops, 256);
		initVoters := {}; FOR i := 0 TO initial - 1 DO INCL(initVoters, i) END;
		initLearners := {}; confChanges := 0; removes := 0; promoted := FALSE;
		FOR i := 0 TO nClients - 1 DO cl[i].state := idle; cl[i].seq := 0 END;
		NEW(decided, 64); decided[0].term := 0;
		NEW(leaderOf, maxTerm); FOR i := 0 TO maxTerm - 1 DO leaderOf[i] := Core.none END;
		installs := 0; compactEvery := 0; IF Rand(2) = 0 THEN compactEvery := 5 + Rand(20) END;
		harsh := 1 + Rand(3); batch := 1 + Rand(4); IF batch = 4 THEN batch := 64 END;
		Heal;
		FOR i := 0 TO size - 1 DO
			IF onKv THEN kvs[i] := KvFault.New() ELSE disk[i] := RaftStorage.NewMem() END;
			restarts[i] := 0; checked[i] := 0; wasLeader[i] := FALSE; wasLearner[i] := FALSE;
			rate[i] := 1 + Rand(3);
			Restart(i)
		END
	END InitConf;

	PROCEDURE Setup (total, initial: INTEGER; s: LONGINT; pv, cq, memb: BOOLEAN; exp: INTEGER);
	(* режим прогона и начальный состав: избиратели 0 .. initial-1 *)
	BEGIN
		preVoteOn := pv; checkQuorumOn := cq; membershipOn := memb; sessionExpiry := exp;
		InitConf(total, initial, s)
	END Setup;

	PROCEDURE Init (size: INTEGER; s: LONGINT);
	(* как раньше: весь состав избиратели, без PreVote/CheckQuorum *)
	BEGIN
		Setup(size, size, s, FALSE, FALSE, FALSE, 0)
	END Init;

	PROCEDURE Features (pv, cq: BOOLEAN);
	(* переключить PreVote/CheckQuorum у всех живых узлов (сценарии:
	   выборы ставятся без них, затем механизм включается) *)
		VAR i: INTEGER;
	BEGIN
		preVoteOn := pv; checkQuorumOn := cq;
		FOR i := 0 TO n - 1 DO IF nodes[i] # NIL THEN Core.SetFeatures(nodes[i], pv, cq) END END
	END Features;

	PROCEDURE RunCommon (steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
		VAR k, i, before: INTEGER;
	BEGIN
		lossy := TRUE;
		k := 0;
		WHILE ok & (k < steps) DO Move; CheckAll; INC(k) END;
		IF ok THEN	(* живость: сеть цела, все подняты, без потерь *)
			lossy := FALSE; Heal;
			FOR i := 0 TO n - 1 DO IF nodes[i] = NIL THEN Restart(i) END END;
			before := maxCommit; k := 0;
			WHILE ok & (k < 20000) & (maxCommit <= before + 2) DO Move; CheckAll; INC(k) END;
			IF ok & (maxCommit <= before + 2) THEN Violation("кластер не фиксирует после лечения", 0) END
		END;
		IF ok THEN CheckHistory END;
		res := ok; msg := why$
	END RunCommon;

	PROCEDURE Run* (size: INTEGER; s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(** Один прогон: steps ходов со сбоями, затем лечение и проверка живости.
		PreVote и CheckQuorum включены, состав постоянен.
		Пост: res — инварианты держались и кластер снова фиксирует; msg — что нарушено. *)
	BEGIN
		Setup(size, size, s, TRUE, TRUE, FALSE, 0);
		RunCommon(steps, res, msg)
	END Run;

	PROCEDURE RunM (total, initial: INTEGER; s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(** То же со сменой состава: всего total узлов, начальные избиратели
		0 .. initial-1; смена — по одному узлу через журнал. *)
	BEGIN
		Setup(total, initial, s, TRUE, TRUE, TRUE, 0);
		RunCommon(steps, res, msg)
	END RunM;

	PROCEDURE Seeds (size, from, count, steps: INTEGER; mut: SET; OUT found: INTEGER; OUT msg: ARRAY OF CHAR);
	(* found — зерно первого провала или -1 *)
		VAR s: INTEGER; r: BOOLEAN;
	BEGIN
		Core.mutations := mut; found := -1; msg := ""; s := from;
		WHILE (found < 0) & (s < from + count) DO
			Run(size, s, steps, r, msg);
			IF ~r THEN found := s END;
			INC(s)
		END;
		Core.mutations := {}
	END Seeds;

	PROCEDURE SeedsM (total, initial, from, count, steps: INTEGER; mut: SET; OUT found: INTEGER; OUT msg: ARRAY OF CHAR);
		VAR s: INTEGER; r: BOOLEAN;
	BEGIN
		Core.mutations := mut; found := -1; msg := ""; s := from;
		WHILE (found < 0) & (s < from + count) DO
			RunM(total, initial, s, steps, r, msg);
			IF ~r THEN found := s END;
			INC(s)
		END;
		Core.mutations := {}
	END SeedsM;

	PROCEDURE Teardown*;
	BEGIN
		Core.mutations := {}; onKv := FALSE
	END Teardown;

	(* --- тесты ----------------------------------------------------------- *)

	PROCEDURE TestThreeNodes*;
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(3, 1, 500, 2000, {}, f, msg);
		UnitAssert.True(f < 0, msg)
	END TestThreeNodes;

	PROCEDURE TestFiveNodes*;
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(5, 1000, 500, 2000, {}, f, msg);
		UnitAssert.True(f < 0, msg)
	END TestFiveNodes;

	PROCEDURE TestOnKv*;
	(** Стойкое состояние — в Kv на KvFault: падение узла выключает питание,
		фиксация может оборваться посередине. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		onKv := TRUE; Seeds(3, 1, 40, 1500, {}, f, msg);
		IF f < 0 THEN Seeds(5, 500, 20, 1500, {}, f, msg) END;
		onKv := FALSE;
		UnitAssert.True(f < 0, msg)
	END TestOnKv;

	PROCEDURE TestMembershipRandom*;
	(** Смена состава вместе с разрывами, падениями и снимками. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		SeedsM(5, 3, 1, 200, 2000, {}, f, msg);
		UnitAssert.True(f < 0, msg)
	END TestMembershipRandom;

	PROCEDURE TestMembershipFive*;
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		SeedsM(7, 5, 1000, 60, 2000, {}, f, msg);
		UnitAssert.True(f < 0, msg)
	END TestMembershipFive;

	PROCEDURE TestMembershipChangesHappen*;
	(** Прогон действительно вводит и выводит узлы: ученик догоняет и
		становится избирателем. *)
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		RunM(5, 3, 1, 2000, r, msg);
		UnitAssert.True(r, msg);
		UnitAssert.True(confChanges > 0, "смена состава началась");
		UnitAssert.True(promoted, "ученик стал избирателем после догона")
	END TestMembershipChangesHappen;

	PROCEDURE TestMembershipOnKv*;
	(** Состав переживает выключение питания: хранится в снимке Kv. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		onKv := TRUE; SeedsM(5, 3, 1, 20, 1500, {}, f, msg); onKv := FALSE;
		UnitAssert.True(f < 0, msg)
	END TestMembershipOnKv;

	PROCEDURE TestReplayIsExact*;
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR; c1, c2, p1: INTEGER;
	BEGIN
		Run(5, 3, 1500, r, msg); c1 := commits; p1 := proposals;
		Run(5, 3, 1500, r, msg); c2 := commits;
		UnitAssert.True(r, msg); UnitAssert.True(c1 > 5, "прогон что-то фиксирует");
		UnitAssert.EqualInt(c1, c2, "тот же прогон по тому же зерну");
		UnitAssert.EqualInt(p1, proposals, "те же предложения")
	END TestReplayIsExact;

	PROCEDURE Caught (mut, size, count: INTEGER);
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		Seeds(size, 1, count, 1500, {mut}, f, msg);
		Strings.IntToString(mut, d);
		UnitAssert.True(f >= 0, "мутация " + d$ + " не поймана");
		IF f >= 0 THEN Log.String("мутация " + d$ + " поймана: " + msg$); Log.Ln END
	END Caught;

	PROCEDURE RunOff (size: INTEGER; s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* прогон без PreVote и CheckQuorum: старые мутации видны лучше *)
	BEGIN
		Setup(size, size, s, FALSE, FALSE, FALSE, 0);
		RunCommon(steps, res, msg)
	END RunOff;

	PROCEDURE SeedsOff (size, from, count, steps: INTEGER; mut: SET; OUT found: INTEGER; OUT msg: ARRAY OF CHAR);
		VAR s: INTEGER; r: BOOLEAN;
	BEGIN
		Core.mutations := mut; found := -1; msg := ""; s := from;
		WHILE (found < 0) & (s < from + count) DO
			RunOff(size, s, steps, r, msg);
			IF ~r THEN found := s END;
			INC(s)
		END;
		Core.mutations := {}
	END SeedsOff;

	PROCEDURE CaughtOff (mut, size, count: INTEGER);
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		SeedsOff(size, 1, count, 1500, {mut}, f, msg);
		Strings.IntToString(mut, d);
		UnitAssert.True(f >= 0, "мутация " + d$ + " не поймана без PreVote");
		IF f >= 0 THEN Log.String("мутация " + d$ + " поймана: " + msg$); Log.Ln END
	END CaughtOff;

	(* --- рисунок 8 статьи по сценарию ----------------------------------- *)

	PROCEDURE Flush (from, to: INTEGER);
	(* доставить по порядку все письма from -> to, пока они есть *)
		VAR k, j: INTEGER; m: Core.Msg; again: BOOLEAN;
	BEGIN
		REPEAT
			again := FALSE; k := 0;
			WHILE k < np DO
				m := pool[k];
				IF (m.from = from) & (m.to = to) THEN
					DEC(np); FOR j := k TO np - 1 DO pool[j] := pool[j + 1] END; pool[np] := NIL;
					IF nodes[to] # NIL THEN StepAt(to, m) END;
					again := TRUE
				ELSE INC(k)
				END
			END
		UNTIL ~again
	END Flush;

	PROCEDURE DropAll;
		VAR k: INTEGER;
	BEGIN
		FOR k := 0 TO np - 1 DO pool[k] := NIL END; np := 0
	END DropAll;

	PROCEDURE DeliverOne (from, to: INTEGER);
	(* доставить первое письмо from -> to *)
		VAR k, j: INTEGER; m: Core.Msg;
	BEGIN
		k := 0; WHILE (k < np) & ~((pool[k].from = from) & (pool[k].to = to)) DO INC(k) END;
		IF k < np THEN
			m := pool[k]; DEC(np); FOR j := k TO np - 1 DO pool[j] := pool[j + 1] END; pool[np] := NIL;
			IF nodes[to] # NIL THEN StepAt(to, m) END;
			CheckAll
		END
	END DeliverOne;

	PROCEDURE Elect2 (c, voter, voter2: INTEGER);
	(* c выигрывает выборы голосом voter в сроке новее, чем у voter; прочие письма теряются *)
		VAR t: INTEGER;
	BEGIN
		IF nodes[c].role # Core.follower THEN Crash(c); Restart(c) END;	(* бывший лидер: перезапуск *)
		t := 0;
		REPEAT
			DropAll;
			WHILE (nodes[c].role # Core.candidate) & (t < 1000) DO Core.Tick(nodes[c]); Process(c); INC(t) END;
			IF nodes[c].term <= nodes[voter].term THEN	(* срок не новее: дождаться следующих выборов *)
				Core.Tick(nodes[c]); Process(c); INC(t);
				WHILE (np = 0) & (t < 1000) DO Core.Tick(nodes[c]); Process(c); INC(t) END
			END
		UNTIL (nodes[c].term > nodes[voter].term) OR (t >= 1000);
		DeliverOne(c, voter); DeliverOne(voter, c);
		IF voter2 >= 0 THEN DeliverOne(c, voter2); DeliverOne(voter2, c) END;
		DropAll
	END Elect2;

	PROCEDURE Elect (c, voter: INTEGER);
	BEGIN
		Elect2(c, voter, -1)
	END Elect;

	PROCEDURE Replicate (l, f, rounds: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO rounds DO Core.Tick(nodes[l]); Process(l); Flush(l, f); Flush(f, l); CheckAll END;
		DropAll
	END Replicate;

	PROCEDURE Figure8 (mut: SET; OUT violated: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* A=0, B=1, C=2, по одной записи в письме.
	   1) A — лидер срока 1, пустая запись 1 у всех; A пишет X в индекс 2 только у себя;
	   2) C — лидер срока 2 голосом B, его пустая запись — в индекс 2 только у C;
	   3) A — лидер срока 3 голосом B и копирует X (и только X) на B: X на большинстве, но срок X старый;
	   4) A падает, C — лидер срока 4 голосом B (последний срок C новее) и затирает X на B. *)
		VAR i, idx: INTEGER; d: Core.Bytes;
	BEGIN
		Core.mutations := mut;
		Init(3, 8); lossy := FALSE; compactEvery := 0; batch := 1;	(* и после перезапусков *)
		FOR i := 0 TO 2 DO Core.SetBatch(nodes[i], 1) END;
		Elect(0, 1); Replicate(0, 1, 4); Replicate(0, 2, 4);
		NEW(d, 1); idx := Core.Propose(nodes[0], d); Process(0); DropAll;
		Elect(2, 1);
		Elect(0, 1);
		Core.Tick(nodes[0]); Process(0);	(* пульс: B отказывает, A шлёт X *)
		DeliverOne(0, 1); DeliverOne(1, 0); DeliverOne(0, 1); DeliverOne(1, 0);
		DropAll; Crash(0);
		Elect(2, 1); Replicate(2, 1, 6);
		Core.mutations := {};
		violated := ~ok; msg := why$
	END Figure8;

	PROCEDURE StaleTail (mut: SET; OUT violated: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* 5 узлов. A=0 — лидер срока 1, пишет X, Y, W и копирует их только на B=1;
	   C=2 — лидер срока 2 голосами D=3 и E=4, его пустая запись и Z ложатся в индексы 2, 3
	   и копируются на B. Хвост B (X, Y, W срока 1) конфликтует и должен уйти целиком. *)
		VAR i, idx: INTEGER; d: Core.Bytes;
	BEGIN
		Core.mutations := mut;
		Init(5, 9); lossy := FALSE; compactEvery := 0; batch := 64;
		FOR i := 0 TO 4 DO Core.SetBatch(nodes[i], 64) END;
		Elect2(0, 1, 2); FOR i := 1 TO 4 DO Replicate(0, i, 3) END;
		FOR i := 1 TO 3 DO NEW(d, 1); idx := Core.Propose(nodes[0], d) END;
		Process(0); DropAll; Replicate(0, 1, 3);
		Elect2(2, 3, 4);
		NEW(d, 1); idx := Core.Propose(nodes[2], d); Process(2); DropAll;
		Replicate(2, 1, 3);
		Core.mutations := {};
		violated := ~ok; msg := why$
	END StaleTail;

	PROCEDURE TestStaleTailSafe*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		StaleTail({}, v, msg);
		UnitAssert.True(~v, msg);
		UnitAssert.EqualInt(nodes[2].last, nodes[1].last, "B совпал с C")
	END TestStaleTailSafe;

	PROCEDURE TestMutStaleTail*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		StaleTail({Core.mutNoTruncate}, v, msg);
		UnitAssert.True(v, "неоткаченный конфликтующий хвост не пойман");
		Log.String("хвост: " + msg); Log.Ln
	END TestMutStaleTail;

	PROCEDURE LostVote (mut: SET; OUT violated: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* B голосует за A и падает посреди Ready; после перезапуска B просят о
	   голосе в том же сроке от C. Голос, отправленный до сохранения,
	   забывается, и у срока 1 два лидера. *)
		VAR t: INTEGER;
	BEGIN
		Core.mutations := mut;
		Init(3, 10); lossy := FALSE; compactEvery := 0;
		t := 0; WHILE (nodes[0].role # Core.candidate) & (t < 1000) DO Core.Tick(nodes[0]); Process(0); INC(t) END;
		crashNext := 1; DeliverOne(0, 1); Restart(1); DeliverOne(1, 0); DropAll;
		t := 0; WHILE (nodes[2].role # Core.candidate) & (t < 1000) DO Core.Tick(nodes[2]); Process(2); INC(t) END;
		DeliverOne(2, 1); DeliverOne(1, 2); DropAll;
		Core.mutations := {};
		violated := ~ok; msg := why$
	END LostVote;

	PROCEDURE TestLostVoteSafe*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		LostVote({}, v, msg); UnitAssert.True(~v, msg)
	END TestLostVoteSafe;

	PROCEDURE TestMutLostVote*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		LostVote({Core.mutSendBeforeSync}, v, msg);
		UnitAssert.True(v, "голос, отправленный до сохранения, не пойман");
		Log.String("голос: " + msg); Log.Ln
	END TestMutLostVote;

	PROCEDURE TestMutLostVoteOnKv*;
	(* голос до сохранения ловится и на Kv: несохранённое пропадает с питанием *)
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		onKv := TRUE; LostVote({Core.mutSendBeforeSync}, v, msg); onKv := FALSE;
		UnitAssert.True(v, "голос до сохранения на Kv не пойман")
	END TestMutLostVoteOnKv;

	PROCEDURE TestSnapshotCatchUp*;
	(* C лежит, пока A и B фиксируют 60 команд и сжимают журнал снимками;
	   поднятый C догоняет снимком (InstallSnapshot) и тем же счётчиком. *)
		VAR i, k, idx: INTEGER; body: ARRAY 1 OF BYTE;
	BEGIN
		Init(3, 11); lossy := FALSE; compactEvery := 10; batch := 64;
		Elect(0, 1); Replicate(0, 1, 3); Replicate(0, 2, 3);
		Crash(2);
		body[0] := ORD("i");
		FOR k := 1 TO 60 DO
			idx := Core.Propose(nodes[0], RaftSm.Encode(1, k, body, 1)); Process(0);
			Flush(0, 1); Flush(1, 0)
		END;
		Replicate(0, 1, 2);
		UnitAssert.True(nodes[0].snapIndex > 40, "лидер сжал журнал");
		Restart(2);
		Replicate(0, 2, 6);
		UnitAssert.True(ok, why);
		UnitAssert.True(installs > 0, "C принял снимок");
		UnitAssert.EqualInt(nodes[0].commit, nodes[2].commit, "C догнал");
		UnitAssert.EqualInt(sms[0].sm(Counter).v, sms[2].sm(Counter).v, "счётчик тот же");
		UnitAssert.EqualInt(60, sms[2].sm(Counter).v, "все 60 приращений")
	END TestSnapshotCatchUp;

	PROCEDURE TestSnapshotsInRandomRuns*;
		VAR f, s, total: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		total := 0; s := 1;
		WHILE s <= 40 DO Seeds(3, s, 1, 2000, {}, f, msg); UnitAssert.True(f < 0, msg); INC(total, installs); INC(s) END;
		UnitAssert.True(total > 0, "в случайных прогонах снимки ставятся")
	END TestSnapshotsInRandomRuns;

	PROCEDURE TestFigure8Safe*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		Figure8({}, v, msg);
		UnitAssert.True(~v, msg);
		UnitAssert.True(nodes[2].role = Core.leader, "C — лидер в конце сценария")
	END TestFigure8Safe;

	PROCEDURE TestMutFigure8*;
		VAR v: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		Figure8({Core.mutOldTermCommit}, v, msg);
		UnitAssert.True(v, "фиксация записи прошлого срока по счёту реплик не поймана");
		Log.String("рисунок 8: " + msg); Log.Ln
	END TestMutFigure8;

	PROCEDURE TestMutStaleRead*; BEGIN CaughtOff(Core.mutStaleRead, 3, 300) END TestMutStaleRead;
	PROCEDURE TestMutNoDedup*; BEGIN Caught(Core.mutNoDedup, 3, 300) END TestMutNoDedup;

	PROCEDURE TestClientsComplete*;
		VAR r: BOOLEAN; msg: ARRAY 256 OF CHAR; k, done, reads: INTEGER;
	BEGIN
		Run(3, 5, 2000, r, msg); UnitAssert.True(r, msg);
		done := 0; reads := 0;
		FOR k := 0 TO nops - 1 DO IF ops[k].done THEN INC(done); IF ops[k].kind = read THEN INC(reads) END END END;
		UnitAssert.True(done > 20, "клиенты получают ответы");
		UnitAssert.True(reads > 5, "и на чтения")
	END TestClientsComplete;

	PROCEDURE TestMutNoUpToDate*; BEGIN Caught(Core.mutNoUpToDate, 3, 300) END TestMutNoUpToDate;
	PROCEDURE TestMutSendBeforeSync*; BEGIN Caught(Core.mutSendBeforeSync, 3, 1000) END TestMutSendBeforeSync;
	PROCEDURE TestMutNoTruncate*; BEGIN Caught(Core.mutNoTruncate, 5, 300) END TestMutNoTruncate;
	PROCEDURE TestMutDoubleVote*; BEGIN CaughtOff(Core.mutDoubleVote, 3, 600) END TestMutDoubleVote;
	PROCEDURE TestMutApplyUncommitted*; BEGIN Caught(Core.mutApplyUncommitted, 3, 300) END TestMutApplyUncommitted;

	(* --- PreVote и CheckQuorum ------------------------------------------ *)

	PROCEDURE Quiet (rounds: INTEGER);
	(* все узлы тикают, письма доставляются без потерь; проверка инвариантов *)
		VAR r, i: INTEGER;
	BEGIN
		FOR r := 1 TO rounds DO
			FOR i := 0 TO n - 1 DO IF nodes[i] # NIL THEN Core.Tick(nodes[i]); Process(i) END END;
			WHILE np > 0 DO Deliver END;
			CheckAll
		END
	END Quiet;

	PROCEDURE PreVoteScenario (mut: SET; OUT deposed: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* лидер 0 с составом 0,1,2; узел 2 изолирован дольше тайм-аута;
	   после лечения узел 2 не должен свергнуть лидера *)
		VAR t: INTEGER;
	BEGIN
		Core.mutations := mut;
		Setup(3, 3, 21, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect(0, 1); Replicate(0, 1, 4); Replicate(0, 2, 4);
		Features(TRUE, TRUE);
		Block(2, 0); Block(2, 1);
		FOR t := 1 TO 400 DO Core.Tick(nodes[2]); Process(2) END;	(* письма копятся: доставки нет *)
		Heal;
		Quiet(200);
		deposed := nodes[0].role # Core.leader;
		msg := why$
	END PreVoteScenario;

	PROCEDURE TestPreVoteKeepsLeader*;
		VAR d: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		PreVoteScenario({}, d, msg);
		UnitAssert.True(~d, "вернувшийся узел сверг лидера с PreVote")
	END TestPreVoteKeepsLeader;

	PROCEDURE TestMutNoPreVote*;
		VAR d: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		PreVoteScenario({Core.mutNoPreVote}, d, msg);
		UnitAssert.True(d, "выборы без PreVote не свергли лидера")
	END TestMutNoPreVote;

	PROCEDURE TestMutPreVoteIgnoreLeader*;
		VAR d: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		PreVoteScenario({Core.mutPreVoteIgnoreLeader}, d, msg);
		UnitAssert.True(d, "предвыборы вопреки слышимому лидеру не свергли лидера")
	END TestMutPreVoteIgnoreLeader;

	PROCEDURE TestMutPreVoteAdoptTerm*;
		VAR d: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		PreVoteScenario({Core.mutPreVoteAdoptTerm}, d, msg);
		UnitAssert.True(d, "принятый срок предвыборов не сверг лидера")
	END TestMutPreVoteAdoptTerm;

	PROCEDURE QuorumIsolated (mut: SET; OUT steppedDown: BOOLEAN);
	(* лидер 0 один: 1 и 2 отрезаны; большинства нет — полномочия слагаются *)
		VAR t: INTEGER;
	BEGIN
		Core.mutations := mut;
		Setup(3, 3, 22, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect(0, 1); Replicate(0, 1, 3); Replicate(0, 2, 3);
		Features(TRUE, TRUE);
		Block(0, 1); Block(0, 2);
		FOR t := 1 TO 100 DO Core.Tick(nodes[0]); Process(0) END;
		steppedDown := nodes[0].role # Core.leader;
		DropAll
	END QuorumIsolated;

	PROCEDURE TestCheckQuorumStepsDown*;
		VAR d: BOOLEAN;
	BEGIN
		QuorumIsolated({}, d); Core.mutations := {};
		UnitAssert.True(d, "лидер без большинства не сложил полномочия")
	END TestCheckQuorumStepsDown;

	PROCEDURE TestMutNoCheckQuorum*;
		VAR d: BOOLEAN;
	BEGIN
		QuorumIsolated({Core.mutNoCheckQuorum}, d); Core.mutations := {};
		UnitAssert.True(~d, "без CheckQuorum лидер в меньшинстве слагает полномочия")
	END TestMutNoCheckQuorum;

	PROCEDURE TestMutCheckQuorumEver*;
	(* считать прежние ответы без срока давности: лидер в меньшинстве не
	   слагает полномочия *)
		VAR d: BOOLEAN;
	BEGIN
		QuorumIsolated({Core.mutCheckQuorumEver}, d); Core.mutations := {};
		UnitAssert.True(~d, "давний ответ засчитан кворумом — лидер не сложил полномочия")
	END TestMutCheckQuorumEver;

	PROCEDURE QuorumStrict (mut: SET; OUT steppedDown: BOOLEAN);
	(* 2 из 3 отвечают: верный CheckQuorum доволен большинством, строгий — нет *)
		VAR t: INTEGER;
	BEGIN
		Core.mutations := mut;
		Setup(3, 3, 24, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect(0, 1); Replicate(0, 1, 3); Replicate(0, 2, 3);
		Features(TRUE, TRUE);
		Block(0, 2);
		FOR t := 1 TO 100 DO Core.Tick(nodes[0]); Process(0); Flush(0, 1); Flush(1, 0) END;
		steppedDown := nodes[0].role # Core.leader;
		DropAll
	END QuorumStrict;

	PROCEDURE TestCheckQuorumMajority*;
		VAR d: BOOLEAN;
	BEGIN
		QuorumStrict({}, d); Core.mutations := {};
		UnitAssert.True(~d, "лидер с большинством сложил полномочия")
	END TestCheckQuorumMajority;

	PROCEDURE TestMutCheckQuorumStrict*;
		VAR d: BOOLEAN;
	BEGIN
		QuorumStrict({Core.mutCheckQuorumStrict}, d); Core.mutations := {};
		UnitAssert.True(d, "строгий CheckQuorum не сложил полномочия без ответа всех")
	END TestMutCheckQuorumStrict;

	(* --- смена состава --------------------------------------------------- *)

	PROCEDURE ConfBarrier (mut: SET; OUT refused, allowed: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* лидер 0 срока 1 ещё не зафиксировал запись своего срока; смена состава
	   должна быть отложена до фиксации (исправление ошибки 2015 года) *)
		VAR idx: INTEGER;
	BEGIN
		Core.mutations := mut;
		Setup(5, 4, 25, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect2(0, 1, 2);	(* большинство 3 из 4: 0, 1, 2; пустая запись не доставлена *)
		idx := Core.AddServer(nodes[0], 4);
		refused := idx = 0;
		IF refused THEN
			Replicate(0, 1, 4); Replicate(0, 2, 4); DropAll;	(* фиксируем пустую запись срока 1 *)
			allowed := Core.AddServer(nodes[0], 4) > 0; Process(0)
		ELSE allowed := TRUE
		END;
		msg := why$;
		Core.mutations := {}
	END ConfBarrier;

	PROCEDURE TestConfBarrier*;
		VAR r, a: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		ConfBarrier({}, r, a, msg);
		UnitAssert.True(r, "смена состава начата до фиксации записи своего срока");
		UnitAssert.True(a, "после фиксации запись состава не принята");
		UnitAssert.True(ok, msg)
	END TestConfBarrier;

	PROCEDURE TestMutNoConfBarrier*;
		VAR r, a: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		ConfBarrier({Core.mutNoConfBarrier}, r, a, msg);
		UnitAssert.True(~r, "без барьера смена состава отложена")
	END TestMutNoConfBarrier;

	PROCEDURE TestMutNoCommitRestore*;
	(* забытый на перезапуске commit откатывает узел к составу старше
	   журнала; два таких узла выбирают лидера без зафиксированных записей *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		SeedsM(5, 3, 1, 200, 2000, {Core.mutNoCommitRestore}, f, msg);
		UnitAssert.True(f >= 0, "потеря commit на перезапуске не поймана");
		IF f >= 0 THEN Log.String("потеря commit поймана: " + msg$); Log.Ln END
	END TestMutNoCommitRestore;

	PROCEDURE LearnerCatchUp (mut: SET; OUT firstLearner, thenVoter, caught: BOOLEAN);
	(* узел 3 отстал; входит учеником, догоняет и только тогда становится избирателем *)
		VAR i, k, t, idx: INTEGER; body: ARRAY 1 OF BYTE;
	BEGIN
		Core.mutations := mut;
		Setup(4, 3, 27, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect(0, 1); Replicate(0, 1, 3); Replicate(0, 2, 3);
		body[0] := ORD("i");
		FOR k := 1 TO 30 DO
			idx := Core.Propose(nodes[0], RaftSm.Encode(1, k, body, 1)); Process(0);
			Flush(0, 1); Flush(1, 0); Flush(0, 2); Flush(2, 0)
		END;
		Replicate(0, 1, 2); Replicate(0, 2, 2);
		IF Core.AddServer(nodes[0], 3) = 0 THEN HALT(73) END;
		Process(0);
		(* запись состава вступает в силу по фиксации старым большинством *)
		Replicate(0, 1, 4); Replicate(0, 2, 4);
		firstLearner := Core.IsLearner(nodes[0], 3) & ~Core.IsVoter(nodes[0], 3);
		idx := nodes[0].commit;
		t := 0;
		REPEAT
			Core.Tick(nodes[0]); Process(0);
			Flush(0, 3); Flush(3, 0); Flush(0, 1); Flush(1, 0); Flush(0, 2); Flush(2, 0);
			CheckAll; INC(t)
		UNTIL Core.IsVoter(nodes[0], 3) OR (t > 500) OR ~ok;
		thenVoter := Core.IsVoter(nodes[0], 3);
		caught := thenVoter & (nodes[3].last >= idx)
	END LearnerCatchUp;

	PROCEDURE TestLearnerCatchUp*;
		VAR l, v, c: BOOLEAN;
	BEGIN
		LearnerCatchUp({}, l, v, c); Core.mutations := {};
		UnitAssert.True(l, "новый узел введён избирателем до догона");
		UnitAssert.True(v, "ученик не стал избирателем после догона");
		UnitAssert.True(c, "новый избиратель отстал от commit")
	END TestLearnerCatchUp;

	PROCEDURE TestMutNoLearner*;
		VAR l, v, c: BOOLEAN;
	BEGIN
		LearnerCatchUp({Core.mutNoLearner}, l, v, c); Core.mutations := {};
		UnitAssert.True(~l, "без ученика отставший узел сразу избиратель")
	END TestMutNoLearner;

	PROCEDURE RemoveLeader (mut: SET; OUT steppedDown, effective: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(* лидер 0 удаляет сам себя: после фиксации слагает полномочия, 1 и 2
	   выбирают нового лидера и фиксируют запись *)
		VAR t: INTEGER;
	BEGIN
		Core.mutations := mut;
		Setup(3, 3, 28, FALSE, FALSE, FALSE, 0); lossy := FALSE; compactEvery := 0;
		Elect(0, 1); Replicate(0, 1, 3); Replicate(0, 2, 3);
		IF Core.RemoveServer(nodes[0], 0) = 0 THEN HALT(74) END;
		Process(0);
		Replicate(0, 1, 5); Replicate(0, 2, 5); CheckAll;
		steppedDown := nodes[0].role # Core.leader;
		effective := ~Core.IsVoter(nodes[0], 0);	(* лидер применил удаление по фиксации *)
		(* новый лидер из 1,2 *)
		Elect(1, 2);
		t := 0;
		WHILE (nodes[1].role # Core.leader) & (t < 50) DO
			Core.Tick(nodes[1]); Process(1); Flush(1, 2); Flush(2, 1); INC(t)
		END;
		UnitAssert.True(nodes[1].role = Core.leader, "после удаления лидера выбран новый");
		IF Core.Propose(nodes[1], NIL) = 0 THEN HALT(75) END;
		Process(1); Flush(1, 2); Flush(2, 1);
		msg := why$;
		Core.mutations := {}
	END RemoveLeader;

	PROCEDURE TestRemoveLeaderStepsDown*;
		VAR s, e: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		RemoveLeader({}, s, e, msg);
		UnitAssert.True(s, "удалённый лидер не сложил полномочия");
		UnitAssert.True(e, "удаление не изменило состав");
		UnitAssert.True(ok, msg)
	END TestRemoveLeaderStepsDown;

	PROCEDURE TestMutNoStepDownRemoved*;
		VAR s, e: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		RemoveLeader({Core.mutNoStepDownRemoved}, s, e, msg);
		UnitAssert.True(~s, "удалённый лидер сложил полномочия вопреки мутации")
	END TestMutNoStepDownRemoved;

	PROCEDURE TestMutRemoveNoEffect*;
		VAR s, e: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		RemoveLeader({Core.mutRemoveNoEffect}, s, e, msg);
		UnitAssert.True(~e, "удаление из состава не изменило состав")
	END TestMutRemoveNoEffect;

	(* --- сеансы клиентов ------------------------------------------------- *)

	PROCEDURE ExpireScenario (mut: SET; OUT evicted: BOOLEAN);
		VAR cnt: Counter; r: Core.Bytes; dup: BOOLEAN; empty: ARRAY 1 OF BYTE;
	BEGIN
		Core.mutations := mut;
		NEW(cnt); sms[0] := RaftSm.New(cnt); sms[0].SetExpiry(5);
		sms[0].Apply(1, 1, RaftSm.Encode(1, 1, empty, 0), r, dup);
		sms[0].Apply(2, 10, RaftSm.Encode(2, 1, empty, 0), r, dup);	(* 1 + 5 < 10: вытеснить 1 *)
		evicted := ~sms[0].Has(1) & sms[0].Has(2);
		Core.mutations := {}
	END ExpireScenario;

	PROCEDURE TestSessionsExpire*;
		VAR e: BOOLEAN;
	BEGIN
		ExpireScenario({}, e);
		UnitAssert.True(e, "старый сеанс не вытеснен")
	END TestSessionsExpire;

	PROCEDURE TestMutNoSessionExpire*;
		VAR e: BOOLEAN;
	BEGIN
		ExpireScenario({Core.mutNoSessionExpire}, e);
		UnitAssert.True(~e, "без вытеснения старый сеанс вытеснен")
	END TestMutNoSessionExpire;

	PROCEDURE ExpireCurrentScenario (mut: SET; OUT v: INTEGER);
	(* повтор в следующем сроке: сеанс не вытесняется — операция не применяется дважды *)
		VAR cnt: Counter; r: Core.Bytes; dup: BOOLEAN; empty: ARRAY 1 OF BYTE;
	BEGIN
		Core.mutations := mut;
		NEW(cnt); sms[0] := RaftSm.New(cnt); sms[0].SetExpiry(5);
		sms[0].Apply(1, 1, RaftSm.Encode(1, 1, empty, 0), r, dup);
		sms[0].Apply(1, 2, RaftSm.Encode(1, 1, empty, 0), r, dup);
		v := cnt.v;
		Core.mutations := {}
	END ExpireCurrentScenario;

	PROCEDURE TestSessionsRetry*;
		VAR v: INTEGER;
	BEGIN
		ExpireCurrentScenario({}, v);
		UnitAssert.EqualInt(v, 1, "повтор применён дважды")
	END TestSessionsRetry;

	PROCEDURE TestMutExpireCurrent*;
		VAR v: INTEGER;
	BEGIN
		ExpireCurrentScenario({Core.mutExpireCurrent}, v);
		UnitAssert.EqualInt(v, 2, "вытеснение текущего срока не дало двойного применения")
	END TestMutExpireCurrent;

	PROCEDURE SessionTermScenario (mut: SET; OUT v: INTEGER);
	(* срок последней работы обновляется: клиент, работавший в сроке 8, не
	   вытесняется в сроке 10 при expiry = 5 *)
		VAR cnt: Counter; r: Core.Bytes; dup: BOOLEAN; empty: ARRAY 1 OF BYTE;
	BEGIN
		Core.mutations := mut;
		NEW(cnt); sms[0] := RaftSm.New(cnt); sms[0].SetExpiry(5);
		sms[0].Apply(1, 1, RaftSm.Encode(1, 1, empty, 0), r, dup);
		sms[0].Apply(2, 8, RaftSm.Encode(1, 2, empty, 0), r, dup);
		sms[0].Apply(3, 10, RaftSm.Encode(2, 1, empty, 0), r, dup);
		sms[0].Apply(4, 10, RaftSm.Encode(1, 2, empty, 0), r, dup);
		v := cnt.v;
		Core.mutations := {}
	END SessionTermScenario;

	PROCEDURE TestSessionTermUpdated*;
		VAR v: INTEGER;
	BEGIN
		SessionTermScenario({}, v);
		UnitAssert.EqualInt(v, 3, "активный сеанс вытеснен или повтор применён")
	END TestSessionTermUpdated;

	PROCEDURE TestMutSessionTermStale*;
		VAR v: INTEGER;
	BEGIN
		SessionTermScenario({Core.mutSessionTermStale}, v);
		UnitAssert.EqualInt(v, 4, "без обновления срока сеанс не вытеснен")
	END TestMutSessionTermStale;

	PROCEDURE TestSessionsSnapshotTerm*;
	(* срок сеанса переживает снимок: после восстановления сеанс не вытесняется *)
		VAR s2: RaftSm.Sessions; snap: Core.Bytes; cnt, cnt2: Counter; r: Core.Bytes; dup: BOOLEAN;
			empty: ARRAY 1 OF BYTE;
	BEGIN
		Core.mutations := {};
		NEW(cnt); sms[0] := RaftSm.New(cnt); sms[0].SetExpiry(5);
		sms[0].Apply(1, 8, RaftSm.Encode(1, 1, empty, 0), r, dup);
		snap := sms[0].Snapshot();
		NEW(cnt2); s2 := RaftSm.New(cnt2); s2.SetExpiry(5); s2.Restore(snap);
		s2.Apply(2, 10, RaftSm.Encode(2, 1, empty, 0), r, dup);
		UnitAssert.True(s2.Has(1), "срок сеанса потерян в снимке")
	END TestSessionsSnapshotTerm;

	(* --- команды --------------------------------------------------------- *)

	PROCEDURE Long*;
	(** Длинный прогон: 3000 зёрен на 3 и на 5 узлах по 3000 ходов. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		Seeds(3, 1, 3000, 3000, {}, f, msg);
		IF f < 0 THEN Seeds(5, 100000, 3000, 3000, {}, f, msg) END;
		IF f < 0 THEN Log.String("Raft long: ok") ELSE Log.String("Raft long: FAIL " + msg) END;
		Log.Ln
	END Long;

	PROCEDURE LongMembership*;
	(** Длинный прогон со сменой состава: 1000 зёрен по 3000 ходов. *)
		VAR f: INTEGER; msg: ARRAY 256 OF CHAR;
	BEGIN
		SeedsM(5, 3, 1, 1000, 3000, {}, f, msg);
		IF f < 0 THEN SeedsM(7, 5, 100000, 300, 3000, {}, f, msg) END;
		IF f < 0 THEN Log.String("Raft membership long: ok") ELSE Log.String("Raft membership long: FAIL " + msg) END;
		Log.Ln
	END LongMembership;

	PROCEDURE WriteSet (s: SET);
		VAR i: INTEGER;
	BEGIN
		Log.String("{");
		FOR i := 0 TO maxN - 1 DO IF i IN s THEN Log.Int(i); Log.String(" ") END END;
		Log.String("}")
	END WriteSet;

	PROCEDURE ReplayM*;
	(** Повтор прогона со сменой состава: RAFT_SEED, RAFT_N (всего узлов),
		RAFT_INIT (начальных избирателей), RAFT_STEPS. *)
		VAR s, nn: ARRAY 32 OF CHAR; v, sz, ini, r, st, k, i: INTEGER; res: BOOLEAN; msg: ARRAY 256 OF CHAR;
			e: Core.Entry;
	BEGIN
		Dialog.GetPar("RAFT_SEED", s, r); Strings.StringToInt(s, v, r);
		Dialog.GetPar("RAFT_N", nn, r); Strings.StringToInt(nn, sz, r); IF r # 0 THEN sz := 5 END;
		Dialog.GetPar("RAFT_INIT", nn, r); Strings.StringToInt(nn, ini, r); IF r # 0 THEN ini := 3 END;
		Dialog.GetPar("RAFT_STEPS", nn, r); Strings.StringToInt(nn, st, r); IF r # 0 THEN st := 2000 END;
		RunM(sz, ini, v, st, res, msg);
		IF res THEN Log.String("ok") ELSE Log.String(msg) END;
		Log.String(" смен "); Log.Int(confChanges); Log.String(", удалений "); Log.Int(removes);
		IF promoted THEN Log.String(", ученик стал избирателем") END;
		Log.String(", зафиксировано "); Log.Int(commits); Log.Ln;
		FOR v := 0 TO sz - 1 DO
			IF nodes[v] # NIL THEN
				Log.Int(v); Log.String(": роль"); Log.Int(nodes[v].role); Log.String(" срок"); Log.Int(nodes[v].term);
				Log.String(" лидер"); Log.Int(nodes[v].leader); Log.String(" last"); Log.Int(nodes[v].last);
				Log.String(" commit"); Log.Int(nodes[v].commit); Log.String(" изб"); Log.Int(Core.CountVoters(nodes[v]));
				IF Core.IsVoter(nodes[v], v) THEN Log.String(" (избиратель)") END;
				IF Core.IsLearner(nodes[v], v) THEN Log.String(" (ученик)") END;
				Log.String(" voters"); WriteSet(nodes[v].voters);
				Log.String(" learners"); WriteSet(nodes[v].learners);
				Log.String(" snapV"); WriteSet(nodes[v].snapVoters);
				Log.String(" conf"); Log.Int(nodes[v].confIndex); Log.String("/"); Log.Int(nodes[v].commit);
				Log.String(" tick"); Log.Int(nodes[v].ticks); Log.String(" leadEl"); Log.Int(nodes[v].leaderElapsed);
				Log.String(" timeout"); Log.Int(nodes[v].timeout); Log.Ln;
				Log.String("    журнал:"); k := nodes[v].last - 8; IF k <= nodes[v].snapIndex THEN k := nodes[v].snapIndex + 1 END;
				FOR i := k TO nodes[v].last DO
					Core.EntryAt(nodes[v], i, e);
					Log.String(" "); Log.Int(i); Log.String(":"); Log.Int(e.term); Log.String("k"); Log.Int(e.kind);
					IF e.kind # Core.entryNormal THEN Log.String("/"); Log.Int(e.node) END
				END;
				Log.Ln
			END
		END
	END ReplayM;

	PROCEDURE Replay*;
	(** Повтор одного прогона: RAFT_SEED=<зерно>, RAFT_N=<3|5>, RAFT_STEPS=<ходов, 2000>. *)
		VAR s, nn: ARRAY 32 OF CHAR; v, sz, r, st: INTEGER; res: BOOLEAN; msg: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.GetPar("RAFT_SEED", s, r); Strings.StringToInt(s, v, r);
		Dialog.GetPar("RAFT_N", nn, r); Strings.StringToInt(nn, sz, r); IF r # 0 THEN sz := 3 END;
		Dialog.GetPar("RAFT_STEPS", nn, r); Strings.StringToInt(nn, st, r); IF r # 0 THEN st := 2000 END;
		Run(sz, v, st, res, msg);
		IF res THEN Log.String("ok") ELSE Log.String(msg) END;
		Log.String(" предложений "); Log.Int(proposals); Log.String(", зафиксировано "); Log.Int(commits);
		Log.String(", писем в пуле "); Log.Int(np); Log.Ln;
		r := 0; FOR v := 0 TO nops - 1 DO IF ops[v].done THEN INC(r) END END;
		Log.String("операций клиентов с ответом "); Log.Int(r); Log.Ln;
		FOR v := 0 TO sz - 1 DO
			IF nodes[v] # NIL THEN
				Log.Int(v); Log.String(": роль"); Log.Int(nodes[v].role); Log.String(" срок"); Log.Int(nodes[v].term);
				Log.String(" голос"); Log.Int(nodes[v].vote); Log.String(" лидер"); Log.Int(nodes[v].leader);
				Log.String(" last"); Log.Int(nodes[v].last); Log.String(" commit"); Log.Int(nodes[v].commit);
				Log.String(" избирателей"); Log.Int(Core.CountVoters(nodes[v])); Log.Ln
			END
		END
	END Replay;

END Raft_devSim.
