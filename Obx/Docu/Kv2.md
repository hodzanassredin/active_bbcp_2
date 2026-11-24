# Overview by Example: ObxKv2

"Compare and swap" in one writing transaction: a transfer between accounts and a refusal with `Abort`.

A money transfer reads two accounts, checks the balance and writes both. If another transfer wedges
in between the read and the write, the sum of money changes. The example shows that a writing `Kv`
transaction makes such steps indivisible, and `Abort` cancels them without a trace. The example belongs
to the guide `Kv/Docu/Kv.md`, sections 3 and 5.

```
MODULE ObxKv2;
	IMPORT Console, Strings, KvStore, KvLin, KvDb;

	CONST path = "/tmp/obxkv2.db";

	VAR db: KvDb.Db;

	PROCEDURE Amount (tx: KvDb.Tx; IN acc: ARRAY OF CHAR): INTEGER;
		VAR v: KvDb.Bytes; s: ARRAY 16 OF CHAR; n, res: INTEGER;
	BEGIN
		n := 0;
		IF tx.Get(KvDb.Str(acc)^, v) THEN KvDb.ToStr(v, s); Strings.StringToInt(s, n, res) END;
		RETURN n
	END Amount;

	PROCEDURE SetAmount (tx: KvDb.Tx; IN acc: ARRAY OF CHAR; n: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(n, s); tx.Put(KvDb.Str(acc)^, KvDb.Str(s)^)
	END SetAmount;

	PROCEDURE Transfer (IN from, to: ARRAY OF CHAR; sum: INTEGER);
		(* прочитать, проверить и записать — в одной пишущей транзакции *)
		VAR tx: KvDb.Tx; a, res: INTEGER;
	BEGIN
		tx := db.Begin(TRUE);
		a := Amount(tx, from);
		IF a >= sum THEN
			SetAmount(tx, from, a - sum); SetAmount(tx, to, Amount(tx, to) + sum);
			tx.Commit(res); Console.WriteStr("transfer done")
		ELSE
			tx.Abort; Console.WriteStr("refused: not enough money")
		END;
		Console.WriteLn
	END Transfer;

	PROCEDURE Show;
		VAR tx: KvDb.Tx; c: KvDb.Cursor; k, v: ARRAY 16 OF CHAR; total, n, res: INTEGER;
	BEGIN
		tx := db.Begin(FALSE); c := tx.First(); total := 0;
		WHILE c.Valid() DO
			KvDb.ToStr(c.Key(), k); KvDb.ToStr(c.Val(), v); Strings.StringToInt(v, n, res);
			Console.WriteStr(k); Console.WriteStr(" "); Console.WriteStr(v); Console.WriteStr("; ");
			INC(total, n); c.Next
		END;
		tx.Abort;
		Strings.IntToString(total, v); Console.WriteStr("total "); Console.WriteStr(v); Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR s: KvStore.Store; tx: KvDb.Tx; res: INTEGER;
	BEGIN
		KvLin.Delete(path); KvLin.Open(path, s, res); KvDb.Open(s, db, res);
		tx := db.Begin(TRUE);
		SetAmount(tx, "alice", 100); SetAmount(tx, "bob", 50); SetAmount(tx, "carol", 0);
		tx.Commit(res);
		Show;
		Transfer("alice", "carol", 70); Show;
		Transfer("bob", "carol", 80); Show;
		Transfer("carol", "bob", 30); Show;
		db.Close; KvLin.Delete(path)
	END Do;

END ObxKv2.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Kv2.cp`; subsystem `Kv` is built
for Linux). In the BlackBox window a commander executes the command:

```
   ObxKv2.Do
```

In the console:

```sh
echo 'ObxKv2.Do' | tools/run64.sh
```

The log shows:

```
alice 100; bob 50; carol 0; total 150
transfer done
alice 30; bob 50; carol 70; total 150
refused: not enough money
alice 30; bob 50; carol 70; total 150
transfer done
alice 30; bob 80; carol 40; total 150
```

## What happened

`Transfer` opens a writing transaction, reads the balance of account `from` and only then decides.
There is one writer, so nobody changes the balance between `Amount` and `SetAmount`; a second
`Begin(TRUE)` before this transaction ends traps 21.

The first transfer is possible: both accounts change, `Commit` commits them together. The second
transfer asks 80 from `bob`, who has 50; `Transfer` calls `Abort`, and the database stays as it was —
`Show` prints the same numbers. The third transfer passes again. The total is `total 150` after every
step: no transfer is executed by half.

If the power failed in the middle of `Commit` of the first transfer, the database would open with
`alice 100` and `carol 0` or with `alice 30` and `carol 70`, but not with a sum of 80 or 220: the meta
points either to the old tree or to the new one.

In this example we have seen how to read, check and change data in one transaction, and how `Abort` cancels the decision.
