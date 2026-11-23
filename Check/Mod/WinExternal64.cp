MODULE CheckWinExternal64;

	IMPORT S := SYSTEM, Kernel, WinKernel, Services, Strings, Dialog, StdConfig, StdCmds, StdLog, Windows,
		WinDialog;

	TYPE Test = POINTER TO RECORD (Services.Action) END;

	VAR action: Test;

	PROCEDURE Fail (code: INTEGER);
		VAR text: ARRAY 64 OF CHAR;
	BEGIN
		Strings.IntToString(code,text); text := "WinExternal x64 FAILED " + text;
		WinKernel.WkWrite(S.ADR(text),LEN(text$));
		Strings.IntToString(WinDialog.lastExternalError,text); text := " / Windows error " + text; WinKernel.WkWrite(S.ADR(text),LEN(text$)); Kernel.Quit(code)
	END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;

	PROCEDURE (a: Test) Do;
		VAR helper, file, scheme: ARRAY 1024 OF CHAR; command, missing: ARRAY 2048 OF CHAR;
			res, length: INTEGER; win: Windows.Window;

	BEGIN
		Dialog.appName := "CheckWinExternal64";
		Dialog.GetPar("BB_EXT_HELPER",helper,res); IF res # 0 THEN Fail(61) END;
		Dialog.GetPar("BB_EXT_FILE",file,res); IF res # 0 THEN Fail(62) END;
		Dialog.GetPar("BB_EXT_SCHEME",scheme,res); IF res # 0 THEN Fail(63) END;
		Dialog.OpenExternal(file); IF WinDialog.lastExternalError # 0 THEN Fail(64) END;
		Dialog.OpenExternal(scheme+"://example/path?value=42"); IF WinDialog.lastExternalError # 0 THEN Fail(65) END;
		command := '"' + helper + '" --run "quoted value Ж"';
		Dialog.RunExternal(command); IF WinDialog.lastExternalError # 0 THEN Fail(66) END;
		WinDialog.hideExtRunWindow := TRUE;
		command := '"' + helper + '" --hidden'; Dialog.RunExternal(command);
		IF (WinDialog.lastExternalError # 0) OR WinDialog.hideExtRunWindow THEN Fail(67) END;
		WinDialog.hideExtRunWindow := FALSE;
		missing := file + ".missing";
		length := StdLog.text.Length(); Dialog.OpenExternal(missing);
		IF (WinDialog.lastExternalError = 0) OR (StdLog.text.Length() <= length) THEN Fail(68) END;
		length := StdLog.text.Length(); Dialog.RunExternal('"'+missing+'"');
		IF (WinDialog.lastExternalError = 0) OR (StdLog.text.Length() <= length) THEN Fail(69) END;
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
		StdCmds.Exit;
		command := "WinExternal x64 OK"; WinKernel.WkWrite(S.ADR(command),LEN(command$))
	END Do;

	PROCEDURE Setup*;
	BEGIN
		StdConfig.Setup; Kernel.InstallTrapViewer(Trap); NEW(action);
		Services.DoLater(action,Services.Ticks()+10)
	END Setup;
END CheckWinExternal64.
