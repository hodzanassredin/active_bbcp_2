MODULE Crypto_devPKCS1;
(** PKCS#1 v1.5: подпись и проверка, шифрование и расшифровка.

	Ключ 1024 бита генерируется здесь же (TestSlow, как Crypto_devRSA).
	Проверяются: правильная подпись, подделанная подпись, другой digest,
	другой алгоритм хеша, слишком длинная подпись и порча шифртекста. *)

	IMPORT PKCS1 := CryptoPKCS1, RSA := CryptoRSA, Primes := CryptoPrimes,
		BN := CryptoAosBigNumbers, U := CryptoUtils, BIT := CryptoAosBIT, UnitAssert;

	PROCEDURE Same (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Same;

	PROCEDURE TestSlowSign*;
		VAR p, q, e: BN.BigNumber; pub, priv: RSA.Key;
			digest: ARRAY 64 OF SHORTCHAR; sig, sig2, msg, cipher, plain: ARRAY 256 OF SHORTCHAR;
			sigLen, msgLen, outLen: INTEGER; i: INTEGER; ok: BOOLEAN;
	BEGIN
		p := Primes.NewPrime(512, FALSE);
		q := Primes.NewPrime(512, FALSE);
		BN.AssignInt(e, 65537);
		RSA.MakeKeys(p, q, e, "PKCS1 test", pub, priv);
		FOR i := 0 TO 31 DO digest[i] := SHORT(CHR(i)) END;
		sigLen := 0;
		PKCS1.SignHash("CryptoSHA256", digest, 32, priv, sig, 0, sigLen);
		UnitAssert.EqualInt(128, sigLen, "signature length equals modulus");
		ok := PKCS1.VerifyHash("CryptoSHA256", digest, 32, pub, sig, 0, sigLen);
		UnitAssert.True(ok, "valid signature accepted");

		FOR i := 0 TO sigLen - 1 DO sig2[i] := sig[i] END;
		sig2[sigLen - 1] := BIT.CXOR(sig2[sigLen - 1], 1X);
		ok := PKCS1.VerifyHash("CryptoSHA256", digest, 32, pub, sig2, 0, sigLen);
		UnitAssert.True(~ok, "tampered signature rejected");

		digest[0] := BIT.CXOR(digest[0], 1X);
		ok := PKCS1.VerifyHash("CryptoSHA256", digest, 32, pub, sig, 0, sigLen);
		UnitAssert.True(~ok, "wrong digest rejected");
		digest[0] := BIT.CXOR(digest[0], 1X);

		ok := PKCS1.VerifyHash("CryptoMD5", digest, 32, pub, sig, 0, sigLen);
		UnitAssert.True(~ok, "wrong hash algorithm rejected");

		ok := PKCS1.VerifyHash("CryptoSHA256", digest, 32, pub, sig, 0, sigLen + 1);
		UnitAssert.True(~ok, "oversized signature rejected");

		(* DigestInfo SHA-384 и SHA-512 (сертификаты публичных CA). *)
		FOR i := 0 TO 47 DO digest[i] := SHORT(CHR(i)) END;
		sigLen := 0;
		PKCS1.SignHash("CryptoSHA384", digest, 48, priv, sig, 0, sigLen);
		UnitAssert.True(PKCS1.VerifyHash("CryptoSHA384", digest, 48, pub, sig, 0, sigLen),
			"SHA-384 signature verifies");
		digest[0] := BIT.CXOR(digest[0], 1X);
		UnitAssert.True(~PKCS1.VerifyHash("CryptoSHA384", digest, 48, pub, sig, 0, sigLen),
			"SHA-384 wrong digest rejected");
		digest[0] := BIT.CXOR(digest[0], 1X);

		FOR i := 0 TO 63 DO digest[i] := SHORT(CHR(i)) END;
		sigLen := 0;
		PKCS1.SignHash("CryptoSHA512", digest, 64, priv, sig, 0, sigLen);
		UnitAssert.True(PKCS1.VerifyHash("CryptoSHA512", digest, 64, pub, sig, 0, sigLen),
			"SHA-512 signature verifies");

		(* Шифрование типа 2 и расшифровка. *)
		FOR i := 0 TO 47 DO msg[i] := SHORT(CHR(i + 1)) END;
		msgLen := 0;
		PKCS1.Encrypt(msg, 0, 48, pub, 2, cipher, 0, msgLen);
		UnitAssert.EqualInt(128, msgLen, "ciphertext length");
		outLen := 0;
		PKCS1.Decrypt(cipher, 0, msgLen, priv, 2, plain, 0, outLen);
		UnitAssert.EqualInt(48, outLen, "decrypted length");
		UnitAssert.True(Same(msg, plain, 48), "encrypt/decrypt roundtrip");

		cipher[10] := BIT.CXOR(cipher[10], 1X);
		outLen := 0;
		PKCS1.Decrypt(cipher, 0, msgLen, priv, 2, plain, 0, outLen);
		UnitAssert.EqualInt(0, outLen, "tampered ciphertext rejected")
	END TestSlowSign;

END Crypto_devPKCS1.
