MODULE CryptoArch;
(** PURPOSE "Машинная часть Crypto для i386: широкое умножение без переполнения"; *)

	(* Пара — Crypto/Mod64/Arch.cp (amd64). На amd64 умножение LONGINT
	   аппаратное, и переполнение заворачивается по модулю 2^64. Компилятор
	   i386 умножает LONGINT на FPU: произведение вне [-2^63, 2^63) — трап
	   143 «undefined real result». Поэтому здесь умножение 32 x 32 бита
	   собирается из двух частичных произведений 32 x 16 (< 2^48) — их FPU
	   считает точно. Модули Crypto зовут только эти имена и не знают цели. *)

	IMPORT SYSTEM;

	TYPE
		(* Младшая половина — индекс 0 (x86, little-endian). *)
		Words = ARRAY 2 OF INTEGER;

	(** lo, hi := a * b + c + d; все четыре — беззнаковые 32-битные.
		Сумма не превосходит 2^64 - 1 и всегда помещается в (hi, lo). *)
	PROCEDURE MulAdd* (a, b, c, d: INTEGER; OUT lo, hi: INTEGER);
		VAR w: Words; ua, p0, p1, s: LONGINT; b0, b1, p1lo, p1hi: INTEGER;
	BEGIN
		w[0] := a; w[1] := 0; ua := SYSTEM.VAL(LONGINT, w);
		b0 := ORD(BITS(b) * {0..15}); b1 := SYSTEM.LSH(b, -16);
		p0 := ua * b0;	(* < 2^48 *)
		p1 := ua * b1;	(* < 2^48 *)
		w := SYSTEM.VAL(Words, p1);
		p1lo := ORD(BITS(w[0]) * {0..15});	(* p1 mod 2^16 *)
		p1hi := SYSTEM.LSH(w[1], 16) + SYSTEM.LSH(w[0], -16);	(* p1 div 2^16, < 2^32 *)
		s := p0 + LONG(p1lo) * 10000H;
		w[0] := c; w[1] := 0; s := s + SYSTEM.VAL(LONGINT, w);
		w[0] := d; w[1] := 0; s := s + SYSTEM.VAL(LONGINT, w);	(* < 2^50 *)
		w := SYSTEM.VAL(Words, s);
		lo := w[0];
		hi := w[1] + p1hi	(* по модулю 2^32; полное значение < 2^64 *)
	END MulAdd;

	(** Младшие 64 бита произведения (по модулю 2^64, знак не важен). *)
	PROCEDURE Mul64* (x, y: LONGINT): LONGINT;
		VAR wx, wy, r: Words;
	BEGIN
		wx := SYSTEM.VAL(Words, x); wy := SYSTEM.VAL(Words, y);
		MulAdd(wx[0], wy[0], 0, 0, r[0], r[1]);
		r[1] := r[1] + wx[0] * wy[1] + wx[1] * wy[0];
		RETURN SYSTEM.VAL(LONGINT, r)
	END Mul64;

	(** Ядро умножения Монтгомери для разрядов по 32 бита (беззнаковые):
		t[0..2n] := a*b + u*p, где u подобраны так, что t[0..n-1] = 0;
		результат a*b/R — в t[n..2n] (t[2n] — 0 или 1). n0 = -p^(-1) mod 2^32. *)
	PROCEDURE MontCore* (n, n0: INTEGER; IN a, b, p: ARRAY OF INTEGER; VAR t: ARRAY OF INTEGER);
		VAR i, j, k, u, carry, s: INTEGER;
	BEGIN
		FOR i := 0 TO 2 * n DO t[i] := 0 END;
		FOR i := 0 TO n - 1 DO
			carry := 0;
			FOR j := 0 TO n - 1 DO
				MulAdd(a[i], b[j], t[i + j], carry, t[i + j], carry)
			END;
			t[i + n] := carry
		END;
		FOR i := 0 TO n - 1 DO
			u := t[i] * n0;
			carry := 0;
			FOR j := 0 TO n - 1 DO
				MulAdd(u, p[j], t[i + j], carry, t[i + j], carry)
			END;
			k := i + n;
			WHILE k <= 2 * n DO
				s := t[k] + carry;
				(* перенос без ветвления: старший бит (x & c) | ((x | c) & ~s) *)
				carry := SYSTEM.LSH(ORD(BITS(t[k]) * BITS(carry) + (BITS(t[k]) + BITS(carry) - BITS(s))), -31);
				t[k] := s;
				INC(k)
			END
		END
	END MontCore;

END CryptoArch.
