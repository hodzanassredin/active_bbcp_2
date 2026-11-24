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
	  под индексом i — одна и та же запись, и она не меняется;
	- в конце, после лечения сети, кластер снова фиксирует команды. *)

	IMPORT Strings, Dialog, Log, Core := RaftCore, UnitAssert;

	CONST
		maxN = 5;
		poolSize = 4000;
		maxTerm = 100000;
		electionTicks = 10;

	TYPE
		Disk = RECORD	(* стойкое состояние узла *)
			term, vote, last: INTEGER;
			log: Core.Entries
		END;

	VAR
		n: INTEGER;
		nodes: ARRAY maxN OF Core.Node;
		disk: ARRAY maxN OF Disk;
		restarts: ARRAY maxN OF INTEGER;
		rate: ARRAY maxN OF INTEGER;	(* темп часов: тиков за ход *)
		checked: ARRAY maxN OF INTEGER;	(* до какого индекса сверена фиксация узла *)
		wasLeader: ARRAY maxN OF BOOLEAN;
		blocked: ARRAY maxN, maxN OF BOOLEAN;
		pool: ARRAY poolSize OF Core.Msg; np: INTEGER;
		leaderOf: POINTER TO ARRAY OF INTEGER;
		decided: Core.Entries; dlast, maxCommit: INTEGER;
		x, seed: LONGINT;
		lossy: BOOLEAN;
		harsh: INTEGER;	(* жёсткость сбоев от зерна: 1 .. 3 *)
		batch: INTEGER;
		ok: BOOLEAN; why: ARRAY 256 OF CHAR;
		proposals, commits*: INTEGER;

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
	BEGIN
		RETURN (a.term = b.term) & (a.data = b.data)
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

	(* --- узлы ------------------------------------------------------------ *)

	PROCEDURE Persist (i: INTEGER; IN r: Core.Ready);
		VAR a: Core.Entries; k: INTEGER; e: Core.Entry;
	BEGIN
		IF r.hard THEN disk[i].term := r.term; disk[i].vote := r.vote END;
		IF r.from <= r.last + 1 THEN
			IF r.last >= LEN(disk[i].log^) THEN
				NEW(a, 2 * r.last + 16); FOR k := 0 TO disk[i].last DO a[k] := disk[i].log[k] END;
				disk[i].log := a
			END;
			FOR k := r.from TO r.last DO Core.EntryAt(nodes[i], k, e); disk[i].log[k] := e END;
			disk[i].last := r.last
		END
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
	BEGIN
		nodes[i] := NIL; wasLeader[i] := FALSE; checked[i] := 0
	END Crash;

	PROCEDURE Restart (i: INTEGER);
	BEGIN
		INC(restarts[i]);
		nodes[i] := Core.New(i, n, seed * 7919 + restarts[i] * 31, electionTicks,
			disk[i].term, disk[i].vote, disk[i].log, disk[i].last);
		Core.SetBatch(nodes[i], batch)
	END Restart;

	PROCEDURE Process (i: INTEGER);
	(* выполнить Ready узла; между сохранением и отправкой узел может упасть *)
		VAR r: Core.Ready; k: INTEGER; e: Core.Entry; nd: Core.Node;
	BEGIN
		nd := nodes[i];
		WHILE (nd # NIL) & Core.HasReady(nd) DO
			Core.GetReady(nd, r);
			IF r.syncFirst THEN Persist(i, r) ELSE Post(r.msgs) END;
			IF lossy & (Rand(1000) < 2 + 8 * (harsh - 1)) THEN Crash(i); RETURN END;
			IF r.syncFirst THEN Post(r.msgs) ELSE Persist(i, r) END;
			FOR k := r.applyLo TO r.applyHi DO Core.EntryAt(nd, k, e); Decide(k, e) END;
			Core.Advance(nd, r)
		END
	END Process;

	(* --- инварианты ------------------------------------------------------ *)

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
					FOR k := 1 TO maxCommit DO
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
			IF k <= nd.last THEN Core.EntryAt(nd, k, e); Decide(k, e) END
		END;
		IF nd.commit > checked[i] THEN checked[i] := nd.commit END;
		IF nd.commit > maxCommit THEN INC(commits, nd.commit - maxCommit); maxCommit := nd.commit END
	END CheckCommit;

	PROCEDURE CheckMatching (a, b: Core.Node);
		VAR i, k: INTEGER; ea, eb: Core.Entry;
	BEGIN
		i := a.last; IF b.last < i THEN i := b.last END;
		WHILE (i > 0) & (Core.TermAt(a, i) # Core.TermAt(b, i)) DO DEC(i) END;
		FOR k := 1 TO i DO
			Core.EntryAt(a, k, ea); Core.EntryAt(b, k, eb);
			IF ~Same(ea, eb) THEN Violation("журналы расходятся до общей записи", k); RETURN END
		END
	END CheckMatching;

	PROCEDURE CheckAll;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO
			IF nodes[i] # NIL THEN CheckLeader(i); CheckCommit(i) END
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
			Core.Step(nodes[m.to], m); Process(m.to)
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

	PROCEDURE ProposeOne;
		VAR i, idx: INTEGER; d: Core.Bytes;
	BEGIN
		i := Rand(n);
		IF (nodes[i] # NIL) & (nodes[i].role # Core.leader) & (nodes[i].leader # Core.none) THEN
			i := nodes[i].leader	(* клиента перенаправили на лидера *)
		END;
		IF nodes[i] # NIL THEN
			NEW(d, 1); idx := Core.Propose(nodes[i], d);
			IF idx > 0 THEN INC(proposals); Process(i) END
		END
	END ProposeOne;

	PROCEDURE Heal;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO maxN - 1 DO FOR j := 0 TO maxN - 1 DO blocked[i, j] := FALSE END END
	END Heal;

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

	PROCEDURE Move;
		VAR k, i: INTEGER;
	BEGIN
		k := Rand(1000);
		IF (k < 700) & (np > 0) THEN	(* сеть быстрее часов: до трёх писем за ход *)
			Deliver; IF np > 0 THEN Deliver END; IF np > 0 THEN Deliver END
		ELSIF k < 850 THEN TickOne
		ELSIF k < 950 THEN IF lossy OR (Rand(10) = 0) THEN ProposeOne END
		ELSIF ~lossy THEN TickOne
		ELSIF k < 950 + 10 * harsh THEN i := Rand(n); IF nodes[i] # NIL THEN Crash(i) END
		ELSIF k < 985 THEN i := Rand(n); IF nodes[i] = NIL THEN Restart(i) END
		ELSE Partition
		END
	END Move;

	PROCEDURE Init (size: INTEGER; s: LONGINT);
		VAR i: INTEGER;
	BEGIN
		n := size; seed := s; x := s * 2654435761 + 88172645463325252;
		ok := TRUE; why := ""; np := 0; dlast := 0; maxCommit := 0; proposals := 0; commits := 0;
		NEW(decided, 64); decided[0].term := 0;
		NEW(leaderOf, maxTerm); FOR i := 0 TO maxTerm - 1 DO leaderOf[i] := Core.none END;
		harsh := 1 + Rand(3); batch := 1 + Rand(4); IF batch = 4 THEN batch := 64 END;
		Heal;
		FOR i := 0 TO n - 1 DO
			disk[i].term := 0; disk[i].vote := Core.none; disk[i].last := 0; NEW(disk[i].log, 16);
			restarts[i] := 0; checked[i] := 0; wasLeader[i] := FALSE;
			rate[i] := 1 + Rand(3);
			Restart(i)
		END
	END Init;

	PROCEDURE Run* (size: INTEGER; s: LONGINT; steps: INTEGER; OUT res: BOOLEAN; OUT msg: ARRAY OF CHAR);
	(** Один прогон: steps ходов со сбоями, затем лечение и проверка живости.
		Пост: res — инварианты держались и кластер снова фиксирует; msg — что нарушено. *)
		VAR k, i, before: INTEGER;
	BEGIN
		Init(size, s); lossy := TRUE;
		k := 0;
		WHILE ok & (k < steps) DO Move; CheckAll; INC(k) END;
		IF ok THEN	(* живость: сеть цела, все подняты, без потерь *)
			lossy := FALSE; Heal;
			FOR i := 0 TO n - 1 DO IF nodes[i] = NIL THEN Restart(i) END END;
			before := maxCommit; k := 0;
			WHILE ok & (k < 20000) & (maxCommit <= before + 2) DO Move; CheckAll; INC(k) END;
			IF ok & (maxCommit <= before + 2) THEN Violation("кластер не фиксирует после лечения", 0) END
		END;
		res := ok; msg := why$
	END Run;

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

	PROCEDURE Teardown*;
	BEGIN
		Core.mutations := {}
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
					IF nodes[to] # NIL THEN Core.Step(nodes[to], m); Process(to) END;
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
			IF nodes[to] # NIL THEN Core.Step(nodes[to], m); Process(to) END;
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
		Init(3, 8); lossy := FALSE;
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
		Init(5, 9); lossy := FALSE;
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

	PROCEDURE TestMutNoUpToDate*; BEGIN Caught(Core.mutNoUpToDate, 3, 300) END TestMutNoUpToDate;
	PROCEDURE TestMutSendBeforeSync*; BEGIN Caught(Core.mutSendBeforeSync, 3, 300) END TestMutSendBeforeSync;
	PROCEDURE TestMutNoTruncate*; BEGIN Caught(Core.mutNoTruncate, 5, 300) END TestMutNoTruncate;
	PROCEDURE TestMutDoubleVote*; BEGIN Caught(Core.mutDoubleVote, 3, 300) END TestMutDoubleVote;
	PROCEDURE TestMutApplyUncommitted*; BEGIN Caught(Core.mutApplyUncommitted, 3, 300) END TestMutApplyUncommitted;

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
		FOR v := 0 TO sz - 1 DO
			IF nodes[v] # NIL THEN
				Log.Int(v); Log.String(": роль"); Log.Int(nodes[v].role); Log.String(" срок"); Log.Int(nodes[v].term);
				Log.String(" голос"); Log.Int(nodes[v].vote); Log.String(" лидер"); Log.Int(nodes[v].leader);
				Log.String(" last"); Log.Int(nodes[v].last); Log.String(" commit"); Log.Int(nodes[v].commit); Log.Ln
			END
		END
	END Replay;

END Raft_devSim.
