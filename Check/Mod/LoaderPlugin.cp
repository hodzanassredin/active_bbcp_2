MODULE CheckLoaderPlugin;
	IMPORT S := SYSTEM, Kernel, A := WinApi, N := WinKernelNtdll, Dep := CheckLoaderDep, State := CheckLoaderState;
	TYPE Derived = RECORD (Dep.Base) END;
	VAR object: POINTER TO Derived; array: POINTER TO ARRAY OF INTEGER; once: S.ADDRESS;
	PROCEDURE [ms64] Callback* (once, param, context: S.ADDRESS): INTEGER;
	BEGIN
		IF param # 100000004L THEN A.ExitProcess(71) END;
		S.PUT(context, param); INC(State.callbacks); RETURN 1
	END Callback;
	PROCEDURE Walk;
		VAR bytes: ARRAY 1248 OF BYTE; context, pc, entry, base, data, frame, handler: S.ADDRESS; i: INTEGER;
	BEGIN
		context := (S.ADR(bytes)+15) DIV 16*16; N.RtlCaptureContext(context);
		FOR i := 0 TO 1 DO
			S.GET(context+248, pc); entry := N.RtlLookupFunctionEntry(pc, base, 0);
			IF entry = 0 THEN A.ExitProcess(72) END;
			handler := N.RtlVirtualUnwind(0, base, pc, entry, context, data, frame, 0)
		END
	END Walk;
	PROCEDURE Run*;
		VAR context: S.ADDRESS; ok, i, sum: INTEGER;
	BEGIN
		NEW(object); object.value := Dep.Twice(Dep.number);
		NEW(array, 7); array[6] := 42; Kernel.Collect;
		IF (object.Answer() # 42) OR (array[6] # 42) THEN A.ExitProcess(73) END;
		sum := 0;
		FOR i := 0 TO 4 DO
			CASE i OF 0: INC(sum, 2) | 1: INC(sum, 4) | 2: INC(sum, 8) | 3: INC(sum, 16) | 4: INC(sum, 12) END
		END;
		IF sum # 42 THEN A.ExitProcess(74) END;
		once := 0; ok := A.InitOnceExecuteOnce(S.ADR(once), S.ADR(Callback), 100000004L, S.ADR(context));
		IF (ok = 0) OR (context # 100000004L) THEN A.ExitProcess(75) END;
		Walk; INC(State.runs)
	END Run;
	PROCEDURE Fault*;
	BEGIN HALT(77) END Fault;
BEGIN INC(State.initialized)
CLOSE INC(State.terminated)
END CheckLoaderPlugin.
