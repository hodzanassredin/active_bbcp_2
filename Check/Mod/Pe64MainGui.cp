MODULE CheckPe64MainGui;

	IMPORT SYSTEM, CheckPe64Api, CheckPe64User32, CheckPe64Dep;

	TYPE Derived = RECORD (CheckPe64Dep.Base) END;

	VAR p: POINTER TO Derived;
		a: POINTER TO ARRAY OF INTEGER;
		r: Derived; i, written, ok, answer: INTEGER; handle: SYSTEM.ADDRESS;
		message: ARRAY 32 OF SHORTCHAR;

BEGIN
	IF CheckPe64Dep.Twice(CheckPe64Dep.value) # 42 THEN CheckPe64Api.ExitProcess(41) END;
	IF r.Answer() # 42 THEN CheckPe64Api.ExitProcess(43) END;
	NEW(p); IF CheckPe64Dep.Dispatch(p^) # 42 THEN CheckPe64Api.ExitProcess(44) END;
	NEW(a, 7); a[6] := 42;
	IF (LEN(a) # 7) OR (a[6] # 42) THEN CheckPe64Api.ExitProcess(45) END;
	FOR i := 0 TO 11 DO
		IF CheckPe64Dep.Select(i) # i + 10 THEN CheckPe64Api.ExitProcess(46) END
	END;
	message := "DevLinker1 Windows x64 OK";
	handle := CheckPe64Api.GetStdHandle(-11);
	ok := CheckPe64Api.WriteFile(handle, SYSTEM.ADR(message), 25, written, 0);
	answer := CheckPe64User32.MessageBoxA(0, "Hershel's BlackBox", "Make it as simple as possible, but not simpler", 0);
	IF (ok = 0) OR (written # 25) THEN CheckPe64Api.ExitProcess(42) END
END CheckPe64MainGui.
