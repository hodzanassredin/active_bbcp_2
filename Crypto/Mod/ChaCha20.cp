MODULE CryptoChaCha20;
(** PURPOSE "ChaCha20 и Poly1305 (RFC 8439)"; *)

	IMPORT SYSTEM, U := CryptoUtils, AO := CryptoAosCompat;

	CONST
		BlockSize = 64;
		TagSize = 16;
		Mask26 = 03FFFFFFH;

	TYPE
		Context* = RECORD
			r0, r1, r2, r3, r4 : INTEGER;
			h0, h1, h2, h3, h4 : INTEGER;
			s0, s1, s2, s3 : INTEGER;
			buf : ARRAY 16 OF SHORTCHAR;
			bufLen : INTEGER
		END;

	VAR
		sigma : ARRAY 4 OF SET;

	PROCEDURE Add32 (a, b : SET): SET;
	BEGIN
		RETURN BITS(AO.UAdd32(ORD(a), ORD(b)))
	END Add32;

	PROCEDURE Xor (a, b : SET): SET;
	BEGIN
		RETURN a / b
	END Xor;

	PROCEDURE Rotl (x : SET; n : INTEGER): SET;
	BEGIN
		RETURN SYSTEM.ROT(x, n)
	END Rotl;

	PROCEDURE QR (VAR a, b, c, d : SET);
	BEGIN
		a := Add32(a, b); d := Xor(d, a); d := Rotl(d, 16);
		c := Add32(c, d); b := Xor(b, c); b := Rotl(b, 12);
		a := Add32(a, b); d := Xor(d, a); d := Rotl(d, 8);
		c := Add32(c, d); b := Xor(b, c); b := Rotl(b, 7)
	END QR;

	PROCEDURE Block (IN key : ARRAY OF SHORTCHAR; IN nonce : ARRAY OF SHORTCHAR;
			counter : INTEGER; VAR out : ARRAY OF SHORTCHAR);
		VAR
			x, w : ARRAY 16 OF SET;
			i : INTEGER;
	BEGIN
		x[0] := sigma[0]; x[1] := sigma[1]; x[2] := sigma[2]; x[3] := sigma[3];
		FOR i := 0 TO 7 DO
			x[4 + i] := U.SetFromBufferLE(key, 4 * i)
		END;
		x[12] := BITS(counter);
		FOR i := 0 TO 2 DO
			x[13 + i] := U.SetFromBufferLE(nonce, 4 * i)
		END;
		FOR i := 0 TO 15 DO
			w[i] := x[i]
		END;
		FOR i := 1 TO 10 DO
			QR(w[0], w[4], w[8], w[12]);
			QR(w[1], w[5], w[9], w[13]);
			QR(w[2], w[6], w[10], w[14]);
			QR(w[3], w[7], w[11], w[15]);
			QR(w[0], w[5], w[10], w[15]);
			QR(w[1], w[6], w[11], w[12]);
			QR(w[2], w[7], w[8], w[13]);
			QR(w[3], w[4], w[9], w[14])
		END;
		FOR i := 0 TO 15 DO
			U.IntToBufferLE(ORD(Add32(w[i], x[i])), out, 4 * i)
		END
	END Block;

	PROCEDURE StreamXor* (IN key : ARRAY OF SHORTCHAR; IN nonce : ARRAY OF SHORTCHAR;
			counter : INTEGER; IN src : ARRAY OF SHORTCHAR; srcLen : INTEGER;
			VAR dst : ARRAY OF SHORTCHAR);
		VAR
			ks : ARRAY BlockSize OF SHORTCHAR;
			i, j, n, c : INTEGER;
	BEGIN
		c := counter;
		i := 0;
		WHILE i < srcLen DO
			Block(key, nonce, c, ks);
			INC(c);
			n := BlockSize;
			IF n > srcLen - i THEN
				n := srcLen - i
			END;
			FOR j := 0 TO n - 1 DO
				dst[i + j] := SHORT(CHR(ORD(BITS(ORD(src[i + j])) / BITS(ORD(ks[j])))))
			END;
			INC(i, BlockSize)
		END
	END StreamXor;

	(* ---------------- Poly1305 ---------------- *)

	PROCEDURE LongOf (x : INTEGER): AO.UNSIGNED64;
	BEGIN
		RETURN AO.LONG32(x)
	END LongOf;

	PROCEDURE And26 (x : AO.UNSIGNED64): INTEGER;
	BEGIN
		RETURN AO.SHORT64(AO.UBitwiseAnd64(x, AO.LONG32(Mask26)))
	END And26;

	PROCEDURE Shr (x : AO.UNSIGNED64; n : INTEGER): AO.UNSIGNED64;
	BEGIN
		RETURN AO.ULSH64(x, -n)
	END Shr;

	PROCEDURE Init* (VAR p : Context; IN key : ARRAY OF SHORTCHAR);
	BEGIN
		p.r0 := ORD(BITS(U.IntFromBufferLE(key, 0)) * BITS(Mask26));
		p.r1 := ORD(BITS(AO.ULSH32(U.IntFromBufferLE(key, 3), -2)) * BITS(03FFFF03H));
		p.r2 := ORD(BITS(AO.ULSH32(U.IntFromBufferLE(key, 6), -4)) * BITS(03FFC0FFH));
		p.r3 := ORD(BITS(AO.ULSH32(U.IntFromBufferLE(key, 9), -6)) * BITS(03F03FFFH));
		p.r4 := ORD(BITS(AO.ULSH32(U.IntFromBufferLE(key, 12), -8)) * BITS(000FFFFFH));
		p.h0 := 0; p.h1 := 0; p.h2 := 0; p.h3 := 0; p.h4 := 0;
		p.s0 := U.IntFromBufferLE(key, 16);
		p.s1 := U.IntFromBufferLE(key, 20);
		p.s2 := U.IntFromBufferLE(key, 24);
		p.s3 := U.IntFromBufferLE(key, 28);
		p.bufLen := 0
	END Init;

	PROCEDURE BlockAdd (VAR p : Context; full : BOOLEAN);
		VAR
			lo, hi, m : AO.UNSIGNED64;
	BEGIN
		lo := AO.UAdd64(LongOf(U.IntFromBufferLE(p.buf, 0)),
			AO.ULSH64(LongOf(U.IntFromBufferLE(p.buf, 4)), 32));
		hi := AO.UAdd64(LongOf(U.IntFromBufferLE(p.buf, 8)),
			AO.ULSH64(LongOf(U.IntFromBufferLE(p.buf, 12)), 32));
		p.h0 := p.h0 + And26(lo);
		p.h1 := p.h1 + And26(Shr(lo, 26));
		p.h2 := p.h2 + And26(AO.UAdd64(Shr(lo, 52), AO.ULSH64(hi, 12)));
		p.h3 := p.h3 + And26(Shr(hi, 14));
		p.h4 := p.h4 + And26(Shr(hi, 40));
		IF full THEN
			p.h4 := p.h4 + ASH(1, 24)
		END
	END BlockAdd;

	PROCEDURE MulR (VAR p : Context);
		VAR
			d0, d1, d2, d3, d4, c : AO.UNSIGNED64;
			r0, r1, r2, r3, r4 : INTEGER;
	BEGIN
		r0 := p.r0; r1 := p.r1; r2 := p.r2; r3 := p.r3;
		r4 := p.r4;
		d0 := AO.UMul64(LongOf(p.h0), LongOf(r0));
		d0 := AO.UAdd64(d0, AO.UMul64(LongOf(p.h1), LongOf(r4 * 5)));
		d0 := AO.UAdd64(d0, AO.UMul64(LongOf(p.h2), LongOf(r3 * 5)));
		d0 := AO.UAdd64(d0, AO.UMul64(LongOf(p.h3), LongOf(r2 * 5)));
		d0 := AO.UAdd64(d0, AO.UMul64(LongOf(p.h4), LongOf(r1 * 5)));

		d1 := AO.UMul64(LongOf(p.h0), LongOf(r1));
		d1 := AO.UAdd64(d1, AO.UMul64(LongOf(p.h1), LongOf(r0)));
		d1 := AO.UAdd64(d1, AO.UMul64(LongOf(p.h2), LongOf(r4 * 5)));
		d1 := AO.UAdd64(d1, AO.UMul64(LongOf(p.h3), LongOf(r3 * 5)));
		d1 := AO.UAdd64(d1, AO.UMul64(LongOf(p.h4), LongOf(r2 * 5)));

		d2 := AO.UMul64(LongOf(p.h0), LongOf(r2));
		d2 := AO.UAdd64(d2, AO.UMul64(LongOf(p.h1), LongOf(r1)));
		d2 := AO.UAdd64(d2, AO.UMul64(LongOf(p.h2), LongOf(r0)));
		d2 := AO.UAdd64(d2, AO.UMul64(LongOf(p.h3), LongOf(r4 * 5)));
		d2 := AO.UAdd64(d2, AO.UMul64(LongOf(p.h4), LongOf(r3 * 5)));

		d3 := AO.UMul64(LongOf(p.h0), LongOf(r3));
		d3 := AO.UAdd64(d3, AO.UMul64(LongOf(p.h1), LongOf(r2)));
		d3 := AO.UAdd64(d3, AO.UMul64(LongOf(p.h2), LongOf(r1)));
		d3 := AO.UAdd64(d3, AO.UMul64(LongOf(p.h3), LongOf(r0)));
		d3 := AO.UAdd64(d3, AO.UMul64(LongOf(p.h4), LongOf(r4 * 5)));

		d4 := AO.UMul64(LongOf(p.h0), LongOf(r4));
		d4 := AO.UAdd64(d4, AO.UMul64(LongOf(p.h1), LongOf(r3)));
		d4 := AO.UAdd64(d4, AO.UMul64(LongOf(p.h2), LongOf(r2)));
		d4 := AO.UAdd64(d4, AO.UMul64(LongOf(p.h3), LongOf(r1)));
		d4 := AO.UAdd64(d4, AO.UMul64(LongOf(p.h4), LongOf(r0)));

		c := Shr(d0, 26); p.h0 := And26(d0); d1 := AO.UAdd64(d1, c);
		c := Shr(d1, 26); p.h1 := And26(d1); d2 := AO.UAdd64(d2, c);
		c := Shr(d2, 26); p.h2 := And26(d2); d3 := AO.UAdd64(d3, c);
		c := Shr(d3, 26); p.h3 := And26(d3); d4 := AO.UAdd64(d4, c);
		c := Shr(d4, 26); p.h4 := And26(d4);
		p.h0 := p.h0 + AO.SHORT64(AO.UMul64(c, LongOf(5)));
		c := LongOf(p.h0);
		p.h0 := And26(c);
		p.h1 := p.h1 + AO.SHORT64(Shr(c, 26))
	END MulR;

	PROCEDURE Update* (VAR p : Context; IN data : ARRAY OF SHORTCHAR; ofs, len : INTEGER);
		VAR
			i : INTEGER;
	BEGIN
		i := 0;
		WHILE i < len DO
			p.buf[p.bufLen] := data[ofs + i];
			INC(p.bufLen);
			INC(i);
			IF p.bufLen = 16 THEN
				BlockAdd(p, TRUE);
				MulR(p);
				p.bufLen := 0
			END
		END
	END Update;

	PROCEDURE Final* (VAR p : Context; VAR tag : ARRAY OF SHORTCHAR);
		VAR
			i, g0, g1, g2, g3, g4, c, sign, mask : INTEGER;
			f, carry : AO.UNSIGNED64;
			h0, h1, h2, h3 : INTEGER;
	BEGIN
		IF p.bufLen > 0 THEN
			p.buf[p.bufLen] := 1X;
			i := p.bufLen + 1;
			WHILE i < 16 DO
				p.buf[i] := 0X;
				INC(i)
			END;
			BlockAdd(p, FALSE);
			MulR(p)
		END;
		(* h += -p = 5, затем выбрать h или h - p. *)
		c := AO.SHORT64(Shr(LongOf(p.h1), 26)); p.h1 := And26(LongOf(p.h1)); p.h2 := p.h2 + AO.SHORT64(c);
		c := AO.SHORT64(Shr(LongOf(p.h2), 26)); p.h2 := And26(LongOf(p.h2)); p.h3 := p.h3 + AO.SHORT64(c);
		c := AO.SHORT64(Shr(LongOf(p.h3), 26)); p.h3 := And26(LongOf(p.h3)); p.h4 := p.h4 + AO.SHORT64(c);
		c := AO.SHORT64(Shr(LongOf(p.h4), 26)); p.h4 := And26(LongOf(p.h4));
		p.h0 := p.h0 + AO.SHORT64(AO.UMul64(c, LongOf(5)));
		c := AO.SHORT64(Shr(LongOf(p.h0), 26)); p.h0 := And26(LongOf(p.h0)); p.h1 := p.h1 + AO.SHORT64(c);

		g0 := p.h0 + 5;
		c := ASH(g0, -26); g0 := ORD(BITS(g0) * BITS(Mask26));
		g1 := p.h1 + c; c := ASH(g1, -26); g1 := ORD(BITS(g1) * BITS(Mask26));
		g2 := p.h2 + c; c := ASH(g2, -26); g2 := ORD(BITS(g2) * BITS(Mask26));
		g3 := p.h3 + c; c := ASH(g3, -26); g3 := ORD(BITS(g3) * BITS(Mask26));
		g4 := p.h4 + c - ASH(1, 26);
		sign := ASH(g4, -31);
		mask := -1 - sign;   (* -1, если g4 >= 0 *)
		p.h0 := ORD(BITS(p.h0) * BITS(-1 - mask)) + ORD(BITS(g0) * BITS(mask));
		p.h1 := ORD(BITS(p.h1) * BITS(-1 - mask)) + ORD(BITS(g1) * BITS(mask));
		p.h2 := ORD(BITS(p.h2) * BITS(-1 - mask)) + ORD(BITS(g2) * BITS(mask));
		p.h3 := ORD(BITS(p.h3) * BITS(-1 - mask)) + ORD(BITS(g3) * BITS(mask));
		p.h4 := ORD(BITS(p.h4) * BITS(-1 - mask)) + ORD(BITS(g4) * BITS(mask));

		h0 := ORD(BITS(p.h0) + BITS(AO.ULSH32(p.h1, 26)));
		h1 := ORD(BITS(AO.ULSH32(p.h1, -6)) + BITS(AO.ULSH32(p.h2, 20)));
		h2 := ORD(BITS(AO.ULSH32(p.h2, -12)) + BITS(AO.ULSH32(p.h3, 14)));
		h3 := ORD(BITS(AO.ULSH32(p.h3, -18)) + BITS(AO.ULSH32(p.h4, 8)));

		f := AO.UAdd64(LongOf(h0), LongOf(p.s0));
		U.IntToBufferLE(AO.SHORT64(f), tag, 0);
		carry := Shr(f, 32);
		f := AO.UAdd64(AO.UAdd64(LongOf(h1), LongOf(p.s1)), carry);
		U.IntToBufferLE(AO.SHORT64(f), tag, 4);
		carry := Shr(f, 32);
		f := AO.UAdd64(AO.UAdd64(LongOf(h2), LongOf(p.s2)), carry);
		U.IntToBufferLE(AO.SHORT64(f), tag, 8);
		carry := Shr(f, 32);
		f := AO.UAdd64(AO.UAdd64(LongOf(h3), LongOf(p.s3)), carry);
		U.IntToBufferLE(AO.SHORT64(f), tag, 12)
	END Final;

	PROCEDURE Poly1305* (IN key : ARRAY OF SHORTCHAR; IN msg : ARRAY OF SHORTCHAR;
			msgLen : INTEGER; VAR tag : ARRAY OF SHORTCHAR);
		VAR p : Context;
	BEGIN
		Init(p, key);
		Update(p, msg, 0, msgLen);
		Final(p, tag)
	END Poly1305;

	(* ---------------- AEAD ---------------- *)

	PROCEDURE Pad (VAR p : Context; n : INTEGER);
		VAR zeros : ARRAY 16 OF SHORTCHAR;
			i, k : INTEGER;
	BEGIN
		FOR i := 0 TO 15 DO zeros[i] := 0X END;
		k := n MOD 16;
		IF k # 0 THEN
			Update(p, zeros, 0, 16 - k)
		END
	END Pad;

	PROCEDURE MakeTag (IN key : ARRAY OF SHORTCHAR; IN nonce : ARRAY OF SHORTCHAR;
			IN aad : ARRAY OF SHORTCHAR; aadLen : INTEGER;
			IN cipher : ARRAY OF SHORTCHAR; cipherLen : INTEGER;
			VAR tag : ARRAY OF SHORTCHAR);
		VAR
			p : Context;
			b0 : ARRAY BlockSize OF SHORTCHAR;
			lens : ARRAY 16 OF SHORTCHAR;
	BEGIN
		Block(key, nonce, 0, b0);
		Init(p, b0);
		Update(p, aad, 0, aadLen);
		Pad(p, aadLen);
		Update(p, cipher, 0, cipherLen);
		Pad(p, cipherLen);
		U.IntToBufferLE(aadLen, lens, 0);
		U.IntToBufferLE(0, lens, 4);
		U.IntToBufferLE(cipherLen, lens, 8);
		U.IntToBufferLE(0, lens, 12);
		Update(p, lens, 0, 16);
		Final(p, tag)
	END MakeTag;

	PROCEDURE Encrypt* (IN key : ARRAY OF SHORTCHAR; IN nonce : ARRAY OF SHORTCHAR;
			IN aad : ARRAY OF SHORTCHAR; aadLen : INTEGER;
			IN plain : ARRAY OF SHORTCHAR; plainLen : INTEGER;
			VAR cipher : ARRAY OF SHORTCHAR; VAR tag : ARRAY OF SHORTCHAR);
	BEGIN
		StreamXor(key, nonce, 1, plain, plainLen, cipher);
		MakeTag(key, nonce, aad, aadLen, cipher, plainLen, tag)
	END Encrypt;

	PROCEDURE Decrypt* (IN key : ARRAY OF SHORTCHAR; IN nonce : ARRAY OF SHORTCHAR;
			IN aad : ARRAY OF SHORTCHAR; aadLen : INTEGER;
			IN cipher : ARRAY OF SHORTCHAR; cipherLen : INTEGER;
			IN tag : ARRAY OF SHORTCHAR;
			VAR plain : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR
			want : ARRAY TagSize OF SHORTCHAR;
			i : INTEGER;
	BEGIN
		MakeTag(key, nonce, aad, aadLen, cipher, cipherLen, want);
		IF ~U.EqualBytesCT(want, tag, 0, 0, TagSize) THEN
			FOR i := 0 TO cipherLen - 1 DO plain[i] := 0X END;
			RETURN FALSE
		END;
		StreamXor(key, nonce, 1, cipher, cipherLen, plain);
		RETURN TRUE
	END Decrypt;

BEGIN
	sigma[0] := BITS(61707865H);
	sigma[1] := BITS(03320646EH);
	sigma[2] := BITS(079622D32H);
	sigma[3] := BITS(06B206574H)
END CryptoChaCha20.
