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
		UnitAssert.EqualInt(1, n, "one default suite");
		IF n > 0 THEN
			UnitAssert.EqualInt(0C02FH, suites[0], "default is ECDHE-RSA-AES128-GCM-SHA256")
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
