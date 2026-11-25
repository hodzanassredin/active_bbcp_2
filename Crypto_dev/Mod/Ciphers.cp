MODULE Crypto_devCiphers;
(** Тесты шифров по эталонным векторам.

	AES/DES/3DES/ARC4/CAST/IDEA: ECB, CBC, CTR. Для каждого вектора
	проверяются шифрование и обратное дешифрование. *)

	IMPORT CryptoCiphers, CryptoUtils, UnitAssert;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE Ecb (IN modname, keyHex, inputHex, outputHex: ARRAY OF SHORTCHAR;
		keybits, datalen: INTEGER; IN msg: ARRAY OF CHAR);
		VAR c: CryptoCiphers.Cipher; key, buf, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		c := CryptoCiphers.NewCipher(modname);
		CryptoUtils.Hex2Bin(keyHex, 0, key, 0, keybits DIV 8);
		CryptoUtils.Hex2Bin(inputHex, 0, buf, 0, datalen);
		CryptoUtils.Hex2Bin(outputHex, 0, want, 0, datalen);
		c.InitKey(key, keybits);
		c.Encrypt(buf, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg);
		c.InitKey(key, keybits);
		c.Decrypt(buf, 0, datalen);
		CryptoUtils.Hex2Bin(inputHex, 0, want, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg)
	END Ecb;

	PROCEDURE Cbc (IN modname, keyHex, ivHex, inputHex, outputHex: ARRAY OF SHORTCHAR;
		keybits, datalen: INTEGER; IN msg: ARRAY OF CHAR);
		VAR c: CryptoCiphers.Cipher; key, iv, buf, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		c := CryptoCiphers.NewCipher(modname);
		CryptoUtils.Hex2Bin(keyHex, 0, key, 0, keybits DIV 8);
		CryptoUtils.Hex2Bin(ivHex, 0, iv, 0, c.blockSize);
		CryptoUtils.Hex2Bin(inputHex, 0, buf, 0, datalen);
		CryptoUtils.Hex2Bin(outputHex, 0, want, 0, datalen);
		c.InitKey(key, keybits);
		c.SetIV(iv, CryptoCiphers.CBC);
		c.Encrypt(buf, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg);
		c.InitKey(key, keybits);
		c.SetIV(iv, CryptoCiphers.CBC);
		c.Decrypt(buf, 0, datalen);
		CryptoUtils.Hex2Bin(inputHex, 0, want, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg)
	END Cbc;

	PROCEDURE Ctr (IN modname, keyHex, ivHex, inputHex, outputHex: ARRAY OF SHORTCHAR;
		keybits, datalen: INTEGER; IN msg: ARRAY OF CHAR);
		VAR c: CryptoCiphers.Cipher; key, iv, buf, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		c := CryptoCiphers.NewCipher(modname);
		CryptoUtils.Hex2Bin(keyHex, 0, key, 0, keybits DIV 8);
		CryptoUtils.Hex2Bin(ivHex, 0, iv, 0, c.blockSize);
		CryptoUtils.Hex2Bin(inputHex, 0, buf, 0, datalen);
		CryptoUtils.Hex2Bin(outputHex, 0, want, 0, datalen);
		c.InitKey(key, keybits);
		c.SetIV(iv, CryptoCiphers.CTR);
		c.Encrypt(buf, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg);
		c.InitKey(key, keybits);
		c.SetIV(iv, CryptoCiphers.CTR);
		c.Decrypt(buf, 0, datalen);
		CryptoUtils.Hex2Bin(inputHex, 0, want, 0, datalen);
		UnitAssert.True(Equal(buf, want, datalen), msg)
	END Ctr;

	PROCEDURE TestECB*;
	BEGIN
		Ecb("CryptoDES", "0123456789ABCDEF", "4E6F772069732074",
			"3FA40E8A984D4815", 64, 8, "DES ECB");
		Ecb("CryptoDES3", "0123456789ABCDEF23456789ABCDEF01456789ABCDEF0123", "4E6F772069732074",
			"314F8327FA7A09A8", 192, 8, "3DES ECB");
		Ecb("CryptoIDEA", "00010002000300040005000600070008", "0000000100020003",
			"11FBED2B01986DE5", 128, 8, "IDEA ECB");
		Ecb("CryptoAES", "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
			"6bc1bee22e409f96e93d7e117393172a",
			"bd334f1d6e45f25ff712a214571fa5cc", 192, 16, "AES-192 ECB");
		Ecb("CryptoAES", "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
			"6bc1bee22e409f96e93d7e117393172a",
			"f3eed1bdb5d2a03c064b5a7e3db181f8", 256, 16, "AES-256 ECB");
		Ecb("CryptoARC4", "0123456789abcdef", "0123456789abcdef",
			"75b7878099e0c596", 64, 8, "ARC4");
		Ecb("CryptoCAST", "0123456712345678234567893456789A", "0123456789abcdef",
			"238B4FE5847E44B2", 128, 8, "CAST-128 ECB");
		Ecb("CryptoCAST", "0123456712345678234567893456789A", "0123456789abcdef",
			"EB6A711A2C02271B", 80, 8, "CAST-80 ECB");
		Ecb("CryptoCAST", "0123456712345678234567893456789A", "0123456789abcdef",
			"7AC816D16E9B302E", 40, 8, "CAST-40 ECB")
	END TestECB;

	PROCEDURE TestCBC*;
	BEGIN
		Cbc("CryptoDES", "0123456789ABCDEF", "0123456789ABCDEF", "4E6F772069732074",
			"96C3D4A6DC1C0117", 64, 8, "DES CBC");
		Cbc("CryptoIDEA", "00010002000300040005000600070008", "0000000000000000",
			"0000000100020003", "11FBED2B01986DE5", 128, 8, "IDEA CBC");
		Cbc("CryptoAES", "2b7e151628aed2a6abf7158809cf4f3c", "000102030405060708090A0B0C0D0E0F",
			"6bc1bee22e409f96e93d7e117393172a",
			"7649abac8119b246cee98e9b12e9197d", 128, 16, "AES-128 CBC");
		Cbc("CryptoAES", "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
			"000102030405060708090A0B0C0D0E0F", "6bc1bee22e409f96e93d7e117393172a",
			"f58c4c04d6e5f1ba779eabfb5f7bfbd6", 256, 16, "AES-256 CBC")
	END TestCBC;

	PROCEDURE TestCTR*;
	BEGIN
		Ctr("CryptoAES", "2b7e151628aed2a6abf7158809cf4f3c", "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
			"6bc1bee22e409f96e93d7e117393172a",
			"874d6191b620e3261bef6864990db6ce", 128, 16, "AES-128 CTR");
		Ctr("CryptoAES", "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
			"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "6bc1bee22e409f96e93d7e117393172a",
			"601ec313775789a5b7a7f504bbf3d228", 256, 16, "AES-256 CTR")
	END TestCTR;

END Crypto_devCiphers.
