# Обзор на примерах: ObxKv0

Открыть базу, записать пары, прочитать одну и обойти диапазон ключей.

Пример открывает файл базы `/tmp/obxkv0.db`, в одной пишущей транзакции записывает четыре пары, затем в
читающей транзакции ищет один ключ и обходит ключи от `fruit/b`. Он показывает весь путь работы с `Kv`:
хранилище, база, транзакция, курсор. Пример относится к руководству `Kv/Docu/ru/Kv.md`.

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

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Kv0.cp`; подсистема `Kv` собирается для
Linux). В окне BlackBox команду исполняет коммандер:

```
   ObxKv0.Do
```

В консоли:

```sh
echo 'ObxKv0.Do' | tools/run64.sh
```

В журнале появится:

```
banana: yellow
fruit/banana = yellow
fruit/cherry = dark red
```

## Что произошло

`KvLin.Open` открывает файл и даёт хранилище `KvStore.Store`; `KvDb.Open` видит пустой файл и
создаёт в нём пустую базу: две меты, пустой список свободных и пустой лист. `KvLin.Delete` в начале
убирает файл прошлого запуска, поэтому вывод всегда один и тот же.

`db.Begin(TRUE)` открывает пишущую транзакцию. Четыре `Put` меняют копию корневого листа в памяти;
на диск ничего не пишется. `Commit` пишет лист на свободную страницу, ждёт `Sync`, пишет мету и снова
ждёт `Sync`. После этого изменения переживут выключение питания.

Читающая транзакция находит `fruit/banana` вызовом `Get`. `Seek` ставит курсор на первый ключ, не
меньший `fruit/b`: ключа `fruit/b` нет, и курсор встаёт на `fruit/banana`. Цикл идёт вперёд, пока ключ
начинается с буквы `f`; `veg/carrot` больше всех ключей `fruit/` и цикл останавливает. Так префикс ключа
заменяет таблицу. `fruit/apple` меньше `fruit/b` и в обход не попадает.

Читающую транзакцию закрывает `Abort`: у неё нечего фиксировать. `db.Close` требует, чтобы открытых
транзакций не осталось; иначе трап 22.

В этом примере мы увидели, как открыть базу, записать пары одной транзакцией и обойти диапазон ключей курсором.
