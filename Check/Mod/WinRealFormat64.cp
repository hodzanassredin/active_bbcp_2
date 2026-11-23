MODULE CheckWinRealFormat64;

	IMPORT S := SYSTEM, CheckRealFormat64, WinKernel, Kernel;

	VAR text: ARRAY 64 OF CHAR;

	PROCEDURE Run (a, b, c: S.ADDRESS);
	BEGIN CheckRealFormat64.Run END Run;

BEGIN
	Kernel.Try(Run, 0, 0, 0);
	IF Kernel.trapCount # 0 THEN Kernel.Quit(1) END;
	text := "REAL formatting OK"; WinKernel.WkWrite(S.ADR(text), LEN(text$)); Kernel.Quit(0)
END CheckWinRealFormat64.
