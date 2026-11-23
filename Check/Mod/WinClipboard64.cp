MODULE CheckWinClipboard64;

	IMPORT S := SYSTEM, Kernel, WinKernel, Services, Dialog, StdConfig, StdCmds, StdWindows,
		Views, Windows, TextModels, TextViews, TextControllers, Fonts, Ports, WinClipboard, WinGui, A := WinApi;

	TYPE Test = POINTER TO RECORD (Services.Action) END;
	VAR action: Test;

	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text), LEN(text$)) END Say;

	PROCEDURE Fail (n: INTEGER);
	BEGIN Say("WinClipboard x64 FAILED"); Kernel.Quit(n) END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;

	PROCEDURE SelectEditor (view: TextViews.View);
		VAR win: Windows.Window;
	BEGIN
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(75) END;
		Windows.dir.Select(win, FALSE); win(StdWindows.Window).backend.Select
	END SelectEditor;

	PROCEDURE PasteAndUndo (source: Views.View);
		VAR t: TextModels.Model; editor: TextViews.View; rd: TextModels.Reader; v: Views.View; n: INTEGER;
	BEGIN
		t := TextModels.dir.New(); editor := TextViews.dir.New(t); Views.OpenView(editor);
		SelectEditor(editor); TextControllers.SetCaret(t, 0); StdCmds.Paste; StdWindows.Validate(NIL);
		IF source IS TextViews.View THEN n := source(TextViews.View).ThisModel().Length()
		ELSE n := 1
		END;
		IF t.Length() # n THEN Fail(76) END;
		IF ~(source IS TextViews.View) THEN
			rd := t.NewReader(NIL); rd.ReadView(v); IF (v = NIL) OR ~Services.Is(v, "WinPictures.StdView") THEN Fail(77) END
		END;
		StdCmds.Undo; IF t.Length() # 0 THEN Fail(78) END;
		StdCmds.Redo; IF t.Length() # n THEN Fail(79) END
	END PasteAndUndo;

	PROCEDURE (a: Test) Do;
		VAR mode: ARRAY 32 OF CHAR; expected: ARRAY 128 OF CHAR; v, copy: Views.View;
			t: TextModels.Model; rd: TextModels.Reader; byte: CHAR;
			w, h, res, i: INTEGER; single: BOOLEAN; win, next: Windows.Window;
			ignored: S.ADDRESS;
	BEGIN
		Dialog.GetPar("BB_CLIP_MODE", mode, res); IF res # 0 THEN Fail(61) END;
		IF ~WinClipboard.ConvertibleTo("") THEN Fail(62) END;
		WinClipboard.GetClipView("", v, w, h, single); IF v = NIL THEN Fail(63) END;
		PasteAndUndo(v);
		IF mode = "picture" THEN
			IF ~Services.Is(v, "WinPictures.StdView") OR (w <= 0) OR (h <= 0) THEN Fail(64) END;
			Views.OpenAux(v, "External metafile"); StdWindows.Validate(NIL);
			res := A.UpdateWindow(WinGui.mainWnd);
			StdCmds.clipboardHook.Register(v, w, h, FALSE)
		ELSE
			IF ~(v IS TextViews.View) THEN Fail(65) END;
			t := v(TextViews.View).ThisModel();
			IF mode = "unicode" THEN expected := "Внешний" + 0DX + "text" + 09X + "end"
			ELSIF mode = "ansi" THEN expected := "ANSI café" + 0DX + "x"
			ELSIF mode = "rtf" THEN expected := "Bold Ж" + 0DX + "tail"
			ELSE expected := "BlackBox Ж" + 0DX + "line"
			END;
			IF t.Length() # LEN(expected$) THEN Fail(66) END;
			rd := t.NewReader(NIL);
			FOR i := 0 TO t.Length()-1 DO rd.ReadChar(byte); IF byte # expected[i] THEN Fail(67) END END;
			IF (mode = "rtf") OR (mode = "native") OR (mode = "native-only") THEN
				rd.SetPos(0); rd.Read; IF rd.attr.font.weight # Fonts.bold THEN Fail(68) END
			END;
			Views.OpenAux(v, "External clipboard text"); StdWindows.Validate(NIL);
			t := TextModels.dir.NewFromString("BlackBox Ж" + 0DX + "line");
			t.SetAttr(0, t.Length(), TextModels.NewWeight(TextModels.dir.attr, Fonts.bold));
			v := TextViews.dir.New(t); Views.OpenView(v); SelectEditor(v(TextViews.View));
			TextControllers.SetSelection(t, 0, t.Length()); StdCmds.Copy;
			IF WinClipboard.lastError # 0 THEN Fail(69) END;
			WinClipboard.GetClipView("TextViews.View", copy, w, h, single);
			IF (copy = NIL) OR (copy = v) OR (copy(TextViews.View).ThisModel().Length() # t.Length()) THEN Fail(70) END;
			(* External ownership/sequence changes must invalidate the private copy. *)
			IF A.OpenClipboard(0) = 0 THEN Fail(71) END;
			res := A.EmptyClipboard(); res := A.CloseClipboard();
			IF WinClipboard.ConvertibleTo("") THEN Fail(72) END;
			WinClipboard.GetClipView("TextViews.View", copy, w, h, single); IF copy # NIL THEN Fail(73) END;
			StdCmds.Copy
		END;
		IF WinClipboard.lastError # 0 THEN Fail(74) END;
		IF A.IsClipboardFormatAvailable(A.RegisterClipboardFormatW("BlackBox Data")) = 0 THEN Fail(81) END;
		WinClipboard.Flush;
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); next := Windows.dir.Next(win); win := next END;
		StdCmds.Exit; Say("WinClipboard x64 external exchange OK")
	END Do;

	PROCEDURE Setup*;
	BEGIN
		StdConfig.Setup; Kernel.InstallTrapViewer(Trap); NEW(action);
		Services.DoLater(action, Services.Ticks()+10)
	END Setup;
END CheckWinClipboard64.
