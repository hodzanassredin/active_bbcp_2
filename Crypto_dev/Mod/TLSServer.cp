MODULE Crypto_devTLSServer;
(** Сервер TLS 1.2 для проверки с openssl s_client.

	Внешние переменные: CRYPTO_TLS_ADDR (host:port для прослушивания),
	CRYPTO_TLS_CERT и CRYPTO_TLS_KEY (PEM серверного сертификата и
	ключа), CRYPTO_TLS_CA (PEM доверенного корня для проверки клиента),
	CRYPTO_TLS_REQUIRE_CLIENT=1 — требовать клиентский сертификат,
	CRYPTO_TLS_EXPECT_REJECT=1 — ожидать, что ни одно рукопожатие не
	завершится (отрицательный случай), CRYPTO_TLS_EXPECT_BAD_APP=1 —
	рукопожатие завершается, но запись после него (CCS, пустая или из
	нулей) отвергается до прихода запроса.

	Сервер принимает одно соединение, читает строку, возвращает её
	перевёрнутой и закрывается. Запускается из tools/test-crypto-tls.sh. *)

	IMPORT Dialog, CryptoTLS, TCP := CryptoAosTCP, Services, UnitAssert;

	CONST
		reqLen = 6;

	PROCEDURE GetPar (IN name: ARRAY OF CHAR; VAR value: ARRAY OF CHAR);
		VAR res: INTEGER;
	BEGIN
		value[0] := 0X;
		Dialog.GetPar(name, value, res);
		IF res # 0 THEN value[0] := 0X END
	END GetPar;

	PROCEDURE Wait (ms: LONGINT);
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE Services.Ticks() - t < ms DO END
	END Wait;

	PROCEDURE TestProcServer*;
		VAR addr, cert, key, ca, require, expectReject, badApp: ARRAY 256 OF CHAR;
			ctx: CryptoTLS.Context;
			srv, tls: CryptoTLS.Connection;
			cl: TCP.Connection;
			buf, rev: ARRAY 64 OF SHORTCHAR;
			res, got, read, written, i: INTEGER;
			deadline: LONGINT;
			accepted, rejected: BOOLEAN;
	BEGIN
		GetPar("CRYPTO_TLS_ADDR", addr);
		GetPar("CRYPTO_TLS_CERT", cert);
		GetPar("CRYPTO_TLS_KEY", key);
		GetPar("CRYPTO_TLS_CA", ca);
		GetPar("CRYPTO_TLS_REQUIRE_CLIENT", require);
		GetPar("CRYPTO_TLS_EXPECT_REJECT", expectReject);
		GetPar("CRYPTO_TLS_EXPECT_BAD_APP", badApp);
		UnitAssert.True(addr[0] # 0X, "CRYPTO_TLS_ADDR is set");
		UnitAssert.True(cert[0] # 0X, "CRYPTO_TLS_CERT is set");
		UnitAssert.True(key[0] # 0X, "CRYPTO_TLS_KEY is set");
		IF addr[0] = 0X THEN RETURN END;

		ctx := CryptoTLS.NewContext();
		UnitAssert.True(ctx.LoadCertificateFile(cert), "server certificate loaded");
		IF ~ctx.LoadPrivateKeyFile(key) THEN
			UnitAssert.True(ctx.LoadEcPrivateKeyFile(key), "server key loaded")
		END;
		IF ca[0] # 0X THEN
			UnitAssert.True(ctx.AddTrustedFile(ca), "trusted CA loaded")
		END;
		ctx.RequireClientCertificate(require[0] = '1');
		srv := CryptoTLS.GetConnection();
		srv.SetContext(ctx);
		srv.Open(addr, "", res);
		UnitAssert.EqualInt(TCP.Ok, res, "server listens");

		accepted := FALSE;
		rejected := TRUE;
		deadline := Services.Ticks() + 20000;
		IF expectReject[0] = '1' THEN deadline := Services.Ticks() + 4000 END;
		REPEAT
			cl := NIL;
			res := -1;
			srv.Accept(cl, res);
			IF res = TCP.Ok THEN
				accepted := TRUE;
				rejected := FALSE
			ELSIF Services.Ticks() < deadline THEN
				Wait(20)
			END
		UNTIL accepted OR (Services.Ticks() >= deadline);

		IF expectReject[0] = '1' THEN
			UnitAssert.True(rejected, "no client completed the handshake");
			srv.Close
		ELSE
			UnitAssert.True(accepted, "server accepted a client");
			UnitAssert.EqualInt(0, CryptoTLS.LastCertError, "client cert error");
			IF accepted THEN
				WITH cl: CryptoTLS.Connection DO
					tls := cl
				END;
				got := 0;
				REPEAT
					read := 0;
					tls.Receive(buf, got, LEN(buf) - got, 1, read, res);
					INC(got, read)
				UNTIL (got >= reqLen) OR (read = 0);
				IF badApp[0] = '1' THEN
					(* Запись после рукопожатия (CCS, пустая или из нулей)
					   отвергнута: до приложения не дошёл запрос, и
					   соединение закрыто. *)
					UnitAssert.True(got < reqLen, "corrupted record rejected");
					UnitAssert.True(tls.State() # TCP.Established,
						"connection closed after corrupted record")
				ELSIF got > 0 THEN
					FOR i := 0 TO got - 1 DO
						rev[i] := buf[got - 1 - i]
					END;
					tls.Send(rev, 0, got, TRUE, written);
					UnitAssert.EqualInt(0, written, "server sends the reply")
				END;
				tls.Close
			END
		END
	END TestProcServer;

END Crypto_devTLSServer.
