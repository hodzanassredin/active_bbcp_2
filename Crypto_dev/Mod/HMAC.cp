MODULE Crypto_devHMAC;
(** Тесты HMAC по векторам RFC 2104.

	Проверяются MD5, SHA1, SHA256, укороченный MAC и совпадение результата
	при одном и нескольких вызовах Update. *)

	IMPORT CryptoHMAC, CryptoUtils, UnitAssert;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE DoTest (IN modname, data, key, hexDigest: ARRAY OF SHORTCHAR;
		dataLen, keyLen, hashLen: INTEGER; IN msg: ARRAY OF CHAR);
		VAR hmac: CryptoHMAC.HMac; got, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		NEW(hmac);
		hmac.Init(modname);
		IF hashLen < hmac.size THEN hmac.ShrinkLength(hashLen) END;
		hmac.Initialize(key, keyLen);
		hmac.Update(data, 0, dataLen);
		hmac.GetMac(got, 0);
		CryptoUtils.Hex2Bin(hexDigest, 0, want, 0, hashLen);
		UnitAssert.True(Equal(got, want, hashLen), msg)
	END DoTest;

	PROCEDURE Fill (VAR a: ARRAY OF SHORTCHAR; len: INTEGER; val: SHORTCHAR);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO len - 1 DO a[i] := val END
	END Fill;

	PROCEDURE Test1MD5*;
		VAR key: ARRAY 64 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", 0, key, 0, 16);
		DoTest("CryptoMD5", "Hi There", key, "9294727a3638bb1c13f48ef8158bfc9d",
			8, 16, 16, "HMAC-MD5 RFC vector 1");
		DoTest("CryptoMD5", "what do ya want for nothing?", "Jefe",
			"750c783e6ab0b503eaa86e310a5db738", 28, 4, 16, "HMAC-MD5 RFC vector 2");
		DoTest("CryptoMD5", "what do ya want for nothing?", "Jefe",
			"750c783e6ab0b503eaa86e310a5db738", 28, 4, 12, "HMAC-MD5 shortened")
	END Test1MD5;

	PROCEDURE Test1SHA1*;
		VAR key: ARRAY 64 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", 0, key, 0, 20);
		DoTest("CryptoSHA1", "Hi There", key,
			"b617318655057264e28bc0b6fb378c8ef146be00", 8, 20, 20, "HMAC-SHA1 RFC vector 1");
		DoTest("CryptoSHA1", "Hi There", key,
			"b617318655057264e28bc0b6fb378c8ef146be00", 8, 20, 12, "HMAC-SHA1 shortened")
	END Test1SHA1;

	PROCEDURE Test1SHA256*;
		VAR key, text: ARRAY 128 OF SHORTCHAR;
	BEGIN
		DoTest("CryptoSHA256", "The quick brown fox jumps over the lazy dog", "key",
			"f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
			43, 3, 32, "HMAC-SHA256 RFC vector 1");
		Fill(text, 50, 0CDX);
		CryptoUtils.Hex2Bin("0102030405060708090a0b0c0d0e0f10111213141516171819", 0, key, 0, 25);
		DoTest("CryptoSHA256", text, key,
			"82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
			50, 25, 32, "HMAC-SHA256 RFC vector 2");
		DoTest("CryptoSHA256", text, key,
			"82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
			50, 25, 12, "HMAC-SHA256 shortened")
	END Test1SHA256;

	PROCEDURE TestConcat*;
		VAR hmac: CryptoHMAC.HMac; key, data, once, split: ARRAY 64 OF SHORTCHAR;
	BEGIN
		NEW(hmac);
		hmac.Init("CryptoMD5");
		key := "abcdefghijklmnop";
		data := "hey mister music";
		hmac.Initialize(key, 16);
		hmac.Update(data, 0, 16);
		hmac.GetMac(once, 0);
		hmac.Initialize(key, 16);
		hmac.Update(data, 0, 4);
		hmac.Update(data, 4, 6);
		hmac.Update(data, 10, 6);
		hmac.GetMac(split, 0);
		UnitAssert.True(Equal(once, split, 16), "HMAC one Update equals three Updates")
	END TestConcat;

END Crypto_devHMAC.
