MODULE Crypto_devX509Modern;
(** X.509 с ECDSA, RSA-PSS и Ed25519: цепочки и отказы.

	Проверяются подписи ECDSA P-256/P-384, RSA-PSS и Ed25519, ключи
	EC/Ed25519 в SPKI и смешанная цепочка RSA-PSS -> EC -> Ed25519.
	Испорченное имя в TBS даёт ErrBadSignature, чужое имя — ErrHostname. *)

	IMPORT X509 := CryptoX509, Data := Crypto_devX509ModernData,
		EC := CryptoEC, UnitAssert;

	PROCEDURE Read (IN pem: ARRAY OF SHORTCHAR): X509.Certificate;
		VAR res: INTEGER;
	BEGIN
		RETURN X509.ReadPemChain(pem, res)
	END Read;

	PROCEDURE TestRsaPss*;
		VAR leaf, bad, roots: X509.Certificate; err: INTEGER;
	BEGIN
		leaf := Read(Data.RsaPssLeafPem);
		bad := Read(Data.RsaPssLeafTamperedPem);
		roots := Read(Data.RsaRootPem);
		UnitAssert.True(leaf # NIL, "RSA-PSS leaf parsed");
		UnitAssert.EqualInt(X509.KeyRsa, leaf.keyType, "RSA-PSS leaf has RSA key");
		UnitAssert.EqualInt(X509.SigRsaPss, leaf.sigType, "leaf signature is RSA-PSS");
		UnitAssert.True(X509.VerifyChain(leaf, leaf, roots, "pss.example", {}, err),
			"RSA-PSS chain verifies");
		UnitAssert.True(~X509.VerifyChain(leaf, leaf, roots, "other.example", {}, err),
			"RSA-PSS rejects wrong host");
		UnitAssert.EqualInt(X509.ErrHostname, err, "RSA-PSS wrong host error");
		UnitAssert.True(~X509.VerifyChain(bad, bad, roots, "pfs.example", {}, err),
			"tampered RSA-PSS leaf rejected");
		UnitAssert.EqualInt(X509.ErrBadSignature, err, "tampered PSS gives bad signature")
	END TestRsaPss;

	PROCEDURE TestEc256*;
		VAR leaf, bad, roots: X509.Certificate; err: INTEGER;
	BEGIN
		leaf := Read(Data.EcLeafPem);
		bad := Read(Data.EcLeafTamperedPem);
		roots := Read(Data.EcRootPem);
		UnitAssert.True((leaf # NIL) & (bad # NIL) & (roots # NIL), "EC certificates parsed");
		UnitAssert.EqualInt(X509.KeyEc, leaf.keyType, "EC leaf has EC key");
		UnitAssert.EqualInt(X509.SigEcdsa, leaf.sigType, "EC leaf signature is ECDSA");
		UnitAssert.EqualInt(32, 4 * leaf.ecCurve.n, "EC leaf uses P-256");
		UnitAssert.True(X509.VerifyChain(leaf, leaf, roots, "ec.example", {}, err),
			"ECDSA P-256 chain verifies");
		UnitAssert.True(~X509.VerifyChain(leaf, leaf, roots, "ecx.example", {}, err),
			"ECDSA rejects wrong host");
		UnitAssert.True(~X509.VerifyChain(bad, bad, roots, "ec.example", {}, err),
			"tampered ECDSA leaf rejected");
		UnitAssert.EqualInt(X509.ErrBadSignature, err, "tampered leaf gives bad signature")
	END TestEc256;

	PROCEDURE TestEc384*;
		VAR leaf, roots: X509.Certificate; err: INTEGER;
	BEGIN
		leaf := Read(Data.Ec384LeafPem);
		roots := Read(Data.Ec384RootPem);
		UnitAssert.True((leaf # NIL) & (roots # NIL), "P-384 certificates parsed");
		UnitAssert.EqualInt(48, 4 * leaf.ecCurve.n, "leaf uses P-384");
		UnitAssert.EqualInt(X509.SigEcdsa, leaf.sigType, "P-384 signature is ECDSA");
		UnitAssert.True(X509.VerifyChain(leaf, leaf, roots, "ec384.example", {}, err),
			"ECDSA P-384 chain verifies")
	END TestEc384;

	PROCEDURE TestEd25519*;
		VAR leaf, bad, roots: X509.Certificate; err: INTEGER;
	BEGIN
		leaf := Read(Data.EdLeafPem);
		bad := Read(Data.EdLeafTamperedPem);
		roots := Read(Data.EdRootPem);
		UnitAssert.True((leaf # NIL) & (roots # NIL), "Ed25519 certificates parsed");
		UnitAssert.EqualInt(X509.KeyEd25519, leaf.keyType, "leaf has Ed25519 key");
		UnitAssert.EqualInt(X509.SigEd25519, leaf.sigType, "leaf signature is Ed25519");
		UnitAssert.True(X509.VerifyChain(leaf, leaf, roots, "ed.example", {}, err),
			"Ed25519 chain verifies");
		UnitAssert.True(~X509.VerifyChain(leaf, leaf, roots, "edx.example", {}, err),
			"Ed25519 rejects wrong host");
		UnitAssert.True(~X509.VerifyChain(bad, bad, roots, "fd.example", {}, err),
			"tampered Ed25519 leaf rejected");
		UnitAssert.EqualInt(X509.ErrBadSignature, err, "tampered Ed25519 gives bad signature")
	END TestEd25519;

	PROCEDURE TestMixed*;
		VAR leaf, mid, roots: X509.Certificate; err: INTEGER;
	BEGIN
		leaf := Read(Data.MixedLeafPem);
		mid := Read(Data.MixedIntPem);
		roots := Read(Data.RsaRootPem);
		UnitAssert.True((leaf # NIL) & (mid # NIL) & (roots # NIL), "mixed chain parsed");
		UnitAssert.EqualInt(X509.KeyEd25519, leaf.keyType, "mixed leaf key is Ed25519");
		UnitAssert.EqualInt(X509.SigEcdsa, leaf.sigType, "mixed leaf signed by EC intermediate");
		UnitAssert.EqualInt(X509.KeyEc, mid.keyType, "intermediate key is EC");
		UnitAssert.EqualInt(X509.SigRsaPss, mid.sigType, "intermediate signed by RSA-PSS");
		UnitAssert.True(X509.VerifyChain(leaf, mid, roots, "mixed.example", {}, err),
			"RSA-PSS -> EC -> Ed25519 chain verifies");
		UnitAssert.True(~X509.VerifyChain(leaf, mid, roots, "mixedx.example", {}, err),
			"mixed chain rejects wrong host")
	END TestMixed;

END Crypto_devX509Modern.
