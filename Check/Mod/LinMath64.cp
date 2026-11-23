MODULE CheckLinMath64;

	IMPORT CheckMath64, CheckRealFormat64, Kernel, Console, Strings;

	PROCEDURE Run*;
		VAR text: ARRAY 128 OF CHAR; i: INTEGER;
	BEGIN
		CheckMath64.Run; CheckRealFormat64.Run;
		Strings.IntToString(CheckMath64.count, text); Console.WriteStr(text);
		Console.WriteStr(" Math checks; failures: ");
		Strings.IntToString(CheckMath64.failures, text); Console.WriteStr(text);
		FOR i := 0 TO MIN(CheckMath64.failures, LEN(CheckMath64.errors))-1 DO
			Console.WriteStr(CheckMath64.errors[i].name); Console.WriteStr(" actual=");
			Strings.RealToString(CheckMath64.errors[i].actual, text); Console.WriteStr(text);
			Console.WriteStr(" expected="); Strings.RealToString(CheckMath64.errors[i].expected, text);
			Console.WriteStr(text); Console.WriteLn
		END;
		IF CheckMath64.failures # 0 THEN Kernel.Quit(1) END;
		Console.WriteStr(" Math x64 OK"); Console.WriteLn; Kernel.Quit(0)
	END Run;

END CheckLinMath64.
