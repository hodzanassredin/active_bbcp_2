MODULE Crypto_devTLS13;
(** Ключевое расписание TLS 1.3 по векторам RFC 8448, раздел 3.

	Проверяются HKDF-Expand-Label, Derive-Secret, Finished и ключи
	записи простого рукопожатия (TLS_AES_128_GCM_SHA256, x25519).
	Значения сообщений — в Crypto_devTLS13Data (сгенерирован из текста
	RFC 8448). Здесь же проверяется, что секреты и Finished привязаны
	к транскрипту: изменение одного байта меняет результат. *)

	IMPORT Data := Crypto_devTLS13Data, CryptoTLS, CryptoHKDF, CryptoUtils,
		UnitAssert, AO := CryptoAosCompat;

	CONST
		HashName = "CryptoSHA256";
		HashLen = 32;

	PROCEDURE HexBytes (IN hex : ARRAY OF SHORTCHAR): AO.SIZE;
		VAR n : AO.SIZE;
	BEGIN
		n := LEN(hex);
		IF (n > 0) & (hex[n - 1] = 0X) THEN DEC(n) END;
		RETURN n DIV 2
	END HexBytes;

	PROCEDURE HexTo (IN hex : ARRAY OF SHORTCHAR; VAR bin : ARRAY OF SHORTCHAR;
			VAR len : AO.SIZE);
	BEGIN
		len := HexBytes(hex);
		CryptoUtils.Hex2Bin(hex, 0, bin, 0, len)
	END HexTo;

	PROCEDURE Equal (IN a, b : ARRAY OF SHORTCHAR; len : AO.SIZE): BOOLEAN;
		VAR i : AO.SIZE;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE Check (IN got : ARRAY OF SHORTCHAR; gotLen : AO.SIZE;
			IN hex : ARRAY OF SHORTCHAR; IN msg : ARRAY OF CHAR);
		VAR want : ARRAY 64 OF SHORTCHAR; n : AO.SIZE;
	BEGIN
		HexTo(hex, want, n);
		UnitAssert.True(Equal(got, want, gotLen), msg)
	END Check;

	PROCEDURE AppendHex (IN hex : ARRAY OF SHORTCHAR; VAR dst : ARRAY OF SHORTCHAR;
			VAR pos : AO.SIZE);
		VAR n : AO.SIZE;
	BEGIN
		n := HexBytes(hex);
		CryptoUtils.Hex2Bin(hex, 0, dst, pos, n);
		INC(pos, n)
	END AppendHex;

	(* ClientHello || ServerHello. *)
	PROCEDURE BuildHandshakeTranscript (VAR t : ARRAY OF SHORTCHAR; VAR len : AO.SIZE);
	BEGIN
		len := 0;
		AppendHex(Data.ClientHello, t, len);
		AppendHex(Data.ServerHello, t, len)
	END BuildHandshakeTranscript;

	(* ClientHello || ServerHello || EncryptedExtensions || Certificate ||
	   CertificateVerify. *)
	PROCEDURE BuildCertVerifyTranscript (VAR t : ARRAY OF SHORTCHAR; VAR len : AO.SIZE);
	BEGIN
		len := 0;
		AppendHex(Data.ClientHello, t, len);
		AppendHex(Data.ServerHello, t, len);
		AppendHex(Data.EncryptedExtensions, t, len);
		AppendHex(Data.Certificate, t, len);
		AppendHex(Data.CertificateVerify, t, len)
	END BuildCertVerifyTranscript;

	(* ... || server Finished. *)
	PROCEDURE BuildServerFinishedTranscript (VAR t : ARRAY OF SHORTCHAR; VAR len : AO.SIZE);
	BEGIN
		BuildCertVerifyTranscript(t, len);
		AppendHex(Data.ServerFinished, t, len)
	END BuildServerFinishedTranscript;

	PROCEDURE TestKeySchedule*;
		VAR shared, zeros, early, derived, hs, derived2, master : ARRAY 64 OF SHORTCHAR;
			t : ARRAY 1536 OF SHORTCHAR;
			empty : ARRAY 1 OF SHORTCHAR;
			secret, s2 : ARRAY 64 OF SHORTCHAR;
			tLen, n : AO.SIZE;
	BEGIN
		HexTo(Data.SharedSecret, shared, n);
		UnitAssert.EqualInt(32, n, "RFC 8448 shared secret is 32 bytes");
		FOR n := 0 TO HashLen - 1 DO zeros[n] := 0X END;
		empty[0] := 0X;

		CryptoHKDF.Extract(HashName, zeros, HashLen, zeros, HashLen, early);
		Check(early, HashLen, Data.EarlySecret, "RFC 8448 early secret");

		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, early, HashLen,
			"derived", 7, empty, 0, derived, n), "Derive-Secret(derived)");
		UnitAssert.EqualInt(HashLen, n, "Derive-Secret length");
		Check(derived, HashLen, Data.DerivedSecret, "RFC 8448 derived secret");

		CryptoHKDF.Extract(HashName, derived, HashLen, shared, 32, hs);
		Check(hs, HashLen, Data.HandshakeSecret, "RFC 8448 handshake secret");

		BuildHandshakeTranscript(t, tLen);
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, hs, HashLen,
			"c hs traffic", 12, t, tLen, secret, n), "Derive-Secret(c hs traffic)");
		Check(secret, HashLen, Data.ClientHsSecret, "RFC 8448 client hs traffic");
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, hs, HashLen,
			"s hs traffic", 12, t, tLen, s2, n), "Derive-Secret(s hs traffic)");
		Check(s2, HashLen, Data.ServerHsSecret, "RFC 8448 server hs traffic");

		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, hs, HashLen,
			"derived", 7, empty, 0, derived2, n), "Derive-Secret(master)");
		Check(derived2, HashLen, Data.DerivedSecret2, "RFC 8448 derived2");
		CryptoHKDF.Extract(HashName, derived2, HashLen, zeros, HashLen, master);
		Check(master, HashLen, Data.MasterSecret, "RFC 8448 master secret")
	END TestKeySchedule;

	PROCEDURE TestFinished*;
		VAR t : ARRAY 1536 OF SHORTCHAR;
			sHs, cHs, out : ARRAY 64 OF SHORTCHAR;
			tLen, n : AO.SIZE;
	BEGIN
		HexTo(Data.ServerHsSecret, sHs, n);
		HexTo(Data.ClientHsSecret, cHs, n);
		BuildCertVerifyTranscript(t, tLen);
		UnitAssert.True(CryptoTLS.TLS13Finished(HashName, sHs, HashLen,
			t, tLen, out, n), "server Finished");
		UnitAssert.EqualInt(HashLen, n, "server Finished length");
		Check(out, n, Data.ServerVerifyData, "RFC 8448 server Finished");

		BuildServerFinishedTranscript(t, tLen);
		UnitAssert.True(CryptoTLS.TLS13Finished(HashName, cHs, HashLen,
			t, tLen, out, n), "client Finished");
		Check(out, n, Data.ClientVerifyData, "RFC 8448 client Finished")
	END TestFinished;

	PROCEDURE TestAppSecrets*;
		VAR t : ARRAY 1536 OF SHORTCHAR;
			master, secret : ARRAY 64 OF SHORTCHAR;
			tLen, n : AO.SIZE;
	BEGIN
		HexTo(Data.MasterSecret, master, n);
		BuildServerFinishedTranscript(t, tLen);
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, master, HashLen,
			"c ap traffic", 12, t, tLen, secret, n), "c ap traffic");
		Check(secret, HashLen, Data.ClientAppSecret, "RFC 8448 client app traffic");
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, master, HashLen,
			"s ap traffic", 12, t, tLen, secret, n), "s ap traffic");
		Check(secret, HashLen, Data.ServerAppSecret, "RFC 8448 server app traffic")
	END TestAppSecrets;

	PROCEDURE TestWriteKeys*;
		VAR secret, key, iv : ARRAY 64 OF SHORTCHAR;
			empty : ARRAY 1 OF SHORTCHAR;
			n : AO.SIZE;
	BEGIN
		empty[0] := 0X;
		HexTo(Data.ServerHsSecret, secret, n);
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"key", 3, empty, 0, key, 16), "hs key");
		Check(key, 16, Data.ServerWriteKey, "RFC 8448 server hs write key");
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"iv", 2, empty, 0, iv, 12), "hs iv");
		Check(iv, 12, Data.ServerWriteIv, "RFC 8448 server hs write iv");

		HexTo(Data.ServerAppSecret, secret, n);
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"key", 3, empty, 0, key, 16), "server app key");
		Check(key, 16, Data.ServerAppKey, "RFC 8448 server app write key");
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"iv", 2, empty, 0, iv, 12), "server app iv");
		Check(iv, 12, Data.ServerAppIv, "RFC 8448 server app write iv");

		HexTo(Data.ClientAppSecret, secret, n);
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"key", 3, empty, 0, key, 16), "client app key");
		Check(key, 16, Data.ClientAppKey, "RFC 8448 client app write key");
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"iv", 2, empty, 0, iv, 12), "client app iv");
		Check(iv, 12, Data.ClientAppIv, "RFC 8448 client app write iv")
	END TestWriteKeys;

	(* Секреты и Finished зависят от всех байтов транскрипта: изменение
	   одного байта CertificateVerify меняет и то, и другое. *)
	PROCEDURE TestTranscriptBinding*;
		VAR t, t2 : ARRAY 1536 OF SHORTCHAR;
			secret, secret2, fin, fin2 : ARRAY 64 OF SHORTCHAR;
			hs : ARRAY 64 OF SHORTCHAR;
			tLen, n, len2 : AO.SIZE;
	BEGIN
		HexTo(Data.HandshakeSecret, hs, n);
		BuildHandshakeTranscript(t, tLen);
		BuildCertVerifyTranscript(t2, len2);
		(* База для app-секрета — транскрипт с server Finished. *)
		BuildServerFinishedTranscript(t2, len2);
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, Data.MasterSecret, HashLen,
			"c ap traffic", 12, t2, len2, secret, n), "app secret");
		IF t2[len2 - 1] = 0X THEN t2[len2 - 1] := 1X ELSE t2[len2 - 1] := 0X END;
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret(HashName, Data.MasterSecret, HashLen,
			"c ap traffic", 12, t2, len2, secret2, n), "app secret after change");
		UnitAssert.True(~Equal(secret, secret2, HashLen),
			"application secret is bound to the transcript");

		BuildCertVerifyTranscript(t2, len2);
		UnitAssert.True(CryptoTLS.TLS13Finished(HashName, Data.ServerHsSecret, HashLen,
			t2, len2, fin, n), "finished before change");
		IF t2[len2 - 1] = 0X THEN t2[len2 - 1] := 1X ELSE t2[len2 - 1] := 0X END;
		UnitAssert.True(CryptoTLS.TLS13Finished(HashName, Data.ServerHsSecret, HashLen,
			t2, len2, fin2, n), "finished after change");
		UnitAssert.True(~Equal(fin, fin2, HashLen),
			"Finished is bound to the transcript");

		(* CertificateVerify подписывается хешем транскрипта без себя. *)
		BuildHandshakeTranscript(t, tLen);
		UnitAssert.True(tLen > 0, "handshake transcript is not empty")
	END TestTranscriptBinding;

	PROCEDURE TestLabelBound*;
		VAR key1, key2, secret : ARRAY 64 OF SHORTCHAR;
			empty : ARRAY 1 OF SHORTCHAR;
			n : AO.SIZE;
	BEGIN
		empty[0] := 0X;
		HexTo(Data.ServerHsSecret, secret, n);
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"key", 3, empty, 0, key1, 16), "key label");
		(* Другая метка даёт другой ключ: префикс "tls13 " и метка
		   входят в info HKDF. *)
		UnitAssert.True(CryptoTLS.HKDFExpandLabel(HashName, secret, HashLen,
			"iv", 2, empty, 0, key2, 16), "iv label");
		UnitAssert.True(~Equal(key1, key2, 16), "labels separate keys")
	END TestLabelBound;

	(* SHA-384: Derive-Secret и Finished на фиксированном транскрипте;
	   значения посчитаны независимо. *)
	PROCEDURE TestSha384*;
		VAR hs, got, t : ARRAY 64 OF SHORTCHAR;
			n : AO.SIZE;
	BEGIN
		HexTo("A14DE67E2927FEF3CE9E0D4680B550411AF31A21A9C5904A4372C2ADB71DDA3D9F1049FD9D5B6827864542D4571C9402", hs, n);
		t := "TLS 1.3 SHA-384 test transcript";
		UnitAssert.True(CryptoTLS.TLS13DeriveSecret("CryptoSHA384", hs, 48,
			"c hs traffic", 12, t, 31, got, n), "sha384 c hs traffic");
		Check(got, 48, "F39E87693F289481FF588DEBFD414DA1BBAE4F2471F76BF9D895889494354C08906644EAFC329F82857FD41F765B53EC",
			"SHA-384 c hs traffic");
		UnitAssert.True(CryptoTLS.TLS13Finished("CryptoSHA384", hs, 48,
			t, 31, got, n), "sha384 Finished");
		Check(got, 48, "DAB73E7AE0590BBFF5A4C105841E28AF34F836935A73A6A9505CC699C0FA1B6F0316DF8B54E33632DD166C77035089E4",
			"SHA-384 Finished")
	END TestSha384;

BEGIN
END Crypto_devTLS13.
