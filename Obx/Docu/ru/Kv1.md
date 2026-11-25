# Обзор на примерах: ObxKv1

Журнал событий: дозапись по ключу «имя + номер», чтение хвоста, снимок читателя во время записи.

Журнал событий — частый случай для хранилища: события только дописываются, а читают их по имени и
номеру. Пример записывает 1000 событий `cpu` десятью транзакциями и 50 событий `net`, читает последние
три события `cpu` и показывает, что читатель, начатый до новой записи, её не видит. Пример относится к
руководству `Kv/Docu/ru/Kv.md`, разделы 2 и 5.

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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Kv1.cp`; подсистема `Kv` собирается для
Linux). В окне BlackBox команду исполняет коммандер:

```
   ObxKv1.Do
```

В консоли:

```sh
echo 'ObxKv1.Do' | tools/run64.sh
```

В журнале появится:

```
cpu events: 1000
cpu #997 = 994009
cpu #998 = 996004
cpu #999 = 998001
old reader sees: 1000
new reader sees: 1500
```

## Что произошло

Ключ события — имя, байт 0 и номер в четырёх байтах старшим вперёд (`Key`). Побайтный порядок ключей
совпадает тогда с порядком номеров, а нулевой байт отделяет имя: все события `cpu` лежат подряд, перед
событиями `net`. Обход `Count` начинает с номера 0 и идёт, пока ключ принадлежит имени (`Of`).

События с возрастающими номерами всегда попадают в правый край дерева. Узел, в конец которого
дописывали, делится на почти полные куски, поэтому журнал занимает мало страниц.

Хвост читается так же: `Seek` на номер 997 и обход до конца имени. Значение события — квадрат номера
строкой.

Затем читатель `r` остаётся открытым, а `Append` фиксирует ещё 500 событий. Писатель не трогает
страницы, которые видит `r`, и `r` по-прежнему насчитывает 1000 событий: это его снимок. Новый
читатель видит 1500. Пока `r` открыт, страницы, освобождённые писателем, не используются заново;
поэтому читателей закрывают сразу, как только они не нужны.

В этом примере мы увидели, как составить ключ журнала, прочитать хвост и как читатель видит снимок, не замечая писателя.
