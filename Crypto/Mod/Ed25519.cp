MODULE CryptoEd25519;
(** PURPOSE "Ed25519 (RFC 8032): проверка подписи"; *)

	IMPORT FE := CryptoFieldElement, SHA := CryptoSHA512, Hashes := CryptoHashes,
		U := CryptoUtils, AO := CryptoAosCompat;

	TYPE
		Point = RECORD
			x, y, z, t : FE.FieldElement
		END;

	VAR
		d, d2, sqrtm1, zero, one : FE.FieldElement;
		base : Point;
		pMod, lBytes, tmpB : ARRAY 32 OF SHORTCHAR;

	PROCEDURE CopyPoint (IN a : Point; VAR b : Point);
	BEGIN
		FE.Copy(b.x, a.x); FE.Copy(b.y, a.y);
		FE.Copy(b.z, a.z); FE.Copy(b.t, a.t)
	END CopyPoint;

	PROCEDURE FAdd (IN a, b : FE.FieldElement; VAR r : FE.FieldElement);
	BEGIN
		FE.Add(a, b, r)
	END FAdd;

	PROCEDURE FSub (IN a, b : FE.FieldElement; VAR r : FE.FieldElement);
	BEGIN
		FE.Sub(a, b, r)
	END FSub;

	PROCEDURE FMul (IN a, b : FE.FieldElement; VAR r : FE.FieldElement);
		VAR x, y, z : FE.FieldElement;
	BEGIN
		FE.Copy(x, a); FE.Copy(y, b);
		x.Multiply(y, z);
		FE.Copy(r, z)
	END FMul;

	PROCEDURE FSqr (IN a : FE.FieldElement; VAR r : FE.FieldElement);
		VAR x, z : FE.FieldElement;
	BEGIN
		FE.Copy(x, a);
		x.Square(z);
		FE.Copy(r, z)
	END FSqr;

	PROCEDURE FNeg (IN a : FE.FieldElement; VAR r : FE.FieldElement);
	BEGIN
		FE.Sub(zero, a, r)
	END FNeg;

	PROCEDURE FIsZero (IN a : FE.FieldElement): BOOLEAN;
		VAR b : ARRAY 32 OF SHORTCHAR;
			x : FE.FieldElement;
			i, acc : INTEGER;
	BEGIN
		FE.Copy(x, a);
		x.ToBytes(b);
		acc := 0;
		FOR i := 0 TO 31 DO
			acc := ORD(BITS(acc) + BITS(ORD(b[i])))
		END;
		RETURN acc = 0
	END FIsZero;

	PROCEDURE FEqual (IN a, b : FE.FieldElement): BOOLEAN;
		VAR x, y : ARRAY 32 OF SHORTCHAR;
			fa, fb : FE.FieldElement;
			i : INTEGER;
	BEGIN
		FE.Copy(fa, a);
		fa.ToBytes(x);
		FE.Copy(fb, b);
		fb.ToBytes(y);
		i := 0;
		WHILE (i < 32) & (x[i] = y[i]) DO INC(i) END;
		RETURN i = 32
	END FEqual;

	PROCEDURE BitOf (IN b : ARRAY OF SHORTCHAR; i : INTEGER): INTEGER;
		VAR s : SET;
	BEGIN
		s := BITS(AO.ULSH32(ORD(b[i DIV 8]), -(i MOD 8))) * BITS(1);
		RETURN ORD(s)
	END BitOf;

	PROCEDURE Pow (IN a : FE.FieldElement; IN exp : ARRAY OF SHORTCHAR;
			bits : INTEGER; VAR r : FE.FieldElement);
		VAR base, acc, t : FE.FieldElement;
			i : INTEGER;
	BEGIN
		FE.Copy(base, a);
		acc.One();
		FOR i := bits - 1 TO 0 BY (-1) DO
			FSqr(acc, t); FE.Copy(acc, t);
			IF BitOf(exp, i) = 1 THEN
				FMul(acc, base, t); FE.Copy(acc, t)
			END
		END;
		FE.Copy(r, acc)
	END Pow;

	PROCEDURE Add (IN p, q : Point; VAR r : Point);
		VAR
			a, b, c, dd, e, f, g, h, t1, t2, t3 : FE.FieldElement;
	BEGIN
		FSub(p.y, p.x, t1); FSub(q.y, q.x, t2); FMul(t1, t2, a);
		FAdd(p.y, p.x, t1); FAdd(q.y, q.x, t2); FMul(t1, t2, b);
		FMul(p.t, d2, t1); FMul(t1, q.t, c);
		FMul(p.z, q.z, t1); FAdd(t1, t1, dd);
		FSub(b, a, e); FSub(dd, c, f);
		FAdd(dd, c, g); FAdd(b, a, h);
		FMul(e, f, r.x); FMul(g, h, r.y);
		FMul(e, h, r.t); FMul(f, g, r.z)
	END Add;

	PROCEDURE CSwapPoint (VAR a, b : Point; bit : INTEGER);
	BEGIN
		FE.CSwap(a.x, b.x, bit); FE.CSwap(a.y, b.y, bit);
		FE.CSwap(a.z, b.z, bit); FE.CSwap(a.t, b.t, bit)
	END CSwapPoint;

	PROCEDURE BitEq (a, b : INTEGER): INTEGER;
		VAR d : INTEGER;
	BEGIN
		d := a - b;
		RETURN (225 - d * d) DIV 225
	END BitEq;

	PROCEDURE ScalarMult (IN scalar : ARRAY OF SHORTCHAR; scalarLen : INTEGER;
			IN p : Point; VAR r : Point);
		VAR
			tab, sel : ARRAY 16 OF Point;
			acc, t, sum : Point;
			w, i, digit, b : INTEGER;
	BEGIN
		FE.Copy(tab[0].x, zero); FE.Copy(tab[0].y, one);
		FE.Copy(tab[0].z, one); FE.Copy(tab[0].t, zero);
		CopyPoint(p, tab[1]);
		FOR i := 2 TO 15 DO
			Add(tab[i - 1], p, tab[i])
		END;
		CopyPoint(tab[0], acc);
		FOR w := scalarLen * 2 - 1 TO 0 BY (-1) DO
			FOR i := 1 TO 4 DO
				Add(acc, acc, t); CopyPoint(t, acc)
			END;
			b := ORD(scalar[w DIV 2]);
			IF ODD(w) THEN
				digit := AO.ULSH32(ORD(BITS(b) * BITS(0F0H)), -4)
			ELSE
				digit := ORD(BITS(b) * BITS(0FH))
			END;
			FOR i := 0 TO 15 DO
				CopyPoint(tab[i], sel[i])
			END;
			CopyPoint(sel[0], t);
			FOR i := 1 TO 15 DO
				CSwapPoint(t, sel[i], BitEq(i, digit))
			END;
			Add(acc, t, sum); CopyPoint(sum, acc)
		END;
		CopyPoint(acc, r)
	END ScalarMult;

	PROCEDURE LessThanP (IN b : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR i : INTEGER; x, y : INTEGER;
	BEGIN
		i := 31;
		WHILE (i >= 0) & (b[i] = pMod[i]) DO DEC(i) END;
		IF i < 0 THEN RETURN FALSE END;
		RETURN ORD(b[i]) < ORD(pMod[i])
	END LessThanP;

	PROCEDURE LessThanL (IN b : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR i : INTEGER;
	BEGIN
		i := 31;
		WHILE (i >= 0) & (b[i] = lBytes[i]) DO DEC(i) END;
		IF i < 0 THEN RETURN FALSE END;
		RETURN ORD(b[i]) < ORD(lBytes[i])
	END LessThanL;

	(* Декодирование точки RFC 8032, раздел 5.1.3. *)
	PROCEDURE Decode (IN enc : ARRAY OF SHORTCHAR; VAR p : Point): BOOLEAN;
		VAR
			b : ARRAY 32 OF SHORTCHAR;
			y, y2, u, v, x, x2, chk, t : FE.FieldElement;
			exp : ARRAY 32 OF SHORTCHAR;
			sign, signX, i : INTEGER;
			xb : ARRAY 32 OF SHORTCHAR;
	BEGIN
		FOR i := 0 TO 31 DO b[i] := enc[i] END;
		sign := ORD(BITS(ORD(b[31])) * BITS(128)) DIV 128;
		b[31] := SHORT(CHR(ORD(BITS(ORD(b[31])) * BITS(127))));
		IF ~LessThanP(b) THEN RETURN FALSE END;
		y.InitFromBytes(b);
		FSqr(y, y2);
		FSub(y2, one, u);
		FMul(d, y2, t); FAdd(t, one, v);
		v.Invert(t);
		FMul(u, t, x2);
		(* exp = (p+3)/8 = 2^252 - 2 *)
		FOR i := 0 TO 31 DO exp[i] := 0X END;
		FOR i := 0 TO 31 DO exp[i] := SHORT(CHR(0FFH)) END;
		exp[0] := SHORT(CHR(0FEH));
		exp[31] := SHORT(CHR(00FH));
		Pow(x2, exp, 252, x);
		FSqr(x, chk);
		IF ~FEqual(chk, x2) THEN
			FMul(x, sqrtm1, x);
			FSqr(x, chk);
			IF ~FEqual(chk, x2) THEN RETURN FALSE END
		END;
		x.ToBytes(xb);
		signX := ORD(BITS(ORD(xb[0])) * BITS(1));
		IF FIsZero(x) & (sign = 1) THEN RETURN FALSE END;
		IF signX # sign THEN FNeg(x, x) END;
		FE.Copy(p.x, x);
		FE.Copy(p.y, y);
		FE.Copy(p.z, one);
		FMul(x, y, p.t);
		RETURN TRUE
	END Decode;

	PROCEDURE Encode (IN p : Point; VAR enc : ARRAY OF SHORTCHAR);
		VAR zinv, x, y : FE.FieldElement;
			xb : ARRAY 32 OF SHORTCHAR;
			sign : INTEGER;
	BEGIN
		FE.Copy(zinv, p.z);
		zinv.Invert(zinv);
		FMul(p.x, zinv, x);
		FMul(p.y, zinv, y);
		FE.Copy(y, y);
		y.ToBytes(enc);
		x.ToBytes(xb);
		sign := ORD(BITS(ORD(xb[0])) * BITS(1));
		enc[31] := SHORT(CHR(ORD(BITS(ORD(enc[31])) * BITS(127)) + sign * 128))
	END Encode;

	PROCEDURE PointEqual (IN a, b : Point): BOOLEAN;
		VAR t1, t2, t3, t4 : FE.FieldElement;
	BEGIN
		FMul(a.x, b.z, t1); FMul(b.x, a.z, t2);
		IF ~FEqual(t1, t2) THEN RETURN FALSE END;
		FMul(a.y, b.z, t3); FMul(b.y, a.z, t4);
		RETURN FEqual(t3, t4)
	END PointEqual;

	(* Проверка подписи: pub 32 байта, sig 64 байта (R || S). *)
	PROCEDURE Verify* (IN pub : ARRAY OF SHORTCHAR;
			IN msg : ARRAY OF SHORTCHAR; msgLen : INTEGER;
			IN sig : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR
			a, r, negA, ha, sb, p : Point;
			hb : ARRAY 64 OF SHORTCHAR;
			h : Hashes.Hash;
			i : INTEGER;
			buf : ARRAY 32 OF SHORTCHAR;
	BEGIN
		FOR i := 32 TO 63 DO buf[i - 32] := sig[i] END;
		IF ~LessThanL(buf) THEN RETURN FALSE END;   (* S < L *)
		IF ~Decode(pub, a) THEN RETURN FALSE END;
		IF ~Decode(sig, r) THEN RETURN FALSE END;
		h := SHA.NewHash();
		h.Initialize;
		h.Update(sig, 0, 32);
		h.Update(pub, 0, 32);
		h.Update(msg, 0, msgLen);
		h.GetHash(hb, 0);

		CopyPoint(a, negA);
		FNeg(a.x, negA.x);
		FNeg(a.t, negA.t);
		ScalarMult(hb, 64, negA, ha);
		FOR i := 0 TO 31 DO buf[i] := sig[32 + i] END;
		ScalarMult(buf, 32, base, sb);
		Add(sb, ha, p);
		RETURN PointEqual(p, r)
	END Verify;

	PROCEDURE FromHex (IN hex : ARRAY OF SHORTCHAR; VAR b : ARRAY OF SHORTCHAR);
	BEGIN
		U.Hex2Bin(hex, 0, b, 0, 32)
	END FromHex;

BEGIN
	zero.Init();
	one.Init();
	one.One();
	(* Модуль поля p = 2^255 - 19. *)
	FromHex("EDFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF7F", pMod);
	FromHex("A3785913CA4DEB75ABD841414D0A700098E879777940C78C73FE6F2BEE6C0352", tmpB);
	d.InitFromBytes(tmpB);
	d2 := d; FE.Add(d2, d, d2);
	FromHex("B0A00E4A271BEEC478E42FAD0618432FA7D7FB3D99004D2B0BDFC14F8024832B", tmpB);
	sqrtm1.InitFromBytes(tmpB);
	FromHex("EDD3F55C1A631258D69CF7A2DEF9DE1400000000000000000000000000000010", lBytes);
	FromHex("5866666666666666666666666666666666666666666666666666666666666666", tmpB);
	IF ~Decode(tmpB, base) THEN HALT(100) END
END CryptoEd25519.
