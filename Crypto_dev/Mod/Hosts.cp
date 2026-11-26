MODULE Crypto_devHosts;
(** Ручной прогон TLS-клиента по публичным HTTPS-хостам.

	Не входит в tools/test.sh: нужна сеть. Запускается скриптом
	tools/test-crypto-hosts.sh. Внешние переменные:
	CRYPTO_HOSTS — список host[:port] через запятую;
	CRYPTO_CA — PEM-файл доверенных корней (по умолчанию системный).

	Для каждого хоста печатает версию, набор, группу ECDHE, схему подписи
	и первую строку ответа HTTP. *)

	IMPORT Dialog, CryptoTLS, TCP := CryptoAosTCP, Services, StdLog;

	PROCEDURE GetPar (IN name : ARRAY OF CHAR; VAR value : ARRAY OF CHAR);
		VAR res : INTEGER;
	BEGIN
		value[0] := 0X;
		Dialog.GetPar(name, value, res);
		IF res # 0 THEN value[0] := 0X END
	END GetPar;

	PROCEDURE Len (IN s : ARRAY OF CHAR) : INTEGER;
		VAR i : INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) DO INC(i) END;
		RETURN i
	END Len;

	PROCEDURE SuiteName (suite : INTEGER; VAR name : ARRAY OF CHAR);
	BEGIN
		CASE suite OF
			0C02BH : name := "ECDHE-ECDSA-AES128-GCM-SHA256"
		|       0C02CH : name := "ECDHE-ECDSA-AES256-GCM-SHA384"
		|       0C02FH : name := "ECDHE-RSA-AES128-GCM-SHA256"
		|       0C030H : name := "ECDHE-RSA-AES256-GCM-SHA384"
		|       0CCA8H : name := "ECDHE-RSA-CHACHA20-POLY1305-SHA256"
		|       0CCA9H : name := "ECDHE-ECDSA-CHACHA20-POLY1305-SHA256"
		ELSE
			name := "other"
		END
	END SuiteName;

	PROCEDURE GroupName (id : INTEGER; VAR name : ARRAY OF CHAR);
	BEGIN
		CASE id OF
			CryptoTLS.GroupX25519 : name := "x25519"
		|       CryptoTLS.GroupSecp256r1 : name := "secp256r1"
		|       CryptoTLS.GroupSecp384r1 : name := "secp384r1"
		ELSE
			name := "none"
		END
	END GroupName;

	PROCEDURE SchemeName (scheme : INTEGER; VAR name : ARRAY OF CHAR);
	BEGIN
		CASE scheme OF
			CryptoTLS.SigRsaPkcs1Sha256 : name := "rsa_pkcs1_sha256"
		|       CryptoTLS.SigRsaPssRsaeSha256 : name := "rsa_pss_rsae_sha256"
		|       CryptoTLS.SigRsaPssRsaeSha384 : name := "rsa_pss_rsae_sha384"
		|       CryptoTLS.SigEcdsaP256Sha256 : name := "ecdsa_secp256r1_sha256"
		|       CryptoTLS.SigEcdsaP384Sha384 : name := "ecdsa_secp384r1_sha384"
		|       CryptoTLS.SigEd25519 : name := "ed25519"
		ELSE
			name := "none"
		END
	END SchemeName;

	PROCEDURE AppendCh (VAR out : ARRAY OF SHORTCHAR; VAR pos : INTEGER; ch : SHORTCHAR);
	BEGIN
		out[pos] := ch;
		INC(pos)
	END AppendCh;

	PROCEDURE AppendS (VAR out : ARRAY OF SHORTCHAR; VAR pos : INTEGER;
			IN s : ARRAY OF CHAR);
		VAR i : INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) DO
			out[pos] := SHORT(CHR(ORD(s[i])));
			INC(i); INC(pos)
		END
	END AppendS;

	PROCEDURE AppendHost (VAR out : ARRAY OF SHORTCHAR; VAR pos : INTEGER;
			IN s : ARRAY OF CHAR);
	BEGIN
		AppendS(out, pos, s)
	END AppendHost;

	PROCEDURE AppendNum (VAR out : ARRAY OF SHORTCHAR; VAR pos : INTEGER; n : INTEGER);
		VAR d : ARRAY 10 OF CHAR; k : INTEGER;
	BEGIN
		k := 0;
		IF n = 0 THEN
			AppendCh(out, pos, SHORT(CHR(ORD("0"))))
		ELSE
			WHILE n > 0 DO
				d[k] := CHR(ORD("0") + n MOD 10);
				n := n DIV 10;
				INC(k)
			END;
			WHILE k > 0 DO
				DEC(k);
				AppendCh(out, pos, SHORT(CHR(ORD(d[k]))))
			END
		END
	END AppendNum;

	PROCEDURE PutS (VAR out : ARRAY OF CHAR; VAR pos : INTEGER; IN s : ARRAY OF CHAR);
		VAR i : INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) DO
			out[pos] := s[i];
			INC(i); INC(pos)
		END
	END PutS;

	PROCEDURE PutCh (VAR out : ARRAY OF CHAR; VAR pos : INTEGER; ch : CHAR);
	BEGIN
		out[pos] := ch;
		INC(pos)
	END PutCh;

	PROCEDURE PutNum (VAR out : ARRAY OF CHAR; VAR pos : INTEGER; n : INTEGER);
		VAR d : ARRAY 10 OF CHAR; k : INTEGER;
	BEGIN
		k := 0;
		IF n = 0 THEN
			PutCh(out, pos, "0")
		ELSE
			WHILE n > 0 DO
				d[k] := CHR(ORD("0") + n MOD 10);
				n := n DIV 10;
				INC(k)
			END;
			WHILE k > 0 DO
				DEC(k);
				PutCh(out, pos, d[k])
			END
		END
	END PutNum;

	PROCEDURE Do*;
		VAR hosts, ca : ARRAY 1024 OF CHAR;
			host, host2, addr, suiteName, groupName, schemeName : ARRAY 256 OF CHAR;
			req : ARRAY 512 OF SHORTCHAR;
			buf : ARRAY 4096 OF SHORTCHAR;
			ctx : CryptoTLS.Context;
			con : CryptoTLS.Connection;
			i, j, n, port, res, written, read, got, pos : INTEGER;
			deadline : LONGINT;
			ok, done : BOOLEAN;
	BEGIN
		GetPar("CRYPTO_HOSTS", hosts);
		GetPar("CRYPTO_CA", ca);
		IF ca[0] = 0X THEN
			ca := "/etc/ssl/certs/ca-certificates.crt"
		END;
		ctx := CryptoTLS.NewContext();
		ok := ctx.AddTrustedFile(ca);
		StdLog.String("CA: "); StdLog.String(ca);
		StdLog.String(" loaded: "); StdLog.Bool(ok); StdLog.Ln;
		i := 0;
		WHILE i < Len(hosts) DO
			n := 0;
			WHILE (i < Len(hosts)) & (hosts[i] # ",") & (hosts[i] # " ") DO
				host[n] := hosts[i];
				INC(i); INC(n)
			END;
			host[n] := 0X;
			WHILE (i < Len(hosts)) & ((hosts[i] = ",") OR (hosts[i] = " ")) DO INC(i) END;
			IF n > 0 THEN
				j := 0; port := 443;
				WHILE (j < n) & (host[j] # ":") DO
					host2[j] := host[j]; INC(j)
				END;
				host2[j] := 0X;
				IF j < n THEN
					port := 0;
					INC(j);
					WHILE j < n DO
						port := 10 * port + (ORD(host[j]) - ORD("0"));
						INC(j)
					END
				END;
				pos := 0;
				PutS(addr, pos, host2);
				PutCh(addr, pos, ":");
				PutNum(addr, pos, port);
				addr[pos] := 0X;

				con := CryptoTLS.GetConnection();
				con.SetContext(ctx);
				con.Open("0.0.0.0:0", addr, res);
				StdLog.String("host "); StdLog.String(host2);
				IF (res = TCP.Ok) & (con.State() = TCP.Established) THEN
					SuiteName(con.NegotiatedSuite(), suiteName);
					GroupName(con.NegotiatedGroup(), groupName);
					SchemeName(con.NegotiatedScheme(), schemeName);
					StdLog.String(" OK version=");
					StdLog.Int(con.NegotiatedVersion() DIV 256);
					StdLog.String(".");
					StdLog.Int(con.NegotiatedVersion() MOD 256);
					StdLog.String(" suite="); StdLog.String(suiteName);
					StdLog.String(" group="); StdLog.String(groupName);
					StdLog.String(" sig="); StdLog.String(schemeName);
					StdLog.Ln;
					pos := 0;
					AppendS(req, pos, "GET / HTTP/1.0");
					AppendCh(req, pos, SHORT(CHR(13)));
					AppendCh(req, pos, SHORT(CHR(10)));
					AppendS(req, pos, "Host: ");
					AppendHost(req, pos, host2);
					AppendCh(req, pos, SHORT(CHR(13)));
					AppendCh(req, pos, SHORT(CHR(10)));
					AppendCh(req, pos, SHORT(CHR(13)));
					AppendCh(req, pos, SHORT(CHR(10)));
					con.Send(req, 0, pos, TRUE, written);
					got := 0;
					done := FALSE;
					deadline := Services.Ticks() + 5000;
					WHILE ~done & (got < LEN(buf) - 1) & (Services.Ticks() <= deadline) DO
						read := 0;
						con.Receive(buf, got, LEN(buf) - 1 - got, 1, read, res);
						IF read = 0 THEN
							done := TRUE
						ELSE
							INC(got, read);
							IF (got >= 2) & (buf[got - 1] = SHORT(CHR(10))) THEN
								done := TRUE
							END
						END
					END;
					j := 0;
					WHILE (j < got) & (buf[j] # SHORT(CHR(13))) & (buf[j] # SHORT(CHR(10))) DO INC(j) END;
					buf[j] := 0X;
					StdLog.String("  reply: "); StdLog.String(LONG(buf)); StdLog.Ln
				ELSE
					StdLog.String(" FAILED res="); StdLog.Int(res);
					StdLog.String(" certError="); StdLog.Int(CryptoTLS.LastCertError);
					StdLog.Ln
				END;
				con.Close
			END
		END
	END Do;

END Crypto_devHosts.
