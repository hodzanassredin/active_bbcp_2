MODULE CheckWinMath64;

	IMPORT S := SYSTEM, CheckMath64, CheckRealFormat64, Strings, WinKernel, Kernel;

	VAR text: ARRAY 128 OF CHAR; i: INTEGER;

	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text), LEN(text$)) END Say;

	PROCEDURE Run (a, b, c: S.ADDRESS);
	BEGIN CheckMath64.Run; CheckRealFormat64.Run END Run;

BEGIN
	Kernel.Try(Run, 0, 0, 0);
	IF Kernel.trapCount # 0 THEN Kernel.Quit(1) END;
	Strings.IntToString(CheckMath64.count, text); Say(text); Say(" Math checks; failures: ");
	Strings.IntToString(CheckMath64.failures, text); Say(text); Say(" ");
	IF CheckMath64.failures # 0 THEN
		FOR i := 0 TO MIN(CheckMath64.failures, LEN(CheckMath64.errors))-1 DO
			Say(CheckMath64.errors[i].name); Say(" actual="); Strings.RealToString(CheckMath64.errors[i].actual, text); Say(text);
			Say(" expected="); Strings.RealToString(CheckMath64.errors[i].expected, text); Say(text); Say("; ")
		END;
		Kernel.Quit(1)
	END;
	Say("Math x64 OK"); Kernel.Quit(0)
END CheckWinMath64.
