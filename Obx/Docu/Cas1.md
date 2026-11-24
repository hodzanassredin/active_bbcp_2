# Overview by Example: ObxCas1

Cutting by content keeps the common chunks of two nearly equal inputs.

The example cuts two inputs of 300 000 bytes, the second of which differs by ten bytes inserted in
the middle, in two ways: exactly every 8192 bytes and by content (`CasChunk.Cut`). It counts how
many chunks of the second input equal chunks of the first. The example shows why cutting by content
is needed; it belongs to the guide `Cas/Docu/Cas.md`.

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

The module is built by `tools/build64.sh` (source `Obx/Mod/Cas1.cp`; the subsystem `Cas` is built for amd64 and i386). In a
BlackBox window the commander executes the command:

```
   ObxCas1.Do
```

In the console:

```sh
echo 'ObxCas1.Do' | tools/run64.sh
```

The log shows:

```
fixed 8K: A 37, B 37, common 18
by content: A 31, B 31, common 30
```

## What happened

`Gen` builds 300 000 pseudo-random bytes with the xorshift32 generator. The input `b` is the input
`a` with ten characters `!` inserted in the middle, at position 150 000. `Chunks` cuts an input and
computes the `Id` of each chunk; `Compare` looks for each chunk of `b` among the chunks of `a`.

With cuts exactly every 8192 bytes the chunks before the insertion are equal — there are 18 of them.
The insertion shifts all later boundaries by ten bytes, and no chunk after it equals an old one,
although the bytes are the same.

`CasChunk.Cut` places a boundary where the rolling hash of the last 48 bytes is divisible by the
mask. The boundary depends on the bytes under the window, not on the position in the input. After
the insertion the window sees the same bytes and finds the same boundaries, so only one chunk is
new — the one that contains the insertion. 30 of 31 chunks are common.

In this example we saw why cutting by position loses common chunks after an insertion, while
cutting by content keeps them.
