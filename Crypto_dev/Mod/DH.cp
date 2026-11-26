MODULE Crypto_devDH;
(** Тесты обмена ключами Диффи — Хеллмана.

	Два участника выбирают общий простой модуль, обмениваются открытыми
	ключами и должны получить одинаковый секрет. *)

	IMPORT CryptoDiffieHellman, BN := CryptoAosBigNumbers, UnitAssert;

	PROCEDURE Do (IN pname: ARRAY OF SHORTCHAR; IN msg: ARRAY OF CHAR);
		VAR dh1, dh2: CryptoDiffieHellman.DH;
			pub1, pub2, k1, k2: BN.BigNumber;
	BEGIN
		NEW(dh1); dh1.Init(0, pname);
		NEW(dh2); dh2.Init(0, pname);
		pub1 := dh1.GenPubKey();
		pub2 := dh2.GenPubKey();
		k1 := dh1.ComputeKey(pub2);
		k2 := dh2.ComputeKey(pub1);
		UnitAssert.True(BN.Cmp(k1, k2) = 0, msg)
	END Do;

	PROCEDURE TestSSL192*;
	BEGIN
		Do("dh.ssl.192", "DH ssl 192")
	END TestSSL192;

	PROCEDURE TestSSL512*;
	BEGIN
		Do("dh.ssl.512", "DH ssl 512")
	END TestSSL512;

	PROCEDURE TestSSH*;
	BEGIN
		Do("dh.ssh.group1", "DH ssh group1")
	END TestSSH;

END Crypto_devDH.
