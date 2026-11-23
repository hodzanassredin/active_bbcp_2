MODULE CheckWinProfiler64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, DevProfiler, DevProfiler__Win;

	VAR sink: INTEGER;

	PROCEDURE Work;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 100000 DO sink := (sink+i) MOD 100003 END
	END Work;

	PROCEDURE Run*;
		VAR start, inside: LONGINT; text: ARRAY 80 OF CHAR;
			context: A.CONTEXT;
	BEGIN
		ASSERT(SIZE(A.CONTEXT) = 1232, 101);
		ASSERT(S.ADR(context.ContextFlags)-S.ADR(context) = 48, 102);
		ASSERT(S.ADR(context.Rsp)-S.ADR(context) = 152, 103);
		ASSERT(S.ADR(context.Rip)-S.ADR(context) = 248, 104);
		ASSERT(S.ADR(context.VectorRegister)-S.ADR(context) = 768, 105);
		DevProfiler.SetModuleList("CheckWinProfiler64");
		DevProfiler.Start; start := A.GetTickCount64();
		REPEAT Work UNTIL A.GetTickCount64()-start >= 500;
		DevProfiler.Stop;
		inside := DevProfiler__Win.GetSampleCount();
		ASSERT(inside > 0, 106);
		(* Repeated start/stop must release timer callbacks and thread handles. *)
		DevProfiler.Start; start := A.GetTickCount64();
		REPEAT Work UNTIL A.GetTickCount64()-start >= 200;
		DevProfiler.Stop;
		inside := DevProfiler__Win.GetSampleCount(); ASSERT(inside > 0, 107);
		text := "Win profiler x64 CONTEXT and timer sampling OK";
		WinKernel.WkWrite(S.ADR(text), LEN(text$)); Kernel.Quit(0)
	END Run;

END CheckWinProfiler64.
