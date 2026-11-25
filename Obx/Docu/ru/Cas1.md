# Обзор на примерах: ObxCas1

Нарезка по содержимому сохраняет общие куски двух почти одинаковых входов.

Пример режет два входа по 300 000 байт, второй из которых отличается вставкой десяти байт в
середину, двумя способами: ровно по 8192 байта и по содержимому (`CasChunk.Cut`). Он считает, сколько
кусков второго входа совпадает с кусками первого. Пример показывает, зачем нужна нарезка по
содержимому; он относится к руководству `Cas/Docu/ru/Cas.md`.

```
MODULE ObxCas1;
	IMPORT SYSTEM, Console, Strings, CasHash, CasChunk;

	CONST size = 300000; at = 150000; ins = 10;

	TYPE
		Data = POINTER TO ARRAY OF BYTE;
		Ids = POINTER TO ARRAY OF CasHash.Id;

	PROCEDURE Gen (n: INTEGER): Data;	(* псевдослучайные байты, xorshift32 *)
		VAR d: Data; i, x: INTEGER;
	BEGIN
		NEW(d, n); x := 1;
		FOR i := 0 TO n - 1 DO
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 13)));
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, -17)));
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 5)));
			d[i] := SHORT(SHORT(x MOD 128))
		END;
		RETURN d
	END Gen;

	PROCEDURE Chunks (d: Data; fixed: BOOLEAN; OUT n: INTEGER): Ids;
	(* Id кусков: по содержимому (CasChunk) или ровно по 8192 байта *)
		VAR ids: Ids; b, e: INTEGER; p: CasChunk.Params;
	BEGIN
		CasChunk.Default(p); NEW(ids, LEN(d) DIV p.min + 1); b := 0; n := 0;
		WHILE b < LEN(d) DO
			IF ~fixed THEN e := CasChunk.Cut(d, b, LEN(d), p)
			ELSIF LEN(d) - b > 8192 THEN e := b + 8192
			ELSE e := LEN(d)
			END;
			CasHash.Of(d, b, e - b, ids[n]); INC(n); b := e
		END;
		RETURN ids
	END Chunks;

	PROCEDURE Compare (a, b: Data; fixed: BOOLEAN; IN title: ARRAY OF CHAR);
		VAR ia, ib: Ids; na, nb, i, j, common: INTEGER; s: ARRAY 16 OF CHAR;
	BEGIN
		ia := Chunks(a, fixed, na); ib := Chunks(b, fixed, nb); common := 0;
		FOR i := 0 TO nb - 1 DO
			j := 0; WHILE (j < na) & ~CasHash.Equal(ib[i], ia[j]) DO INC(j) END;
			IF j < na THEN INC(common) END
		END;
		Console.WriteStr(title);
		Strings.IntToString(na, s); Console.WriteStr(": A "); Console.WriteStr(s);
		Strings.IntToString(nb, s); Console.WriteStr(", B "); Console.WriteStr(s);
		Strings.IntToString(common, s); Console.WriteStr(", common "); Console.WriteStr(s);
		Console.WriteLn
	END Compare;

	PROCEDURE Do*;
		VAR a, b: Data; i: INTEGER;
	BEGIN
		a := Gen(size); NEW(b, size + ins);
		FOR i := 0 TO at - 1 DO b[i] := a[i] END;
		FOR i := 0 TO ins - 1 DO b[at + i] := ORD("!") END;	(* вставка десяти байт *)
		FOR i := at TO size - 1 DO b[i + ins] := a[i] END;
		Compare(a, b, TRUE, "fixed 8K");
		Compare(a, b, FALSE, "by content")
	END Do;

END ObxCas1.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod/Cas1.cp`; подсистема `Cas` собирается для amd64 и i386). В окне
BlackBox команду исполняет коммандер:

```
   ObxCas1.Do
```

В консоли:

```sh
echo 'ObxCas1.Do' | tools/run64.sh
```

В журнале появится:

```
fixed 8K: A 37, B 37, common 18
by content: A 31, B 31, common 30
```

## Что произошло

`Gen` строит 300 000 псевдослучайных байт генератором xorshift32. Вход `b` — тот же вход `a`, в
середину которого, в позицию 150 000, вставлены десять знаков `!`. `Chunks` режет вход и считает
`Id` каждого куска; `Compare` ищет каждый кусок `b` среди кусков `a`.

При нарезке ровно по 8192 байта куски до места вставки совпадают — их 18. Вставка сдвигает все
следующие границы на десять байт, и ни один кусок после неё не совпадает со старым, хотя байты те
же.

`CasChunk.Cut` ставит границу там, где скользящий хеш последних 48 байт делится на маску. Граница
зависит от байт под окном, а не от позиции во входе. После вставки окно видит те же байты и
находит те же границы, поэтому новым оказывается один кусок — тот, что содержит вставку. Общих
кусков 30 из 31.

В этом примере мы увидели, почему нарезка по позиции теряет общие куски после вставки, а нарезка
по содержимому сохраняет их.
