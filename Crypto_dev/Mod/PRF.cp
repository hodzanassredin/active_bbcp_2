MODULE Crypto_devPRF;
(** Эталонный вектор PRF TLS 1.2 (RFC 5246, P_SHA256), сравнение
	постоянного времени и источник энтропии ОС.

	Вектор: secret = 00..2f, label = "master secret", seed = 32 байта
	'A'; ожидаемые 48 байт получены независимой реализацией P_SHA256
	на Python/hmac (см. Crypto_dev/Vectors/prf.txt). *)

	IMPORT TLS := CryptoTLS, U := CryptoUtils, OsRandom := CryptoOsRandom, AO := CryptoAosCompat, UnitAssert;

	PROCEDURE Hex (IN s: ARRAY OF SHORTCHAR; VAR b: ARRAY OF SHORTCHAR; len: INTEGER);
	BEGIN
		U.Hex2Bin(s, 0, b, 0, len)
	END Hex;

	PROCEDURE Same (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Same;

	PROCEDURE TestPRF*;
		VAR secret, seed, out, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		Hex("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F" +
			"202122232425262728292A2B2C2D2E2F", secret, 48);
		Hex("4141414141414141414141414141414141414141414141414141414141414141", seed, 32);
		TLS.PRFBytes(secret, "master secret", seed, 48, 32, 48, out);
		Hex("35C4ED36804F98F2F98BACAAC69A4CF747040FD83F39B560B8102E12E9F43A70" +
			"B33428F81A2F665639D4BC62C5C99D24", want, 48);
		UnitAssert.True(Same(out, want, 48), "TLS 1.2 PRF P_SHA256 vector")
	END TestPRF;

	PROCEDURE TestConstantTime*;
		VAR a, b: ARRAY 32 OF SHORTCHAR; i: INTEGER; allDiff: BOOLEAN;
	BEGIN
		FOR i := 0 TO 31 DO a[i] := SHORT(CHR(i)); b[i] := SHORT(CHR(i)) END;
		UnitAssert.True(U.EqualBytesCT(a, b, 0, 0, 32), "CT equal");
		allDiff := TRUE;
		FOR i := 0 TO 31 DO
			b[i] := SHORT(CHR(255 - i));
			IF U.EqualBytesCT(a, b, 0, 0, 32) THEN allDiff := FALSE END;
			b[i] := SHORT(CHR(i))
		END;
		UnitAssert.True(allDiff, "CT detects a difference at every position")
	END TestConstantTime;

	PROCEDURE TestDefaults*;
		VAR suites : ARRAY 20 OF AO.SIGNED32; n : AO.SIZE;
	BEGIN
		TLS.DefaultSuites(suites, n);
		UnitAssert.EqualInt(9, n, "nine default suites");
		IF n >= 9 THEN
			(* TLS 1.3 идёт первой, затем ECDHE+AEAD TLS 1.2. *)
			UnitAssert.EqualInt(1302H, suites[0], "default 1: TLS_AES_256_GCM_SHA384");
			UnitAssert.EqualInt(1303H, suites[1], "default 2: TLS_CHACHA20_POLY1305_SHA256");
			UnitAssert.EqualInt(1301H, suites[2], "default 3: TLS_AES_128_GCM_SHA256");
			UnitAssert.EqualInt(0C02CH, suites[3], "default 4: ECDHE-ECDSA-AES256-GCM-SHA384");
			UnitAssert.EqualInt(0C030H, suites[4], "default 5: ECDHE-RSA-AES256-GCM-SHA384");
			UnitAssert.EqualInt(0CCA9H, suites[5], "default 6: ECDHE-ECDSA-CHACHA20-POLY1305-SHA256");
			UnitAssert.EqualInt(0CCA8H, suites[6], "default 7: ECDHE-RSA-CHACHA20-POLY1305-SHA256");
			UnitAssert.EqualInt(0C02BH, suites[7], "default 8: ECDHE-ECDSA-AES128-GCM-SHA256");
			UnitAssert.EqualInt(0C02FH, suites[8], "default 9: ECDHE-RSA-AES128-GCM-SHA256")
		END
	END TestDefaults;

	PROCEDURE TestEntropy*;
		VAR a, b: ARRAY 64 OF SHORTCHAR; i, same, zeros: INTEGER; ok: BOOLEAN;
	BEGIN
		ok := OsRandom.Fill(a, 0, 32);
		UnitAssert.True(ok, "/dev/urandom readable");
		ok := OsRandom.Fill(b, 0, 32);
		UnitAssert.True(ok, "/dev/urandom readable twice");
		same := 0; zeros := 0;
		FOR i := 0 TO 31 DO
			IF a[i] = b[i] THEN INC(same) END;
			IF a[i] = 0X THEN INC(zeros) END
		END;
		UnitAssert.True(same < 32, "two OS entropy blocks differ");
		UnitAssert.True(zeros < 32, "OS entropy is not all zero");
		UnitAssert.True(~OsRandom.Fill(a, 60, 8), "Fill checks bounds")
	END TestEntropy;

END Crypto_devPRF.
