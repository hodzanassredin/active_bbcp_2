MODULE Crypto_devBigNumbers;
(** Тесты арифметики больших чисел.

	Сложение, вычитание, умножение и возведение в степень по модулю
	сверяются с эталонными значениями CryptoAosBigNumbers. *)

	IMPORT BN := CryptoAosBigNumbers, Streams := CryptoAosStreams, AO := CryptoAosCompat, UnitAssert;

	PROCEDURE Test1*;
		VAR b1, b2, b3, b4, result, want: BN.BigNumber;
	BEGIN
		BN.AssignHex(b1, "0a66791dc6988168de7ab77419bb7fb0", 32);
		BN.AssignHex(b2, "c001c62710270075142942e19a8d8c51", 32);
		BN.AssignHex(b3, "d053b3e3782a1de5dc5af4ebe9946817", 32);
		BN.AssignHex(b4, "3", 1);

		BN.AssignHex(want, "CA683F44D6BF81DDF2A3FA55B4490C01", 32);
		result := BN.Add(b1, b2);
		UnitAssert.True(result.EQ(want), "big number add");

		BN.AssignHex(want, "B59B4D09498E7F0C35AE8B6D80D20CA1", 32);
		want.Negate;
		result := BN.Sub(b1, b2);
		UnitAssert.True(result.EQ(want), "big number subtract");

		BN.AssignHex(want, "07CCED49A6019FFD6F318DA3F0C56B150E7EC8CE70E1F3EA8B7B1A93E217A6B0", 64);
		result := BN.Mul(b1, b2);
		UnitAssert.True(result.EQ(want), "big number multiply");

		BN.AssignHex(want, "2A5D449E77E846103A5471B0C07C279D", 32);
		result := BN.ModExp(b1, b4, b3);
		UnitAssert.True(result.EQ(want), "big number modular exponentiation")
	END Test1;

	PROCEDURE TestNet64*;
		VAR w: Streams.StringWriter; r: Streams.StringReader;
			buf: ARRAY 16 OF SHORTCHAR; len: AO.SIZE; x, y: AO.SIGNED64;
	BEGIN
		NEW(w); w.InitStringWriter(16);
		x := 123456789ABCDEF0L;
		w.Net64(x);
		len := 0; w.GetRaw(buf, len);
		UnitAssert.EqualInt(8, len, "Net64 writes 8 bytes");
		NEW(r); r.InitStringReader(len); r.SetRaw(buf, 0, len);
		y := r.Net64();
		UnitAssert.True(x = y, "Net64 roundtrip keeps all 64 bits");
		NEW(w); w.InitStringWriter(16);
		x := LONG(-1);
		(* старший бит: значение как SIGNED64 отрицательно, байты те же *)
		w.Net64(x);
		len := 0; w.GetRaw(buf, len);
		NEW(r); r.InitStringReader(len); r.SetRaw(buf, 0, len);
		UnitAssert.True(x = r.Net64(), "Net64 roundtrip with high bit")
	END TestNet64;

	PROCEDURE Test64*;
		(* 64-битные помощники: беззнаковые сравнения, перенос, заём и
		   сдвиги через границу 32 бит. *)
		VAR a, b: AO.UNSIGNED64;
	BEGIN
		UnitAssert.True(AO.ULss32(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(2)),
			"ULss32 same sign");
		UnitAssert.True(AO.ULss32(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(80000000H)),
			"ULss32 across sign bit");
		UnitAssert.True(~AO.ULss32(AO.TOUNSIGNED32(2), AO.TOUNSIGNED32(1)),
			"ULss32 reverse");
		UnitAssert.True(AO.ULss32(AO.TOUNSIGNED32(80000001H), AO.TOUNSIGNED32(80000002H)),
			"ULss32 both high bit");
		UnitAssert.True(~AO.ULss32(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(1)),
			"ULss32 is strict");
		UnitAssert.True(AO.UGtr32(AO.TOUNSIGNED32(2), AO.TOUNSIGNED32(1)), "UGtr32");
		UnitAssert.True(~AO.UGtr32(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(2)),
			"UGtr32 reverse");
		UnitAssert.True(AO.UGtr32(AO.TOUNSIGNED32(80000000H), AO.TOUNSIGNED32(1)),
			"UGtr32 across sign bit");

		a := AO.CDQ(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(0));
		b := AO.CDQ(AO.TOUNSIGNED32(2), AO.TOUNSIGNED32(0));
		UnitAssert.True(AO.ULss64(a, b), "ULss64 same high half");
		a := AO.CDQ(AO.TOUNSIGNED32(0FFFFFFFFH), AO.TOUNSIGNED32(0));
		b := AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(1));
		UnitAssert.True(AO.ULss64(a, b), "ULss64 carries into high half");
		a := AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(1));
		b := AO.CDQ(AO.TOUNSIGNED32(0FFFFFFFFH), AO.TOUNSIGNED32(0));
		UnitAssert.True(AO.UGtr64(a, b), "UGtr64 carries into high half");

		a := AO.CDQ(AO.TOUNSIGNED32(0FFFFFFFFH), AO.TOUNSIGNED32(0));
		b := AO.CDQ(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(0));
		UnitAssert.True(AO.UAdd64(a, b) = AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(1)),
			"UAdd64 carry into high half");
		a := AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(1));
		UnitAssert.True(AO.USub64(a, b) = AO.CDQ(AO.TOUNSIGNED32(0FFFFFFFFH), AO.TOUNSIGNED32(0)),
			"USub64 borrow from high half");

		a := AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(1));
		UnitAssert.True(AO.ULSH64(a, -32) = AO.CDQ(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(0)), "ULSH64 right through 32");
		UnitAssert.True(AO.ULSH64(AO.CDQ(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(0)), 33) = AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(2)),
			"ULSH64 left through 32");
		UnitAssert.True(AO.ULSH64(AO.CDQ(AO.TOUNSIGNED32(0), AO.TOUNSIGNED32(43776)), -40) = AO.CDQ(AO.TOUNSIGNED32(171), AO.TOUNSIGNED32(0)),
			"ULSH64 right by 40");

		a := AO.TOUNSIGNED64(100000000L);	(* 2^32 *)
		UnitAssert.True(AO.UDiv64(a, AO.CDQ(AO.TOUNSIGNED32(3), AO.TOUNSIGNED32(0))) = AO.CDQ(AO.TOUNSIGNED32(55555555H), AO.TOUNSIGNED32(0)),
			"UDiv64 general path");
		UnitAssert.True(AO.UMod64(a, AO.CDQ(AO.TOUNSIGNED32(3), AO.TOUNSIGNED32(0))) = AO.CDQ(AO.TOUNSIGNED32(1), AO.TOUNSIGNED32(0)),
			"UMod64 general path");
		a := AO.TOUNSIGNED64(123456789ABCDEF0L);
		UnitAssert.True(AO.UDiv64(a, AO.TOUNSIGNED64(100000000L)) = AO.CDQ(AO.TOUNSIGNED32(12345678H), AO.TOUNSIGNED32(0)),
			"UDiv64 by 2^32");
		UnitAssert.True(AO.UMod64(a, AO.TOUNSIGNED64(100000000L)) = AO.CDQ(AO.TOUNSIGNED32(9ABCDEF0H), AO.TOUNSIGNED32(0)),
			"UMod64 by 2^32")
	END Test64;

END Crypto_devBigNumbers.
