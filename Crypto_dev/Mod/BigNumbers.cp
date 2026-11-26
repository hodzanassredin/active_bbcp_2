MODULE Crypto_devBigNumbers;
(** Тесты арифметики больших чисел.

	Сложение, вычитание, умножение и возведение в степень по модулю
	сверяются с эталонными значениями CryptoAosBigNumbers. *)

	IMPORT BN := CryptoAosBigNumbers, UnitAssert;

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

END Crypto_devBigNumbers.
