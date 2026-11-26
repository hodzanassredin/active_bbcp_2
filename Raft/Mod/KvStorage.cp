MODULE RaftKvStorage;
(** Стойкое состояние узла Raft в базе Kv.

	Раскладка ключей:
	- "h" — срок, голос и известный commit (по 4 байта);
	- "l" — индекс последней записи журнала (4 байта);
	- "s" — снимок: индекс и срок последней записи снимка, состав на его
	  момент (избиратели и ученики, по 4 байта каждый), затем байты снимка;
	- "e" + индекс (4 байта, big-endian, чтобы ключи шли по порядку) —
	  запись: срок (4 байта), вид записи (4 байта), номер узла состава
	  (4 байта), признак команды (1 байт), команда.

	Save — одна пишущая транзакция KvDb: удалить отброшенный хвост,
	положить новые записи, обновить "h" и "l", Commit. Commit делает два
	Sync (страницы, затем мета), и после него сохранённое переживает
	выключение питания; оборванная транзакция не видна вовсе — после
	падения Load видит прежнее состояние целиком.

	Состав кластера хранится только при снимке: записи состава в журнале
	переживают Save как обычные записи, а записи до снимка заменяет
	состав из "s". Известный commit лежит в "h" рядом со сроком и голосом:
	он только растёт, и восстановленный узел не откатывается к составу
	старше своего журнала. Снимок старого формата (без состава)
	принимается: hasConf = FALSE, владелец задаёт начальный состав сам. *)

	IMPORT KvDb, Core := RaftCore, RaftStorage;

	TYPE
		Storage = POINTER TO RECORD (RaftStorage.Storage)
			db: KvDb.Db;
			last: INTEGER;	(* последний сохранённый индекс *)
			commit: INTEGER;	(* известный зафиксированный индекс *)
			snapIndex: INTEGER	(* записи по него заменены снимком *)
		END;

	VAR
		hkey, lkey, skey: ARRAY 1 OF BYTE;

	PROCEDURE PutInt (VAR b: ARRAY OF BYTE; i, x: INTEGER);
	BEGIN	(* big-endian *)
		b[i] := SHORT(SHORT(ASH(x, -24) MOD 256)); b[i + 1] := SHORT(SHORT(ASH(x, -16) MOD 256));
		b[i + 2] := SHORT(SHORT(ASH(x, -8) MOD 256)); b[i + 3] := SHORT(SHORT(x MOD 256))
	END PutInt;

	PROCEDURE Int (IN b: ARRAY OF BYTE; i: INTEGER): INTEGER;
	BEGIN
		RETURN ASH(LONG(b[i]), 24) + b[i + 1] MOD 256 * 10000H + b[i + 2] MOD 256 * 100H + b[i + 3] MOD 256
	END Int;

	PROCEDURE SetToInt (s: SET): INTEGER;
		VAR i, x: INTEGER;
	BEGIN
		x := 0; FOR i := 0 TO Core.maxNodes - 1 DO IF i IN s THEN x := x + ASH(1, i) END END;
		RETURN x
	END SetToInt;

	PROCEDURE IntToSet (x: INTEGER): SET;
		VAR i: INTEGER; s: SET;
	BEGIN
		s := {}; FOR i := 0 TO Core.maxNodes - 1 DO IF ASH(x, -i) MOD 2 # 0 THEN INCL(s, i) END END;
		RETURN s
	END IntToSet;

	PROCEDURE Key (i: INTEGER; OUT k: ARRAY OF BYTE);
	BEGIN
		k[0] := ORD("e"); PutInt(k, 1, i)
	END Key;

	PROCEDURE PutSnap (s: Storage; tx: KvDb.Tx; index, term: INTEGER; snap: Core.Bytes;
		voters, learners: SET);
	(* положить снимок, состав на его момент и удалить записи, которые он заменил *)
		VAR v: POINTER TO ARRAY OF BYTE; k, len: INTEGER; key: ARRAY 5 OF BYTE; found: BOOLEAN;
	BEGIN
		IF snap = NIL THEN len := 0 ELSE len := LEN(snap^) END;
		NEW(v, 16 + len); PutInt(v^, 0, index); PutInt(v^, 4, term);
		PutInt(v^, 8, SetToInt(voters)); PutInt(v^, 12, SetToInt(learners));
		FOR k := 0 TO len - 1 DO v[16 + k] := snap[k] END;
		tx.Put(skey, v^);
		FOR k := s.snapIndex + 1 TO MIN(index, s.last) DO Key(k, key); found := tx.Delete(key) END
	END PutSnap;

	PROCEDURE (s: Storage) Compact* (index, term: INTEGER; snap: Core.Bytes;
		voters, learners: SET; OUT res: INTEGER);
		VAR tx: KvDb.Tx;
	BEGIN
		res := RaftStorage.ok;
		IF index > s.snapIndex THEN
			tx := s.db.Begin(TRUE); PutSnap(s, tx, index, term, snap, voters, learners); tx.Commit(res);
			IF res = KvDb.ok THEN res := RaftStorage.ok; s.snapIndex := index ELSE res := RaftStorage.failed END
		END
	END Compact;

	PROCEDURE (s: Storage) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
		VAR tx: KvDb.Tx; k, j, len: INTEGER; key: ARRAY 5 OF BYTE; e: Core.Entry; v: POINTER TO ARRAY OF BYTE;
			h: ARRAY 12 OF BYTE; l: ARRAY 4 OF BYTE; found: BOOLEAN;
	BEGIN
		IF ~r.hard & (r.snap = NIL) & (r.from > r.last) & (r.last = s.last) & (nd.commit = s.commit) THEN
			res := RaftStorage.ok; RETURN	(* нечего *)
		END;
		tx := s.db.Begin(TRUE);
		IF r.hard OR (nd.commit # s.commit) THEN
			PutInt(h, 0, r.term); PutInt(h, 4, r.vote); PutInt(h, 8, nd.commit); tx.Put(hkey, h)
		END;
		IF r.snap # NIL THEN PutSnap(s, tx, r.snapIndex, r.snapTerm, r.snap, nd.snapVoters, nd.snapLearners) END;
		IF r.from <= r.last + 1 THEN
			FOR k := MAX(r.last, s.snapIndex) + 1 TO s.last DO Key(k, key); found := tx.Delete(key) END;
			FOR k := r.from TO r.last DO
				Core.EntryAt(nd, k, e);
				IF e.data = NIL THEN len := 0 ELSE len := LEN(e.data^) END;
				NEW(v, 13 + len); PutInt(v^, 0, e.term); PutInt(v^, 4, e.kind); PutInt(v^, 8, e.node);
				IF e.data = NIL THEN v[12] := 0 ELSE v[12] := 1 END;
				FOR j := 0 TO len - 1 DO v[13 + j] := e.data[j] END;
				Key(k, key); tx.Put(key, v^)
			END;
			PutInt(l, 0, r.last); tx.Put(lkey, l)
		END;
		tx.Commit(res);
		IF res = KvDb.ok THEN
			res := RaftStorage.ok;
			IF r.from <= r.last + 1 THEN s.last := r.last END;
			IF nd.commit > s.commit THEN s.commit := nd.commit END;
			IF (r.snap # NIL) & (r.snapIndex > s.snapIndex) THEN s.snapIndex := r.snapIndex END
		ELSE res := RaftStorage.failed
		END
	END Save;

	PROCEDURE (s: Storage) Load* (OUT term, vote, commit, snapIndex, snapTerm: INTEGER; OUT snap: Core.Bytes;
		OUT log: Core.Entries; OUT last, res: INTEGER; OUT voters, learners: SET; OUT hasConf: BOOLEAN);
		VAR tx: KvDb.Tx; v: KvDb.Bytes; k, j, slast, base: INTEGER; key: ARRAY 5 OF BYTE;
	BEGIN
		term := 0; vote := Core.none; commit := 0; last := 0; snapIndex := 0; snapTerm := 0; snap := NIL;
		voters := {}; learners := {}; hasConf := FALSE; res := RaftStorage.ok;
		tx := s.db.Begin(FALSE);
		IF tx.Get(hkey, v) THEN
			term := Int(v, 0); vote := Int(v, 4);
			IF LEN(v^) >= 12 THEN commit := Int(v, 8) END
		END;
		IF tx.Get(lkey, v) THEN last := Int(v, 0) END;
		IF tx.Get(skey, v) THEN
			snapIndex := Int(v, 0); snapTerm := Int(v, 4);
			IF LEN(v^) >= 16 THEN	(* снимок с составом *)
				voters := IntToSet(Int(v, 8)); learners := IntToSet(Int(v, 12)); hasConf := TRUE; base := 16
			ELSE base := 8
			END;
			NEW(snap, LEN(v^) - base); FOR j := 0 TO LEN(v^) - base - 1 DO snap[j] := v[base + j] END
		END;
		slast := last; IF last < snapIndex THEN last := snapIndex END;
		NEW(log, last - snapIndex + 1);
		k := snapIndex + 1;
		WHILE (res = RaftStorage.ok) & (k <= last) DO
			Key(k, key);
			IF tx.Get(key, v) THEN
				log[k - snapIndex].term := Int(v, 0);
				log[k - snapIndex].kind := Int(v, 4);
				log[k - snapIndex].node := Int(v, 8);
				IF v[12] # 0 THEN
					NEW(log[k - snapIndex].data, LEN(v^) - 13);
					FOR j := 0 TO LEN(v^) - 14 DO log[k - snapIndex].data[j] := v[13 + j] END
				END
			ELSE res := RaftStorage.failed
			END;
			INC(k)
		END;
		tx.Abort;
		IF commit < snapIndex THEN commit := snapIndex END;
		s.last := slast; s.snapIndex := snapIndex; s.commit := commit
	END Load;

	PROCEDURE New* (db: KvDb.Db): RaftStorage.Storage;
	(** Пред: db открыта — 20. База отдана узлу целиком: другие ключи "h", "l", "s", "e…" не писать. *)
		VAR s: Storage; t, vt, cm, si, st, l, res: INTEGER; log: Core.Entries; snap: Core.Bytes;
			voters, learners: SET; hasConf: BOOLEAN;
	BEGIN
		ASSERT(db # NIL, 20);
		NEW(s); s.db := db;
		s.Load(t, vt, cm, si, st, snap, log, l, res, voters, learners, hasConf);
		RETURN s
	END New;

BEGIN
	hkey[0] := ORD("h"); lkey[0] := ORD("l"); skey[0] := ORD("s")
END RaftKvStorage.
