MODULE Kv_devTree;
(** Тесты дерева Kv против эталона и учёта свободных страниц. *)

	IMPORT KvPages, KvFault, KvDb, M := Kv_devModel, UnitAssert;

	CONST
		steps = 4000;

	PROCEDURE Commit (tx: KvDb.Tx);
		VAR res: INTEGER;
	BEGIN
		tx.Commit(res); UnitAssert.EqualInt(KvDb.ok, res, "фиксация")
	END Commit;

	PROCEDURE Random (seed, alphabet, maxVal: INTEGER);
		(* случайные Put/Delete/Get по зерну; после каждой фиксации —
			Check и обход против эталона; внутри транзакции — Get и обход *)
		VAR db: KvDb.Db; w: KvDb.Tx; m: M.Model; i, op, res, bad: INTEGER;
			k, v, got: M.Bytes; found: BOOLEAN; msg: ARRAY 64 OF CHAR;
	BEGIN
		M.seed := seed; KvDb.Open(KvFault.New(), db, res); m := M.New();
		w := db.Begin(TRUE); bad := 0;
		FOR i := 1 TO steps DO
			op := M.Rand(10);
			IF (op < 5) OR (m.n = 0) THEN
				k := M.RandBytes(1, 12, alphabet);
				IF M.Rand(50) = 0 THEN v := M.RandBytes(5000, 20000, 26)	(* страницы продолжения *)
				ELSE v := M.RandBytes(1, maxVal, 26)
				END;
				w.Put(k^, v^); m.Put(k^, v^)
			ELSIF op < 8 THEN
				IF M.Rand(2) = 0 THEN k := m.Key(M.Rand(m.n)) ELSE k := M.RandBytes(1, 12, alphabet) END;
				found := m.Delete(k^);
				IF w.Delete(k^) # found THEN INC(bad) END
			ELSE
				k := M.RandBytes(1, 12, alphabet);
				found := w.Get(k^, got);
				IF found # (m.Has(k^)) THEN INC(bad) END
			END;
			IF i MOD 100 = 0 THEN
				IF ~M.Equal(w, m) THEN INC(bad) END;
				Commit(w);
				w := db.Begin(TRUE);
				IF ~w.Check(msg) THEN INC(bad); UnitAssert.EqualStr("", msg, "Check") END;
				IF ~M.Equal(w, m) THEN INC(bad) END
			END
		END;
		Commit(w);
		UnitAssert.EqualInt(0, bad, "расхождений с эталоном");
		M.CheckDb(db, m, "итог");
		db.Close
	END Random;

	PROCEDURE TestRandomDenseKeys*;
	BEGIN
		Random(1, 3, 40)	(* мало букв — много совпадений и удалений *)
	END TestRandomDenseKeys;

	PROCEDURE TestRandomWideKeys*;
	BEGIN
		Random(2, 26, 300)
	END TestRandomWideKeys;

	PROCEDURE TestRandomSmallValues*;
	BEGIN
		Random(3, 10, 8)
	END TestRandomSmallValues;

	PROCEDURE TestDeleteAllShrinksToEmpty*;
		VAR db: KvDb.Db; w: KvDb.Tx; m: M.Model; i, res: INTEGER; k: M.Bytes;
	BEGIN
		M.seed := 5; KvDb.Open(KvFault.New(), db, res); m := M.New();
		w := db.Begin(TRUE);
		FOR i := 1 TO 3000 DO k := M.RandBytes(4, 10, 26); w.Put(k^, k^); m.Put(k^, k^) END;
		Commit(w);
		w := db.Begin(TRUE);
		WHILE m.n > 0 DO k := m.Key(0); UnitAssert.True(m.Delete(k^) & w.Delete(k^), "удаление") END;
		Commit(w);
		M.CheckDb(db, m, "пустая база цела")
	END TestDeleteAllShrinksToEmpty;

	PROCEDURE Key (i: INTEGER; OUT k: ARRAY OF BYTE);
		(* «ev» и номер старшим байтом вперёд: ключи возрастают с i *)
	BEGIN
		KvPages.PutByte(k, 0, 101); KvPages.PutByte(k, 1, 118);
		KvPages.PutByte(k, 2, i DIV 1000000H); KvPages.PutByte(k, 3, i DIV 10000H);
		KvPages.PutByte(k, 4, i DIV 100H); KvPages.PutByte(k, 5, i)
	END Key;

	PROCEDURE AppendPages (n: INTEGER): INTEGER;
		VAR db: KvDb.Db; w: KvDb.Tx; i, res: INTEGER; k: ARRAY 6 OF BYTE; v: ARRAY 40 OF BYTE;
	BEGIN
		KvDb.Open(KvFault.New(), db, res);
		FOR i := 0 TO n - 1 DO
			w := db.Begin(TRUE); Key(i, k); w.Put(k, v); Commit(w)
		END;
		RETURN db.Pages()
	END AppendPages;

	PROCEDURE TestAppendFillsLeftPages*;
		VAR pages: INTEGER;
	BEGIN
		(* 4000 записей по 6 + 40 + 6 байт = 52 байта: около 51 полной страницы
			листьев; пополам их было бы около 100 *)
		pages := AppendPages(4000);
		UnitAssert.True(pages < 80, "дозапись заполняет страницы почти целиком")
	END TestAppendFillsLeftPages;

	(* --- 5. свободные страницы -------------------------------------------- *)

	PROCEDURE Churn (db: KvDb.Db; m: M.Model; rounds: INTEGER);
		(* каждая фиксация вставляет 30 ключей и, когда их больше тысячи,
			удаляет 30: дерево в два уровня, около 30 листьев *)
		VAR w: KvDb.Tx; i, j: INTEGER; k, v: M.Bytes; b: BOOLEAN;
	BEGIN
		FOR i := 1 TO rounds DO
			w := db.Begin(TRUE);
			FOR j := 1 TO 30 DO
				k := M.RandBytes(3, 8, 26); v := M.RandBytes(40, 80, 26); w.Put(k^, v^); m.Put(k^, v^)
			END;
			FOR j := 1 TO 30 DO
				IF m.n > 1000 THEN k := m.Key(M.Rand(m.n)); b := m.Delete(k^); b := w.Delete(k^) END
			END;
			Commit(w)
		END
	END Churn;

	PROCEDURE TestFileDoesNotGrowWithoutReaders*;
		VAR db: KvDb.Db; m: M.Model; res, p1, p2: INTEGER;
	BEGIN
		M.seed := 11; KvDb.Open(KvFault.New(), db, res); m := M.New();
		Churn(db, m, 100); p1 := db.Pages();
		Churn(db, m, 300); p2 := db.Pages();
		UnitAssert.True(p2 <= p1 + 10, "страницы переиспользуются");
		M.CheckDb(db, m, "нет утечек")
	END TestFileDoesNotGrowWithoutReaders;

	PROCEDURE TestReaderHoldsPages*;
		VAR db: KvDb.Db; m, old: M.Model; r: KvDb.Tx; res, p0, p1, p2, p3: INTEGER; msg: ARRAY 64 OF CHAR;
	BEGIN
		M.seed := 12; KvDb.Open(KvFault.New(), db, res); m := M.New();
		Churn(db, m, 60); p0 := db.Pages();
		old := m.Clone(); r := db.Begin(FALSE);
		Churn(db, m, 100); p1 := db.Pages();
		UnitAssert.True(p1 > p0 + 20, "с читателем файл растёт: старые страницы заняты");
		UnitAssert.True(r.Check(msg) & M.Equal(r, old), "снимок читателя цел после 100 фиксаций");
		r.Abort;
		Churn(db, m, 2); p2 := db.Pages();
		Churn(db, m, 200); p3 := db.Pages();
		UnitAssert.True(p3 <= p2 + 10, "после читателя страницы переиспользуются");
		M.CheckDb(db, m, "нет утечек")
	END TestReaderHoldsPages;

END Kv_devTree.
