MODULE RaftKvStorage;
(** Стойкое состояние узла Raft в базе Kv.

	Раскладка ключей:
	- "h" — срок и голос (по 4 байта);
	- "l" — индекс последней записи журнала (4 байта);
	- "e" + индекс (4 байта, big-endian, чтобы ключи шли по порядку) —
	  запись: срок (4 байта), признак команды (1 байт), команда.

	Save — одна пишущая транзакция KvDb: удалить отброшенный хвост,
	положить новые записи, обновить "h" и "l", Commit. Commit делает два
	Sync (страницы, затем мета), и после него сохранённое переживает
	выключение питания; оборванная транзакция не видна вовсе — после
	падения Load видит прежнее состояние целиком. *)

	IMPORT KvDb, Core := RaftCore, RaftStorage;

	TYPE
		Storage = POINTER TO RECORD (RaftStorage.Storage)
			db: KvDb.Db;
			last: INTEGER	(* последний сохранённый индекс *)
		END;

	VAR
		hkey, lkey: ARRAY 1 OF BYTE;

	PROCEDURE PutInt (VAR b: ARRAY OF BYTE; i, x: INTEGER);
	BEGIN	(* big-endian *)
		b[i] := SHORT(SHORT(ASH(x, -24) MOD 256)); b[i + 1] := SHORT(SHORT(ASH(x, -16) MOD 256));
		b[i + 2] := SHORT(SHORT(ASH(x, -8) MOD 256)); b[i + 3] := SHORT(SHORT(x MOD 256))
	END PutInt;

	PROCEDURE Int (IN b: ARRAY OF BYTE; i: INTEGER): INTEGER;
	BEGIN
		RETURN ASH(LONG(b[i]), 24) + b[i + 1] MOD 256 * 10000H + b[i + 2] MOD 256 * 100H + b[i + 3] MOD 256
	END Int;

	PROCEDURE Key (i: INTEGER; OUT k: ARRAY OF BYTE);
	BEGIN
		k[0] := ORD("e"); PutInt(k, 1, i)
	END Key;

	PROCEDURE (s: Storage) Save* (nd: Core.Node; IN r: Core.Ready; OUT res: INTEGER);
		VAR tx: KvDb.Tx; k, j, len: INTEGER; key: ARRAY 5 OF BYTE; e: Core.Entry; v: POINTER TO ARRAY OF BYTE;
			h: ARRAY 8 OF BYTE; l: ARRAY 4 OF BYTE; found: BOOLEAN;
	BEGIN
		IF ~r.hard & (r.from > r.last) & (r.last = s.last) THEN res := RaftStorage.ok; RETURN END;	(* нечего *)
		tx := s.db.Begin(TRUE);
		IF r.hard THEN PutInt(h, 0, r.term); PutInt(h, 4, r.vote); tx.Put(hkey, h) END;
		IF r.from <= r.last + 1 THEN
			FOR k := r.last + 1 TO s.last DO Key(k, key); found := tx.Delete(key) END;
			FOR k := r.from TO r.last DO
				Core.EntryAt(nd, k, e);
				IF e.data = NIL THEN len := 0 ELSE len := LEN(e.data^) END;
				NEW(v, 5 + len); PutInt(v^, 0, e.term);
				IF e.data = NIL THEN v[4] := 0 ELSE v[4] := 1 END;
				FOR j := 0 TO len - 1 DO v[5 + j] := e.data[j] END;
				Key(k, key); tx.Put(key, v^)
			END;
			PutInt(l, 0, r.last); tx.Put(lkey, l)
		END;
		tx.Commit(res);
		IF res = KvDb.ok THEN
			res := RaftStorage.ok;
			IF r.from <= r.last + 1 THEN s.last := r.last END
		ELSE res := RaftStorage.failed
		END
	END Save;

	PROCEDURE (s: Storage) Load* (OUT term, vote: INTEGER; OUT log: Core.Entries; OUT last, res: INTEGER);
		VAR tx: KvDb.Tx; v: KvDb.Bytes; k, j: INTEGER; key: ARRAY 5 OF BYTE;
	BEGIN
		term := 0; vote := Core.none; last := 0; res := RaftStorage.ok;
		tx := s.db.Begin(FALSE);
		IF tx.Get(hkey, v) THEN term := Int(v, 0); vote := Int(v, 4) END;
		IF tx.Get(lkey, v) THEN last := Int(v, 0) END;
		NEW(log, last + 1);
		k := 1;
		WHILE (res = RaftStorage.ok) & (k <= last) DO
			Key(k, key);
			IF tx.Get(key, v) THEN
				log[k].term := Int(v, 0);
				IF v[4] # 0 THEN
					NEW(log[k].data, LEN(v^) - 5); FOR j := 0 TO LEN(v^) - 6 DO log[k].data[j] := v[5 + j] END
				END
			ELSE res := RaftStorage.failed
			END;
			INC(k)
		END;
		tx.Abort;
		s.last := last
	END Load;

	PROCEDURE New* (db: KvDb.Db): RaftStorage.Storage;
	(** Пред: db открыта — 20. База отдана узлу целиком: другие ключи "h", "l", "e…" не писать. *)
		VAR s: Storage; t, vt, l, res: INTEGER; log: Core.Entries;
	BEGIN
		ASSERT(db # NIL, 20);
		NEW(s); s.db := db; s.Load(t, vt, log, l, res);
		RETURN s
	END New;

BEGIN
	hkey[0] := ORD("h"); lkey[0] := ORD("l")
END RaftKvStorage.
