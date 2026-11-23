MODULE CheckMdiFramework64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, WinWindows, Windows,
		Views, TextModels, TextViews, TextControllers, StdCmds, Dialog,
		StdApi, FormViews, TextCmds, CheckWinCFrames64, Files, Converters, WinClipboard, MdiMenus;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
		VAR text: ARRAY 64 OF CHAR;
	BEGIN
		IF ~ok THEN
			text := "BlackBox MDI x64 check failed"; WinKernel.WkWrite(S.ADR(text), LEN(text$)); Kernel.Quit(code)
		END
	END Check;

	PROCEDURE Run*;
		VAR model: TextModels.Model; view: TextViews.View; w: Windows.Window;
			form, reopened, clip: Views.View; loc: Files.Locator; width, height: INTEGER; single: BOOLEAN; result: INTEGER; ignored: S.ADDRESS; text: ARRAY 64 OF CHAR; ch: CHAR;
	BEGIN
		Check(Kernel.ThisLoadedMod("WinBackends") = NIL, 101);
		Check(Dialog.isMDI & (Windows.dir IS WinWindows.Directory), 102);
		Check(Dialog.languageHook # NIL, 123);
		Dialog.ResetLanguage;
		Check((WinWindows.main # 0) & (WinWindows.client # 0), 103);
		Check(A.IsWindowVisible(WinWindows.main) # 0, 104);
		Check(A.GetMenu(WinWindows.main) = MdiMenus.menuBar, 113);
		Check(A.GetMenuItemCount(MdiMenus.menuBar) >= 4, 114);
		w := Windows.dir.First(); result := 0;
		WHILE w # NIL DO
			IF w.name = "MDI startup Ж.odc" THEN INC(result) END;
			w := Windows.dir.Next(w)
		END;
		Check(result = 1, 121);
		model := TextModels.dir.New(); view := TextViews.dir.New(model);
		Views.OpenAux(view, "MDI x64 edit test");
		w := Windows.dir.First();
		WHILE (w # NIL) & (w.doc.ThisView() # view) DO w := Windows.dir.Next(w) END;
		Check((w # NIL) & (w IS WinWindows.Window), 105);
		Windows.dir.Select(w, FALSE); TextControllers.SetCaret(model, 0);
		ignored := A.SendMessageW(w(WinWindows.Window).wnd, A.WM_CHAR, ORD("Ж"), 1);
		Check(model.Length() = 1, 106);
		model.NewReader(NIL).ReadChar(ch); Check(ch = "Ж", 107);
		StdCmds.Undo; Check(model.Length() = 0, 108);
		StdCmds.Redo; Check(model.Length() = 1, 109);
		loc := Files.dir.This("");
		Converters.Export(loc, "MDI проверка.odc", NIL, w.doc); Check(loc.res = 0, 115);
		reopened := Views.OldView(loc, "MDI проверка.odc");
		Check((reopened # NIL) & (reopened IS TextViews.View), 116);
		Check(reopened(TextViews.View).ThisModel().Length() = 1, 117);
		Files.dir.Delete(loc, "MDI проверка.odc"); Check(loc.res = 0, 118);
		StdCmds.clipboardHook.Register(view, Views.undefined, Views.undefined, FALSE);
		WinClipboard.GetClipView("TextViews.View", clip, width, height, single);
		Check((clip # NIL) & (clip IS TextViews.View), 119);
		Check(clip(TextViews.View).ThisModel().Length() = 1, 120);
		MdiMenus.TileHorizontal; MdiMenus.TileVertical;
		TextCmds.InitFindDialog;
		StdApi.OpenToolDialog("Text/Rsrc/Cmds", "#Text:Find / Replace", form);
		Check((form # NIL) & (form IS FormViews.View), 110);
		CheckWinCFrames64.Verify;
		Dialog.Call("CheckWinColor64.Run", "", result); Check(result = 0, 124);
		Dialog.GetPar("BB_PRINT_TEST", text, result);
		IF (result = 0) & (text = "1") THEN
			Dialog.Call("CheckWinPrinting64.Run", "", result); Check(result = 0, 122)
		END;
		w := Windows.dir.First();
		WHILE w # NIL DO w.seq.SetDirty(FALSE); w := Windows.dir.Next(w) END;
		Check(Kernel.trapCount = 0, 111);
		text := "BlackBox MDI x64 OK"; WinKernel.WkWrite(S.ADR(text), LEN(text$));
		ignored := A.SendMessageW(WinWindows.main, A.WM_CLOSE, 0, 0)
	END Run;

END CheckMdiFramework64.
