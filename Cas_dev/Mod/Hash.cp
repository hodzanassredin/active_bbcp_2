MODULE Cas_devHash;
(** Тесты CasHash: векторы NIST для SHA-256, шестнадцатеричная запись Id. *)

	IMPORT CasHash, UnitAssert;

	PROCEDURE Bytes (IN s: ARRAY OF CHAR; OUT b: ARRAY OF BYTE; OUT n: INTEGER);
	BEGIN
		n := 0;
		WHILE s[n] # 0X DO b[n] := SHORT(SHORT(ORD(s[n]))); INC(n) END
	END Bytes;

	PROCEDURE CheckStr (IN s, want: ARRAY OF CHAR);
		VAR b: ARRAY 256 OF BYTE; n: INTEGER; id: CasHash.Id; h: ARRAY 65 OF CHAR;
	BEGIN
		Bytes(s, b, n);
		CasHash.Of(b, 0, n, id);
		CasHash.ToHex(id, h);
		UnitAssert.EqualStr(want, h, s)
	END CheckStr;

	PROCEDURE TestEmpty*;
	BEGIN
		CheckStr("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
	END TestEmpty;

	PROCEDURE TestAbc*;
	BEGIN
		CheckStr("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
	END TestAbc;

	PROCEDURE TestLong448*;
	BEGIN
		CheckStr("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")
	END TestLong448;

	PROCEDURE TestLong896*;
	BEGIN
		CheckStr("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
			"cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1")
	END TestLong896;

	PROCEDURE TestMillionA*;
		VAR b: POINTER TO ARRAY OF BYTE; i: INTEGER; id: CasHash.Id; h: ARRAY 65 OF CHAR;
	BEGIN
		NEW(b, 1000000);
		FOR i := 0 TO LEN(b) - 1 DO b[i] := ORD("a") END;
		CasHash.Of(b, 0, LEN(b), id);
		CasHash.ToHex(id, h);
		UnitAssert.EqualStr("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", h, "миллион a")
	END TestMillionA;

	PROCEDURE TestOffset*;
	(* Of хеширует отрезок [beg, beg + len), а не начало массива. *)
		VAR b: ARRAY 8 OF BYTE; n: INTEGER; x, y: CasHash.Id;
	BEGIN
		Bytes("xxabcxx", b, n);
		CasHash.Of(b, 2, 3, x);
		Bytes("abc", b, n);
		CasHash.Of(b, 0, 3, y);
		UnitAssert.True(CasHash.Equal(x, y), "отрезок")
	END TestOffset;

	PROCEDURE TestHexRoundTrip*;
		VAR b: ARRAY 4 OF BYTE; n: INTEGER; x, y: CasHash.Id; h: ARRAY 65 OF CHAR; ok: BOOLEAN;
	BEGIN
		Bytes("abc", b, n);
		CasHash.Of(b, 0, n, x);
		CasHash.ToHex(x, h);
		CasHash.FromHex(h, y, ok);
		UnitAssert.True(ok, "разбор");
		UnitAssert.True(CasHash.Equal(x, y), "обратно");
		CasHash.FromHex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", y, ok);
		UnitAssert.True(ok & CasHash.Equal(x, y), "заглавные")
	END TestHexRoundTrip;

	PROCEDURE TestHexBad*;
		VAR y: CasHash.Id; ok: BOOLEAN;
	BEGIN
		CasHash.FromHex("abc", y, ok);
		UnitAssert.True(~ok, "короткая");
		CasHash.FromHex("zz7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", y, ok);
		UnitAssert.True(~ok, "не цифра");
		CasHash.FromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad0", y, ok);
		UnitAssert.True(~ok, "длинная")
	END TestHexBad;

	PROCEDURE TestNilAndEqual*;
		VAR b: ARRAY 4 OF BYTE; n: INTEGER; x, y, z: CasHash.Id;
	BEGIN
		CasHash.Clear(z);
		UnitAssert.True(CasHash.IsNil(z), "пустой");
		Bytes("abc", b, n); CasHash.Of(b, 0, n, x);
		Bytes("abd", b, n); CasHash.Of(b, 0, n, y);
		UnitAssert.True(~CasHash.IsNil(x), "хеш не пуст");
		UnitAssert.True(~CasHash.Equal(x, y), "разные");
		UnitAssert.True(CasHash.Equal(x, x), "тот же")
	END TestNilAndEqual;

END Cas_devHash.
