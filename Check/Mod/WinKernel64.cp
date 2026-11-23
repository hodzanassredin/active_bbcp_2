MODULE CheckWinKernel64;
	IMPORT S := SYSTEM, Kernel, WinKernel;
	TYPE Node = POINTER TO RECORD value: INTEGER; next: Node END;
	VAR root: Node; a: POINTER TO ARRAY OF INTEGER;
		calls, traps: INTEGER; ok: BOOLEAN; adr: INTEGER;
	PROCEDURE Callback (x, y, z: S.ADDRESS);
	BEGIN
		IF (x # 100000001L) OR (y # 22) OR (z # 33) THEN WinKernel.WkExit(21) END;
		INC(calls)
	END Callback;
	PROCEDURE TrapViewer;
	BEGIN INC(traps) END TrapViewer;
	PROCEDURE Fault (x, y, z: S.ADDRESS);
	BEGIN HALT(77) END Fault;
	PROCEDURE Nested (x, y, z: S.ADDRESS);
	BEGIN
		Kernel.Try(Fault, 0, 0, 0);
		Kernel.Try(Callback, 100000001L, 22, 33)
	END Nested;
	PROCEDURE BadRead (x, y, z: S.ADDRESS);
		VAR n: INTEGER;
	BEGIN S.GET(1, n); calls := n END BadRead;

	PROCEDURE Run;
		VAR p: Node; i, d, m, c, v: INTEGER; t, firstPage, secondPage: LONGINT; msg: ARRAY 32 OF CHAR;
	BEGIN
		firstPage := WinKernel.WkAlloc(4096); secondPage := WinKernel.WkAlloc(4096);
		IF (firstPage = 0) OR (secondPage = 0) OR (firstPage = secondPage) THEN WinKernel.WkExit(38) END;
		IF (WinKernel.WkFree(firstPage, 0, 1) = 0) OR (WinKernel.WkFree(secondPage, 0, 1) = 0) THEN WinKernel.WkExit(39) END;
		NEW(root); IF S.VAL(S.ADDRESS,root) < 100000000L THEN WinKernel.WkExit(37) END; root.value := 123;
		FOR i := 1 TO 2000 DO NEW(p); p.value := i; p.next := root; root := p END;
		NEW(a, 31); a[30] := 456;
		Kernel.Collect;
		IF (root.value # 2000) OR (a[30] # 456) OR (LEN(a) # 31) THEN WinKernel.WkExit(22) END;
		IF ~Kernel.IsReadable(S.ADR(root.value), S.ADR(root.value) + 4) THEN WinKernel.WkExit(23) END;
		IF Kernel.IsReadable(1, 8) THEN WinKernel.WkExit(24) END;
		IF ~Kernel.AllocModMem(64, 64, 64, 64, d, m, c, v) THEN WinKernel.WkExit(31) END;
		S.PUT(d, 123); S.PUT(v, 456);
		IF ~Kernel.ProtectModMem(64, 64, m, c) THEN WinKernel.WkExit(32) END;
		Kernel.DeallocModMem(64, 64, 64, 64, d, m, c, v);
		IF Kernel.IsReadable(d, d+4) THEN WinKernel.WkExit(33) END;
		t := Kernel.Time(); IF Kernel.Time() < t THEN WinKernel.WkExit(25) END;
		Kernel.LoadDll("kernel32.dll", ok); IF ~ok THEN WinKernel.WkExit(26) END;
		adr := Kernel.ThisDllObj(4, 0, "kernel32.dll", "GetTickCount64"); IF adr = 0 THEN WinKernel.WkExit(27) END;
		Kernel.Try(Callback, 100000001L, 22, 33);
		IF calls # 1 THEN WinKernel.WkExit(28) END;
		Kernel.InstallTrapViewer(TrapViewer);
		Kernel.Try(Fault, 0, 0, 0);
		IF traps # 1 THEN WinKernel.WkExit(29) END;
		Kernel.Try(Callback, 100000001L, 22, 33);
		IF calls # 2 THEN WinKernel.WkExit(30) END;
		Kernel.Try(Nested, 0, 0, 0);
		IF (calls # 3) OR (traps # 2) THEN WinKernel.WkExit(34) END;
		Kernel.Try(BadRead, 0, 0, 0);
		IF traps # 3 THEN WinKernel.WkExit(35) END;
		Kernel.Collect;
		IF (root.value # 2000) OR (a[30] # 456) THEN WinKernel.WkExit(36) END;
		msg := "WinKernel x64 OK"; WinKernel.WkWrite(S.ADR(msg), LEN(msg$));
		Kernel.Quit(0)
	END Run;
BEGIN Kernel.Start(Run)
END CheckWinKernel64.
