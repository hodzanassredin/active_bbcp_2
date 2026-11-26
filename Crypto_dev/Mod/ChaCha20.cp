MODULE Crypto_devChaCha20;
(** Тесты ChaCha20 и Poly1305 по RFC 8439.

	Блок функции, потоковое шифрование, MAC Poly1305 и AEAD
	ChaCha20-Poly1305 сверяются с векторами RFC 8439; проверяются
	подмена тега, AAD и nonce, а также длины не кратные блоку. *)

	IMPORT CryptoChaCha20, CryptoUtils, UnitAssert;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE HexTo (IN hex: ARRAY OF SHORTCHAR; VAR buf: ARRAY OF SHORTCHAR);
		VAR len: INTEGER;
	BEGIN
		len := StrLen(hex) DIV 2;
		CryptoUtils.Hex2Bin(hex, 0, buf, 0, len)
	END HexTo;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE CheckBytes (IN got, hex: ARRAY OF SHORTCHAR; len: INTEGER; IN msg: ARRAY OF CHAR);
		VAR want: ARRAY 160 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin(hex, 0, want, 0, len);
		UnitAssert.True(Equal(got, want, len), msg)
	END CheckBytes;

	PROCEDURE TestBlock*;
		VAR key, nonce, zero, ks: ARRAY 64 OF SHORTCHAR;
			i: INTEGER;
	BEGIN
		HexTo("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", key);
		HexTo("000000000000004A00000000", nonce);
		FOR i := 0 TO 63 DO zero[i] := 0X END;
		CryptoChaCha20.StreamXor(key, nonce, 1, zero, 64, ks);
		CheckBytes(ks, "224F51F3401BD9E12FDE276FB8631DED8C131F823D2C06E27E4FCAEC9EF3CF78" +
			"8A3B0AA372600A92B57974CDED2B9334794CBA40C63E34CDEA212C4CF07D41B7",
			64, "RFC 8439 block keystream")
	END TestBlock;

	PROCEDURE TestStream*;
		VAR key, nonce, plain, cipher, back, want: ARRAY 160 OF SHORTCHAR;
			len: INTEGER;
	BEGIN
		HexTo("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", key);
		HexTo("000000000000004A00000000", nonce);
		plain := "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
		len := StrLen(plain);
		CryptoChaCha20.StreamXor(key, nonce, 1, plain, len, cipher);
		CheckBytes(cipher, "6E2E359A2568F98041BA0728DD0D6981E97E7AEC1D4360C20A27AFCCFD9FAE0B" +
			"F91B65C5524733AB8F593DABCD62B3571639D624E65152AB8F530C359F0861D8" +
			"07CA0DBF500D6A6156A38E088A22B65E52BC514D16CCF806818CE91AB7793736" +
			"5AF90BBF74A35BE6B40B8EEDF2785E42874D", len, "RFC 8439 ciphertext");
		CryptoChaCha20.StreamXor(key, nonce, 1, cipher, len, back);
		UnitAssert.True(Equal(back, plain, len), "ChaCha20 decrypts back")
	END TestStream;

	PROCEDURE TestPoly1305*;
		VAR key, msg, tag: ARRAY 64 OF SHORTCHAR;
	BEGIN
		HexTo("85D6BE7857556D337F4452FE42D506A80103808AFB0DB2FD4ABFF6AF4149F51B", key);
		msg := "Cryptographic Forum Research Group";
		CryptoChaCha20.Poly1305(key, msg, StrLen(msg), tag);
		CheckBytes(tag, "A8061DC1305136C6C22B8BAF0C0127A9", 16, "RFC 8439 Poly1305 tag")
	END TestPoly1305;

	PROCEDURE TestAEAD*;
		VAR key, nonce, aad, plain, cipher, tag, back: ARRAY 160 OF SHORTCHAR;
			len: INTEGER; ok: BOOLEAN;
	BEGIN
		HexTo("808182838485868788898A8B8C8D8E8F909192939495969798999A9B9C9D9E9F", key);
		HexTo("070000004041424344454647", nonce);
		HexTo("50515253C0C1C2C3C4C5C6C7", aad);
		plain := "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
		len := StrLen(plain);
		CryptoChaCha20.Encrypt(key, nonce, aad, 12, plain, len, cipher, tag);
		CheckBytes(cipher, "D31A8D34648E60DB7B86AFBC53EF7EC2A4ADED51296E08FEA9E2B5A736EE62D6" +
			"3DBEA45E8CA9671282FAFB69DA92728B1A71DE0A9E060B2905D6A5B67ECD3B36" +
			"92DDBD7F2D778B8C9803AEE328091B58FAB324E4FAD675945585808B4831D7BC" +
			"3FF4DEF08E4B7A9DE576D26586CEC64B6116", len, "RFC 8439 AEAD ciphertext");
		CheckBytes(tag, "1AE10B594F09E26A7E902ECBD0600691", 16, "RFC 8439 AEAD tag");
		UnitAssert.True(CryptoChaCha20.Decrypt(key, nonce, aad, 12, cipher, len, tag, back),
			"AEAD decrypts");
		UnitAssert.True(Equal(back, plain, len), "AEAD plaintext matches");
		tag[0] := SHORT(CHR(ORD(tag[0]) + 1));
		UnitAssert.True(~CryptoChaCha20.Decrypt(key, nonce, aad, 12, cipher, len, tag, back),
			"AEAD rejects tampered tag");
		tag[0] := SHORT(CHR(ORD(tag[0]) - 1));
		aad[0] := SHORT(CHR(ORD(aad[0]) + 1));
		UnitAssert.True(~CryptoChaCha20.Decrypt(key, nonce, aad, 12, cipher, len, tag, back),
			"AEAD rejects changed AAD");
		aad[0] := SHORT(CHR(ORD(aad[0]) - 1));
		nonce[0] := SHORT(CHR(ORD(nonce[0]) + 1));
		ok := CryptoChaCha20.Decrypt(key, nonce, aad, 12, cipher, len, tag, back);
		UnitAssert.True(~ok, "AEAD rejects changed nonce")
	END TestAEAD;

	PROCEDURE TestLengths*;
		VAR key, nonce, aad, plain, cipher, tag, back: ARRAY 160 OF SHORTCHAR;
			i, len: INTEGER; ok: BOOLEAN;
	BEGIN
		HexTo("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F", key);
		HexTo("000000000000004A00000000", nonce);
		HexTo("A0A1A2A3A4", aad);
		FOR i := 0 TO 40 DO
			plain[i] := SHORT(CHR(65 + i MOD 26))
		END;
		FOR len := 0 TO 40 DO
			CryptoChaCha20.Encrypt(key, nonce, aad, 5, plain, len, cipher, tag);
			ok := CryptoChaCha20.Decrypt(key, nonce, aad, 5, cipher, len, tag, back);
			UnitAssert.True(ok & Equal(back, plain, len), "AEAD roundtrip for every length")
		END
	END TestLengths;

END Crypto_devChaCha20.
