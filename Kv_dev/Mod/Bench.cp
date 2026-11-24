MODULE Kv_devBench;
(** Замеры Kv: строки BENCH, без порогов. Файл базы — в текущем каталоге
	(каталог установки на диске, а не tmpfs: иначе fsync ничего не стоит).

		tools/unit.sh Kv_devBench --marks slow --compile "KvPages KvStore KvFault KvLin KvTree KvDb Kv_devModel" *)

	IMPORT Kernel, Console, Strings, KvPages, KvStore, KvLin, KvDb, M := Kv_devModel, UnitAssert;

	CONST
		n = 100000;
		commits = 200;
		path = "kv-bench.db";

	TYPE
		NoSync = POINTER TO RECORD (KvStore.Store) s: KvStore.Store END;

	PROCEDURE (s: NoSync) Read (id: INTEGER; VAR buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
	BEGIN
		s.s.Read(id, buf, beg, res)
	END Read;

	PROCEDURE (s: NoSync) Write (id: INTEGER; IN buf: ARRAY OF BYTE; beg: INTEGER; OUT res: INTEGER);
	BEGIN
		s.s.Write(id, buf, beg, res)
	END Write;

	PROCEDURE (s: NoSync) Sync (OUT res: INTEGER);
	BEGIN
		res := KvStore.ok
	END Sync;

	PROCEDURE (s: NoSync) Pages (): INTEGER;
	BEGIN
		RETURN s.s.Pages()
	END Pages;

	PROCEDURE (s: NoSync) Close;
	BEGIN
		s.s.Close
	END Close;

	PROCEDURE Report (IN what: ARRAY OF CHAR; ops: INTEGER; ms: LONGINT);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		IF ms = 0 THEN ms := 1 END;
		Console.WriteStr("BENCH kv "); Console.WriteStr(what); Console.WriteStr(": ");
		Strings.IntToString(SHORT(LONG(ops) * 1000 DIV ms), s); Console.WriteStr(s);
		Console.WriteStr(" ops/s ("); Strings.IntToString(ops, s); Console.WriteStr(s);
		Console.WriteStr(" in "); Strings.IntToString(SHORT(ms), s); Console.WriteStr(s);
		Console.WriteStr(" ms)"); Console.WriteLn
	END Report;

	PROCEDURE Key (i: INTEGER; VAR k: ARRAY OF BYTE);
	BEGIN
		KvPages.PutByte(k, 0, i DIV 1000000H); KvPages.PutByte(k, 1, i DIV 10000H);
		KvPages.PutByte(k, 2, i DIV 100H); KvPages.PutByte(k, 3, i)
	END Key;

	PROCEDURE OpenDb (sync: BOOLEAN): KvDb.Db;
		VAR s: KvStore.Store; ns: NoSync; db: KvDb.Db; res: INTEGER;
	BEGIN
		KvLin.Delete(path); KvLin.Open(path, s, res);
		IF ~sync THEN NEW(ns); ns.s := s; s := ns END;
		KvDb.Open(s, db, res); ASSERT(res = KvDb.ok, 100);
		RETURN db
	END OpenDb;

	PROCEDURE Fill (db: KvDb.Db; random: BOOLEAN; IN what: ARRAY OF CHAR);
		VAR tx: KvDb.Tx; i, res: INTEGER; t: LONGINT; k: ARRAY 4 OF BYTE; v: ARRAY 100 OF BYTE;
	BEGIN
		M.seed := 1; t := Kernel.Time(); tx := db.Begin(TRUE);
		FOR i := 0 TO n - 1 DO
			IF random THEN Key(M.Rand(40000000H), k) ELSE Key(i, k) END;
			tx.Put(k, v)
		END;
		tx.Commit(res); ASSERT(res = KvDb.ok, 101);
		Report(what, n, Kernel.Time() - t)
	END Fill;

	PROCEDURE TestSlowBench*;
		VAR db: KvDb.Db; tx: KvDb.Tx; i, res, found: INTEGER; t: LONGINT;
			k: ARRAY 4 OF BYTE; v: ARRAY 100 OF BYTE; got: KvDb.Bytes;
	BEGIN
		db := OpenDb(TRUE); Fill(db, FALSE, "insert sequential, one tx"); db.Close;
		db := OpenDb(TRUE); Fill(db, TRUE, "insert random, one tx");
		M.seed := 1; t := Kernel.Time(); tx := db.Begin(FALSE); found := 0;
		FOR i := 0 TO n - 1 DO
			Key(M.Rand(40000000H), k); IF tx.Get(k, got) THEN INC(found) END
		END;
		tx.Abort; Report("get random", n, Kernel.Time() - t);
		UnitAssert.EqualInt(n, found, "все ключи найдены");
		db.Close;
		db := OpenDb(TRUE); t := Kernel.Time();
		FOR i := 0 TO commits - 1 DO
			tx := db.Begin(TRUE); Key(i, k); tx.Put(k, v); tx.Commit(res)
		END;
		Report("commit with fsync, 1 put", commits, Kernel.Time() - t); db.Close;
		db := OpenDb(FALSE); t := Kernel.Time();
		FOR i := 0 TO commits * 10 - 1 DO
			tx := db.Begin(TRUE); Key(i, k); tx.Put(k, v); tx.Commit(res)
		END;
		Report("commit without fsync, 1 put", commits * 10, Kernel.Time() - t); db.Close;
		KvLin.Delete(path)
	END TestSlowBench;

END Kv_devBench.
