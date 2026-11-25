MODULE Kv_devTasks;
(** Тесты Kv с задачами и трапами: Update и View закрывают транзакцию при
	трапе тела и передают трап с тем же номером; второй писатель из другой
	задачи ждёт по порядку прихода; отменённый ждущий уходит из очереди. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, KvFault, KvDb, M := Kv_devModel, UnitAssert;

	TYPE
		Fill = POINTER TO RECORD (KvDb.Body) n: INTEGER; trap: BOOLEAN END;
		Look = POINTER TO RECORD (KvDb.Body) END;
		Writer = POINTER TO RECORD (Tasks.Task) id, ms: INTEGER END;
		Twice = POINTER TO RECORD (Tasks.Task) code: INTEGER END;

	VAR
		db: KvDb.Db;
		order: ARRAY 8 OF INTEGER; nOrder, inside, maxInside: INTEGER;
		body: KvDb.Body; viewBody: BOOLEAN;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Services.actionHook.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Key (i: INTEGER): M.Bytes;
		VAR s: ARRAY 8 OF CHAR;
	BEGIN
		s := "k0"; s[1] := CHR(ORD("0") + i); RETURN KvDb.Str(s)
	END Key;

	PROCEDURE (f: Fill) Do (tx: KvDb.Tx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO f.n - 1 DO tx.Put(Key(i)^, Key(i)^) END;
		IF f.trap THEN HALT(42) END
	END Do;

	PROCEDURE (l: Look) Do (tx: KvDb.Tx);
	BEGIN
		HALT(43)
	END Do;

	PROCEDURE Call (a, b, c: S.ADDRESS);
		VAR res: INTEGER;
	BEGIN
		IF viewBody THEN KvDb.View(db, body) ELSE KvDb.Update(db, body, res) END;
		S.PUT(a, TRUE)
	END Call;

	PROCEDURE TrapCode (b: KvDb.Body; view: BOOLEAN): INTEGER;
		(* номер трапа Update/View или -1, если трапа не было *)
		VAR ok: BOOLEAN;
	BEGIN
		body := b; viewBody := view; ok := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(Call)), S.ADR(ok), 0, 0);
		IF ok THEN RETURN -1 ELSE RETURN Kernel.err END
	END TrapCode;

	PROCEDURE TestUpdateTrapRollsBack*;
		VAR f: Fill; res, pages: INTEGER; m: M.Model; w: KvDb.Tx;
	BEGIN
		KvDb.Open(KvFault.New(), db, res); m := M.New();
		NEW(f); f.n := 3; KvDb.Update(db, f, res);
		UnitAssert.EqualInt(KvDb.ok, res, "Update без трапа фиксирует");
		m.Put(Key(0)^, Key(0)^); m.Put(Key(1)^, Key(1)^); m.Put(Key(2)^, Key(2)^);
		pages := db.Pages();
		NEW(f); f.n := 9; f.trap := TRUE;
		UnitAssert.EqualInt(42, TrapCode(f, FALSE), "трап тела передан с тем же номером");
		w := db.Begin(TRUE);	(* база не заперта *)
		UnitAssert.True(w.res = KvDb.ok, "следующая пишущая начинается");
		w.Abort;
		M.CheckDb(db, m, "изменения тела откатаны, утечек нет");
		UnitAssert.EqualInt(pages, db.Pages(), "файл не вырос");
		db.Close
	END TestUpdateTrapRollsBack;

	PROCEDURE TestViewTrapClosesReader*;
		VAR l: Look; res: INTEGER;
	BEGIN
		KvDb.Open(KvFault.New(), db, res); NEW(l);
		UnitAssert.EqualInt(43, TrapCode(l, TRUE), "трап тела View передан");
		db.Close;	(* трап 22, если читатель остался открытым *)
		UnitAssert.Checked
	END TestViewTrapClosesReader;

	PROCEDURE (w: Writer) Run;
		VAR tx: KvDb.Tx; res: INTEGER;
	BEGIN
		tx := db.Begin(TRUE);
		INC(inside); maxInside := MAX(maxInside, inside);
		tx.Put(Key(w.id)^, Key(w.id)^);
		Tasks.Sleep(w.ms);	(* точка переключения внутри транзакции *)
		order[nOrder] := w.id; INC(nOrder); DEC(inside);
		tx.Commit(res)
	END Run;

	PROCEDURE StartWriter (id, ms: INTEGER): Writer;
		VAR w: Writer;
	BEGIN
		NEW(w); w.id := id; w.ms := ms; Tasks.Start(w);
		RETURN w
	END StartWriter;

	PROCEDURE Reset;
		VAR res: INTEGER;
	BEGIN
		KvDb.Open(KvFault.New(), db, res); nOrder := 0; inside := 0; maxInside := 0
	END Reset;

	PROCEDURE TestWritersQueueFifo*;
		VAR a, b, c: Writer; m: M.Model; i: INTEGER;
	BEGIN
		Reset;
		a := StartWriter(1, 30); b := StartWriter(2, 5); c := StartWriter(3, 5);
		UnitAssert.True(Pump(5000), "писатели закончили");
		UnitAssert.EqualInt(3, nOrder, "все три зафиксировались");
		UnitAssert.EqualInt(1, maxInside, "транзакции не перекрываются");
		FOR i := 0 TO 2 DO UnitAssert.EqualInt(i + 1, order[i], "порядок прихода") END;
		m := M.New(); FOR i := 1 TO 3 DO m.Put(Key(i)^, Key(i)^) END;
		M.CheckDb(db, m, "все изменения на месте")
	END TestWritersQueueFifo;

	PROCEDURE TestCancelledWaiterLeavesQueue*;
		VAR a, b, c: Writer; m: M.Model; i: INTEGER;
	BEGIN
		Reset;
		a := StartWriter(1, 50); b := StartWriter(2, 5); c := StartWriter(3, 5);
		WHILE (Tasks.State(a) # Tasks.Sleeping) & (Tasks.Count() > 0) DO Services.actionHook.Step END;
		Services.actionHook.Step; Services.actionHook.Step;
		Tasks.Cancel(b);
		UnitAssert.True(Pump(5000), "задачи закончили");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(b), "отменённый ждущий прерван");
		UnitAssert.EqualInt(2, nOrder, "зафиксировались двое");
		UnitAssert.EqualInt(1, order[0], "первый"); UnitAssert.EqualInt(3, order[1], "третий после первого");
		m := M.New(); FOR i := 1 TO 3 BY 2 DO m.Put(Key(i)^, Key(i)^) END;
		M.CheckDb(db, m, "изменений отменённого нет")
	END TestCancelledWaiterLeavesQueue;

	PROCEDURE BeginWrite (a, b, c: S.ADDRESS);
		VAR tx: KvDb.Tx;
	BEGIN
		tx := db.Begin(TRUE); S.PUT(a, TRUE)
	END BeginWrite;

	PROCEDURE (t: Twice) Run;
		VAR tx: KvDb.Tx; ok: BOOLEAN;
	BEGIN
		tx := db.Begin(TRUE); ok := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(BeginWrite)), S.ADR(ok), 0, 0);
		IF ok THEN t.code := -1 ELSE t.code := Kernel.err END;
		tx.Abort
	END Run;

	PROCEDURE TestSameTaskSecondWriterTraps*;
		VAR t: Twice;
	BEGIN
		Reset; NEW(t); Tasks.Start(t);
		UnitAssert.True(Pump(5000), "задача закончила");
		UnitAssert.EqualInt(21, t.code, "вторая пишущая той же задачи — трап 21")
	END TestSameTaskSecondWriterTraps;

END Kv_devTasks.
