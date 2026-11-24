# Overview by Example: ObxKv0

Open a database, write pairs, read one and walk a range of keys.

The example opens the database file `/tmp/obxkv0.db`, writes four pairs in one writing transaction,
then in a reading transaction looks up one key and walks the keys from `fruit/b`. It shows the whole
path of working with `Kv`: store, database, transaction, cursor. The example belongs to the guide
`Kv/Docu/Kv.md`.

```
MODULE ObxKv0;
	IMPORT Console, KvStore, KvLin, KvDb;

	CONST path = "/tmp/obxkv0.db";

	VAR db: KvDb.Db;

	PROCEDURE Put (tx: KvDb.Tx; IN key, val: ARRAY OF CHAR);
	BEGIN
		tx.Put(KvDb.Str(key)^, KvDb.Str(val)^)
	END Put;

	PROCEDURE Show (c: KvDb.Cursor);
		VAR k, v: ARRAY 64 OF CHAR;
	BEGIN
		KvDb.ToStr(c.Key(), k); KvDb.ToStr(c.Val(), v);
		Console.WriteStr(k); Console.WriteStr(" = "); Console.WriteStr(v); Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR s: KvStore.Store; tx: KvDb.Tx; c: KvDb.Cursor; res: INTEGER;
			v: KvDb.Bytes; str: ARRAY 64 OF CHAR;
	BEGIN
		KvLin.Delete(path);
		KvLin.Open(path, s, res); KvDb.Open(s, db, res);
		tx := db.Begin(TRUE);
		Put(tx, "fruit/apple", "red"); Put(tx, "fruit/banana", "yellow");
		Put(tx, "fruit/cherry", "dark red"); Put(tx, "veg/carrot", "orange");
		tx.Commit(res);
		tx := db.Begin(FALSE);
		IF tx.Get(KvDb.Str("fruit/banana")^, v) THEN
			KvDb.ToStr(v, str); Console.WriteStr("banana: "); Console.WriteStr(str); Console.WriteLn
		END;
		c := tx.Seek(KvDb.Str("fruit/b")^);
		WHILE c.Valid() & (c.Key()[0] = ORD("f")) DO Show(c); c.Next END;
		tx.Abort;
		db.Close; KvLin.Delete(path)
	END Do;

END ObxKv0.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Kv0.cp`; subsystem `Kv` is built
for Linux). In the BlackBox window a commander executes the command:

```
   ObxKv0.Do
```

In the console:

```sh
echo 'ObxKv0.Do' | tools/run64.sh
```

The log shows:

```
banana: yellow
fruit/banana = yellow
fruit/cherry = dark red
```

## What happened

`KvLin.Open` opens the file and gives a `KvStore.Store`; `KvDb.Open` sees an empty file and creates
an empty database in it: two metas, an empty free list and an empty leaf. `KvLin.Delete` at the start
removes the file of a previous run, so the output is always the same.

`db.Begin(TRUE)` opens a writing transaction. Four `Put` calls change a copy of the root leaf in
memory; nothing is written to disk. `Commit` writes the leaf to a free page, waits for `Sync`, writes
the meta and waits for `Sync` again. After that the changes survive a power failure.

The reading transaction finds `fruit/banana` with `Get`. `Seek` puts the cursor on the first key not
less than `fruit/b`: there is no key `fruit/b`, and the cursor stands on `fruit/banana`. The loop goes
forward while the key starts with the letter `f`; `veg/carrot` is greater than all `fruit/` keys and
stops the loop. So a key prefix replaces a table. `fruit/apple` is less than `fruit/b` and is not
walked.

`Abort` closes the reading transaction: it has nothing to commit. `db.Close` requires that no
transactions remain open; otherwise trap 22.

In this example we have seen how to open a database, write pairs in one transaction and walk a range of keys with a cursor.
