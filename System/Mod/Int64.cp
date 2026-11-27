MODULE Int64;
(** PURPOSE "Умножение LONGINT по модулю 2^64 на i386"; *)

	(* Пара — System/Mod64/Int64.cp (amd64), там Mul — просто x * y.
	   Компилятор i386 умножает LONGINT на FPU: произведение вне
	   [-2^63, 2^63) — трап 143 «undefined real result», а не заворот.
	   Генераторы псевдослучайных чисел (LCG Кнута, перемешивание зерна)
	   и хеши рассчитывают на заворот, поэтому зовут Mul. Сложение и
	   вычитание LONGINT в выражении на i386 тоже идут через FPU и при
	   переполнении дают тот же трап; заворачиваются только INC и DEC
	   (целочисленные add/adc, DevCPC486.LargeInc). Шаг LCG поэтому
	   пишется x := Int64.Mul(x, a); INC(x, c) (System_devInt64.TestIncWraps). *)

	IMPORT SYSTEM;

	TYPE
		Words = ARRAY 2 OF INTEGER;	(* младшая половина — индекс 0 *)

	(* lo, hi := a * b, a и b — беззнаковые 32-битные. Произведение
	   собирается из двух частичных 32 x 16 (< 2^48), которые FPU считает
	   точно. *)
	PROCEDURE Mul32 (a, b: INTEGER; OUT lo, hi: INTEGER);
		VAR w: Words; ua, p0, p1, s: LONGINT; p1lo, p1hi: INTEGER;
	BEGIN
		w[0] := a; w[1] := 0; ua := SYSTEM.VAL(LONGINT, w);
		p0 := ua * ORD(BITS(b) * {0..15});
		p1 := ua * SYSTEM.LSH(b, -16);
		w := SYSTEM.VAL(Words, p1);
		p1lo := ORD(BITS(w[0]) * {0..15});
		p1hi := SYSTEM.LSH(w[1], 16) + SYSTEM.LSH(w[0], -16);
		s := p0 + LONG(p1lo) * 10000H;	(* < 2^49 *)
		w := SYSTEM.VAL(Words, s);
		lo := w[0]; hi := w[1] + p1hi
	END Mul32;

	(** Младшие 64 бита произведения x * y (заворот, как на amd64). *)
	PROCEDURE Mul* (x, y: LONGINT): LONGINT;
		VAR wx, wy, r: Words;
	BEGIN
		wx := SYSTEM.VAL(Words, x); wy := SYSTEM.VAL(Words, y);
		Mul32(wx[0], wy[0], r[0], r[1]);
		r[1] := r[1] + wx[0] * wy[1] + wx[1] * wy[0];	(* INTEGER: по модулю 2^32 *)
		RETURN SYSTEM.VAL(LONGINT, r)
	END Mul;

END Int64.
