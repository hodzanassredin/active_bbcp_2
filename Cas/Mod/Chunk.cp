MODULE CasChunk;
(** Нарезка по содержимому: граница куска там, где скользящий хеш Buzhash
	последних window байт делится на маску.

	Хеш зависит только от последних window байт, поэтому вставка в начало
	входа сдвигает границы, но не меняет их положения относительно
	содержимого: после места вставки куски те же. Таблица из 256 слов
	строится в теле модуля генератором xorshift32 с постоянным зерном и
	одинакова на i386 и amd64. *)

	IMPORT SYSTEM;

	CONST
		window* = 48;	(** байт в окне хеша *)
		seed = 314159265;

	TYPE
		Params* = RECORD
			min*, mask*, max*: INTEGER	(** длины куска и маска границы *)
		END;

	VAR
		t, tOut: ARRAY 256 OF SET;	(* tOut[b] = t[b], повёрнутое на window *)

	PROCEDURE Default* (OUT p: Params);
	(** Пост: p.min = 2048, p.mask = 8191, p.max = 65536 — средний кусок около 10 КБ. *)
	BEGIN
		p.min := 2048; p.mask := 8191; p.max := 65536
	END Default;

	PROCEDURE Valid (IN p: Params): BOOLEAN;
	BEGIN
		RETURN (p.min >= window) & (p.max >= p.min) & (p.mask > 0)
			& (SYSTEM.VAL(SET, p.mask) * SYSTEM.VAL(SET, p.mask + 1) = {})
	END Valid;

	PROCEDURE Cut* (IN data: ARRAY OF BYTE; beg, end: INTEGER; IN p: Params): INTEGER;
	(** Конец куска, который начинается в beg.
		Пред: 0 <= beg < end <= LEN(data) — 20.
		Пред: window <= p.min <= p.max, p.mask + 1 — степень двойки — 21.
		Пост: beg < результат <= end; результат - beg <= p.max;
		результат = end или результат - beg >= p.min. *)
		VAR i, lim, b: INTEGER; h, m: SET;
	BEGIN
		ASSERT((beg >= 0) & (beg < end) & (end <= LEN(data)), 20);
		ASSERT(Valid(p), 21);
		IF end - beg <= p.min THEN RETURN end END;
		lim := beg + p.max; IF lim > end THEN lim := end END;
		m := SYSTEM.VAL(SET, p.mask);
		h := {};
		FOR i := beg + p.min - window TO beg + p.min - 1 DO
			b := data[i]; h := SYSTEM.ROT(h, 1) / t[b MOD 256]
		END;
		i := beg + p.min;
		WHILE (h * m # {}) & (i < lim) DO
			b := data[i]; h := SYSTEM.ROT(h, 1) / t[b MOD 256];
			b := data[i - window]; h := h / tOut[b MOD 256];
			INC(i)
		END;
		ASSERT((i > beg) & (i <= end) & (i - beg <= p.max), 60);
		RETURN i
	END Cut;

	PROCEDURE Init;
		VAR i, x: INTEGER;
	BEGIN
		x := seed;
		FOR i := 0 TO 255 DO
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 13)));
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, -17)));
			x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 5)));
			t[i] := SYSTEM.VAL(SET, x);
			tOut[i] := SYSTEM.ROT(t[i], window MOD 32)
		END
	END Init;

BEGIN
	Init
END CasChunk.
