MODULE Kv_devModel;
(** Эталон для тестов Kv: отсортированный массив пар, жребий по зерну,
	сравнение базы с эталоном. *)

	IMPORT KvPages, KvDb, UnitAssert;

	TYPE
		Bytes* = KvPages.Bytes;

		Model* = POINTER TO RECORD
			n-: INTEGER;
			keys, vals: POINTER TO ARRAY OF Bytes
		END;

	VAR
		seed*: INTEGER;

	PROCEDURE Rand* (n: INTEGER): INTEGER;
	(** Пост: 0 <= результат < n; последовательность задана seed. *)
	BEGIN
		seed := SHORT((LONG(seed) * 1103515245 + 12345) MOD 40000000H);
		RETURN seed DIV 256 MOD n
	END Rand;

	PROCEDURE New* (): Model;
		VAR m: Model;
	BEGIN
		NEW(m); NEW(m.keys, 16); NEW(m.vals, 16);
		RETURN m
	END New;

	PROCEDURE (m: Model) Clone* (): Model, NEW;
		VAR c: Model; i: INTEGER;
	BEGIN
		NEW(c); NEW(c.keys, LEN(m.keys^)); NEW(c.vals, LEN(m.keys^)); c.n := m.n;
		FOR i := 0 TO m.n - 1 DO c.keys[i] := m.keys[i]; c.vals[i] := m.vals[i] END;
		RETURN c
	END Clone;

	PROCEDURE (m: Model) Find (IN k: ARRAY OF BYTE; OUT found: BOOLEAN): INTEGER, NEW;
		VAR lo, hi, mid, c: INTEGER;
	BEGIN
		lo := 0; hi := m.n; found := FALSE;
		WHILE lo < hi DO
			mid := (lo + hi) DIV 2;
			c := KvPages.Compare(m.keys[mid]^, LEN(m.keys[mid]^), k, LEN(k));
			IF c < 0 THEN lo := mid + 1 ELSE hi := mid END
		END;
		found := (lo < m.n) & (KvPages.Compare(m.keys[lo]^, LEN(m.keys[lo]^), k, LEN(k)) = 0);
		RETURN lo
	END Find;

	PROCEDURE (m: Model) Has* (IN k: ARRAY OF BYTE): BOOLEAN, NEW;
		VAR i: INTEGER; found: BOOLEAN;
	BEGIN
		i := m.Find(k, found);
		RETURN found
	END Has;

	PROCEDURE (m: Model) Put* (IN k, v: ARRAY OF BYTE), NEW;
		VAR i, j: INTEGER; found: BOOLEAN; nk, nv: POINTER TO ARRAY OF Bytes;
	BEGIN
		i := m.Find(k, found);
		IF found THEN m.vals[i] := KvPages.Copy(v, 0, LEN(v))
		ELSE
			IF m.n = LEN(m.keys^) THEN
				NEW(nk, 2 * m.n); NEW(nv, 2 * m.n);
				FOR j := 0 TO m.n - 1 DO nk[j] := m.keys[j]; nv[j] := m.vals[j] END;
				m.keys := nk; m.vals := nv
			END;
			FOR j := m.n TO i + 1 BY -1 DO m.keys[j] := m.keys[j - 1]; m.vals[j] := m.vals[j - 1] END;
			m.keys[i] := KvPages.Copy(k, 0, LEN(k)); m.vals[i] := KvPages.Copy(v, 0, LEN(v));
			INC(m.n)
		END
	END Put;

	PROCEDURE (m: Model) Delete* (IN k: ARRAY OF BYTE): BOOLEAN, NEW;
		VAR i, j: INTEGER; found: BOOLEAN;
	BEGIN
		i := m.Find(k, found);
		IF found THEN
			FOR j := i TO m.n - 2 DO m.keys[j] := m.keys[j + 1]; m.vals[j] := m.vals[j + 1] END;
			DEC(m.n)
		END;
		RETURN found
	END Delete;

	PROCEDURE (m: Model) Key* (i: INTEGER): Bytes, NEW;
	BEGIN
		RETURN m.keys[i]
	END Key;

	PROCEDURE Same* (a, b: Bytes): BOOLEAN;
	BEGIN
		RETURN (a # NIL) & (b # NIL) & (KvPages.Compare(a^, LEN(a^), b^, LEN(b^)) = 0)
	END Same;

	PROCEDURE RandBytes* (minLen, maxLen, alphabet: INTEGER): Bytes;
	(** Пост: случайная строка длиной minLen..maxLen из первых alphabet букв. *)
		VAR b: Bytes; i: INTEGER;
	BEGIN
		NEW(b, minLen + Rand(maxLen - minLen + 1));
		FOR i := 0 TO LEN(b^) - 1 DO KvPages.PutByte(b^, i, 97 + Rand(alphabet)) END;
		RETURN b
	END RandBytes;

	PROCEDURE Equal* (tx: KvDb.Tx; m: Model): BOOLEAN;
	(** Пост: обход вперёд и назад и Get дают ровно пары эталона. *)
		VAR c: KvDb.Cursor; i: INTEGER; ok: BOOLEAN; v: Bytes;
	BEGIN
		c := tx.First(); i := 0; ok := TRUE;
		WHILE ok & c.Valid() DO
			ok := (i < m.n) & Same(c.Key(), m.keys[i]) & Same(c.Val(), m.vals[i]);
			c.Next; INC(i)
		END;
		ok := ok & (i = m.n);
		c := tx.Last(); i := m.n - 1;
		WHILE ok & c.Valid() DO
			ok := (i >= 0) & Same(c.Key(), m.keys[i]); c.Prev; DEC(i)
		END;
		ok := ok & (i = -1);
		i := 0;
		WHILE ok & (i < m.n) DO
			ok := tx.Get(m.keys[i]^, v) & Same(v, m.vals[i]); INC(i)
		END;
		RETURN ok
	END Equal;

	PROCEDURE CheckDb* (db: KvDb.Db; m: Model; IN what: ARRAY OF CHAR);
	(** Проверка целостности снимка и равенства эталону — одна проверка Unit. *)
		VAR tx: KvDb.Tx; msg: ARRAY 64 OF CHAR; good: BOOLEAN;
	BEGIN
		tx := db.Begin(FALSE);
		good := tx.Check(msg);
		IF ~good THEN UnitAssert.EqualStr("", msg, what)
		ELSE UnitAssert.True(Equal(tx, m), what)
		END;
		tx.Abort
	END CheckDb;

END Kv_devModel.
