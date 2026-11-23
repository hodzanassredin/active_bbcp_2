MODULE CheckWinFramework64;
	IMPORT S := SYSTEM, Kernel, WinKernel, Files, Files64, Fonts, Dates, Services, Dialog,
		Windows, Views, Controllers, TextModels, TextViews, TextControllers,
		StdWindows, StdConfig, StdGrids, StdTiles, StdCmds, StdRasters,
		WinGui, WinClipboard, WinDlg, StdApi, TextCmds, FormViews, FormModels, Controls, A := WinApi, CheckWinCFrames64;
	TYPE Test = POINTER TO RECORD (Services.Action) END;
	VAR action: Test; configured: BOOLEAN;
	PROCEDURE Fail (code: INTEGER);
		VAR message: ARRAY 64 OF CHAR;
	BEGIN
		message := "BlackBox framework test failed"; WinKernel.WkWrite(S.ADR(message), LEN(message$));
		Kernel.Quit(code)
	END Fail;
	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;
	PROCEDURE Loaded (IN name: ARRAY OF CHAR);
	BEGIN
		IF Kernel.ThisLoadedMod(name) = NIL THEN
			WinKernel.WkWrite(S.ADR(name), LEN(name$)); Fail(81)
		END
	END Loaded;
	PROCEDURE (a: Test) Do;
		VAR root, w, next: Windows.Window; text: TextModels.Model; view: TextViews.View;
			v: Views.View; before, result, width, height, count: INTEGER;
			argument: ARRAY 256 OF CHAR; opened: BOOLEAN; ignored: S.ADDRESS; ch: CHAR; message: ARRAY 64 OF CHAR; singleton: BOOLEAN;
			loc: Files.Locator; dirs: Files.LocInfo; list: Files.FileInfo;
			date: Dates.Date; time: Dates.Time; bias: INTEGER;
			form: Views.View; reader: FormModels.Reader; controls: INTEGER;
	BEGIN
		IF ~configured OR (Kernel.trapCount # 0) THEN Fail(82) END;
		IF Dialog.languageHook = NIL THEN Fail(183) END;
		Dialog.ResetLanguage;
		IF Files64.dir = NIL THEN Fail(182) END; Loaded("WinFiles64");
		Loaded("StdConfig");
		IF Kernel.ThisLoadedMod("DevDebug") = NIL THEN Loaded("StdDebug") END;
		Loaded("WinConfig"); Loaded("WinRegistry");
		Loaded("StdStdCFrames"); Loaded("WinDates"); Loaded("StdFilesBrowser"); Loaded("StdColorDialog");
		IF (StdCmds.clipboardHook = NIL) OR (StdRasters.New("png") = NIL) THEN Fail(83) END;
		IF (SIZE(A.MSG) # 48) OR (SIZE(A.WNDCLASSW) # 72)
			OR (SIZE(A.PAINTSTRUCT) # 72) OR (SIZE(A.CREATESTRUCTW) # 80)
			OR (SIZE(A.TEXTMETRICW) # 60) OR (SIZE(A.SYSTEMTIME) # 16)
			OR (SIZE(A.TIME_ZONE_INFORMATION) # 172) OR (SIZE(A.WIN32_FIND_DATAW) # 592)
			OR (SIZE(WinDlg.OPENFILENAMEW) # 152) THEN Fail(84) END;
		root := Windows.dir.First();
		WHILE (root # NIL) & ~(root.doc.ThisView() IS StdGrids.View) DO root := Windows.dir.Next(root) END;
		IF root = NIL THEN Fail(85) END;
		Dialog.GetPar("", argument, result);
		IF result = 0 THEN
			opened := FALSE; w := Windows.dir.First();
			WHILE w # NIL DO
				IF (w.name = "About") OR (w.name = "About.odc") THEN opened := TRUE END;
				w := Windows.dir.Next(w)
			END;
			IF ~opened THEN Fail(78) END
		END;
		loc := Files.dir.This("System/Rsrc"); list := Files.dir.FileList(loc);
		WHILE (list # NIL) & (list.name # "Menus.odc") DO list := list.next END;
		IF (list = NIL) OR (list.type # "odc") OR (loc.res # 0) THEN Fail(86) END;
		dirs := Files.dir.LocList(Files.dir.This(""));
		WHILE (dirs # NIL) & (dirs.name # "System") DO dirs := dirs.next END;
		IF dirs = NIL THEN Fail(87) END;
		Dates.GetDateTime(date, time); IF ~Dates.ValidDate(date) OR ~Dates.ValidTime(time) THEN Fail(88) END;
		Dates.GetUTCBias(bias);
		IF Fonts.dir.TypefaceList() = NIL THEN Fail(89) END;
		(* The normal tiled workspace is already verified. Add a native auxiliary
			window to exercise its Windows message boundary independently. *)
		StdTiles.Off;
		text := TextModels.dir.New(); view := TextViews.dir.New(text); Views.OpenAux(view, "AMD64 editor test");
		w := Windows.dir.First();
		WHILE (w # NIL) & (w.doc.ThisView() # view) DO w := Windows.dir.Next(w) END;
		IF (w = NIL) OR (WinGui.mainWnd = 0) THEN Fail(90) END;
		Windows.dir.Select(w, FALSE); TextControllers.SetCaret(text, 0); before := text.Length();
		ignored := A.SendMessageW(WinGui.mainWnd, A.WM_CHAR, ORD("Ж"), 1);
		StdWindows.Validate(NIL);
		IF text.Length() # before+1 THEN Fail(91) END;
		text.NewReader(NIL).ReadChar(ch); IF ch # "Ж" THEN Fail(92) END;
		StdCmds.Undo; IF text.Length() # before THEN Fail(93) END;
		StdCmds.Redo; IF text.Length() # before+1 THEN Fail(94) END;
		StdCmds.clipboardHook.Register(view, Views.undefined, Views.undefined, FALSE);
		IF ~WinClipboard.ConvertibleTo("TextViews.View") THEN Fail(95) END;
		WinClipboard.GetClipView("TextViews.View", v, width, height, singleton);
		IF (v = NIL) OR ~(v IS TextViews.View) OR (v(TextViews.View).ThisModel().Length() # text.Length()) THEN Fail(96) END;
		result := A.SetWindowPos(WinGui.mainWnd, 0, 0, 0, 720, 500, {1, 2, 4});
		IF result = 0 THEN Fail(97) END;
		result := A.UpdateWindow(WinGui.mainWnd); StdWindows.Validate(NIL);
		(* Exercise the same resource and initialization as Text -> Find/Replace. *)
		Dialog.GetPar("BB_NATIVE_CONTROLS", argument, result);
		IF (result = 0) & (argument = "1") THEN CheckWinCFrames64.Install END;
		TextCmds.InitFindDialog;
		StdApi.OpenToolDialog("Text/Rsrc/Cmds", "#Text:Find / Replace", form);
		IF (form = NIL) OR ~(form IS FormViews.View) THEN Fail(101) END;
		IF form(FormViews.View).ThisController() = NIL THEN Fail(102) END;
		reader := form(FormViews.View).ThisModel().NewReader(NIL); controls := 0;
		LOOP
			reader.ReadView(v); IF v = NIL THEN EXIT END;
			IF v IS Views.Alien THEN Fail(103) END;
			IF v IS Controls.Control THEN INC(controls) END
		END;
		IF controls < 5 THEN Fail(104) END;
		IF (result = 0) & (argument = "1") THEN CheckWinCFrames64.Verify END;
		StdWindows.Validate(NIL); result := A.UpdateWindow(WinGui.mainWnd);
		(* Explicitly discard only documents created by this test process. *)
		w := Windows.dir.First(); count := 0;
		WHILE w # NIL DO
			w.seq.SetDirty(FALSE); next := Windows.dir.Next(w); w := next; INC(count)
		END;
		StdCmds.Exit;
		IF (Windows.dir.First() # NIL) OR (Kernel.trapCount # 0) OR (count < 2) THEN Fail(98) END;
		message := "BlackBox x64 framework OK"; WinKernel.WkWrite(S.ADR(message), LEN(message$))
	END Do;
	PROCEDURE Verify*;
	BEGIN
		IF configured THEN Fail(99) END;
		configured := TRUE; Kernel.InstallTrapViewer(Trap);
		NEW(action); Services.DoLater(action, Services.Ticks()+10)
	END Verify;
	PROCEDURE Setup*;
	BEGIN
		(* Invoked via Meta's appConfigProcedure lookup in the unchanged WinInit.Init. *)
		IF configured THEN Fail(99) END;
		StdConfig.Setup; Verify
	END Setup;
END CheckWinFramework64.
