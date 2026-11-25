MODULE Cas_devChange;
(** Тесты CasChange: запись и чтение набора, публикация через ссылку,
	отказ при устаревшем родителе, перенос (Rebase) с конфликтом и без,
	обход цепочки. *)

	IMPORT CasHash, CasStore, CasChange, UnitAssert;

	VAR
		s: CasStore.Store;
		a, b, c, nil: CasHash.Id;	(* Id содержимого: три разных блока *)

	PROCEDURE PutStr (IN str: ARRAY OF CHAR; OUT id: CasHash.Id);
		VAR d: ARRAY 64 OF BYTE; n, res: INTEGER;
	BEGIN
		n := 0;
		WHILE str[n] # 0X DO d[n] := SHORT(SHORT(ORD(str[n]))); INC(n) END;
		s.Put(d, n, id, res)
	END PutStr;

	PROCEDURE Setup*;
	BEGIN
		s := CasStore.NewMem();
		PutStr("alpha", a); PutStr("beta", b); PutStr("gamma", c);
		CasHash.Clear(nil)
	END Setup;

	PROCEDURE IsHead (IN id: CasHash.Id): BOOLEAN;
		VAR h: CasHash.Id; res: INTEGER;
	BEGIN
		s.GetRef("main", h, res); RETURN CasHash.Equal(id, h)
	END IsHead;

	PROCEDURE Head (OUT h: CasHash.Id);
		VAR res: INTEGER;
	BEGIN
		s.GetRef("main", h, res)
	END Head;

	PROCEDURE Commit (IN parent: CasHash.Id; IN author, name: ARRAY OF CHAR; IN id: CasHash.Id;
		OUT new: CasHash.Id): INTEGER;
		VAR ch: CasChange.Change; res: INTEGER;
	BEGIN
		ch := CasChange.New(parent, author, 1, "");
		ch.Set(name, id);
		CasChange.Commit(s, "main", ch, new, res);
		RETURN res
	END Commit;

	PROCEDURE TestPutGet*;
		VAR ch, back: CasChange.Change; id, x: CasHash.Id; res: INTEGER; e: CasChange.Entry;
	BEGIN
		ch := CasChange.New(a, "Вирт", 1234567890123L, "первый набор");
		ch.Set("файл.txt", b); ch.Set("a.txt", a); ch.Set("файл.txt", c);
		CasChange.Put(s, ch, id, res);
		UnitAssert.EqualInt(CasStore.ok, res, "Put");
		UnitAssert.True(CasHash.Equal(id, ch.id), "id набора");
		CasChange.Get(s, id, back, res);
		UnitAssert.EqualInt(CasStore.ok, res, "Get");
		UnitAssert.True(CasHash.Equal(a, back.parent), "родитель");
		UnitAssert.EqualStr("Вирт", back.author, "автор");
		UnitAssert.EqualInt(1234567890123L, back.time, "время");
		UnitAssert.EqualStr("первый набор", back.note, "пояснение");
		e := back.entries;
		UnitAssert.EqualStr("a.txt", e.name, "по порядку имён");
		e := e.next;
		UnitAssert.EqualStr("файл.txt", e.name, "второе имя");
		UnitAssert.True(CasHash.Equal(c, e.id), "Set заменил значение");
		UnitAssert.True(e.next = NIL, "два имени");
		UnitAssert.True(back.Find("a.txt", x) & CasHash.Equal(a, x), "Find");
		UnitAssert.True(~back.Find("нет", x), "Find нет")
	END TestPutGet;

	PROCEDURE TestNotAChange*;
		VAR ch: CasChange.Change; res: INTEGER;
	BEGIN
		CasChange.Get(s, a, ch, res);
		UnitAssert.EqualInt(CasStore.corrupt, res, "блок не набор");
		UnitAssert.True(ch = NIL, "без набора")
	END TestNotAChange;

	PROCEDURE TestCommitAndStale*;
		VAR h1, h2: CasHash.Id;
	BEGIN
		UnitAssert.EqualInt(CasStore.ok, Commit(nil, "A", "x", a, h1), "первый");
		UnitAssert.True(IsHead(h1), "ссылка = первый");
		UnitAssert.EqualInt(CasChange.conflict, Commit(nil, "B", "y", b, h2), "устаревший родитель");
		UnitAssert.True(IsHead(h1), "отказ не меняет ссылку");
		UnitAssert.EqualInt(CasStore.ok, Commit(h1, "B", "y", b, h2), "от вершины");
		UnitAssert.True(IsHead(h2), "ссылка = второй")
	END TestCommitAndStale;

	PROCEDURE TestRebaseDisjoint*;
		VAR h, h0, h1, h2: CasHash.Id; mine, moved: CasChange.Change; res: INTEGER;
			clash: CasChange.Name; log: CasChange.Change;
	BEGIN
		res := Commit(nil, "A", "base", a, h0);
		res := Commit(h0, "A", "x", b, h1);	(* чужой набор поверх h0 *)
		mine := CasChange.New(h0, "B", 2, "мой");
		mine.Set("y", c);
		Head(h); CasChange.Rebase(s, mine, h, moved, clash, res);
		UnitAssert.EqualInt(CasStore.ok, res, "разные имена — без конфликта");
		UnitAssert.True(CasHash.Equal(h1, moved.parent), "родитель — новая вершина");
		UnitAssert.EqualStr("B", moved.author, "автор сохранён");
		CasChange.Commit(s, "main", moved, h2, res);
		UnitAssert.EqualInt(CasStore.ok, res, "публикация после переноса");
		Head(h); CasChange.Log(s, h, log, res);
		UnitAssert.EqualInt(CasStore.ok, res, "Log");
		UnitAssert.True(CasHash.Equal(h2, log.id), "1");
		UnitAssert.True(CasHash.Equal(h1, log.older.id), "2");
		UnitAssert.True(CasHash.Equal(h0, log.older.older.id), "3");
		UnitAssert.True(log.older.older.older = NIL, "конец цепочки")
	END TestRebaseDisjoint;

	PROCEDURE TestRebaseConflict*;
		VAR h, h0, h1: CasHash.Id; mine, moved: CasChange.Change; res: INTEGER; clash: CasChange.Name;
	BEGIN
		res := Commit(nil, "A", "base", a, h0);
		res := Commit(h0, "A", "x", b, h1);
		mine := CasChange.New(h0, "B", 2, "");
		mine.Set("w", a); mine.Set("x", c);
		Head(h); CasChange.Rebase(s, mine, h, moved, clash, res);
		UnitAssert.EqualInt(CasChange.conflict, res, "оба изменили x по-разному");
		UnitAssert.EqualStr("x", clash, "имя конфликта");
		UnitAssert.True(moved = NIL, "без нового набора")
	END TestRebaseConflict;

	PROCEDURE TestRebaseSameValue*;
	(* оба записали в x одно и то же — это не конфликт *)
		VAR h, h0, h1: CasHash.Id; mine, moved: CasChange.Change; res: INTEGER; clash: CasChange.Name;
	BEGIN
		res := Commit(nil, "A", "base", a, h0);
		res := Commit(h0, "A", "x", b, h1);
		mine := CasChange.New(h0, "B", 2, "");
		mine.Set("x", b);
		Head(h); CasChange.Rebase(s, mine, h, moved, clash, res);
		UnitAssert.EqualInt(CasStore.ok, res, "то же значение")
	END TestRebaseSameValue;

	PROCEDURE TestRebaseUnrelated*;
		VAR h, h0, other: CasHash.Id; mine, moved: CasChange.Change; res: INTEGER; clash: CasChange.Name;
			x: CasChange.Change;
	BEGIN
		res := Commit(nil, "A", "base", a, h0);
		x := CasChange.New(nil, "C", 3, "чужая история"); x.Set("z", c);
		CasChange.Put(s, x, other, res);
		mine := CasChange.New(other, "B", 2, ""); mine.Set("y", b);
		Head(h); CasChange.Rebase(s, mine, h, moved, clash, res);
		UnitAssert.EqualInt(CasChange.unrelated, res, "родитель не в цепочке вершины")
	END TestRebaseUnrelated;

	PROCEDURE TestLogEmpty*;
		VAR log: CasChange.Change; res: INTEGER;
	BEGIN
		CasChange.Log(s, nil, log, res);
		UnitAssert.EqualInt(CasStore.ok, res, "пустая вершина");
		UnitAssert.True(log = NIL, "нет наборов")
	END TestLogEmpty;

END Cas_devChange.
