# Обзор на примерах: ObxKv2

«Сравнить и заменить» в одной пишущей транзакции: перевод между счетами и отказ `Abort`.

Перевод денег читает два счёта, проверяет остаток и записывает оба. Если между чтением и записью
вклинится другой перевод, сумма денег изменится. Пример показывает, что пишущая транзакция `Kv` делает
такие шаги неделимыми, а `Abort` отменяет их без следа. Пример относится к руководству
`Kv/Docu/ru/Kv.md`, разделы 3 и 5.

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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Kv2.cp`; подсистема `Kv` собирается для
Linux). В окне BlackBox команду исполняет коммандер:

```
   ObxKv2.Do
```

В консоли:

```sh
echo 'ObxKv2.Do' | tools/run64.sh
```

В журнале появится:

```
alice 100; bob 50; carol 0; total 150
transfer done
alice 30; bob 50; carol 70; total 150
refused: not enough money
alice 30; bob 50; carol 70; total 150
transfer done
alice 30; bob 80; carol 40; total 150
```

## Что произошло

`Transfer` открывает пишущую транзакцию, читает остаток счёта `from` и только потом решает. Писатель
один, поэтому никто не изменит остаток между `Amount` и `SetAmount`; второй `Begin(TRUE)` до конца
этой транзакции — трап 21.

Первый перевод возможен: оба счёта меняются, `Commit` фиксирует их вместе. Второй перевод просит 80 у
`bob`, у которого 50; `Transfer` зовёт `Abort`, и база остаётся прежней — `Show` печатает те же
числа. Третий перевод снова проходит. Итог `total 150` после каждого шага: ни один перевод не
выполнен наполовину.

Если бы питание пропало посреди `Commit` первого перевода, база открылась бы с `alice 100` и
`carol 0` или с `alice 30` и `carol 70`, но не с суммой 80 или 220: мета указывает либо на старое
дерево, либо на новое.

В этом примере мы увидели, как прочитать, проверить и изменить данные одной транзакцией и как `Abort` отменяет решение.
