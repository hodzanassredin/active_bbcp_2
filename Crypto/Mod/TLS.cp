MODULE CryptoTLS;(** AUTHOR "F.N."; PURPOSE "RFC 2246: Transport Layer Security v1.0"; *)
(*
	05.2014 -- Timothée Martiel -- Implemented TLS client connection.
*)

	IMPORT SYSTEM, IP := CryptoAosIP, TCP := CryptoAosTCP, Streams := CryptoAosStreams, Files := CryptoAosFileStreams, StdLog, Kernel, BIT:= CryptoAosBIT, Dates:= CryptoAosDates, Strings := CryptoAosStrings, Ciphers := CryptoCiphers, HMAC := CryptoHMAC, Rng := CryptoFortunaRng, CryptoHashes, CryptoMD5, CryptoSHA1, CryptoRSA, AosBigNumbers := CryptoAosBigNumbers, PKCS1 := CryptoPKCS1, X509 := CryptoX509, AO := CryptoAosCompat, Pipes := CryptoAosPipes, Services, X25519 := CryptoX25519, Gcm := CryptoGcm, U := CryptoUtils, SysFiles := Files,
		EC := CryptoEC, ECDSA := CryptoECDSA, PSS := CryptoPSS, Ed25519 := CryptoEd25519, ChaCha20 := CryptoChaCha20;

	CONST
	(* debugging *)
		Trace = FALSE;
	(** tlsStates *)
		ServerHandshake* = 12;
		ClientHandshake* = 13;
	(* client and server common handshake states *)
		NotAValidHandshakeState = 100;
		HandshakeFinished = 101;
		GenerateChangeCipherSpec = 102;
		AwaitFinished = 103;
	(* server-side handshake states *)
		AwaitClientHello = 201;
		GenerateServerHello = 202;
		GenerateServerHelloDone = 203;
		AwaitClientKeyExchange = 204;
		AwaitChangeSpec = 205;
		GenerateHelloWithResumption = 210;
		AwaitChangeSpecWithResumption = 211;
		AwaitFinishedWithResumption = 212;
	(* client-side handshake states *)
		GenerateClientHello = 300;
		AwaitServerHello = 301;
		AwaitCertificate = 302;
		AwaitServerKeyExchange = 303;
		AwaitServerHelloDone = 304;
		GenerateClientKeyExchange = 305;

	(** available cipher suites *)
		TlsRsaWithNullMd5* = 1H;
		TlsRsaWithNullSha* = 2H;
		TlsRsaWithRc4128Md5* = 4H;
		TlsRsaWithRc4128Sha* = 5H;
		TlsRsaWithIdeaCbcSha* = 7H;
		TlsRsaWithDesCbcSha* = 9H;
		TlsRsaWith3DesEdeCbcSha* = 0AH;
				(* not supported
				TlsDhDssWithDesCbcSha* = 000CH; 
				TlsDhDssWith3DesEdeCbcSha* = 000DH;
				TlsDhRsaWithDesCbcSha* = 000FH; 
				TlsDhRsaWith3DesEdeCbcSha* = 0010H;
				TlsDhAnonWithRc4128Md5* = 0018H; 
				TlsDhAnonWithDesCbcSha* = 001AH; 
				TlsDhAnonWith3DesEdeCbcSha* = 001BH;
				*)
	(* TLS 1.2 cipher suites *)
		TlsRsaWithAes128CbcSha* = 2FH;
				(* not supported
				TlsDhDssWithAes128CbcSha* = 0030H; 
				TlsDhRsaWithAes128CbcSha* = 0031H;
				TlsDheDssWithAes128CbcSha* = 0032H; 
				TlsDheRsaWithAes128CbcSha* = 0033H;
				TlsDhAnonWithAes128CbcSha* = 0034H;
				*)
		TlsRsaWithAes256CbcSha* = 35H; (*TLS_RSA_WITH_AES_256_CBC_SHA*)

				TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256  = 0C027H;(*49191*)
				TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256  = 0C02FH;
		(* Современные наборы TLS 1.2 (RFC 5289, 7905). *)
		TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256* = 0C02BH;
		TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384* = 0C02CH;
		TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384* = 0C030H;
		TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256* = 0CCA8H;
		TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256* = 0CCA9H;

		(* Группы ECDHE (named groups, RFC 8422, 8446). *)
		GroupSecp256r1* = 23;
		GroupSecp384r1* = 24;
		GroupX25519* = 29;
		MaxGroupPublic = 97;
		MaxSharedSecret = 48;

		(* SignatureScheme (RFC 8446, 4.2.3); в TLS 1.2 те же пары
		   (hash, signature) для PKCS#1/ECDSA. *)
		SigRsaPkcs1Sha256* = 0401H;
		SigRsaPkcs1Sha384* = 0501H;
		SigEcdsaP256Sha256* = 0403H;
		SigEcdsaP384Sha384* = 0503H;
		SigRsaPssRsaeSha256* = 0804H;
		SigRsaPssRsaeSha384* = 0805H;
		SigEd25519* = 0807H;
				
				(* not supported
				TlsDhDssWithAes256CbcSha* = 0036H; 
				TlsDhRsaWithAes256CbcSha* = 0037H;
				TlsDheDssWithAes256CbcSha* = 0038H; 
				TlsDheRsaWithAes256CbcSha* = 0039H;
				TlsDhAnonWithAes256CbcSha* = 003AH;
				*)

				(** record layer content type *)
		ChangeCipherSpec* = 20;
		Alert* = 21;
		Handshake* = 22;
		ApplicationData* = 23;
		SSLv2ClientHello* = 128;
		HelloRequest* = 0;
		ClientHello* = 1;
		ServerHello* = 2;
		Certificate* = 11;
		ServerKeyExchange* = 12;
		CertificateRequest* = 13;
		ServerHelloDone* = 14;
		CertificateVerify* = 15;
		ClientKeyExchange* = 16;
		Finished* = 20;
		V2ClientHello* = 30;
		Warning = 1;
		Fatal = 2;
		NoError = (-1);
		CloseNotify = 0;
		UnexpectedMessage = 10;
		BadRecordMac = 20;
		DecryptionFailed = 21;
		RecordOverflow = 22;
		DecompressionFailure = 30;
		HandshakeFailure = 40;
		NoCertificate = 41;
		BadCertificate = 42;
		UnsupportedCertificate = 43;
		CertificateRevoked = 44;
		CertificateExpired = 45;
		CertificateUnknown = 46;
		IllegalParameter = 47;
		UnknownCA = 48;
		AccessDenied = 49;
		DecodeError = 50;
		DecryptError = 51;
		ExportRestriction = 60;
		ProtocolVersion = 70;
		InsufficientSecurity = 71;
		InternalError = 80;
		UserCancelled = 90;
		NoRenegotiation = 100;
		UnsupportedExtension = 110;
		Ok* = 0;
		TLSHandshakeAborted* = 2;
		Suites* = 20;
		Buflen = 18500;
		MaxPHashKernelLogput = 1024;
		MaxKeyBlock = 144;
		MaxPHashSeed = 128;
		MaxPlaintextLength = 16384;
		MaxCompressedLength = 17408;
		MaxCiphertextLength = 18432;
		SessionIdLength = 16;
		SessionHashtableSize = 1023;
		SessionCleanUpInterval = 60;
		DefaultSessionLifetime = 3600;
		DefaultSuitesNbr = 6;
		MaxSignatureLength = 512;
		MaxCertificateChain = 8;

	TYPE
		(* Эфемерная группа ECDHE: открытый ключ в формате TLS и
		   общий секрет. Реализации: X25519 (шаг 1), P-256/P-384
		   (шаг 2). *)
		Group = POINTER TO ABSTRACT RECORD
			id : AO.SIGNED32;
			publicLen, secretLen : AO.SIZE;
			publicKey : ARRAY MaxGroupPublic OF SHORTCHAR
		END;
		X25519Group = POINTER TO RECORD(Group)
			keys : X25519.X25519KeyPair
		END;
		EcGroup = POINTER TO RECORD(Group)
			curve : EC.Curve;
			priv : EC.BigInt
		END;

		(* AEAD записи TLS 1.2: Seal/Open, длины nonce и тега.
		   GCM — 4 байта соли и 8 байт явного nonce в записи;
		   ChaCha20-Poly1305 (RFC 7905) — 12 байт соли, nonce из
		   номера записи, явной части нет. *)
		RecordAEAD = POINTER TO ABSTRACT RECORD
			tagLen, explicitLen, saltLen : AO.SIZE
		END;
		GcmRecordAEAD = POINTER TO RECORD(RecordAEAD)
			mode : Gcm.GcmMode;
			cipher : Ciphers.Cipher;
			salt : ARRAY 4 OF SHORTCHAR
		END;
		ChaChaRecordAEAD = POINTER TO RECORD(RecordAEAD)
			key : ARRAY 32 OF SHORTCHAR;
			nonce : ARRAY 12 OF SHORTCHAR
		END;

		SecurityParameters = POINTER TO EXTENSIBLE RECORD
			cipherSuite, cipherKeySize : AO.SIGNED32;
			clientRandom, serverRandom : ARRAY 32 OF SHORTCHAR;
			PRFAlgorithm : ARRAY 32 OF SHORTCHAR
		END;
		ConnectionState = POINTER TO EXTENSIBLE RECORD
			cipher : Ciphers.Cipher;
			aead : RecordAEAD;
			mac : HMAC.HMac;
			recordSeq : DoubleLong;
			cipherKey : ARRAY 32 OF SHORTCHAR;
			iv : ARRAY 16 OF SHORTCHAR;
			macSecret : ARRAY 32 OF SHORTCHAR
		END;
		DoubleLong = POINTER TO EXTENSIBLE RECORD
			numH, numL : AO.SIGNED32
		END;
		PHash = POINTER TO EXTENSIBLE RECORD
			hMac : HMAC.HMac
		END;
		PRF = POINTER TO EXTENSIBLE RECORD
			pMD5, pSHA : PHash;
			pHash : PHash
		END;
		Policy* = POINTER TO EXTENSIBLE RECORD
			cipherSuites : ARRAY Suites OF AO.SIGNED32;
			nofCipherSuites : AO.SIZE;
			sessionResumptionEnabled : BOOLEAN;
			sessionLifetime : AO.SIGNED32;
			version : ARRAY 2 OF SHORTCHAR
		END;
		Session = POINTER TO EXTENSIBLE RECORD
			id : ARRAY 32 OF SHORTCHAR;
			idLength : AO.SIZE;
			resumable : BOOLEAN;
			cipherSuite : AO.SIGNED32;
			preMasterSecret, masterSecret : ARRAY 48 OF SHORTCHAR;
			preMasterSecretLen:INTEGER;
			lifetime : AO.SIGNED32;
			next : Session;
			sessionInvalidator : SessionInvalidator;
		END;
		Context* = POINTER TO EXTENSIBLE RECORD
			policy : Policy;
			rsaCertificate : ARRAY 10000 OF SHORTCHAR;
			rsaPrivateKey, rsaPublicKey : CryptoRSA.Key;
			ecCurve : EC.Curve;
			ecPrivate : EC.BigInt;
			lengthOfRsaCertificate : AO.SIZE;
			sessionCache : ARRAY SessionHashtableSize OF Session;
			active : BOOLEAN;
			sessionCacheCleaner : SessionCacheCleaner;
			(* Проверка сертификатов: доверенные корни, своя цепочка,
			   необязательный клиентский сертификат. *)
			trustedRoots : X509.Certificate;
			certChain : X509.Certificate;
			clientCertChain : X509.Certificate;
			clientPrivateKey : CryptoRSA.Key;
			clientEcCurve : EC.Curve;
			clientEcPrivate : EC.BigInt;
			requireClientCert : BOOLEAN;
			verifyFlags : SET;
			owned : BOOLEAN
		END;
		Connection* = POINTER TO EXTENSIBLE RECORD(TCP.Connection)
			conn : TCP.Connection;
			(* "in", "out" are tcp stream reader writers, decoded data from "in" stream pushes into appdatawriter and finaly to applicationDataPipe, cosumers read decoded data directly from  applicationDataPipe, appDataReader unused, consumers send raw data by Send and write encoded data into out *)
			in, appDataReader : Streams.Reader;
			out, appDataWriter : Streams.Writer;
			(* async bounded buffer*)
			applicationDataPipe : Pipes.Pipe; 
			context : Context;
			session : Session;
			tlsErrorCause : AO.SIGNED32;
			handshakeState : AO.SIGNED32;
			outbuf, inbuf : ARRAY Buflen OF SHORTCHAR;
			hsMD5send, hsMD5verify : CryptoMD5.Hash;
			hsSHAsend, hsSHAverify : CryptoSHA1.Hash;
			hsHashSend, hsHashVerify : CryptoHashes.Hash;
			pendingSecurityParameters : SecurityParameters;
			currentWriteState, pendingWriteState, currentReadState, pendingReadState : ConnectionState;
			version : ARRAY 2 OF SHORTCHAR;
			client, trapped : BOOLEAN;
			asyncWriter : AsyncWriter;
			serverName : ARRAY 256 OF SHORTCHAR;
			group : Group;
			sharedSecret : ARRAY MaxSharedSecret OF SHORTCHAR;
			sharedLen : AO.SIZE;
			(* Выбор второй стороны из ClientHello. *)
			clientSigSchemes : SET;
			haveSigSchemes : BOOLEAN;
			clientGroups : SET;
			haveGroups : BOOLEAN;
			serverScheme : AO.SIGNED32;
			negotiatedSigScheme : AO.SIGNED32;
			(* Результаты проверки сертификатов. *)
			serverCert, clientCert : X509.Certificate;
			peerVerified, certificateRequested, ownsContext, clientCertVerified : BOOLEAN;
			(* Копия транскрипта рукопожатия: нужна, чтобы подписать
			   CertificateVerify в середине, не разрушая текущий хеш
			   (GetHash дописывает паддинг). *)
			transcript : POINTER TO ARRAY OF SHORTCHAR;
			transcriptLen, transcriptCap : AO.SIZE;
			finishedDigest : ARRAY 64 OF SHORTCHAR
		END;
		
		ConnectionReceiver = POINTER TO RECORD(Streams.Receiver) conn: Connection END;
		ConnectionSender = POINTER TO RECORD(Streams.Sender) conn: Connection END;
		AsyncWriter = POINTER TO RECORD(Pipes.AsyncAction);conn: Connection END;
		SessionInvalidator = POINTER TO RECORD(Services.Action) session:Session END;
		SessionCacheCleaner = POINTER TO RECORD(Services.Action) ctx:Context END;
		
	VAR
		defaultCipherPolicy: Policy;
		LastCertError*: AO.SIGNED32;

	(* ------------------- группа ECDHE ------------------- *)

	PROCEDURE (g: Group) Generate* , NEW, ABSTRACT;

	PROCEDURE (g: Group) Shared* (IN peer : ARRAY OF SHORTCHAR; peerLen : AO.SIZE;
			VAR secret : ARRAY OF SHORTCHAR; VAR secretLen : AO.SIZE): BOOLEAN, NEW, ABSTRACT;

	PROCEDURE (g: X25519Group) Generate ;
		VAR i : AO.SIZE;
	BEGIN
		X25519.GenerateKeyPair(g.keys);
		FOR i := 0 TO 31 DO
			g.publicKey[i] := g.keys.PublicKey[i]
		END;
		g.publicLen := 32;
		g.secretLen := 32
	END Generate;

	PROCEDURE (g: X25519Group) Shared (IN peer : ARRAY OF SHORTCHAR; peerLen : AO.SIZE;
			VAR secret : ARRAY OF SHORTCHAR; VAR secretLen : AO.SIZE): BOOLEAN;
		VAR ok : BOOLEAN; i : AO.SIZE; tmp, pk, sk : ARRAY 32 OF SHORTCHAR;
	BEGIN
		secretLen := 0;
		IF peerLen # 32 THEN
			RETURN FALSE
		END;
		FOR i := 0 TO 31 DO
			pk[i] := peer[i];
			sk[i] := g.keys.PrivateKey[i]
		END;
		ok := X25519.AgreementChecked(sk, pk, tmp);
		IF ~ok THEN
			RETURN FALSE
		END;
		FOR i := 0 TO 31 DO
			secret[i] := tmp[i]
		END;
		secretLen := 32;
		RETURN TRUE
	END Shared;

	PROCEDURE (g: EcGroup) Generate ;
		VAR raw : ARRAY 64 OF SHORTCHAR; x, y : EC.BigInt;
			n, tries : AO.SIZE; ok : BOOLEAN;
	BEGIN
		n := 4 * g.curve.n;
		tries := 0;
		ok := FALSE;
		WHILE ~ok & (tries < 64) DO
			INC(tries);
			IF ~U.RandomBytesOk(raw, 0, n) THEN
				tries := 64
			ELSIF EC.OrderFromBytes(g.curve, raw, 0, n, g.priv)
				& EC.OrderInRange(g.curve, g.priv) THEN
				EC.ScalarMulBase(g.curve, g.priv, x, y, ok)
			END
		END;
		IF ~ok THEN
			g.publicLen := 0;
			RETURN
		END;
		EC.PointToBytes(g.curve, x, y, g.publicKey);
		g.publicLen := 1 + 8 * g.curve.n;
		g.secretLen := n
	END Generate;

	PROCEDURE (g: EcGroup) Shared (IN peer : ARRAY OF SHORTCHAR; peerLen : AO.SIZE;
			VAR secret : ARRAY OF SHORTCHAR; VAR secretLen : AO.SIZE): BOOLEAN;
		VAR x, y, sx : EC.BigInt; n : AO.SIZE;
	BEGIN
		secretLen := 0;
		IF (g.curve = NIL) OR (peerLen # 1 + 8 * g.curve.n) THEN
			RETURN FALSE
		END;
		IF ~EC.PointFromBytes(g.curve, peer, peerLen, x, y) THEN
			RETURN FALSE
		END;
		IF ~EC.ECDH(g.curve, g.priv, x, y, sx) THEN
			RETURN FALSE
		END;
		n := 4 * g.curve.n;
		EC.GetBytes(g.curve, sx, secret, 0);
		secretLen := n;
		RETURN TRUE
	END Shared;

	PROCEDURE NewGroup* (id : AO.SIGNED32): Group;
		VAR g : X25519Group; e : EcGroup;
	BEGIN
		IF id = GroupX25519 THEN
			NEW(g);
			g.id := GroupX25519;
			g.publicLen := 32;
			g.secretLen := 32;
			RETURN g
		ELSIF (id = GroupSecp256r1) OR (id = GroupSecp384r1) THEN
			NEW(e);
			e.id := id;
			IF id = GroupSecp256r1 THEN
				e.curve := EC.P256()
			ELSE
				e.curve := EC.P384()
			END;
			e.publicLen := 1 + 8 * e.curve.n;
			e.secretLen := 4 * e.curve.n;
			RETURN e
		END;
		RETURN NIL
	END NewGroup;

	(* ------------------- AEAD записи ------------------- *)

	PROCEDURE (a: RecordAEAD) Init* (cipher : Ciphers.Cipher; IN key : ARRAY OF SHORTCHAR;
			keyLen : AO.SIZE; IN iv : ARRAY OF SHORTCHAR), NEW, ABSTRACT;

	PROCEDURE (a: RecordAEAD) Seal* (IN plain : ARRAY OF SHORTCHAR; plainOfs, plainLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			VAR out : ARRAY OF SHORTCHAR; outOfs : AO.SIZE;
			VAR explicit : ARRAY OF SHORTCHAR; VAR tag : ARRAY OF SHORTCHAR), NEW, ABSTRACT;

	PROCEDURE (a: RecordAEAD) Open* (IN cipherText : ARRAY OF SHORTCHAR; cipherOfs, cipherLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			IN explicit : ARRAY OF SHORTCHAR; IN tag : ARRAY OF SHORTCHAR;
			VAR plain : ARRAY OF SHORTCHAR; plainOfs : AO.SIZE): BOOLEAN, NEW, ABSTRACT;

	PROCEDURE (a: GcmRecordAEAD) Init (cipher : Ciphers.Cipher; IN key : ARRAY OF SHORTCHAR;
			keyLen : AO.SIZE; IN iv : ARRAY OF SHORTCHAR);
		VAR i : AO.SIZE;
	BEGIN
		a.cipher := cipher;
		FOR i := 0 TO 3 DO
			a.salt[i] := iv[i]
		END
	END Init;

	PROCEDURE (a: GcmRecordAEAD) Seal (IN plain : ARRAY OF SHORTCHAR; plainOfs, plainLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			VAR out : ARRAY OF SHORTCHAR; outOfs : AO.SIZE;
			VAR explicit : ARRAY OF SHORTCHAR; VAR tag : ARRAY OF SHORTCHAR);
		VAR nonce : ARRAY 12 OF SHORTCHAR; i : AO.SIZE;
	BEGIN
		FOR i := 0 TO 3 DO
			nonce[i] := a.salt[i]
		END;
		FOR i := 0 TO 7 DO
			nonce[4 + i] := seq[i];
			explicit[i] := seq[i]
		END;
		a.mode.Init(a.cipher, nonce, 12);
		a.mode.Encrypt(plain, aad, plainOfs, plainLen, aadLen, 8 * a.tagLen, out, tag, outOfs)
	END Seal;

	PROCEDURE (a: GcmRecordAEAD) Open (IN cipherText : ARRAY OF SHORTCHAR; cipherOfs, cipherLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			IN explicit : ARRAY OF SHORTCHAR; IN tag : ARRAY OF SHORTCHAR;
			VAR plain : ARRAY OF SHORTCHAR; plainOfs : AO.SIZE): BOOLEAN;
		VAR nonce : ARRAY 12 OF SHORTCHAR; i, res : AO.SIZE;
	BEGIN
		FOR i := 0 TO 3 DO
			nonce[i] := a.salt[i]
		END;
		FOR i := 0 TO 7 DO
			nonce[4 + i] := explicit[i]
		END;
		a.mode.Init(a.cipher, nonce, 12);
		a.mode.Decrypt(cipherText, aad, tag, cipherOfs, cipherLen, aadLen,
			8 * a.tagLen, plain, plainOfs, res);
		RETURN res = 0
	END Open;

	PROCEDURE (a: ChaChaRecordAEAD) Init (cipher : Ciphers.Cipher; IN key : ARRAY OF SHORTCHAR;
			keyLen : AO.SIZE; IN iv : ARRAY OF SHORTCHAR);
		VAR i : AO.SIZE;
	BEGIN
		FOR i := 0 TO 31 DO
			a.key[i] := key[i]
		END;
		FOR i := 0 TO 11 DO
			a.nonce[i] := iv[i]
		END
	END Init;

	(* RFC 7905: nonce = write_iv XOR seq_num(64), дополненный слева
	   четырьмя нулями; явной части nonce в записи нет. *)
	PROCEDURE (a: ChaChaRecordAEAD) Seal (IN plain : ARRAY OF SHORTCHAR; plainOfs, plainLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			VAR out : ARRAY OF SHORTCHAR; outOfs : AO.SIZE;
			VAR explicit : ARRAY OF SHORTCHAR; VAR tag : ARRAY OF SHORTCHAR);
		VAR n : ARRAY 12 OF SHORTCHAR; i : AO.SIZE;
	BEGIN
		FOR i := 0 TO 11 DO
			n[i] := a.nonce[i]
		END;
		FOR i := 0 TO 7 DO
			n[4 + i] := SHORT(CHR(ORD(BITS(ORD(n[4 + i])) / BITS(ORD(seq[i])))))
		END;
		ChaCha20.Encrypt(a.key, n, aad, aadLen, plain, plainLen, out, tag)
	END Seal;

	PROCEDURE (a: ChaChaRecordAEAD) Open (IN cipherText : ARRAY OF SHORTCHAR; cipherOfs, cipherLen : AO.SIZE;
			IN aad : ARRAY OF SHORTCHAR; aadLen : AO.SIZE; IN seq : ARRAY OF SHORTCHAR;
			IN explicit : ARRAY OF SHORTCHAR; IN tag : ARRAY OF SHORTCHAR;
			VAR plain : ARRAY OF SHORTCHAR; plainOfs : AO.SIZE): BOOLEAN;
		VAR n : ARRAY 12 OF SHORTCHAR; i : AO.SIZE; tmp : POINTER TO ARRAY OF SHORTCHAR;
	BEGIN
		FOR i := 0 TO 11 DO
			n[i] := a.nonce[i]
		END;
		FOR i := 0 TO 7 DO
			n[4 + i] := SHORT(CHR(ORD(BITS(ORD(n[4 + i])) / BITS(ORD(seq[i])))))
		END;
		(* ChaCha20.Decrypt не принимает смещение; копируем фрагмент. *)
		NEW(tmp, cipherLen);
		FOR i := 0 TO cipherLen - 1 DO
			tmp^[i] := cipherText[cipherOfs + i]
		END;
		IF ~ChaCha20.Decrypt(a.key, n, aad, aadLen, tmp^, cipherLen, tag, plain) THEN
			RETURN FALSE
		END;
		(* Результат уже записан в plain с нулевого смещения; переносим. *)
		IF plainOfs > 0 THEN
			FOR i := cipherLen - 1 TO 0 BY (-1) DO
				plain[plainOfs + i] := plain[i]
			END
		END;
		RETURN TRUE
	END Open;

	PROCEDURE NewGcmRecordAEAD (): RecordAEAD;
		VAR a : GcmRecordAEAD;
	BEGIN
		NEW(a);
		NEW(a.mode);
		a.tagLen := 16;
		a.explicitLen := 8;
		a.saltLen := 4;
		RETURN a
	END NewGcmRecordAEAD;

	PROCEDURE NewChaChaRecordAEAD (): RecordAEAD;
		VAR a : ChaChaRecordAEAD;
	BEGIN
		NEW(a);
		a.tagLen := 16;
		a.explicitLen := 0;
		a.saltLen := 12;
		RETURN a
	END NewChaChaRecordAEAD;

	PROCEDURE^ IsGcmSuite (cipherSuite : AO.SIGNED32): BOOLEAN;
	PROCEDURE^ IsChaChaSuite (cipherSuite : AO.SIGNED32): BOOLEAN;

	PROCEDURE NewRecordAEAD (cipherSuite : AO.SIGNED32): RecordAEAD;
	BEGIN
		IF IsGcmSuite(cipherSuite) THEN
			RETURN NewGcmRecordAEAD()
		ELSIF IsChaChaSuite(cipherSuite) THEN
			RETURN NewChaChaRecordAEAD()
		END;
		RETURN NIL
	END NewRecordAEAD;

	(* ------------------- подпись рукопожатия ------------------- *)

	PROCEDURE IsGcmSuite (cipherSuite : AO.SIGNED32): BOOLEAN;
	BEGIN
		RETURN (cipherSuite = TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256)
			OR (cipherSuite = TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
			OR (cipherSuite = TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
			OR (cipherSuite = TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384)
	END IsGcmSuite;

	PROCEDURE IsChaChaSuite (cipherSuite : AO.SIGNED32): BOOLEAN;
	BEGIN
		RETURN (cipherSuite = TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256)
			OR (cipherSuite = TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256)
	END IsChaChaSuite;

	(* Сертификат сервера обязан быть того же вида, что и набор. *)
	PROCEDURE IsEcdsaSuite (cipherSuite : AO.SIGNED32): BOOLEAN;
	BEGIN
		RETURN (cipherSuite = TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256)
			OR (cipherSuite = TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
			OR (cipherSuite = TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256)
	END IsEcdsaSuite;

	PROCEDURE IsEcdheSuite (cipherSuite : AO.SIGNED32): BOOLEAN;
	BEGIN
		RETURN (cipherSuite = TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256)
			OR IsGcmSuite(cipherSuite) OR IsChaChaSuite(cipherSuite)
	END IsEcdheSuite;

	PROCEDURE SchemeBit (scheme : AO.SIGNED32): AO.SIGNED32;
	BEGIN
		CASE scheme OF
			SigRsaPkcs1Sha256 : RETURN 0
		|       SigRsaPkcs1Sha384 : RETURN 1
		|       SigEcdsaP256Sha256 : RETURN 2
		|       SigEcdsaP384Sha384 : RETURN 3
		|       SigRsaPssRsaeSha256 : RETURN 4
		|       SigRsaPssRsaeSha384 : RETURN 5
		|       SigEd25519 : RETURN 6
		ELSE
			RETURN (-1)
		END
	END SchemeBit;

	PROCEDURE GroupBit (id : AO.SIGNED32): AO.SIGNED32;
	BEGIN
		CASE id OF
			GroupX25519 : RETURN 0
		|       GroupSecp256r1 : RETURN 1
		|       GroupSecp384r1 : RETURN 2
		ELSE
			RETURN (-1)
		END
	END GroupBit;

	PROCEDURE ClientSchemeSupported (scheme : AO.SIGNED32): BOOLEAN;
		VAR b : AO.SIGNED32;
	BEGIN
		b := SchemeBit(scheme);
		RETURN b >= 0
	END ClientSchemeSupported;

	PROCEDURE ServerHasEc (ctx : Context): BOOLEAN;
	BEGIN
		RETURN (ctx # NIL) & (ctx.certChain # NIL) & (ctx.certChain.keyType = X509.KeyEc)
			& (ctx.ecCurve # NIL)
	END ServerHasEc;

	PROCEDURE ServerHasRsa (ctx : Context): BOOLEAN;
	BEGIN
		RETURN (ctx # NIL) & (ctx.rsaPrivateKey # NIL)
	END ServerHasRsa;

	(* Набор должен подходить к ключу сертификата. *)
	PROCEDURE SuiteCompatible (ctx : Context; cipherSuite : AO.SIGNED32): BOOLEAN;
	BEGIN
		IF IsEcdsaSuite(cipherSuite) THEN
			RETURN ServerHasEc(ctx)
		END;
		RETURN ServerHasRsa(ctx)
	END SuiteCompatible;

	PROCEDURE ServerScheme (SELF : Connection): AO.SIGNED32;
		VAR offered : SET;
	BEGIN
		offered := SELF.clientSigSchemes;
		IF ServerHasEc(SELF.context) THEN
			IF SELF.context.ecCurve.n = 8 THEN
				IF ~SELF.haveSigSchemes OR (2 IN offered) THEN RETURN SigEcdsaP256Sha256 END
			ELSIF SELF.context.ecCurve.n = 12 THEN
				IF ~SELF.haveSigSchemes OR (3 IN offered) THEN RETURN SigEcdsaP384Sha384 END
			END;
			RETURN 0
		END;
		IF ServerHasRsa(SELF.context) THEN
			IF ~SELF.haveSigSchemes THEN RETURN SigRsaPkcs1Sha256 END;
			IF 4 IN offered THEN RETURN SigRsaPssRsaeSha256 END;
			IF 5 IN offered THEN RETURN SigRsaPssRsaeSha384 END;
			IF 0 IN offered THEN RETURN SigRsaPkcs1Sha256 END;
			IF 1 IN offered THEN RETURN SigRsaPkcs1Sha384 END
		END;
		RETURN 0
	END ServerScheme;

	PROCEDURE ServerGroup (SELF : Connection): AO.SIGNED32;
	BEGIN
		(* OpenSSL (и не только) требует в TLS 1.2, чтобы кривая
		   ECDSA-подписи совпадала с группой ECDHE. *)
		IF ServerHasEc(SELF.context) THEN
			IF SELF.context.ecCurve.n = 8 THEN
				IF ~SELF.haveGroups OR (1 IN SELF.clientGroups) THEN
					RETURN GroupSecp256r1
				END
			ELSIF SELF.context.ecCurve.n = 12 THEN
				IF ~SELF.haveGroups OR (2 IN SELF.clientGroups) THEN
					RETURN GroupSecp384r1
				END
			END;
			RETURN 0
		END;
		IF ~SELF.haveGroups THEN RETURN GroupX25519 END;
		IF 0 IN SELF.clientGroups THEN RETURN GroupX25519 END;
		IF 1 IN SELF.clientGroups THEN RETURN GroupSecp256r1 END;
		IF 2 IN SELF.clientGroups THEN RETURN GroupSecp384r1 END;
		RETURN 0
	END ServerGroup;

	PROCEDURE TLSNewHash (scheme : AO.SIGNED32): CryptoHashes.Hash;
	BEGIN
		CASE scheme OF
			SigRsaPkcs1Sha256, SigEcdsaP256Sha256, SigRsaPssRsaeSha256 :
				RETURN CryptoHashes.NewHash("CryptoSHA256")
		|       SigRsaPkcs1Sha384, SigEcdsaP384Sha384, SigRsaPssRsaeSha384 :
				RETURN CryptoHashes.NewHash("CryptoSHA384")
		ELSE
			RETURN NIL
		END
	END TLSNewHash;

	PROCEDURE DerToBig (c : EC.Curve; IN buf : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE;
			VAR v : EC.BigInt): BOOLEAN;
		VAR tmp : ARRAY 64 OF SHORTCHAR; n, start, i : AO.SIZE;
	BEGIN
		n := 4 * c.n;
		IF len <= 0 THEN RETURN FALSE END;
		start := 0;
		WHILE (start < len - 1) & (buf[ofs + start] = 0X) DO INC(start) END;
		(* Отрицательное число (старший бит без ведущего нуля). *)
		IF (start = 0) & (ORD(buf[ofs]) >= 128) THEN RETURN FALSE END;
		IF len - start > n THEN RETURN FALSE END;
		FOR i := 0 TO n - 1 DO tmp[i] := 0X END;
		FOR i := start TO len - 1 DO
			tmp[n - (len - start) + (i - start)] := buf[ofs + i]
		END;
		RETURN EC.OrderFromBytes(c, tmp, 0, n, v)
	END DerToBig;

	(* Хеш как целое для ECDSA: если хеш короче порядка, он дополняется
	   нулями слева; если длиннее — берутся старшие 4n байт. *)
	PROCEDURE EcdsaHash (c : EC.Curve; IN digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			VAR buf : ARRAY OF SHORTCHAR; VAR len : AO.SIZE): BOOLEAN;
		VAR n, i : AO.SIZE;
	BEGIN
		len := 0;
		IF (c = NIL) OR (digestLen <= 0) THEN RETURN FALSE END;
		n := 4 * c.n;
		FOR i := 0 TO n - 1 DO buf[i] := 0X END;
		IF digestLen >= n THEN
			FOR i := 0 TO n - 1 DO buf[i] := digest[i] END
		ELSE
			FOR i := 0 TO digestLen - 1 DO
				buf[n - digestLen + i] := digest[i]
			END
		END;
		len := n;
		RETURN TRUE
	END EcdsaHash;

	(* ECDSA-Sig-Value ::= SEQUENCE { r INTEGER, s INTEGER }. *)
	PROCEDURE EcdsaVerifyDer (c : EC.Curve; IN qx, qy : EC.BigInt;
			IN digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			IN sig : ARRAY OF SHORTCHAR; sigOfs, sigLen : AO.SIZE): BOOLEAN;
		VAR r, s : EC.BigInt; pos, seqLen, rLen, sLen, hLen : AO.SIZE;
			hbuf : ARRAY 64 OF SHORTCHAR;
	BEGIN
		IF (c = NIL) OR (sigLen < 8) OR (sig[sigOfs] # 30X) THEN RETURN FALSE END;
		IF ~EcdsaHash(c, digest, digestLen, hbuf, hLen) THEN RETURN FALSE END;
		seqLen := ORD(sig[sigOfs + 1]);
		IF seqLen + 2 # sigLen THEN RETURN FALSE END;
		pos := sigOfs + 2;
		IF sig[pos] # 02X THEN RETURN FALSE END;
		rLen := ORD(sig[pos + 1]);
		IF pos + 2 + rLen + 2 > sigOfs + sigLen THEN RETURN FALSE END;
		IF ~DerToBig(c, sig, pos + 2, rLen, r) THEN RETURN FALSE END;
		INC(pos, 2 + rLen);
		IF sig[pos] # 02X THEN RETURN FALSE END;
		sLen := ORD(sig[pos + 1]);
		IF pos + 2 + sLen # sigOfs + sigLen THEN RETURN FALSE END;
		IF ~DerToBig(c, sig, pos + 2, sLen, s) THEN RETURN FALSE END;
		RETURN ECDSA.Verify(c, qx, qy, hbuf, hLen, r, s)
	END EcdsaVerifyDer;

	PROCEDURE PutDerInt (c : EC.Curve; IN v : EC.BigInt; VAR out : ARRAY OF SHORTCHAR;
			ofs : AO.SIZE): AO.SIZE;
		VAR raw : ARRAY 64 OF SHORTCHAR; n, start, len, i : AO.SIZE;
	BEGIN
		n := 4 * c.n;
		EC.OrderBytes(c, v, raw, 0);
		start := 0;
		WHILE (start < n - 1) & (raw[start] = 0X) DO INC(start) END;
		len := n - start;
		out[ofs] := 02X;
		IF ORD(raw[start]) >= 128 THEN
			out[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(len + 1));
			out[ofs + 2] := 0X;
			FOR i := 0 TO len - 1 DO out[ofs + 3 + i] := raw[start + i] END;
			RETURN 3 + len
		END;
		out[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(len));
		FOR i := 0 TO len - 1 DO out[ofs + 2 + i] := raw[start + i] END;
		RETURN 2 + len
	END PutDerInt;

	PROCEDURE EcdsaSignDer (c : EC.Curve; IN priv : EC.BigInt;
			IN digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			VAR sig : ARRAY OF SHORTCHAR; sigOfs : AO.SIZE; VAR sigLen : AO.SIZE): BOOLEAN;
		VAR r, s : EC.BigInt; body : ARRAY 160 OF SHORTCHAR; n, m, pos, hLen : AO.SIZE;
			hbuf : ARRAY 64 OF SHORTCHAR;
	BEGIN
		sigLen := 0;
		IF (c = NIL) OR ~EcdsaHash(c, digest, digestLen, hbuf, hLen) THEN RETURN FALSE END;
		IF ~ECDSA.Sign(c, priv, hbuf, hLen, r, s) THEN RETURN FALSE END;
		pos := 2;
		INC(pos, PutDerInt(c, r, body, pos));
		INC(pos, PutDerInt(c, s, body, pos));
		n := pos - 2;
		body[0] := 30X;
		body[1] := (*WARN CONV CAST*)SHORT(CHR(n));
		IF n > 127 THEN RETURN FALSE END;
		IF sigOfs + 2 + n > LEN(sig) THEN RETURN FALSE END;
		FOR m := 0 TO 2 + n - 1 DO sig[sigOfs + m] := body[m] END;
		sigLen := 2 + n;
		RETURN TRUE
	END EcdsaSignDer;

	PROCEDURE TLSSignDigest (scheme : AO.SIGNED32; rsaKey : CryptoRSA.Key;
			ecCurve : EC.Curve; IN ecPriv : EC.BigInt;
			IN digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			VAR sig : ARRAY OF SHORTCHAR; sigOfs : AO.SIZE; VAR sigLen : AO.SIZE): BOOLEAN;
	BEGIN
		sigLen := 0;
		CASE scheme OF
			SigRsaPkcs1Sha256 :
				IF rsaKey = NIL THEN RETURN FALSE END;
				PKCS1.SignHash("CryptoSHA256", digest, digestLen, rsaKey, sig, sigOfs, sigLen)
		|       SigRsaPkcs1Sha384 :
				IF rsaKey = NIL THEN RETURN FALSE END;
				PKCS1.SignHash("CryptoSHA384", digest, digestLen, rsaKey, sig, sigOfs, sigLen)
		|       SigRsaPssRsaeSha256 :
				IF (rsaKey = NIL) OR (digestLen # 32) THEN RETURN FALSE END;
				PSS.SignHash("CryptoSHA256", digest, digestLen, rsaKey, sig, sigOfs, sigLen)
		|       SigRsaPssRsaeSha384 :
				IF (rsaKey = NIL) OR (digestLen # 48) THEN RETURN FALSE END;
				PSS.SignHash("CryptoSHA384", digest, digestLen, rsaKey, sig, sigOfs, sigLen)
		|       SigEcdsaP256Sha256 :
				IF (ecCurve = NIL) OR (digestLen # 32) THEN RETURN FALSE END;
				RETURN EcdsaSignDer(ecCurve, ecPriv, digest, digestLen, sig, sigOfs, sigLen)
		|       SigEcdsaP384Sha384 :
				IF (ecCurve = NIL) OR (digestLen # 48) THEN RETURN FALSE END;
				RETURN EcdsaSignDer(ecCurve, ecPriv, digest, digestLen, sig, sigOfs, sigLen)
		ELSE
			RETURN FALSE
		END;
		RETURN sigLen > 0
	END TLSSignDigest;

	PROCEDURE TLSVerifyDigest (scheme : AO.SIGNED32; cert : X509.Certificate;
			IN digest : ARRAY OF SHORTCHAR; digestLen : AO.SIZE;
			IN sig : ARRAY OF SHORTCHAR; sigOfs, sigLen : AO.SIZE): BOOLEAN;
	BEGIN
		IF (cert = NIL) OR (sigLen <= 0) THEN RETURN FALSE END;
		CASE scheme OF
			SigRsaPkcs1Sha256 :
				RETURN (cert.keyType = X509.KeyRsa) & (cert.publicKey # NIL)
					& PKCS1.VerifyHash("CryptoSHA256", digest, digestLen,
						cert.publicKey, sig, sigOfs, sigLen)
		|       SigRsaPkcs1Sha384 :
				RETURN (cert.keyType = X509.KeyRsa) & (cert.publicKey # NIL)
					& PKCS1.VerifyHash("CryptoSHA384", digest, digestLen,
						cert.publicKey, sig, sigOfs, sigLen)
		|       SigRsaPssRsaeSha256 :
				RETURN (cert.keyType = X509.KeyRsa) & (cert.publicKey # NIL)
					& (digestLen = 32)
					& PSS.VerifyHash("CryptoSHA256", digest, digestLen,
						cert.publicKey, sig, sigOfs, sigLen)
		|       SigRsaPssRsaeSha384 :
				RETURN (cert.keyType = X509.KeyRsa) & (cert.publicKey # NIL)
					& (digestLen = 48)
					& PSS.VerifyHash("CryptoSHA384", digest, digestLen,
						cert.publicKey, sig, sigOfs, sigLen)
		|       SigEcdsaP256Sha256 :
				RETURN (cert.keyType = X509.KeyEc) & (cert.ecCurve # NIL)
					& (digestLen = 32)
					& EcdsaVerifyDer(cert.ecCurve, cert.ecX, cert.ecY, digest, digestLen,
						sig, sigOfs, sigLen)
		|       SigEcdsaP384Sha384 :
				RETURN (cert.keyType = X509.KeyEc) & (cert.ecCurve # NIL)
					& (digestLen = 48)
					& EcdsaVerifyDer(cert.ecCurve, cert.ecX, cert.ecY, digest, digestLen,
						sig, sigOfs, sigLen)
		END;
		RETURN FALSE
	END TLSVerifyDigest;

	PROCEDURE TLSSign (scheme : AO.SIGNED32; rsaKey : CryptoRSA.Key;
			ecCurve : EC.Curve; IN ecPriv : EC.BigInt;
			IN data : ARRAY OF SHORTCHAR; dataLen : AO.SIZE;
			VAR sig : ARRAY OF SHORTCHAR; sigOfs : AO.SIZE; VAR sigLen : AO.SIZE): BOOLEAN;
		VAR h : CryptoHashes.Hash; digest : ARRAY 64 OF SHORTCHAR;
	BEGIN
		sigLen := 0;
		(* Ed25519 подписывать не умеем; проверка — в TLSVerify. *)
		h := TLSNewHash(scheme);
		IF h = NIL THEN
			RETURN FALSE
		END;
		h.Initialize;
		h.Update(data, 0, dataLen);
		h.GetHash(digest, 0);
		RETURN TLSSignDigest(scheme, rsaKey, ecCurve, ecPriv, digest, h.size, sig, sigOfs, sigLen)
	END TLSSign;

	PROCEDURE TLSVerify (scheme : AO.SIGNED32; cert : X509.Certificate;
			IN data : ARRAY OF SHORTCHAR; dataLen : AO.SIZE;
			IN sig : ARRAY OF SHORTCHAR; sigOfs, sigLen : AO.SIZE): BOOLEAN;
		VAR h : CryptoHashes.Hash; digest : ARRAY 64 OF SHORTCHAR;
	BEGIN
		IF (cert = NIL) OR (sigLen <= 0) THEN RETURN FALSE END;
		IF scheme = SigEd25519 THEN
			RETURN (cert.keyType = X509.KeyEd25519) & (sigLen = 64)
				& Ed25519.Verify(cert.edKey, data, dataLen, sig)
		END;
		h := TLSNewHash(scheme);
		IF h = NIL THEN
			RETURN FALSE
		END;
		h.Initialize;
		h.Update(data, 0, dataLen);
		h.GetHash(digest, 0);
		RETURN TLSVerifyDigest(scheme, cert, digest, h.size, sig, sigOfs, sigLen)
	END TLSVerify;


	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: DoubleLong) Init , NEW;
	BEGIN
		SELF.numH := (-1);
		SELF.numL := (-1)
	END Init;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: ConnectionState) Init* , NEW;

	BEGIN
		NEW(SELF.recordSeq);SELF.recordSeq.Init();
	END Init;

	PROCEDURE (SELF: DoubleLong) Inc , NEW;

	BEGIN
		IF SELF.numL # (-1) THEN
			INC(SELF.numL)
		ELSIF SELF.numH = (-1) THEN
			SELF.numH := 0;
			SELF.numL := 0
		ELSE
			INC(SELF.numH);
			SELF.numL := 0
		END
	END Inc;

	PROCEDURE (SELF: DoubleLong) GetBytes (VAR buf : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR
			p: AO.SIZE;
			val: AO.SIGNED32;


	BEGIN
		p := ofs + 7;
		val := SELF.numL;
		REPEAT
			buf[p] := (*WARN CONV CAST*)SHORT(CHR(val MOD 256));
			IF p = ofs + 4 THEN
				val := SELF.numH
			ELSE
				val := val DIV 256
			END;
			DEC(p)
		UNTIL p < ofs
	END GetBytes;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: PHash) Init* (hashname : ARRAY OF SHORTCHAR), NEW;

	BEGIN
		NEW(SELF.hMac); SELF.hMac.Init(hashname)
	END Init;

	PROCEDURE (SELF: PHash) Expand (IN secret, seed : ARRAY OF SHORTCHAR; VAR outbuf : ARRAY OF SHORTCHAR; secretLen, seedLen, outLen : AO.SIZE), NEW;
		VAR
			a: ARRAY 2 * MaxPHashSeed OF SHORTCHAR;
			i, iterations: AO.SIZE;


	BEGIN
		ASSERT(seedLen <= (MaxPHashSeed));
		ASSERT(outLen <= (MaxPHashKernelLogput));
		SELF.hMac.Initialize(secret, secretLen);
		SELF.hMac.Update(seed, 0, seedLen);
		SELF.hMac.GetMac(a, 0);
		FOR i := 0 TO seedLen - 1 DO
			a[SELF.hMac.size + i] := seed[i]
		END;
		iterations := (outLen DIV SELF.hMac.size) + 1;
		FOR i := 0 TO iterations - 1 DO
			SELF.hMac.Initialize(secret, secretLen);
			SELF.hMac.Update(a, 0, SELF.hMac.size + seedLen);
			SELF.hMac.GetMac(outbuf, i * SELF.hMac.size);
			SELF.hMac.Initialize(secret, secretLen);
			SELF.hMac.Update(a, 0, SELF.hMac.size);
			SELF.hMac.GetMac(a, 0)
		END
	END Expand;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: PRF) Init* (IN algorithm : ARRAY OF SHORTCHAR), NEW;

	BEGIN
		IF algorithm = "SHA1 XOR MD5" THEN
			NEW(SELF.pMD5);SELF.pMD5.Init("CryptoMD5");
			NEW(SELF.pSHA); SELF.pSHA.Init("CryptoSHA1")
		ELSE
			NEW(SELF.pHash); SELF.pHash.Init(algorithm)
		END
	END Init;

	PROCEDURE (SELF: PRF) GetBytes (IN secret, seed : ARRAY OF SHORTCHAR; VAR outbuf : ARRAY OF SHORTCHAR; IN label : ARRAY OF SHORTCHAR; secretLen, seedLen, outLen : AO.SIZE), NEW;
		VAR
			md5Result, shaResult: ARRAY MaxKeyBlock OF SHORTCHAR;
			seedBuf, s1, s2: ARRAY 140 OF SHORTCHAR;
			hashResult: ARRAY 256 OF SHORTCHAR;
			i, j, labelLen: AO.SIZE;


	BEGIN
		labelLen := Strings.Length(label);
		FOR i := 0 TO labelLen - 1 DO
			seedBuf[i] := label[i]
		END;
		FOR i := 0 TO seedLen - 1 DO
			seedBuf[labelLen + i] := seed[i]
		END;
		IF SELF.pHash = NIL THEN
			j := secretLen DIV 2 + secretLen MOD 2;
			FOR i := 0 TO j - 1 DO
				s1[i] := secret[i]
			END;
			FOR i := secretLen - j TO secretLen - 1 DO
				s2[i + j - secretLen] := secret[i]
			END;
			SELF.pMD5.Expand(s1, seedBuf, md5Result, j, seedLen + labelLen, outLen);
			SELF.pSHA.Expand(s2, seedBuf, shaResult, j, seedLen + labelLen, outLen);
			FOR i := 0 TO outLen - 1 DO
				outbuf[i] := BIT.CXOR(md5Result[i], shaResult[i])
			END
		ELSE
			SELF.pHash.Expand(secret, seedBuf, hashResult, secretLen, seedLen + labelLen, outLen);
			FOR i := 0 TO outLen - 1 DO
				outbuf[i] := hashResult[i]
			END
		END
	END GetBytes;

	(* Обёртка для эталонного вектора PRF (RFC 5246, P_SHA256). *)
	PROCEDURE PRFBytes* (IN secret, label, seed : ARRAY OF SHORTCHAR;
			secretLen, seedLen, outLen : AO.SIZE; VAR out : ARRAY OF SHORTCHAR);
		VAR prf : PRF;
	BEGIN
		NEW(prf);
		prf.Init("CryptoSHA256");
		prf.GetBytes(secret, seed, out, label, secretLen, seedLen, outLen)
	END PRFBytes;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: Policy) Init* , NEW;

	BEGIN
		SELF.sessionResumptionEnabled := TRUE;
		SELF.sessionLifetime := DefaultSessionLifetime
	END Init;

	PROCEDURE (SELF: Policy) SetCipherSuites* (VAR s : ARRAY OF AO.SIGNED32; n : AO.SIZE), NEW;
		VAR
			i: AO.SIZE;


	BEGIN
		ASSERT(n <= Suites);
		FOR i := 0 TO n - 1 DO
			SELF.cipherSuites[i] := s[i]
		END;
		SELF.nofCipherSuites := n
	END SetCipherSuites;

	PROCEDURE (SELF: Policy) IsSupported* (s : AO.SIGNED32): BOOLEAN, NEW;
		VAR
			i: AO.SIZE;


	BEGIN
		FOR i := 0 TO SELF.nofCipherSuites - 1 DO
			IF SELF.cipherSuites[i] = s THEN
				RETURN TRUE
			END
		END;
		RETURN FALSE
	END IsSupported;

	PROCEDURE (SELF: Policy) EnableSessionResumption* (enable : BOOLEAN), NEW;

	BEGIN
		SELF.sessionResumptionEnabled := enable
	END EnableSessionResumption;

	PROCEDURE (SELF: Policy) SetSessionLifetime* (t : AO.SIGNED32), NEW;

	BEGIN
		ASSERT(t >= 0);
		SELF.sessionLifetime := t
	END SetSessionLifetime;

	PROCEDURE (SELF: Policy) SetVersion* (major, minor : AO.SIGNED32), NEW;

	BEGIN
		ASSERT(major = 1);
		ASSERT(minor >= 0);
		ASSERT(minor <= 2);
		SELF.version[0] := (*WARN CONV CAST*)SHORT(CHR(3));
		SELF.version[1] := (*WARN CONV CAST*)SHORT(CHR(1 + minor))
	END SetVersion;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: Session) Init* (caching : BOOLEAN; VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE; lifetime : AO.SIGNED32), NEW;
		VAR
			i: AO.SIZE;


	BEGIN
		ASSERT(ofs > (-1));
		ASSERT(len > (-1));
		ASSERT(ofs + len <= LEN(data));
		ASSERT(lifetime > 0);
		SELF.resumable := caching;
		IF caching THEN
			IF len = 0 THEN
				SELF.idLength := SessionIdLength;
				Rng.Generate(SELF.id, 0, SessionIdLength)
			ELSE
				SELF.idLength := len;
				FOR i := 0 TO len - 1 DO
					SELF.id[i] := data[ofs + i]
				END
			END
		END;
		SELF.cipherSuite := 0;
		SELF.lifetime := lifetime;
				
				NEW(SELF.sessionInvalidator);SELF.sessionInvalidator.session := SELF;

				
				Services.DoLater(SELF.sessionInvalidator, Services.now + Services.resolution * lifetime);
	END Init;

	PROCEDURE (SELF: Session) Invalidate , NEW;

	BEGIN
		SELF.resumable := FALSE
	END Invalidate;

	PROCEDURE (SELF: Session) SetPreMasterSecret (data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE), NEW;
		VAR
			i: AO.SIZE;


	BEGIN
		IF (len <= 0) OR (len > LEN(SELF.preMasterSecret)) OR (ofs < 0) OR (LEN(data) < ofs + len) THEN
			RETURN
		END;
		FOR i := 0 TO len - 1 DO
			SELF.preMasterSecret[i] := data[ofs + i]
		END;
		SELF.preMasterSecretLen := len
	END SetPreMasterSecret;



	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: Context) Init* (p : Policy), NEW;

	BEGIN
		SELF.policy := p;
	     (*   NEW(SELF.timer); *)
		SELF.active := TRUE;
		SELF.trustedRoots := NIL;
		SELF.certChain := NIL;
		SELF.clientCertChain := NIL;
		SELF.clientPrivateKey := NIL;
		SELF.requireClientCert := FALSE;
		SELF.verifyFlags := {};
		SELF.owned := TRUE;
				NEW(SELF.sessionCacheCleaner);SELF.sessionCacheCleaner.ctx := SELF;
				Services.DoLater(SELF.sessionCacheCleaner,Services.now + Services.resolution * SessionCleanUpInterval)
	END Init;

	PROCEDURE (SELF: Context) Dispose* , NEW;

	BEGIN
		SELF.active := FALSE;
				Services.RemoveAction(SELF.sessionCacheCleaner);
	END Dispose;

	(* ---- Файлы сертификатов и ключей ---- *)

	PROCEDURE ReadFile* (IN path : ARRAY OF CHAR; VAR buf : ARRAY OF SHORTCHAR; VAR len : INTEGER): BOOLEAN;
		VAR
			dirName, name : SysFiles.Name;
			loc : SysFiles.Locator;
			f : SysFiles.File;
			r : SysFiles.Reader;
			i, last, n : INTEGER;
			b : BYTE;
	BEGIN
		len := 0;
		last := -1;
		i := 0;
		WHILE (i < LEN(path)) & (path[i] # 0X) DO
			IF path[i] = '/' THEN last := i END;
			INC(i)
		END;
		IF last < 0 THEN
			loc := SysFiles.dir.This("");
			i := 0;
			WHILE path[i] # 0X DO name[i] := path[i]; INC(i) END;
			name[i] := 0X
		ELSIF last = 0 THEN
			loc := SysFiles.dir.This("/");
			i := 1;
			n := 0;
			WHILE path[i] # 0X DO name[n] := path[i]; INC(i); INC(n) END;
			name[n] := 0X
		ELSE
			i := 0;
			WHILE i < last DO dirName[i] := path[i]; INC(i) END;
			dirName[i] := 0X;
			loc := SysFiles.dir.This(dirName);
			i := 0;
			WHILE path[last + 1 + i] # 0X DO name[i] := path[last + 1 + i]; INC(i) END;
			name[i] := 0X
		END;
		IF loc = NIL THEN RETURN FALSE END;
		f := SysFiles.dir.Old(loc, name, TRUE);
		IF f = NIL THEN RETURN FALSE END;
		n := f.Length();
		IF (n <= 0) OR (n >= LEN(buf)) THEN
			f.Close;
			RETURN FALSE
		END;
		r := f.NewReader(NIL);
		FOR i := 0 TO n - 1 DO
			r.ReadByte(b);
			buf[i] := SYSTEM.VAL(SHORTCHAR, b)
		END;
		buf[n] := 0X;
		len := n;
		f.Close;
		RETURN TRUE
	END ReadFile;

	PROCEDURE (SELF: Context) AddTrustedPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
		VAR c, tail : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		c := X509.ReadPemChain(pem, res);
		IF c = NIL THEN
			RETURN FALSE
		END;
		tail := c;
		WHILE tail.next # NIL DO tail := tail.next END;
		tail.next := SELF.trustedRoots;
		SELF.trustedRoots := c;
		RETURN TRUE
	END AddTrustedPem;

	PROCEDURE (SELF: Context) AddTrustedFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : POINTER TO ARRAY OF SHORTCHAR; len : INTEGER; ok : BOOLEAN;
	BEGIN
		(* Системный набор CA бывает больше 16 КиБ; читаем динамически. *)
		NEW(pem, 1024 * 1024);
		IF ~ReadFile(path, pem^, len) THEN
			RETURN FALSE
		END;
		ok := SELF.AddTrustedPem(pem^);
		RETURN ok
	END AddTrustedFile;

	PROCEDURE (SELF: Context) LoadCertificatePem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
		VAR c : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		c := X509.ReadPemChain(pem, res);
		IF c = NIL THEN
			RETURN FALSE
		END;
		SELF.certChain := c;
		SELF.rsaPublicKey := c.publicKey;
		RETURN TRUE
	END LoadCertificatePem;

	PROCEDURE (SELF: Context) LoadCertificateFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadCertificatePem(pem)
	END LoadCertificateFile;

	PROCEDURE (SELF: Context) SetPrivateKey (IN pem : ARRAY OF SHORTCHAR; VAR key : CryptoRSA.Key): BOOLEAN, NEW;
		VAR p, q, e : AosBigNumbers.BigNumber; dummy : CryptoRSA.Key; res : AO.SIGNED32;
	BEGIN
		IF ~X509.ReadPemPrivateKey(pem, p, q, e, res) THEN
			RETURN FALSE
		END;
		NEW(key);
		NEW(dummy);
		CryptoRSA.MakeKeys(p, q, e, "TLS", dummy, key);
		RETURN TRUE
	END SetPrivateKey;

	PROCEDURE (SELF: Context) LoadPrivateKeyPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
	BEGIN
		RETURN SELF.SetPrivateKey(pem, SELF.rsaPrivateKey)
	END LoadPrivateKeyPem;

	PROCEDURE (SELF: Context) LoadPrivateKeyFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadPrivateKeyPem(pem)
	END LoadPrivateKeyFile;

	(* Ключ ECDSA (SEC1 или PKCS#8) для наборов ECDHE-ECDSA. *)
	PROCEDURE (SELF: Context) LoadEcPrivateKeyPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
		VAR res : AO.SIGNED32;
	BEGIN
		SELF.ecCurve := NIL;
		RETURN X509.ReadPemEcPrivateKey(pem, SELF.ecCurve, SELF.ecPrivate, res)
	END LoadEcPrivateKeyPem;

	PROCEDURE (SELF: Context) LoadEcPrivateKeyFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadEcPrivateKeyPem(pem)
	END LoadEcPrivateKeyFile;

	PROCEDURE (SELF: Context) LoadClientCertificatePem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
		VAR c : X509.Certificate; res : AO.SIGNED32;
	BEGIN
		c := X509.ReadPemChain(pem, res);
		IF c = NIL THEN
			RETURN FALSE
		END;
		SELF.clientCertChain := c;
		RETURN TRUE
	END LoadClientCertificatePem;

	PROCEDURE (SELF: Context) LoadClientCertificateFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadClientCertificatePem(pem)
	END LoadClientCertificateFile;

	PROCEDURE (SELF: Context) LoadClientPrivateKeyPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
	BEGIN
		RETURN SELF.SetPrivateKey(pem, SELF.clientPrivateKey)
	END LoadClientPrivateKeyPem;

	PROCEDURE (SELF: Context) LoadClientPrivateKeyFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadClientPrivateKeyPem(pem)
	END LoadClientPrivateKeyFile;

	PROCEDURE (SELF: Context) LoadClientEcPrivateKeyPem* (IN pem : ARRAY OF SHORTCHAR): BOOLEAN, NEW;
		VAR res : AO.SIGNED32;
	BEGIN
		SELF.clientEcCurve := NIL;
		RETURN X509.ReadPemEcPrivateKey(pem, SELF.clientEcCurve, SELF.clientEcPrivate, res)
	END LoadClientEcPrivateKeyPem;

	PROCEDURE (SELF: Context) LoadClientEcPrivateKeyFile* (IN path : ARRAY OF CHAR): BOOLEAN, NEW;
		VAR pem : ARRAY 16384 OF SHORTCHAR; len : INTEGER;
	BEGIN
		IF ~ReadFile(path, pem, len) THEN
			RETURN FALSE
		END;
		RETURN SELF.LoadClientEcPrivateKeyPem(pem)
	END LoadClientEcPrivateKeyFile;

	PROCEDURE (SELF: Context) RequireClientCertificate* (require : BOOLEAN), NEW;
	BEGIN
		SELF.requireClientCert := require
	END RequireClientCertificate;

	PROCEDURE (SELF: Context) VerifyPeer* (leaf, chain : X509.Certificate; IN host : ARRAY OF SHORTCHAR; VAR err : AO.SIGNED32): BOOLEAN, NEW;
	BEGIN
		RETURN X509.VerifyChain(leaf, chain, SELF.trustedRoots, host, SELF.verifyFlags, err)
	END VerifyPeer;


	PROCEDURE (SELF: Context) PrintRsaCertificate* , NEW;

	BEGIN
		StdLog.String("Certificate [");
		StdLog.Int(SELF.lengthOfRsaCertificate);
		StdLog.String("]");
		StdLog.Ln;
		AO.LogBuffer(SELF.rsaCertificate, 6, SELF.lengthOfRsaCertificate)
	END PrintRsaCertificate;

	PROCEDURE (SELF: Context) LoadRsaCertificate* (filename : ARRAY OF SHORTCHAR): AO.SIGNED32, NEW;
		VAR
			f: Files.File;
			r: Files.Reader;
			i: AO.SIZE;


	BEGIN
		f := Files.Old(filename);
		IF f # NIL THEN
			Files.OpenReader(r, f, 0);
			i := 0;
			WHILE r.res # Streams.EOF DO
				SELF.rsaCertificate[i + 6] := r.Get();
				INC(i)
			END;
			SELF.lengthOfRsaCertificate := i - 1;
			IF Trace THEN
				SELF.PrintRsaCertificate()
			END;
			SELF.rsaCertificate[0] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate + 3) DIV (256 * 256)));
			SELF.rsaCertificate[1] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate + 3) DIV 256 MOD 256));
			SELF.rsaCertificate[2] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate + 3) MOD 256));
			SELF.rsaCertificate[3] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate) DIV (256 * 256)));
			SELF.rsaCertificate[4] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate) DIV 256 MOD 256));
			SELF.rsaCertificate[5] := (*WARN CONV CAST*)SHORT(CHR((SELF.lengthOfRsaCertificate) MOD 256));
			INC(SELF.lengthOfRsaCertificate, 6);
			RETURN Ok
		ELSE
			StdLog.String("RSA Certificate file not found");
			StdLog.Ln;
			RETURN (-1)
		END
	END LoadRsaCertificate;

	PROCEDURE (SELF: Context) LoadRsaPrivateKey* (pHex, qHex, eHex : ARRAY OF SHORTCHAR; pLen, qLen, eLen : AO.SIGNED16), NEW;
		VAR
			p, q, e: AosBigNumbers.BigNumber;
			dummy: CryptoRSA.Key;


	BEGIN
		AosBigNumbers.AssignHex(p, pHex, pLen);
		AosBigNumbers.AssignHex(q, qHex, qLen);
		AosBigNumbers.AssignHex(e, eHex, eLen);
		NEW(SELF.rsaPrivateKey);
		NEW(dummy);
		CryptoRSA.MakeKeys(p, q, e, "AosTLS", dummy, SELF.rsaPrivateKey);
		IF Trace THEN
			StdLog.String("RSA-key loaded");
			StdLog.Ln;
			StdLog.String("   prime p:");
			StdLog.Ln;
			AosBigNumbers.Print(p);
			StdLog.String("   prime q:");
			StdLog.Ln;
			AosBigNumbers.Print(q);
			StdLog.String("   public exponent:");
			StdLog.Ln;
			AosBigNumbers.Print(e);
			StdLog.String("   modulus:");
			StdLog.Ln;
			AosBigNumbers.Print(SELF.rsaPrivateKey.modulus);
			StdLog.String("   private exponent:");
			StdLog.Ln;
			AosBigNumbers.Print(SELF.rsaPrivateKey.exponent)
		END
	END LoadRsaPrivateKey;

	PROCEDURE (SELF: Context) GetRsaPublicKey* , NEW;
		VAR
			certificate: X509.Certificate;
			reader: Streams.StringReader;


	BEGIN
		IF SELF.rsaPublicKey = NIL THEN
			NEW(certificate);
			NEW(reader); reader.InitStringReader(SELF.lengthOfRsaCertificate);
			reader.SetRaw(SELF.rsaCertificate, 6, SELF.lengthOfRsaCertificate);
			certificate := X509.Read(reader);
			IF certificate # NIL THEN
				SELF.rsaPublicKey := certificate.publicKey
			END
		END
	END GetRsaPublicKey;



	PROCEDURE (SELF: Context) GetNewServerSession* (): Session, NEW;
		VAR
			dummy: ARRAY 1 OF SHORTCHAR;
			s: Session;


	BEGIN
		NEW(s); s.Init( SELF.policy.sessionResumptionEnabled, dummy, 0, 0, SELF.policy.sessionLifetime);
		RETURN s
	END GetNewServerSession;

	PROCEDURE (SELF: Context) EqualSessionID (VAR data1, data2 : ARRAY OF SHORTCHAR; ofs1, ofs2, len : AO.SIZE): BOOLEAN, NEW;
		VAR
			i: AO.SIZE;


	BEGIN
		FOR i := 0 TO len - 1 DO
			IF data1[ofs1 + i] # data2[ofs2 + i] THEN
				RETURN FALSE
			END
		END;
		RETURN TRUE
	END EqualSessionID;

	PROCEDURE (SELF: Context) FindSessionByID* (VAR data : ARRAY OF SHORTCHAR; ofs, idLen : AO.SIZE): Session, NEW;
		VAR
			i: AO.SIZE;
			current, previous: Session;


	BEGIN
		IF (idLen < 2) OR (idLen > 32) OR (ofs < 0) OR (LEN(data) < ofs + idLen) THEN
			RETURN NIL
		END;
		IF ~SELF.policy.sessionResumptionEnabled THEN
			RETURN NIL
		END;
		i := (ORD(data[ofs]) + 256 * ORD(data[ofs + 1])) MOD SessionHashtableSize;
		current := SELF.sessionCache[i];
		previous := SELF.sessionCache[i];
		WHILE current # NIL DO
			IF SELF.EqualSessionID(current.id, data, 0, ofs, idLen) THEN
				IF current.resumable THEN
					IF current # previous THEN
						previous.next := current.next;
						current.next := SELF.sessionCache[i];
						SELF.sessionCache[i] := current
					END;
					RETURN current
				ELSE
					RETURN NIL
				END
			ELSE
				previous := current;
				current := current.next
			END
		END;
		RETURN NIL
	END FindSessionByID;

	PROCEDURE (SELF: Context) StoreSession (s : Session), NEW;
		VAR
			i: AO.SIGNED32;


	BEGIN
		ASSERT(s.resumable);
		i := ORD(s.id[0]) + 256 * ORD(s.id[1]);
		i := i MOD SessionHashtableSize;
		s.next := SELF.sessionCache[i];
		SELF.sessionCache[i] := s
	END StoreSession;



	PROCEDURE (SELF: Context) DeleteUnresumableSessions (s : Session): Session, NEW;

	BEGIN
		IF s = NIL THEN
			RETURN NIL
		END;
		IF s.resumable THEN
			s.next := SELF.DeleteUnresumableSessions(s.next);
			RETURN s
		ELSE
			RETURN SELF.DeleteUnresumableSessions(s.next)
		END
	END DeleteUnresumableSessions;

	PROCEDURE (SELF: Context) CleanUpSessionCache , NEW;
		VAR
			i: AO.SIGNED32;


	BEGIN
		FOR i := 0 TO SessionHashtableSize - 1 DO
			SELF.sessionCache[i] := SELF.DeleteUnresumableSessions(SELF.sessionCache[i])
		END
	END CleanUpSessionCache;

		PROCEDURE (SELF:SessionCacheCleaner) Do;
		BEGIN
			IF SELF.ctx.active THEN
				IF Trace THEN
					StdLog.String("TLS CleanUpSessionCache"); StdLog.Ln;
				END;
				SELF.ctx.CleanUpSessionCache;
				Services.DoLater(SELF,Services.now + Services.resolution * SessionCleanUpInterval)
			END
		END Do;

	PROCEDURE (*WARN CONV NOT SUPPORTED: &*)
(SELF: Connection) Init* ;

	BEGIN
		SELF.state := TCP.Unused;
		SELF.tlsErrorCause := NoError;

				NEW(SELF.asyncWriter); SELF.asyncWriter.conn := SELF;
		NEW(SELF.applicationDataPipe); SELF.applicationDataPipe.Init(1024 * 1024,SELF.asyncWriter);
		NEW(SELF.appDataWriter);
				SELF.appDataWriter.InitWriter(SELF.applicationDataPipe.sender, 1024 * 1024);
		Streams.OpenReader(SELF.appDataReader, SELF.applicationDataPipe.receiver);
		NEW(SELF.pendingWriteState);SELF.pendingWriteState.Init();
		NEW(SELF.currentWriteState);SELF.currentWriteState.Init();
		NEW(SELF.pendingReadState);SELF.pendingReadState.Init();
		NEW(SELF.currentReadState);SELF.currentReadState.Init();
		NEW(SELF.pendingSecurityParameters);
		SELF.group := NewGroup(GroupX25519);
		IF SELF.group = NIL THEN
			ASSERT(FALSE)
		END;
		SELF.group.Generate;
		SELF.sharedLen := 0;
		SELF.ownsContext := FALSE;
		SELF.transcript := NIL;
		SELF.transcriptLen := 0;
		SELF.transcriptCap := 0
	END Init;

	PROCEDURE (SELF: Connection) AppendTranscript (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE), NEW;
		VAR buf : POINTER TO ARRAY OF SHORTCHAR; cap, i : AO.SIZE;
	BEGIN
		IF len <= 0 THEN RETURN END;
		IF SELF.transcript = NIL THEN
			cap := 2048;
			NEW(SELF.transcript, cap);
			SELF.transcriptCap := cap;
			SELF.transcriptLen := 0
		END;
		IF SELF.transcriptLen + len > SELF.transcriptCap THEN
			cap := SELF.transcriptCap;
			WHILE cap < SELF.transcriptLen + len DO
				cap := cap * 2
			END;
			NEW(buf, cap);
			FOR i := 0 TO SELF.transcriptLen - 1 DO
				buf^[i] := SELF.transcript^[i]
			END;
			SELF.transcript := buf;
			SELF.transcriptCap := cap
		END;
		FOR i := 0 TO len - 1 DO
			SELF.transcript^[SELF.transcriptLen + i] := data[ofs + i]
		END;
		INC(SELF.transcriptLen, len)
	END AppendTranscript;

	PROCEDURE (SELF: Connection) TranscriptDigestAt (len : AO.SIZE; VAR digest : ARRAY OF SHORTCHAR), NEW;
		VAR h : CryptoHashes.Hash;
	BEGIN
		(* TLS 1.2: хеш транскрипта — хеш PRF (SHA-256 или SHA-384). *)
		IF SELF.version[1] >= 3X THEN
			h := CryptoHashes.NewHash(SELF.pendingSecurityParameters.PRFAlgorithm)
		ELSE
			h := CryptoHashes.NewHash("CryptoSHA256")
		END;
		IF h = NIL THEN
			h := CryptoHashes.NewHash("CryptoSHA256")
		END;
		h.Initialize;
		IF (SELF.transcript # NIL) & (len > 0) THEN
			IF len > SELF.transcriptLen THEN len := SELF.transcriptLen END;
			h.Update(SELF.transcript^, 0, len)
		END;
		h.GetHash(digest, 0)
	END TranscriptDigestAt;

	PROCEDURE (SELF: Connection) PrfDigestLen (): AO.SIZE, NEW;
	BEGIN
		IF (SELF.version[1] >= 3X)
			& (SELF.pendingSecurityParameters.PRFAlgorithm = "CryptoSHA384") THEN
			RETURN 48
		END;
		RETURN 32
	END PrfDigestLen;

	PROCEDURE (SELF: Connection) TranscriptDigest (VAR digest : ARRAY OF SHORTCHAR), NEW;
	BEGIN
		SELF.TranscriptDigestAt(SELF.transcriptLen, digest)
	END TranscriptDigest;

	(* Хеш транскрипта хешем схемы подписи (CertificateVerify TLS 1.2). *)
	PROCEDURE (SELF: Connection) TranscriptHash (scheme : AO.SIGNED32;
			VAR digest : ARRAY OF SHORTCHAR; VAR digestLen : AO.SIZE): BOOLEAN, NEW;
		VAR h : CryptoHashes.Hash;
	BEGIN
		digestLen := 0;
		h := TLSNewHash(scheme);
		IF h = NIL THEN RETURN FALSE END;
		h.Initialize;
		IF (SELF.transcript # NIL) & (SELF.transcriptLen > 0) THEN
			h.Update(SELF.transcript^, 0, SELF.transcriptLen)
		END;
		h.GetHash(digest, 0);
		digestLen := h.size;
		RETURN TRUE
	END TranscriptHash;

	PROCEDURE (SELF: Connection) ClientSignScheme (): AO.SIGNED32, NEW;
	BEGIN
		IF (SELF.context # NIL) & (SELF.context.clientEcCurve # NIL) THEN
			IF SELF.context.clientEcCurve.n = 8 THEN RETURN SigEcdsaP256Sha256 END;
			IF SELF.context.clientEcCurve.n = 12 THEN RETURN SigEcdsaP384Sha384 END;
			RETURN 0
		END;
		IF (SELF.context # NIL) & (SELF.context.clientPrivateKey # NIL) THEN
			RETURN SigRsaPkcs1Sha256
		END;
		RETURN 0
	END ClientSignScheme;

	PROCEDURE (SELF: Connection) AwaitStateNotEqual* (s : AO.SIGNED32) : BOOLEAN, NEW;

	BEGIN

		RETURN ( SELF.state #  s);

	END AwaitStateNotEqual;

	PROCEDURE (SELF: Connection) AwaitStateEqual* (s : AO.SIGNED32) : BOOLEAN, NEW;

	BEGIN

		RETURN ( SELF.state =  s)

	END AwaitStateEqual; 

	PROCEDURE (SELF: Connection) SetState* (s : AO.SIGNED8), NEW;

	BEGIN

		SELF.state := s
	END SetState;

	PROCEDURE (SELF: Connection) ChangeWriteState , NEW;

	BEGIN
		SELF.currentWriteState := SELF.pendingWriteState;
		NEW(SELF.pendingWriteState); SELF.pendingWriteState.Init();
	END ChangeWriteState;

	PROCEDURE (SELF: Connection) ChangeReadState , NEW;

	BEGIN
		SELF.currentReadState := SELF.pendingReadState;
		NEW(SELF.pendingReadState);SELF.pendingReadState.Init();
	END ChangeReadState;

	PROCEDURE (SELF: Connection) SetContext* (ctx : Context), NEW;

	BEGIN
		ASSERT(SELF.conn = NIL);
		SELF.context := ctx;
		SELF.version[0] := ctx.policy.version[0];
		SELF.version[1] := ctx.policy.version[1];
		IF SELF.version[1] < 3X THEN
			NEW(SELF.hsMD5send);
			NEW(SELF.hsSHAsend);
			NEW(SELF.hsMD5verify);
			NEW(SELF.hsSHAverify);
			SELF.hsMD5send.Initialize;
			SELF.hsSHAsend.Initialize;
			SELF.hsMD5verify.Initialize;
			SELF.hsSHAverify.Initialize
		ELSE
			SELF.hsHashSend := CryptoHashes.NewHash("CryptoSHA256");
			SELF.hsHashVerify := CryptoHashes.NewHash("CryptoSHA256");
			IF SELF.hsHashSend = SELF.hsHashVerify THEN HALT(20) END;
			SELF.hsHashSend.Initialize;
			SELF.hsHashVerify.Initialize
		END
	END SetContext;

	PROCEDURE (SELF: Connection) SetDefaultPolicy* , NEW;
		VAR
			ctx: Context;
	BEGIN
		NEW(ctx); ctx.Init(defaultCipherPolicy);
		SELF.SetContext(ctx);
		SELF.ownsContext := TRUE
	END SetDefaultPolicy;


	PROCEDURE (SELF: Connection) Connect* , NEW;

	BEGIN
		Streams.OpenReader(SELF.in, SELF.conn. receiver);
		Streams.OpenWriter(SELF.out, SELF.conn.sender);
		SELF.SetState((*WARN CONV CAST*)SHORT(ClientHandshake));
		SELF.handshakeState := GenerateClientHello
	END Connect;


		PROCEDURE (s: Connection) IsConnected* (): BOOLEAN;
		BEGIN
			RETURN s.conn.IsConnected();
		END IsConnected;

	PROCEDURE (SELF: Connection) NegotiatedVersion* (): AO.SIGNED32, NEW;
	BEGIN
		RETURN 256 * ORD(SELF.version[0]) + ORD(SELF.version[1])
	END NegotiatedVersion;

	PROCEDURE (SELF: Connection) NegotiatedSuite* (): AO.SIGNED32, NEW;
	BEGIN
		IF SELF.pendingSecurityParameters # NIL THEN
			RETURN SELF.pendingSecurityParameters.cipherSuite
		END;
		RETURN 0
	END NegotiatedSuite;

	PROCEDURE (SELF: Connection) NegotiatedGroup* (): AO.SIGNED32, NEW;
	BEGIN
		IF SELF.group # NIL THEN
			RETURN SELF.group.id
		END;
		RETURN 0
	END NegotiatedGroup;

	PROCEDURE (SELF: Connection) NegotiatedScheme* (): AO.SIGNED32, NEW;
	BEGIN
		RETURN SELF.negotiatedSigScheme
	END NegotiatedScheme;


	PROCEDURE (SELF: Connection) SetConnection* (c : TCP.Connection), NEW;

	BEGIN
		ASSERT(SELF.conn = NIL);
		ASSERT(SELF.State() = TCP.Unused);
		SELF.conn := c;
		Streams.OpenReader(SELF.in, SELF.conn.receiver);
		Streams.OpenWriter(SELF.out, SELF.conn.sender);
		SELF.SetState((*WARN CONV CAST*)SHORT(ServerHandshake));
		SELF.handshakeState := AwaitClientHello
	END SetConnection;



	PROCEDURE (SELF: Connection) PrintDirection (send : BOOLEAN), NEW;

	BEGIN
		IF SELF.client & send THEN
			StdLog.String("CLIENT: C > S ")
		ELSIF SELF.client & ~send THEN
			StdLog.String("CLIENT: S > C ")
		ELSIF ~SELF.client & send THEN
			StdLog.String("SERVER: S > C ")
		ELSE
			StdLog.String("SERVER: C > S ")
		END
	END PrintDirection;

	PROCEDURE (SELF: Connection) SendRecord (VAR data : ARRAY OF SHORTCHAR; contentType, ofs, len : AO.SIZE), NEW;
		VAR
			macInput: ARRAY 13 OF SHORTCHAR;
						additionalData: ARRAY 13 OF SHORTCHAR;
						tag: ARRAY 16 OF SHORTCHAR;
			iv: ARRAY 16 OF SHORTCHAR;
			seq, explicit: ARRAY 8 OF SHORTCHAR;
			i, length, padLen, blocksize: AO.SIZE;
			sendIV: BOOLEAN;


	BEGIN
				blocksize := 0;
		ASSERT(len <= MaxPlaintextLength);
		SELF.currentWriteState.recordSeq.Inc();
		length := len;

				(*IF Trace & (SELF.currentWriteState.aeadCipher# NIL) THEN
					StdLog.String("AEAD MODE "); StdLog.Ln;
					StdLog.String("Payload len:");StdLog.Int(len);StdLog.Ln;
					StdLog.String("Payload:");AO.LogBuffer(data, ofs, len);StdLog.Ln;U.PrintHex(data, ofs, len); StdLog.Ln;
				END;*)
		IF (SELF.currentWriteState.mac # NIL) & (SELF.currentWriteState.aead = NIL) THEN
			SELF.currentWriteState.recordSeq.GetBytes(macInput, 0);
			macInput[8] := (*WARN CONV CAST*)SHORT(CHR(contentType));
			macInput[9] := SELF.version[0];
			macInput[10] := SELF.version[1];
			macInput[11] := (*WARN CONV CAST*)SHORT(CHR(len DIV 256));
			macInput[12] := (*WARN CONV CAST*)SHORT(CHR(len));
			SELF.currentWriteState.mac.Initialize(SELF.currentWriteState.macSecret, SELF.currentWriteState.mac.size);
			SELF.currentWriteState.mac.Update(macInput, 0, 13);
			SELF.currentWriteState.mac.Update(data, ofs, len);
			SELF.currentWriteState.mac.GetMac(data, ofs + len);
			INC(length, SELF.currentWriteState.mac.size)
		END;
		IF (SELF.currentWriteState.cipher # NIL) & (SELF.currentWriteState.aead = NIL) THEN
			blocksize := SELF.currentWriteState.cipher.blockSize;
			IF blocksize > 1 THEN
				sendIV := ORD(SELF.version[1]) >= 2;
				IF sendIV THEN
					Rng.Generate(iv, 0, blocksize);
					SELF.currentWriteState.cipher.Encrypt(iv, 0, blocksize)
				END;
				padLen := (blocksize - ((length + 1) MOD blocksize)) MOD blocksize;
				IF padLen > 0 THEN
					ASSERT(padLen < 255);
					FOR i := 0 TO padLen - 1 DO
						data[ofs + length + i] := SHORT(CHR(padLen MOD 100H))
					END
				END;
				data[ofs + length + padLen] := SHORT(CHR(padLen MOD 100H));
				length := length + padLen + 1
			END;
			ASSERT(length MOD blocksize = 0);
			SELF.currentWriteState.cipher.Encrypt(data, ofs, length)
		END;
				IF SELF.currentWriteState.aead # NIL THEN
					(*additional_data = seq_num + TLSCompressed.type +
			TLSCompressed.version + TLSCompressed.length;*)
					SELF.currentWriteState.recordSeq.GetBytes(additionalData, 0);
					additionalData[8] := SHORT(CHR(contentType));
		    additionalData[9] := SELF.version[0];
		    additionalData[10] := SELF.version[1];
		    additionalData[11] := SHORT(CHR(len DIV 256));
		    additionalData[12] := SHORT(CHR(len));
					SELF.currentWriteState.recordSeq.GetBytes(seq, 0);
					(* nonce строит AEAD: GCM — salt(4) || seq(8) с явной
					   частью seq в записи; ChaCha — соль(12), номер
					   записи в nonce, явной части нет. *)
					SELF.currentWriteState.aead.Seal(data, ofs, length,
						additionalData, LEN(additionalData), seq,
						data, ofs, explicit, tag);
					sendIV := TRUE
				END;

		IF sendIV THEN
						IF SELF.currentWriteState.aead # NIL THEN
							INC(length, SELF.currentWriteState.aead.explicitLen);
							INC(length, SELF.currentWriteState.aead.tagLen)
						ELSE
				INC(length, blocksize)
						END;
		END;
		SELF.out.Char(SHORT(CHR(contentType)));
		SELF.out.Char(SELF.version[0]);
		SELF.out.Char(SELF.version[1]);
		SELF.out.Char(SHORT(CHR(length DIV 256)));
		SELF.out.Char(SHORT(CHR(length)));
		IF sendIV THEN

						IF SELF.currentWriteState.aead # NIL THEN
							SELF.out.Bytes(explicit, 0, SELF.currentWriteState.aead.explicitLen);
		       	 DEC(length, SELF.currentWriteState.aead.explicitLen);
							DEC(length, SELF.currentWriteState.aead.tagLen)
						ELSE
				SELF.out.Bytes(iv, 0, blocksize);
				DEC(length, blocksize)
						END;
		END;
		SELF.out.Bytes(data, ofs, length);
				IF SELF.currentWriteState.aead # NIL THEN
					SELF.out.Bytes(tag, 0, SELF.currentWriteState.aead.tagLen);
				END;
		SELF.out.Update;
	END SendRecord;

	PROCEDURE (SELF: Connection) SendWarning (alertType : AO.SIGNED32), NEW;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("Warning");
			StdLog.Ln;
			StdLog.String("  alertType: ");
			StdLog.Int(alertType);
			StdLog.Ln
		END;
		SELF.outbuf[0] := (*WARN CONV CAST*)SHORT(CHR(1));
		SELF.outbuf[1] := (*WARN CONV CAST*)SHORT(CHR(alertType));
		SELF.SendRecord(SELF.outbuf, Alert, 0, 2)
	END SendWarning;

	PROCEDURE (SELF: Connection) Close* ;

	BEGIN
		IF Trace THEN
			StdLog.String("Close");
			StdLog.Ln
		END;
		ASSERT(SELF.conn # NIL);
				IF (SELF.conn.State() = TCP.Established) THEN
			IF SELF.State() = TCP.Established THEN
		 	       SELF.SendWarning(CloseNotify)
			END;
				END;
		SELF.conn.Close();
				
		SELF.applicationDataPipe.Close();
		SELF.handshakeState := NotAValidHandshakeState;
				IF SELF.ownsContext & (SELF.context # NIL) THEN
					SELF.context.Dispose()
				END;
				IF SELF.session # NIL THEN
					Services.RemoveAction(SELF.session.sessionInvalidator);
				END;
		SELF.SetState(TCP.Closed)
	END Close;

	PROCEDURE (SELF: Connection) Send* (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE; propagate : BOOLEAN; VAR res : INTEGER), NEW;
		VAR
			i, size: AO.SIZE;
	BEGIN
				(*StdLog.String("TLS Client Send App Data: ");AO.LogBuffer(data, ofs, len);StdLog.Ln;*)
				ASSERT (  (SELF.state =  TCP.Established) OR   (SELF.state =  TCP.Closed ));
		res := Streams.Ok;
		IF SELF.state = TCP.Established THEN
			WHILE len > 0 DO
				IF Trace THEN
					SELF.PrintDirection(TRUE);
					StdLog.String("Application Data");
					StdLog.Ln
				END;
				size := len;
				IF size > MaxPlaintextLength THEN
					size := MaxPlaintextLength
				END;
				FOR i := 0 TO size - 1 DO
					SELF.outbuf[i] := data[ofs + i]
				END;
								
				SELF.SendRecord(SELF.outbuf, ApplicationData, 0, size);
				INC(ofs, size);
				DEC(len, size)
			END;
		ELSE
			res := Streams.EOF
		END
	END Send;

	PROCEDURE (SELF: ConnectionSender) Send* (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE; propagate : BOOLEAN; VAR res : INTEGER);

	BEGIN
				SELF.conn.Send(data, ofs, len,propagate, res);;
	END Send;

	PROCEDURE (SELF: Connection) Receive* (VAR data : ARRAY OF SHORTCHAR; ofs, size, min : AO.SIZE; VAR len : AO.SIZE; VAR res : INTEGER), NEW;

	BEGIN
		SELF.applicationDataPipe.Receive(data, ofs, size, min, len, res)
	END Receive;


	PROCEDURE (SELF: ConnectionReceiver) Receive* (VAR data : ARRAY OF SHORTCHAR; ofs, size, min : AO.SIZE; VAR len : AO.SIZE; VAR res : INTEGER);

	BEGIN
		SELF.conn.Receive(data, ofs, size, min, len, res)
	END Receive;

	PROCEDURE (SELF: Connection) GenerateMasterSecret , NEW;
		VAR
			prf: PRF;
			seed: ARRAY 64 OF SHORTCHAR;
			i: AO.SIZE;


	BEGIN
		FOR i := 0 TO 31 DO
			seed[i] := SELF.pendingSecurityParameters.clientRandom[i];
			seed[i + 32] := SELF.pendingSecurityParameters.serverRandom[i]
		END;
				ASSERT(SELF.session.preMasterSecretLen # 0);
		NEW(prf); prf.Init(SELF.pendingSecurityParameters.PRFAlgorithm);
		prf.GetBytes(SELF.session.preMasterSecret, seed, SELF.session.masterSecret, "master secret", SELF.session.preMasterSecretLen, 64, 48);
		FOR i := 0 TO 47 DO
			SELF.session.preMasterSecret[i] := SHORT(CHR(0))
		END
	END GenerateMasterSecret;

	PROCEDURE (SELF: Connection) GenerateKeys , NEW;
		VAR
			prf: PRF;
			keyBlock: ARRAY 136 OF SHORTCHAR;
			seed: ARRAY 64 OF SHORTCHAR;
			i, keyBlockLen, ofs, macSecretSize, cipherKeySize, cipherBlockSize: AO.SIZE;


	BEGIN
				
		macSecretSize := 0;
		IF SELF.pendingWriteState.mac # NIL THEN
			macSecretSize := SELF.pendingWriteState.mac.size
		END;
				IF SELF.pendingWriteState.aead # NIL THEN
					macSecretSize := 0;
				END;
		cipherKeySize := SELF.pendingSecurityParameters.cipherKeySize;
		IF SELF.pendingWriteState.aead # NIL THEN
			cipherBlockSize := SELF.pendingWriteState.aead.saltLen
		ELSIF SELF.pendingWriteState.cipher # NIL THEN
			cipherBlockSize := SELF.pendingWriteState.cipher.blockSize
		ELSE
			cipherBlockSize := 0
		END;
		keyBlockLen := 2 * macSecretSize + 2 * cipherKeySize;
		IF cipherBlockSize > 1 THEN
			INC(keyBlockLen, 2 * cipherBlockSize)
		END;
				(*StdLog.String("keyBlockLen=");StdLog.Int(keyBlockLen);StdLog.Ln;*)
		ASSERT(keyBlockLen <= 136);
		FOR i := 0 TO 31 DO
			seed[i] := SELF.pendingSecurityParameters.serverRandom[i];
			seed[i + 32] := SELF.pendingSecurityParameters.clientRandom[i]
		END;
		NEW(prf); prf.Init(SELF.pendingSecurityParameters.PRFAlgorithm);
		prf.GetBytes(SELF.session.masterSecret, seed, keyBlock, "key expansion", 48, 64, keyBlockLen);
				(*StdLog.String("Key block key expansion:");StdLog.Ln;U.PrintHex(keyBlock, 0, keyBlockLen); StdLog.Ln;*)
		FOR i := 0 TO macSecretSize - 1 DO
			IF SELF.client THEN
				SELF.pendingWriteState.macSecret[i] := keyBlock[i];
				SELF.pendingReadState.macSecret[i] := keyBlock[macSecretSize + i]
			ELSE
				SELF.pendingReadState.macSecret[i] := keyBlock[i];
				SELF.pendingWriteState.macSecret[i] := keyBlock[macSecretSize + i]
			END
		END;
		ofs := 2 * macSecretSize;
		FOR i := 0 TO cipherKeySize - 1 DO
			IF SELF.client THEN
				SELF.pendingWriteState.cipherKey[i] := keyBlock[ofs + i];
				SELF.pendingReadState.cipherKey[i] := keyBlock[ofs + cipherKeySize + i]
			ELSE
				SELF.pendingReadState.cipherKey[i] := keyBlock[ofs + i];
				SELF.pendingWriteState.cipherKey[i] := keyBlock[ofs + cipherKeySize + i]
			END
		END;
		IF cipherBlockSize > 1 THEN
						(*StdLog.String("Generation iv from key:"); StdLog.Int(cipherBlockSize); StdLog.Ln;*)
			ofs := 2 * macSecretSize + 2 * cipherKeySize;
			FOR i := 0 TO cipherBlockSize - 1 DO
				IF SELF.client THEN
					SELF.pendingWriteState.iv[i] := keyBlock[ofs + i];
					SELF.pendingReadState.iv[i] := keyBlock[ofs + cipherBlockSize + i]
				ELSE
					SELF.pendingReadState.iv[i] := keyBlock[ofs + i];
					SELF.pendingWriteState.iv[i] := keyBlock[ofs + cipherBlockSize + i]
				END
			END;
						(*StdLog.String("Created iv for writing:");StdLog.Ln;U.PrintHex(SELF.pendingWriteState.iv, 0, cipherBlockSize); StdLog.Ln;*)
						(*StdLog.String("Created iv for reading:");StdLog.Ln;U.PrintHex(SELF.pendingReadState.iv, 0, cipherBlockSize); StdLog.Ln;*)

		END
	END GenerateKeys;

	PROCEDURE (SELF: Connection) InitializeConnectionState (state : ConnectionState; sp : SecurityParameters), NEW;

	BEGIN
		IF state.cipher # NIL THEN
						(*StdLog.String('Cipher key: ');U.PrintHex(state.cipherKey, 0, sp.cipherKeySize); StdLog.Ln;*)
			state.cipher.InitKey(state.cipherKey, 8 * sp.cipherKeySize);
			IF (state.cipher.blockSize > 0) & (state.aead = NIL) THEN
				state.cipher.SetIV(state.iv, Ciphers.CBC)
			END
		END;
		IF state.aead # NIL THEN
			state.aead.Init(state.cipher, state.cipherKey, sp.cipherKeySize, state.iv)
		END

	END InitializeConnectionState;

	PROCEDURE (SELF: Connection) PrepareConnectionState (state : ConnectionState; cipherSuite : AO.SIGNED32; VAR res : INTEGER), NEW;

	BEGIN
		ASSERT(cipherSuite # 0);
		res := Ok;
		IF IsGcmSuite(cipherSuite) THEN
			(* HMAC не используется, но GenerateKeys читает его длину. *)
			NEW(state.mac); state.mac.Init("CryptoSHA256");
			state.cipher := Ciphers.NewCipher("CryptoAES");
			state.aead := NewRecordAEAD(cipherSuite)
		ELSIF IsChaChaSuite(cipherSuite) THEN
			NEW(state.mac); state.mac.Init("CryptoSHA256");
			state.cipher := NIL;
			state.aead := NewRecordAEAD(cipherSuite)
		ELSE
		CASE cipherSuite OF
			TlsRsaWithNullMd5 :
							NEW(state.mac); state.mac.Init( "CryptoMD5");
					state.cipher := NIL
		|       TlsRsaWithNullSha :
							NEW(state.mac); state.mac.Init( "CryptoSHA1");
					state.cipher := NIL
		|       TlsRsaWithRc4128Md5 :
							NEW(state.mac); state.mac.Init( "CryptoMD5");
					state.cipher := Ciphers.NewCipher("CryptoARC4")
		|       TlsRsaWithRc4128Sha :
							NEW(state.mac); state.mac.Init( "CryptoSHA1");
					state.cipher := Ciphers.NewCipher("CryptoARC4")
		|       TlsRsaWithIdeaCbcSha :
							NEW(state.mac); state.mac.Init("CryptoSHA1");
					state.cipher := Ciphers.NewCipher("CryptoIDEA")
		|       TlsRsaWithDesCbcSha :
							NEW(state.mac); state.mac.Init( "CryptoSHA1");
					state.cipher := Ciphers.NewCipher("CryptoDES")
		|       TlsRsaWith3DesEdeCbcSha :
							NEW(state.mac); state.mac.Init(  "CryptoSHA1");
					state.cipher := Ciphers.NewCipher("CryptoDES3")
		|       TlsRsaWithAes128CbcSha,TlsRsaWithAes256CbcSha :
							NEW(state.mac); state.mac.Init( "CryptoSHA1");
					state.cipher := Ciphers.NewCipher("CryptoAES")
		 		|       TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256 :
							NEW(state.mac); state.mac.Init( "CryptoSHA256");
					state.cipher := Ciphers.NewCipher("CryptoAES")
		ELSE
			res := (-1)
		END
		END
	END PrepareConnectionState;

	PROCEDURE (SELF: Connection) PrepareSecurityParameters (sp : SecurityParameters; cipherSuite : AO.SIGNED32; VAR res : INTEGER), NEW;

	BEGIN
		ASSERT(cipherSuite # 0);
		res := Ok;
		sp.cipherSuite := cipherSuite;
		CASE cipherSuite OF
			TlsRsaWithNullMd5 :	     sp.cipherKeySize := 0
		|       TlsRsaWithNullSha :	     sp.cipherKeySize := 0
		|       TlsRsaWithRc4128Md5 :	   sp.cipherKeySize := 16
		|       TlsRsaWithRc4128Sha :	   sp.cipherKeySize := 16
		|       TlsRsaWithIdeaCbcSha :	 	sp.cipherKeySize := 16
		|       TlsRsaWithDesCbcSha :	  sp.cipherKeySize := 8
		|       TlsRsaWith3DesEdeCbcSha :    sp.cipherKeySize := 24
		|       TlsRsaWithAes128CbcSha :      sp.cipherKeySize := 16
		|       TlsRsaWithAes256CbcSha :      sp.cipherKeySize := 32
				|       TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256 :  sp.cipherKeySize := 16
				|       TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
						TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 : sp.cipherKeySize := 16
				|       TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
						TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
						TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
						TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 : sp.cipherKeySize := 32
		ELSE
			res := (-1)
		END;

		IF SELF.version[1] < 3X THEN
			sp.PRFAlgorithm := "SHA1 XOR MD5"
		ELSIF (cipherSuite = TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384)
			OR (cipherSuite = TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384) THEN
			sp.PRFAlgorithm := "CryptoSHA384"
		ELSE
			sp.PRFAlgorithm := "CryptoSHA256"
		END
	END PrepareSecurityParameters;


	PROCEDURE (SELF: Connection) SendError (alertType : AO.SIGNED32), NEW;

	BEGIN

		IF SELF.state = TCP.Closed THEN
			RETURN
		END;
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("Error");
			StdLog.Ln;
			StdLog.String("  alertType: ");
			StdLog.Int(alertType);
			StdLog.Ln
		END;
		SELF.outbuf[0] := (*WARN CONV CAST*)SHORT(CHR(2));
		SELF.outbuf[1] := (*WARN CONV CAST*)SHORT(CHR(alertType));
		SELF.SendRecord(SELF.outbuf, Alert, 0, 2);
		IF SELF.session # NIL THEN
			SELF.session.Invalidate()
		END;
		SELF.Close();

	END SendError;

	PROCEDURE (SELF: Connection) ReceiveChangeCipherSpec (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ChangeCipherSpec");
			StdLog.Ln
		END;
		IF ORD(data[ofs]) # 1 THEN
			SELF.SendError(IllegalParameter)
		ELSE
			SELF.ChangeReadState()
		END
	END ReceiveChangeCipherSpec;


	PROCEDURE (SELF: Connection) SendHandshake (VAR data : ARRAY OF SHORTCHAR; hsType : AO.SIGNED32; ofs, len : AO.SIZE), NEW;
		VAR
			l: AO.SIZE;


	BEGIN
		ASSERT(len < 256 * 256 * 256);
		l := len;
		data[ofs] := (*WARN CONV CAST*)SHORT(CHR(hsType));
		data[ofs + 3] := (*WARN CONV CAST*)SHORT(CHR(l MOD 256));
		l := l DIV 256;
		data[ofs + 2] := (*WARN CONV CAST*)SHORT(CHR(l MOD 256));
		l := l DIV 256;
		data[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(l MOD 256));
		IF hsType # HelloRequest THEN
			IF SELF.version[1] < 03X THEN
				IF (hsType # Finished) OR (hsType # ClientHello) THEN
					SELF.hsMD5send.Update(data, ofs, len + 4);
					SELF.hsSHAsend.Update(data, ofs, len + 4)
				END;
				SELF.hsMD5verify.Update(data, ofs, len + 4);
				SELF.hsSHAverify.Update(data, ofs, len + 4)
			ELSE
				IF (hsType # Finished) OR (hsType # ClientHello) THEN
					SELF.hsHashSend.Update(data, ofs, len + 4)
				END;
				SELF.hsHashVerify.Update(data, ofs, len + 4)
			END
		END;
		SELF.AppendTranscript(data, ofs, len + 4);
		SELF.SendRecord(data, Handshake, ofs, len + 4)
	END SendHandshake;

	PROCEDURE (SELF: Connection) SendCertificateChain (chain : X509.Certificate; VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR
			c : X509.Certificate;
			listLen, pos, i, n : AO.SIZE;
	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("Certificate");
			StdLog.Ln
		END;
		listLen := 0;
		c := chain;
		WHILE c # NIL DO
			IF (c.raw = NIL) OR (c.rawLen <= 0) THEN
				SELF.SendError(InternalError);
				RETURN
			END;
			INC(listLen, 3 + c.rawLen);
			c := c.next
		END;
		IF ofs + 3 + listLen > LEN(data) THEN
			SELF.SendError(InternalError);
			RETURN
		END;
		data[ofs] := (*WARN CONV CAST*)SHORT(CHR(listLen DIV (256 * 256)));
		data[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(listLen DIV 256 MOD 256));
		data[ofs + 2] := (*WARN CONV CAST*)SHORT(CHR(listLen MOD 256));
		pos := ofs + 3;
		c := chain;
		WHILE c # NIL DO
			n := c.rawLen;
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(n DIV (256 * 256)));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(n DIV 256 MOD 256));
			data[pos + 2] := (*WARN CONV CAST*)SHORT(CHR(n MOD 256));
			INC(pos, 3);
			FOR i := 0 TO n - 1 DO
				data[pos + i] := c.raw^[i]
			END;
			INC(pos, n);
			c := c.next
		END;
		SELF.SendHandshake(data, Certificate, ofs - 4, 3 + listLen)
	END SendCertificateChain;

	PROCEDURE (SELF: Connection) SendCertificate (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR i : AO.SIZE;
	BEGIN
		IF SELF.client THEN
			SELF.SendCertificateChain(SELF.context.clientCertChain, data, ofs)
		ELSIF SELF.context.certChain # NIL THEN
			SELF.SendCertificateChain(SELF.context.certChain, data, ofs)
		ELSIF (SELF.context.lengthOfRsaCertificate > 0)
			& (SELF.context.lengthOfRsaCertificate <= LEN(data) - ofs) THEN
			(* Совместимость со старым LoadRsaCertificate: там уже лежит
			   готовое тело Certificate вместе с длиной списка. *)
			FOR i := 0 TO SELF.context.lengthOfRsaCertificate - 1 DO
				data[ofs + i] := SELF.context.rsaCertificate[i]
			END;
			SELF.SendHandshake(data, Certificate, ofs - 4, SELF.context.lengthOfRsaCertificate)
		ELSE
			SELF.SendCertificateChain(NIL, data, ofs)
		END
	END SendCertificate;

	PROCEDURE (SELF: Connection) SendServerKeyExchange (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE): BOOLEAN, NEW;
		VAR
			params : ARRAY 4 + MaxGroupPublic OF SHORTCHAR;
			signedData : ARRAY 64 + 4 + MaxGroupPublic OF SHORTCHAR;
			scheme : AO.SIGNED32;
			i, sigLen, signedLen : AO.SIZE;
	BEGIN
		params[0] := 3X;	(* named_curve *)
		params[1] := 0X;
		params[2] := (*WARN CONV CAST*)SHORT(CHR(SELF.group.id));
		params[3] := (*WARN CONV CAST*)SHORT(CHR(SELF.group.publicLen));
		FOR i := 0 TO SELF.group.publicLen - 1 DO
			params[4 + i] := SELF.group.publicKey[i]
		END;
		FOR i := 0 TO 31 DO
			signedData[i] := SELF.pendingSecurityParameters.clientRandom[i];
			signedData[i + 32] := SELF.pendingSecurityParameters.serverRandom[i]
		END;
		signedLen := 64 + 4 + SELF.group.publicLen;
		FOR i := 0 TO 4 + SELF.group.publicLen - 1 DO
			signedData[64 + i] := params[i]
		END;
		scheme := SELF.serverScheme;
		IF scheme = 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN FALSE
		END;
		sigLen := 0;
		IF ~TLSSign(scheme, SELF.context.rsaPrivateKey, SELF.context.ecCurve,
				SELF.context.ecPrivate, signedData, signedLen,
				data, ofs + 4 + SELF.group.publicLen + 4, sigLen) THEN
			SELF.SendError(InternalError);
			RETURN FALSE
		END;
		FOR i := 0 TO 4 + SELF.group.publicLen - 1 DO
			data[ofs + i] := params[i]
		END;
		data[ofs + 4 + SELF.group.publicLen] := (*WARN CONV CAST*)SHORT(CHR(scheme DIV 256));
		data[ofs + 5 + SELF.group.publicLen] := (*WARN CONV CAST*)SHORT(CHR(scheme MOD 256));
		data[ofs + 6 + SELF.group.publicLen] := (*WARN CONV CAST*)SHORT(CHR(sigLen DIV 256));
		data[ofs + 7 + SELF.group.publicLen] := (*WARN CONV CAST*)SHORT(CHR(sigLen MOD 256));
		SELF.SendHandshake(data, ServerKeyExchange, ofs - 4, 8 + SELF.group.publicLen + sigLen);
		RETURN TRUE
	END SendServerKeyExchange;

	PROCEDURE (SELF: Connection) SendCertificateRequest (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR schemes : ARRAY 5 OF AO.SIGNED32; i, pos : AO.SIZE;
	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("CertificateRequest");
			StdLog.Ln
		END;
		(* certificate_types: rsa_sign(1), ecdsa_sign(64). *)
		data[ofs] := 2X;
		data[ofs + 1] := 1X;
		data[ofs + 2] := 40X;
		pos := ofs + 3;
		(* supported_signature_algorithms: 5 схем по 2 байта. *)
		schemes[0] := SigRsaPkcs1Sha256;
		schemes[1] := SigRsaPssRsaeSha256;
		schemes[2] := SigEcdsaP256Sha256;
		schemes[3] := SigEcdsaP384Sha384;
		schemes[4] := SigEd25519;
		data[pos] := 0X; data[pos + 1] := 0AX;
		INC(pos, 2);
		FOR i := 0 TO 4 DO
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(schemes[i] DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(schemes[i] MOD 256));
			INC(pos, 2)
		END;
		(* Нет имён CA. *)
		data[pos] := 0X; data[pos + 1] := 0X;
		INC(pos, 2);
		SELF.SendHandshake(data, CertificateRequest, ofs - 4, pos - ofs)
	END SendCertificateRequest;

	PROCEDURE (SELF: Connection) SendCertificateVerify (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE): BOOLEAN, NEW;
		VAR digest : ARRAY 64 OF SHORTCHAR; scheme : AO.SIGNED32;
			sigLen, digestLen : AO.SIZE;
	BEGIN
		IF (SELF.context.clientCertChain = NIL)
			OR ((SELF.context.clientPrivateKey = NIL) & (SELF.context.clientEcCurve = NIL)) THEN
			SELF.SendError(InternalError);
			RETURN FALSE
		END;
		scheme := SELF.ClientSignScheme();
		IF scheme = 0 THEN
			SELF.SendError(InternalError);
			RETURN FALSE
		END;
		IF ~SELF.TranscriptHash(scheme, digest, digestLen) THEN
			SELF.SendError(InternalError);
			RETURN FALSE
		END;
		IF ~TLSSignDigest(scheme, SELF.context.clientPrivateKey, SELF.context.clientEcCurve,
				SELF.context.clientEcPrivate, digest, digestLen,
				data, ofs + 4, sigLen) THEN
			SELF.SendError(InternalError);
			RETURN FALSE
		END;
		data[ofs] := (*WARN CONV CAST*)SHORT(CHR(scheme DIV 256));
		data[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(scheme MOD 256));
		data[ofs + 2] := (*WARN CONV CAST*)SHORT(CHR(sigLen DIV 256));
		data[ofs + 3] := (*WARN CONV CAST*)SHORT(CHR(sigLen MOD 256));
		SELF.SendHandshake(data, CertificateVerify, ofs - 4, 4 + sigLen);
		RETURN TRUE
	END SendCertificateVerify;

	PROCEDURE (SELF: Connection) ReceiveClientCertificate (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): BOOLEAN, NEW;
		VAR
			listLen, pos, endPos, certLen, i : AO.SIZE;
			der : POINTER TO ARRAY OF SHORTCHAR;
			chain, tail, cert : X509.Certificate;
			res, err : AO.SIGNED32;
			emptyHost : ARRAY 1 OF SHORTCHAR;
	BEGIN
		IF (len < 3) OR (ofs + 3 > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		listLen := ORD(data[ofs]) * 256 * 256 + ORD(data[ofs + 1]) * 256 + ORD(data[ofs + 2]);
		IF listLen + 3 > len THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		pos := ofs + 3;
		endPos := pos + listLen;
		chain := NIL;
		tail := NIL;
		WHILE pos < endPos DO
			IF pos + 3 > endPos THEN
				SELF.SendError(DecodeError);
				RETURN FALSE
			END;
			certLen := ORD(data[pos]) * 256 * 256 + ORD(data[pos + 1]) * 256 + ORD(data[pos + 2]);
			IF (certLen = 0) OR (pos + 3 + certLen > endPos) THEN
				SELF.SendError(DecodeError);
				RETURN FALSE
			END;
			NEW(der, certLen);
			FOR i := 0 TO certLen - 1 DO
				der^[i] := data[pos + 3 + i]
			END;
			cert := X509.ReadCert(der^, 0, certLen, res);
			IF cert = NIL THEN
				LastCertError := 100 + res;
				SELF.SendError(BadCertificate);
				RETURN FALSE
			END;
			IF chain = NIL THEN chain := cert ELSE tail.next := cert END;
			tail := cert;
			INC(pos, 3 + certLen)
		END;
		IF chain = NIL THEN
			LastCertError := 200;
			SELF.SendError(BadCertificate);
			RETURN FALSE
		END;
		emptyHost[0] := 0X;
		IF ~SELF.context.VerifyPeer(chain, chain, emptyHost, err) THEN
			LastCertError := err;
			SELF.SendError(BadCertificate);
			RETURN FALSE
		END;
		SELF.clientCert := chain;
		RETURN TRUE
	END ReceiveClientCertificate;

	PROCEDURE (SELF: Connection) ReceiveCertificateVerify (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): BOOLEAN, NEW;
		VAR digest : ARRAY 64 OF SHORTCHAR; dummy : ARRAY 1 OF SHORTCHAR;
			scheme, sigLen, digestLen : AO.SIZE; ok : BOOLEAN;
	BEGIN
		IF (len < 4) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		scheme := 256 * ORD(data[ofs]) + ORD(data[ofs + 1]);
		sigLen := 256 * ORD(data[ofs + 2]) + ORD(data[ofs + 3]);
		IF (sigLen = 0) OR (sigLen > MaxSignatureLength) OR (4 + sigLen > len) THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		IF SELF.clientCert = NIL THEN
			LastCertError := 300;
			SELF.SendError(BadCertificate);
			RETURN FALSE
		END;
		IF scheme = SigEd25519 THEN
			IF SELF.transcript # NIL THEN
				ok := TLSVerify(scheme, SELF.clientCert, SELF.transcript^,
					SELF.transcriptLen, data, ofs + 4, sigLen)
			ELSE
				ok := TLSVerify(scheme, SELF.clientCert, dummy, 0,
					data, ofs + 4, sigLen)
			END
		ELSE
			ok := SELF.TranscriptHash(scheme, digest, digestLen)
				& TLSVerifyDigest(scheme, SELF.clientCert, digest, digestLen,
					data, ofs + 4, sigLen)
		END;
		IF ~ok THEN
			SELF.SendError(DecryptError);
			RETURN FALSE
		END;
		RETURN TRUE
	END ReceiveCertificateVerify;


	PROCEDURE (SELF: Connection) ReceiveClientHello (VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): AO.SIGNED32, NEW;
		VAR
			nofSuites, i, idLen, cipherSuite, major, minor, suiteBytes, chosen: AO.SIGNED32;
			idPos, pos, endPos, compPos, extPos, extEnd, epos, extType, elen, listLen, ipos: AO.SIZE;
			gid, scheme, b: AO.SIGNED32;
			res: INTEGER;
			found: BOOLEAN;


	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ClientHello");
			StdLog.Ln
		END;
		IF (len < 2 + 32 + 1) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		pos := ofs;
		endPos := ofs + len;
		major := ORD(data[pos]);
		minor := ORD(data[pos + 1]);
		IF Trace THEN
			StdLog.String("  version: "); StdLog.Int(major); StdLog.String("."); StdLog.Int(minor);
			StdLog.Ln
		END;
		(* Сервер говорит только TLS 1.2. *)
		IF (major # 3) OR (minor < 3) THEN
			SELF.SendError(ProtocolVersion);
			RETURN (-1)
		END;
		INC(pos, 2);
		FOR i := 0 TO 31 DO
			SELF.pendingSecurityParameters.clientRandom[i] := data[pos + i]
		END;
		INC(pos, 32);
		idLen := ORD(data[pos]);
		idPos := pos + 1;
		IF (idLen > 32) OR (pos + 1 + idLen + 2 > endPos) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		INC(pos, idLen + 1);
		suiteBytes := 256 * ORD(data[pos]) + ORD(data[pos + 1]);
		IF ODD(suiteBytes) OR (suiteBytes = 0) OR (pos + 2 + suiteBytes + 1 > endPos) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		nofSuites := suiteBytes DIV 2;
		INC(pos, 2);
		IF (idLen = SessionIdLength) & SELF.context.policy.sessionResumptionEnabled THEN
			SELF.session := SELF.context.FindSessionByID(data, idPos, idLen);
			IF SELF.session # NIL THEN
				FOR i := 0 TO nofSuites - 1 DO
					cipherSuite := 256 * ORD(data[pos + 2 * i]) + ORD(data[pos + 2 * i + 1]);
					IF cipherSuite = SELF.session.cipherSuite THEN
						SELF.pendingSecurityParameters.cipherSuite := SELF.session.cipherSuite;
						RETURN GenerateHelloWithResumption
					END
				END
			END
		END;
		SELF.session := SELF.context.GetNewServerSession();
		(* Разбор расширений: supported_groups (10), signature_algorithms
		   (13), ec_point_formats (11), server_name (0), renegotiation_info
		   (0xff01). Остальные пропускаются. *)
		SELF.haveSigSchemes := FALSE;
		SELF.clientSigSchemes := {};
		SELF.haveGroups := FALSE;
		SELF.clientGroups := {};
		compPos := pos + suiteBytes;
		IF compPos + 1 > endPos THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		extPos := compPos + 1 + ORD(data[compPos]);
		IF extPos > endPos THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		IF extPos + 2 <= endPos THEN
			extEnd := extPos + 2 + 256 * ORD(data[extPos]) + ORD(data[extPos + 1]);
			IF extEnd > endPos THEN
				SELF.SendError(DecodeError);
				RETURN (-1)
			END;
			epos := extPos + 2;
			WHILE epos + 4 <= extEnd DO
				extType := 256 * ORD(data[epos]) + ORD(data[epos + 1]);
				elen := 256 * ORD(data[epos + 2]) + ORD(data[epos + 3]);
				IF epos + 4 + elen > extEnd THEN
					SELF.SendError(DecodeError);
					RETURN (-1)
				END;
				IF (extType = 13) & (elen >= 2) THEN
					(* signature_algorithms: список по 2 байта. *)
					listLen := 256 * ORD(data[epos + 4]) + ORD(data[epos + 5]);
					IF listLen + 2 <= elen THEN
						SELF.haveSigSchemes := TRUE;
						ipos := epos + 6;
						WHILE ipos + 2 <= epos + 6 + listLen DO
							scheme := 256 * ORD(data[ipos]) + ORD(data[ipos + 1]);
							b := SchemeBit(scheme);
							IF b >= 0 THEN INCL(SELF.clientSigSchemes, SHORT(b)) END;
							INC(ipos, 2)
						END
					END
				ELSIF (extType = 10) & (elen >= 2) THEN
					(* supported_groups: список по 2 байта. *)
					listLen := 256 * ORD(data[epos + 4]) + ORD(data[epos + 5]);
					IF listLen + 2 <= elen THEN
						SELF.haveGroups := TRUE;
						ipos := epos + 6;
						WHILE ipos + 2 <= epos + 6 + listLen DO
							gid := 256 * ORD(data[ipos]) + ORD(data[ipos + 1]);
							b := GroupBit(gid);
							IF b >= 0 THEN INCL(SELF.clientGroups, SHORT(b)) END;
							INC(ipos, 2)
						END
					END
				END;
				INC(epos, 4 + elen)
			END
		END;
		found := FALSE;
		chosen := 0;
		FOR i := 0 TO nofSuites - 1 DO
			cipherSuite := 256 * ORD(data[pos + 2 * i]) + ORD(data[pos + 2 * i + 1]);
			IF ~found & SELF.context.policy.IsSupported(cipherSuite)
				& SuiteCompatible(SELF.context, cipherSuite) THEN
				chosen := cipherSuite;
				found := TRUE
			END
		END;
		IF ~found THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		cipherSuite := chosen;
		SELF.serverScheme := ServerScheme(SELF);
		SELF.negotiatedSigScheme := SELF.serverScheme;
		gid := ServerGroup(SELF);
		IF (SELF.serverScheme = 0) OR (gid = 0) THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.group := NewGroup(gid);
		IF SELF.group = NIL THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.group.Generate;
		IF Trace THEN
			StdLog.String("cipherSuite:");AO.LogHex(cipherSuite, -4);StdLog.Ln
		END;
		SELF.session.cipherSuite := cipherSuite;
		SELF.PrepareSecurityParameters(SELF.pendingSecurityParameters, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.PrepareConnectionState(SELF.pendingWriteState, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.PrepareConnectionState(SELF.pendingReadState, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		RETURN GenerateServerHello
	END ReceiveClientHello;




	PROCEDURE (SELF: Connection) Create32RandomBytes (VAR data : ARRAY OF SHORTCHAR), NEW;
		VAR
			i, timestamp, d, h, m: AO.SIGNED32;
			unixEpoch, now: Dates.DateTime;


	BEGIN
		ASSERT(LEN(data) > 31);
		Rng.Generate(data, 0, 32);
				
		unixEpoch.year := 1970;
		unixEpoch.month := 1;
		unixEpoch.day := 1;
				unixEpoch.hour := 0;
				unixEpoch.minute := 0;
				unixEpoch.second := 0;
		ASSERT(Dates.ValidDateTime(unixEpoch));
				now := Dates.Now();
		Dates.TimeDifference(now, unixEpoch, d, h, m, timestamp);

		INC(timestamp, m * 60);
		INC(timestamp, h * 3600);
		INC(timestamp, d * 86400);

		FOR i := 3 TO 0 BY (-1) DO
			data[i] := (*WARN CONV CAST*)SHORT(CHR(timestamp));
			timestamp := timestamp DIV 256
		END
	END Create32RandomBytes;

	PROCEDURE (SELF: Connection) SendServerHello (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR
			i, pos: AO.SIZE;


	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("ServerHello");
			StdLog.Ln
		END;
		ASSERT(ofs > 3);
		pos := ofs;
		data[pos] := SELF.version[0];
		data[pos + 1] := SELF.version[1];
		INC(pos, 2);
		SELF.Create32RandomBytes(SELF.pendingSecurityParameters.serverRandom);
		FOR i := 0 TO 31 DO
			data[pos + i] := SELF.pendingSecurityParameters.serverRandom[i]
		END;
		INC(pos, 32);
		data[pos] := (*WARN CONV CAST*)SHORT(CHR(SELF.session.idLength));
		INC(pos);
		FOR i := 0 TO SELF.session.idLength - 1 DO
			data[pos + i] := SELF.session.id[i]
		END;
		INC(pos, SELF.session.idLength);
		data[pos] := (*WARN CONV CAST*)SHORT(CHR(SELF.pendingSecurityParameters.cipherSuite DIV 256));
		data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(SELF.pendingSecurityParameters.cipherSuite MOD 256));
		INC(pos, 2);
		data[pos] := (*WARN CONV CAST*)SHORT(CHR(0));
		INC(pos);
		(* Расширения: renegotiation_info (RFC 5746) с пустой связкой.
		   Без него OpenSSL 3 отвергает рукопожатие как небезопасное. *)
		data[pos] := 0X;
		data[pos + 1] := 5X;	(* длина списка расширений *)
		data[pos + 2] := 0FFX;
		data[pos + 3] := 01X;	(* тип renegotiation_info *)
		data[pos + 4] := 0X;
		data[pos + 5] := 1X;	(* длина данных *)
		data[pos + 6] := 0X;	(* пустая renegotiated_connection *)
		INC(pos, 7);
		SELF.SendHandshake(data, ServerHello, ofs - 4, pos - ofs)
	END SendServerHello;

	PROCEDURE (SELF: Connection) SendServerHelloDone (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;

	BEGIN
		ASSERT(ofs > 3);
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("ServerHelloDone");
			StdLog.Ln
		END;
		SELF.SendHandshake(data, ServerHelloDone, ofs - 4, 0)
	END SendServerHelloDone;

	PROCEDURE (SELF: Connection) ReceiveClientKeyExchange (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): BOOLEAN, NEW;
		VAR
			remotePubKey: ARRAY MaxGroupPublic OF SHORTCHAR;
			tmp: ARRAY 512 OF SHORTCHAR;
			rnd: ARRAY 48 OF SHORTCHAR;
			i, plen: AO.SIZE;
			ok: BOOLEAN;


	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ClientKeyExchange");
			StdLog.Ln
		END;
		IF (len < 1) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		IF IsEcdheSuite(SELF.pendingSecurityParameters.cipherSuite) THEN
			IF (len # 1 + SELF.group.publicLen) OR (ORD(data[ofs]) # SELF.group.publicLen) THEN
				SELF.SendError(DecodeError);
				RETURN FALSE
			END;
			FOR i := 0 TO SELF.group.publicLen - 1 DO
				remotePubKey[i] := data[ofs + 1 + i]
			END;
			ok := SELF.group.Shared(remotePubKey, SELF.group.publicLen,
				SELF.sharedSecret, SELF.sharedLen);
			IF ~ok THEN
				SELF.SendError(IllegalParameter);
				RETURN FALSE
			END;
			SELF.session.preMasterSecretLen := SELF.sharedLen;
			SELF.session.SetPreMasterSecret(SELF.sharedSecret, 0, SELF.sharedLen)
		ELSE
			(* RSA key transport: только при явно заданном наборе. *)
			IF (SELF.context.rsaPrivateKey = NIL) OR (len < 3) THEN
				SELF.SendError(IllegalParameter);
				RETURN FALSE
			END;
			IF ~U.RandomBytesOk(rnd, 0, 48) THEN
				SELF.SendError(InternalError);
				RETURN FALSE
			END;
			plen := 0;
			PKCS1.Decrypt(data, ofs + 2, len - 2, SELF.context.rsaPrivateKey, 2, tmp, 0, plen);
			IF plen = 48 THEN
				SELF.session.SetPreMasterSecret(tmp, 0, 48)
			ELSE
				(* Ошибка дополнения не должна менять ход рукопожатия
				   заметно для атакующего (Bleichenbacher). *)
				SELF.session.SetPreMasterSecret(rnd, 0, 48)
			END
		END;
		SELF.GenerateMasterSecret();
		SELF.GenerateKeys();
		SELF.InitializeConnectionState(SELF.pendingWriteState, SELF.pendingSecurityParameters);
		SELF.InitializeConnectionState(SELF.pendingReadState, SELF.pendingSecurityParameters);
		RETURN TRUE
	END ReceiveClientKeyExchange;

	PROCEDURE (SELF: Connection) ReceiveFinished (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): BOOLEAN, NEW;
		VAR
			seed: ARRAY 64 OF SHORTCHAR;
			verifyData: ARRAY 12 OF SHORTCHAR;
			prf: PRF;
			i, seedSize: AO.SIZE;


	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("Finished");
			StdLog.Ln
		END;
		IF len # 12 THEN
			SELF.SendError(DecodeError);
			RETURN FALSE
		END;
		IF SELF.version[1] < 03X THEN
			SELF.hsMD5verify.GetHash(seed, 0);
			SELF.hsSHAverify.GetHash(seed, 16);
			seedSize := 36
		ELSE
			seedSize := SELF.PrfDigestLen();
			FOR i := 0 TO seedSize - 1 DO
				seed[i] := SELF.finishedDigest[i]
			END
		END;
		NEW(prf); prf.Init(SELF.pendingSecurityParameters.PRFAlgorithm);
		IF SELF.client THEN
			prf.GetBytes(SELF.session.masterSecret, seed, verifyData, "server finished", 48, seedSize, 12)
		ELSE
			prf.GetBytes(SELF.session.masterSecret, seed, verifyData, "client finished", 48, seedSize, 12)
		END;
		(* Сравнение постоянного времени: verify_data — секрет. *)
		IF ~U.EqualBytesCT(verifyData, data, 0, ofs, 12) THEN
			SELF.SendError(DecryptError);
			RETURN FALSE
		END;
		RETURN TRUE
	END ReceiveFinished;
	PROCEDURE (SELF: Connection) SendChangeCipherSpec , NEW;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("ChangeCipherSpec");
			StdLog.Ln
		END;
		SELF.outbuf[0] := (*WARN CONV CAST*)SHORT(CHR(1));
		SELF.SendRecord(SELF.outbuf, ChangeCipherSpec, 0, 1);
		SELF.ChangeWriteState()
	END SendChangeCipherSpec;

	PROCEDURE (SELF: Connection) SendFinished (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE), NEW;
		VAR
			seed: ARRAY 64 OF SHORTCHAR;
			verifyData: ARRAY 12 OF SHORTCHAR;
			i, seedSize: AO.SIZE;
			prf: PRF;


	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("Finished");
			StdLog.Ln
		END;
		ASSERT(ofs > 3);
		IF SELF.version[1] < 03X THEN
			SELF.hsMD5send.GetHash(seed, 0);
			SELF.hsSHAsend.GetHash(seed, 16);
			seedSize := 36
		ELSE
			seedSize := SELF.PrfDigestLen();
			SELF.TranscriptDigest(seed)
		END;
		NEW(prf); prf.Init(SELF.pendingSecurityParameters.PRFAlgorithm);
		IF SELF.client THEN
			prf.GetBytes(SELF.session.masterSecret, seed, verifyData, "client finished", 48, seedSize, 12)
		ELSE
			prf.GetBytes(SELF.session.masterSecret, seed, verifyData, "server finished", 48, seedSize, 12)
		END;
		FOR i := 0 TO 11 DO
			data[ofs + i] := verifyData[i]
		END;
		SELF.SendHandshake(data, Finished, ofs - 4, 12)
	END SendFinished;

	PROCEDURE (SELF: Connection) ReceiveServerHello (VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): AO.SIGNED32, NEW;
		VAR
			major, minor, i, idLen, cipherSuite: AO.SIGNED32;
			idPos, pos, endPos: AO.SIZE;
			res: INTEGER;
			empty: ARRAY 1 OF SHORTCHAR;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ServerHello");
			StdLog.Ln
		END;
		IF (len < 2 + 32 + 1 + 2 + 1) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		pos := ofs;
		endPos := ofs + len;
		major := ORD(data[pos]);
		minor := ORD(data[pos + 1]);
		(* Мы предлагали только TLS 1.2. *)
		IF (major # 3) OR (minor # 3) THEN
			SELF.SendError(ProtocolVersion);
			RETURN (-1)
		END;
		INC(pos, 2);
		FOR i := 0 TO 31 DO
			SELF.pendingSecurityParameters.serverRandom[i] := data[pos + i]
		END;
		INC(pos, 32);
		idLen := ORD(data[pos]);
		idPos := pos + 1;
		IF (idLen > 32) OR (pos + 1 + idLen + 3 > endPos) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		INC(pos, idLen + 1);
		cipherSuite := 256 * ORD(data[pos]) + ORD(data[pos + 1]);
		IF ~SELF.context.policy.IsSupported(cipherSuite) THEN
			(* Сервер не вправе выбрать набор, которого мы не предлагали. *)
			SELF.SendError(IllegalParameter);
			RETURN (-1)
		END;
		IF ORD(data[pos + 2]) # 0 THEN
			SELF.SendError(IllegalParameter);
			RETURN (-1)
		END;
		empty[0] := 0X;
		SELF.session := NIL;
		IF (idLen >= 2) & (idLen <= 32) THEN
			SELF.session := SELF.context.FindSessionByID(data, idPos, idLen)
		END;
		IF SELF.session = NIL THEN
			NEW(SELF.session); SELF.session.Init(FALSE, empty, 0, 0, SELF.context.policy.sessionLifetime)
		END;
		SELF.session.cipherSuite := cipherSuite;
		SELF.PrepareSecurityParameters(SELF.pendingSecurityParameters, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.PrepareConnectionState(SELF.pendingWriteState, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		SELF.PrepareConnectionState(SELF.pendingReadState, cipherSuite, res);
		IF res < 0 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		RETURN AwaitCertificate
	END ReceiveServerHello;

	PROCEDURE (SELF: Connection) SendClientHello (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIGNED32), NEW;
		VAR
			pos, i, sniLen, extLenPos, extStart, extLen: AO.SIZE;
			n, id: AO.SIGNED32;
	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("ClientHello");
			StdLog.Ln
		END;
		ASSERT(ofs > 3);
		pos := ofs;
		data[pos] := SELF.version[0];
		data[pos + 1] := SELF.version[1];
		INC(pos, 2);
		SELF.Create32RandomBytes(SELF.pendingSecurityParameters.clientRandom);
		FOR i := 0 TO 31 DO
			data[pos + i] := SELF.pendingSecurityParameters.clientRandom[i]
		END;
		INC(pos, 32);
		IF SELF.session # NIL THEN
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(SELF.session.idLength));
			INC(pos);
			FOR i := 0 TO SELF.session.idLength - 1 DO
				data[pos + i] := SELF.session.id[i]
			END;
			INC(pos, SELF.session.idLength)
		ELSE
			data[pos] := 0X;
			INC(pos)
		END;
		n := 2 * SELF.context.policy.nofCipherSuites;
		data[pos] := (*WARN CONV CAST*)SHORT(CHR(n DIV 256));
		data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(n MOD 256));
		INC(pos, 2);
		FOR i := 0 TO SELF.context.policy.nofCipherSuites - 1 DO
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(SELF.context.policy.cipherSuites[i] DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(SELF.context.policy.cipherSuites[i] MOD 256));
			INC(pos, 2)
		END;
		(* Только null-сжатие. *)
		data[pos] := 1X;
		INC(pos);
		data[pos] := 0X;
		INC(pos);
		extLenPos := pos;
		INC(pos, 2);
		extStart := pos;
		(* SNI: нужно для выбора виртуального хоста. *)
		sniLen := 0;
		WHILE (sniLen < 255) & (SELF.serverName[sniLen] # 0X) DO INC(sniLen) END;
		IF sniLen > 0 THEN
			data[pos] := 0X; data[pos + 1] := 0X; INC(pos, 2);
			n := 5 + sniLen;
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(n DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(n MOD 256));
			INC(pos, 2);
			n := 3 + sniLen;
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(n DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(n MOD 256));
			INC(pos, 2);
			data[pos] := 0X; INC(pos);
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(sniLen DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(sniLen MOD 256));
			INC(pos, 2);
			FOR i := 0 TO sniLen - 1 DO
				data[pos + i] := SELF.serverName[i]
			END;
			INC(pos, sniLen)
		END;
		(* supported_groups: x25519, secp256r1, secp384r1. *)
		data[pos] := 0X; data[pos + 1] := 0AX; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 8X; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 6X; INC(pos, 2);
		FOR i := 0 TO 2 DO
			CASE i OF
				0 : id := GroupX25519
			| 1 : id := GroupSecp256r1
			ELSE id := GroupSecp384r1
			END;
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(id DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(id MOD 256));
			INC(pos, 2)
		END;
		(* ec_point_formats: только uncompressed. *)
		data[pos] := 0X; data[pos + 1] := 0BX; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 2X; INC(pos, 2);
		data[pos] := 1X; INC(pos);
		data[pos] := 0X; INC(pos);
		(* signature_algorithms: ECDSA P-256/P-384, RSA-PSS 256/384,
		   RSA PKCS#1 SHA-256, Ed25519 (только проверка). *)
		data[pos] := 0X; data[pos + 1] := 0DX; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 0EX; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 0CX; INC(pos, 2);
		FOR i := 0 TO 5 DO
			CASE i OF
				0 : n := SigEcdsaP256Sha256
			| 1 : n := SigEcdsaP384Sha384
			| 2 : n := SigRsaPssRsaeSha256
			| 3 : n := SigRsaPssRsaeSha384
			| 4 : n := SigRsaPkcs1Sha256
			ELSE n := SigEd25519
			END;
			data[pos] := (*WARN CONV CAST*)SHORT(CHR(n DIV 256));
			data[pos + 1] := (*WARN CONV CAST*)SHORT(CHR(n MOD 256));
			INC(pos, 2)
		END;
		(* renegotiation_info (RFC 5746), пустая связка. *)
		data[pos] := 0FFX; data[pos + 1] := 01X; INC(pos, 2);
		data[pos] := 0X; data[pos + 1] := 1X; INC(pos, 2);
		data[pos] := 0X; INC(pos);
		extLen := pos - extStart;
		data[extLenPos] := (*WARN CONV CAST*)SHORT(CHR(extLen DIV 256));
		data[extLenPos + 1] := (*WARN CONV CAST*)SHORT(CHR(extLen MOD 256));
		SELF.SendHandshake(data, ClientHello, ofs - 4, pos - ofs)
	END SendClientHello;

	PROCEDURE (SELF: Connection) ReceiveCertificate (VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): AO.SIGNED32, NEW;
		VAR
			listLen, pos, endPos, certLen, i : AO.SIZE;
			der : POINTER TO ARRAY OF SHORTCHAR;
			chain, tail, cert : X509.Certificate;
			res, err : AO.SIGNED32;
	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("Certificate");
			StdLog.Ln
		END;
		IF (len < 6) OR (ofs + 6 > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		listLen := ORD(data[ofs]) * 256 * 256 + ORD(data[ofs + 1]) * 256 + ORD(data[ofs + 2]);
		IF listLen + 3 > len THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		pos := ofs + 3;
		endPos := pos + listLen;
		chain := NIL;
		tail := NIL;
		WHILE pos < endPos DO
			IF pos + 3 > endPos THEN
				SELF.SendError(DecodeError);
				RETURN (-1)
			END;
			certLen := ORD(data[pos]) * 256 * 256 + ORD(data[pos + 1]) * 256 + ORD(data[pos + 2]);
			IF (certLen = 0) OR (pos + 3 + certLen > endPos) THEN
				SELF.SendError(DecodeError);
				RETURN (-1)
			END;
			NEW(der, certLen);
			FOR i := 0 TO certLen - 1 DO
				der^[i] := data[pos + 3 + i]
			END;
			cert := X509.ReadCert(der^, 0, certLen, res);
			IF cert = NIL THEN
				SELF.SendError(BadCertificate);
				RETURN (-1)
			END;
			IF chain = NIL THEN chain := cert ELSE tail.next := cert END;
			tail := cert;
			INC(pos, 3 + certLen)
		END;
		IF chain = NIL THEN
			SELF.SendError(BadCertificate);
			RETURN (-1)
		END;
		SELF.serverCert := chain;
		IF ~SELF.context.VerifyPeer(chain, chain, SELF.serverName, err) THEN
			LastCertError := err;
			IF Trace THEN
				StdLog.String("Certificate rejected, code "); StdLog.Int(err); StdLog.Ln
			END;
			CASE err OF
				X509.ErrExpired, X509.ErrNotYetValid :    SELF.SendError(CertificateExpired)
			|       X509.ErrHostname :	         SELF.SendError(BadCertificate)
			|       X509.ErrUnsupported :	       SELF.SendError(UnsupportedCertificate)
			ELSE
				SELF.SendError(BadCertificate)
			END;
			RETURN (-1)
		END;
		SELF.peerVerified := TRUE;
		IF IsEcdheSuite(SELF.pendingSecurityParameters.cipherSuite) THEN
			RETURN AwaitServerKeyExchange
		END;
		RETURN AwaitServerHelloDone
	END ReceiveCertificate;

	PROCEDURE (SELF: Connection) ReceiveServerKeyExchange (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): AO.SIGNED32, NEW;
			VAR
				remotePubKey : ARRAY MaxGroupPublic OF SHORTCHAR;
				signedData : ARRAY 64 + 4 + MaxGroupPublic OF SHORTCHAR;
				curveId, scheme, pubLen, pos, sigLen, signedLen, paramsLen : AO.SIZE;
				ok : BOOLEAN;
	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ReceiveServerKeyExchange");
			StdLog.Ln
		END;
		(* ServerECDHParams: curve_type(1) + named_curve(2) + pubkey_len(1)
		   + pubkey, затем SignatureScheme(2), sig_len(2), подпись.
		   Все длины проверяются до копирования. *)
		IF (len < 4 + 1 + 4) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		IF ORD(data[ofs]) # 3 THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		curveId := 256 * ORD(data[ofs + 1]) + ORD(data[ofs + 2]);
		pubLen := ORD(data[ofs + 3]);
		(* Группу сервер выбирает из supported_groups; ключ
		   генерируем под неё. *)
		IF (curveId = GroupX25519) OR (curveId = GroupSecp256r1)
			OR (curveId = GroupSecp384r1) THEN
			IF (SELF.group = NIL) OR (SELF.group.id # curveId) THEN
				SELF.group := NewGroup(curveId);
				IF SELF.group # NIL THEN SELF.group.Generate END
			END
		ELSE
			SELF.group := NIL
		END;
		IF (SELF.group = NIL) OR (pubLen # SELF.group.publicLen)
			OR (4 + pubLen + 4 > len) THEN
			SELF.SendError(IllegalParameter);
			RETURN (-1)
		END;
		paramsLen := 4 + pubLen;
		scheme := 256 * ORD(data[ofs + paramsLen]) + ORD(data[ofs + paramsLen + 1]);
		sigLen := 256 * ORD(data[ofs + paramsLen + 2]) + ORD(data[ofs + paramsLen + 3]);
		IF (sigLen = 0) OR (sigLen > MaxSignatureLength) OR (paramsLen + 4 + sigLen > len) THEN
			SELF.SendError(DecodeError);
			RETURN (-1)
		END;
		IF ~ClientSchemeSupported(scheme) THEN
			SELF.SendError(HandshakeFailure);
			RETURN (-1)
		END;
		(* Подпись покрывает client_random || server_random || params. *)
		FOR pos := 0 TO 31 DO
			signedData[pos] := SELF.pendingSecurityParameters.clientRandom[pos];
			signedData[pos + 32] := SELF.pendingSecurityParameters.serverRandom[pos]
		END;
		signedLen := 64 + paramsLen;
		FOR pos := 0 TO paramsLen - 1 DO
			signedData[64 + pos] := data[ofs + pos]
		END;
		IF SELF.serverCert = NIL THEN
			SELF.SendError(BadCertificate);
			RETURN (-1)
		END;
		IF ~TLSVerify(scheme, SELF.serverCert, signedData, signedLen,
				data, ofs + paramsLen + 4, sigLen) THEN
			IF Trace THEN
				StdLog.String("ServerKeyExchange signature is invalid"); StdLog.Ln
			END;
			SELF.SendError(DecryptError);
			RETURN (-1)
		END;
		SELF.negotiatedSigScheme := scheme;
		FOR pos := 0 TO pubLen - 1 DO
			remotePubKey[pos] := data[ofs + 4 + pos]
		END;
		ok := SELF.group.Shared(remotePubKey, pubLen, SELF.sharedSecret, SELF.sharedLen);
		IF ~ok THEN
			SELF.SendError(IllegalParameter);
			RETURN (-1)
		END;
		RETURN AwaitServerHelloDone
	END ReceiveServerKeyExchange;

	PROCEDURE (SELF: Connection) ReceiveServerHelloDone (IN data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE): AO.SIGNED32, NEW;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("ServerHelloDone");
			StdLog.Ln
		END;
		RETURN GenerateClientKeyExchange
	END ReceiveServerHelloDone;


	PROCEDURE (SELF: Connection) ReceiveWarning (desc : AO.SIGNED32), NEW;

	BEGIN
		SELF.tlsErrorCause := desc;
		SELF.SendError(CloseNotify)
	END ReceiveWarning;

	PROCEDURE (SELF: Connection) ReceiveError (alertType : AO.SIGNED32), NEW;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(FALSE);
			StdLog.String("Error");
			StdLog.Ln;
			StdLog.String("  alertType: ");
			StdLog.Int(alertType);
			StdLog.Ln
		END;
		IF SELF.session # NIL THEN
			SELF.session.Invalidate()
		END;
		SELF.Close();


	END ReceiveError;


	PROCEDURE (SELF: Connection) ReceiveAlert (VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE), NEW;
		VAR
			level, description: AO.SIGNED32;
			ptr: AO.SIZE;


	BEGIN
		ptr := ofs;
		WHILE ptr < ofs + len DO
			level := ORD(data[ptr]);
			description := ORD(data[ptr + 1]);
			IF level = Warning THEN
				SELF.ReceiveWarning(description)
			ELSIF level = Fatal THEN
				SELF.ReceiveError(description)
			ELSE
				SELF.SendError(IllegalParameter)
			END;
			INC(ptr, 2)
		END
	END ReceiveAlert;


	PROCEDURE (SELF: Connection) SendClientKeyExchange (VAR data : ARRAY OF SHORTCHAR; ofs : AO.SIZE): AO.SIGNED32, NEW;
		VAR
			length, bodyLen: AO.SIZE;
						i : INTEGER;

	BEGIN
		IF Trace THEN
			SELF.PrintDirection(TRUE);
			StdLog.String("Client Key Exchange");
			StdLog.Ln
		END;
		(* Взаимная проверка: если сервер попросил сертификат, сначала
		   отправляем свою цепочку. *)
		IF SELF.certificateRequested & (SELF.context.clientCertChain # NIL) THEN
			SELF.SendCertificate(data, ofs)
		END;
		IF IsEcdheSuite(SELF.pendingSecurityParameters.cipherSuite) THEN
			SELF.session.preMasterSecretLen := SELF.sharedLen;
			data[ofs] := (*WARN CONV CAST*)SHORT(CHR(SELF.group.publicLen));
			FOR i := 0 TO SELF.sharedLen - 1 DO
				SELF.session.preMasterSecret[i] := SELF.sharedSecret[i]
			END;
			FOR i := 0 TO SELF.group.publicLen - 1 DO
				data[ofs + 1 + i] := SELF.group.publicKey[i]
			END;
			bodyLen := 1 + SELF.group.publicLen
		ELSE
			SELF.context.GetRsaPublicKey;
			IF SELF.context.rsaPublicKey = NIL THEN
				SELF.SendError(InternalError);
				RETURN (-1)
			END;
			SELF.session.preMasterSecretLen := 48;
			length := 0;
			IF ~U.RandomBytesOk(SELF.session.preMasterSecret, 0, 48) THEN
				SELF.SendError(InternalError);
				RETURN (-1)
			END;
			SELF.session.preMasterSecret[0] := SELF.version[0];
			SELF.session.preMasterSecret[1] := SELF.version[1];
			PKCS1.Encrypt(SELF.session.preMasterSecret, 0, SELF.session.preMasterSecretLen,
				SELF.context.rsaPublicKey, 2, data, ofs + 2, length);
			IF length = 0 THEN
				SELF.SendError(InternalError);
				RETURN (-1)
			END;
			data[ofs] := (*WARN CONV CAST*)SHORT(CHR(length DIV 256));
			data[ofs + 1] := (*WARN CONV CAST*)SHORT(CHR(length MOD 256));
			bodyLen := length + 2
		END;
		SELF.SendHandshake(data, ClientKeyExchange, ofs - 4, bodyLen);
		IF SELF.certificateRequested & (SELF.context.clientCertChain # NIL) THEN
			IF ~SELF.SendCertificateVerify(data, ofs) THEN
				RETURN (-1)
			END
		END;
		SELF.GenerateMasterSecret;
		SELF.GenerateKeys;
		SELF.InitializeConnectionState(SELF.pendingWriteState, SELF.pendingSecurityParameters);
		SELF.InitializeConnectionState(SELF.pendingReadState, SELF.pendingSecurityParameters);
		RETURN GenerateChangeCipherSpec
	END SendClientKeyExchange;


	PROCEDURE (SELF: Connection) Transition (VAR data : ARRAY OF SHORTCHAR; action, subAction : AO.SIGNED32; ofs, len : AO.SIZE), NEW;
		VAR
			dummy: ARRAY 1 OF SHORTCHAR;
			res: INTEGER;


	BEGIN
		IF action = ChangeCipherSpec THEN
			CASE SELF.handshakeState OF
				AwaitChangeSpec :		       SELF.ReceiveChangeCipherSpec(data, ofs);
			SELF.handshakeState := AwaitFinished
			|       AwaitChangeSpecWithResumption :			 SELF.ReceiveChangeCipherSpec(data, ofs);
			SELF.handshakeState := AwaitFinishedWithResumption
			ELSE
				SELF.SendError(UnexpectedMessage)
			END
		ELSIF action = Handshake THEN
			IF SELF.State() = ServerHandshake THEN
				CASE SELF.handshakeState OF
					AwaitClientHello :
						IF subAction = ClientHello THEN
							SELF.handshakeState := SELF.ReceiveClientHello(data, ofs, len);
							SELF.Transition(dummy, Handshake, 0, 0, 0)
						ELSE
							(* SSLv2 ClientHello: разбор старого формата не
							   поддерживается. *)
							SELF.SendError(ProtocolVersion)
						END
				|       GenerateServerHello :			   SELF.SendServerHello(SELF.outbuf, 4);
				SELF.SendCertificate(SELF.outbuf, 4);
					IF IsEcdheSuite(SELF.pendingSecurityParameters.cipherSuite) THEN
						IF ~SELF.SendServerKeyExchange(SELF.outbuf, 4) THEN
							RETURN
						END
					END;
					IF SELF.context.requireClientCert THEN
						SELF.SendCertificateRequest(SELF.outbuf, 4)
					END;
				SELF.handshakeState := GenerateServerHelloDone;
				SELF.Transition(dummy, Handshake, 0, 0, 0)
				|       GenerateServerHelloDone :			       SELF.SendServerHelloDone(SELF.outbuf, 4);
				SELF.handshakeState := AwaitClientKeyExchange
				|       AwaitClientKeyExchange :
										IF subAction = Certificate THEN
						IF ~SELF.ReceiveClientCertificate(data, ofs, len) THEN
							RETURN
						END
					ELSIF subAction = CertificateVerify THEN
						IF ~SELF.ReceiveCertificateVerify(data, ofs, len) THEN
							RETURN
						END;
						SELF.clientCertVerified := TRUE;
						SELF.handshakeState := AwaitChangeSpec
					ELSIF subAction = ClientKeyExchange THEN
						IF ~SELF.ReceiveClientKeyExchange(data, ofs, len) THEN
							RETURN
						END;
						IF SELF.context.requireClientCert & ~SELF.clientCertVerified THEN
							SELF.handshakeState := AwaitClientKeyExchange
						ELSE
							SELF.handshakeState := AwaitChangeSpec
						END
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				|       AwaitFinished :				 
										IF subAction = Finished THEN
						IF SELF.context.requireClientCert & ~SELF.clientCertVerified THEN
							LastCertError := 400;
							SELF.SendError(BadCertificate);
							RETURN
						END;
						IF ~SELF.ReceiveFinished(data, ofs, len) THEN
							RETURN
						END;
						SELF.handshakeState := GenerateChangeCipherSpec;
						SELF.Transition(dummy, Handshake, 0, 0, 0)
						ELSE
						SELF.SendError(UnexpectedMessage)
	    				    END
				|       GenerateChangeCipherSpec :			      
										SELF.SendChangeCipherSpec;
						SELF.SendFinished(SELF.outbuf, 4);
						SELF.handshakeState := HandshakeFinished;
						SELF.SetState(TCP.Established);
						IF SELF.context.policy.sessionResumptionEnabled THEN
						SELF.context.StoreSession(SELF.session)
						END
				|       GenerateHelloWithResumption :			   			
										SELF.SendServerHello(SELF.outbuf, 4);
						SELF.PrepareSecurityParameters(SELF.pendingSecurityParameters, 			SELF.pendingSecurityParameters.cipherSuite, res);
						IF res < 0 THEN
						SELF.SendError(UnexpectedMessage)
	    				    END;
		    			    SELF.PrepareConnectionState(SELF.pendingWriteState, SELF.pendingSecurityParameters.cipherSuite, res);
			    		    IF res < 0 THEN
				    	    SELF.SendError(HandshakeFailure)
						END;
						SELF.PrepareConnectionState(SELF.pendingReadState, SELF.pendingSecurityParameters.cipherSuite, res);
						IF res < 0 THEN
						SELF.SendError(HandshakeFailure)
						END;
						SELF.GenerateKeys();
						SELF.InitializeConnectionState(SELF.pendingWriteState, SELF.pendingSecurityParameters);
						SELF.InitializeConnectionState(SELF.pendingReadState, SELF.pendingSecurityParameters);
						SELF.SendChangeCipherSpec;
						SELF.SendFinished(SELF.outbuf, 4);
						SELF.handshakeState := AwaitChangeSpecWithResumption
				|       AwaitFinishedWithResumption :			   
										IF subAction = Finished THEN
						IF ~SELF.ReceiveFinished(data, ofs, len) THEN
							RETURN
						END;
						SELF.handshakeState := HandshakeFinished;
						SELF.SetState(TCP.Established)
						ELSE
						SELF.SendError(UnexpectedMessage)
	    				    END
				END
			ELSIF SELF.State() = ClientHandshake THEN
				CASE SELF.handshakeState OF
				GenerateClientHello :
					SELF.SendClientHello(SELF.outbuf, 4);
					SELF.handshakeState := AwaitServerHello
				|       AwaitServerHello :
					IF subAction = ServerHello THEN
						SELF.handshakeState := SELF.ReceiveServerHello(data, ofs, len)
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				|       AwaitCertificate :
					IF subAction = Certificate THEN
						SELF.handshakeState := SELF.ReceiveCertificate(data, ofs, len)
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				|       AwaitServerKeyExchange :
					IF subAction = ServerKeyExchange THEN
						SELF.handshakeState := SELF.ReceiveServerKeyExchange(data, ofs, len)
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				|       AwaitServerHelloDone :
					IF subAction = CertificateRequest THEN
						SELF.certificateRequested := TRUE
					ELSIF subAction = ServerHelloDone THEN
						SELF.handshakeState := SELF.ReceiveServerHelloDone(data, ofs, len);
						SELF.Transition(dummy, Handshake, 0, 0, 0)
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				|       GenerateClientKeyExchange :
					SELF.handshakeState := SELF.SendClientKeyExchange(SELF.outbuf, 4);
					SELF.Transition(dummy, Handshake, 0, 0, 0)
				|       GenerateChangeCipherSpec :
					SELF.SendChangeCipherSpec;
					SELF.SendFinished(SELF.outbuf, 4);
					SELF.handshakeState := AwaitChangeSpec
				|       AwaitChangeSpec :
					(* ChangeCipherSpec приходит отдельным действием; handshake
					   здесь — нарушение порядка. *)
					SELF.SendError(UnexpectedMessage)
				|       AwaitFinished :
					IF subAction = Finished THEN
						IF ~SELF.ReceiveFinished(data, ofs, len) THEN
							RETURN
						END;
						SELF.SetState(TCP.Established)
					ELSE
						SELF.SendError(UnexpectedMessage)
					END
				END
			ELSIF SELF.State() = TCP.Established THEN
				IF subAction = ClientHello THEN
					SELF.SetState((*WARN CONV CAST*)SHORT(ServerHandshake));
					SELF.handshakeState := SELF.ReceiveClientHello(data, ofs, len);
					SELF.Transition(dummy, Handshake, 0, 0, 0)
				ELSE
					SELF.SendError(UnexpectedMessage)
				END
			END
		END
	END Transition;



	PROCEDURE (SELF: Connection) ReceiveHandshake (VAR data : ARRAY OF SHORTCHAR; ofs, len : AO.SIZE), NEW;
		VAR
			msgLen, hsType: AO.SIGNED32;
			ptr: AO.SIZE;
			updateVerify: BOOLEAN;


	BEGIN
		IF (ofs < 0) OR (len < 0) OR (ofs + len > LEN(data)) THEN
			SELF.SendError(DecodeError);
			RETURN
		END;
		ptr := ofs;
		WHILE ptr < ofs + len DO
			IF ptr + 4 > ofs + len THEN
				SELF.SendError(DecodeError);
				RETURN
			END;
			hsType := ORD(data[ptr]);
			msgLen := 256 * 256 * ORD(data[ptr + 1]) + 256 * ORD(data[ptr + 2]) + ORD(data[ptr + 3]);
			IF ptr + 4 + msgLen > ofs + len THEN
				SELF.SendError(DecodeError);
				RETURN
			END;
			IF hsType = Finished THEN
				(* Снимок транскрипта до этого Finished: он и есть
				   вход PRF для проверки verify_data. *)
				SELF.TranscriptDigestAt(SELF.transcriptLen, SELF.finishedDigest)
			END;
			IF SELF.version[1] < 3X THEN
				SELF.hsMD5send.Update(data, ptr, msgLen + 4);
				SELF.hsSHAsend.Update(data, ptr, msgLen + 4);
				IF hsType # Finished THEN
					SELF.hsMD5verify.Update(data, ptr, msgLen + 4);
					SELF.hsSHAverify.Update(data, ptr, msgLen + 4)
				END
			ELSE
				SELF.hsHashSend.Update(data, ptr, msgLen + 4);
				IF hsType # Finished THEN
					SELF.hsHashVerify.Update(data, ptr, msgLen + 4)
				END
			END;
			IF hsType # CertificateVerify THEN
				SELF.AppendTranscript(data, ptr, msgLen + 4)
			END;
			SELF.Transition(data, Handshake, hsType, ptr + 4, msgLen);
			IF hsType = CertificateVerify THEN
				(* CertificateVerify подписывает транскрипт без себя;
				   в хеш он входит только после проверки. *)
				IF SELF.version[1] < 3X THEN
					SELF.hsMD5send.Update(data, ptr, msgLen + 4);
					SELF.hsSHAsend.Update(data, ptr, msgLen + 4);
					SELF.hsMD5verify.Update(data, ptr, msgLen + 4);
					SELF.hsSHAverify.Update(data, ptr, msgLen + 4)
				ELSE
					SELF.hsHashSend.Update(data, ptr, msgLen + 4);
					SELF.hsHashVerify.Update(data, ptr, msgLen + 4)
				END;
				SELF.AppendTranscript(data, ptr, msgLen + 4)
			END;
			ptr := ptr + 4 + msgLen
		END
	END ReceiveHandshake;

	PROCEDURE (SELF: Connection) ReceiveRecord , NEW;
		VAR
			macInput: ARRAY 13 OF SHORTCHAR;
			verify: ARRAY 32 OF SHORTCHAR;
			i, padd, len, type, macSize, major, minor, ofs, length, bs: AO.SIZE;
			badRecordMac: BOOLEAN;
			additionalData: ARRAY 13 OF SHORTCHAR;
			tag: ARRAY 16 OF SHORTCHAR;
			seq, explicit: ARRAY 8 OF SHORTCHAR;
	BEGIN
		badRecordMac := FALSE;
		len := 0;
		type := 0;
		macSize := 0;
		major := 0;
		minor := 0;
		ofs := 0;
		length := 0;
		type := ORD(SELF.in.Get());
		IF SELF.in.res # Streams.Ok THEN
			SELF.Close;
			RETURN
		END;
		IF type = 128 THEN
			(* SSLv2 ClientHello не поддерживаем: непроверенный разбор
			   старого формата — лишняя поверхность атаки. *)
			SELF.SendError(ProtocolVersion);
			RETURN
		END;
		major := ORD(SELF.in.Get());
		minor := ORD(SELF.in.Get());
		len := 256 * ORD(SELF.in.Get()) + ORD(SELF.in.Get());
		IF SELF.in.res # Streams.Ok THEN
			SELF.Close;
			RETURN
		END;
		IF (major < 3) OR (minor < 1) OR (major > ORD(SELF.version[0])) OR (minor > ORD(SELF.version[1])) THEN
			SELF.SendError(ProtocolVersion);
			RETURN
		END;
		IF (len > MaxCiphertextLength) OR (len > LEN(SELF.inbuf)) THEN
			SELF.SendError(RecordOverflow);
			RETURN
		END;
		SELF.in.Bytes(SELF.inbuf, 0, len, length);
		IF (SELF.in.res # Streams.Ok) OR (length # len) THEN
			IF Trace THEN
				StdLog.String("Can't read record: supposed to read:");
				StdLog.Int(len);
				StdLog.String(" read: ");
				StdLog.Int(length)
			END;
			SELF.Close;
			RETURN
		END;
		IF (type < 20) OR (type > 23) THEN
			RETURN
		END;
		SELF.currentReadState.recordSeq.Inc();
		IF (SELF.currentReadState.cipher # NIL) & (SELF.currentReadState.aead = NIL) THEN
			bs := SELF.currentReadState.cipher.blockSize;
			IF (bs > 1) & ((len < bs) OR (len MOD bs # 0)) THEN
				SELF.SendError(BadRecordMac);
				RETURN
			END;
			SELF.currentReadState.cipher.Decrypt(SELF.inbuf, 0, len);
			IF (minor >= 2) & (bs > 1) THEN
				IF len <= bs THEN
					SELF.SendError(BadRecordMac);
					RETURN
				END;
				ofs := bs;
				DEC(len, bs)
			END;
			IF bs > 1 THEN
				IF len < 1 THEN
					SELF.SendError(BadRecordMac);
					RETURN
				END;
				padd := ORD(SELF.inbuf[ofs + len - 1]);
				IF padd + 1 > len THEN
					badRecordMac := TRUE
				ELSE
					FOR i := 0 TO padd DO
						IF ORD(SELF.inbuf[ofs + len - 1 - i]) # padd THEN
							badRecordMac := TRUE
						END
					END;
					DEC(len, padd + 1)
				END
			END
		END;
		IF SELF.currentReadState.aead # NIL THEN
			IF len < SELF.currentReadState.aead.tagLen + SELF.currentReadState.aead.explicitLen THEN
				SELF.SendError(BadRecordMac);
				RETURN
			END;
			DEC(len, SELF.currentReadState.aead.tagLen);
			FOR i := 0 TO SELF.currentReadState.aead.tagLen - 1 DO
				tag[i] := SELF.inbuf[ofs + len + i]
			END;
			FOR i := 0 TO SELF.currentReadState.aead.explicitLen - 1 DO
				explicit[i] := SELF.inbuf[ofs + i]
			END;
			DEC(len, SELF.currentReadState.aead.explicitLen);
			INC(ofs, SELF.currentReadState.aead.explicitLen);
			SELF.currentReadState.recordSeq.GetBytes(seq, 0);
			SELF.currentReadState.recordSeq.GetBytes(additionalData, 0);
			additionalData[8] := SHORT(CHR(type));
			additionalData[9] := SHORT(CHR(major));
			additionalData[10] := SHORT(CHR(minor));
			additionalData[11] := SHORT(CHR(len DIV 256));
			additionalData[12] := SHORT(CHR(len));
			IF ~SELF.currentReadState.aead.Open(SELF.inbuf, ofs, len,
					additionalData, LEN(additionalData), seq, explicit, tag,
					SELF.inbuf, ofs) THEN
				badRecordMac := TRUE
			END
		END;
		IF (SELF.currentReadState.mac # NIL) & (SELF.currentReadState.aead = NIL) THEN
			macSize := SELF.currentReadState.mac.size;
			IF (len < macSize) OR badRecordMac THEN
				badRecordMac := TRUE;
				macSize := 0
			ELSE
				DEC(len, macSize)
			END;
			IF ~badRecordMac THEN
				SELF.currentReadState.recordSeq.GetBytes(macInput, 0);
				macInput[8] := SHORT(CHR(type));
				macInput[9] := SHORT(CHR(major));
				macInput[10] := SHORT(CHR(minor));
				macInput[11] := SHORT(CHR(len DIV 256));
				macInput[12] := SHORT(CHR(len));
				SELF.currentReadState.mac.Initialize(SELF.currentReadState.macSecret, macSize);
				SELF.currentReadState.mac.Update(macInput, 0, 13);
				SELF.currentReadState.mac.Update(SELF.inbuf, ofs, len);
				SELF.currentReadState.mac.GetMac(verify, 0);
				IF ~U.EqualBytesCT(SELF.inbuf, verify, ofs + len, 0, macSize) THEN
					badRecordMac := TRUE
				END
			END
		END;
		IF badRecordMac THEN
			SELF.SendError(BadRecordMac);
			RETURN
		END;
		CASE type OF
			Handshake :
				SELF.ReceiveHandshake(SELF.inbuf, ofs, len)
		|       ApplicationData :
				SELF.appDataWriter.Bytes(SELF.inbuf, ofs, len);
				SELF.appDataWriter.Update
		|       Alert :
				IF len # 2 THEN
					SELF.SendError(DecodeError);
					RETURN
				END;
				SELF.ReceiveAlert(SELF.inbuf, ofs, len)
		|       ChangeCipherSpec :
				IF len # 1 THEN
					SELF.SendError(DecodeError);
					RETURN
				END;
				SELF.Transition(SELF.inbuf, ChangeCipherSpec, 0, ofs, len)
		ELSE
			SELF.SendError(UnexpectedMessage);
			RETURN
		END
		END ReceiveRecord;

	PROCEDURE (SELF: Connection) StartHandshake , NEW;

	BEGIN

		Streams.OpenReader(SELF.in, SELF.conn.receiver);
		Streams.OpenWriter(SELF.out, SELF.conn.sender);
		SELF.SetState((*WARN CONV CAST*)SHORT(ClientHandshake));
		SELF.handshakeState := GenerateClientHello;
		SELF.Transition(SELF.outbuf, Handshake, 0, 0, 0)
	END StartHandshake;

	PROCEDURE GetConnection* (): Connection;
		VAR
			con: Connection;


	BEGIN
		NEW(con); con.Init();
		RETURN con
	END GetConnection;

	PROCEDURE NewContext* (): Context;
		VAR
			ctx: Context;
	BEGIN
		NEW(ctx);
		ctx.Init(defaultCipherPolicy);
		RETURN ctx
	END NewContext;

	PROCEDURE GetServerConnection* (IN certificate, pHex, qHex, eHex : ARRAY OF SHORTCHAR; pLen, qLen, eLen : AO.SIGNED32): Connection;
		VAR
			con: Connection;
			ctx: Context;


	BEGIN
		NEW(ctx); ctx.Init(defaultCipherPolicy);
		IF ctx.LoadRsaCertificate(certificate) # Ok THEN
			RETURN NIL
		END;
		ctx.LoadRsaPrivateKey(pHex, qHex, eHex, SHORT(pLen), SHORT(qLen), SHORT(eLen));
		NEW(con);
		con.SetContext(ctx);
		RETURN con
	END GetServerConnection;

	PROCEDURE DefaultSuites* (VAR suites : ARRAY OF AO.SIGNED32; VAR n : AO.SIZE);
		VAR i : AO.SIZE;
	BEGIN
		n := defaultCipherPolicy.nofCipherSuites;
		IF n > LEN(suites) THEN n := LEN(suites) END;
		FOR i := 0 TO n - 1 DO
			suites[i] := defaultCipherPolicy.cipherSuites[i]
		END
	END DefaultSuites;

	PROCEDURE InitDefaults ;
		VAR
			defaultSuites: ARRAY DefaultSuitesNbr OF AO.SIGNED32;
	BEGIN
		(* TLS 1.2: только ECDHE и AEAD. CBC, 3DES, RC4, NULL и RSA key
		   transport остаются в коде, но включаются лишь явным
		   SetCipherSuites. *)
		defaultSuites[0] := TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384;
		defaultSuites[1] := TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384;
		defaultSuites[2] := TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256;
		defaultSuites[3] := TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256;
		defaultSuites[4] := TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256;
		defaultSuites[5] := TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
		NEW(defaultCipherPolicy);defaultCipherPolicy.Init();
		defaultCipherPolicy.SetCipherSuites(defaultSuites, DefaultSuitesNbr);
		defaultCipherPolicy.SetVersion(1, 2)
	END InitDefaults;

	PROCEDURE (SELF: Connection) Accept* (VAR client : TCP.Connection; VAR res : INTEGER);
		VAR
			newTLSConn: Connection;
			newTCPConn: TCP.Connection;


	BEGIN
		res := (-1);
		IF SELF.State() # TCP.Listen THEN
			RETURN
		END;
		IF SELF.conn = NIL THEN
			RETURN
		END;
		NEW(newTCPConn);
		newTCPConn.Init();
		SELF.conn.Accept(newTCPConn, res);
		IF res # TCP.Ok THEN
			res := (-1);
			RETURN
		END;
		NEW(newTLSConn);
		newTLSConn.Init();
		newTLSConn.SetContext(SELF.context);
		newTLSConn.SetConnection(newTCPConn);
		(* Рукопожатие сервера ведём до конца: клиент уже подключён. *)
		REPEAT
			newTLSConn.ReceiveRecord
		UNTIL (newTLSConn.State() = TCP.Established) OR (newTLSConn.State() = TCP.Closed);
		IF newTLSConn.State() # TCP.Established THEN
			res := (-1);
			RETURN
		END;
		client := newTLSConn;
		res := Ok
	END Accept;

	PROCEDURE (SELF: Connection) Open* (llocalAdr, remoteAdr: ARRAY OF CHAR;  VAR res : INTEGER);
			VAR cr : ConnectionReceiver;
					cs : ConnectionSender;
					i : INTEGER;
	BEGIN
		ASSERT(SELF.conn = NIL);
		IF SELF.context = NIL THEN
			SELF.SetDefaultPolicy
		END;
		SELF.client := (LEN(remoteAdr) > 0) & (remoteAdr[0] # 0X);
		NEW(SELF.conn);SELF.conn.Init();
		SELF.conn.Open(llocalAdr, remoteAdr, res);
		IF res # TCP.Ok THEN
						SELF.Close;
			RETURN
		END;
				SELF.remoteAdr := SELF.conn.remoteAdr;
				
				i := 0;
				WHILE (i < LEN(remoteAdr)) & (remoteAdr[i] # ':') DO
					SELF.serverName[i] := SHORT(remoteAdr[i]);
					INC(i)
				END;
				SELF.serverName[i] := 0X;
				StdLog.String("Server name:");StdLog.String(LONG(SELF.serverName));
				StdLog.Ln;
				
				NEW(cr);cr.conn := SELF; SELF.receiver := cr;
				NEW(cs);cs.conn := SELF; SELF.sender := cs;
		SELF.SetState(SELF.conn.state);
		res := Ok;
		IF SELF.client THEN
			SELF.Connect
		END;
		ASSERT(SELF.conn # NIL);
				IF SELF.client THEN
					SELF.StartHandshake;
					
					REPEAT
						SELF.ReceiveRecord
					UNTIL (SELF.State() = TCP.Established) OR (SELF.State() = TCP.Closed);
				ELSE
					(* TODO *)
					IF SELF.State() # TCP.Listen THEN
						(* listen for incoming messages *)
						REPEAT
							SELF.ReceiveRecord( )
						UNTIL (SELF.State() = TCP.Closed) OR (SELF.in.res # Streams.Ok);
						SELF.Close();
					END;
				END
	END Open;

		PROCEDURE (SELF: AsyncWriter) Do* () : BOOLEAN;
		BEGIN

			IF (SELF.conn.State() = TCP.Closed) OR (SELF.conn.in.res # Streams.Ok)THEN
				SELF.conn.Close;
				RETURN FALSE;
			ELSE
				SELF.conn.ReceiveRecord;
				RETURN TRUE;
			END
		END Do;
		
		PROCEDURE (SELF:SessionInvalidator) Do;
		BEGIN
			IF Trace THEN
				StdLog.String("TLS session.Invalidate"); StdLog.Ln;
			END;
			SELF.session.Invalidate;
		END Do;


BEGIN
	InitDefaults
END CryptoTLS.
(*
 .\openssl.exe s_client -cipher 'ECDHE-RSA-AES128-GCM-SHA256' -connect www.microsoft.com:443 -tls1_2


 .\openssl.exe req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
 .\openssl.exe s_server -key key.pem -cert cert.pem -accept 44330 -www -cipher 'ECDHE-RSA-AES128-GCM-SHA256' -tls1_2

https://www.ssllabs.com/ssltest/index.html

https://tls.dxdt.ru/tls.html#logic







*)
