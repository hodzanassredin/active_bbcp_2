MODULE Crypto_devRSA;
(** Тест RSA: генерация ключей, подпись и проверка.

	Простые числа 512 бит дают модуль 1024 бита. Тест помечен TestSlow,
	потому что поиск простых чисел занимает время. *)

	IMPORT CryptoRSA, CryptoPrimes, BN := CryptoAosBigNumbers, UnitAssert;

	PROCEDURE TestSlowRSA*;
		CONST Size = 1024;
		VAR pub, priv: CryptoRSA.Key;
			p, q, e, signature: BN.BigNumber;
			digest: ARRAY 64 OF SHORTCHAR;
	BEGIN
		p := CryptoPrimes.NewPrime(Size DIV 2, FALSE);
		q := CryptoPrimes.NewPrime(Size DIV 2, FALSE);
		BN.AssignInt(e, 3);
		CryptoRSA.MakeKeys(p, q, e, "Test Key", pub, priv);
		digest := "Everyone gets Friday off her panties.";
		signature := priv.Sign(digest, 32);
		UnitAssert.NotNil(signature, "RSA signature");
		IF signature # NIL THEN
			UnitAssert.True(pub.Verify(digest, 32, signature), "RSA sign and verify")
		END
	END TestSlowRSA;

END Crypto_devRSA.
