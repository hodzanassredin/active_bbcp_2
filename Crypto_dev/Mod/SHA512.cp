MODULE Crypto_devSHA512;
(** Тесты SHA-384 и SHA-512 по FIPS 180-4.

	Проверяются пустое сообщение, короткий и длинный вход, а также
	совпадение результата при одном и нескольких вызовах Update. *)

	IMPORT CryptoHashes, CryptoUtils, UnitAssert;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE Check (IN modname, data, hex: ARRAY OF SHORTCHAR; IN msg: ARRAY OF CHAR);
		VAR h: CryptoHashes.Hash; got, want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		h := CryptoHashes.NewHash(modname);
		UnitAssert.True(h # NIL, msg);
		IF h # NIL THEN
			h.Initialize;
			h.Update(data, 0, StrLen(data));
			h.GetHash(got, 0);
			CryptoUtils.Hex2Bin(hex, 0, want, 0, h.size);
			UnitAssert.True(Equal(got, want, h.size), msg)
		END
	END Check;

	PROCEDURE TestSHA384*;
	BEGIN
		Check("CryptoSHA384", "",
			"38B060A751AC96384CD9327EB1B1E36A21FDB71114BE07434C0CC7BF63F6E1DA" +
			"274EDEBFE76F65FBD51AD2F14898B95B", "SHA-384 empty");
		Check("CryptoSHA384", "abc",
			"CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED" +
			"8086072BA1E7CC2358BAECA134C825A7", "SHA-384 abc");
		Check("CryptoSHA384",
			"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
			"3391FDDDFC8DC7393707A65B1B4709397CF8B1D162AF05ABFE8F450DE5F36BC6" +
			"B0455A8520BC4E6F5FE95B1FE3C8452B", "SHA-384 two blocks")
	END TestSHA384;

	PROCEDURE TestSHA512*;
	BEGIN
		Check("CryptoSHA512", "",
			"CF83E1357EEFB8BDF1542850D66D8007D620E4050B5715DC83F4A921D36CE9CE" +
			"47D0D13C5D85F2B0FF8318D2877EEC2F63B931BD47417A81A538327AF927DA3E",
			"SHA-512 empty");
		Check("CryptoSHA512", "abc",
			"DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A" +
			"2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F",
			"SHA-512 abc");
		Check("CryptoSHA512",
			"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno" +
			"ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
			"8E959B75DAE313DA8CF4F72814FC143F8F7779C6EB9F7FA17299AEADB6889018" +
			"501D289E4900F7E4331B99DEC4B5433AC7D329EEB6DD26545E96E55B874BE909",
			"SHA-512 long input")
	END TestSHA512;

	PROCEDURE TestUpdateSplit*;
		VAR one, split: CryptoHashes.Hash; a, b: ARRAY 64 OF SHORTCHAR;
			data: ARRAY 200 OF SHORTCHAR; i: INTEGER;
	BEGIN
		FOR i := 0 TO 199 DO data[i] := SHORT(CHR(i MOD 251 + 1)) END;
		one := CryptoHashes.NewHash("CryptoSHA512");
		split := CryptoHashes.NewHash("CryptoSHA512");
		one.Initialize;
		one.Update(data, 0, 200);
		one.GetHash(a, 0);
		split.Initialize;
		split.Update(data, 0, 1);
		split.Update(data, 1, 127);
		split.Update(data, 128, 72);
		split.GetHash(b, 0);
		UnitAssert.True(Equal(a, b, 64), "SHA-512 one Update equals three Updates");

		one := CryptoHashes.NewHash("CryptoSHA384");
		split := CryptoHashes.NewHash("CryptoSHA384");
		one.Initialize;
		one.Update(data, 0, 200);
		one.GetHash(a, 0);
		split.Initialize;
		split.Update(data, 0, 63);
		split.Update(data, 63, 1);
		split.Update(data, 64, 136);
		split.GetHash(b, 0);
		UnitAssert.True(Equal(a, b, 48), "SHA-384 one Update equals three Updates")
	END TestUpdateSplit;

	PROCEDURE TestSize*;
		VAR h: CryptoHashes.Hash;
	BEGIN
		h := CryptoHashes.NewHash("CryptoSHA512");
		UnitAssert.EqualInt(64, h.size, "SHA-512 size is 64 bytes");
		UnitAssert.EqualInt(128, h.blockSize, "SHA-512 block size is 128 bytes");
		h := CryptoHashes.NewHash("CryptoSHA384");
		UnitAssert.EqualInt(48, h.size, "SHA-384 size is 48 bytes");
		UnitAssert.EqualInt(128, h.blockSize, "SHA-384 block size is 128 bytes")
	END TestSize;

END Crypto_devSHA512.
