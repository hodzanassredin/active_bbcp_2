MODULE CheckRealFormat64;

	IMPORT S := SYSTEM, Strings, Math;

	PROCEDURE Check (x: REAL; exponent: INTEGER; IN expected: ARRAY OF CHAR);
		VAR text: ARRAY 128 OF CHAR;
	BEGIN
		ASSERT(Math.Exponent(x) = exponent, 100);
		Strings.RealToString(x, text); ASSERT(text = expected, 101)
	END Check;

	PROCEDURE Value (high, low: INTEGER): REAL;
		VAR x: REAL;
	BEGIN
		S.PUT(S.ADR(x), low); S.PUT(S.ADR(x) + 4, high); RETURN x
	END Value;

	PROCEDURE Run*;
		VAR text: ARRAY 128 OF CHAR;
	BEGIN
		Check(10.56, 3, "10.56"); Check(21.0, 4, "21.0"); Check(29.7, 4, "29.7");
		Check(-10.56, 3, "-10.56"); Check(0.125, -3, "0.125"); Check(0, 0, "0.0");
		Check(Value(MIN(INTEGER), 0), 0, "0.0");
		ASSERT(Math.Exponent(Value(1, 0)) = -1042, 102);
		ASSERT(Math.Exponent(Value(0, 1)) = -1074, 103);
		ASSERT(Math.Exponent(Value(000FFFFFH, -1)) = -1023, 104);
		ASSERT(Math.Exponent(Value(00100000H, 0)) = -1022, 105);
		ASSERT(Math.Exponent(Value(7FEFFFFFH, -1)) = 1023, 106);
		ASSERT(Math.Exponent(Value(7FF00000H, 0)) = MAX(INTEGER), 107);
		Strings.RealToStringForm(10.56, 16, 0, -2, " ", text); ASSERT(text = "10.56", 109);
		Strings.RealToStringForm(-29.7, 16, 0, -2, " ", text); ASSERT(text = "-29.70", 110)
	END Run;

END CheckRealFormat64.
