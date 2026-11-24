# Overview by Example: ObxKv1

An event log: appending by key "name + number", reading the tail, a reader's snapshot during writing.

An event log is a common case for a store: events are only appended, and they are read by name and
number. The example writes 1000 `cpu` events in ten transactions and 50 `net` events, reads the last
three `cpu` events and shows that a reader begun before new writing does not see it. The example
belongs to the guide `Kv/Docu/Kv.md`, sections 2 and 5.

```
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
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Kv1.cp`; subsystem `Kv` is built
for Linux). In the BlackBox window a commander executes the command:

```
   ObxKv1.Do
```

In the console:

```sh
echo 'ObxKv1.Do' | tools/run64.sh
```

The log shows:

```
cpu events: 1000
cpu #997 = 994009
cpu #998 = 996004
cpu #999 = 998001
old reader sees: 1000
new reader sees: 1500
```

## What happened

An event key is the name, a zero byte and the number in four bytes, most significant first (`Key`).
The bytewise order of keys then coincides with the order of numbers, and the zero byte separates the
name: all `cpu` events lie together, before the `net` events. Walk `Count` starts at number 0 and goes
while the key belongs to the name (`Of`).

Events with ascending numbers always land at the right edge of the tree. A node that was appended to
is split into nearly full pieces, so the log occupies few pages.

The tail is read the same way: `Seek` to number 997 and a walk to the end of the name. The value of
an event is the square of the number as a string.

Then reader `r` stays open while `Append` commits 500 more events. The writer does not touch the pages
`r` sees, and `r` still counts 1000 events: this is its snapshot. A new reader sees 1500. While `r` is
open, pages freed by the writer are not reused; therefore readers are closed as soon as they are not
needed.

In this example we have seen how to compose a log key, read the tail, and how a reader sees a snapshot without noticing the writer.
