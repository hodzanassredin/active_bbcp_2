MODULE Crypto_devGcm;
(** Тесты режима GCM на векторах NIST SP 800-38D.

	Проверяются тег пустого сообщения, шифрование с AAD и дешифрование,
	а также работа на месте. *)

	IMPORT CryptoGcm, CryptoCiphers, CryptoUtils, UnitAssert;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE TestEmpty*;
		VAR key, iv, plain, aad, tag, want: ARRAY 32 OF SHORTCHAR;
			g: CryptoGcm.GcmMode; c: CryptoCiphers.Cipher;
	BEGIN
		CryptoUtils.Hex2Bin("11754cd72aec309bf52f7687212e8957", 0, key, 0, 16);
		CryptoUtils.Hex2Bin("3c819d9a9bed087615030b65", 0, iv, 0, 12);
		CryptoUtils.Hex2Bin("250327c674aaf477aef2675748cf6971", 0, want, 0, 16);
		c := CryptoCiphers.NewCipher("CryptoAES");
		c.InitKey(key, 128);
		NEW(g);
		g.Init(c, iv, 12);
		g.Encrypt(plain, aad, 0, 0, 0, 128, plain, tag, 0);
		UnitAssert.True(Equal(tag, want, 16), "GCM NIST vector 1 tag")
	END TestEmpty;

	PROCEDURE TestVector2*;
		VAR key, iv, plain, aad, want, ciphertext, tag: ARRAY 257 OF SHORTCHAR;
			got, decrypted: ARRAY 257 OF SHORTCHAR;
			g: CryptoGcm.GcmMode; c: CryptoCiphers.Cipher; res: INTEGER;
			hex: ARRAY 512 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin("fe47fcce5fc32665d2ae399e4eec72ba", 0, key, 0, 16);
		CryptoUtils.Hex2Bin("5adb9609dbaeb58cbd6e7275", 0, iv, 0, 12);
		hex := "7c0e88c88899a779228465074797cd4c2e1498d259b54390b85e3eef1c02df60e743f1b840382c4bccaf" +
			"3bafb4ca8429bea063";
		CryptoUtils.Hex2Bin(hex, 0, plain, 0, LEN(hex$) DIV 2);
		hex := "88319d6e1d3ffa5f987199166c8a9b56c2aeba5a";
		CryptoUtils.Hex2Bin(hex, 0, aad, 0, LEN(hex$) DIV 2);
		hex := "98f4826f05a265e6dd2be82db241c0fbbbf9ffb1c173aa83964b7cf5393043736365253ddb" +
			"c5db8778371495da76d269e5db3e";
		CryptoUtils.Hex2Bin(hex, 0, want, 0, LEN(hex$) DIV 2);
		CryptoUtils.Hex2Bin("291ef1982e4defedaa2249f898556b47", 0, tag, 0, 16);
		c := CryptoCiphers.NewCipher("CryptoAES");
		c.InitKey(key, 128);
		NEW(g);
		g.Init(c, iv, 12);
		g.Encrypt(plain, aad, 0, 51, 20, 128, got, ciphertext, 0);
		UnitAssert.True(Equal(got, want, 51), "GCM NIST vector 2 ciphertext");
		UnitAssert.True(Equal(ciphertext, tag, 16), "GCM NIST vector 2 tag");
		g.Decrypt(got, aad, ciphertext, 0, 51, 20, 128, decrypted, 0, res);
		UnitAssert.EqualInt(0, res, "GCM NIST vector 2 decrypt status");
		UnitAssert.True(Equal(decrypted, plain, 51), "GCM NIST vector 2 plaintext")
	END TestVector2;

	PROCEDURE TestInplace*;
		VAR key, iv, text, ad, tag: ARRAY 257 OF SHORTCHAR;
			g: CryptoGcm.GcmMode; c: CryptoCiphers.Cipher; res: INTEGER;
	BEGIN
		CryptoUtils.Hex2Bin("c7d9358af0fd737b118dbf4347fd252a", 0, key, 0, 16);
		CryptoUtils.Hex2Bin("5adb9609dbaeb58cbd6e7275", 0, iv, 0, 12);
		text := "123hello world456";
		ad := "ad";
		c := CryptoCiphers.NewCipher("CryptoAES");
		c.InitKey(key, 128);
		NEW(g);
		g.Init(c, iv, 12);
		g.Encrypt(text, ad, 3, 11, 2, 128, text, tag, 3);
		g.Decrypt(text, ad, tag, 3, 11, 2, 128, text, 3, res);
		UnitAssert.EqualInt(0, res, "GCM in-place decrypt status");
		UnitAssert.True(text = "123hello world456", "GCM in-place plaintext")
	END TestInplace;

END Crypto_devGcm.
