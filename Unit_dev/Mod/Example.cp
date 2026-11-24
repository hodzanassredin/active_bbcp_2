MODULE Unit_devExample;

	IMPORT Strings, UnitAssert;

	VAR s: ARRAY 32 OF CHAR;

	PROCEDURE Setup*;
	BEGIN s := ""
	END Setup;

	PROCEDURE TestIntToStringNegative*;
	BEGIN
		Strings.IntToString(-42, s);
		UnitAssert.EqualStr("-42", s, "знак и цифры")
	END TestIntToStringNegative;

	PROCEDURE TestFindMissing*;
		VAR pos: INTEGER;
	BEGIN
		Strings.Find("abc", "x", 0, pos);
		UnitAssert.EqualInt(-1, pos, "нет образца")
	END TestFindMissing;

END Unit_devExample.
