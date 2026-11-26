MODULE CryptoX509;
(**
	X.509 certificates and certificate management.
	See RFC 5280.

	Written by Timothée Martiel, 2014, with fragments by P. Hunziker.
	2026: разбор без доверия к длинам, проверка подписи, цепочки,
	сроков и имени (SAN/CN), чтение PEM. Все проверки возвращают
	FALSE/код ошибки, а не ASSERT: сертификат приходит из сети.
*)

	IMPORT Streams := CryptoAosStreams, Dates := CryptoAosDates, AosBigNumbers := CryptoAosBigNumbers,
		CryptoRSA, ASN1 := CryptoASN1, (*CONV FROM KernelLog*)
		StdLog, AO := CryptoAosCompat, Hashes := CryptoHashes, PKCS1 := CryptoPKCS1,
		EC := CryptoEC, ECDSA := CryptoECDSA, Ed25519 := CryptoEd25519, PSS := CryptoPSS,
		U := CryptoUtils, Base64 := CryptoBase64;

	CONST
		Trace = FALSE;
		AllowNotTrusted* = 0;
		AllowOutdated* = 1;
		Bypass* = 31;

		(* Вид открытого ключа и подписи сертификата. *)
		KeyRsa* = 1;
		KeyEc* = 2;
		KeyEd25519* = 3;
		SigRsaPkcs1* = 0;
		SigEcdsa* = 1;
		SigRsaPss* = 2;
		SigEd25519* = 3;

		(* Коды ошибок проверки. *)
		Ok* = 0;
		ErrMalformed* = 1;
		ErrExpired* = 2;
		ErrNotYetValid* = 3;
		ErrUntrusted* = 4;
		ErrBadSignature* = 5;
		ErrHostname* = 6;
		ErrUnsupported* = 7;
		ErrNoCA* = 8;

		MaxChainDepth = 8;
		MaxSans = 64;
		MaxPem = 16384;

		(* OID в DER-содержимом (без заголовка). *)
		OidSha256WithRsa = "2A864886F70D01010B";
		OidSha384WithRsa = "2A864886F70D01010C";
		OidSha512WithRsa = "2A864886F70D01010D";
		OidSha1WithRsa = "2A864886F70D010105";
		OidMd5WithRsa = "2A864886F70D010104";
		OidRsaEncryption = "2A864886F70D010101";
		OidRsaPss = "2A864886F70D01010A";
		OidMgf1 = "2A864886F70D010108";
		OidEcdsaSha256 = "2A8648CE3D040302";
		OidEcdsaSha384 = "2A8648CE3D040303";
		OidEcPublicKey = "2A8648CE3D0201";
		OidPrime256v1 = "2A8648CE3D030107";
		OidSecp384r1 = "2B81040022";
		OidEd25519 = "2B6570";
		OidSha256Alg = "608648016503040201";
		OidSha384Alg = "608648016503040202";
		OidSha512Alg = "608648016503040203";
		OidSubjectAltName = "551D11";
		OidBasicConstraints = "551D13";
		OidKeyUsage = "551D0F";
		OidCommonName = "550403";
		OidCountry = "550406";
		OidOrganisation = "55040A";
		OidOrganisationUnit = "55040B";
		OidState = "550408";

	TYPE
		StdLogSender = POINTER TO RECORD(Streams.Sender) END;
		Entity* = EXTENSIBLE RECORD
			country-, organisation-, organisationUnit-, distName-, state-, commonName- : POINTER TO ARRAY OF SHORTCHAR;
			serial- : AO.SIGNED32
		END;
		Certificate* = POINTER TO EXTENSIBLE RECORD
			version-, serial- : AO.SIGNED32;
			algorithm- : POINTER TO ARRAY OF SHORTCHAR;
			validity- : EXTENSIBLE RECORD
				notBefore-, notAfter- : Dates.DateTime
			END;
			identifier- : POINTER TO ARRAY OF SHORTCHAR;
			publicKey- : CryptoRSA.Key;
			keyType* : AO.SIGNED32;
			ecCurve* : EC.Curve;
			ecX*, ecY* : EC.BigInt;
			edKey* : ARRAY 32 OF SHORTCHAR;
			sigType* : AO.SIGNED32;
			pssSaltLen* : AO.SIGNED32;
			issuer-, subject- : Entity;
			parent-, next* : Certificate;
			(* Проверка подписи. *)
			hashName* : ARRAY 16 OF SHORTCHAR;
			signature* : POINTER TO ARRAY OF SHORTCHAR;
			sigLen* : AO.SIGNED32;
			tbs* : POINTER TO ARRAY OF SHORTCHAR;	(* подписанная часть DER *)
			tbsLen* : AO.SIGNED32;
			raw* : POINTER TO ARRAY OF SHORTCHAR;	(* весь сертификат DER *)
			rawLen* : AO.SIGNED32;
			issuerRaw* : POINTER TO ARRAY OF SHORTCHAR;
			issuerLen* : AO.SIGNED32;
			subjectRaw* : POINTER TO ARRAY OF SHORTCHAR;
			subjectLen* : AO.SIGNED32;
			isCA* : BOOLEAN;
			hasPathLen* : BOOLEAN;	(* basicConstraints содержит pathLenConstraint *)
			pathLen* : AO.SIGNED32;
			keyUsagePresent* : BOOLEAN;
			keyCertSign* : BOOLEAN;
			critUnsupported* : BOOLEAN;	(* критичное расширение, которого мы не знаем *)
			sanCount* : AO.SIGNED32;
			sanValue* : ARRAY MaxSans OF POINTER TO ARRAY OF SHORTCHAR;
			sanLen* : ARRAY MaxSans OF AO.SIGNED32;
			sanIsIP* : ARRAY MaxSans OF BOOLEAN
		END;

	VAR
		roots: Certificate;

	PROCEDURE (SELF: StdLogSender) Send* (IN buf : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE; propagate : BOOLEAN; VAR res : INTEGER);
		VAR arr : POINTER TO ARRAY OF CHAR; i : INTEGER;
	BEGIN
		NEW(arr, len);
		FOR i:= 0 TO len - 1 DO
			arr[i] := buf[ofs+i]
		END;
		StdLog.String(arr);
		res := Streams.Ok;
	END Send;

	PROCEDURE CopyStr* (IN src : ARRAY OF SHORTCHAR; VAR dst : ARRAY OF SHORTCHAR);
		VAR i : AO.SIZE;
	BEGIN
		i := 0;
		WHILE (i < LEN(src)) & (i < LEN(dst) - 1) & (src[i] # 0X) DO
			dst[i] := src[i];
			INC(i)
		END;
		dst[i] := 0X
	END CopyStr;

	PROCEDURE OidIs (t : ASN1.Triplet; IN hex : ARRAY OF SHORTCHAR; hexLen : AO.SIZE): BOOLEAN;
		VAR want : ARRAY 32 OF SHORTCHAR;
	BEGIN
		IF (t = NIL) OR (t.class # ASN1.ASN1Universal) OR (t.tag # ASN1.Oid)
			OR (t.svalue = NIL) OR (LEN(t.svalue^) # hexLen) OR (hexLen > LEN(want)) THEN
			RETURN FALSE
		END;
		U.Hex2Bin(hex, 0, want, 0, hexLen);
		RETURN U.EqualBytesCT(want, t.svalue^, 0, 0, hexLen)
	END OidIs;

	PROCEDURE HashAlgName (oid : ASN1.Triplet; VAR name : ARRAY OF SHORTCHAR): BOOLEAN;
	BEGIN
		IF OidIs(oid, OidSha256Alg, 9) THEN
			CopyStr("CryptoSHA256", name); RETURN TRUE
		ELSIF OidIs(oid, OidSha384Alg, 9) THEN
			CopyStr("CryptoSHA384", name); RETURN TRUE
		ELSIF OidIs(oid, OidSha512Alg, 9) THEN
			CopyStr("CryptoSHA512", name); RETURN TRUE
		END;
		RETURN FALSE
	END HashAlgName;

	(* AlgorithmIdentifier подписи: PKCS#1 v1.5, ECDSA, RSA-PSS или Ed25519.
	   Для PSS разбираются параметры и принимается только соль длины хеша
	   и MGF1 по тому же хешу. *)
	PROCEDURE ParseSigAlg (alg : ASN1.Triplet; VAR sigType : AO.SIGNED32;
			VAR hashName : ARRAY OF SHORTCHAR; VAR saltLen : AO.SIGNED32): BOOLEAN;
		VAR oid, params, c, h, mgfHashAlg : ASN1.Triplet;
			mgfName : ARRAY 16 OF SHORTCHAR;
			tag, hSize : AO.SIGNED32;
	BEGIN
		hashName[0] := 0X;
		saltLen := -1;
		sigType := SigRsaPkcs1;
		IF (alg = NIL) OR (alg.class # ASN1.ASN1Universal) OR (alg.tag # ASN1.Sequence) THEN
			RETURN FALSE
		END;
		oid := alg.child;
		IF OidIs(oid, OidSha256WithRsa, 9) THEN
			CopyStr("CryptoSHA256", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidSha384WithRsa, 9) THEN
			CopyStr("CryptoSHA384", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidSha512WithRsa, 9) THEN
			CopyStr("CryptoSHA512", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidSha1WithRsa, 9) THEN
			CopyStr("CryptoSHA1", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidMd5WithRsa, 9) THEN
			CopyStr("CryptoMD5", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidEcdsaSha256, 8) THEN
			sigType := SigEcdsa; CopyStr("CryptoSHA256", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidEcdsaSha384, 8) THEN
			sigType := SigEcdsa; CopyStr("CryptoSHA384", hashName); RETURN TRUE
		ELSIF OidIs(oid, OidEd25519, 3) THEN
			sigType := SigEd25519; RETURN TRUE
		ELSIF OidIs(oid, OidRsaPss, 9) THEN
			sigType := SigRsaPss;
			params := oid.next;
			IF (params = NIL) OR (params.class # ASN1.ASN1Universal)
				OR (params.tag # ASN1.Sequence) THEN
				RETURN FALSE
			END;
			c := params.child;
			mgfName[0] := 0X;
			WHILE c # NIL DO
				tag := c.tag MOD 32;
				IF (c.class = ASN1.ASN1Context) & (c.child # NIL) THEN
					IF tag = 0 THEN
						h := c.child;
						IF ~HashAlgName(h.child, hashName) THEN RETURN FALSE END
					ELSIF tag = 1 THEN
						h := c.child;   (* AlgorithmIdentifier MGF1 *)
						IF (h.child = NIL) OR ~OidIs(h.child, OidMgf1, 9) THEN
							RETURN FALSE
						END;
						mgfHashAlg := h.child.next;
						IF mgfHashAlg = NIL THEN RETURN FALSE END;
						IF ~HashAlgName(mgfHashAlg.child, mgfName) THEN
							RETURN FALSE
						END
					ELSIF tag = 2 THEN
						IF c.child.tag # ASN1.Integer THEN RETURN FALSE END;
						saltLen := c.child.ivalue
					END
				END;
				c := c.next
			END;
			IF hashName[0] = 0X THEN RETURN FALSE END;
			IF mgfName[0] # 0X THEN
				IF mgfName # hashName THEN RETURN FALSE END
			ELSE
				RETURN FALSE
			END;
			IF saltLen < 0 THEN RETURN FALSE END;
			IF hashName = "CryptoSHA256" THEN hSize := 32
			ELSIF hashName = "CryptoSHA384" THEN hSize := 48
			ELSIF hashName = "CryptoSHA512" THEN hSize := 64
			ELSE RETURN FALSE
			END;
			IF saltLen # hSize THEN RETURN FALSE END;
			RETURN TRUE
		END;
		RETURN FALSE
	END ParseSigAlg;

	PROCEDURE SameBytes (IN a, b : ARRAY OF SHORTCHAR; aLen, bLen : AO.SIGNED32): BOOLEAN;
	BEGIN
		IF aLen # bLen THEN
			RETURN FALSE
		END;
		IF (aLen <= 0) OR (LEN(a) < aLen) OR (LEN(b) < bLen) THEN
			RETURN FALSE
		END;
		RETURN U.EqualBytesCT(a, b, 0, 0, aLen)
	END SameBytes;

	PROCEDURE SameCert (a, b : Certificate): BOOLEAN;
	BEGIN
		IF (a = NIL) OR (b = NIL) OR (a.raw = NIL) OR (b.raw = NIL) THEN
			RETURN FALSE
		END;
		RETURN SameBytes(a.raw^, b.raw^, a.rawLen, b.rawLen)
	END SameCert;

	PROCEDURE SameKey (a, b : Certificate): BOOLEAN;
		VAR ba, bb : ARRAY 128 OF SHORTCHAR; i : AO.SIZE;
	BEGIN
		IF (a = NIL) OR (b = NIL) OR (a.keyType # b.keyType) THEN
			RETURN FALSE
		END;
		IF a.keyType = KeyRsa THEN
			RETURN (a.publicKey # NIL) & (b.publicKey # NIL)
				& (AosBigNumbers.Cmp(a.publicKey.modulus, b.publicKey.modulus) = 0)
				& (AosBigNumbers.Cmp(a.publicKey.exponent, b.publicKey.exponent) = 0)
		ELSIF a.keyType = KeyEc THEN
			IF (a.ecCurve = NIL) OR (b.ecCurve = NIL) OR (a.ecCurve.n # b.ecCurve.n) THEN
				RETURN FALSE
			END;
			FOR i := 0 TO LEN(ba) - 1 DO ba[i] := 0X; bb[i] := 0X END;
			EC.GetBytes(a.ecCurve, a.ecX, ba, 0);
			EC.GetBytes(b.ecCurve, b.ecX, bb, 0);
			IF ~U.EqualBytesCT(ba, bb, 0, 0, 4 * a.ecCurve.n) THEN RETURN FALSE END;
			FOR i := 0 TO LEN(ba) - 1 DO ba[i] := 0X; bb[i] := 0X END;
			EC.GetBytes(a.ecCurve, a.ecY, ba, 0);
			EC.GetBytes(b.ecCurve, b.ecY, bb, 0);
			RETURN U.EqualBytesCT(ba, bb, 0, 0, 4 * a.ecCurve.n)
		ELSIF a.keyType = KeyEd25519 THEN
			RETURN U.EqualBytesCT(a.edKey, b.edKey, 0, 0, 32)
		END;
		RETURN FALSE
	END SameKey;

	(* Доверенный корень и предъявленный сертификат могут быть разными
	   кодировками одного ключа (cross-signed root): сравниваем субъект
	   и открытый ключ, как это делает построение пути по RFC 5280. *)
	PROCEDURE SameAnchor (a, b : Certificate): BOOLEAN;
	BEGIN
		IF (a = NIL) OR (b = NIL) OR (a.subjectRaw = NIL) OR (b.subjectRaw = NIL)
			OR (a.subjectLen # b.subjectLen) THEN
			RETURN FALSE
		END;
		RETURN U.EqualBytesCT(a.subjectRaw^, b.subjectRaw^, 0, 0, a.subjectLen)
			& SameKey(a, b)
	END SameAnchor;

	PROCEDURE InList (cert, list : Certificate): BOOLEAN;
		VAR c : Certificate;
	BEGIN
		c := list;
		WHILE c # NIL DO
			IF SameCert(cert, c) OR SameAnchor(cert, c) THEN
				RETURN TRUE
			END;
			c := c.next
		END;
		RETURN FALSE
	END InList;

	PROCEDURE CheckDate (c : Certificate; flags : SET): AO.SIGNED32;
		VAR now : Dates.DateTime; cmpBefore, cmpAfter : AO.SIGNED32;
	BEGIN
		IF AllowOutdated IN flags THEN
			RETURN Ok
		END;
		IF c = NIL THEN
			RETURN ErrMalformed
		END;
		now := Dates.Now();
		cmpBefore := Dates.CompareDateTime(c.validity.notBefore, now);
		cmpAfter := Dates.CompareDateTime(now, c.validity.notAfter);
		IF cmpBefore > 0 THEN
			RETURN ErrNotYetValid
		ELSIF cmpAfter > 0 THEN
			RETURN ErrExpired
		END;
		RETURN Ok
	END CheckDate;

	(* Число из DER-INTEGER в BigInt кривой: ведущий 0x00 допустим,
	   отрицательное значение (старший бит без 0x00) отвергается. *)
	PROCEDURE IntToBig (c : EC.Curve; IN src : ARRAY OF SHORTCHAR; len : AO.SIGNED32;
			VAR b : EC.BigInt): BOOLEAN;
		VAR buf : ARRAY 128 OF SHORTCHAR;
			i, n, ofs : AO.SIGNED32;
	BEGIN
		n := 4 * c.n;
		IF len <= 0 THEN RETURN FALSE END;
		ofs := 0;
		WHILE (ofs < len - 1) & (src[ofs] = 0X) DO INC(ofs) END;
		(* Без ведущего нуля старший бит означает отрицательное число. *)
		IF (ofs = 0) & (ORD(src[0]) >= 128) THEN RETURN FALSE END;
		IF len - ofs > n THEN RETURN FALSE END;
		FOR i := 0 TO n - 1 DO buf[i] := 0X END;
		FOR i := ofs TO len - 1 DO buf[n - len + i] := src[i] END;
		RETURN EC.OrderFromBytes(c, buf, 0, n, b)
	END IntToBig;

	(* Подпись ECDSA в X.509 — DER SEQUENCE { r INTEGER, s INTEGER }. *)
	PROCEDURE EcdsaRS (c : EC.Curve; IN sig : ARRAY OF SHORTCHAR; sigLen : AO.SIGNED32;
			VAR r, s : EC.BigInt): BOOLEAN;
		VAR reader : Streams.StringReader; root, ri, si : ASN1.Triplet; len0 : AO.SIZE;
	BEGIN
		NEW(reader);
		reader.InitStringReader(sigLen);
		reader.SetRaw(sig, 0, sigLen);
		root := ASN1.Decode(reader, 0, len0);
		IF (root = NIL) OR ~root.ok OR (root.tag # ASN1.Sequence)
			OR (root.child = NIL) OR (root.child.next = NIL)
			OR (root.child.next.next # NIL) THEN
			RETURN FALSE
		END;
		ri := root.child;
		si := ri.next;
		IF (ri.tag # ASN1.Integer) OR (si.tag # ASN1.Integer)
			OR (ri.svalue = NIL) OR (si.svalue = NIL) THEN
			RETURN FALSE
		END;
		RETURN IntToBig(c, ri.svalue^, LEN(ri.svalue^), r)
			& IntToBig(c, si.svalue^, LEN(si.svalue^), s)
	END EcdsaRS;

	(* Подпись сертификата открытым ключом издателя. *)
	PROCEDURE CheckSignature* (c, issuer : Certificate): BOOLEAN;
		VAR h : Hashes.Hash; digest : ARRAY 64 OF SHORTCHAR;
			r, s : EC.BigInt; hLen : AO.SIGNED32;
	BEGIN
		IF (c = NIL) OR (issuer = NIL) OR (c.tbs = NIL) OR (c.signature = NIL)
			OR (c.tbsLen <= 0) OR (c.sigLen <= 0) THEN
			RETURN FALSE
		END;
		IF c.sigType = SigEd25519 THEN
			RETURN (issuer.keyType = KeyEd25519) & (c.sigLen = 64)
				& Ed25519.Verify(issuer.edKey, c.tbs^, c.tbsLen, c.signature^)
		END;
		h := Hashes.NewHash(c.hashName);
		IF h = NIL THEN
			RETURN FALSE
		END;
		h.Initialize;
		h.Update(c.tbs^, 0, c.tbsLen);
		h.GetHash(digest, 0);
		hLen := h.size;
		IF c.sigType = SigRsaPkcs1 THEN
			RETURN (issuer.keyType = KeyRsa) & (issuer.publicKey # NIL)
				& PKCS1.VerifyHash(c.hashName, digest, hLen, issuer.publicKey,
					c.signature^, 0, c.sigLen)
		ELSIF c.sigType = SigRsaPss THEN
			RETURN (issuer.keyType = KeyRsa) & (issuer.publicKey # NIL)
				& (c.pssSaltLen = hLen)
				& PSS.VerifyHash(c.hashName, digest, hLen, issuer.publicKey,
					c.signature^, 0, c.sigLen)
		ELSIF c.sigType = SigEcdsa THEN
			IF (issuer.keyType # KeyEc) OR (issuer.ecCurve = NIL)
				OR (hLen # 4 * issuer.ecCurve.n) THEN
				RETURN FALSE
			END;
			IF ~EcdsaRS(issuer.ecCurve, c.signature^, c.sigLen, r, s) THEN
				RETURN FALSE
			END;
			RETURN ECDSA.Verify(issuer.ecCurve, issuer.ecX, issuer.ecY,
				digest, hLen, r, s)
		END;
		RETURN FALSE
	END CheckSignature;

	PROCEDURE Verify* (chain : Certificate; flags : SET): BOOLEAN;
		(* Старый вход: проверяет сроки и, если известен издатель,
		   подпись. Цепочка до корня — VerifyChain. *)
		VAR err : AO.SIGNED32;
	BEGIN
		err := CheckDate(chain, flags);
		RETURN err = Ok
	END Verify;

	PROCEDURE FormatEntity (triplet : ASN1.Triplet; VAR entity : Entity);
		VAR sets, seq, oid, value : ASN1.Triplet;
	BEGIN
		IF (triplet = NIL) OR (triplet.child = NIL) THEN
			RETURN
		END;
		sets := triplet.child;
		WHILE sets # NIL DO
			seq := sets.child;
			WHILE seq # NIL DO
				oid := seq.child;
				IF oid # NIL THEN
					value := oid.next
				ELSE
					value := NIL
				END;
				IF (value # NIL) & (value.svalue # NIL) THEN
					IF OidIs(oid, OidCommonName, 3) THEN
						entity.commonName := value.svalue
					ELSIF OidIs(oid, OidCountry, 3) THEN
						entity.country := value.svalue
					ELSIF OidIs(oid, OidOrganisation, 3) THEN
						entity.organisation := value.svalue
					ELSIF OidIs(oid, OidOrganisationUnit, 3) THEN
						entity.organisationUnit := value.svalue
					ELSIF OidIs(oid, OidState, 3) THEN
						entity.state := value.svalue
					END
				END;
				seq := seq.next
			END;
			sets := sets.next
		END
	END FormatEntity;


	PROCEDURE IsDigit (char : SHORTCHAR): BOOLEAN;
	BEGIN
		IF (ORD(char) < ORD('0')) OR (ORD(char) > ORD('9')) THEN
			RETURN FALSE
		ELSE
			RETURN TRUE
		END
	END IsDigit;

	PROCEDURE ReadUTCTime (reader : Streams.Reader; VAR date : Dates.DateTime): BOOLEAN;
		VAR
			char: SHORTCHAR;
	BEGIN
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.year := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.year, ORD(char) - ORD('0'));
		IF date.year >= 50 THEN
			INC(date.year, 1900)
		ELSE
			INC(date.year, 2000)
		END;
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.month := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.month, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.day := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.day, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.hour := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.hour, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.minute := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.minute, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.second := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.second, ORD(char) - ORD('0'));
		char := reader.Get();
		IF char # 'Z' THEN
			RETURN FALSE
		ELSE
			RETURN TRUE
		END
	END ReadUTCTime;

	PROCEDURE ReadGeneralizedTime (reader : Streams.Reader; VAR date : Dates.DateTime): BOOLEAN;
		VAR
			char: SHORTCHAR;
	BEGIN
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.year := 1000 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.year, 100 * (ORD(char) - ORD('0')));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.year, 10 * (ORD(char) - ORD('0')));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.year, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.month := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.month, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.day := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.day, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.hour := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.hour, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.minute := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.minute, ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		date.second := 10 * (ORD(char) - ORD('0'));
		char := reader.Get();
		IF ~IsDigit(char) THEN
			RETURN FALSE
		END;
		INC(date.second, ORD(char) - ORD('0'));
		char := reader.Get();
		IF char # 'Z' THEN
			RETURN FALSE
		ELSE
			RETURN TRUE
		END
	END ReadGeneralizedTime;

	PROCEDURE ReadTime (t : ASN1.Triplet; VAR date : Dates.DateTime): BOOLEAN;
		VAR r : Streams.StringReader; res : BOOLEAN;
	BEGIN
		IF (t = NIL) OR (t.svalue = NIL) OR (LEN(t.svalue^) = 0) THEN
			RETURN FALSE
		END;
		NEW(r);
		r.InitStringReader(LEN(t.svalue^));
		r.SetRaw(t.svalue^, 0, LEN(t.svalue^));
		IF t.tag = ASN1.UTCTime THEN
			res := ReadUTCTime(r, date)
		ELSE
			res := ReadGeneralizedTime(r, date)
		END;
		RETURN res & Dates.ValidDateTime(date)
	END ReadTime;

	PROCEDURE AssignIntValue (t : ASN1.Triplet; VAR b : AosBigNumbers.BigNumber);
	BEGIN
		IF (t # NIL) & (t.svalue # NIL) & (LEN(t.svalue^) > 0) THEN
			AosBigNumbers.AssignBin(b, t.svalue^, 0, LEN(t.svalue^))
		ELSE
			b := NIL
		END
	END AssignIntValue;

	PROCEDURE FormatRsaPublicKey* (publicKey : ASN1.Triplet; VAR key : CryptoRSA.Key);
		VAR r : Streams.StringReader; seq, mod, exp : ASN1.Triplet; len0 : AO.SIZE;
	BEGIN
		IF key = NIL THEN
			NEW(key)
		END;
		key.modulus := NIL;
		key.exponent := NIL;
		IF (publicKey = NIL) OR (publicKey.child = NIL) OR (publicKey.child.next = NIL)
			OR (publicKey.child.next.svalue = NIL) THEN
			RETURN
		END;
		NEW(r);
		r.InitStringReader(LEN(publicKey.child.next.svalue^));
		r.SetRaw(publicKey.child.next.svalue^, 0, LEN(publicKey.child.next.svalue^));
		seq := ASN1.Decode(r, 0, len0);
		IF (seq = NIL) OR ~seq.ok OR (seq.child = NIL) OR (seq.child.next = NIL) THEN
			RETURN
		END;
		mod := seq.child;
		exp := mod.next;
		IF (mod.svalue = NIL) OR (exp.svalue = NIL) THEN
			RETURN
		END;
		AosBigNumbers.AssignBin(key.modulus, mod.svalue^, 0, LEN(mod.svalue^));
		AosBigNumbers.AssignBin(key.exponent, exp.svalue^, 0, LEN(exp.svalue^))
	END FormatRsaPublicKey;

	PROCEDURE PrintEntity (IN entity : Entity; writer : Streams.Writer);
	BEGIN
		IF entity.country # NIL THEN
			writer.String(" country:	");
			writer.Bytes(entity.country^, 0, LEN(entity.country));
			writer.Ln
		END;
		IF entity.organisation # NIL THEN
			writer.String(" organisation:   ");
			writer.Bytes(entity.organisation^, 0, LEN(entity.organisation));
			writer.Ln
		END;
		IF entity.organisationUnit # NIL THEN
			writer.String(" organisation unit:      ");
			writer.Bytes(entity.organisationUnit^, 0, LEN(entity.organisationUnit));
			writer.Ln
		END;
		IF entity.distName # NIL THEN
			writer.String(" distinguished name:     ");
			writer.Bytes(entity.distName^, 0, LEN(entity.distName));
			writer.Ln
		END;
		IF entity.state # NIL THEN
			writer.String(" state:  ");
			writer.Bytes(entity.state^, 0, LEN(entity.state));
			writer.Ln
		END;
		IF entity.commonName # NIL THEN
			writer.String(" common name:    ");
			writer.Bytes(entity.commonName^, 0, LEN(entity.commonName));
			writer.Ln
		END
	END PrintEntity;

	PROCEDURE PrintDate (date : Dates.DateTime; writer : Streams.Writer);
	BEGIN
		writer.Int(date.year, 0);
		writer.String('/');
		writer.Int(date.month, 0);
		writer.String('/');
		writer.Int(date.day, 0);
		writer.String(" ");
		writer.Int(date.hour, 0);
		writer.String(":");
		writer.Int(date.minute, 0);
		writer.String(":");
		writer.Int(date.second, 0)
	END PrintDate;

	PROCEDURE PrintCertificate* (certificate : Certificate; writer : Streams.Writer);
		VAR count : AO.SIZE;
	BEGIN
		writer.String("== Begin of Certificate ==");
		writer.Ln;
		writer.String("version: ");
		writer.Int(certificate.version, 0);
		writer.Ln;
		writer.String("serial:  ");
		writer.Int(certificate.serial, 0);
		writer.Ln;
		writer.String("Issuer:");
		writer.Ln;
		PrintEntity(certificate.issuer, writer);
		writer.String("validity:");
		writer.Ln;
		writer.String(" not before:     ");
		PrintDate(certificate.validity.notBefore, writer);
		writer.Ln;
		writer.String(" not after:      ");
		PrintDate(certificate.validity.notAfter, writer);
		writer.Ln;
		writer.String("Subject:");
		writer.Ln;
		PrintEntity(certificate.subject, writer);
		writer.String("RSA public key:");
		writer.Ln;
		writer.String(" modulus:	");
		AosBigNumbers.TextWrite(writer, certificate.publicKey.modulus);
		writer.Ln;
		writer.String(" exponent:       ");
		AosBigNumbers.TextWrite(writer, certificate.publicKey.exponent);
		writer.Ln;
		writer.String("== End of Certificate ==");
		writer.Ln
	END PrintCertificate;

	PROCEDURE AddSan (cert : Certificate; val : POINTER TO ARRAY OF SHORTCHAR; len : AO.SIZE; isIP : BOOLEAN);
	BEGIN
		IF cert.sanCount < MaxSans THEN
			cert.sanValue[cert.sanCount] := val;
			cert.sanLen[cert.sanCount] := len;
			cert.sanIsIP[cert.sanCount] := isIP;
			INC(cert.sanCount)
		END
	END AddSan;

	PROCEDURE ParseExtensions (ext : ASN1.Triplet; cert : Certificate; VAR res : AO.SIGNED32);
		VAR exts, e, oid, critT, val, gns, gn : ASN1.Triplet; r : Streams.StringReader;
			len0 : AO.SIZE; crit : BOOLEAN;
	BEGIN
		IF (ext = NIL) OR (ext.child = NIL) THEN
			res := ErrMalformed;
			RETURN
		END;
		exts := ext.child;
		IF (exts.class # ASN1.ASN1Universal) OR (exts.tag # ASN1.Sequence) THEN
			res := ErrMalformed;
			RETURN
		END;
		e := exts.child;
		WHILE e # NIL DO
			IF (e.class # ASN1.ASN1Universal) OR (e.tag # ASN1.Sequence) THEN
				res := ErrMalformed;
				RETURN
			END;
			(* Extension ::= SEQUENCE { extnID OID, critical BOOLEAN DEFAULT FALSE,
			   extnValue OCTET STRING } *)
			oid := e.child;
			crit := FALSE;
			val := NIL;
			IF oid # NIL THEN
				critT := oid.next;
				IF (critT # NIL) & (critT.class = ASN1.ASN1Universal) & (critT.tag = ASN1.Boolean) THEN
					crit := critT.bvalue;
					critT := critT.next
				END;
				IF (critT # NIL) & (critT.class = ASN1.ASN1Universal) & (critT.tag = ASN1.String) THEN
					val := critT
				END
			END;
			IF (oid = NIL) OR (val = NIL) OR (val.svalue = NIL) THEN
				res := ErrMalformed;
				RETURN
			END;
			IF OidIs(oid, OidSubjectAltName, 3) THEN
				NEW(r);
				r.InitStringReader(LEN(val.svalue^));
				r.SetRaw(val.svalue^, 0, LEN(val.svalue^));
				gns := ASN1.Decode(r, 0, len0);
				IF (gns = NIL) OR ~gns.ok OR (gns.class # ASN1.ASN1Universal) OR (gns.tag # ASN1.Sequence) THEN
					res := ErrMalformed;
					RETURN
				END;
				gn := gns.child;
				WHILE gn # NIL DO
					IF (gn.class = ASN1.ASN1Context) & ~gn.constructed & (gn.svalue # NIL) THEN
						IF gn.tag MOD 32 = 2 THEN
							AddSan(cert, gn.svalue, LEN(gn.svalue^), FALSE)
						ELSIF gn.tag MOD 32 = 7 THEN
							AddSan(cert, gn.svalue, LEN(gn.svalue^), TRUE)
						END
					END;
					gn := gn.next
				END
			ELSIF OidIs(oid, OidBasicConstraints, 3) THEN
				(* BasicConstraints ::= SEQUENCE { cA BOOLEAN DEFAULT FALSE,
				   pathLenConstraint INTEGER OPTIONAL } *)
				NEW(r);
				r.InitStringReader(LEN(val.svalue^));
				r.SetRaw(val.svalue^, 0, LEN(val.svalue^));
				gns := ASN1.Decode(r, 0, len0);
				IF (gns = NIL) OR ~gns.ok OR (gns.class # ASN1.ASN1Universal) OR (gns.tag # ASN1.Sequence) THEN
					res := ErrMalformed;
					RETURN
				END;
				gn := gns.child;
				IF (gn # NIL) & (gn.class = ASN1.ASN1Universal) & (gn.tag = ASN1.Boolean) THEN
					cert.isCA := gn.bvalue;
					gn := gn.next
				END;
				IF (gn # NIL) & (gn.class = ASN1.ASN1Universal) & (gn.tag = ASN1.Integer) THEN
					IF gn.length > 4 THEN
						res := ErrMalformed;
						RETURN
					END;
					cert.hasPathLen := TRUE;
					cert.pathLen := gn.ivalue;
					gn := gn.next
				END;
				IF gn # NIL THEN
					res := ErrMalformed;
					RETURN
				END
			ELSIF OidIs(oid, OidKeyUsage, 3) THEN
				(* KeyUsage ::= BIT STRING; keyCertSign — бит 5 (маска 04H). *)
				NEW(r);
				r.InitStringReader(LEN(val.svalue^));
				r.SetRaw(val.svalue^, 0, LEN(val.svalue^));
				gns := ASN1.Decode(r, 0, len0);
				IF (gns = NIL) OR ~gns.ok OR (gns.class # ASN1.ASN1Universal) OR (gns.tag # ASN1.BitString) THEN
					res := ErrMalformed;
					RETURN
				END;
				cert.keyUsagePresent := TRUE;
				IF (gns.svalue # NIL) & (LEN(gns.svalue^) > 0) THEN
					cert.keyCertSign := (ORD(gns.svalue^[0]) DIV 4 MOD 2) = 1
				END
			ELSIF crit THEN
				(* RFC 5280, 4.2: неизвестное критичное расширение
				   делает сертификат непригодным. *)
				cert.critUnsupported := TRUE
			END;
			e := e.next
		END
	END ParseExtensions;

	PROCEDURE ReadCert* (IN der : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE; VAR res : AO.SIGNED32): Certificate;
		VAR
			c : Certificate;
			r : Streams.StringReader;
			root, tbs, sigAlg, sigVal, d, alg, spki, ext : ASN1.Triplet;
			innerHash : ARRAY 16 OF SHORTCHAR;
			innerType, innerSalt, innerLen : AO.SIGNED32;
			len0, i : AO.SIZE;
			ok : BOOLEAN;
	BEGIN
		res := Ok;
		c := NIL;
		IF (ofs < 0) OR (len <= 0) OR (ofs + len > LEN(der)) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		NEW(r);
		r.InitStringReader(len);
		r.SetRaw(der, ofs, len);
		root := ASN1.Decode(r, 0, len0);
		IF (root = NIL) OR ~root.ok OR (root.class # ASN1.ASN1Universal) OR (root.tag # ASN1.Sequence)
			OR (root.child = NIL) OR (root.child.next = NIL) OR (root.child.next.next = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		NEW(c);
		c.sanCount := 0;
		c.keyType := 0;
		c.sigType := SigRsaPkcs1;
		c.pssSaltLen := -1;
		c.publicKey := NIL;
		c.isCA := FALSE;
		c.hasPathLen := FALSE;
		c.pathLen := 0;
		c.keyUsagePresent := FALSE;
		c.keyCertSign := FALSE;
		c.critUnsupported := FALSE;
		c.version := 1;
		(* Полная DER-запись и подписанная часть. *)
		IF root.raw # NIL THEN
			c.raw := root.raw;
			c.rawLen := root.rawLen
		END;
		tbs := root.child;
		sigAlg := tbs.next;
		sigVal := sigAlg.next;
		IF (tbs.class # ASN1.ASN1Universal) OR (tbs.tag # ASN1.Sequence) OR ~tbs.ok
			OR (tbs.raw = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		c.tbs := tbs.raw;
		c.tbsLen := tbs.rawLen;
		IF ~ParseSigAlg(sigAlg, c.sigType, c.hashName, c.pssSaltLen) THEN
			res := ErrUnsupported;
			RETURN NIL
		END;
		IF (sigVal.class # ASN1.ASN1Universal) OR (sigVal.tag # ASN1.BitString)
			OR (sigVal.svalue = NIL) OR (LEN(sigVal.svalue^) = 0) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		c.signature := sigVal.svalue;
		c.sigLen := LEN(sigVal.svalue^);
		(* Поля TBSCertificate. *)
		d := tbs.child;
		IF (d # NIL) & (d.class = ASN1.ASN1Context) & (d.tag MOD 32 = 0) & d.constructed THEN
			IF (d.child # NIL) & (d.child.ivalue >= 0) THEN
				c.version := d.child.ivalue + 1
			END;
			d := d.next
		END;
		IF (d = NIL) OR (d.tag # ASN1.Integer) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		c.serial := d.ivalue;
		d := d.next;
		IF (d = NIL) OR (d.tag # ASN1.Sequence) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		(* Внутренний AlgorithmIdentifier должен совпасть с внешним. *)
		innerLen := -1;
		IF ~ParseSigAlg(d, innerType, innerHash, innerLen) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		IF (innerType # c.sigType) OR (innerHash # c.hashName) OR (innerLen # c.pssSaltLen) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		d := d.next;
		IF d = NIL THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		FormatEntity(d, c.issuer);
		c.issuerRaw := d.raw;
		c.issuerLen := d.rawLen;
		d := d.next;
		IF (d = NIL) OR (d.child = NIL) OR (d.child.next = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		ok := ReadTime(d.child, c.validity.notBefore) & ReadTime(d.child.next, c.validity.notAfter);
		IF ~ok THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		d := d.next;
		IF d = NIL THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		FormatEntity(d, c.subject);
		c.subjectRaw := d.raw;
		c.subjectLen := d.rawLen;
		d := d.next;
		IF (d = NIL) OR (d.child = NIL) OR (d.child.next = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		spki := d;
		alg := spki.child;
		IF (alg = NIL) OR (alg.child = NIL) OR (spki.child.next = NIL)
			OR (spki.child.next.svalue = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		IF OidIs(alg.child, OidRsaEncryption, 9) THEN
			c.keyType := KeyRsa;
			FormatRsaPublicKey(spki, c.publicKey);
			IF (c.publicKey = NIL) OR (c.publicKey.modulus = NIL)
				OR (c.publicKey.exponent = NIL) THEN
				res := ErrMalformed;
				RETURN NIL
			END
		ELSIF OidIs(alg.child, OidEcPublicKey, 7) THEN
			c.keyType := KeyEc;
			IF OidIs(alg.child.next, OidPrime256v1, 8) THEN
				c.ecCurve := EC.P256()
			ELSIF OidIs(alg.child.next, OidSecp384r1, 5) THEN
				c.ecCurve := EC.P384()
			ELSE
				res := ErrUnsupported;
				RETURN NIL
			END;
			IF ~EC.PointFromBytes(c.ecCurve, spki.child.next.svalue^,
					LEN(spki.child.next.svalue^), c.ecX, c.ecY) THEN
				res := ErrMalformed;
				RETURN NIL
			END
		ELSIF OidIs(alg.child, OidEd25519, 3) THEN
			c.keyType := KeyEd25519;
			IF (alg.child.next # NIL) OR (LEN(spki.child.next.svalue^) # 32) THEN
				res := ErrMalformed;
				RETURN NIL
			END;
			FOR i := 0 TO 31 DO
				c.edKey[i] := spki.child.next.svalue^[i]
			END
		ELSE
			res := ErrUnsupported;
			RETURN NIL
		END;
		(* Дополнительные поля: extensions [3]. *)
		d := d.next;
		WHILE d # NIL DO
			IF (d.class = ASN1.ASN1Context) & (d.tag MOD 32 = 3) & d.constructed THEN
				ParseExtensions(d, c, res);
				IF res # Ok THEN
					RETURN NIL
				END
			END;
			d := d.next
		END;
		IF (c.issuerRaw = NIL) OR (c.subjectRaw = NIL) OR (c.raw = NIL) THEN
			res := ErrMalformed;
			RETURN NIL
		END;
		IF Trace THEN
			StdLog.String("========== Read X509 Certificate =========");
			StdLog.Ln;
			StdLog.String("========== End X509 Certificate =========");
			StdLog.Ln
		END;
		RETURN c
	END ReadCert;

	PROCEDURE Read* (reader : Streams.Reader): Certificate;
		VAR buf : ARRAY MaxPem OF SHORTCHAR; n, len0 : AO.SIZE; res : AO.SIGNED32; c : Certificate;
	BEGIN
		(* Совместимый вход: читаем весь доступный DER во временный буфер. *)
		n := reader.Available();
		IF (n <= 0) OR (n > MaxPem) THEN
			RETURN NIL
		END;
		reader.Bytes(buf, 0, n, len0);
		IF len0 # n THEN
			RETURN NIL
		END;
		c := ReadCert(buf, 0, n, res);
		RETURN c
	END Read;

	PROCEDURE FindPem (IN pem : ARRAY OF SHORTCHAR; from : AO.SIZE; IN header : ARRAY OF SHORTCHAR;
			VAR start, stop : AO.SIZE): BOOLEAN;
		VAR i, j, hlen, plen : AO.SIZE; found : BOOLEAN;
	BEGIN
		hlen := 0;
		WHILE (hlen < LEN(header)) & (header[hlen] # 0X) DO INC(hlen) END;
		plen := 0;
		WHILE (plen < LEN(pem)) & (pem[plen] # 0X) DO INC(plen) END;
		i := from;
		WHILE i + hlen <= plen DO
			found := TRUE;
			j := 0;
			WHILE found & (j < hlen) DO
				IF pem[i + j] # header[j] THEN found := FALSE END;
				INC(j)
			END;
			IF found THEN
				start := i + hlen;
				WHILE (start < plen) & (pem[start] <= ' ') DO INC(start) END;
				stop := start;
				WHILE (stop < plen) & (pem[stop] # '-') & (pem[stop] # 0X) DO INC(stop) END;
				RETURN TRUE
			END;
			INC(i)
		END;
		RETURN FALSE
	END FindPem;

	PROCEDURE PemToDer (IN pem : ARRAY OF SHORTCHAR; start, stop : AO.SIZE;
			VAR der : ARRAY OF SHORTCHAR; VAR derLen : AO.SIZE): BOOLEAN;
		VAR b64 : ARRAY MaxPem OF SHORTCHAR; i, n : AO.SIZE; got : AO.SIGNED32;
	BEGIN
		n := 0;
		i := start;
		WHILE (i < stop) & (n < LEN(b64) - 1) DO
			IF pem[i] > ' ' THEN
				b64[n] := pem[i];
				INC(n)
			END;
			INC(i)
		END;
		b64[n] := 0X;
		got := Base64.Decode(b64, der);
		IF got < 0 THEN
			RETURN FALSE
		END;
		derLen := got;
		RETURN TRUE
	END PemToDer;

	PROCEDURE ReadPemChain* (IN pem : ARRAY OF SHORTCHAR; VAR res : AO.SIGNED32): Certificate;
		CONST
			CertHeader = "-----BEGIN CERTIFICATE-----";
		VAR
			head, tail, c : Certificate;
			start, stop, from, derLen : AO.SIZE;
			der : ARRAY MaxPem OF SHORTCHAR;
	BEGIN
		res := Ok;
		head := NIL;
		tail := NIL;
		from := 0;
		WHILE FindPem(pem, from, CertHeader, start, stop) DO
			(* Набор доверенных корней может содержать сертификат,
			   который мы не разбираем (неизвестный алгоритм). Один
			   такой сертификат не должен отбрасывать остальные. *)
			from := stop;
			IF PemToDer(pem, start, stop, der, derLen) THEN
				c := ReadCert(der, 0, derLen, res);
				IF c # NIL THEN
					IF head = NIL THEN
						head := c
					ELSE
						tail.next := c
					END;
					tail := c
				END
			END
		END;
		IF head = NIL THEN
			res := ErrMalformed
		END;
		RETURN head
	END ReadPemChain;

	PROCEDURE ReadPemCert* (IN pem : ARRAY OF SHORTCHAR; VAR res : AO.SIGNED32): Certificate;
	BEGIN
		RETURN ReadPemChain(pem, res)
	END ReadPemCert;

	PROCEDURE AddTrusted* (certificate : Certificate);
	BEGIN
		IF certificate # NIL THEN
			certificate.next := roots;
			roots := certificate
		END
	END AddTrusted;

	PROCEDURE AddTrustedPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR c, tail : Certificate; res : AO.SIGNED32;
	BEGIN
		c := ReadPemChain(pem, res);
		IF c = NIL THEN
			RETURN FALSE
		END;
		tail := c;
		WHILE tail.next # NIL DO tail := tail.next END;
		tail.next := roots;
		roots := c;
		RETURN TRUE
	END AddTrustedPem;

	PROCEDURE TrustedRoots* (): Certificate;
	BEGIN
		RETURN roots
	END TrustedRoots;

	PROCEDURE FindIssuer (cert, pool : Certificate): Certificate;
		VAR c : Certificate;
	BEGIN
		IF (cert = NIL) OR (cert.issuerRaw = NIL) THEN
			RETURN NIL
		END;
		c := pool;
		WHILE c # NIL DO
			IF (c.subjectRaw # NIL) & ~SameCert(cert, c)
				& SameBytes(cert.issuerRaw^, c.subjectRaw^, cert.issuerLen, c.subjectLen) THEN
				RETURN c
			END;
			c := c.next
		END;
		RETURN NIL
	END FindIssuer;

	PROCEDURE MatchDNS (IN pattern, host : ARRAY OF SHORTCHAR;
			pOfs, plen, hOfs, hlen : AO.SIZE): BOOLEAN;
		VAR i : AO.SIZE; ok, hasDot : BOOLEAN;
	BEGIN
		IF (plen <= 0) OR (hlen <= 0) OR (pOfs < 0) OR (hOfs < 0)
			OR (pOfs + plen > LEN(pattern)) OR (hOfs + hlen > LEN(host)) THEN
			RETURN FALSE
		END;
		(* Один шаблон '*' в начале: ровно одна метка. *)
		IF pattern[pOfs] = '*' THEN
			IF (plen < 3) OR (pattern[pOfs + 1] # '.') THEN
				RETURN FALSE
			END;
			(* RFC 6125, 6.4.3: шаблон не может покрывать домен
			   верхнего уровня — после "*." нужно не меньше двух меток. *)
			hasDot := FALSE;
			FOR i := pOfs + 2 TO pOfs + plen - 1 DO
				IF pattern[i] = '.' THEN hasDot := TRUE END
			END;
			IF ~hasDot THEN
				RETURN FALSE
			END;
			i := 0;
			WHILE (i < hlen) & (host[hOfs + i] # '.') DO INC(i) END;
			IF (i = 0) OR (i >= hlen) THEN
				RETURN FALSE
			END;
			RETURN MatchDNS(pattern, host, pOfs + 1, plen - 1, hOfs + i, hlen - i)
		END;
		IF plen # hlen THEN
			RETURN FALSE
		END;
		ok := TRUE;
		FOR i := 0 TO plen - 1 DO
			IF CAP(pattern[pOfs + i]) # CAP(host[hOfs + i]) THEN
				ok := FALSE
			END
		END;
		RETURN ok
	END MatchDNS;

	PROCEDURE ParseIPv4 (IN host : ARRAY OF SHORTCHAR; hostLen : AO.SIZE; VAR addr : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR i, part, digits : AO.SIZE; v : AO.SIGNED32; ok : BOOLEAN;
	BEGIN
		IF (hostLen <= 0) OR (hostLen > LEN(host)) OR (LEN(addr) < 4) THEN
			RETURN FALSE
		END;
		part := 0;
		v := 0;
		digits := 0;
		ok := TRUE;
		FOR i := 0 TO hostLen - 1 DO
			IF (host[i] >= '0') & (host[i] <= '9') THEN
				v := 10 * v + ORD(host[i]) - ORD('0');
				INC(digits);
				IF v > 255 THEN ok := FALSE END
			ELSIF host[i] = '.' THEN
				IF (digits = 0) OR (part >= 4) OR ~ok THEN
					RETURN FALSE
				END;
				addr[part] := SHORT(CHR(v));
				INC(part);
				v := 0;
				digits := 0
			ELSE
				RETURN FALSE
			END
		END;
		IF (part # 3) OR (digits = 0) OR ~ok THEN
			RETURN FALSE
		END;
		addr[3] := SHORT(CHR(v));
		RETURN TRUE
	END ParseIPv4;

	PROCEDURE MatchHost* (cert : Certificate; IN host : ARRAY OF SHORTCHAR): BOOLEAN;
		VAR i, hostLen : AO.SIZE; ip : ARRAY 4 OF SHORTCHAR; isIP : BOOLEAN;
	BEGIN
		IF cert = NIL THEN
			RETURN FALSE
		END;
		hostLen := 0;
		WHILE (hostLen < LEN(host)) & (host[hostLen] # 0X) DO INC(hostLen) END;
		IF hostLen = 0 THEN
			RETURN FALSE
		END;
		isIP := ParseIPv4(host, hostLen, ip);
		IF cert.sanCount > 0 THEN
			FOR i := 0 TO cert.sanCount - 1 DO
				IF cert.sanIsIP[i] THEN
					IF isIP & (cert.sanLen[i] = 4) THEN
						IF U.EqualBytesCT(ip, cert.sanValue[i]^, 0, 0, 4) THEN
							RETURN TRUE
						END
					END
				ELSE
					IF ~isIP & MatchDNS(cert.sanValue[i]^, host, 0, cert.sanLen[i], 0, hostLen) THEN
						RETURN TRUE
					END
				END
			END;
			RETURN FALSE
		END;
		(* CN — только если SAN нет (RFC 6125, 6.4.4). *)
		IF cert.subject.commonName # NIL THEN
			RETURN ~isIP & MatchDNS(cert.subject.commonName^, host, 0, LEN(cert.subject.commonName^), 0, hostLen)
		END;
		RETURN FALSE
	END MatchHost;

	PROCEDURE HostOk (cert : Certificate; IN host : ARRAY OF SHORTCHAR): BOOLEAN;
	BEGIN
		IF (LEN(host) > 0) & (host[0] = 0X) THEN
			RETURN TRUE	(* клиентский сертификат: имя проверяет вызывающий *)
		END;
		RETURN MatchHost(cert, host)
	END HostOk;

	(* Годится ли сертификат как издатель: CA, keyCertSign, длина пути,
	   сроки и отсутствие непонятных критичных расширений. walk — число
	   промежуточных сертификатов между листом и этим издателем. *)
	PROCEDURE CheckIssuer (issuer : Certificate; walk : AO.SIZE; flags : SET;
			VAR err : AO.SIGNED32): BOOLEAN;
	BEGIN
		IF issuer = NIL THEN
			err := ErrUntrusted;
			RETURN FALSE
		END;
		IF issuer.critUnsupported THEN
			err := ErrUnsupported;
			RETURN FALSE
		END;
		IF ~issuer.isCA THEN
			err := ErrNoCA;
			RETURN FALSE
		END;
		IF issuer.keyUsagePresent & ~issuer.keyCertSign THEN
			err := ErrNoCA;
			RETURN FALSE
		END;
		IF issuer.hasPathLen & (issuer.pathLen < walk) THEN
			err := ErrNoCA;
			RETURN FALSE
		END;
		err := CheckDate(issuer, flags);
		RETURN err = Ok
	END CheckIssuer;

	PROCEDURE SelfIssued (c : Certificate): BOOLEAN;
	BEGIN
		RETURN (c # NIL) & (c.subjectRaw # NIL) & (c.issuerRaw # NIL)
			& SameBytes(c.subjectRaw^, c.issuerRaw^, c.subjectLen, c.issuerLen)
	END SelfIssued;

	(* Цепочка: лист -> издатели из chain (может включать лист) -> корень
	   из roots. Проверяются подписи, сроки, basicConstraints/keyUsage,
	   pathLenConstraint и непонятные критичные расширения. *)
	PROCEDURE VerifyChain* (leaf, chain, rootsList : Certificate; IN host : ARRAY OF SHORTCHAR;
			flags : SET; VAR err : AO.SIGNED32): BOOLEAN;
		VAR current, issuer, pool : Certificate; depth, intermediates : AO.SIZE;
	BEGIN
		err := Ok;
		IF leaf = NIL THEN
			err := ErrMalformed;
			RETURN FALSE
		END;
		current := leaf;
		depth := 0;
		intermediates := 0;
		REPEAT
			IF current.critUnsupported THEN
				err := ErrUnsupported;
				RETURN FALSE
			END;
			err := CheckDate(current, flags);
			IF err # Ok THEN
				RETURN FALSE
			END;
			IF InList(current, rootsList) THEN
				IF HostOk(leaf, host) THEN
					RETURN TRUE
				END;
				err := ErrHostname;
				RETURN FALSE
			END;
			(* Издателя ищем среди промежуточных и корней. *)
			issuer := FindIssuer(current, chain);
			IF issuer = NIL THEN
				issuer := FindIssuer(current, rootsList)
			END;
			IF issuer = NIL THEN
				err := ErrUntrusted;
				RETURN FALSE
			END;
			IF ~CheckSignature(current, issuer) THEN
				err := ErrBadSignature;
				RETURN FALSE
			END;
			IF ~CheckIssuer(issuer, intermediates, flags, err) THEN
				RETURN FALSE
			END;
			IF InList(issuer, rootsList) THEN
				IF HostOk(leaf, host) THEN
					RETURN TRUE
				END;
				err := ErrHostname;
				RETURN FALSE
			END;
			(* pathLenConstraint считает только несамоподписанные
			   промежуточные сертификаты (RFC 5280, 6.1.4). *)
			IF ~SelfIssued(issuer) THEN
				INC(intermediates)
			END;
			current := issuer;
			INC(depth);
			IF depth > MaxChainDepth THEN
				err := ErrUntrusted;
				RETURN FALSE
			END
		UNTIL FALSE;
		err := ErrUntrusted;
		RETURN FALSE
	END VerifyChain;

	PROCEDURE ReadPemPrivateKey* (IN pem : ARRAY OF SHORTCHAR; VAR p, q, e : AosBigNumbers.BigNumber;
			VAR res : AO.SIGNED32): BOOLEAN;
		CONST
			RsaHeader = "-----BEGIN RSA PRIVATE KEY-----";
			Pkcs8Header = "-----BEGIN PRIVATE KEY-----";
		VAR
			start, stop, derLen, len0 : AO.SIZE;
			der, inner : ARRAY MaxPem OF SHORTCHAR;
			r, r2 : Streams.StringReader;
			root, seq, field, version : ASN1.Triplet;
			n, d, pp, qq, ee : ASN1.Triplet;
			nBn, prod : AosBigNumbers.BigNumber;
			ok : BOOLEAN;
	BEGIN
		res := ErrMalformed;
		IF FindPem(pem, 0, RsaHeader, start, stop) THEN
			ok := TRUE
		ELSIF FindPem(pem, 0, Pkcs8Header, start, stop) THEN
			ok := TRUE
		ELSE
			res := ErrUnsupported;
			RETURN FALSE
		END;
		IF ~ok OR ~PemToDer(pem, start, stop, der, derLen) THEN
			RETURN FALSE
		END;
		NEW(r);
		r.InitStringReader(derLen);
		r.SetRaw(der, 0, derLen);
		root := ASN1.Decode(r, 0, len0);
		IF (root = NIL) OR ~root.ok OR (root.child = NIL) THEN
			RETURN FALSE
		END;
		seq := root;
		IF (root.child.next # NIL) & (root.child.next.class = ASN1.ASN1Universal)
			& (root.child.next.tag = ASN1.Sequence) THEN
			(* PKCS#8: PrivateKeyInfo -> OCTET STRING с PKCS#1. *)
			field := root.child.next.next;
			IF (field = NIL) OR (field.tag # ASN1.String) OR (field.svalue = NIL) THEN
				RETURN FALSE
			END;
			NEW(r2);
			r2.InitStringReader(LEN(field.svalue^));
			r2.SetRaw(field.svalue^, 0, LEN(field.svalue^));
			seq := ASN1.Decode(r2, 0, len0);
			IF (seq = NIL) OR ~seq.ok OR (seq.child = NIL) THEN
				RETURN FALSE
			END
		END;
		(* PKCS#1 RSAPrivateKey: version, n, e, d, p, q, ... *)
		version := seq.child;
		n := version.next;
		IF (n = NIL) OR (n.next = NIL) OR (n.next.next = NIL)
			OR (n.next.next.next = NIL) OR (n.next.next.next.next = NIL) THEN
			RETURN FALSE
		END;
		ee := n.next;
		d := ee.next;
		pp := d.next;
		qq := pp.next;
		IF (pp.svalue = NIL) OR (qq.svalue = NIL) OR (ee.svalue = NIL) THEN
			RETURN FALSE
		END;
		AosBigNumbers.AssignBin(p, pp.svalue^, 0, LEN(pp.svalue^));
		AosBigNumbers.AssignBin(q, qq.svalue^, 0, LEN(qq.svalue^));
		AosBigNumbers.AssignBin(e, ee.svalue^, 0, LEN(ee.svalue^));
		(* Проверка согласованности ключа: p*q должно совпасть с n. *)
		IF (n.svalue # NIL) & (LEN(n.svalue^) > 0) THEN
			AosBigNumbers.AssignBin(nBn, n.svalue^, 0, LEN(n.svalue^));
			prod := AosBigNumbers.Mul(p, q);
			IF AosBigNumbers.Cmp(prod, nBn) # 0 THEN
				res := ErrMalformed;
				RETURN FALSE
			END
		END;
		res := Ok;
		RETURN (p # NIL) & (q # NIL) & (e # NIL)
	END ReadPemPrivateKey;

	PROCEDURE ReadPemEcPrivateKey* (IN pem : ARRAY OF SHORTCHAR; VAR curve : EC.Curve;
			VAR d : EC.BigInt; VAR res : AO.SIGNED32): BOOLEAN;
		CONST
			EcHeader = "-----BEGIN EC PRIVATE KEY-----";
			Pkcs8Header = "-----BEGIN PRIVATE KEY-----";
		VAR
			start, stop, derLen, len0, n, i : AO.SIZE;
			der : ARRAY MaxPem OF SHORTCHAR;
			buf : ARRAY 128 OF SHORTCHAR;
			r, r2 : Streams.StringReader;
			root, seq, version, priv, params, alg, inner : ASN1.Triplet;
			sec1 : BOOLEAN;
	BEGIN
		res := ErrMalformed;
		curve := NIL;
		IF FindPem(pem, 0, EcHeader, start, stop) THEN
			sec1 := TRUE
		ELSIF FindPem(pem, 0, Pkcs8Header, start, stop) THEN
			sec1 := FALSE
		ELSE
			res := ErrUnsupported;
			RETURN FALSE
		END;
		IF ~PemToDer(pem, start, stop, der, derLen) THEN
			RETURN FALSE
		END;
		NEW(r);
		r.InitStringReader(derLen);
		r.SetRaw(der, 0, derLen);
		root := ASN1.Decode(r, 0, len0);
		IF (root = NIL) OR ~root.ok OR (root.child = NIL) THEN
			RETURN FALSE
		END;
		seq := root;
		IF ~sec1 THEN
			(* PKCS#8: PrivateKeyInfo { version, AlgorithmIdentifier,
			   privateKey OCTET STRING }. *)
			alg := root.child.next;
			IF (alg = NIL) OR (alg.child = NIL) THEN
				RETURN FALSE
			END;
			params := alg.child.next;
			IF OidIs(params, OidPrime256v1, 8) THEN
				curve := EC.P256()
			ELSIF OidIs(params, OidSecp384r1, 5) THEN
				curve := EC.P384()
			ELSE
				res := ErrUnsupported;
				RETURN FALSE
			END;
			priv := alg.next;
			IF (priv = NIL) OR (priv.tag # ASN1.String) OR (priv.svalue = NIL) THEN
				RETURN FALSE
			END;
			NEW(r2);
			r2.InitStringReader(LEN(priv.svalue^));
			r2.SetRaw(priv.svalue^, 0, LEN(priv.svalue^));
			inner := ASN1.Decode(r2, 0, len0);
			IF (inner = NIL) OR ~inner.ok OR (inner.child = NIL) THEN
				RETURN FALSE
			END;
			seq := inner
		ELSE
			(* SEC1: ECPrivateKey { version, privateKey OCTET STRING,
			   [0] parameters OID, ... }. *)
			params := root.child.next.next;
			IF (params = NIL) OR (params.class # ASN1.ASN1Context)
				OR (params.child = NIL) THEN
				res := ErrUnsupported;
				RETURN FALSE
			END;
			IF OidIs(params.child, OidPrime256v1, 8) THEN
				curve := EC.P256()
			ELSIF OidIs(params.child, OidSecp384r1, 5) THEN
				curve := EC.P384()
			ELSE
				res := ErrUnsupported;
				RETURN FALSE
			END
		END;
		version := seq.child;
		priv := version.next;
		IF (priv = NIL) OR (priv.tag # ASN1.String) OR (priv.svalue = NIL) THEN
			RETURN FALSE
		END;
		n := 4 * curve.n;
		IF (LEN(priv.svalue^) = 0) OR (LEN(priv.svalue^) > n) THEN
			RETURN FALSE
		END;
		FOR i := 0 TO n - 1 DO buf[i] := 0X END;
		FOR i := 0 TO LEN(priv.svalue^) - 1 DO
			buf[n - LEN(priv.svalue^) + i] := priv.svalue^[i]
		END;
		IF ~EC.OrderFromBytes(curve, buf, 0, n, d) OR ~EC.OrderInRange(curve, d) THEN
			RETURN FALSE
		END;
		res := Ok;
		RETURN TRUE
	END ReadPemEcPrivateKey;

	PROCEDURE Check (b : BOOLEAN);
	BEGIN
		ASSERT(b)
	END Check;

END CryptoX509.
