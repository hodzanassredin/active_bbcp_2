MODULE CasHash;
(** SHA-256 (FIPS 180-4) и имя блока хранилища Cas.

	Id — 32 байта хеша. Слова — INTEGER (32 бита на i386 и amd64);
	сложение по модулю 2^32 идёт в LONGINT, сдвиги — через LONGINT,
	поэтому результат не зависит от разрядности регистров. *)

	IMPORT SYSTEM;

	CONST
		size* = 32;	(* байт в Id *)
		mod32 = 100000000L;	(* 2^32 *)

	TYPE
		Id* = ARRAY size OF BYTE;

	VAR
		k: ARRAY 64 OF INTEGER;
		hexDigit: ARRAY 17 OF CHAR;

	PROCEDURE Fold (x: LONGINT): INTEGER;
	(* x по модулю 2^32 как INTEGER со знаком *)
	BEGIN
		x := x MOD mod32;
		IF x >= 80000000L THEN DEC(x, mod32) END;
		RETURN SHORT(x)
	END Fold;

	PROCEDURE U (x: INTEGER): LONGINT;
	(* слово без знака *)
	BEGIN
		RETURN LONG(x) MOD mod32
	END U;

	PROCEDURE Rotr (x: INTEGER; n: INTEGER): INTEGER;
		VAR u, p: LONGINT;
	BEGIN
		u := U(x); p := ASH(1L, n);
		RETURN Fold(u DIV p + (u MOD p) * ASH(1L, 32 - n))
	END Rotr;

	PROCEDURE Shr (x: INTEGER; n: INTEGER): INTEGER;
	BEGIN
		RETURN Fold(U(x) DIV ASH(1L, n))
	END Shr;

	PROCEDURE Xor3 (a, b, c: INTEGER): INTEGER;
	BEGIN
		RETURN SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, a) / SYSTEM.VAL(SET, b) / SYSTEM.VAL(SET, c))
	END Xor3;

	PROCEDURE Compress (VAR h: ARRAY OF INTEGER; IN m: ARRAY OF BYTE; beg: INTEGER);
		VAR w: ARRAY 64 OF INTEGER; i: INTEGER; a, b, c, d, e, f, g, hh, t1, t2: INTEGER;
			se, sa: SET;
	BEGIN
		FOR i := 0 TO 15 DO
			w[i] := Fold(((LONG(m[beg] MOD 256) * 256 + m[beg + 1] MOD 256) * 256
				+ m[beg + 2] MOD 256) * 256 + m[beg + 3] MOD 256);
			INC(beg, 4)
		END;
		FOR i := 16 TO 63 DO
			w[i] := Fold(LONG(Xor3(Rotr(w[i - 2], 17), Rotr(w[i - 2], 19), Shr(w[i - 2], 10)))
				+ w[i - 7] + Xor3(Rotr(w[i - 15], 7), Rotr(w[i - 15], 18), Shr(w[i - 15], 3))
				+ w[i - 16])
		END;
		a := h[0]; b := h[1]; c := h[2]; d := h[3]; e := h[4]; f := h[5]; g := h[6]; hh := h[7];
		FOR i := 0 TO 63 DO
			se := SYSTEM.VAL(SET, e);
			t1 := Fold(LONG(hh) + Xor3(Rotr(e, 6), Rotr(e, 11), Rotr(e, 25))
				+ SYSTEM.VAL(INTEGER, (se * SYSTEM.VAL(SET, f)) / ((-se) * SYSTEM.VAL(SET, g)))
				+ k[i] + w[i]);
			sa := SYSTEM.VAL(SET, a);
			t2 := Fold(LONG(Xor3(Rotr(a, 2), Rotr(a, 13), Rotr(a, 22)))
				+ SYSTEM.VAL(INTEGER, (sa * SYSTEM.VAL(SET, b)) / (sa * SYSTEM.VAL(SET, c))
					/ (SYSTEM.VAL(SET, b) * SYSTEM.VAL(SET, c))));
			hh := g; g := f; f := e; e := Fold(LONG(d) + t1);
			d := c; c := b; b := a; a := Fold(LONG(t1) + t2)
		END;
		h[0] := Fold(LONG(h[0]) + a); h[1] := Fold(LONG(h[1]) + b);
		h[2] := Fold(LONG(h[2]) + c); h[3] := Fold(LONG(h[3]) + d);
		h[4] := Fold(LONG(h[4]) + e); h[5] := Fold(LONG(h[5]) + f);
		h[6] := Fold(LONG(h[6]) + g); h[7] := Fold(LONG(h[7]) + hh)
	END Compress;

	PROCEDURE PutByte (VAR b: ARRAY OF BYTE; i: INTEGER; v: LONGINT);
	(* младший байт v в b[i] *)
		VAR x: INTEGER;
	BEGIN
		x := SHORT(v MOD 256);
		IF x >= 128 THEN DEC(x, 256) END;
		b[i] := SHORT(SHORT(x))
	END PutByte;

	PROCEDURE Of* (IN data: ARRAY OF BYTE; beg, len: INTEGER; OUT id: Id);
	(** Хеш SHA-256 отрезка data[beg .. beg + len - 1].
		Пред: beg >= 0, len >= 0 — 20; beg + len <= LEN(data) — 21.
		Пост: id — SHA-256 отрезка. *)
		VAR h: ARRAY 8 OF INTEGER; tail: ARRAY 128 OF BYTE; n, i: INTEGER; bits: LONGINT;
	BEGIN
		ASSERT((beg >= 0) & (len >= 0), 20); ASSERT(beg + len <= LEN(data), 21);
		h[0] := 6A09E667H; h[1] := Fold(0BB67AE85L); h[2] := 3C6EF372H; h[3] := Fold(0A54FF53AL);
		h[4] := 510E527FH; h[5] := Fold(09B05688CL); h[6] := 1F83D9ABH; h[7] := 5BE0CD19H;
		bits := LONG(len) * 8;
		WHILE len >= 64 DO Compress(h, data, beg); INC(beg, 64); DEC(len, 64) END;
		FOR i := 0 TO len - 1 DO tail[i] := data[beg + i] END;
		tail[len] := -128; n := len + 1;	(* бит 1, затем нули *)
		IF n > 56 THEN
			WHILE n < 128 DO tail[n] := 0; INC(n) END;
			n := 128
		ELSE
			WHILE n < 64 DO tail[n] := 0; INC(n) END;
			n := 64
		END;
		FOR i := 1 TO 8 DO PutByte(tail, n - i, bits); bits := bits DIV 256 END;
		Compress(h, tail, 0);
		IF n = 128 THEN Compress(h, tail, 64) END;
		FOR i := 0 TO 31 DO PutByte(id, i, U(h[i DIV 4]) DIV ASH(1L, 24 - 8 * (i MOD 4))) END
	END Of;

	PROCEDURE Equal* (IN a, b: Id): BOOLEAN;
	(** Пост: результат = a и b совпадают побайтно. *)
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < size) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = size
	END Equal;

	PROCEDURE Clear* (OUT id: Id);
	(** Пост: id — пустой Id (все байты 0); IsNil(id). *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO size - 1 DO id[i] := 0 END
	END Clear;

	PROCEDURE IsNil* (IN id: Id): BOOLEAN;
	(** Пост: результат = все байты id равны 0. Пустой Id означает «нет блока». *)
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < size) & (id[i] = 0) DO INC(i) END;
		RETURN i = size
	END IsNil;

	PROCEDURE ToHex* (IN id: Id; OUT s: ARRAY OF CHAR);
	(** Шестнадцатеричная запись строчными буквами.
		Пред: LEN(s) > 2 * size — 20.
		Пост: s — 64 знака 0-9a-f и 0X. *)
		VAR i, v: INTEGER;
	BEGIN
		ASSERT(LEN(s) > 2 * size, 20);
		FOR i := 0 TO size - 1 DO
			v := id[i] MOD 256;
			s[2 * i] := hexDigit[v DIV 16]; s[2 * i + 1] := hexDigit[v MOD 16]
		END;
		s[2 * size] := 0X
	END ToHex;

	PROCEDURE Digit (c: CHAR): INTEGER;
	BEGIN
		IF (c >= "0") & (c <= "9") THEN RETURN ORD(c) - ORD("0")
		ELSIF (c >= "a") & (c <= "f") THEN RETURN ORD(c) - ORD("a") + 10
		ELSIF (c >= "A") & (c <= "F") THEN RETURN ORD(c) - ORD("A") + 10
		ELSE RETURN -1
		END
	END Digit;

	PROCEDURE FromHex* (IN s: ARRAY OF CHAR; OUT id: Id; OUT ok: BOOLEAN);
	(** Разбор записи ToHex; заглавные буквы допустимы.
		Пост: ok = s — ровно 64 шестнадцатеричных знака; тогда ToHex(id) совпадает с s
		без учёта регистра. ~ok — id пуст. *)
		VAR i, hi, lo: INTEGER;
	BEGIN
		ok := TRUE; i := 0;
		WHILE ok & (i < size) DO
			IF 2 * i + 1 < LEN(s) THEN hi := Digit(s[2 * i]); lo := Digit(s[2 * i + 1])
			ELSE hi := -1; lo := -1
			END;
			IF (hi < 0) OR (lo < 0) THEN ok := FALSE
			ELSE PutByte(id, i, hi * 16 + lo); INC(i)
			END
		END;
		IF ok & (2 * size < LEN(s)) & (s[2 * size] # 0X) THEN ok := FALSE END;
		IF ~ok THEN Clear(id) END
	END FromHex;

	PROCEDURE Init;
		VAR i: INTEGER; t: ARRAY 64 OF LONGINT;
	BEGIN
		hexDigit := "0123456789abcdef";
		t[0] := 428A2F98H; t[1] := 71374491H; t[2] := 0B5C0FBCFL; t[3] := 0E9B5DBA5L;
		t[4] := 3956C25BH; t[5] := 59F111F1H; t[6] := 923F82A4L; t[7] := 0AB1C5ED5L;
		t[8] := 0D807AA98L; t[9] := 12835B01H; t[10] := 243185BEH; t[11] := 550C7DC3H;
		t[12] := 72BE5D74H; t[13] := 80DEB1FEL; t[14] := 9BDC06A7L; t[15] := 0C19BF174L;
		t[16] := 0E49B69C1L; t[17] := 0EFBE4786L; t[18] := 0FC19DC6H; t[19] := 240CA1CCH;
		t[20] := 2DE92C6FH; t[21] := 4A7484AAH; t[22] := 5CB0A9DCH; t[23] := 76F988DAH;
		t[24] := 983E5152L; t[25] := 0A831C66DL; t[26] := 0B00327C8L; t[27] := 0BF597FC7L;
		t[28] := 0C6E00BF3L; t[29] := 0D5A79147L; t[30] := 06CA6351H; t[31] := 14292967H;
		t[32] := 27B70A85H; t[33] := 2E1B2138H; t[34] := 4D2C6DFCH; t[35] := 53380D13H;
		t[36] := 650A7354H; t[37] := 766A0ABBH; t[38] := 81C2C92EL; t[39] := 92722C85L;
		t[40] := 0A2BFE8A1L; t[41] := 0A81A664BL; t[42] := 0C24B8B70L; t[43] := 0C76C51A3L;
		t[44] := 0D192E819L; t[45] := 0D6990624L; t[46] := 0F40E3585L; t[47] := 106AA070H;
		t[48] := 19A4C116H; t[49] := 1E376C08H; t[50] := 2748774CH; t[51] := 34B0BCB5H;
		t[52] := 391C0CB3H; t[53] := 4ED8AA4AH; t[54] := 5B9CCA4FH; t[55] := 682E6FF3H;
		t[56] := 748F82EEH; t[57] := 78A5636FH; t[58] := 84C87814L; t[59] := 8CC70208L;
		t[60] := 90BEFFFAL; t[61] := 0A4506CEBL; t[62] := 0BEF9A3F7L; t[63] := 0C67178F2L;
		FOR i := 0 TO 63 DO k[i] := Fold(t[i]) END
	END Init;

BEGIN
	Init
END CasHash.
