MODULE CryptoEC;
(** PURPOSE "Арифметика NIST P-256 и P-384, ECDH"; *)

	(* Поле и группа точек на короткой кривой Вейерштрасса.
	   Координаты хранятся в проективных координатах (X : Y : Z), умножение
	   в поле — Монтгомери. Сложение точек — полные формулы Ренеса,
	   Костелло и Батины (2015): алгоритм 7 для a = 0 (P-256) и алгоритм 4
	   для a = -3 (P-384). Полные формулы верны и для совпадающих, и для
	   противоположных точек, поэтому в умножении на скаляр нет особых
	   случаев и ветвлений по секрету. *)

	IMPORT SYSTEM, U := CryptoUtils, AO := CryptoAosCompat, Arch := CryptoArch;

	CONST
		MaxLimbs = 12;
		Window = 4;          (* бит в окне скаляра *)
		WindowCount = 16;

	TYPE
		BigInt* = ARRAY MaxLimbs OF INTEGER;

		Modulus = RECORD
			n : INTEGER;
			p : BigInt;
			n0 : INTEGER;
			r2 : BigInt;      (* R^2 mod p *)
			one : BigInt      (* R mod p *)
		END;

		Point = RECORD
			x, y, z : BigInt
		END;

		(* Две 32-битные половины 64-битного промежуточного значения:
		   младшая — индекс 0 (x86, little-endian), как в CryptoAosCompat. *)
		Words = ARRAY 2 OF INTEGER;

		Curve* = POINTER TO RECORD
			n*, nbits* : INTEGER;
			aIsZero : BOOLEAN;
			fp, fq : Modulus;
			aM, bM, b3M : BigInt;
			gx*, gy*, order* : BigInt
		END;

	VAR
		p256, p384 : Curve;

	(* ------------------------- вспомогательные ------------------------- *)

	PROCEDURE Zero (VAR a : BigInt);
		VAR i : INTEGER;
	BEGIN
		FOR i := 0 TO MaxLimbs - 1 DO
			a[i] := 0
		END
	END Zero;

	PROCEDURE Copy (IN a : BigInt; VAR b : BigInt);
		VAR i : INTEGER;
	BEGIN
		FOR i := 0 TO MaxLimbs - 1 DO
			b[i] := a[i]
		END
	END Copy;

	PROCEDURE SetInt (VAR a : BigInt; v : INTEGER);
	BEGIN
		Zero(a);
		a[0] := v
	END SetInt;

	PROCEDURE IsZeroN (IN a : BigInt; n : INTEGER): BOOLEAN;
		VAR i, acc : INTEGER;
	BEGIN
		(* Без раннего выхода: время не зависит от того, в каком разряде
		   встретился ненулевой. *)
		acc := 0;
		FOR i := 0 TO n - 1 DO
			acc := ORD(BITS(acc) + BITS(a[i]))
		END;
		RETURN acc = 0
	END IsZeroN;

	PROCEDURE IsOne (IN a : BigInt; n : INTEGER): BOOLEAN;
		VAR i : INTEGER;
	BEGIN
		IF a[0] # 1 THEN
			RETURN FALSE
		END;
		i := 1;
		WHILE (i < n) & (a[i] = 0) DO
			INC(i)
		END;
		RETURN i = n
	END IsOne;

	(* Выбор без ветвления: mask = 0 или -1. *)
	PROCEDURE Sel (x, y, mask : INTEGER): INTEGER;
	BEGIN
		RETURN ORD(BITS(x) * BITS(-1 - mask)) + ORD(BITS(y) * BITS(mask))
	END Sel;

	(* a < b как беззнаковые 32-битные слова; результат — 0 или -1. *)
	PROCEDURE MaskLess (a, b : INTEGER): INTEGER;
		VAR t : AO.SIGNED64;
	BEGIN
		t := AO.LONG32(a) - AO.LONG32(b);
		RETURN ASH(AO.SHORTHIGH64Raw(t), -31)
	END MaskLess;

	(* -1, если a = b, иначе 0; без ветвления. *)
	PROCEDURE MaskEq (a, b : INTEGER): INTEGER;
	BEGIN
		RETURN -1 - ORD(BITS(MaskLess(a, b)) + BITS(MaskLess(b, a)))
	END MaskEq;

	PROCEDURE CmpCanonical (IN a, b : BigInt; n : INTEGER): INTEGER;
		VAR i : INTEGER;
	BEGIN
		i := n - 1;
		WHILE (i >= 0) & (a[i] = b[i]) DO
			DEC(i)
		END;
		IF i < 0 THEN
			RETURN 0
		ELSIF AO.ULss32(a[i], b[i]) THEN
			RETURN -1
		ELSE
			RETURN 1
		END
	END CmpCanonical;

	(* ------------------------- поле по модулю ------------------------- *)

	PROCEDURE ModAdd (IN m : Modulus; IN a, b : BigInt; VAR r : BigInt);
		VAR
			s, d : BigInt;
			w : Words;
			i, carry, borrow, bm, hm, nm, mask : INTEGER;
			acc : LONGINT;
	BEGIN
		carry := 0;
		FOR i := 0 TO m.n - 1 DO
			w[0] := a[i]; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
			w[0] := b[i]; w[1] := 0; acc := acc + SYSTEM.VAL(LONGINT, w) + LONG(carry);
			w := SYSTEM.VAL(Words, acc);
			s[i] := w[0];
			carry := w[1]
		END;
		(* s - p; borrow после последнего разряда *)
		borrow := 0;
		FOR i := 0 TO m.n - 1 DO
			w[0] := s[i]; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
			w[0] := m.p[i]; w[1] := 0; acc := acc - SYSTEM.VAL(LONGINT, w) - LONG(borrow);
			w := SYSTEM.VAL(Words, acc);
			d[i] := w[0];
			bm := ASH(w[1], -31);
			borrow := -bm
		END;
		hm := -carry;
		nm := -borrow;  (* -1, если занимали *)
		(* вычитаем, если был перенос или s >= p (заёма не было) *)
		mask := ORD(BITS(hm) + BITS(-1 - nm));
		FOR i := 0 TO m.n - 1 DO
			r[i] := Sel(s[i], d[i], mask)
		END;
		FOR i := m.n TO MaxLimbs - 1 DO
			r[i] := 0
		END
	END ModAdd;

	PROCEDURE ModSub (IN m : Modulus; IN a, b : BigInt; VAR r : BigInt);
		VAR
			d : BigInt;
			w : Words;
			i, borrow, bm, mask, carry : INTEGER;
			acc : LONGINT;
	BEGIN
		borrow := 0;
		FOR i := 0 TO m.n - 1 DO
			w[0] := a[i]; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
			w[0] := b[i]; w[1] := 0; acc := acc - SYSTEM.VAL(LONGINT, w) - LONG(borrow);
			w := SYSTEM.VAL(Words, acc);
			d[i] := w[0];
			bm := ASH(w[1], -31);
			borrow := -bm
		END;
		mask := -borrow;
		carry := 0;
		FOR i := 0 TO m.n - 1 DO
			w[0] := d[i]; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
			w[0] := ORD(BITS(m.p[i]) * BITS(mask)); w[1] := 0;
			acc := acc + SYSTEM.VAL(LONGINT, w) + LONG(carry);
			w := SYSTEM.VAL(Words, acc);
			r[i] := w[0];
			carry := w[1]
		END;
		FOR i := m.n TO MaxLimbs - 1 DO
			r[i] := 0
		END
	END ModSub;

	(* Умножение Монтгомери: r = a*b/R mod m. Горячие циклы — в
	   CryptoArch.MontCore: на amd64 промежуточные суммы держатся в
	   LONGINT без вызовов на разряд, на i386 широкое умножение собрано
	   без переполнения FPU (Crypto/Mod64/Arch.cp и Crypto/Mod/Arch.cp). *)
	PROCEDURE MontMul (IN m : Modulus; IN a, b : BigInt; VAR r : BigInt);
		VAR
			t : ARRAY 2 * MaxLimbs + 1 OF INTEGER;
			d : BigInt;
			w : Words;
			i, borrow, bm, hm, nm, mask : INTEGER;
			acc : LONGINT;
	BEGIN
		Arch.MontCore(m.n, m.n0, a, b, m.p, t);
		(* Результат в t[n..2n-1], возможный старший разряд — t[2n]. *)
		borrow := 0;
		FOR i := 0 TO m.n - 1 DO
			w[0] := t[m.n + i]; w[1] := 0; acc := SYSTEM.VAL(LONGINT, w);
			w[0] := m.p[i]; w[1] := 0; acc := acc - SYSTEM.VAL(LONGINT, w) - LONG(borrow);
			w := SYSTEM.VAL(Words, acc);
			d[i] := w[0];
			bm := ASH(w[1], -31);
			borrow := -bm
		END;
		hm := -t[2 * m.n];
		nm := -borrow;
		mask := ORD(BITS(hm) + BITS(-1 - nm));
		FOR i := 0 TO m.n - 1 DO
			r[i] := Sel(t[m.n + i], d[i], mask)
		END;
		FOR i := m.n TO MaxLimbs - 1 DO
			r[i] := 0
		END
	END MontMul;

	PROCEDURE ToMont (IN m : Modulus; IN a : BigInt; VAR r : BigInt);
	BEGIN
		MontMul(m, a, m.r2, r)
	END ToMont;

	PROCEDURE FromMont (IN m : Modulus; IN a : BigInt; VAR r : BigInt);
		VAR one : BigInt;
	BEGIN
		SetInt(one, 1);
		MontMul(m, a, one, r)
	END FromMont;

	PROCEDURE BitOf (IN a : BigInt; i : INTEGER): INTEGER;
		VAR s : SET;
	BEGIN
		s := BITS(AO.ULSH32(a[i DIV 32], -(i MOD 32))) * BITS(1);
		RETURN ORD(s)
	END BitOf;

	(* a^e в Монтгомери-представлении; показатель e открыт. *)
	PROCEDURE ModExp (IN m : Modulus; IN a, e : BigInt; VAR r : BigInt);
		VAR
			base, acc : BigInt;
			i, top : INTEGER;
	BEGIN
		Copy(a, base);
		Copy(m.one, acc);
		top := m.n * 32 - 1;
		WHILE (top >= 0) & (BitOf(e, top) = 0) DO
			DEC(top)
		END;
		FOR i := top TO 0 BY (-1) DO
			MontMul(m, acc, acc, acc);
			IF BitOf(e, i) = 1 THEN
				MontMul(m, acc, base, acc)
			END
		END;
		Copy(acc, r)
	END ModExp;

	PROCEDURE ModInv (IN m : Modulus; IN a : BigInt; VAR r : BigInt);
		VAR e : BigInt;
			i : INTEGER;
	BEGIN
		(* e = p - 2 *)
		Copy(m.p, e);
		i := 0;
		WHILE e[i] = 0 DO
			e[i] := 0FFFFFFFFH;
			INC(i)
		END;
		DEC(e[i]);
		DEC(e[i]);
		ModExp(m, a, e, r)
	END ModInv;

	(* ------------------------- точки ------------------------- *)

	PROCEDURE SetInfinity (VAR p : Point; IN one : BigInt);
	BEGIN
		Zero(p.x);
		Copy(one, p.y);
		Zero(p.z)
	END SetInfinity;

	PROCEDURE PointIsInfinity (IN p : Point; n : INTEGER): BOOLEAN;
	BEGIN
		RETURN IsZeroN(p.z, n)
	END PointIsInfinity;

	(* Полное сложение: X/Y/Z уже в Монтгомери-представлении. *)
	PROCEDURE PointAdd (c : Curve; IN p, q : Point; VAR r : Point);
		VAR
			t0, t1, t2, t3, t4, t5, x3, y3, z3 : BigInt;
			m : Modulus;
			i : INTEGER;
	BEGIN
		m := c.fp;
		MontMul(m, p.x, q.x, t0);
		MontMul(m, p.y, q.y, t1);
		MontMul(m, p.z, q.z, t2);
		ModAdd(m, p.x, p.y, t3);
		ModAdd(m, q.x, q.y, t4);
		MontMul(m, t3, t4, t3);
		ModAdd(m, t0, t1, t4);
		ModSub(m, t3, t4, t3);
		IF c.aIsZero THEN
			(* Алгоритм 7: a = 0, используется b3 = 3b. *)
			ModAdd(m, p.y, p.z, t4);
			ModAdd(m, q.y, q.z, x3);
			MontMul(m, t4, x3, t4);
			ModAdd(m, t1, t2, x3);
			ModSub(m, t4, x3, t4);
			ModAdd(m, p.x, p.z, x3);
			ModAdd(m, q.x, q.z, y3);
			MontMul(m, x3, y3, x3);
			ModAdd(m, t0, t2, y3);
			ModSub(m, x3, y3, y3);
			ModAdd(m, t0, t0, x3);
			ModAdd(m, x3, t0, t0);
			MontMul(m, c.b3M, t2, t2);
			ModAdd(m, t1, t2, z3);
			ModSub(m, t1, t2, t1);
			MontMul(m, c.b3M, y3, y3);
			MontMul(m, t4, y3, x3);
			MontMul(m, t3, t1, t2);
			ModSub(m, t2, x3, x3);
			MontMul(m, y3, t0, y3);
			MontMul(m, t1, z3, t1);
			ModAdd(m, t1, y3, y3);
			MontMul(m, t0, t3, t0);
			MontMul(m, z3, t4, z3);
			ModAdd(m, z3, t0, z3)
		ELSE
			(* Алгоритм 4: a = -3. *)
			ModAdd(m, p.y, p.z, t4);
			ModAdd(m, q.y, q.z, x3);
			MontMul(m, t4, x3, t4);
			ModAdd(m, t1, t2, x3);
			ModSub(m, t4, x3, t4);
			ModAdd(m, p.x, p.z, x3);
			ModAdd(m, q.x, q.z, y3);
			MontMul(m, x3, y3, x3);
			ModAdd(m, t0, t2, y3);
			ModSub(m, x3, y3, y3);
			MontMul(m, c.bM, t2, z3);
			ModSub(m, y3, z3, x3);
			ModAdd(m, x3, x3, z3);
			ModAdd(m, x3, z3, x3);
			ModSub(m, t1, x3, z3);
			ModAdd(m, t1, x3, x3);
			MontMul(m, c.bM, y3, y3);
			ModAdd(m, t2, t2, t1);
			ModAdd(m, t1, t2, t2);
			ModSub(m, y3, t2, y3);
			ModSub(m, y3, t0, y3);
			ModAdd(m, y3, y3, t1);
			ModAdd(m, t1, y3, y3);
			ModAdd(m, t0, t0, t1);
			ModAdd(m, t1, t0, t0);
			ModSub(m, t0, t2, t0);
			MontMul(m, t4, y3, t1);
			MontMul(m, t0, y3, t2);
			MontMul(m, x3, z3, y3);
			ModAdd(m, y3, t2, y3);
			MontMul(m, t3, x3, x3);
			ModSub(m, x3, t1, x3);
			MontMul(m, t4, z3, z3);
			MontMul(m, t3, t0, t1);
			ModAdd(m, z3, t1, z3)
		END;
		(* x3, y3, z3 содержат результат; временные имена совпадали
		   с выходными в формулах, поэтому копируем в r. *)
		Copy(x3, r.x);
		Copy(y3, r.y);
		Copy(z3, r.z)
	END PointAdd;

	PROCEDURE Digit4 (IN k : BigInt; w : INTEGER): INTEGER;
		VAR s : SET;
			sh : INTEGER;
	BEGIN
		sh := (w MOD 8) * 4;
		s := BITS(k[w DIV 8]) * BITS(SYSTEM.LSH(0FH, sh));
		RETURN AO.ULSH32(ORD(s), -sh)
	END Digit4;

	PROCEDURE SelectTable (IN tab : ARRAY OF Point; digit, n : INTEGER; VAR r : Point);
		VAR i, j, mask : INTEGER;
	BEGIN
		Zero(r.x); Zero(r.y); Zero(r.z);
		FOR i := 0 TO WindowCount - 1 DO
			mask := MaskEq(i, digit);
			FOR j := 0 TO n - 1 DO
				r.x[j] := Sel(r.x[j], tab[i].x[j], mask);
				r.y[j] := Sel(r.y[j], tab[i].y[j], mask);
				r.z[j] := Sel(r.z[j], tab[i].z[j], mask)
			END
		END
	END SelectTable;

	PROCEDURE ScalarMult (c : Curve; IN k : BigInt; IN px, py : BigInt;
			VAR rx, ry : BigInt; VAR ok : BOOLEAN);
		VAR
			tab : ARRAY WindowCount OF Point;
			p, r, t, acc : Point;
			w, i : INTEGER;
			oneC : BigInt;
	BEGIN
		SetInt(oneC, 1);
		ToMont(c.fp, oneC, oneC);
		ToMont(c.fp, px, p.x);
		ToMont(c.fp, py, p.y);
		Copy(oneC, p.z);
		SetInfinity(tab[0], oneC);
		FOR i := 1 TO WindowCount - 1 DO
			PointAdd(c, tab[i - 1], p, tab[i])
		END;
		SetInfinity(r, oneC);
		FOR w := c.nbits DIV Window - 1 TO 0 BY (-1) DO
			FOR i := 1 TO Window DO
				PointAdd(c, r, r, r)
			END;
			SelectTable(tab, Digit4(k, w), c.n, t);
			PointAdd(c, r, t, r)
		END;
		(* Проективные -> аффинные. *)
		ok := ~PointIsInfinity(r, c.n);
		IF ok THEN
			ModInv(c.fp, r.z, acc.z);
			MontMul(c.fp, r.x, acc.z, acc.x);
			MontMul(c.fp, r.y, acc.z, acc.y);
			FromMont(c.fp, acc.x, rx);
			FromMont(c.fp, acc.y, ry)
		ELSE
			Zero(rx);
			Zero(ry)
		END
	END ScalarMult;

	(* ------------------------- открытый интерфейс ------------------------- *)

	PROCEDURE SetBytes* (c : Curve; IN buf : ARRAY OF SHORTCHAR; ofs, len : INTEGER;
			VAR x : BigInt): BOOLEAN;
		VAR i : INTEGER;
	BEGIN
		IF len # 4 * c.n THEN
			RETURN FALSE
		END;
		Zero(x);
		FOR i := 0 TO c.n - 1 DO
			x[i] := U.IntFromBufferBE(buf, ofs + 4 * (c.n - 1 - i))
		END;
		RETURN TRUE
	END SetBytes;

	PROCEDURE GetBytes* (c : Curve; IN x : BigInt; VAR buf : ARRAY OF SHORTCHAR; ofs : INTEGER);
		VAR i : INTEGER;
	BEGIN
		FOR i := 0 TO c.n - 1 DO
			U.IntToBufferBE(x[c.n - 1 - i], buf, ofs + 4 * i)
		END
	END GetBytes;

	PROCEDURE InRange (IN x, p : BigInt; n : INTEGER): BOOLEAN;
		VAR i, borrow, bm : INTEGER;
			acc : AO.SIGNED64;
	BEGIN
		(* x < p без ветвления: заём после вычитания всех разрядов. *)
		borrow := 0;
		FOR i := 0 TO n - 1 DO
			acc := AO.LONG32(x[i]) - AO.LONG32(p[i]) - AO.LONG32(borrow);
			bm := ASH(AO.SHORTHIGH64Raw(acc), -31);
			borrow := -bm
		END;
		RETURN borrow = 1
	END InRange;

	PROCEDURE OnCurve (c : Curve; IN x, y : BigInt): BOOLEAN;
		VAR
			xm, ym, lhs, rhs, t : BigInt;
	BEGIN
		ToMont(c.fp, x, xm);
		ToMont(c.fp, y, ym);
		MontMul(c.fp, ym, ym, lhs);          (* y^2 *)
		MontMul(c.fp, xm, xm, t);
		MontMul(c.fp, t, xm, rhs);           (* x^3 *)
		MontMul(c.fp, c.aM, xm, t);
		ModAdd(c.fp, rhs, t, rhs);           (* + a*x *)
		ModAdd(c.fp, rhs, c.bM, rhs);        (* + b *)
		RETURN CmpCanonical(lhs, rhs, c.n) = 0
	END OnCurve;

	PROCEDURE Validate* (c : Curve; IN x, y : BigInt): BOOLEAN;
	BEGIN
		IF ~InRange(x, c.fp.p, c.n) OR ~InRange(y, c.fp.p, c.n) THEN
			RETURN FALSE
		END;
		(* Точка (0,0) не лежит на этих кривых, но проверка дешева. *)
		IF IsZeroN(x, c.n) & IsZeroN(y, c.n) THEN
			RETURN FALSE
		END;
		RETURN OnCurve(c, x, y)
	END Validate;

	PROCEDURE PointFromBytes* (c : Curve; IN buf : ARRAY OF SHORTCHAR; len : INTEGER;
			VAR x, y : BigInt): BOOLEAN;
	BEGIN
		IF (len # 1 + 8 * c.n) OR (buf[0] # 4X) THEN
			RETURN FALSE
		END;
		IF ~SetBytes(c, buf, 1, 4 * c.n, x) THEN
			RETURN FALSE
		END;
		IF ~SetBytes(c, buf, 1 + 4 * c.n, 4 * c.n, y) THEN
			RETURN FALSE
		END;
		RETURN Validate(c, x, y)
	END PointFromBytes;

	PROCEDURE PointToBytes* (c : Curve; IN x, y : BigInt; VAR buf : ARRAY OF SHORTCHAR);
	BEGIN
		buf[0] := 4X;
		GetBytes(c, x, buf, 1);
		GetBytes(c, y, buf, 1 + 4 * c.n)
	END PointToBytes;

	(* ECDH: общий секрет — x-координата k*Q. Точка Q проверяется. *)
	PROCEDURE ECDH* (c : Curve; IN priv : BigInt; IN peerX, peerY : BigInt;
			VAR secretX : BigInt): BOOLEAN;
		VAR ok : BOOLEAN;
			dummy, zero : BigInt;
	BEGIN
		IF ~InRange(priv, c.fq.p, c.n) OR IsZeroN(priv, c.n) THEN
			RETURN FALSE
		END;
		IF ~Validate(c, peerX, peerY) THEN
			RETURN FALSE
		END;
		ScalarMult(c, priv, peerX, peerY, secretX, dummy, ok);
		RETURN ok & ~IsZeroN(secretX, c.n)
	END ECDH;

	(* k*G; ok = FALSE, если результат — бесконечность. *)
	PROCEDURE ScalarMulBase* (c : Curve; IN k : BigInt; VAR x, y : BigInt;
			VAR ok : BOOLEAN);
	BEGIN
		IF IsZeroN(k, c.n) OR ~InRange(k, c.fq.p, c.n) THEN
			ok := FALSE;
			Zero(x); Zero(y);
			RETURN
		END;
		ScalarMult(c, k, c.gx, c.gy, x, y, ok)
	END ScalarMulBase;

	(* k1*G + k2*Q; Q проверяется вызывающим (ECDSA проверяет точку). *)
	PROCEDURE ScalarMulAdd* (c : Curve; IN k1 : BigInt; IN qx, qy : BigInt;
			IN k2 : BigInt; VAR x, y : BigInt; VAR ok : BOOLEAN);
		VAR
			x1, y1, x2, y2 : BigInt;
			ok1, ok2 : BOOLEAN;
			p1, p2, p3 : Point;
			oneC : BigInt;
			inv : BigInt;
	BEGIN
		ScalarMult(c, k1, c.gx, c.gy, x1, y1, ok1);
		ScalarMult(c, k2, qx, qy, x2, y2, ok2);
		IF ~ok1 & ~ok2 THEN
			ok := FALSE; Zero(x); Zero(y); RETURN
		END;
		SetInt(oneC, 1);
		ToMont(c.fp, oneC, oneC);
		IF ok1 THEN
			ToMont(c.fp, x1, p1.x); ToMont(c.fp, y1, p1.y); Copy(oneC, p1.z)
		ELSE
			SetInfinity(p1, oneC)
		END;
		IF ok2 THEN
			ToMont(c.fp, x2, p2.x); ToMont(c.fp, y2, p2.y); Copy(oneC, p2.z)
		ELSE
			SetInfinity(p2, oneC)
		END;
		PointAdd(c, p1, p2, p3);
		ok := ~PointIsInfinity(p3, c.n);
		IF ok THEN
			ModInv(c.fp, p3.z, inv);
			MontMul(c.fp, p3.x, inv, p3.x);
			MontMul(c.fp, p3.y, inv, p3.y);
			FromMont(c.fp, p3.x, x);
			FromMont(c.fp, p3.y, y)
		ELSE
			Zero(x); Zero(y)
		END
	END ScalarMulAdd;

	PROCEDURE ReduceOrder* (c : Curve; IN x : BigInt; VAR r : BigInt);
	BEGIN
		(* Для хеша и координаты x < 2^(32n) < 2q, поэтому хватает
		   одного вычитания. *)
		IF CmpCanonical(x, c.order, c.n) >= 0 THEN
			ModSub(c.fq, x, c.order, r)
		ELSE
			Copy(x, r)
		END
	END ReduceOrder;

	PROCEDURE OrderAdd* (c : Curve; IN a, b : BigInt; VAR r : BigInt);
	BEGIN
		ModAdd(c.fq, a, b, r)
	END OrderAdd;

	PROCEDURE OrderMul* (c : Curve; IN a, b : BigInt; VAR r : BigInt);
		VAR am, bm : BigInt;
	BEGIN
		ToMont(c.fq, a, am);
		ToMont(c.fq, b, bm);
		MontMul(c.fq, am, bm, r);
		FromMont(c.fq, r, r)
	END OrderMul;

	PROCEDURE OrderInv* (c : Curve; IN a : BigInt; VAR r : BigInt);
		VAR am : BigInt;
	BEGIN
		ToMont(c.fq, a, am);
		ModInv(c.fq, am, r);
		FromMont(c.fq, r, r)
	END OrderInv;

	PROCEDURE OrderInRange* (c : Curve; IN a : BigInt): BOOLEAN;
	BEGIN
		RETURN ~IsZeroN(a, c.n) & InRange(a, c.fq.p, c.n)
	END OrderInRange;

	PROCEDURE OrderIsZero* (c : Curve; IN a : BigInt): BOOLEAN;
	BEGIN
		RETURN IsZeroN(a, c.n)
	END OrderIsZero;

	PROCEDURE OrderBytes* (c : Curve; IN a : BigInt; VAR buf : ARRAY OF SHORTCHAR; ofs : INTEGER);
	BEGIN
		GetBytes(c, a, buf, ofs)
	END OrderBytes;

	PROCEDURE OrderFromBytes* (c : Curve; IN buf : ARRAY OF SHORTCHAR; ofs, len : INTEGER;
			VAR a : BigInt): BOOLEAN;
	BEGIN
		RETURN SetBytes(c, buf, ofs, len, a)
	END OrderFromBytes;

	(* ------------------------- параметры кривых ------------------------- *)

	PROCEDURE InitMod (VAR m : Modulus; n : INTEGER; IN p, r2, one : BigInt; n0 : INTEGER);
	BEGIN
		m.n := n;
		Copy(p, m.p);
		Copy(r2, m.r2);
		Copy(one, m.one);
		m.n0 := n0
	END InitMod;

	(* Загрузка n 32-битных разрядов из строки, младший — первым. *)
	PROCEDURE Load (n : INTEGER; IN s : ARRAY OF SHORTCHAR; VAR a : BigInt);
		VAR buf : U.InitBuffer;
			i : INTEGER;
	BEGIN
		NEW(buf);
		buf.Init(LEN(s) + 4);
		buf.Add(s);
		Zero(a);
		FOR i := 0 TO n - 1 DO
			a[i] := buf.GetInt()
		END
	END Load;

	PROCEDURE MakeP256 (): Curve;
		VAR c : Curve;
			p, r2, one, b, b3, gx, gy, a : BigInt;
	BEGIN
		NEW(c);
		c.n := 8;
		c.nbits := 256;
		c.aIsZero := FALSE;

		Load(8, "FFFFFFFF FFFFFFFF FFFFFFFF 00000000 00000000 00000000 00000001 FFFFFFFF", p);
		Load(8, "00000003 00000000 FFFFFFFF FFFFFFFB FFFFFFFE FFFFFFFF FFFFFFFD 00000004", r2);
		Load(8, "00000001 00000000 00000000 FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFE 00000000", one);
		InitMod(c.fp, 8, p, r2, one, 000000001H);

		Load(8, "FC632551 F3B9CAC2 A7179E84 BCE6FAAD FFFFFFFF FFFFFFFF 00000000 FFFFFFFF", p);
		Load(8, "BE79EEA2 83244C95 49BD6FA6 4699799C 2B6BEC59 2845B239 F3D95620 66E12D94", r2);
		Load(8, "039CDAAF 0C46353D 58E8617B 43190552 00000000 00000000 FFFFFFFF 00000000", one);
		InitMod(c.fq, 8, p, r2, one, 0EE00BC4FH);
		Copy(p, c.order);

		Zero(a);
		Load(8, "FFFFFFFC FFFFFFFF FFFFFFFF 00000000 00000000 00000000 00000001 FFFFFFFF", a);
		Load(8, "27D2604B 3BCE3C3E CC53B0F6 651D06B0 769886BC B3EBBD55 AA3A93E7 5AC635D8", b);
		Load(8, "777720E2 B36AB4BA 64FB12E2 2F571411 63C99435 1BC33800 FEAFBBB6 1052A18A", b3);
		Load(8, "D898C296 F4A13945 2DEB33A0 77037D81 63A440F2 F8BCE6E5 E12C4247 6B17D1F2", gx);
		Load(8, "37BF51F5 CBB64068 6B315ECE 2BCE3357 7C0F9E16 8EE7EB4A FE1A7F9B 4FE342E2", gy);
		Copy(gx, c.gx);
		Copy(gy, c.gy);
		ToMont(c.fp, a, c.aM);
		ToMont(c.fp, b, c.bM);
		ToMont(c.fp, b3, c.b3M);
		RETURN c
	END MakeP256;

	PROCEDURE MakeP384 (): Curve;
		VAR c : Curve;
			p, r2, one, a, b, gx, gy : BigInt;
	BEGIN
		NEW(c);
		c.n := 12;
		c.nbits := 384;
		c.aIsZero := FALSE;

		Load(12, "FFFFFFFF 00000000 00000000 FFFFFFFF FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF", p);
		Load(12, "00000001 FFFFFFFE 00000000 00000002 00000000 FFFFFFFE 00000000 00000002 00000001 00000000 00000000 00000000", r2);
		Load(12, "00000001 FFFFFFFF FFFFFFFF 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000", one);
		InitMod(c.fp, 12, p, r2, one, 000000001H);

		Load(12, "CCC52973 ECEC196A 48B0A77A 581A0DB2 F4372DDF C7634D81 FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF", p);
		Load(12, "19B409A9 2D319B24 DF1AA419 FF3D81E5 FCB82947 BC3E483A 4AAB1CC5 D40D4917 28266895 3FB05B7A 2B39BF21 0C84EE01", r2);
		Load(12, "333AD68D 1313E695 B74F5885 A7E5F24D 0BC8D220 389CB27E 00000000 00000000 00000000 00000000 00000000 00000000", one);
		InitMod(c.fq, 12, p, r2, one, 0E88FDC45H);
		Copy(p, c.order);

		Load(12, "FFFFFFFC 00000000 00000000 FFFFFFFF FFFFFFFE FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF", a);
		Load(12, "D3EC2AEF 2A85C8ED 8A2ED19D C656398D 5013875A 0314088F FE814112 181D9C6E E3F82D19 988E056B E23EE7E4 B3312FA7", b);
		Load(12, "72760AB7 3A545E38 BF55296C 5502F25D 82542A38 59F741E0 8BA79B98 6E1D3B62 F320AD74 8EB1C71E BE8B0537 AA87CA22", gx);
		Load(12, "90EA0E5F 7A431D7C 1D7E819D 0A60B1CE B5F0B8C0 E9DA3113 289A147C F8F41DBD 9292DC29 5D9E98BF 96262C6F 3617DE4A", gy);
		Copy(gx, c.gx);
		Copy(gy, c.gy);
		ToMont(c.fp, a, c.aM);
		ToMont(c.fp, b, c.bM);
		Zero(c.b3M);
		RETURN c
	END MakeP384;

	PROCEDURE P256* (): Curve;
	BEGIN
		IF p256 = NIL THEN
			p256 := MakeP256()
		END;
		RETURN p256
	END P256;

	PROCEDURE P384* (): Curve;
	BEGIN
		IF p384 = NIL THEN
			p384 := MakeP384()
		END;
		RETURN p384
	END P384;

END CryptoEC.
