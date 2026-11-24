# Обзор на примерах: ObxCas0

Блок называется своим хешем: одинаковые данные хранятся один раз.

Пример кладёт в хранилище в памяти три слова, из которых два одинаковы, и читает блок обратно по
имени. Он показывает, что имя блока — хеш SHA-256 его байт и что повторная запись того же блока
ничего не добавляет. Пример относится к руководству `Cas/Docu/ru/Cas.md`.

```
MODULE ObxCas0;
	IMPORT Console, Strings, CasHash, CasStore;

	PROCEDURE Put (s: CasStore.Store; IN text: ARRAY OF CHAR; OUT id: CasHash.Id);
		VAR b: ARRAY 64 OF BYTE; n, res: INTEGER; hex: ARRAY 65 OF CHAR;
	BEGIN
		n := 0;
		WHILE text[n] # 0X DO b[n] := SHORT(SHORT(ORD(text[n]))); INC(n) END;
		s.Put(b, n, id, res);
		CasHash.ToHex(id, hex); hex[16] := 0X;	(* первые 16 знаков из 64 *)
		Console.WriteStr(text); Console.WriteStr(" -> "); Console.WriteStr(hex); Console.WriteLn
	END Put;

	PROCEDURE Do*;
		VAR s: CasStore.Mem; a, b, c: CasHash.Id; data: CasStore.Block; res, i: INTEGER;
			n: ARRAY 16 OF CHAR;
	BEGIN
		s := CasStore.NewMem();
		Put(s, "hello", a); Put(s, "world", b); Put(s, "hello", c);
		Strings.IntToString(s.blocks, n);
		Console.WriteStr("blocks: "); Console.WriteStr(n); Console.WriteLn;
		s.Get(c, data, res);
		Console.WriteStr("get: ");
		FOR i := 0 TO LEN(data) - 1 DO Console.WriteChar(CHR(data[i])) END;
		Console.WriteLn
	END Do;

END ObxCas0.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod/Cas0.cp`; подсистема `Cas` собирается для amd64 и i386). В окне
BlackBox команду исполняет коммандер:

```
   ObxCas0.Do
```

В консоли:

```sh
echo 'ObxCas0.Do' | tools/run64.sh
```

В журнале появится:

```
hello -> 2cf24dba5fb0a30e
world -> 486ea46224d1bb4f
hello -> 2cf24dba5fb0a30e
blocks: 2
get: hello
```

## Что произошло

`CasStore.NewMem` создаёт пустое хранилище в памяти. Процедура `Put` переписывает знаки слова в
байты и зовёт `s.Put`; хранилище считает SHA-256 этих байт, и хеш становится именем блока. Для
краткости печатаются первые 16 из 64 шестнадцатеричных знаков имени.

Слова `hello` и `world` получают разные имена. Второе `hello` получает то же имя, что и первое:
хеш зависит только от байт. Хранилище находит блок с этим именем и не записывает его второй раз,
поэтому поле `blocks` равно 2, а не 3.

`s.Get(c, data, res)` возвращает копию блока по имени `c`. Имя второго `hello` совпадает с именем
первого, поэтому возвращаются байты первой записи — `hello`.

В этом примере мы увидели, как блок получает имя из своего содержимого, почему запись идемпотентна
и как одинаковые данные хранятся один раз.
