MODULE Crypto_devX25519;
(** Тесты X25519 по RFC 7748 и векторам порта.

	Проверяются генерация пары, совпадение общего секрета, вывод открытого
	ключа из закрытого и два известных набора ключей. *)

	IMPORT CryptoX25519, CryptoBase64, CryptoUtils, UnitAssert;

	PROCEDURE NonZero (IN a: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = 0X) DO INC(i) END;
		RETURN i < len
	END NonZero;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE TestGenerate*;
		VAR key: CryptoX25519.X25519KeyPair;
	BEGIN
		CryptoX25519.GenerateKeyPair(key);
		UnitAssert.True(NonZero(key.PrivateKey, 32), "X25519 private key is generated");
		UnitAssert.True(NonZero(key.PublicKey, 32), "X25519 public key is generated")
	END TestGenerate;

	PROCEDURE TestAgreement*;
		VAR k1, k2: CryptoX25519.X25519KeyPair; s1, s2: ARRAY 32 OF SHORTCHAR;
	BEGIN
		CryptoX25519.GenerateKeyPair(k1);
		CryptoX25519.GenerateKeyPair(k2);
		CryptoX25519.Agreement(k1.PrivateKey, k2.PublicKey, s1);
		CryptoX25519.Agreement(k2.PrivateKey, k1.PublicKey, s2);
		UnitAssert.True(Equal(s1, s2, 32), "X25519 shared secrets agree")
	END TestAgreement;

	PROCEDURE TestGenerateFromPrivate*;
		VAR key, derived: CryptoX25519.X25519KeyPair;
	BEGIN
		CryptoX25519.GenerateKeyPair(key);
		CryptoX25519.GenerateKeyFromPrivateKey(key.PrivateKey, derived);
		UnitAssert.True(Equal(key.PublicKey, derived.PublicKey, 32),
			"X25519 public key is derived from private key")
	END TestGenerateFromPrivate;

	PROCEDURE TestKnownSecrets*;
		CONST secret = "dbfEcOMjYactMkh33DRhg0h1VCbmhxoWt6AR3rp6000=";
		VAR k1, k2: CryptoX25519.X25519KeyPair;
			priv1, priv2, shared1, shared2: ARRAY 32 OF SHORTCHAR;
			secret1, secret2: ARRAY 256 OF SHORTCHAR;
			len: INTEGER;
	BEGIN
		len := CryptoBase64.Decode("sECe8YYQT/bODurKruM8QpGFBTahurW8GqxFL+AYiW8=", priv1);
		UnitAssert.EqualInt(32, len, "X25519 first private key decoded");
		len := CryptoBase64.Decode("wAidyKs9iF+KA1cgBxa1rMtPwemOLFHqSIe5nkVRN2o=", priv2);
		UnitAssert.EqualInt(32, len, "X25519 second private key decoded");
		CryptoX25519.GenerateKeyFromPrivateKey(priv1, k1);
		CryptoX25519.GenerateKeyFromPrivateKey(priv2, k2);
		CryptoX25519.Agreement(k1.PrivateKey, k2.PublicKey, shared1);
		CryptoX25519.Agreement(k2.PrivateKey, k1.PublicKey, shared2);
		UnitAssert.True(Equal(shared1, shared2, 32), "X25519 known keys agree");
		CryptoBase64.Encode(shared1, 32, secret1);
		CryptoBase64.Encode(shared2, 32, secret2);
		UnitAssert.True(secret1 = secret2, "X25519 known secret encodings agree");
		UnitAssert.True(secret1 = secret, "X25519 known shared secret")
	END TestKnownSecrets;

	PROCEDURE TestRFC7748*;
		VAR apriv, apub, bpriv, bpub, shared, want: ARRAY 32 OF SHORTCHAR;
			ka, kb: CryptoX25519.X25519KeyPair; got: ARRAY 32 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin("77076D0A7318A57D3C16C17251B26645DF4C2F87EBC0992AB177FBA51DB92C2A", 0, apriv, 0, 32);
		CryptoUtils.Hex2Bin("8520F0098930A754748B7DDCB43EF75A0DBF3A0D26381AF4EBA4A98EAA9B4E6A", 0, apub, 0, 32);
		CryptoUtils.Hex2Bin("5DAB087E624A8A4B79E17F8B83800EE66F3BB1292618B6FD1C2F8B27FF88E0EB", 0, bpriv, 0, 32);
		CryptoUtils.Hex2Bin("DE9EDB7D7B7DC1B4D35B61C2ECE435373F8343C85B78674DADFC7E146F882B4F", 0, bpub, 0, 32);
		CryptoUtils.Hex2Bin("4A5D9D5BA4CE2DE1728E3BF480350F25E07E21C947D19E3376F09B3C1E161742", 0, want, 0, 32);
		CryptoX25519.GenerateKeyFromPrivateKey(apriv, ka);
		CryptoX25519.GenerateKeyFromPrivateKey(bpriv, kb);
		UnitAssert.True(Equal(ka.PublicKey, apub, 32), "RFC 7748 Alice public");
		UnitAssert.True(Equal(kb.PublicKey, bpub, 32), "RFC 7748 Bob public");
		CryptoX25519.Agreement(apriv, bpub, got);
		UnitAssert.True(Equal(got, want, 32), "RFC 7748 shared secret")
	END TestRFC7748;

	PROCEDURE TestKnownPublic*;
		VAR key: CryptoX25519.X25519KeyPair;
			priv, pub: ARRAY 32 OF SHORTCHAR;
			len: INTEGER;
	BEGIN
		len := CryptoBase64.Decode("aF9hmPSeJfKvjPam++gl7MRIQydQQu2Jdee8zOTX+lY=", priv);
		UnitAssert.EqualInt(32, len, "X25519 private key decoded");
		len := CryptoBase64.Decode("FTM52WXsEjj5hBY53RTUFmG2qUwzZxPRJdYs9lu/y3M=", pub);
		UnitAssert.EqualInt(32, len, "X25519 public key decoded");
		CryptoX25519.GenerateKeyFromPrivateKey(priv, key);
		UnitAssert.True(Equal(key.PublicKey, pub, 32), "X25519 known public key")
	END TestKnownPublic;

END Crypto_devX25519.
