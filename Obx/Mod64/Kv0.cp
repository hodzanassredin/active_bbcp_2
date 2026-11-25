MODULE ObxKv0;
	IMPORT Console, KvStore, KvLin, KvDb;

	CONST path = "/tmp/obxkv0.db";

	VAR db: KvDb.Db;

	PROCEDURE Put (tx: KvDb.Tx; IN key, val: ARRAY OF CHAR);
	BEGIN
		tx.Put(KvDb.Str(key)^, KvDb.Str(val)^)
	END Put;

	PROCEDURE Show (c: KvDb.Cursor);
		VAR k, v: ARRAY 64 OF CHAR;
	BEGIN
		KvDb.ToStr(c.Key(), k); KvDb.ToStr(c.Val(), v);
		Console.WriteStr(k); Console.WriteStr(" = "); Console.WriteStr(v); Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR s: KvStore.Store; tx: KvDb.Tx; c: KvDb.Cursor; res: INTEGER;
			v: KvDb.Bytes; str: ARRAY 64 OF CHAR;
	BEGIN
		KvLin.Delete(path);
		KvLin.Open(path, s, res); KvDb.Open(s, db, res);
		tx := db.Begin(TRUE);
		Put(tx, "fruit/apple", "red"); Put(tx, "fruit/banana", "yellow");
		Put(tx, "fruit/cherry", "dark red"); Put(tx, "veg/carrot", "orange");
		tx.Commit(res);
		tx := db.Begin(FALSE);
		IF tx.Get(KvDb.Str("fruit/banana")^, v) THEN
			KvDb.ToStr(v, str); Console.WriteStr("banana: "); Console.WriteStr(str); Console.WriteLn
		END;
		c := tx.Seek(KvDb.Str("fruit/b")^);
		WHILE c.Valid() & (c.Key()[0] = ORD("f")) DO Show(c); c.Next END;
		tx.Abort;
		db.Close; KvLin.Delete(path)
	END Do;

END ObxKv0.
