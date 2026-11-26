MODULE CryptoSHA512;
(** AUTHOR "GF"; PURPOSE "SHA2-512 and SHA2-384"; *)

	IMPORT Hashes := CryptoHashes, U := CryptoUtils, AO := CryptoAosCompat;

	CONST
		BlockSize* = 128;
		HashSize512* = 64;
		HashSize384* = 48;

		Variant512 = 0;
		Variant384 = 1;

	TYPE
		State = RECORD
			hash : ARRAY 8 OF AO.SIGNED64;
			bitLenLo, bitLenHi : AO.SIGNED64;
			cdata : ARRAY BlockSize OF SHORTCHAR;
			cn : AO.SIZE;
			outSize : AO.SIZE;
			variant : AO.SIGNED32
		END;

		Hash* = POINTER TO EXTENSIBLE RECORD(Hashes.Hash)
			state : State
		END;

	VAR
		K512 : ARRAY 80 OF AO.SIGNED64;

	PROCEDURE Read64 (IN buf : ARRAY OF SHORTCHAR; pos : AO.SIZE): AO.SIGNED64;
		VAR hi, lo : AO.SIGNED32;
	BEGIN
		hi := U.IntFromBufferBE(buf, pos);
		lo := U.IntFromBufferBE(buf, pos + 4);
		RETURN AO.ULSH64(AO.LONG32(hi), 32) + AO.LONG32(lo)
	END Read64;

	PROCEDURE Write64 (v : AO.SIGNED64; VAR buf : ARRAY OF SHORTCHAR; pos : AO.SIZE);
	BEGIN
		U.IntToBufferBE(AO.SHORTHIGH64Raw(v), buf, pos);
		U.IntToBufferBE(AO.SHORT64Raw(v), buf, pos + 4)
	END Write64;

	PROCEDURE ROTR (x : AO.SIGNED64; n : INTEGER): AO.SIGNED64;
	BEGIN
		RETURN AO.URotR64(x, n)
	END ROTR;

	PROCEDURE XOR (x, y : AO.SIGNED64): AO.SIGNED64;
	BEGIN
		RETURN AO.UBitXor64(x, y)
	END XOR;

	PROCEDURE AND (x, y : AO.SIGNED64): AO.SIGNED64;
	BEGIN
		RETURN AO.UBitwiseAnd64(x, y)
	END AND;

	(* SHA-2 работает с 64-битными словами по модулю 2^64. *)
	PROCEDURE ADD (x, y : AO.SIGNED64): AO.SIGNED64;
	BEGIN
		RETURN AO.UAdd64(x, y)
	END ADD;

	(* Старшие и младшие 32 бита 64-битного слова: компилятор не
	   принимает 64-битные литералы, поэтому собираем из половин. *)
	PROCEDURE Make64 (hi, lo : AO.SIGNED32): AO.SIGNED64;
	BEGIN
		RETURN AO.CDQ(lo, hi)
	END Make64;

	PROCEDURE (VAR SELF: State) Initialize , NEW;
	BEGIN
		IF SELF.variant = Variant384 THEN
			SELF.hash[0] := Make64(0CBBB9D5DH, 0C1059ED8H);
			SELF.hash[1] := Make64(0629A292AH, 0367CD507H);
			SELF.hash[2] := Make64(09159015AH, 03070DD17H);
			SELF.hash[3] := Make64(0152FECD8H, 0F70E5939H);
			SELF.hash[4] := Make64(067332667H, 0FFC00B31H);
			SELF.hash[5] := Make64(08EB44A87H, 068581511H);
			SELF.hash[6] := Make64(0DB0C2E0DH, 064F98FA7H);
			SELF.hash[7] := Make64(047B5481DH, 0BEFA4FA4H)
		ELSE
			SELF.hash[0] := Make64(06A09E667H, 0F3BCC908H);
			SELF.hash[1] := Make64(0BB67AE85H, 084CAA73BH);
			SELF.hash[2] := Make64(03C6EF372H, 0FE94F82BH);
			SELF.hash[3] := Make64(0A54FF53AH, 05F1D36F1H);
			SELF.hash[4] := Make64(0510E527FH, 0ADE682D1H);
			SELF.hash[5] := Make64(09B05688CH, 02B3E6C1FH);
			SELF.hash[6] := Make64(01F83D9ABH, 0FB41BD6BH);
			SELF.hash[7] := Make64(05BE0CD19H, 0137E2179H)
		END;
		SELF.bitLenLo := 0;
		SELF.bitLenHi := 0;
		SELF.cn := 0
	END Initialize;

	PROCEDURE (VAR SELF: State) HashBlock (IN data : ARRAY OF SHORTCHAR; pos : AO.SIZE), NEW;
		VAR
			W : ARRAY 80 OF AO.SIGNED64;
			a, b, c, d, e, f, g, h : AO.SIGNED64;
			t1, t2, s0, s1, ch, maj : AO.SIGNED64;
			i : AO.SIZE;
	BEGIN
		FOR i := 0 TO 15 DO
			W[i] := Read64(data, pos + 8 * i)
		END;
		FOR i := 16 TO 79 DO
			s0 := XOR(XOR(ROTR(W[i - 15], 1), ROTR(W[i - 15], 8)), AO.ULSH64(W[i - 15], -7));
			s1 := XOR(XOR(ROTR(W[i - 2], 19), ROTR(W[i - 2], 61)), AO.ULSH64(W[i - 2], -6));
			W[i] := ADD(ADD(W[i - 16], s0), ADD(W[i - 7], s1))
		END;

		a := SELF.hash[0];
		b := SELF.hash[1];
		c := SELF.hash[2];
		d := SELF.hash[3];
		e := SELF.hash[4];
		f := SELF.hash[5];
		g := SELF.hash[6];
		h := SELF.hash[7];

		FOR i := 0 TO 79 DO
			s1 := XOR(XOR(ROTR(e, 14), ROTR(e, 18)), ROTR(e, 41));
			(* Ch(e,f,g) = (e & f) xor (g & ~e); g & ~e = g xor (g & e). *)
			ch := XOR(AND(e, f), XOR(g, AND(g, e)));
			t1 := ADD(ADD(ADD(h, s1), ADD(ch, K512[i])), W[i]);
			s0 := XOR(XOR(ROTR(a, 28), ROTR(a, 34)), ROTR(a, 39));
			maj := XOR(XOR(AND(a, b), AND(a, c)), AND(b, c));
			t2 := ADD(s0, maj);
			h := g;
			g := f;
			f := e;
			e := ADD(d, t1);
			d := c;
			c := b;
			b := a;
			a := ADD(t1, t2)
		END;

		SELF.hash[0] := ADD(SELF.hash[0], a);
		SELF.hash[1] := ADD(SELF.hash[1], b);
		SELF.hash[2] := ADD(SELF.hash[2], c);
		SELF.hash[3] := ADD(SELF.hash[3], d);
		SELF.hash[4] := ADD(SELF.hash[4], e);
		SELF.hash[5] := ADD(SELF.hash[5], f);
		SELF.hash[6] := ADD(SELF.hash[6], g);
		SELF.hash[7] := ADD(SELF.hash[7], h);
		SELF.cn := 0
	END HashBlock;

	PROCEDURE (VAR SELF: State) Update (IN data : ARRAY OF SHORTCHAR; pos, len : AO.SIZE), NEW;
		VAR
			i, n : AO.SIZE;
			added, old : AO.SIGNED64;
	BEGIN
		added := AO.LONG32(len) * 8;
		old := SELF.bitLenLo;
		SELF.bitLenLo := ADD(SELF.bitLenLo, added);
		IF AO.ULss64(SELF.bitLenLo, old) THEN
			INC(SELF.bitLenHi)
		END;

		IF SELF.cn > 0 THEN
			IF SELF.cn + len < BlockSize THEN
				i := SELF.cn;
				INC(SELF.cn, len);
				WHILE i < SELF.cn DO
					SELF.cdata[i] := data[pos];
					INC(i);
					INC(pos)
				END;
				RETURN
			ELSE
				WHILE SELF.cn < BlockSize DO
					SELF.cdata[SELF.cn] := data[pos];
					INC(SELF.cn);
					INC(pos);
					DEC(len)
				END;
				SELF.HashBlock(SELF.cdata, 0)
			END
		END;

		n := 0;
		WHILE n < len DIV BlockSize DO
			SELF.HashBlock(data, pos);
			INC(n);
			INC(pos, BlockSize)
		END;
		len := len MOD BlockSize;
		WHILE SELF.cn < len DO
			SELF.cdata[SELF.cn] := data[pos];
			INC(SELF.cn);
			INC(pos)
		END
	END Update;

	PROCEDURE (VAR SELF: State) GetHash (VAR buf : ARRAY OF SHORTCHAR; pos : AO.SIZE), NEW;
		VAR
			p, i : AO.SIZE;
	BEGIN
		SELF.cdata[SELF.cn] := 80X;
		INC(SELF.cn);
		IF SELF.cn > BlockSize - 16 THEN
			WHILE SELF.cn < BlockSize DO
				SELF.cdata[SELF.cn] := 0X;
				INC(SELF.cn)
			END;
			SELF.HashBlock(SELF.cdata, 0)
		END;
		p := BlockSize - 16;
		WHILE SELF.cn < p DO
			SELF.cdata[SELF.cn] := 0X;
			INC(SELF.cn)
		END;
		Write64(SELF.bitLenHi, SELF.cdata, p);
		Write64(SELF.bitLenLo, SELF.cdata, p + 8);
		SELF.HashBlock(SELF.cdata, 0);
		i := 0;
		WHILE i * 8 < SELF.outSize DO
			Write64(SELF.hash[i], buf, pos + 8 * i);
			INC(i)
		END
	END GetHash;

	PROCEDURE (SELF: Hash) Init* ;
	BEGIN
		SELF.state.outSize := HashSize512;
		SELF.state.variant := Variant512;
		SELF.SetNameAndSize("sha2-512", HashSize512);
		SELF.blockSize := BlockSize;
		SELF.initialized := FALSE
	END Init;

	PROCEDURE (SELF: Hash) Initialize* ;
	BEGIN
		SELF.state.Initialize;
		SELF.initialized := TRUE
	END Initialize;

	PROCEDURE (SELF: Hash) Update* (IN data : ARRAY OF SHORTCHAR; pos, len : AO.SIZE);
	BEGIN
		ASSERT(SELF.initialized);
		SELF.state.Update(data, pos, len)
	END Update;

	PROCEDURE (SELF: Hash) GetHash* (VAR buf : ARRAY OF SHORTCHAR; pos : AO.SIZE);
	BEGIN
		SELF.state.GetHash(buf, pos)
	END GetHash;

	PROCEDURE NewHash* (): Hashes.Hash;
		VAR h: Hash;
	BEGIN
		NEW(h);h.Init();
		RETURN h
	END NewHash;

	(* Фабрика для CryptoSHA384: тот же код с другими начальными
	   значениями и длиной результата. *)
	PROCEDURE NewHash384* (): Hashes.Hash;
		VAR h: Hash;
	BEGIN
		NEW(h);h.Init();
		h.state.outSize := HashSize384;
		h.state.variant := Variant384;
		h.SetNameAndSize("sha2-384", HashSize384);
		RETURN h
	END NewHash384;

	PROCEDURE InitializeK ;
		VAR buf: U.InitBuffer; i: AO.SIGNED32; lo, hi: AO.SIGNED32;
	BEGIN
		NEW(buf); buf.Init(4096);
		buf.Add("428A2F98 D728AE22 71374491 23EF65CD B5C0FBCF EC4D3B2F E9B5DBA5 8189DBBC");
		buf.Add("3956C25B F348B538 59F111F1 B605D019 923F82A4 AF194F9B AB1C5ED5 DA6D8118");
		buf.Add("D807AA98 A3030242 12835B01 45706FBE 243185BE 4EE4B28C 550C7DC3 D5FFB4E2");
		buf.Add("72BE5D74 F27B896F 80DEB1FE 3B1696B1 9BDC06A7 25C71235 C19BF174 CF692694");
		buf.Add("E49B69C1 9EF14AD2 EFBE4786 384F25E3 0FC19DC6 8B8CD5B5 240CA1CC 77AC9C65");
		buf.Add("2DE92C6F 592B0275 4A7484AA 6EA6E483 5CB0A9DC BD41FBD4 76F988DA 831153B5");
		buf.Add("983E5152 EE66DFAB A831C66D 2DB43210 B00327C8 98FB213F BF597FC7 BEEF0EE4");
		buf.Add("C6E00BF3 3DA88FC2 D5A79147 930AA725 06CA6351 E003826F 14292967 0A0E6E70");
		buf.Add("27B70A85 46D22FFC 2E1B2138 5C26C926 4D2C6DFC 5AC42AED 53380D13 9D95B3DF");
		buf.Add("650A7354 8BAF63DE 766A0ABB 3C77B2A8 81C2C92E 47EDAEE6 92722C85 1482353B");
		buf.Add("A2BFE8A1 4CF10364 A81A664B BC423001 C24B8B70 D0F89791 C76C51A3 0654BE30");
		buf.Add("D192E819 D6EF5218 D6990624 5565A910 F40E3585 5771202A 106AA070 32BBD1B8");
		buf.Add("19A4C116 B8D2D0C8 1E376C08 5141AB53 2748774C DF8EEB99 34B0BCB5 E19B48A8");
		buf.Add("391C0CB3 C5C95A63 4ED8AA4A E3418ACB 5B9CCA4F 7763E373 682E6FF3 D6B2B8A3");
		buf.Add("748F82EE 5DEFB2FC 78A5636F 43172F60 84C87814 A1F0AB72 8CC70208 1A6439EC");
		buf.Add("90BEFFFA 23631E28 A4506CEB DE82BDE9 BEF9A3F7 B2C67915 C67178F2 E372532B");
		buf.Add("CA273ECE EA26619C D186B8C7 21C0C207 EADA7DD6 CDE0EB1E F57D4F7F EE6ED178");
		buf.Add("06F067AA 72176FBA 0A637DC5 A2C898A6 113F9804 BEF90DAE 1B710B35 131C471B");
		buf.Add("28DB77F5 23047D84 32CAAB7B 40C72493 3C9EBE0A 15C9BEBC 431D67C4 9C100D4C");
		buf.Add("4CC5D4BE CB3E42B6 597F299C FC657E2A 5FCB6FAB 3AD6FAEC 6C44198C 4A475817");
		FOR i := 0 TO 79 DO
			hi := buf.GetInt();
			lo := buf.GetInt();
			K512[i] := AO.CDQ(lo, hi)
		END
	END InitializeK;

BEGIN
	InitializeK
END CryptoSHA512.
