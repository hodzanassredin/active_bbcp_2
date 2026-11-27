MODULE Crypto_devArch;
(** Тесты машинной части CryptoArch: широкое умножение и ядро Монтгомери.

	Одни и те же векторы проходят на amd64 (Crypto/Mod64/Arch.cp) и на
	i386 (Crypto/Mod/Arch.cp). Эталон для случайных пар — умножение
	по разрядам в 16 бит, без LONGINT-умножения с переполнением. *)

	IMPORT SYSTEM, CryptoArch, UnitAssert;

	TYPE Words = ARRAY 2 OF INTEGER;

	VAR seed: INTEGER;

	PROCEDURE Next (): INTEGER;
	BEGIN	(* LCG по модулю 2^32, заворот INTEGER *)
		seed := seed * 1664525 + 1013904223;
		RETURN seed
	END Next;

	PROCEDURE Make (hi, lo: INTEGER): LONGINT;
		VAR w: Words;
	BEGIN w[0] := lo; w[1] := hi; RETURN SYSTEM.VAL(LONGINT, w)
	END Make;

	(* Эталон: младшие 64 бита x*y по разрядам в 16 бит. Произведения
	   разрядов < 2^32 и их суммы < 2^35 — LONGINT без переполнения. *)
	PROCEDURE RefMul64 (x, y: LONGINT): LONGINT;
		VAR wx, wy: Words; xs, ys: ARRAY 4 OF INTEGER; r: ARRAY 4 OF LONGINT; i, j: INTEGER;
	BEGIN
		wx := SYSTEM.VAL(Words, x); wy := SYSTEM.VAL(Words, y);
		FOR i := 0 TO 1 DO
			xs[2 * i] := ORD(BITS(wx[i]) * {0..15}); xs[2 * i + 1] := SYSTEM.LSH(wx[i], -16);
			ys[2 * i] := ORD(BITS(wy[i]) * {0..15}); ys[2 * i + 1] := SYSTEM.LSH(wy[i], -16)
		END;
		FOR i := 0 TO 3 DO r[i] := 0 END;
		FOR i := 0 TO 3 DO
			FOR j := 0 TO 3 - i DO
				r[i + j] := r[i + j] + LONG(xs[i]) * ys[j]
			END
		END;
		FOR i := 0 TO 2 DO
			r[i + 1] := r[i + 1] + r[i] DIV 10000H;
			r[i] := r[i] MOD 10000H
		END;
		r[3] := r[3] MOD 10000H;
		RETURN Make(SHORT(r[2]) + SYSTEM.LSH(SHORT(r[3]), 16), SHORT(r[0]) + SYSTEM.LSH(SHORT(r[1]), 16))
	END RefMul64;

	PROCEDURE TestMulAddVectors*;
		VAR lo, hi: INTEGER;
	BEGIN
		CryptoArch.MulAdd(-1, -1, -1, -1, lo, hi);	(* (2^32-1)^2 + 2(2^32-1) = 2^64-1 *)
		UnitAssert.EqualInt(-1, lo, "max lo"); UnitAssert.EqualInt(-1, hi, "max hi");
		CryptoArch.MulAdd(-1, 2, 0, 0, lo, hi);
		UnitAssert.EqualInt(-2, lo, "x2 lo"); UnitAssert.EqualInt(1, hi, "x2 hi");
		CryptoArch.MulAdd(12345678H, 9ABCDEF0H, 0DEADBEEFH, 0CAFEBABEH, lo, hi);
		UnitAssert.EqualInt(0CDD99A2DH, lo, "mix lo"); UnitAssert.EqualInt(0B00EA4FH, hi, "mix hi");
		CryptoArch.MulAdd(0, 0, -1, 1, lo, hi);
		UnitAssert.EqualInt(0, lo, "carry lo"); UnitAssert.EqualInt(1, hi, "carry hi")
	END TestMulAddVectors;

	PROCEDURE TestMul64Vectors*;
	BEGIN
		UnitAssert.EqualInt(Make(2236D88FH, 0E5618CF0H),
			CryptoArch.Mul64(Make(01234567H, 89ABCDEFH), Make(0FEDCBA98H, 76543210H)), "full");
		UnitAssert.EqualInt(1, CryptoArch.Mul64(-1, -1), "-1*-1");
		UnitAssert.EqualInt(Make(7FFFFFFFH, 0FFFFFFFDH), CryptoArch.Mul64(MAX(LONGINT), 3), "max*3");
		UnitAssert.EqualInt(0, CryptoArch.Mul64(Make(1, 0), Make(1, 0)), "2^64")
	END TestMul64Vectors;

	PROCEDURE TestRandom*;
		VAR i, lo, hi, a, b: INTEGER; x, y, ref: LONGINT; w: Words;
	BEGIN
		seed := 20261127;
		FOR i := 1 TO 2000 DO
			x := Make(Next(), Next()); y := Make(Next(), Next());
			ref := RefMul64(x, y);
			UnitAssert.EqualInt(ref, CryptoArch.Mul64(x, y), "Mul64 случайная пара");
			a := Next(); b := Next();
			CryptoArch.MulAdd(a, b, 0, 0, lo, hi);
			w[0] := a; w[1] := 0; x := SYSTEM.VAL(LONGINT, w);
			w[0] := b; w[1] := 0; y := SYSTEM.VAL(LONGINT, w);
			ref := RefMul64(x, y);	(* < 2^64: младшие 64 бита — всё произведение *)
			UnitAssert.EqualInt(ref, Make(hi, lo), "MulAdd случайная пара")
		END
	END TestRandom;

END Crypto_devArch.
