MODULE Cas_devChunk;
(** Тесты CasChunk: точки разреза совпадают с эталоном (скользящий хеш
	Buzhash, таблица из кода), размеры кусков в пределах, вставка в
	середину меняет лишь куски вокруг места вставки. *)

	IMPORT SYSTEM, CasHash, CasChunk, UnitAssert;

	CONST
		maxCuts = 4096;

	TYPE
		Data = POINTER TO ARRAY OF BYTE;
		Ids = POINTER TO ARRAY OF CasHash.Id;

	PROCEDURE Xs (x: INTEGER): INTEGER;
	(* xorshift32 — тот же генератор, что у эталона на Python *)
	BEGIN
		x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 13)));
		x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, -17)));
		x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 5)));
		RETURN x
	END Xs;

	PROCEDURE Gen (n, seed: INTEGER): Data;
		VAR d: Data; i, x, v: INTEGER;
	BEGIN
		NEW(d, n); x := seed;
		FOR i := 0 TO n - 1 DO
			x := Xs(x); v := x MOD 256;
			IF v >= 128 THEN DEC(v, 256) END;
			d[i] := SHORT(SHORT(v))
		END;
		RETURN d
	END Gen;

	PROCEDURE Cuts (d: Data; IN p: CasChunk.Params; OUT c: ARRAY OF INTEGER; OUT n: INTEGER);
		VAR b: INTEGER;
	BEGIN
		b := 0; n := 0;
		WHILE b < LEN(d) DO b := CasChunk.Cut(d, b, LEN(d), p); c[n] := b; INC(n) END
	END Cuts;

	PROCEDURE ChunkIds (d: Data; IN p: CasChunk.Params; OUT n: INTEGER): Ids;
		VAR c: ARRAY maxCuts OF INTEGER; ids: Ids; i, b: INTEGER;
	BEGIN
		Cuts(d, p, c, n); NEW(ids, n); b := 0;
		FOR i := 0 TO n - 1 DO CasHash.Of(d, b, c[i] - b, ids[i]); b := c[i] END;
		RETURN ids
	END ChunkIds;

	PROCEDURE TestDefaults*;
		VAR p: CasChunk.Params;
	BEGIN
		CasChunk.Default(p);
		UnitAssert.EqualInt(2048, p.min, "min");
		UnitAssert.EqualInt(8191, p.mask, "mask");
		UnitAssert.EqualInt(65536, p.max, "max")
	END TestDefaults;

	PROCEDURE TestMatchesReference*;
	(* точки разреза эталона (Python) на 100000 байтах xorshift32, seed 1 *)
		VAR d: Data; p: CasChunk.Params; c: ARRAY maxCuts OF INTEGER; n: INTEGER;
	BEGIN
		d := Gen(100000, 1);
		CasChunk.Default(p);
		Cuts(d, p, c, n);
		UnitAssert.EqualInt(10, n, "кусков");
		UnitAssert.EqualInt(5393, c[0], "1"); UnitAssert.EqualInt(9387, c[1], "2");
		UnitAssert.EqualInt(24494, c[2], "3"); UnitAssert.EqualInt(34697, c[3], "4");
		UnitAssert.EqualInt(41686, c[4], "5");
		p.min := 64; p.mask := 255; p.max := 1024;
		Cuts(d, p, c, n);
		UnitAssert.EqualInt(307, n, "мелких кусков");
		UnitAssert.EqualInt(110, c[0], "м1"); UnitAssert.EqualInt(208, c[1], "м2");
		UnitAssert.EqualInt(453, c[2], "м3"); UnitAssert.EqualInt(650, c[3], "м4");
		UnitAssert.EqualInt(899, c[4], "м5")
	END TestMatchesReference;

	PROCEDURE TestSizeBounds*;
		VAR d: Data; p: CasChunk.Params; c: ARRAY maxCuts OF INTEGER; n, i, b, len: INTEGER;
			ok: BOOLEAN;
	BEGIN
		d := Gen(300000, 7);
		p.min := 256; p.mask := 1023; p.max := 2048;
		Cuts(d, p, c, n);
		UnitAssert.EqualInt(LEN(d), c[n - 1], "куски покрывают вход");
		ok := TRUE; b := 0;
		FOR i := 0 TO n - 2 DO
			len := c[i] - b; b := c[i];
			IF (len < p.min) OR (len > p.max) THEN ok := FALSE END
		END;
		UnitAssert.True(ok, "min <= длина <= max");
		UnitAssert.True(c[n - 1] - b <= p.max, "последний не длиннее max")
	END TestSizeBounds;

	PROCEDURE TestMaxOnUniform*;
	(* одинаковые байты: граница по маске не находится, куски режутся по max *)
		VAR d: Data; p: CasChunk.Params; i: INTEGER;
	BEGIN
		NEW(d, 10000);
		FOR i := 0 TO LEN(d) - 1 DO d[i] := 7 END;
		p.min := 100; p.mask := 0FFFFFH; p.max := 3000;
		UnitAssert.EqualInt(3000, CasChunk.Cut(d, 0, LEN(d), p), "первый");
		UnitAssert.EqualInt(6000, CasChunk.Cut(d, 3000, LEN(d), p), "второй");
		UnitAssert.EqualInt(10000, CasChunk.Cut(d, 9000, LEN(d), p), "хвост")
	END TestMaxOnUniform;

	PROCEDURE TestShortTail*;
		VAR d: Data; p: CasChunk.Params;
	BEGIN
		d := Gen(5000, 3);
		CasChunk.Default(p);
		UnitAssert.EqualInt(5000, CasChunk.Cut(d, 4000, 5000, p), "короче min — целиком")
	END TestShortTail;

	PROCEDURE TestInsertLocal*;
	(* вставка 100 байт в середину 512 КБ: новых кусков не больше трёх *)
		CONST size = 524288; at = 262144; ins = 100;
		VAR a, b, x: Data; p: CasChunk.Params; ia, ib: Ids; na, nb, i, j, fresh: INTEGER;
			found: BOOLEAN;
	BEGIN
		a := Gen(size, 11); x := Gen(ins, 99);
		NEW(b, size + ins);
		FOR i := 0 TO at - 1 DO b[i] := a[i] END;
		FOR i := 0 TO ins - 1 DO b[at + i] := x[i] END;
		FOR i := at TO size - 1 DO b[i + ins] := a[i] END;
		CasChunk.Default(p);
		ia := ChunkIds(a, p, na); ib := ChunkIds(b, p, nb);
		fresh := 0;
		FOR i := 0 TO nb - 1 DO
			found := FALSE; j := 0;
			WHILE ~found & (j < na) DO found := CasHash.Equal(ib[i], ia[j]); INC(j) END;
			IF ~found THEN INC(fresh) END
		END;
		UnitAssert.True(na > 20, "кусков достаточно для проверки");
		UnitAssert.True((fresh >= 1) & (fresh <= 3), "новых кусков 1..3");
		UnitAssert.True(nb - fresh >= na - 3, "остальные общие")
	END TestInsertLocal;

	PROCEDURE BadMask;
		VAR d: Data; p: CasChunk.Params;
	BEGIN
		NEW(d, 100000); p.min := 2048; p.mask := 1000; p.max := 65536;
		IF CasChunk.Cut(d, 0, LEN(d), p) > 0 THEN END
	END BadMask;

	PROCEDURE TestBadMaskTraps*;
	BEGIN
		UnitAssert.Traps(BadMask, "маска не вида 2^k - 1")
	END TestBadMaskTraps;

END Cas_devChunk.
