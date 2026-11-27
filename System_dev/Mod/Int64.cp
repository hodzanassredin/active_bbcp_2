MODULE System_devInt64;
(** Тесты Int64: заворот умножения LONGINT на обеих целях.

	Векторы посчитаны на Python (по модулю 2^64); случайные пары
	сверяются с умножением по разрядам в 16 бит. *)

	IMPORT SYSTEM, Int64, UnitAssert;

	TYPE Words = ARRAY 2 OF INTEGER;

	VAR seed: INTEGER;

	PROCEDURE Make (hi, lo: INTEGER): LONGINT;
		VAR w: Words;
	BEGIN w[0] := lo; w[1] := hi; RETURN SYSTEM.VAL(LONGINT, w)
	END Make;

	PROCEDURE Next (): INTEGER;
	BEGIN
		seed := seed * 1664525 + 1013904223;	(* INTEGER: по модулю 2^32 *)
		RETURN seed
	END Next;

	(* Эталон: младшие 64 бита по разрядам в 16 бит (суммы < 2^35). *)
	PROCEDURE Ref (x, y: LONGINT): LONGINT;
		VAR wx, wy: Words; xs, ys: ARRAY 4 OF INTEGER; r: ARRAY 4 OF LONGINT; i, j: INTEGER;
	BEGIN
		wx := SYSTEM.VAL(Words, x); wy := SYSTEM.VAL(Words, y);
		FOR i := 0 TO 1 DO
			xs[2 * i] := ORD(BITS(wx[i]) * {0..15}); xs[2 * i + 1] := SYSTEM.LSH(wx[i], -16);
			ys[2 * i] := ORD(BITS(wy[i]) * {0..15}); ys[2 * i + 1] := SYSTEM.LSH(wy[i], -16)
		END;
		FOR i := 0 TO 3 DO r[i] := 0 END;
		FOR i := 0 TO 3 DO
			FOR j := 0 TO 3 - i DO r[i + j] := r[i + j] + LONG(xs[i]) * ys[j] END
		END;
		FOR i := 0 TO 2 DO
			r[i + 1] := r[i + 1] + r[i] DIV 10000H; r[i] := r[i] MOD 10000H
		END;
		r[3] := r[3] MOD 10000H;
		RETURN Make(SHORT(r[2]) + SYSTEM.LSH(SHORT(r[3]), 16), SHORT(r[0]) + SYSTEM.LSH(SHORT(r[1]), 16))
	END Ref;

	PROCEDURE TestVectors*;
	BEGIN
		(* Шаг LCG Кнута из ActorNetMem/RaftCore: x * a + c по модулю 2^64. *)
		UnitAssert.EqualInt(Make(5851F42DH, 4C957F2DH), Int64.Mul(1, 6364136223846793005), "a*1");
		UnitAssert.EqualInt(Make(6C576FACH, 43FD007CH) - 1442695040888963407,
			Int64.Mul(-1, -6364136223846793005), "-1*-a");
		UnitAssert.EqualInt(Make(2236D88FH, 0E5618CF0H),
			Int64.Mul(Make(01234567H, 89ABCDEFH), Make(0FEDCBA98H, 76543210H)), "full");
		UnitAssert.EqualInt(1, Int64.Mul(-1, -1), "-1*-1");
		UnitAssert.EqualInt(Make(7FFFFFFFH, 0FFFFFFFDH), Int64.Mul(MAX(LONGINT), 3), "max*3");
		UnitAssert.EqualInt(0, Int64.Mul(Make(1, 0), Make(1, 0)), "2^64");
		UnitAssert.EqualInt(-6, Int64.Mul(-2, 3), "-2*3");
		UnitAssert.EqualInt(MIN(LONGINT), Int64.Mul(MIN(LONGINT), -1), "min*-1")
	END TestVectors;

	PROCEDURE TestRandom*;
		VAR i: INTEGER; x, y: LONGINT;
	BEGIN
		seed := 20261127;
		FOR i := 1 TO 5000 DO
			x := Make(Next(), Next()); y := Make(Next(), Next());
			UnitAssert.EqualInt(Ref(x, y), Int64.Mul(x, y), "случайная пара")
		END
	END TestRandom;

	(* INC и DEC LONGINT заворачиваются на обеих целях (на i386 — add/adc,
	   а не FPU, как x + y в выражении). На них стоит шаг LCG. *)
	PROCEDURE TestIncWraps*;
		VAR x: LONGINT;
	BEGIN
		x := MAX(LONGINT); INC(x);
		UnitAssert.EqualInt(MIN(LONGINT), x, "max+1");
		x := MIN(LONGINT); DEC(x);
		UnitAssert.EqualInt(MAX(LONGINT), x, "min-1");
		x := Make(7FFFFFFFH, 0); INC(x, Make(7FFFFFFFH, 1));
		UnitAssert.EqualInt(Make(0FFFFFFFEH, 1), x, "inc wide");
		x := Int64.Mul(Make(01234567H, 89ABCDEFH), 6364136223846793005); INC(x, 1442695040888963407);
		UnitAssert.EqualInt(Make(02CE32D23H, 035DF4552H), x, "lcg via INC")
	END TestIncWraps;

END System_devInt64.
