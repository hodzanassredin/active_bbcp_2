MODULE Crypto_devTLS;
(** Рукопожатие TLS 1.2 и обмен строкой с локальным сервером.

	Адрес сервера приходит внешней переменной CRYPTO_TLS_ADDR
	(host:port). Ожидается openssl s_server с ключом -rev: клиент шлёт
	"hello", сервер возвращает перевёрнутую строку. Без сервера тест не
	запускается: его вызывает tools/test-crypto-tls.sh, а не
	tools/test.sh. *)

	IMPORT Dialog, CommStreams, UnitAssert;

	CONST
		reqLen = 6;
		repLen = 6;

	PROCEDURE EqualBytes (IN a, b: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END EqualBytes;

	PROCEDURE TestProcHandshake*;
		VAR addr: ARRAY 128 OF CHAR;
			req, want, buf: ARRAY 16 OF BYTE;
			res, written, read, got: INTEGER;
			s: CommStreams.Stream;
	BEGIN
		Dialog.GetPar("CRYPTO_TLS_ADDR", addr, res);
		UnitAssert.EqualInt(0, res, "CRYPTO_TLS_ADDR is set");
		IF res = 0 THEN
			req[0] := 68H; req[1] := 65H; req[2] := 6CH;
			req[3] := 6CH; req[4] := 6FH; req[5] := 0AH;
			want[0] := 6FH; want[1] := 6CH; want[2] := 6CH;
			want[3] := 65H; want[4] := 68H; want[5] := 0AH;
			s := NIL;
			CommStreams.NewStream("CryptoTLSStream", "0.0.0.0:0", addr, s, res);
			UnitAssert.EqualInt(0, res, "TLS stream opened");
			UnitAssert.NotNil(s, "TLS stream is not NIL");
			IF s # NIL THEN
				s.WriteBytes(req, 0, reqLen, written);
				UnitAssert.EqualInt(reqLen, written, "TLS stream writes the request");
				got := 0;
				REPEAT
					read := 0;
					s.ReadBytes(buf, got, LEN(buf) - got, read);
					INC(got, read)
				UNTIL (got >= repLen) OR (read = 0);
				UnitAssert.EqualInt(repLen, got, "TLS stream reads the reply");
				UnitAssert.True(EqualBytes(buf, want, repLen), "TLS reply is reversed request");
				s.Close
			END
		END
	END TestProcHandshake;

END Crypto_devTLS.
