MODULE ObxKv1;
	IMPORT Console, Strings, KvStore, KvLin, KvDb;

	CONST path = "/tmp/obxkv1.db";

	VAR db: KvDb.Db;

	PROCEDURE Key (IN name: ARRAY OF CHAR; n: INTEGER; OUT k: KvDb.Bytes);
		(* имя, байт 0 и номер старшим байтом вперёд: события одного имени
			лежат подряд и по возрастанию номера *)
		VAR i, len, b: INTEGER;
	BEGIN
		len := LEN(name$); NEW(k, len + 5);
		FOR i := 0 TO len - 1 DO k[i] := SHORT(SHORT(ORD(name[i]))) END;
		k[len] := 0;
		FOR i := len + 4 TO len + 1 BY -1 DO
			b := n MOD 256; n := n DIV 256;
			IF b > 127 THEN b := b - 256 END;
			k[i] := SHORT(SHORT(b))
		END
	END Key;

	PROCEDURE Number (k: KvDb.Bytes): INTEGER;
		VAR i, n: INTEGER;
	BEGIN
		n := 0;
		FOR i := LEN(k) - 4 TO LEN(k) - 1 DO n := n * 256 + k[i] MOD 256 END;
		RETURN n
	END Number;

	PROCEDURE Of (k: KvDb.Bytes; IN name: ARRAY OF CHAR): BOOLEAN;
		(* ключ k — событие с именем name *)
		VAR i, len: INTEGER;
	BEGIN
		len := LEN(name$);
		IF LEN(k) # len + 5 THEN RETURN FALSE END;
		FOR i := 0 TO len - 1 DO
			IF k[i] # ORD(name[i]) THEN RETURN FALSE END
		END;
		RETURN k[len] = 0
	END Of;

	PROCEDURE Append (IN name: ARRAY OF CHAR; from, to: INTEGER);
		VAR tx: KvDb.Tx; k: KvDb.Bytes; s: ARRAY 16 OF CHAR; i, res: INTEGER;
	BEGIN
		tx := db.Begin(TRUE);
		FOR i := from TO to DO
			Key(name, i, k); Strings.IntToString(i * i, s); tx.Put(k^, KvDb.Str(s)^)
		END;
		tx.Commit(res)
	END Append;

	PROCEDURE Count (tx: KvDb.Tx; IN name: ARRAY OF CHAR): INTEGER;
		VAR c: KvDb.Cursor; k: KvDb.Bytes; n: INTEGER;
	BEGIN
		Key(name, 0, k); c := tx.Seek(k^); n := 0;
		WHILE c.Valid() & Of(c.Key(), name) DO INC(n); c.Next END;
		RETURN n
	END Count;

	PROCEDURE WriteCount (IN title: ARRAY OF CHAR; n: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(n, s);
		Console.WriteStr(title); Console.WriteStr(s); Console.WriteLn
	END WriteCount;

	PROCEDURE Do*;
		VAR s: KvStore.Store; r: KvDb.Tx; c: KvDb.Cursor; k: KvDb.Bytes;
			res, i: INTEGER; v, n: ARRAY 16 OF CHAR;
	BEGIN
		KvLin.Delete(path); KvLin.Open(path, s, res); KvDb.Open(s, db, res);
		FOR i := 0 TO 9 DO Append("cpu", i * 100, i * 100 + 99) END;
		Append("net", 0, 49);
		r := db.Begin(FALSE);
		WriteCount("cpu events: ", Count(r, "cpu"));
		Key("cpu", 997, k); c := r.Seek(k^);
		WHILE c.Valid() & Of(c.Key(), "cpu") DO
			KvDb.ToStr(c.Val(), v); Strings.IntToString(Number(c.Key()), n);
			Console.WriteStr("cpu #"); Console.WriteStr(n); Console.WriteStr(" = ");
			Console.WriteStr(v); Console.WriteLn;
			c.Next
		END;
		Append("cpu", 1000, 1499);
		WriteCount("old reader sees: ", Count(r, "cpu"));
		r.Abort; r := db.Begin(FALSE);
		WriteCount("new reader sees: ", Count(r, "cpu"));
		r.Abort; db.Close; KvLin.Delete(path)
	END Do;

END ObxKv1.
