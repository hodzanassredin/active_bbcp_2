MODULE ObsdIntDialog;

	IMPORT Dialog, Console, Env := ObsdEnv, Files, HostFiles := ObsdFiles;

	TYPE
		ShowHook = POINTER TO RECORD (Dialog.ShowHook) END;
		DialogGetParHook = POINTER TO RECORD (Dialog.GetParHook) END;
		HostLocHook = POINTER TO RECORD (Dialog.HostLocHook) END;

	PROCEDURE WriteParamMsg (IN str, p0, p1, p2: ARRAY OF CHAR);
	VAR msg: ARRAY 1024 OF CHAR; i: INTEGER; ch: CHAR;
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, msg);
		i := 0; ch := msg[0];
		WHILE ch # 0X DO
			IF ch = 0DX THEN Console.WriteLn
			ELSIF ch = 0EX THEN Console.WriteLn
			ELSIF ch = 09X THEN Console.WriteChar(09X)
			ELSIF ch >= " " THEN Console.WriteChar(ch)
			END;
			INC(i); ch := msg[i];
		END;
		Console.WriteLn
	END WriteParamMsg;

	PROCEDURE (h: ShowHook) ShowParamMsg (IN str, p0, p1, p2: ARRAY OF CHAR);
	BEGIN
		WriteParamMsg(str, p0, p1, p2)
	END ShowParamMsg;

	PROCEDURE (h: ShowHook) ShowParamStatus (IN str, p0, p1, p2: ARRAY OF CHAR);
	BEGIN
		IF Dialog.showsStatus THEN
			WriteParamMsg(str, p0, p1, p2)
		END
	END ShowParamStatus;

	PROCEDURE (hook: DialogGetParHook) GetPar (IN key: ARRAY OF CHAR;	
		OUT val: ARRAY OF CHAR; OUT res: INTEGER);
	BEGIN
		Env.GetPar(key, val, res)
	END GetPar;

	PROCEDURE (h: HostLocHook) GetLocPath (loc: Files.Locator; OUT path: ARRAY OF CHAR);
	BEGIN
		IF loc # NIL THEN
			WITH loc: HostFiles.Locator DO
				path := loc.path$; loc.res := 0
			ELSE
				path := ""; loc.res := 1 (* не получен *)
			END
		ELSE
			path := ""
		END
	END GetLocPath;

	PROCEDURE (h: HostLocHook) CheckHostLoc (loc: Files.Locator);
	BEGIN
		IF loc # NIL THEN
			IF loc IS HostFiles.Locator THEN loc.res := 0 ELSE loc.res := 1 END
		END
	END CheckHostLoc;

	PROCEDURE Init;
	VAR
		showHook: ShowHook;
		dialogGetParHook: DialogGetParHook;
		hostLocHook: HostLocHook;
		res: INTEGER;
	BEGIN
		Dialog.platform := Dialog.openbsd;
		Env.GetPar("PAR", Dialog.commandLinePars, res);
		NEW(showHook); Dialog.SetShowHook(showHook);
		NEW(dialogGetParHook); Dialog.SetGetParHook(dialogGetParHook);
		NEW(hostLocHook); Dialog.SetHostLocHook(hostLocHook);
	END Init;

BEGIN
	Init
END ObsdIntDialog.
