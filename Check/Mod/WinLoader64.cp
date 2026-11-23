MODULE CheckWinLoader64;
	IMPORT S := SYSTEM, Kernel, WinKernel, Files, WinFiles, StdLoader,
		N := WinKernelNtdll, State := CheckLoaderState;
	VAR cmd: Kernel.Command; traps: INTEGER;
	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text), LEN(text$)) END Say;
	PROCEDURE Fail (code: INTEGER);
		VAR res: INTEGER; importing, imported, object: Kernel.Name;
	BEGIN
		Say("WinLoader FAILED "); Kernel.GetLoaderResult(res, importing, imported, object);
		Say(importing); Say(" -> "); Say(imported); Say(" : "); Say(object); Kernel.Quit(code)
	END Fail;
	PROCEDURE Viewer;
	BEGIN INC(traps) END Viewer;
	PROCEDURE Invoke (a,b,c: S.ADDRESS);
	BEGIN cmd() END Invoke;
	PROCEDURE Reject (IN name: ARRAY OF CHAR);
		VAR m: Kernel.Module;
	BEGIN
		m := Kernel.ThisMod(name);
		IF (m # NIL) OR (StdLoader.res # Kernel.syntaxError) OR (Kernel.ThisLoadedMod(name) # NIL) THEN Fail(96) END
	END Reject;
	PROCEDURE Run;
		VAR m, dep: Kernel.Module; obj: Kernel.Object; pc, base, entry: S.ADDRESS; i: INTEGER;
	BEGIN
		Kernel.InstallTrapViewer(Viewer);
		Reject("CheckLoaderBroken"); Reject("CheckLoaderTruncated"); Reject("CheckLoaderArch");
		IF State.initialized # 0 THEN Fail(97) END;
		m := Kernel.ThisMod("CheckLoaderLegacy"); IF m = NIL THEN Fail(98) END;
		cmd := Kernel.ThisCommand(m, "Run"); IF cmd = NIL THEN Fail(99) END;
		cmd(); cmd := NIL; Kernel.UnloadMod(m);
		FOR i := 1 TO 2 DO
			m := Kernel.ThisMod("CheckLoaderPlugin"); IF m = NIL THEN Fail(81) END;
			IF State.initialized # 2*i THEN Fail(82) END;
			IF Kernel.ThisMod("CheckLoaderPlugin") # m THEN Fail(83) END;
			cmd := Kernel.ThisCommand(m, "Run"); IF cmd = NIL THEN Fail(84) END;
			cmd(); IF (State.runs # i) OR (State.callbacks # i) THEN Fail(85) END;
			obj := Kernel.ThisObject(m, "Callback"); IF obj = NIL THEN Fail(86) END;
			pc := m.procBase+obj.offs; entry := N.RtlLookupFunctionEntry(pc, base, 0);
			IF entry = 0 THEN Fail(87) END;
			cmd := Kernel.ThisCommand(m, "Fault"); Kernel.Try(Invoke, 0,0,0);
			IF traps # i THEN Fail(88) END;
			dep := Kernel.ThisLoadedMod("CheckLoaderDep"); IF dep = NIL THEN Fail(89) END;
			Kernel.UnloadMod(dep); IF Kernel.ThisLoadedMod("CheckLoaderDep") = NIL THEN Fail(90) END;
			cmd := NIL; Kernel.UnloadMod(m);
			IF Kernel.ThisLoadedMod("CheckLoaderPlugin") # NIL THEN Fail(91) END;
			IF N.RtlLookupFunctionEntry(pc, base, 0) # 0 THEN Fail(92) END;
			Kernel.UnloadMod(dep); IF State.terminated # 2*i THEN Fail(93) END;
			Kernel.Collect
		END;
		m := Kernel.ThisMod("CheckLoaderMissing"); IF m # NIL THEN Fail(94) END;
		IF StdLoader.res # Kernel.fileNotFound THEN Fail(95) END;
		Say("WinLoader x64 OK"); Kernel.Quit(0)
	END Run;
BEGIN Kernel.Start(Run)
END CheckWinLoader64.
