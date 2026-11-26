MODULE Crypto_devTLS;
(** Рукопожатие TLS 1.2 и обмен строкой с локальным сервером.

	Адрес сервера приходит внешней переменной CRYPTO_TLS_ADDR
	(host:port). Ожидается openssl s_server с ключом -rev: клиент шлёт
	"hello", сервер возвращает перевёрнутую строку.

	CRYPTO_TLS_CA — путь к PEM доверенного корня (без него проверка
	сертификата обязана провалиться). CRYPTO_TLS_CERT/CRYPTO_TLS_KEY —
	необязательный клиентский сертификат для взаимной проверки.
	CRYPTO_TLS_EXPECT_FAIL=1 означает, что рукопожатие должно быть
	отвергнуто: тест тогда требует отказа, а не обмена.

	Без сервера тест не запускается: его вызывает tools/test-crypto-tls.sh,
	а не tools/test.sh. *)

	IMPORT Dialog, CryptoTLS, TCP := CryptoAosTCP, UnitAssert, Services;

	CONST
		reqLen = 6;
		repLen = 6;

	PROCEDURE EqualBytes (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (ORD(a[i]) = ORD(b[i])) DO INC(i) END;
		RETURN i = len
	END EqualBytes;

	PROCEDURE GetPar (IN name: ARRAY OF CHAR; VAR value: ARRAY OF CHAR);
		VAR res: INTEGER;
	BEGIN
		value[0] := 0X;
		Dialog.GetPar(name, value, res);
		IF res # 0 THEN value[0] := 0X END
	END GetPar;

	PROCEDURE TestProcHandshake*;
		VAR addr, ca, cert, key, expectFail: ARRAY 256 OF CHAR;
			deadline: LONGINT;
			stateAfterOpen: INTEGER;
			req, want, buf: ARRAY 16 OF SHORTCHAR;
			res, written, read, got: INTEGER;
			con: CryptoTLS.Connection;
			ctx: CryptoTLS.Context;
			failed: BOOLEAN;
	BEGIN
		GetPar("CRYPTO_TLS_ADDR", addr);
		GetPar("CRYPTO_TLS_CA", ca);
		GetPar("CRYPTO_TLS_CERT", cert);
		GetPar("CRYPTO_TLS_KEY", key);
		GetPar("CRYPTO_TLS_EXPECT_FAIL", expectFail);
		UnitAssert.True(addr[0] # 0X, "CRYPTO_TLS_ADDR is set");
		IF addr[0] = 0X THEN RETURN END;

		ctx := CryptoTLS.NewContext();
		IF ca[0] # 0X THEN
			UnitAssert.True(ctx.AddTrustedFile(ca), "trusted CA loaded")
		END;
		IF (cert[0] # 0X) & (key[0] # 0X) THEN
			UnitAssert.True(ctx.LoadClientCertificateFile(cert), "client certificate loaded");
			UnitAssert.True(ctx.LoadClientPrivateKeyFile(key), "client key loaded")
		END;
		con := CryptoTLS.GetConnection();
		con.SetContext(ctx);
		con.Open("0.0.0.0:0", addr, res);
		stateAfterOpen := con.State();
		failed := (res # TCP.Ok) OR (stateAfterOpen # TCP.Established);

		IF expectFail[0] = '1' THEN
			UnitAssert.True(failed, "handshake must be rejected");
			IF ~failed THEN con.Close END
		ELSE
			UnitAssert.EqualInt(TCP.Ok, res, "TLS connection opened");
			UnitAssert.True(~failed, "handshake completed");
			IF ~failed THEN
				req[0] := SHORT(CHR(68H)); req[1] := SHORT(CHR(65H)); req[2] := SHORT(CHR(6CH));
				req[3] := SHORT(CHR(6CH)); req[4] := SHORT(CHR(6FH)); req[5] := SHORT(CHR(0AH));
				want[0] := SHORT(CHR(6FH)); want[1] := SHORT(CHR(6CH)); want[2] := SHORT(CHR(6CH));
				want[3] := SHORT(CHR(65H)); want[4] := SHORT(CHR(68H)); want[5] := SHORT(CHR(0AH));
				con.Send(req, 0, reqLen, TRUE, written);
				UnitAssert.EqualInt(0, written, "TLS sends the request");
				got := 0;
				deadline := Services.Ticks() + 5000;
				REPEAT
					read := 0;
					con.Receive(buf, got, LEN(buf) - got, 1, read, res);
					INC(got, read)
				UNTIL (got >= repLen) OR (Services.Ticks() > deadline);
				UnitAssert.EqualInt(repLen, got, "TLS receives the reply");
				UnitAssert.True(EqualBytes(buf, want, repLen), "TLS reply is reversed request");
				con.Close
			END
		END
	END TestProcHandshake;

END Crypto_devTLS.
