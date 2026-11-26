MODULE Crypto_devECDSA;
(** Тесты ECDSA: векторы RFC 6979 (детерминированный k), проверка и отказы.

	Подпись на P-256/SHA-256 и P-384/SHA-384 сверяется с r и s из RFC 6979
	для сообщений sample и test. Проверяются совпадение при повторной
	подписи, отказ при подделке r, s, хеша и чужом открытом ключе. *)

	IMPORT CryptoECDSA, CryptoEC, CryptoHashes, CryptoUtils, UnitAssert;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE HexTo (IN hex: ARRAY OF SHORTCHAR; VAR buf: ARRAY OF SHORTCHAR): INTEGER;
		VAR len: INTEGER;
	BEGIN
		len := StrLen(hex) DIV 2;
		CryptoUtils.Hex2Bin(hex, 0, buf, 0, len);
		RETURN len
	END HexTo;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE Hash (IN modname, msg: ARRAY OF SHORTCHAR; VAR out: ARRAY OF SHORTCHAR): INTEGER;
		VAR h: CryptoHashes.Hash;
	BEGIN
		h := CryptoHashes.NewHash(modname);
		UnitAssert.True(h # NIL, "hash for ECDSA is available");
		h.Initialize;
		h.Update(msg, 0, StrLen(msg));
		h.GetHash(out, 0);
		RETURN h.size
	END Hash;

	PROCEDURE CheckOrder (c: CryptoEC.Curve; IN v: CryptoEC.BigInt;
			IN hex: ARRAY OF SHORTCHAR; IN msg: ARRAY OF CHAR);
		VAR got: ARRAY 64 OF SHORTCHAR; want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		CryptoEC.OrderBytes(c, v, got, 0);
		CryptoUtils.Hex2Bin(hex, 0, want, 0, 4 * c.n);
		UnitAssert.True(Equal(got, want, 4 * c.n), msg)
	END CheckOrder;

	PROCEDURE TestP256Sample*;
		VAR c: CryptoEC.Curve; priv, qx, qy, r, s: CryptoEC.BigInt;
			buf, h, rh, sh: ARRAY 64 OF SHORTCHAR;
			len, hlen: INTEGER; ok: BOOLEAN;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "P-256 RFC private parsed");
		hlen := Hash("CryptoSHA256", "sample", h);
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, r, s), "RFC 6979 P-256 sample signs");
		CheckOrder(c, r, "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716",
			"RFC 6979 P-256 sample r");
		CheckOrder(c, s, "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8",
			"RFC 6979 P-256 sample s");
		CryptoEC.ScalarMulBase(c, priv, qx, qy, ok);
		UnitAssert.True(ok, "P-256 public from RFC private");
		UnitAssert.True(CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "RFC 6979 P-256 sample verifies");
		(* Подпись детерминирована. *)
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, qx, qy), "P-256 signs again");
		CryptoEC.OrderBytes(c, qx, rh, 0);
		CryptoEC.OrderBytes(c, r, sh, 0);
		UnitAssert.True(Equal(rh, sh, 32), "P-256 deterministic r");
		(* Испорченная подпись и чужой хеш отвергаются. *)
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, qy, s), "P-256 rejects tampered r");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, qx), "P-256 rejects tampered s")
	END TestP256Sample;

	PROCEDURE TestP256Test*;
		VAR c: CryptoEC.Curve; priv, r, s: CryptoEC.BigInt;
			buf, h: ARRAY 64 OF SHORTCHAR; len, hlen: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "ECDSA private key parsed");
		hlen := Hash("CryptoSHA256", "test", h);
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, r, s), "RFC 6979 P-256 test signs");
		CheckOrder(c, r, "F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367",
			"RFC 6979 P-256 test r");
		CheckOrder(c, s, "019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083",
			"RFC 6979 P-256 test s")
	END TestP256Test;

	PROCEDURE TestP384Sample*;
		VAR c: CryptoEC.Curve; priv, qx, qy, r, s: CryptoEC.BigInt;
			buf, h: ARRAY 64 OF SHORTCHAR;
			len, hlen: INTEGER; ok: BOOLEAN;
	BEGIN
		c := CryptoEC.P384();
		len := HexTo("6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D8" +
			"96D5724E4C70A825F872C9EA60D2EDF5", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "P-384 RFC private parsed");
		hlen := Hash("CryptoSHA384", "sample", h);
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, r, s), "RFC 6979 P-384 sample signs");
		CheckOrder(c, r, "94EDBB92A5ECB8AAD4736E56C691916B3F88140666CE9FA73D64C4EA95AD133C" +
			"81A648152E44ACF96E36DD1E80FABE46", "RFC 6979 P-384 sample r");
		CheckOrder(c, s, "99EF4AEB15F178CEA1FE40DB2603138F130E740A19624526203B6351D0A3A94F" +
			"A329C145786E679E7B82C71A38628AC8", "RFC 6979 P-384 sample s");
		CryptoEC.ScalarMulBase(c, priv, qx, qy, ok);
		UnitAssert.True(ok, "P-384 public from RFC private");
		UnitAssert.True(CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "RFC 6979 P-384 sample verifies")
	END TestP384Sample;

	PROCEDURE TestP384Test*;
		VAR c: CryptoEC.Curve; priv, r, s: CryptoEC.BigInt;
			buf, h: ARRAY 64 OF SHORTCHAR; len, hlen: INTEGER;
	BEGIN
		c := CryptoEC.P384();
		len := HexTo("6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D8" +
			"96D5724E4C70A825F872C9EA60D2EDF5", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "ECDSA private key parsed");
		hlen := Hash("CryptoSHA384", "test", h);
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, r, s), "RFC 6979 P-384 test signs");
		CheckOrder(c, r, "8203B63D3C853E8D77227FB377BCF7B7B772E97892A80F36AB775D509D7A5FEB" +
			"0542A7F0812998DA8F1DD3CA3CF023DB", "RFC 6979 P-384 test r");
		CheckOrder(c, s, "DDD0760448D42D8A43AF45AF836FCE4DE8BE06B485E9B61B827C2F13173923E0" +
			"6A739F040649A667BF3B828246BAA5A5", "RFC 6979 P-384 test s")
	END TestP384Test;

	PROCEDURE TestReject*;
		VAR c: CryptoEC.Curve; priv, qx, qy, r, s, z: CryptoEC.BigInt;
			buf, h: ARRAY 64 OF SHORTCHAR; len, hlen: INTEGER; ok: BOOLEAN;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "ECDSA private key parsed");
		hlen := Hash("CryptoSHA256", "sample", h);
		UnitAssert.True(CryptoECDSA.Sign(c, priv, h, hlen, r, s), "P-256 reference signature");
		CryptoEC.ScalarMulBase(c, priv, qx, qy, ok);
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, c.order),
			"P-256 rejects s = order");
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000000", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, z);
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, z, s),
			"P-256 rejects r = 0");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, z),
			"P-256 rejects s = 0");
		h[0] := SHORT(CHR(ORD(h[0]) + 1));
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s),
			"P-256 rejects changed hash");
		UnitAssert.True(~CryptoECDSA.Sign(c, c.order, h, hlen, r, s),
			"P-256 rejects private key = order");
		UnitAssert.True(~CryptoECDSA.Sign(c, priv, h, 20, r, s),
			"P-256 rejects short hash")
	END TestReject;

	PROCEDURE HashBytes (IN modname: ARRAY OF SHORTCHAR; IN data: ARRAY OF SHORTCHAR; len: INTEGER;
			VAR out: ARRAY OF SHORTCHAR): INTEGER;
		VAR h: CryptoHashes.Hash;
	BEGIN
		h := CryptoHashes.NewHash(modname);
		h.Initialize;
		h.Update(data, 0, len);
		h.GetHash(out, 0);
		RETURN h.size
	END HashBytes;

	PROCEDURE ParsePoint (c: CryptoEC.Curve; IN qxHex, qyHex: ARRAY OF SHORTCHAR;
			VAR qx, qy: CryptoEC.BigInt);
		VAR buf: ARRAY 64 OF SHORTCHAR; len: INTEGER;
	BEGIN
		len := HexTo(qxHex, buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, qx), "Wycheproof qx parsed");
		len := HexTo(qyHex, buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, qy), "Wycheproof qy parsed")
	END ParsePoint;

	PROCEDURE TestWycheproof*;
		(** ECDSA P-256: выбранные случаи Wycheproof (ecdsa_secp256r1_sha256).
			Подпись проверяется по хешу SHA-256 от msg. *)
		VAR c: CryptoEC.Curve; qx, qy, r, s: CryptoEC.BigInt;
			msg, h: ARRAY 64 OF SHORTCHAR; len, hlen: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		ParsePoint(c, "2927B10512BAE3EDDCFE467828128BAD2903269919F7086069C8C4DF6C732838",
			"C7787964EAAC00E5921FB1498A60F4606766B3D9685001558D1A974E7341513E", qx, qy);
		len := HexTo("313233343030", msg);
		hlen := HashBytes("CryptoSHA256", msg, len, h);
		len := HexTo("2BA3A8BE6B94D5EC80A6D9D1190A436EFFE50D85A1EEE859B8CC6AF9BD5C2E18", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, r), "Wycheproof r parsed");
		len := HexTo("4CD60B855D442F5B3C7B11EB6C4E0AE7525FE710FAB9AA7C77A67F79E6FADD76", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, s), "Wycheproof s parsed");
		UnitAssert.True(CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s),
			"Wycheproof valid signature accepted");

		(* r = 0 *)
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000000", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, r), "Wycheproof value parsed");
		len := HexTo("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, s), "Wycheproof value parsed");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "Wycheproof rejects r = 0");

		(* r = 1, s = n *)
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000001", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, r), "Wycheproof value parsed");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "Wycheproof rejects s = n");

		(* r = 1, s = n - 1 *)
		len := HexTo("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632550", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, s), "Wycheproof value parsed");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "Wycheproof rejects r = 1, s = n - 1");

		(* Другой ключ: r = p (больше порядка). *)
		ParsePoint(c, "0AD99500288D466940031D72A9F5445A4D43784640855BF0A69874D2DE5FE103",
			"C5011E6EF2C42DCD50D5D3D29F99AE6EBA2C80C9244F4C5422F0979FF0C3BA5E", qx, qy);
		len := HexTo("FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, r), "Wycheproof value parsed");
		len := HexTo("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC63254E", msg);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, msg, 0, len, s), "Wycheproof value parsed");
		UnitAssert.True(~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s), "Wycheproof rejects r too large")
	END TestWycheproof;

END Crypto_devECDSA.
