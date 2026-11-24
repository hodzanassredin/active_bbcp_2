MODULE Kv_devCrash;
(** Главный тест Kv: выключение питания в каждой точке фиксации.

	Для каждой транзакции из ряда тест сначала считает события фиксации
	(записи страниц и Sync) на исправной копии хранилища. Затем для каждого
	k он берёт копию состояния до транзакции, обрывает событие k (целиком
	или посреди страницы), выключает питание (несинхронизированное пропадает
	целиком или по жребию), открывает базу заново и требует: Check цел, а
	содержимое равно состоянию до транзакции или после неё. *)

	IMPORT KvPages, KvFault, KvDb, M := Kv_devModel, UnitAssert;

	CONST
		txCount = 16;

	VAR
		checked, before, after: INTEGER;
		Keep: ARRAY 3 OF INTEGER;

	PROCEDURE RunTx (db: KvDb.Db; m: M.Model; seed: INTEGER; OUT res: INTEGER);
		(* случайный набор изменений по зерну, затем Commit *)
		VAR w: KvDb.Tx; i, n: INTEGER; k, v: M.Bytes; b: BOOLEAN;
	BEGIN
		M.seed := seed; w := db.Begin(TRUE); n := 1 + M.Rand(120);
		FOR i := 1 TO n DO
			IF (M.Rand(3) > 0) OR (m.n = 0) THEN
				k := M.RandBytes(1, 8, 6);
				IF M.Rand(20) = 0 THEN v := M.RandBytes(4000, 9000, 26) ELSE v := M.RandBytes(1, 200, 26) END;
				w.Put(k^, v^); m.Put(k^, v^)
			ELSE
				k := m.Key(M.Rand(m.n)); b := m.Delete(k^); b := w.Delete(k^)
			END
		END;
		w.Commit(res)
	END RunTx;

	PROCEDURE Verify (s: KvFault.Store; old, new: M.Model; IN what: ARRAY OF CHAR);
		VAR db: KvDb.Db; r: KvDb.Tx; res: INTEGER; msg: ARRAY 64 OF CHAR;
	BEGIN
		KvDb.Open(s, db, res);
		IF res # KvDb.ok THEN UnitAssert.EqualInt(KvDb.ok, res, what)
		ELSE
			r := db.Begin(FALSE);
			IF ~r.Check(msg) THEN UnitAssert.EqualStr("", msg, what)
			ELSIF M.Equal(r, old) THEN INC(before)
			ELSIF M.Equal(r, new) THEN INC(after)
			ELSE UnitAssert.Fail(what)
			END;
			INC(checked); r.Abort; db.Close
		END
	END Verify;

	PROCEDURE CrashAll (seed: INTEGER);
		VAR base, s: KvFault.Store; db: KvDb.Db; m, next: M.Model; t, k, events, res, rnd: INTEGER;
			v: INTEGER; what: ARRAY 64 OF CHAR;
	BEGIN
		base := KvFault.New(); KvDb.Open(base, db, res); db.Close; m := M.New();
		FOR t := 1 TO txCount DO
			(* число событий фиксации на исправной копии *)
			s := base.Clone(); KvDb.Open(s, db, res); next := m.Clone();
			s.Arm(-1, -1); RunTx(db, next, seed * 100 + t, res); events := s.events;
			UnitAssert.EqualInt(KvDb.ok, res, "фиксация без сбоя");
			FOR k := 0 TO events - 1 DO
				FOR v := 0 TO 5 DO	(* обрыв: вся запись, 30 байтов (середина меты), полстраницы; питание: всё пропало, жребий *)
					s := base.Clone(); KvDb.Open(s, db, res);
					s.Arm(k, Keep[v DIV 2]); RunTx(db, m.Clone(), seed * 100 + t, res);
					UnitAssert.True(res # KvDb.ok, "сбой виден в res");
					s.PowerOff((v MOD 2) * (k + 1) * 7919 * t);
					what := "обрыв"; Verify(s, m, next, what)
				END
			END;
			(* шаг вперёд: фиксация без сбоя *)
			KvDb.Open(base, db, res); RunTx(db, m, seed * 100 + t, res); db.Close;
			base.PowerOff(t);	(* зафиксированное переживает питание *)
			Verify(base, next, next, "после шага")
		END
	END CrashAll;

	PROCEDURE TestPowerOffAtEveryCommitPoint*;
	BEGIN
		Keep[0] := -1; Keep[1] := 30; Keep[2] := KvPages.size DIV 2;
		checked := 0; before := 0; after := 0;
		CrashAll(1); CrashAll(2); CrashAll(3);
		UnitAssert.True(checked > 200, "точек обрыва проверено");
		UnitAssert.True((before > 0) & (after > 0), "встречаются и «до», и «после»")
	END TestPowerOffAtEveryCommitPoint;

	PROCEDURE TestFailedCommitPoisonsDb*;
		VAR s: KvFault.Store; db: KvDb.Db; w: KvDb.Tx; m: M.Model; res: INTEGER;
	BEGIN
		s := KvFault.New(); KvDb.Open(s, db, res); m := M.New();
		s.Arm(0, -1); RunTx(db, m, 9, res);
		UnitAssert.EqualInt(KvDb.ioError, res, "запись не удалась");
		w := db.Begin(TRUE);
		UnitAssert.EqualInt(KvDb.ioError, w.res, "база после сбоя требует переоткрытия");
		w.Abort
	END TestFailedCommitPoisonsDb;

END Kv_devCrash.
