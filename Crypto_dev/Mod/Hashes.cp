MODULE Crypto_devHashes;
(** Тесты хешей Crypto: MD5, SHA1, SHA256, SHA3 по эталонным векторам.

	Каждый вектор сверяется побайтно с ожидаемым значением; провал —
	проверка UnitAssert. *)

	IMPORT CryptoHashes, CryptoUtils, UnitAssert;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE Check (IN modname, data, hex: ARRAY OF SHORTCHAR; IN msg: ARRAY OF CHAR);
		VAR h: CryptoHashes.Hash; got, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		h := CryptoHashes.NewHash(modname);
		h.Initialize;
		h.Update(data, 0, StrLen(data));
		h.GetHash(got, 0);
		CryptoUtils.Hex2Bin(hex, 0, want, 0, h.size);
		UnitAssert.True(Equal(got, want, h.size), msg)
	END Check;

	PROCEDURE TestMD5*;
	BEGIN
		Check("CryptoMD5", "", "D41D8CD98F00B204E9800998ECF8427E", "MD5 empty");
		Check("CryptoMD5", "abc", "900150983CD24FB0D6963F7D28E17F72", "MD5 abc");
		Check("CryptoMD5", "abcdefghijklmnopqrstuvwxyz",
			"C3FCD3D76192E4007DFB496CCA67E13B", "MD5 alphabet")
	END TestMD5;

	PROCEDURE TestSHA1*;
	BEGIN
		Check("CryptoSHA1", "", "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709", "SHA1 empty");
		Check("CryptoSHA1", "abc", "A9993E364706816ABA3E25717850C26C9CD0D89D", "SHA1 abc");
		Check("CryptoSHA1", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
			"84983E441C3BD26EBAAE4AA1F95129E5E54670F1", "SHA1 two blocks")
	END TestSHA1;

	PROCEDURE TestSHA256*;
	BEGIN
		Check("CryptoSHA256", "abc",
			"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "SHA256 abc");
		Check("CryptoSHA256", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
			"248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1",
			"SHA256 two blocks")
	END TestSHA256;

	PROCEDURE TestSHA3*;
	BEGIN
		Check("CryptoSHA3", "",
			"a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", "SHA3 empty");
		Check("CryptoSHA3", "abc",
			"3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", "SHA3 abc");
		Check("CryptoSHA3", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
			"41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376",
			"SHA3 two blocks")
	END TestSHA3;

END Crypto_devHashes.
