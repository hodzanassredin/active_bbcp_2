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
		Fill(key, 20, 0BX);
		DoTest("CryptoSHA256", "Hi There", key,
			"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
			8, 20, 32, "HMAC-SHA256 RFC 4231 case 1");
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

	PROCEDURE TestRFC4231SHA384*;
		VAR key, text: ARRAY 128 OF SHORTCHAR;
	BEGIN
		Fill(key, 20, 0BX);
		DoTest("CryptoSHA384", "Hi There", key,
			"AFD03944D84895626B0825F4AB46907F15F9DADBE4101EC682AA034C7CEBC59C" +
			"FAEA9EA9076EDE7F4AF152E8B2FA9CB6", 8, 20, 48, "HMAC-SHA384 RFC 4231 case 1");
		DoTest("CryptoSHA384", "what do ya want for nothing?", "Jefe",
			"AF45D2E376484031617F78D2B58A6B1B9C7EF464F5A01B47E42EC3736322445E" +
			"8E2240CA5E69E2C78B3239ECFAB21649", 28, 4, 48, "HMAC-SHA384 RFC 4231 case 2");
		Fill(key, 20, 0AAX);
		Fill(text, 50, 0DDX);
		DoTest("CryptoSHA384", text, key,
			"88062608D3E6AD8A0AA2ACE014C8A86F0AA635D947AC9FEBE83EF4E55966144B" +
			"2A5AB39DC13814B94E3AB6E101A34F27", 50, 20, 48, "HMAC-SHA384 RFC 4231 case 3");
		Fill(key, 20, 0CX);
		DoTest("CryptoSHA384", "Test With Truncation", key,
			"3ABF34C3503B2A23A46EFC619BAEF897F4C8E42C934CE55CCBAE9740FCBC1AF4" +
			"CA62269E2A37CD88BA926341EFE4AEEA", 20, 20, 48, "HMAC-SHA384 RFC 4231 case 5")
	END TestRFC4231SHA384;

	PROCEDURE TestRFC4231SHA512*;
		VAR key, text: ARRAY 128 OF SHORTCHAR;
	BEGIN
		Fill(key, 20, 0BX);
		DoTest("CryptoSHA512", "Hi There", key,
			"87AA7CDEA5EF619D4FF0B4241A1D6CB02379F4E2CE4EC2787AD0B30545E17CDE" +
			"DAA833B7D6B8A702038B274EAEA3F4E4BE9D914EEB61F1702E696C203A126854",
			8, 20, 64, "HMAC-SHA512 RFC 4231 case 1");
		DoTest("CryptoSHA512", "what do ya want for nothing?", "Jefe",
			"164B7A7BFCF819E2E395FBE73B56E0A387BD64222E831FD610270CD7EA250554" +
			"9758BF75C05A994A6D034F65F8F0E6FDCAEAB1A34D4A6B4B636E070A38BCE737",
			28, 4, 64, "HMAC-SHA512 RFC 4231 case 2");
		Fill(key, 20, 0AAX);
		Fill(text, 50, 0DDX);
		DoTest("CryptoSHA512", text, key,
			"FA73B0089D56A284EFB0F0756C890BE9B1B5DBDD8EE81A3655F83E33B2279D39" +
			"BF3E848279A722C806B485A47E67C807B946A337BEE8942674278859E13292FB",
			50, 20, 64, "HMAC-SHA512 RFC 4231 case 3");
		Fill(key, 20, 0CX);
		DoTest("CryptoSHA512", "Test With Truncation", key,
			"415FAD6271580A531D4179BC891D87A650188707922A4FBB36663A1EB16DA008" +
			"711C5B50DDD0FC235084EB9D3364A1454FB2EF67CD1D29FE6773068EA266E96B",
			20, 20, 64, "HMAC-SHA512 RFC 4231 case 5")
	END TestRFC4231SHA512;

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
