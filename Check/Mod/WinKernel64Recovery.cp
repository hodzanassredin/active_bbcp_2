MODULE CheckWinKernel64Recovery;
	IMPORT S := SYSTEM, Kernel, WinKernel;
	TYPE
		Node = POINTER TO RECORD value: INTEGER END;
		Tick = PROCEDURE [ms64] (): LONGINT;
	VAR rounds, traps, callbacks, reports: INTEGER;
	PROCEDURE Viewer;
	BEGIN INC(traps) END Viewer;
	PROCEDURE Quiet (IN text: ARRAY OF CHAR);
	BEGIN INC(reports) END Quiet;
	PROCEDURE QuietLn;
	BEGIN END QuietLn;
	PROCEDURE Fault (a,b,c: S.ADDRESS);
	BEGIN HALT(77) END Fault;
	PROCEDURE After (a,b,c: S.ADDRESS);
	BEGIN
		IF (a # 100000001L) OR (b # 200000002L) OR (c # 300000003L) THEN WinKernel.WkExit(41) END;
		INC(callbacks)
	END After;
	PROCEDURE Deep (a,b,c: S.ADDRESS);
		VAR space: ARRAY 20000 OF BYTE; real: REAL;
	BEGIN
		space[0] := 12; space[19999] := 34; real := 1.25;
		IF (real < 1.0) OR (space[0] # 12) OR (space[19999] # 34) THEN WinKernel.WkExit(42) END;
		Fault(0,0,0)
	END Deep;
	PROCEDURE BadCall (a,b,c: S.ADDRESS);
		VAR call: Kernel.TryHandler;
	BEGIN call := S.VAL(Kernel.TryHandler, LONG(1)); call(0,0,0) END BadCall;
	PROCEDURE BadWrite (a,b,c: S.ADDRESS);
	BEGIN S.PUT(1, 42) END BadWrite;
	PROCEDURE Run;
		VAR i, address: INTEGER; size, base: S.ADDRESS;
			dll, name: ARRAY 32 OF CHAR; stamp: ARRAY 6 OF SHORTINT;
			local: Node; tick: Tick; t: LONGINT; msg: ARRAY 64 OF CHAR;
	BEGIN
		INC(rounds);
		IF rounds = 1 THEN HALT(77) END; (* Start must resume with a fresh Run call. *)
		IF (rounds # 2) OR (traps # 1) THEN WinKernel.WkExit(43) END;
		FOR i := 1 TO 20 DO
			NEW(local); local.value := 700+i;
			Kernel.Try(Deep, 0,0,0);
			Kernel.Try(After, 100000001L, 200000002L, 300000003L);
			Kernel.Collect;
			IF local.value # 700+i THEN WinKernel.WkExit(44) END
		END;
		IF (callbacks # 20) OR (traps # 21) OR (reports = 0) THEN WinKernel.WkExit(45) END;
		dll := "kernel32.dll"; name := "GetTickCount64";
		address := WinKernel.WkResolve(S.ADR(dll), S.ADR(name));
		IF address = 0 THEN WinKernel.WkExit(46) END;
		IF WinKernel.WkResolve(S.ADR(dll), S.ADR(name)) # address THEN WinKernel.WkExit(47) END;
		tick := S.VAL(Tick, LONG(address)); t := tick();
		IF (t < 0) OR (WinKernel.WkTime() < t) THEN WinKernel.WkExit(48) END;
		WinKernel.WkLocalTime(S.ADR(stamp));
		IF (stamp[0] < 2020) OR (stamp[1] < 1) OR (stamp[1] > 12) OR (stamp[2] < 1) OR (stamp[2] > 31) THEN WinKernel.WkExit(49) END;
		size := WinKernel.WkPageSize(); base := WinKernel.WkAlloc(3*size);
		IF (base = 0) OR (base >= 70000000H) THEN WinKernel.WkExit(50) END;
		IF WinKernel.WkFree(base+1, size, 1) # 0 THEN WinKernel.WkExit(51) END;
		IF WinKernel.WkFree(base+size, size, 1) = 0 THEN WinKernel.WkExit(52) END;
		IF (WinKernel.WkReadable(base+size, base+size+1) # 0) OR (WinKernel.WkReadable(base, base+1) = 0) THEN WinKernel.WkExit(53) END;
		IF WinKernel.WkFree(base+size, size, 1) # 0 THEN WinKernel.WkExit(54) END;
		IF WinKernel.WkFree(base, size, 1) = 0 THEN WinKernel.WkExit(55) END;
		IF WinKernel.WkFree(base+2*size, size, 1) = 0 THEN WinKernel.WkExit(56) END;
		IF WinKernel.WkReadable(base, base+1) # 0 THEN WinKernel.WkExit(57) END;
		IF WinKernel.WkInstall() = 0 THEN WinKernel.WkExit(58) END; (* idempotent *)
		WinKernel.WkRemove;
		IF WinKernel.WkInstall() = 0 THEN WinKernel.WkExit(59) END;
		Kernel.Try(Fault, 0,0,0);
		IF traps # 22 THEN WinKernel.WkExit(60) END;
		Kernel.Try(BadCall, 0,0,0); Kernel.Try(BadWrite, 0,0,0);
		IF traps # 24 THEN WinKernel.WkExit(61) END;
		msg := "WinKernel native recovery OK"; WinKernel.WkWrite(S.ADR(msg), LEN(msg$))
	END Run;
BEGIN
	WinKernel.SetErr(Quiet, QuietLn); Kernel.InstallTrapViewer(Viewer); Kernel.Start(Run);
	IF (rounds # 2) OR (traps # 24) THEN WinKernel.WkExit(62) END; Kernel.Quit(0)
END CheckWinKernel64Recovery.
