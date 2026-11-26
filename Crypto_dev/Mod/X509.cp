MODULE Crypto_devX509;
(** Тесты X.509: разбор, цепочка, сроки, имя, повреждённые данные.

	Сертификаты в Crypto_dev/Vectors/x509 сгенерированы для этого
	репозитория (см. README): корневой CA, лист с SAN localhost/127.0.0.1
	и просроченный лист. Отдельно проверяется, что обрезанный и мусорный
	DER не роняет процесс, а возвращает ошибку. *)

	IMPORT X509 := CryptoX509, AO := CryptoAosCompat, UnitAssert, BIT := CryptoAosBIT,
		Streams := CryptoAosStreams, ASN1 := CryptoASN1, U := CryptoUtils, Data := Crypto_devX509Data;

	CONST
		CaPem = "-----BEGIN CERTIFICATE-----MIIDIzCCAgugAwIBAgIUDhS4Cuef5MvlufV1+Ddko/TYaaowDQYJKoZIhvcNAQELBQAwGTEXMBUGA1UEAwwOQ3J5cHRvIFRlc3QgQ0EwHhcNMjYxMTI2MTExMjU0WhcNMzYxMTIzMTExMjU0WjAZMRcwF" +
		"QYDVQQDDA5DcnlwdG8gVGVzdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKQKnXJJU8tKQhVcSPDdm2jrjhYsjNRg3XKpZupjNhINYGD4UWbY/D3j/pdMjXhWoNiXccjxX5A4L64ROR/P/UbCL1WU8SvyMQSxYf2cv9cdu" +
		"Z6DamwBWH8/3IG+DcC/KnouRYwd7x4Cu1iqhNKsYdPxoqr5zJeKvilnw29GruvYq3+NTC/6ErOSK4TmYsW3hkCvUgTTA/3g+bOMGIZzqEQ0Z1/TVkG9YdAFp64KQV3+Vh2Mw1KnDlY+m49s3I6B0B/Kp358ouoHjJsq4NAEDkW5rCjgHNiMo" +
		"CATy07jAjbDnSp8LJxTufVtpACeOHAWF98NXVzDHrWTsQWZ3KNlov0CAwEAAaNjMGEwHQYDVR0OBBYEFB4kUdQLwDpa3dtJPkf1a/f1Ae46MB8GA1UdIwQYMBaAFB4kUdQLwDpa3dtJPkf1a/f1Ae46MA8GA1UdEwEB/wQFMAMBAf8wDgYDV" +
		"R0PAQH/BAQDAgEGMA0GCSqGSIb3DQEBCwUAA4IBAQAnGrcCaS7E6cxFCaK1UXztTSBjt2Ha/NJDjaNC+PJ5GgyJXJeVFS02uo3TRz4V/aaC+Xn5MzSZIsxF2G4u5iS6R8vqy3Q0uFgew3Ygy74UuR6Z4qTEztEHtSg5Kz7aWDPznL31j//dN" +
		"uYpiqXaC+jFN6F6YPqcj8Dqb9BOlrxcTdpm5ZCzxVxXF9IW5/boGSbDZ7aAoHaa9K5oSpLd/QFR5+Gj97JG7alENt+ywTNNJGFS2Ky2ND+afCaZryiPhC83ItnqNlPTfdsGwjsi2b9V0Hgtc3Ze4rJZOt9veJO+fKOHAls8JBXqh3aA2dkoK" +
		"RSJrTSbWjht1XVwS/1xLBtF-----END CERTIFICATE-----";
		LeafPem = "-----BEGIN CERTIFICATE-----MIIDTjCCAjagAwIBAgIUCXl2q9dwfCPSIZP8A7eV2QOigI8wDQYJKoZIhvcNAQELBQAwGTEXMBUGA1UEAwwOQ3J5cHRvIFRlc3QgQ0EwHhcNMjYxMTI2MTExMjU1WhcNMzYxMTIzMTExMjU1WjAUMRIwE" +
		"AYDVQQDDAlsb2NhbGhvc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCaX8TWhgdF1p8Y5fjU6JO8Ll49etkXGsZ9TzxX2vtVukjT6bHHCBpgzbLtmjGGDejw/EIIMy86+GEYNEJHmod14DTjalFE45xTIY4PcMcKcUN/44Jt+" +
		"ooSXbe40HWMYQper8whodBY7+ZI9/WzpUgspLBQk5+84DjvYljt1JnAkPtv7XFNW9ai0pcTn1vb7GzRqO2p4TdTEjRWjq3cTP23POwMtcJFDf2MJi7oN94e5k7K/thX8SL5b0Y/Tjn3+zRAHaDlTuEz/ZNmuUVMqjkC45GmpXdmqSgPiMjLJ" +
		"XCzd5MxjQt5O+XsCyymQ+9qmbotm4r0w+qqfIocB1+7WpvZAgMBAAGjgZIwgY8wGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/BAQDAgWgMBMGA1UdJQQMMAoGCCsGAQUFBwMBMB0GA1UdDgQWB" +
		"BSHPGf72PvttB7hhDQdMIEsN6FcNzAfBgNVHSMEGDAWgBQeJFHUC8A6Wt3bST5H9Wv39QHuOjANBgkqhkiG9w0BAQsFAAOCAQEACz6flErHIBJRNLDHMkwd8AKVQ6k3Z+0hgh42pAnjlenYXdu1A8PzW/KQeCyxeH3D/ao6HcuY9eo/G5yFf" +
		"haw5XOVRj4GuwlfiCw1i8nnjQksn551guiIFm0skN8034//HMeEjstYXIQBo2UQ3ZbHUi+STsEBRik/XnmCnQqjydinL8FzcwBWLdcoOHed32sGG3WFwoZF3lyvpw4MxI4a9ZkhMYWxRrm2GoV4e78huPYYBZWvvn+332ijvackxi8gBAuye" +
		"96cPU8NIGUU4aeKRBbAFXmdzOb27JxkpSLAKR3ARKpiWadYocrCespKlBDZ0K2aXvu3sy0acgd56Q0VUw==-----END CERTIFICATE-----";
		ExpiredPem = "-----BEGIN CERTIFICATE-----MIIDLDCCAhSgAwIBAgIUCXl2q9dwfCPSIZP8A7eV2QOigJAwDQYJKoZIhvcNAQELBQAwGTEXMBUGA1UEAwwOQ3J5cHRvIFRlc3QgQ0EwHhcNMjYxMTI2MTExMjU1WhcNMjYxMTI2MTExMjU1WjAUMRIwE" +
		"AYDVQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDq0cvl3dEjFsBuDnbJQBdAjy0betFO9chitRDr0tPxiP6iWMPyTQ0bUKo1OmUa7ov+xuaMZ6LYUu0gsyTv5k5M2p78O+Rs/TuxQOukJp84XlNO0cq+K" +
		"yXFuvrZbpsnvMlHGKO3voYLIMzMOJSimeo9B8jpcqlyhvrfuHF8/r88mDS5nDeWjFpzcSCT3vBuXzz+bNItgMbaPPZ1aqXfgJSPKRJj+igmqmcCBONPUn5BWmbV8jrd7PL43K6VDJhPWguFbHueVCsu3/HSbvwOQFA3wMxLDNYMLgzYfdf8p" +
		"dGhvlu4MioXRzrH5hEeLmpnuqEfb4ujAPb3fAsH9nW/UgLrAgMBAAGjcTBvMA8GA1UdEQQIMAaHBH8AAAEwDAYDVR0TAQH/BAIwADAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0OBBYEFJdn1oIUyxVPbBaPBWdixVpvT5PSMB8GA1UdIwQYMBaAF" +
		"B4kUdQLwDpa3dtJPkf1a/f1Ae46MA0GCSqGSIb3DQEBCwUAA4IBAQB1ReY+URyv1XyacVVM3o1fm4NQHUj3Ii1gQqaXbHD9vl7fmJw3tlf4I38Yxo9ctmD2qQeSwyOq2sbCLEzX+aZ0ljezP8xLvv07LiOxR9qUewhtjlpIYPD9XYmY8pzKv" +
		"NmH3Xw2gQRYI52O2DAL7iWnC/LW6lSyOIn4jMfdUc0YCBpG0TpkourJwZfOmNJFgcEqK+eiAb+PJSnOkWcaxaVvyS3Rj4ltcFq6gwdiph/lAuI3K7YNMYgHfsduaRD2N9ckNw9jibIbpCRVb2iWoikaCQ3lgYQvRy+HWYf04yT7n2YY8mXH8" +
		"fbfG3J/uz4/Uv2X3nJA9VAa92oYAKlg5aVZ-----END CERTIFICATE-----";

	PROCEDURE TestParse*;
		VAR leaf: X509.Certificate; res: AO.SIGNED32;
	BEGIN
		leaf := X509.ReadPemChain(LeafPem, res);
		UnitAssert.NotNil(leaf, "leaf parsed");
		UnitAssert.EqualInt(0, res, "leaf parse status");
		IF leaf # NIL THEN
			UnitAssert.True(leaf.subject.commonName # NIL, "subject CN");
			IF leaf.subject.commonName # NIL THEN
				UnitAssert.True(leaf.subject.commonName^ = "localhost", "CN value")
			END;
			UnitAssert.True(X509.MatchHost(leaf, "127.0.0.1"), "IP SAN");
			UnitAssert.True(X509.MatchHost(leaf, "localhost"), "DNS SAN");
			UnitAssert.True(~X509.MatchHost(leaf, "example.com"), "wrong host rejected")
		END
	END TestParse;

	PROCEDURE TestChain*;
		VAR leaf, roots: X509.Certificate; res, err: AO.SIGNED32;
	BEGIN
		leaf := X509.ReadPemChain(LeafPem, res);
		roots := X509.ReadPemChain(CaPem, res);
		UnitAssert.NotNil(leaf, "leaf");
		UnitAssert.NotNil(roots, "root");
		IF (leaf # NIL) & (roots # NIL) THEN
			UnitAssert.True(X509.VerifyChain(leaf, leaf, roots, "127.0.0.1", {}, err), "chain and IP");
			UnitAssert.EqualInt(X509.Ok, err, "chain status");
			UnitAssert.True(~X509.VerifyChain(leaf, leaf, roots, "example.com", {}, err), "wrong host");
			UnitAssert.EqualInt(X509.ErrHostname, err, "hostname error");
			UnitAssert.True(~X509.VerifyChain(leaf, leaf, NIL, "127.0.0.1", {}, err), "untrusted");
			UnitAssert.EqualInt(X509.ErrUntrusted, err, "untrusted error")
		END
	END TestChain;

	PROCEDURE Load3 (IN leafPem, midPem, rootPem : ARRAY OF SHORTCHAR;
			VAR leaf, mid, root : X509.Certificate): BOOLEAN;
		VAR res : AO.SIGNED32;
	BEGIN
		leaf := X509.ReadPemChain(leafPem, res);
		mid := X509.ReadPemChain(midPem, res);
		root := X509.ReadPemChain(rootPem, res);
		RETURN (leaf # NIL) & (mid # NIL) & (root # NIL)
	END Load3;

	PROCEDURE CheckChain (leaf, chain, roots : X509.Certificate; IN host : ARRAY OF SHORTCHAR;
			want : AO.SIGNED32; IN msg : ARRAY OF CHAR);
		VAR err : AO.SIGNED32; ok : BOOLEAN;
	BEGIN
		ok := X509.VerifyChain(leaf, chain, roots, host, {}, err);
		IF want = X509.Ok THEN
			UnitAssert.True(ok, msg);
			UnitAssert.EqualInt(X509.Ok, err, msg)
		ELSE
			UnitAssert.True(~ok, msg);
			UnitAssert.EqualInt(want, err, msg)
		END
	END CheckChain;

	PROCEDURE TestIntermediate*;
		(* Корень -> промежуточный CA -> лист: полный путь принимается;
		   непонятное НЕкритичное расширение у промежуточного не мешает. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.ChainLeafPem, Data.IntermediatePem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "chain.example", X509.Ok, "intermediate chain accepted");
			CheckChain(leaf, leaf, root, "other.example", X509.ErrHostname, "wrong host in chain")
		ELSE
			UnitAssert.Fail("chain fixtures parsed")
		END
	END TestIntermediate;

	PROCEDURE TestNoCA*;
		(* Классическая атака: лист с basicConstraints CA:FALSE подписывает
		   другой лист. Издатель обязан быть CA. *)
		VAR leaf, signer, root : X509.Certificate;
	BEGIN
		IF Load3(Data.VictimPem, Data.BadSignerPem, Data.RootPem, leaf, signer, root) THEN
			leaf.next := signer;
			CheckChain(leaf, leaf, root, "victim.example", X509.ErrNoCA, "CA:FALSE issuer rejected")
		ELSE
			UnitAssert.Fail("noCA fixtures parsed")
		END
	END TestNoCA;

	PROCEDURE TestNoBasicConstraints*;
		(* У промежуточного нет basicConstraints вовсе: isCA по умолчанию FALSE. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.NoBcLeafPem, Data.NoBcPem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "nobc.example", X509.ErrNoCA, "issuer without BC rejected")
		ELSE
			UnitAssert.Fail("noBC fixtures parsed")
		END
	END TestNoBasicConstraints;

	PROCEDURE TestKeyUsage*;
		(* keyUsage есть, но keyCertSign в нём нет: подписывать нельзя. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.KuLeafPem, Data.KuPem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "kuno.example", X509.ErrNoCA, "keyUsage without keyCertSign rejected")
		ELSE
			UnitAssert.Fail("keyUsage fixtures parsed")
		END
	END TestKeyUsage;

	PROCEDURE TestPathLen*;
		(* pathLenConstraint = 0, а под сертификатом ещё один CA —
		   путь длиннее разрешённого. *)
		VAR leaf, sub, mid, root : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		IF Load3(Data.Path0LeafPem, Data.Path0SubPem, Data.Path0Pem, leaf, sub, mid) THEN
			root := X509.ReadPemChain(Data.RootPem, res);
			UnitAssert.NotNil(root, "pathLen root parsed");
			IF root # NIL THEN
				leaf.next := sub;
				sub.next := mid;
				CheckChain(leaf, leaf, root, "pathlen.example", X509.ErrNoCA, "pathLen 0 exceeded rejected")
			END
		ELSE
			UnitAssert.Fail("pathLen fixtures parsed")
		END
	END TestPathLen;

	PROCEDURE TestPathLenAccept*;
		(* pathLenConstraint = 0 и лист прямо под CA: ноль промежуточных
		   разрешён, граница не должна быть слишком строгой. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.Path0DirectPem, Data.Path0Pem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "pathlen-direct.example", X509.Ok, "pathLen 0 with direct leaf accepted")
		ELSE
			UnitAssert.Fail("pathLen direct fixtures parsed")
		END
	END TestPathLenAccept;

	PROCEDURE TestCriticalExtension*;
		(* Неизвестное критичное расширение у промежуточного — отказ. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.CritLeafPem, Data.CritPem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "crit.example", X509.ErrUnsupported, "unknown critical extension rejected")
		ELSE
			UnitAssert.Fail("critical extension fixtures parsed")
		END
	END TestCriticalExtension;

	PROCEDURE TestCriticalLeaf*;
		(* Неизвестное критичное расширение у самого листа тоже отказ. *)
		VAR leaf, root : X509.Certificate;
	BEGIN
		IF Load3(Data.CritOnlyLeafPem, Data.RootPem, Data.RootPem, leaf, root, root) THEN
			CheckChain(leaf, leaf, root, "critleaf.example", X509.ErrUnsupported, "unknown critical extension on leaf")
		ELSE
			UnitAssert.Fail("critical leaf parsed")
		END
	END TestCriticalLeaf;

	PROCEDURE TestLegacyVerify*;
		(* Старый Verify проверяет только сроки; цепочку проверяет VerifyChain. *)
		VAR leaf, expired : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		leaf := X509.ReadPemChain(LeafPem, res);
		UnitAssert.NotNil(leaf, "legacy verify: leaf parsed");
		IF leaf # NIL THEN
			UnitAssert.True(X509.Verify(leaf, {}), "legacy verify accepts valid dates")
		END;
		expired := X509.ReadPemChain(ExpiredPem, res);
		UnitAssert.NotNil(expired, "legacy verify: expired parsed");
		IF expired # NIL THEN
			UnitAssert.True(~X509.Verify(expired, {}), "legacy verify rejects expired")
		END
	END TestLegacyVerify;

	PROCEDURE TestExpiredIntermediate*;
		(* Лист действителен, промежуточный CA просрочен: сроки каждого звена. *)
		VAR leaf, mid, root : X509.Certificate;
	BEGIN
		IF Load3(Data.ExpiredIntLeafPem, Data.ExpiredIntPem, Data.RootPem, leaf, mid, root) THEN
			leaf.next := mid;
			CheckChain(leaf, leaf, root, "expiredint.example", X509.ErrExpired, "expired intermediate rejected")
		ELSE
			UnitAssert.Fail("expired intermediate fixtures parsed")
		END
	END TestExpiredIntermediate;

	PROCEDURE TestExpiredRoot*;
		(* Сроки проверяются и у доверенного корня. *)
		VAR leaf, root : X509.Certificate;
	BEGIN
		IF Load3(Data.ExpiredRootLeafPem, Data.ExpiredRootPem, Data.ExpiredRootPem, leaf, root, root) THEN
			CheckChain(leaf, leaf, root, "expiredroot.example", X509.ErrExpired, "expired trusted root rejected")
		ELSE
			UnitAssert.Fail("expired root fixtures parsed")
		END
	END TestExpiredRoot;

	PROCEDURE TestUntrustedRoot*;
		(* Корень только из доверенного набора: поддельный корень с тем же
		   именем, что доверенный, но своим ключом не принимается. *)
		VAR leaf, forged, trust : X509.Certificate;
	BEGIN
		IF Load3(Data.ForgedLeafPem, Data.ForgedRootPem, Data.RootPem, leaf, forged, trust) THEN
			leaf.next := forged;
			CheckChain(leaf, leaf, trust, "forged.example", X509.ErrBadSignature, "forged same-name root rejected");
			CheckChain(leaf, leaf, NIL, "forged.example", X509.ErrUntrusted, "chain without trusted roots rejected")
		ELSE
			UnitAssert.Fail("forged root fixtures parsed")
		END
	END TestUntrustedRoot;

	PROCEDURE TestCycle*;
		(* A подписан B, B подписан A (тот же ключ). Проверка обязана
		   завершиться отказом по пределу глубины, а не зациклиться. *)
		VAR leaf, a, b, root : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		IF Load3(Data.CycleLeafPem, Data.CycleAPem, Data.CycleBPem, leaf, a, b) THEN
			root := X509.ReadPemChain(Data.RootPem, res);
			leaf.next := a;
			a.next := b;
			CheckChain(leaf, leaf, root, "cycle.example", X509.ErrUntrusted, "cycle rejected")
		ELSE
			UnitAssert.Fail("cycle fixtures parsed")
		END
	END TestCycle;

	PROCEDURE NewSanCert (VAR cert : X509.Certificate);
	BEGIN
		NEW(cert);
		cert.sanCount := 0;
		cert.isCA := FALSE
	END NewSanCert;

	PROCEDURE AddDns (cert : X509.Certificate; IN name : ARRAY OF SHORTCHAR);
		VAR i, n : AO.SIZE; p : POINTER TO ARRAY OF SHORTCHAR;
	BEGIN
		n := 0;
		WHILE (n < LEN(name)) & (name[n] # 0X) DO INC(n) END;
		NEW(p, n);
		FOR i := 0 TO n - 1 DO p^[i] := name[i] END;
		cert.sanValue[cert.sanCount] := p;
		cert.sanLen[cert.sanCount] := n;
		cert.sanIsIP[cert.sanCount] := FALSE;
		INC(cert.sanCount)
	END AddDns;

	PROCEDURE AddIp (cert : X509.Certificate; a, b, c, d : AO.SIGNED32);
		VAR p : POINTER TO ARRAY OF SHORTCHAR;
	BEGIN
		NEW(p, 4);
		p^[0] := SHORT(CHR(a));
		p^[1] := SHORT(CHR(b));
		p^[2] := SHORT(CHR(c));
		p^[3] := SHORT(CHR(d));
		cert.sanValue[cert.sanCount] := p;
		cert.sanLen[cert.sanCount] := 4;
		cert.sanIsIP[cert.sanCount] := TRUE;
		INC(cert.sanCount)
	END AddIp;

	PROCEDURE TestWildcard*;
		(* Шаблон '*' — ровно одна метка и не домен верхнего уровня
		   (RFC 6125, 6.4.3). *)
		VAR cert : X509.Certificate;
	BEGIN
		NewSanCert(cert);
		AddDns(cert, "*.example.com");
		UnitAssert.True(X509.MatchHost(cert, "a.example.com"), "wildcard one label");
		UnitAssert.True(~X509.MatchHost(cert, "a.b.example.com"), "wildcard not two labels");
		UnitAssert.True(~X509.MatchHost(cert, "example.com"), "wildcard needs label");
		UnitAssert.True(~X509.MatchHost(cert, ".example.com"), "wildcard empty label");
		UnitAssert.True(~X509.MatchHost(cert, "x.example.com.evil"), "wildcard suffix");
		UnitAssert.True(~X509.MatchHost(cert, "badexample.com"), "wildcard boundary dot");
		UnitAssert.True(~X509.MatchHost(cert, "a.*.example.com"), "pattern not in host");
		UnitAssert.True(X509.MatchHost(cert, "A.EXAMPLE.COM"), "wildcard case-insensitive");
		NewSanCert(cert);
		AddDns(cert, "*.com");
		UnitAssert.True(~X509.MatchHost(cert, "example.com"), "wildcard over TLD rejected");
		UnitAssert.True(~X509.MatchHost(cert, "com"), "wildcard over TLD bare rejected");
		NewSanCert(cert);
		AddDns(cert, "*.co.uk");
		(* Политики публичных суффиксов (PSL) в модуле нет: по RFC 6125
		   шаблон над двумя метками допустим. *)
		UnitAssert.True(X509.MatchHost(cert, "a.co.uk"), "wildcard over two-label suffix");
		NewSanCert(cert);
		AddDns(cert, "*.xn--e1afmkfd.xn--p1ai");
		UnitAssert.True(X509.MatchHost(cert, "a.xn--e1afmkfd.xn--p1ai"), "wildcard over IDN suffix");
		NewSanCert(cert);
		AddDns(cert, "*.xn--p1ai");
		UnitAssert.True(~X509.MatchHost(cert, "a.xn--p1ai"), "wildcard over IDN TLD rejected");
		NewSanCert(cert);
		AddDns(cert, "xn--*.example.com");
		UnitAssert.True(~X509.MatchHost(cert, "xn--foo.example.com"), "wildcard inside A-label rejected")
	END TestWildcard;

	PROCEDURE TestHostIp*;
		(* IP и DNS не смешиваются; регистр букв не важен. *)
		VAR cert : X509.Certificate;
	BEGIN
		NewSanCert(cert);
		AddIp(cert, 127, 0, 0, 1);
		UnitAssert.True(X509.MatchHost(cert, "127.0.0.1"), "IP SAN exact");
		UnitAssert.True(~X509.MatchHost(cert, "127.0.0.2"), "IP SAN other address");
		UnitAssert.True(~X509.MatchHost(cert, "localhost"), "IP SAN is not DNS name");
		NewSanCert(cert);
		AddDns(cert, "127.0.0.1");
		UnitAssert.True(~X509.MatchHost(cert, "127.0.0.1"), "DNS SAN is not IP");
		NewSanCert(cert);
		AddDns(cert, "LOCALHOST");
		UnitAssert.True(X509.MatchHost(cert, "localhost"), "DNS SAN case-insensitive");
		UnitAssert.True(X509.MatchHost(cert, "LocalHost"), "DNS SAN mixed case");
		NewSanCert(cert);
		AddDns(cert, "localhost");
		AddIp(cert, 127, 0, 0, 1);
		UnitAssert.True(X509.MatchHost(cert, "127.0.0.1"), "mixed SAN IP");
		UnitAssert.True(X509.MatchHost(cert, "localhost"), "mixed SAN DNS");
		UnitAssert.True(~X509.MatchHost(cert, ""), "empty host rejected")
	END TestHostIp;

	PROCEDURE TestCnFallback*;
		(* CN используется, только если SAN нет (RFC 6125, 6.4.4). *)
		VAR cert : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		cert := X509.ReadPemChain(Data.CnLeafPem, res);
		UnitAssert.NotNil(cert, "CN leaf parsed");
		IF cert # NIL THEN
			UnitAssert.True(X509.MatchHost(cert, "cn.example"), "CN fallback")
		END;
		cert := X509.ReadPemChain(Data.CnWildPem, res);
		UnitAssert.NotNil(cert, "CN wildcard parsed");
		IF cert # NIL THEN
			UnitAssert.True(X509.MatchHost(cert, "a.example.org"), "CN wildcard one label");
			UnitAssert.True(~X509.MatchHost(cert, "a.b.example.org"), "CN wildcard not two labels")
		END;
		cert := X509.ReadPemChain(Data.CnSanShadowPem, res);
		UnitAssert.NotNil(cert, "CN shadowed by SAN parsed");
		IF cert # NIL THEN
			UnitAssert.True(~X509.MatchHost(cert, "cn.example"), "SAN shadows CN");
			UnitAssert.True(X509.MatchHost(cert, "other.example"), "SAN used when present")
		END
	END TestCnFallback;

	PROCEDURE TestExpired*;
		VAR leaf, roots: X509.Certificate; res, err: AO.SIGNED32;
	BEGIN
		leaf := X509.ReadPemChain(ExpiredPem, res);
		roots := X509.ReadPemChain(CaPem, res);
		UnitAssert.NotNil(leaf, "expired leaf parsed");
		IF (leaf # NIL) & (roots # NIL) THEN
			UnitAssert.True(~X509.VerifyChain(leaf, leaf, roots, "127.0.0.1", {}, err), "expired rejected");
			UnitAssert.EqualInt(X509.ErrExpired, err, "expired error")
		END
	END TestExpired;

	PROCEDURE TestTampered*;
		VAR leaf, bad, roots: X509.Certificate; res, err: AO.SIGNED32;
			der: ARRAY 8192 OF SHORTCHAR; i, len: AO.SIZE;
	BEGIN
		leaf := X509.ReadPemChain(LeafPem, res);
		roots := X509.ReadPemChain(CaPem, res);
		UnitAssert.NotNil(leaf, "leaf for tamper");
		IF (leaf # NIL) & (roots # NIL) THEN
			len := leaf.rawLen;
			UnitAssert.True((len > 0) & (len < LEN(der)), "raw fits");
			FOR i := 0 TO len - 1 DO der[i] := leaf.raw^[i] END;
			(* Подпись — последние байты DER-последовательности. *)
			der[len - 1] := BIT.CXOR(der[len - 1], 1X);
			bad := X509.ReadCert(der, 0, len, res);
			UnitAssert.NotNil(bad, "tampered parsed");
			IF bad # NIL THEN
				UnitAssert.True(~X509.VerifyChain(bad, bad, roots, "127.0.0.1", {}, err), "tampered rejected")
			END
		END
	END TestTampered;

	PROCEDURE DecodeOk (IN hex: ARRAY OF SHORTCHAR): BOOLEAN;
		VAR der: ARRAY 32 OF SHORTCHAR; r: Streams.StringReader;
			t: ASN1.Triplet; n, len: AO.SIZE;
	BEGIN
		n := LEN(hex$) DIV 2;
		U.Hex2Bin(hex, 0, der, 0, n);
		NEW(r); r.InitStringReader(n); r.SetRaw(der, 0, n);
		t := ASN1.Decode(r, 0, len);
		RETURN (t # NIL) & t.ok
	END DecodeOk;

	PROCEDURE TestAsn1*;
		(* Границы разбора DER: заявленная длина не должна уводить
		   разбор за буфер и за родительский элемент. *)
	BEGIN
		UnitAssert.True(DecodeOk("3003020105"), "valid DER accepted");
		UnitAssert.True(~DecodeOk("30800000"), "indefinite length rejected");
		UnitAssert.True(~DecodeOk("30850100000000"), "five length bytes rejected");
		UnitAssert.True(~DecodeOk("300302050000000000"), "child overruns parent rejected");
		UnitAssert.True(~DecodeOk("030000"), "zero-length BIT STRING rejected");
		UnitAssert.True(~DecodeOk("30050201"), "truncated DER rejected")
	END TestAsn1;

	PROCEDURE TestMalformed*;
		VAR der: ARRAY 16 OF SHORTCHAR; c: X509.Certificate; res: AO.SIGNED32;
	BEGIN
		der[0] := 30X; der[1] := 7FX; der[2] := 0FFX; der[3] := 0FFX;
		c := X509.ReadCert(der, 0, 4, res);
		UnitAssert.True(c = NIL, "huge length rejected");
		UnitAssert.EqualInt(X509.ErrMalformed, res, "huge length code");
		der[0] := 30X; der[1] := 3X; der[2] := 2X; der[3] := 1X; der[4] := 0X;
		c := X509.ReadCert(der, 0, 5, res);
		UnitAssert.True(c = NIL, "truncated rejected");
		c := X509.ReadCert(der, 0, 0, res);
		UnitAssert.True(c = NIL, "empty rejected");
		c := X509.ReadPemChain("not a certificate", res);
		UnitAssert.True(c = NIL, "garbage PEM rejected")
	END TestMalformed;

END Crypto_devX509.
