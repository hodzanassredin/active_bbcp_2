MODULE CheckLinGui64;

	IMPORT S := SYSTEM, Kernel, Console, Strings, Windows, Views, StdWindows, StdApi, StdCmds,
		TextModels, TextViews, TextControllers, TextCmds, FormViews, FormModels, Controls;

	PROCEDURE Fail (code: INTEGER);
		VAR n: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(code,n); Console.WriteStr("Linux GUI x64 FAILED "); Console.WriteStr(n); Console.WriteLn;
		Kernel.Quit(code)
	END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;

	PROCEDURE Select (view: Views.View);
		VAR win: Windows.Window;
	BEGIN
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(83) END;
		Windows.dir.Select(win,FALSE); win(StdWindows.Window).backend.Select
	END Select;

	PROCEDURE Verify*;
		VAR source,target: TextViews.View; model: TextModels.Model; form,view: Views.View;
			win: Windows.Window; rd: TextModels.Reader; reader: FormModels.Reader; ch: CHAR; i,controls: INTEGER; expected: ARRAY 32 OF CHAR;
	BEGIN
		Kernel.InstallTrapViewer(Trap);
		IF Windows.dir.First() = NIL THEN Fail(84) END;
		expected := "Linux x64 GC Ж"; model := TextModels.dir.NewFromString(expected); source := TextViews.dir.New(model); Views.OpenView(source);
		Select(source); TextControllers.SetSelection(model,0,model.Length()); StdCmds.Copy;
		IF S.VAL(S.ADDRESS,model) < 100000000L THEN Fail(85) END;
		target := TextViews.dir.New(TextModels.dir.New()); Views.OpenView(target); Select(target);
		TextControllers.SetCaret(target.ThisModel(),0); StdCmds.Paste; StdWindows.Validate(NIL); Kernel.Collect;
		IF target.ThisModel().Length() # model.Length() THEN Fail(86) END;
		rd := target.ThisModel().NewReader(NIL);
		FOR i := 0 TO model.Length()-1 DO rd.ReadChar(ch); IF ch # expected[i] THEN Fail(87) END END;
		StdCmds.Undo; IF target.ThisModel().Length() # 0 THEN Fail(88) END;
		StdCmds.Redo; IF target.ThisModel().Length() # model.Length() THEN Fail(89) END;
		TextCmds.InitFindDialog;
		StdApi.OpenToolDialog("Text/Rsrc/Cmds","#Text:Find / Replace",form);
		IF (form = NIL) OR ~(form IS FormViews.View) THEN Fail(90) END;
		reader := form(FormViews.View).ThisModel().NewReader(NIL); controls := 0;
		LOOP
			reader.ReadView(view); IF view = NIL THEN EXIT END;
			IF view IS Views.Alien THEN Fail(91) END;
			IF view IS Controls.Control THEN INC(controls) END
		END;
		IF controls < 5 THEN Fail(92) END;
		StdWindows.Validate(NIL); Kernel.Collect;
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
		StdCmds.Exit; Console.WriteStr("Linux GUI x64 GC, clipboard, undo/redo and Find OK"); Console.WriteLn
	END Verify;
END CheckLinGui64.
