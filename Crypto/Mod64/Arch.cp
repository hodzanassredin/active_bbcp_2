MODULE CryptoArch;
(** PURPOSE "Машинная часть Crypto для amd64: широкое умножение"; *)

	(* Пара — Crypto/Mod/Arch.cp (i386). Здесь умножение LONGINT аппаратное,
	   переполнение заворачивается по модулю 2^64 — это и нужно. Ядро
	   Монтгомери держит промежуточные суммы в LONGINT и не зовёт MulAdd
	   на каждом разряде: вызов стоил бы больше самой арифметики. *)

	IMPORT SYSTEM;

	TYPE
		(* Младшая половина — индекс 0 (x86, little-endian). *)
		Words = ARRAY 2 OF INTEGER;

	(** lo, hi := a * b + c + d; все четыре — беззнаковые 32-битные. *)
	PROCEDURE MulAdd* (a, b, c, d: INTEGER; OUT lo, hi: INTEGER);
		VAR w: Words; acc, x: LONGINT;
	BEGIN
		w[0] := a; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
		w[0] := b; w[1] := 0; acc := acc * SYSTEM.VAL(LONGINT, w);
		w[0] := c; w[1] := 0; x := SYSTEM.VAL(LONGINT, w); acc := acc + x;
		w[0] := d; w[1] := 0; x := SYSTEM.VAL(LONGINT, w); acc := acc + x;
		w := SYSTEM.VAL(Words, acc);
		lo := w[0]; hi := w[1]
	END MulAdd;

	(** Младшие 64 бита произведения (по модулю 2^64, знак не важен). *)
	PROCEDURE Mul64* (x, y: LONGINT): LONGINT;
	BEGIN
		RETURN x * y
	END Mul64;

	(** Ядро умножения Монтгомери для разрядов по 32 бита (беззнаковые):
		t[0..2n] := a*b + u*p, где u подобраны так, что t[0..n-1] = 0;
		результат a*b/R — в t[n..2n] (t[2n] — 0 или 1). n0 = -p^(-1) mod 2^32. *)
	PROCEDURE MontCore* (n, n0: INTEGER; IN a, b, p: ARRAY OF INTEGER; VAR t: ARRAY OF INTEGER);
		CONST MaxLimbs = 16;	(* P-384 — 12 разрядов *)
		VAR
			tt : ARRAY 2 * MaxLimbs + 1 OF LONGINT;
			az, bz, pz : ARRAY MaxLimbs OF LONGINT;
			w : Words;
			i, j, k, lo, hi, u : INTEGER;
			acc, carry, uz : LONGINT;
	BEGIN
		ASSERT(n <= MaxLimbs, 20);
		FOR i := 0 TO 2 * n DO tt[i] := 0 END;
		(* Разряды — беззнаковые 32-битные в 64-битных словах. *)
		FOR i := 0 TO n - 1 DO
			w[0] := a[i]; w[1] := 0; az[i] := SYSTEM.VAL(LONGINT, w);
			w[0] := b[i]; w[1] := 0; bz[i] := SYSTEM.VAL(LONGINT, w);
			w[0] := p[i]; w[1] := 0; pz[i] := SYSTEM.VAL(LONGINT, w)
		END;
		(* Школьное умножение a*b в tt. *)
		FOR i := 0 TO n - 1 DO
			carry := 0;
			FOR j := 0 TO n - 1 DO
				acc := az[i] * bz[j] + tt[i + j] + carry;
				w := SYSTEM.VAL(Words, acc);
				lo := w[0]; hi := w[1];
				w[0] := lo; w[1] := 0; tt[i + j] := SYSTEM.VAL(LONGINT, w);
				w[0] := hi; w[1] := 0; carry := SYSTEM.VAL(LONGINT, w)
			END;
			tt[i + n] := carry
		END;
		(* Редукция Монтгомери; tt переполняется в старшую сторону. *)
		FOR i := 0 TO n - 1 DO
			w := SYSTEM.VAL(Words, tt[i]);
			u := w[0] * n0;
			w[0] := u; w[1] := 0; uz := SYSTEM.VAL(LONGINT, w);
			carry := 0;
			FOR j := 0 TO n - 1 DO
				acc := uz * pz[j] + tt[i + j] + carry;
				w := SYSTEM.VAL(Words, acc);
				lo := w[0]; hi := w[1];
				w[0] := lo; w[1] := 0; tt[i + j] := SYSTEM.VAL(LONGINT, w);
				w[0] := hi; w[1] := 0; carry := SYSTEM.VAL(LONGINT, w)
			END;
			k := i + n;
			WHILE k <= 2 * n DO
				acc := tt[k] + carry;
				w := SYSTEM.VAL(Words, acc);
				lo := w[0]; hi := w[1];
				w[0] := lo; w[1] := 0; tt[k] := SYSTEM.VAL(LONGINT, w);
				w[0] := hi; w[1] := 0; carry := SYSTEM.VAL(LONGINT, w);
				INC(k)
			END
		END;
		FOR i := 0 TO 2 * n DO
			w := SYSTEM.VAL(Words, tt[i]); t[i] := w[0]
		END
	END MontCore;

END CryptoArch.
