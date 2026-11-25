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
