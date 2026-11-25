MODULE Kv_devBasic;
(** Тесты Kv: страницы и мета, хранилище со сбоями, изоляция читателя,
	настоящий файл и повреждённые меты, нарушения контракта. *)

	IMPORT KvPages, KvStore, KvFault, KvLin, KvDb, M := Kv_devModel, UnitAssert;

	VAR
		gdb: KvDb.Db;
		gtx: KvDb.Tx;
		path: ARRAY 256 OF CHAR;

	PROCEDURE Setup*;
	BEGIN
		path := "/tmp/kv-test.db"; KvLin.Delete(path)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		KvLin.Delete(path)
	END Teardown;

	(* --- 1. страницы ------------------------------------------------------ *)

	PROCEDURE TestNumbersRoundTrip*;
		VAR b: ARRAY 16 OF BYTE;
	BEGIN
		KvPages.PutInt(b, 0, -2); KvPages.PutInt(b, 4, 7FFFFFFFH); KvPages.PutShort(b, 8, 0FFFFH);
		UnitAssert.EqualInt(-2, KvPages.Int(b, 0), "минус два");
		UnitAssert.EqualInt(7FFFFFFFH, KvPages.Int(b, 4), "наибольшее");
		UnitAssert.EqualInt(0FFFFH, KvPages.Short(b, 8), "16 бит");
		UnitAssert.EqualInt(0FEH, KvPages.Byte(b, 0), "младший байт вперёд")
	END TestNumbersRoundTrip;

	PROCEDURE TestCrcKnownValue*;
		VAR b: ARRAY 9 OF BYTE; i: INTEGER;
	BEGIN
		FOR i := 0 TO 8 DO KvPages.PutByte(b, i, ORD("1") + i) END;	(* "123456789" *)
		UnitAssert.EqualInt(0CBF43926H, KvPages.Crc(b, 0, 9), "CRC32 образца")
	END TestCrcKnownValue;

	PROCEDURE TestMetaRoundTrip*;
		VAR b: ARRAY KvPages.size OF BYTE; m, r: KvPages.Meta; ok: BOOLEAN;
	BEGIN
		m.root := 7; m.free := 5; m.pages := 9; m.txid := 123456789012L;
		KvPages.PutMeta(b, m); KvPages.GetMeta(b, r, ok);
		UnitAssert.True(ok, "мета читается");
		UnitAssert.EqualInt(7, r.root, "root"); UnitAssert.EqualInt(5, r.free, "free");
		UnitAssert.EqualInt(9, r.pages, "pages"); UnitAssert.EqualInt(123456789012L, r.txid, "txid");
		KvPages.PutByte(b, 36, KvPages.Byte(b, 36) + 1);	(* старший байт txid *)
		KvPages.GetMeta(b, r, ok);
		UnitAssert.True(~ok, "испорченный байт ловит CRC")
	END TestMetaRoundTrip;

	PROCEDURE TestCompareUnsigned*;
		VAR a, b: ARRAY 2 OF BYTE;
	BEGIN
		KvPages.PutByte(a, 0, 1); KvPages.PutByte(b, 0, 200); KvPages.PutByte(a, 1, 0);
		UnitAssert.EqualInt(-1, KvPages.Compare(a, 1, b, 1), "байт без знака");
		UnitAssert.EqualInt(-1, KvPages.Compare(a, 1, a, 2), "начало меньше");
		UnitAssert.EqualInt(0, KvPages.Compare(b, 1, b, 1), "равны")
	END TestCompareUnsigned;

	(* --- 2. хранилище со сбоями ------------------------------------------- *)

	PROCEDURE Fill (VAR b: ARRAY OF BYTE; x: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(b) - 1 DO KvPages.PutByte(b, i, x) END
	END Fill;

	PROCEDURE TestFaultLosesUnsynced*;
		VAR s: KvFault.Store; b: ARRAY KvPages.size OF BYTE; res: INTEGER;
	BEGIN
		s := KvFault.New();
		Fill(b, 1); s.Write(0, b, 0, res); s.Sync(res);
		Fill(b, 2); s.Write(0, b, 0, res); s.Write(1, b, 0, res);
		s.Read(0, b, 0, res); UnitAssert.EqualInt(2, KvPages.Byte(b, 9), "чтение видит запись");
		s.PowerOff(0);
		UnitAssert.EqualInt(1, s.Pages(), "вторая страница пропала");
		s.Read(0, b, 0, res); UnitAssert.EqualInt(1, KvPages.Byte(b, 9), "осталось синхронизированное")
	END TestFaultLosesUnsynced;

	PROCEDURE TestFaultTornWrite*;
		VAR s: KvFault.Store; b: ARRAY KvPages.size OF BYTE; res: INTEGER;
	BEGIN
		s := KvFault.New();
		Fill(b, 1); s.Write(0, b, 0, res); s.Sync(res);
		s.Arm(1, KvPages.size DIV 2);
		Fill(b, 2); s.Write(1, b, 0, res); UnitAssert.EqualInt(KvStore.ok, res, "событие 0 исполнено");
		s.Write(0, b, 0, res); UnitAssert.EqualInt(KvStore.ioError, res, "событие 1 — сбой");
		s.Sync(res); UnitAssert.EqualInt(KvStore.ioError, res, "устройство мертво");
		s.Read(0, b, 0, res);
		UnitAssert.EqualInt(2, KvPages.Byte(b, 0), "первая половина новая");
		UnitAssert.EqualInt(1, KvPages.Byte(b, KvPages.size - 1), "вторая половина старая")
	END TestFaultTornWrite;

	PROCEDURE TestFaultRandomKeepIsSeeded*;
		VAR s1, s2: KvFault.Store; b: ARRAY KvPages.size OF BYTE; i, res: INTEGER; same: BOOLEAN;
	BEGIN
		s1 := KvFault.New(); s2 := KvFault.New();
		FOR i := 0 TO 31 DO Fill(b, i); s1.Write(i, b, 0, res); s2.Write(i, b, 0, res) END;
		s1.PowerOff(77); s2.PowerOff(77);
		same := s1.Pages() = s2.Pages();
		UnitAssert.True(same & (s1.Pages() > 0) & (s1.Pages() <= 32), "одно зерно — один исход")
	END TestFaultRandomKeepIsSeeded;

	(* --- 6. изоляция ------------------------------------------------------ *)

	PROCEDURE Commit (tx: KvDb.Tx);
		VAR res: INTEGER;
	BEGIN
		tx.Commit(res); UnitAssert.EqualInt(KvDb.ok, res, "фиксация")
	END Commit;

	PROCEDURE TestReaderSeesSnapshot*;
		VAR db: KvDb.Db; w, r, r2: KvDb.Tx; res: INTEGER; v: M.Bytes;
	BEGIN
		KvDb.Open(KvFault.New(), db, res);
		w := db.Begin(TRUE); w.Put(KvDb.Str("a")^, KvDb.Str("1")^); Commit(w);
		r := db.Begin(FALSE);
		w := db.Begin(TRUE);
		w.Put(KvDb.Str("a")^, KvDb.Str("2")^); w.Put(KvDb.Str("b")^, KvDb.Str("3")^);
		UnitAssert.True(r.Get(KvDb.Str("a")^, v) & M.Same(v, KvDb.Str("1")), "до фиксации — старое");
		Commit(w);
		UnitAssert.True(r.Get(KvDb.Str("a")^, v) & M.Same(v, KvDb.Str("1")), "после фиксации — старое");
		UnitAssert.True(~r.Get(KvDb.Str("b")^, v), "нового ключа не видно");
		r2 := db.Begin(FALSE);
		UnitAssert.True(r2.Get(KvDb.Str("a")^, v) & M.Same(v, KvDb.Str("2")), "новый читатель — новое");
		r.Abort; r2.Abort; db.Close
	END TestReaderSeesSnapshot;

	PROCEDURE TestAbortLeavesNoTrace*;
		VAR db: KvDb.Db; w: KvDb.Tx; res, pages: INTEGER; m: M.Model;
	BEGIN
		KvDb.Open(KvFault.New(), db, res); m := M.New();
		w := db.Begin(TRUE); w.Put(KvDb.Str("k")^, KvDb.Str("v")^); Commit(w);
		m.Put(KvDb.Str("k")^, KvDb.Str("v")^); pages := db.Pages();
		w := db.Begin(TRUE); w.Put(KvDb.Str("x")^, KvDb.Str("y")^);
		UnitAssert.True(w.Delete(KvDb.Str("k")^), "удаление в транзакции"); w.Abort;
		M.CheckDb(db, m, "Abort не меняет базу");
		UnitAssert.EqualInt(pages, db.Pages(), "файл не вырос");
		db.Close
	END TestAbortLeavesNoTrace;

	(* --- 7. настоящий файл ------------------------------------------------ *)

	PROCEDURE OpenFile (OUT db: KvDb.Db; OUT res: INTEGER);
		VAR s: KvStore.Store;
	BEGIN
		db := NIL; KvLin.Open(path, s, res);
		IF res = KvStore.ok THEN KvDb.Open(s, db, res); IF res # KvDb.ok THEN s.Close END END
	END OpenFile;

	PROCEDURE PutCommit (db: KvDb.Db; IN k, v: ARRAY OF CHAR);
		VAR w: KvDb.Tx;
	BEGIN
		w := db.Begin(TRUE); w.Put(KvDb.Str(k)^, KvDb.Str(v)^); Commit(w)
	END PutCommit;

	PROCEDURE GetStr (db: KvDb.Db; IN k: ARRAY OF CHAR; OUT v: ARRAY OF CHAR);
		VAR r: KvDb.Tx; b: M.Bytes;
	BEGIN
		r := db.Begin(FALSE);
		IF r.Get(KvDb.Str(k)^, b) THEN KvDb.ToStr(b, v) ELSE v := "-" END;
		r.Abort
	END GetStr;

	PROCEDURE Damage (id: INTEGER);
		VAR s: KvStore.Store; b: ARRAY KvPages.size OF BYTE; res: INTEGER;
	BEGIN
		KvLin.Open(path, s, res); UnitAssert.EqualInt(KvStore.ok, res, "файл открыт");
		s.Read(id, b, 0, res); KvPages.PutByte(b, 36, KvPages.Byte(b, 36) + 1);	(* txid растёт *)
		s.Write(id, b, 0, res); s.Sync(res); s.Close
	END Damage;

	PROCEDURE TestFileReopen*;
		VAR db: KvDb.Db; res, i: INTEGER; v: ARRAY 32 OF CHAR; m: M.Model; w: KvDb.Tx;
			k, x: ARRAY 16 OF CHAR;
	BEGIN
		OpenFile(db, res); UnitAssert.EqualInt(KvDb.ok, res, "новая база");
		m := M.New(); w := db.Begin(TRUE);
		FOR i := 0 TO 499 DO
			k := "key"; x[0] := CHR(ORD("0") + i DIV 100); x[1] := CHR(ORD("0") + i DIV 10 MOD 10);
			x[2] := CHR(ORD("0") + i MOD 10); x[3] := 0X; k := k + x;
			w.Put(KvDb.Str(k)^, KvDb.Str(x)^); m.Put(KvDb.Str(k)^, KvDb.Str(x)^)
		END;
		Commit(w); db.Close;
		OpenFile(db, res); UnitAssert.EqualInt(KvDb.ok, res, "открыта заново");
		GetStr(db, "key123", v); UnitAssert.EqualStr("123", v, "значение после закрытия");
		M.CheckDb(db, m, "вся база после закрытия");
		db.Close
	END TestFileReopen;

	PROCEDURE TestDamagedMetaFallsBack*;
		VAR db: KvDb.Db; res: INTEGER; v: ARRAY 32 OF CHAR;
	BEGIN
		OpenFile(db, res);
		PutCommit(db, "a", "old");	(* txid 2, страница 0 *)
		PutCommit(db, "a", "new");	(* txid 3, страница 1 *)
		db.Close;
		Damage(1);
		OpenFile(db, res); UnitAssert.EqualInt(KvDb.ok, res, "вторая мета годна");
		GetStr(db, "a", v); UnitAssert.EqualStr("old", v, "состояние предыдущей фиксации");
		db.Close;
		Damage(0);
		OpenFile(db, res); UnitAssert.EqualInt(KvDb.corrupt, res, "обе меты испорчены — res, не трап");
		UnitAssert.True(db = NIL, "базы нет")
	END TestDamagedMetaFallsBack;

	(* --- нарушения контракта -------------------------------------------- *)

	PROCEDURE SecondWriter;
		VAR t: KvDb.Tx;
	BEGIN
		t := gdb.Begin(TRUE)
	END SecondWriter;

	PROCEDURE PutInReader;
	BEGIN
		gtx.Put(KvDb.Str("a")^, KvDb.Str("b")^)
	END PutInReader;

	PROCEDURE CursorAfterPut;
		VAR c: KvDb.Cursor; b: BOOLEAN;
	BEGIN
		c := gtx.First(); gtx.Put(KvDb.Str("z")^, KvDb.Str("z")^); b := c.Valid()
	END CursorAfterPut;

	PROCEDURE TestSecondWriterTraps*;
		VAR res: INTEGER; w: KvDb.Tx;
	BEGIN
		KvDb.Open(KvFault.New(), gdb, res);
		w := gdb.Begin(TRUE);
		UnitAssert.Traps(SecondWriter, "вторая пишущая — трап 21");
		w.Abort;
		gtx := gdb.Begin(FALSE);
		UnitAssert.Traps(PutInReader, "Put в читающей — трап 21");
		gtx.Abort;
		gtx := gdb.Begin(TRUE);
		UnitAssert.Traps(CursorAfterPut, "курсор после изменения — трап 22");
		gtx.Abort; gdb.Close
	END TestSecondWriterTraps;

END Kv_devBasic.
