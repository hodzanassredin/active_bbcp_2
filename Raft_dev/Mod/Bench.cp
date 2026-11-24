MODULE Raft_devBench;
(** Замеры Raft: запись через журнал на 3 узлах (в памяти и на Kv с fsync)
	при 1, 8 и 64 клиентах, выборы после падения лидера (в тиках),
	догон отставшего снимком.

	Сеть — очередь писем по порядку, без сбоев; узлы в одном процессе.
	Клиентов C: в полёте держится C команд; лидер копит предложения между
	двумя Ready, и одна Save (одна фиксация Kv) уносит всю пачку.

	Запуск: echo 'Raft_devBench.Run' | tools/run64.sh *)

	IMPORT Log, Strings, System_devClock, KvDb, KvLin, KvStore,
		Core := RaftCore, RaftStorage, RaftKvStorage;

	CONST
		n = 3; poolSize = 100000;

	VAR
		nodes: ARRAY n OF Core.Node;
		disk: ARRAY n OF RaftStorage.Storage;
		pool: ARRAY poolSize OF Core.Msg; head, tail: INTEGER;
		saves: INTEGER;

	PROCEDURE Post (m: Core.Msg);
		VAR nx: Core.Msg;
	BEGIN
		WHILE m # NIL DO
			nx := m.next; m.next := NIL;
			IF nodes[m.to] # NIL THEN pool[tail] := m; tail := (tail + 1) MOD poolSize END;
			m := nx
		END
	END Post;

	PROCEDURE Process (i: INTEGER);
		VAR r: Core.Ready; res: INTEGER;
	BEGIN
		WHILE (nodes[i] # NIL) & Core.HasReady(nodes[i]) DO
			Core.GetReady(nodes[i], r);
			IF r.hard OR (r.from <= r.last) OR (r.snap # NIL) THEN disk[i].Save(nodes[i], r, res); INC(saves) END;
			Post(r.msgs);
			Core.Advance(nodes[i], r)
		END
	END Process;

	PROCEDURE Drain;
	(* кругами: все письма, что уже в очереди, затем по одному Ready на узел.
	   Узел, получивший за круг много писем, сохраняет их одной Save —
	   групповая фиксация и у последователей. *)
		VAR m: Core.Msg; end, i: INTEGER;
	BEGIN
		WHILE head # tail DO
			end := tail;
			WHILE head # end DO
				m := pool[head]; pool[head] := NIL; head := (head + 1) MOD poolSize;
				IF nodes[m.to] # NIL THEN Core.Step(nodes[m.to], m) END
			END;
			FOR i := 0 TO n - 1 DO Process(i) END
		END
	END Drain;

	PROCEDURE Start (i: INTEGER; seed: LONGINT);
		VAR t, v, si, st, last, res: INTEGER; snap: Core.Bytes; log: Core.Entries;
	BEGIN
		disk[i].Load(t, v, si, st, snap, log, last, res);
		nodes[i] := Core.New(i, n, seed, 10, t, v, si, st, snap, log, last)
	END Start;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO IF (nodes[i] # NIL) & (nodes[i].role = Core.leader) THEN RETURN i END END;
		RETURN -1
	END Leader;

	PROCEDURE Elect (): INTEGER;
	(* тики всем живым до лидера с зафиксированной пустой записью; результат — тиков *)
		VAR i, t: INTEGER;
	BEGIN
		t := 0;
		WHILE ((Leader() < 0) OR (nodes[Leader()].commit < nodes[Leader()].last)) & (t < 10000) DO
			FOR i := 0 TO n - 1 DO IF nodes[i] # NIL THEN Core.Tick(nodes[i]); Process(i) END END;
			Drain; INC(t)
		END;
		RETURN t
	END Elect;

	PROCEDURE Setup (onKv: BOOLEAN; seed: LONGINT);
		VAR i, res: INTEGER; st: KvStore.Store; db: KvDb.Db; path, d: ARRAY 64 OF CHAR;
	BEGIN
		head := 0; tail := 0; saves := 0;
		FOR i := 0 TO n - 1 DO
			IF onKv THEN
				(* файл — в каталоге запуска: /tmp бывает tmpfs, где fsync ничего не стоит *)
				Strings.IntToString(i, d); path := "raft-bench-" + d$ + ".db"; KvLin.Delete(path);
				KvLin.Open(path, st, res); ASSERT(res = KvStore.ok, 60);
				KvDb.Open(st, db, res); ASSERT(res = KvDb.ok, 61);
				disk[i] := RaftKvStorage.New(db)
			ELSE disk[i] := RaftStorage.NewMem()
			END;
			Start(i, seed)
		END
	END Setup;

	PROCEDURE Write (onKv: BOOLEAN; clients, ops: INTEGER);
		VAR l, proposed, base, k: INTEGER; t0, ns: LONGINT; d: Core.Bytes; s0: INTEGER;
	BEGIN
		Setup(onKv, 1); k := Elect(); l := Leader();
		base := nodes[l].applied; proposed := 0; s0 := saves;
		t0 := System_devClock.now();
		WHILE nodes[l].applied - base < ops DO
			WHILE (proposed < ops) & (proposed - (nodes[l].applied - base) < clients) DO
				NEW(d, 16); k := Core.Propose(nodes[l], d); INC(proposed)
			END;
			Process(l); Drain
		END;
		ns := System_devClock.now() - t0;
		IF onKv THEN Log.String("Kv+fsync") ELSE Log.String("память  ") END;
		Log.String(", клиентов "); Log.Int(clients); Log.String(": команд/с "); Log.Int(SHORT(LONG(ops) * 1000000000 DIV ns));
		Log.String(", сохранений на команду (все узлы) "); Log.Real((saves - s0) / ops); Log.Ln
	END Write;

	PROCEDURE Elections;
	(* лидер падает; тиков до нового лидера, зафиксировавшего свою пустую запись *)
		VAR s, l, sum, mx, t: INTEGER;
	BEGIN
		sum := 0; mx := 0;
		FOR s := 1 TO 200 DO
			Setup(FALSE, s); t := Elect(); l := Leader();
			nodes[l] := NIL; head := tail;	(* письма в полёте пропали вместе с лидером *)
			t := Elect(); INC(sum, t); IF t > mx THEN mx := t END
		END;
		Log.String("выборы после падения лидера (200 зёрен, тайм-аут 10..19 тиков): в среднем ");
		Log.Real(sum / 200); Log.String(" тиков, наибольшее "); Log.Int(mx); Log.Ln
	END Elections;

	PROCEDURE CatchUp;
		VAR l, f, k: INTEGER; d: Core.Bytes; t0, ns: LONGINT;
	BEGIN
		Setup(FALSE, 3); k := Elect(); l := Leader(); f := (l + 1) MOD n;
		nodes[f] := NIL;
		FOR k := 1 TO 100000 DO
			NEW(d, 16); IF Core.Propose(nodes[l], d) = 0 THEN HALT(62) END;
			IF k MOD 64 = 0 THEN Process(l); Drain END
		END;
		Process(l); Drain;
		NEW(d, 100000);	(* снимок автомата: 100 КБ *)
		Core.Compact(nodes[l], nodes[l].applied, d);
		t0 := System_devClock.now();
		Start(f, 7); FOR k := 1 TO 3 DO Core.Tick(nodes[l]); Process(l); Drain END;
		ns := System_devClock.now() - t0;
		Log.String("догон снимком после 100000 команд: мкс "); Log.Int(SHORT(ns DIV 1000));
		Log.String(", отставший commit "); Log.Int(nodes[f].commit); Log.String(" из "); Log.Int(nodes[l].commit); Log.Ln
	END CatchUp;

	PROCEDURE Run*;
	BEGIN
		Write(FALSE, 1, 20000); Write(FALSE, 8, 20000); Write(FALSE, 64, 20000);
		Write(TRUE, 1, 200); Write(TRUE, 8, 1000); Write(TRUE, 64, 4000);
		Elections; CatchUp
	END Run;

END Raft_devBench.
