MODULE CasChange;
(** Неизменяемый набор изменений: родитель, отображение имя -> Id, автор,
	время и пояснение. Набор хранится блоком CasStore; вершина цепочки —
	ссылка. Публикация — CasRef от родителя к новому набору; перенос
	(Rebase) — явное переподчинение своего набора новой вершине с
	проверкой конфликтов по именам. Содержимое не сливается. *)

	IMPORT CasHash, CasStore;

	CONST
		(** res, кроме CasStore.ok, absent, corrupt, failed *)
		conflict* = 10;	(** вершина ушла вперёд или имя изменено по-разному *)
		unrelated* = 11;	(** родитель набора не лежит в цепочке вершины *)

		magic = 43415343H;	(* "CASC" *)
		version = 1;

	TYPE
		Name* = ARRAY 64 OF CHAR;
		Note* = ARRAY 256 OF CHAR;

		Entry* = POINTER TO RECORD
			next-: Entry;
			name-: Name;
			id-: CasHash.Id
		END;

		Change* = POINTER TO RECORD
			id-: CasHash.Id;	(** Id блока набора; пуст, пока набор не записан *)
			parent-: CasHash.Id;	(** пуст у первого набора *)
			author-: Name;
			time-: LONGINT;
			note-: Note;
			entries-: Entry;	(** по возрастанию имён, имена разные *)
			older-: Change	(** заполняет Log *)
		END;

		Buf = RECORD
			b: POINTER TO ARRAY OF BYTE;
			n: INTEGER;	(* запись: длина; чтение: позиция *)
			bad: BOOLEAN	(* чтение вышло за конец *)
		END;

	(* кодирование: целые big-endian, строка — 2 байта длины и по 2 байта на знак *)

	PROCEDURE PutByte (VAR w: Buf; x: INTEGER);
		VAR new: POINTER TO ARRAY OF BYTE; i: INTEGER;
	BEGIN
		IF w.n = LEN(w.b) THEN
			NEW(new, 2 * w.n);
			FOR i := 0 TO w.n - 1 DO new[i] := w.b[i] END;
			w.b := new
		END;
		x := x MOD 256; IF x >= 128 THEN DEC(x, 256) END;
		w.b[w.n] := SHORT(SHORT(x)); INC(w.n)
	END PutByte;

	PROCEDURE PutInt (VAR w: Buf; x: LONGINT; bytes: INTEGER);
	BEGIN
		IF bytes > 1 THEN PutInt(w, x DIV 256, bytes - 1) END;
		PutByte(w, SHORT(x MOD 256))
	END PutInt;

	PROCEDURE PutStr (VAR w: Buf; IN s: ARRAY OF CHAR);
		VAR i, n: INTEGER;
	BEGIN
		n := LEN(s$); PutInt(w, n, 2);
		FOR i := 0 TO n - 1 DO PutInt(w, ORD(s[i]), 2) END
	END PutStr;

	PROCEDURE PutId (VAR w: Buf; IN id: CasHash.Id);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO CasHash.size - 1 DO PutByte(w, id[i]) END
	END PutId;

	PROCEDURE GetInt (VAR r: Buf; bytes: INTEGER): LONGINT;
		VAR x: LONGINT;
	BEGIN
		x := 0;
		IF r.n + bytes > LEN(r.b) THEN r.bad := TRUE
		ELSE
			WHILE bytes > 0 DO x := x * 256 + r.b[r.n] MOD 256; INC(r.n); DEC(bytes) END
		END;
		RETURN x
	END GetInt;

	PROCEDURE GetStr (VAR r: Buf; OUT s: ARRAY OF CHAR);
		VAR i, n: INTEGER;
	BEGIN
		n := SHORT(GetInt(r, 2));
		IF n >= LEN(s) THEN r.bad := TRUE; n := 0 END;
		FOR i := 0 TO n - 1 DO s[i] := CHR(SHORT(GetInt(r, 2))) END;
		s[n] := 0X
	END GetStr;

	PROCEDURE GetId (VAR r: Buf; OUT id: CasHash.Id);
		VAR i: INTEGER;
	BEGIN
		IF r.n + CasHash.size > LEN(r.b) THEN r.bad := TRUE; CasHash.Clear(id)
		ELSE
			FOR i := 0 TO CasHash.size - 1 DO id[i] := r.b[r.n]; INC(r.n) END
		END
	END GetId;

	PROCEDURE Fits (IN s: ARRAY OF CHAR; max: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < LEN(s)) & (s[i] # 0X) DO INC(i) END;
		RETURN i < max
	END Fits;

	(* наборы *)

	PROCEDURE New* (IN parent: CasHash.Id; IN author: ARRAY OF CHAR; time: LONGINT;
		IN note: ARRAY OF CHAR): Change;
	(** Новый пустой набор поверх parent (пустой Id — набор без родителя).
		Пред: author короче 64 знаков — 20; note короче 256 знаков — 21.
		Пост: результат # NIL, entries = NIL, id пуст. *)
		VAR c: Change;
	BEGIN
		ASSERT(Fits(author, LEN(Name)), 20); ASSERT(Fits(note, LEN(Note)), 21);
		NEW(c); c.parent := parent; c.author := author$; c.time := time; c.note := note$;
		CasHash.Clear(c.id);
		RETURN c
	END New;

	PROCEDURE (c: Change) Set* (IN name: ARRAY OF CHAR; IN id: CasHash.Id), NEW;
	(** Имя name получает значение id; прежнее значение имени заменяется.
		Пред: name из 1..63 знаков — 22; набор не записан (id пуст) — 23.
		Пост: Find(name) = id; entries по возрастанию имён. *)
		VAR e, p, q: Entry;
	BEGIN
		ASSERT((name # "") & Fits(name, LEN(Name)), 22); ASSERT(CasHash.IsNil(c.id), 23);
		p := NIL; q := c.entries;
		WHILE (q # NIL) & (q.name < name) DO p := q; q := q.next END;
		IF (q # NIL) & (q.name = name) THEN q.id := id
		ELSE
			NEW(e); e.name := name$; e.id := id; e.next := q;
			IF p = NIL THEN c.entries := e ELSE p.next := e END
		END
	END Set;

	PROCEDURE (c: Change) Find* (IN name: ARRAY OF CHAR; OUT id: CasHash.Id): BOOLEAN, NEW;
	(** Пост: TRUE — набор задаёт имени name значение id; FALSE — имени нет, id пуст. *)
		VAR e: Entry;
	BEGIN
		e := c.entries;
		WHILE (e # NIL) & (e.name # name) DO e := e.next END;
		IF e = NIL THEN CasHash.Clear(id); RETURN FALSE END;
		id := e.id; RETURN TRUE
	END Find;

	PROCEDURE Put* (s: CasStore.Store; c: Change; OUT id: CasHash.Id; OUT res: INTEGER);
	(** Записать набор блоком; ссылки не трогаются.
		Пред: s # NIL — 20; c # NIL — 21.
		Пост: res = CasStore.ok: id = c.id — Id блока набора. *)
		VAR w: Buf; e: Entry; n: INTEGER;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(c # NIL, 21);
		NEW(w.b, 256); w.n := 0;
		PutInt(w, magic, 4); PutByte(w, version); PutId(w, c.parent); PutInt(w, c.time, 8);
		PutStr(w, c.author); PutStr(w, c.note);
		n := 0; e := c.entries; WHILE e # NIL DO INC(n); e := e.next END;
		PutInt(w, n, 4);
		e := c.entries;
		WHILE e # NIL DO PutStr(w, e.name); PutId(w, e.id); e := e.next END;
		s.Put(w.b, w.n, id, res);
		IF res = CasStore.ok THEN c.id := id END
	END Put;

	PROCEDURE Get* (s: CasStore.Store; IN id: CasHash.Id; OUT c: Change; OUT res: INTEGER);
	(** Прочитать набор.
		Пред: s # NIL — 20.
		Пост: res = CasStore.ok: c.id = id, поля — как при Put.
		Пост: res = corrupt — блок не набор; absent, failed — от хранилища; c = NIL. *)
		VAR r: Buf; e, last: Entry; n: INTEGER;
	BEGIN
		ASSERT(s # NIL, 20);
		c := NIL; s.Get(id, r.b, res);
		IF res # CasStore.ok THEN RETURN END;
		r.n := 0; r.bad := FALSE;
		IF (GetInt(r, 4) # magic) OR (GetInt(r, 1) # version) THEN res := CasStore.corrupt; RETURN END;
		NEW(c); c.id := id;
		GetId(r, c.parent); c.time := GetInt(r, 8); GetStr(r, c.author); GetStr(r, c.note);
		n := SHORT(GetInt(r, 4)); last := NIL;
		WHILE ~r.bad & (n > 0) DO
			NEW(e); GetStr(r, e.name); GetId(r, e.id);
			IF last = NIL THEN c.entries := e ELSE last.next := e END;
			last := e; DEC(n)
		END;
		IF r.bad OR (r.n # LEN(r.b)) THEN c := NIL; res := CasStore.corrupt END
	END Get;

	PROCEDURE Commit* (s: CasStore.Store; IN ref: ARRAY OF CHAR; c: Change; OUT id: CasHash.Id;
		OUT res: INTEGER);
	(** Записать набор и опубликовать его: ссылка ref переходит от c.parent к набору.
		Пред: s # NIL — 20; c # NIL — 21; CasStore.ValidRef(ref) — 22.
		Пост: res = CasStore.ok: ref = id = c.id.
		Пост: res = conflict: ref # c.parent (вершина ушла вперёд), ссылка не изменилась;
		блок набора записан, его можно перенести (Rebase).
		Пост: иначе — ошибка хранилища, ссылка не изменилась. *)
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(c # NIL, 21); ASSERT(CasStore.ValidRef(ref), 22);
		Put(s, c, id, res);
		IF (res = CasStore.ok) & ~s.CasRef(ref, c.parent, id) THEN res := conflict END
	END Commit;

	PROCEDURE Rebase* (s: CasStore.Store; c: Change; IN head: CasHash.Id; OUT new: Change;
		OUT clash: Name; OUT res: INTEGER);
	(** Перенести набор c на вершину head: те же имена, автор, время и пояснение,
		родитель — head.
		Пред: s # NIL — 20; c # NIL — 21.
		Пост: res = CasStore.ok: new.parent = head, new.entries = c.entries, new не записан.
		Пост: res = conflict: набор между c.parent (не включая) и head (включая) задаёт имени
		clash другое значение, чем c; new = NIL.
		Пост: res = unrelated: c.parent не встречается в цепочке head; new = NIL.
		Пост: absent, corrupt, failed — ошибка чтения цепочки; new = NIL. *)
		TYPE Seen = POINTER TO RECORD next: Seen; name: Name END;
		VAR cur: CasHash.Id; ch: Change; e: Entry; seen, x: Seen; mine: CasHash.Id;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(c # NIL, 21);
		new := NIL; clash := ""; res := CasStore.ok; seen := NIL; cur := head;
		WHILE (res = CasStore.ok) & ~CasHash.Equal(cur, c.parent) DO
			IF CasHash.IsNil(cur) THEN res := unrelated
			ELSE
				Get(s, cur, ch, res);
				IF res = CasStore.ok THEN
					e := ch.entries;
					WHILE (res = CasStore.ok) & (e # NIL) DO
						x := seen; WHILE (x # NIL) & (x.name # e.name) DO x := x.next END;
						IF x = NIL THEN	(* самое новое чужое значение имени *)
							NEW(x); x.name := e.name; x.next := seen; seen := x;
							IF c.Find(e.name, mine) & ~CasHash.Equal(mine, e.id) THEN
								clash := e.name$; res := conflict
							END
						END;
						e := e.next
					END;
					cur := ch.parent
				END
			END
		END;
		IF res = CasStore.ok THEN
			new := New(head, c.author, c.time, c.note);
			e := c.entries;
			WHILE e # NIL DO new.Set(e.name, e.id); e := e.next END
		END
	END Rebase;

	PROCEDURE Log* (s: CasStore.Store; IN head: CasHash.Id; OUT first: Change; OUT res: INTEGER);
	(** Цепочка наборов от head к первому.
		Пред: s # NIL — 20.
		Пост: res = CasStore.ok: first — набор head, older — его родитель, и так до набора
		без родителя (older = NIL); head пуст — first = NIL.
		Пост: иначе — ошибка чтения, first = NIL. *)
		VAR cur: CasHash.Id; ch, last: Change;
	BEGIN
		ASSERT(s # NIL, 20);
		first := NIL; last := NIL; res := CasStore.ok; cur := head;
		WHILE (res = CasStore.ok) & ~CasHash.IsNil(cur) DO
			Get(s, cur, ch, res);
			IF res = CasStore.ok THEN
				IF last = NIL THEN first := ch ELSE last.older := ch END;
				last := ch; cur := ch.parent
			END
		END;
		IF res # CasStore.ok THEN first := NIL END
	END Log;

END CasChange.
