MODULE Crypto_devTiming;
(** Время криптопримитивов, из которых складывается рукопожатие TLS:
	SHA-256, ECDSA P-256 (подпись и проверка), X25519. Числа печатаются
	в миллисекундах на пачку вызовов. *)

	IMPORT Kernel, StdLog, Strings,
		CryptoEC, CryptoECDSA, CryptoHashes, CryptoUtils, CryptoX25519;

	PROCEDURE Msg (IN name: ARRAY OF CHAR; ms: LONGINT);
		VAR x: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(ms, x);
		StdLog.String(name); StdLog.String(" "); StdLog.String(x); StdLog.String(" ms"); StdLog.Ln
	END Msg;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END; RETURN i
	END StrLen;

	PROCEDURE Do*;
		VAR c: CryptoEC.Curve; priv, qx, qy, r, s: CryptoEC.BigInt;
			buf, h: ARRAY 64 OF SHORTCHAR; len, hlen, i: INTEGER; t0: LONGINT;
			hh: CryptoHashes.Hash; k1, k2: CryptoX25519.X25519KeyPair;
			sec: ARRAY 32 OF SHORTCHAR; block: ARRAY 1024 OF SHORTCHAR; ok: BOOLEAN;
	BEGIN
		FOR i := 0 TO LEN(block) - 1 DO block[i] := SHORT(CHR(i MOD 256)) END;
		hh := CryptoHashes.NewHash("CryptoSHA256");
		t0 := Kernel.Time();
		FOR i := 1 TO 1000 DO
			hh.Initialize; hh.Update(block, 0, LEN(block)); hh.GetHash(h, 0)
		END;
		Msg("sha256-1000x1KiB", Kernel.Time() - t0);

		c := CryptoEC.P256();
		len := StrLen("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721") DIV 2;
		CryptoUtils.Hex2Bin("C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721",
			0, buf, 0, len);
		IF CryptoEC.OrderFromBytes(c, buf, 0, len, priv) THEN END;
		hh.Initialize; hh.Update(buf, 0, 32); hh.GetHash(h, 0); hlen := hh.size;
		t0 := Kernel.Time();
		FOR i := 1 TO 10 DO IF ~CryptoECDSA.Sign(c, priv, h, hlen, r, s) THEN END END;
		Msg("ecdsa-sign-10", Kernel.Time() - t0);

		CryptoEC.ScalarMulBase(c, priv, qx, qy, ok);
		t0 := Kernel.Time();
		FOR i := 1 TO 10 DO IF ~CryptoECDSA.Verify(c, qx, qy, h, hlen, r, s) THEN END END;
		Msg("ecdsa-verify-10", Kernel.Time() - t0);

		t0 := Kernel.Time();
		FOR i := 1 TO 10 DO CryptoX25519.GenerateKeyPair(k1) END;
		Msg("x25519-keygen-10", Kernel.Time() - t0);
		CryptoX25519.GenerateKeyPair(k2);
		t0 := Kernel.Time();
		FOR i := 1 TO 10 DO CryptoX25519.Agreement(k1.PrivateKey, k2.PublicKey, sec) END;
		Msg("x25519-agree-10", Kernel.Time() - t0)
	END Do;

END Crypto_devTiming.
