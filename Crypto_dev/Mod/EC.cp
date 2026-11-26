MODULE Crypto_devEC;
(** Тесты P-256/P-384: скалярное умножение, ECDH и отказ от чужих точек.

	Открытый ключ из закрытого сверяется с вектором NIST CAVP (P-256) и с
	независимо посчитанными значениями; общий секрет ECDH совпадает у двух
	сторон. Точки вне кривой, с координатами за модулем и неверным префиксом
	отвергаются. *)

	IMPORT CryptoEC, CryptoUtils, UnitAssert;

	PROCEDURE StrLen (IN s: ARRAY OF SHORTCHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO INC(i) END;
		RETURN i
	END StrLen;

	PROCEDURE HexTo (IN hex: ARRAY OF SHORTCHAR; VAR buf: ARRAY OF SHORTCHAR): INTEGER;
		VAR len: INTEGER;
	BEGIN
		len := StrLen(hex) DIV 2;
		CryptoUtils.Hex2Bin(hex, 0, buf, 0, len);
		RETURN len
	END HexTo;

	PROCEDURE Equal (IN a, b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < len) & (a[i] = b[i]) DO INC(i) END;
		RETURN i = len
	END Equal;

	PROCEDURE CheckBytes (IN got, hex: ARRAY OF SHORTCHAR; len: INTEGER; IN msg: ARRAY OF CHAR);
		VAR want: ARRAY 64 OF SHORTCHAR;
	BEGIN
		CryptoUtils.Hex2Bin(hex, 0, want, 0, len);
		UnitAssert.True(Equal(got, want, len), msg)
	END CheckBytes;

	PROCEDURE TestP256Base*;
		VAR c: CryptoEC.Curve; d, qx, qy: CryptoEC.BigInt;
			buf, got: ARRAY 64 OF SHORTCHAR;
			ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("7D7DC5F71EB29DDAF80D6214632EEAE03D9058AF1FB6D22ED80BADB62BC1A534", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, d), "P-256 private key parsed");
		CryptoEC.ScalarMulBase(c, d, qx, qy, ok);
		UnitAssert.True(ok, "P-256 scalar multiplication succeeds");
		CryptoEC.GetBytes(c, qx, got, 0);
		CheckBytes(got, "EAD218590119E8876B29146FF89CA61770C4EDBBF97D38CE385ED281D8A6B230",
			32, "P-256 NIST CAVP x");
		CryptoEC.GetBytes(c, qy, got, 0);
		CheckBytes(got, "28AF61281FD35E2FA7002523ACC85A429CB06EE6648325389F59EDFCE1405141",
			32, "P-256 NIST CAVP y")
	END TestP256Base;

	PROCEDURE TestP256ECDH*;
		VAR c: CryptoEC.Curve;
			da, db, qax, qay, qbx, qby, s1, s2: CryptoEC.BigInt;
			buf, got: ARRAY 64 OF SHORTCHAR;
			ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("5ECF19A73C37A843687335975B4CC046742039BEEB809AEBAF30F1FEE6C03CBC", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, da), "P-256 Alice key parsed");
		CryptoEC.ScalarMulBase(c, da, qax, qay, ok);
		UnitAssert.True(ok, "P-256 Alice public");
		len := HexTo("FD70776F4595D358C0921B94FF229B0A5A46B16DB64610EB90189C3AEFC1C7CA", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, db), "P-256 Bob key parsed");
		CryptoEC.ScalarMulBase(c, db, qbx, qby, ok);
		UnitAssert.True(ok, "P-256 Bob public");
		UnitAssert.True(CryptoEC.ECDH(c, da, qbx, qby, s1), "P-256 ECDH Alice");
		UnitAssert.True(CryptoEC.ECDH(c, db, qax, qay, s2), "P-256 ECDH Bob");
		CryptoEC.GetBytes(c, s1, got, 0);
		CheckBytes(got, "29AEA7D26AA53A0D3139927C3B0B197C7CD6A6D520FF0B95F03394F076AC3728",
			32, "P-256 shared secret");
		CryptoEC.GetBytes(c, s2, buf, 0);
		UnitAssert.True(Equal(got, buf, 32), "P-256 both sides agree")
	END TestP256ECDH;

	PROCEDURE TestP384ECDH*;
		VAR c: CryptoEC.Curve;
			da, db, qax, qay, qbx, qby, s1, s2: CryptoEC.BigInt;
			buf, got: ARRAY 64 OF SHORTCHAR;
			ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P384();
		len := HexTo("8C00DD1A8F9B8A2D732F7070D01B1990B232D3D139002228A821C4249F1303BBA7A0EF856B805835B44558FD15E2D593", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, da), "P-384 Alice key parsed");
		CryptoEC.ScalarMulBase(c, da, qax, qay, ok);
		UnitAssert.True(ok, "P-384 Alice public");
		CryptoEC.GetBytes(c, qax, got, 0);
		CheckBytes(got, "0399E0997D8CA1DD312329F0D341AEFC9CEE4FBB237DC289AEB314CBD47B2B72AD8FD1C850B13B2FA9FC00F8E9129B79",
			48, "P-384 public x");
		CryptoEC.GetBytes(c, qay, got, 0);
		CheckBytes(got, "ED1CD93A9DF870A4D9FD6F79E40DB184434F02D528848DE9B7C1251A07497DCD9DFFC058EB549BB1D20ABCEAB02E8E8B",
			48, "P-384 public y");
		len := HexTo("A24E3187553221517AA5DC2A4CC5AF9E564BD6AB464A8A6AFB2599572AE0655B798F2C96508CFE21635F5055BCAF065D", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, db), "P-384 Bob key parsed");
		CryptoEC.ScalarMulBase(c, db, qbx, qby, ok);
		UnitAssert.True(ok, "P-384 Bob public");
		UnitAssert.True(CryptoEC.ECDH(c, da, qbx, qby, s1), "P-384 ECDH Alice");
		UnitAssert.True(CryptoEC.ECDH(c, db, qax, qay, s2), "P-384 ECDH Bob");
		CryptoEC.GetBytes(c, s1, got, 0);
		CheckBytes(got, "A985C635840E1E1C5B4BC5AA74726BE9E16BA765FC9FBCB28BA325EF7E4DF5C2798490C5B21FDB326BBA820F93DAA11B",
			48, "P-384 shared secret");
		CryptoEC.GetBytes(c, s2, buf, 0);
		UnitAssert.True(Equal(got, buf, 48), "P-384 both sides agree")
	END TestP384ECDH;

	PROCEDURE TestReject*;
		VAR c: CryptoEC.Curve; x, y, s: CryptoEC.BigInt;
			buf, point: ARRAY 128 OF SHORTCHAR;
			ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000000", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "zero x parsed");
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, y), "zero y parsed");
		UnitAssert.True(~CryptoEC.Validate(c, x, y), "P-256 rejects (0,0)");
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000001", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, x);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, y);
		UnitAssert.True(~CryptoEC.Validate(c, x, y), "P-256 rejects point not on curve");
		len := HexTo("FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, x);
		UnitAssert.True(~CryptoEC.Validate(c, x, y), "P-256 rejects x = p");
		len := HexTo("6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296" +
			"4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5", point);
		UnitAssert.True(~CryptoEC.PointFromBytes(c, point, 64, x, y), "P-256 generator without prefix rejected");
		(* SEC1: 0x04 || G *)
		point[0] := 4X;
		CryptoEC.GetBytes(c, c.gx, point, 1);
		CryptoEC.GetBytes(c, c.gy, point, 33);
		UnitAssert.True(CryptoEC.PointFromBytes(c, point, 65, x, y), "P-256 generator parsed");
		point[0] := 3X;
		UnitAssert.True(~CryptoEC.PointFromBytes(c, point, 65, x, y), "P-256 compressed prefix rejected");
		(* Нулевой общий секрет: закрытый ключ вне диапазона. *)
		UnitAssert.True(~CryptoEC.ECDH(c, c.order, c.gx, c.gy, s), "P-256 rejects private key = order");
		len := HexTo("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632552", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "order + 1 parsed");
		UnitAssert.True(~CryptoEC.ECDH(c, x, c.gx, c.gy, s), "P-256 rejects private key = order + 1")
	END TestReject;

	PROCEDURE TestP256BaseSmall*;
		(* 1*G и 2*G сверяются с параметрами кривой. *)
		VAR c: CryptoEC.Curve; k, x, y: CryptoEC.BigInt;
			got, buf: ARRAY 64 OF SHORTCHAR; ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000001", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, k);
		CryptoEC.ScalarMulBase(c, k, x, y, ok);
		UnitAssert.True(ok, "1*G succeeds");
		CryptoEC.GetBytes(c, x, got, 0);
		CheckBytes(got, "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
			32, "1*G equals base point");
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000002", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, k);
		CryptoEC.ScalarMulBase(c, k, x, y, ok);
		UnitAssert.True(ok, "2*G succeeds");
		CryptoEC.GetBytes(c, x, got, 0);
		CheckBytes(got, "7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC47669978",
			32, "2*G x")
	END TestP256BaseSmall;

	PROCEDURE TestOrderArith*;
		VAR c: CryptoEC.Curve; a, b, p, q, r, one: CryptoEC.BigInt;
			got, buf: ARRAY 64 OF SHORTCHAR; ok: BOOLEAN; len: INTEGER;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000005", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, a);
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000007", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, b);
		CryptoEC.OrderMul(c, a, b, p);
		CryptoEC.OrderBytes(c, p, got, 0);
		CheckBytes(got, "0000000000000000000000000000000000000000000000000000000000000023",
			32, "5*7 = 35 mod q");
		CryptoEC.OrderInv(c, b, q);
		CryptoEC.OrderMul(c, p, q, p);
		CryptoEC.OrderBytes(c, p, got, 0);
		CheckBytes(got, "0000000000000000000000000000000000000000000000000000000000000005",
			32, "35 * 7^-1 = 5 mod q");
		(* q - 1 + 1 = 0 *)
		len := HexTo("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632550", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, r);
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000001", buf);
		ok := CryptoEC.OrderFromBytes(c, buf, 0, len, one);
		CryptoEC.OrderAdd(c, r, one, r);
		CryptoEC.OrderBytes(c, r, got, 0);
		CheckBytes(got, "0000000000000000000000000000000000000000000000000000000000000000",
			32, "(q-1)+1 = 0 mod q");
		UnitAssert.True(CryptoEC.OrderInRange(c, q), "7^-1 in range");
		UnitAssert.True(~CryptoEC.OrderInRange(c, c.order), "order itself is not a valid scalar")
	END TestOrderArith;

	PROCEDURE TestWycheproof*;
		(** ECDH P-256: выбранные случаи Wycheproof (ecdh_secp256r1).
			Годные общие секреты сверяются, точки вне кривой, (0,0) и
			пустая точка отвергаются. *)
		VAR c: CryptoEC.Curve; priv, x, y, sh: CryptoEC.BigInt;
			buf, got: ARRAY 64 OF SHORTCHAR; len: INTEGER; ok: BOOLEAN;
			empty: ARRAY 1 OF SHORTCHAR;
	BEGIN
		c := CryptoEC.P256();
		len := HexTo("0612465C89A023AB17855B0A6BCEBFD3FEBB53AEF84138647B5352E02C10C346", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "Wycheproof ECDH private parsed");
		len := HexTo("62D5BD3372AF75FE85A040715D0F502428E07046868B0BFDFA61D731AFE44F26", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "Wycheproof value parsed");
		len := HexTo("AC333A93A9E70A81CD5A95B5BF8D13990EB741C8C38872B4A07D275A014E30CF", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, y), "Wycheproof value parsed");
		UnitAssert.True(CryptoEC.ECDH(c, priv, x, y, sh), "Wycheproof ECDH normal case accepted");
		CryptoEC.GetBytes(c, sh, got, 0);
		CheckBytes(got, "53020D908B0219328B658B525F26780E3AE12BCD952BB25A93BC0895E1714285",
			32, "Wycheproof ECDH shared secret");

		(* Второй вектор: общий секрет 2^16. *)
		len := HexTo("0A0D622A47E48F6BC1038ACE438C6F528AA00AD2BD1DA5F13EE46BF5F633D71A", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, priv), "Wycheproof value parsed");
		len := HexTo("1B0E7437C33D379929430D3EC10DF59BED7FE2A1D950C5791E1E9DDEEF1F4D70", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "Wycheproof value parsed");
		len := HexTo("FBDB0E3BBCE63A27F27838C685207F2CCAF689D25EB622744DB1168AC92619E8", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, y), "Wycheproof value parsed");
		UnitAssert.True(CryptoEC.ECDH(c, priv, x, y, sh), "Wycheproof ECDH 2^16 case accepted");
		CryptoEC.GetBytes(c, sh, got, 0);
		CheckBytes(got, "0000000000000000000000000000000000000000000000000000000000010000",
			32, "Wycheproof ECDH 2^16 shared secret");

		(* Точка (0,0) и точка вне кривой отвергаются. *)
		len := HexTo("0000000000000000000000000000000000000000000000000000000000000000", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "Wycheproof value parsed");
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, y), "Wycheproof value parsed");
		UnitAssert.True(~CryptoEC.ECDH(c, priv, x, y, sh), "Wycheproof rejects (0,0)");
		len := HexTo("5496D929CD5407FFBB4511D623A153FB672A3842F13C31AA1C0279DC9812BCAF", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, x), "Wycheproof value parsed");
		len := HexTo("36E67549C03B05D587168BCEFCBFD67A77F5EA16DDB93CB86129E81BE63D647C", buf);
		UnitAssert.True(CryptoEC.OrderFromBytes(c, buf, 0, len, y), "Wycheproof value parsed");
		UnitAssert.True(~CryptoEC.ECDH(c, priv, x, y, sh), "Wycheproof rejects point of order 3");
		(* Пустая точка: PointFromBytes не принимает нулевую длину. *)
		UnitAssert.True(~CryptoEC.PointFromBytes(c, empty, 0, x, y), "Wycheproof rejects empty point")
	END TestWycheproof;

END Crypto_devEC.
