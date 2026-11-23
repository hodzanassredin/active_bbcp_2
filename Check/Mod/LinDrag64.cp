MODULE CheckLinDrag64;

	IMPORT Kernel, Console, Dialog, Services, Strings, Files, Views, Windows, StdWindows, StdTiles, StdCmds,
		TextModels, TextViews, Host := LinBackends, LinDialog, Gtk := LinGtk4Gtk, GU := LinGtk4Util, Gdk := LinGtk4Gdk;

	TYPE Test = POINTER TO RECORD (Services.Action) END;

	VAR action: Test; stage,tries: INTEGER; waiting: BOOLEAN;
		target: TextViews.View; targetWin: Windows.Window; backend: Host.Backend;
		base,peer: ARRAY 1024 OF CHAR;

	PROCEDURE Fail (code: INTEGER);
		VAR n: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(code,n); Console.WriteStr("Linux file drop FAILED "); Console.WriteStr(n); Console.WriteLn; Kernel.Quit(code) END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100) END Trap;

	PROCEDURE HasText (model: TextModels.Model; IN text: ARRAY OF CHAR): BOOLEAN;
		VAR reader: TextModels.Reader; ch: CHAR; i: INTEGER;
	BEGIN
		IF model.Length() # LEN(text$) THEN RETURN FALSE END;
		reader := model.NewReader(NIL);
		FOR i := 0 TO model.Length()-1 DO reader.ReadChar(ch); IF ch # text[i] THEN RETURN FALSE END END;
		RETURN TRUE
	END HasText;

	PROCEDURE Documents;
		VAR win: Windows.Window; view: Views.View; one,two: INTEGER;
	BEGIN
		one := 0; two := 0; win := Windows.dir.First();
		WHILE win # NIL DO
			view := win.doc.ThisView();
			IF view IS TextViews.View THEN
				IF HasText(view(TextViews.View).ThisModel(),"Drop one") THEN INC(one) END;
				IF HasText(view(TextViews.View).ThisModel(),"Drop two Ж") THEN INC(two) END
			END;
			win := Windows.dir.Next(win)
		END;
		IF (one # 1) OR (two # 1) OR (target.ThisModel().Length() # 0) THEN Fail(72) END
	END Documents;

	PROCEDURE (a: Test) Do;
		VAR n,xs,ys: ARRAY 32 OF CHAR; name: Files.Name; mime: ARRAY 32 OF CHAR;
			command: ARRAY 4096 OF CHAR; status: Files.File; reader: Files.Reader; byte: BYTE;
			ignored: INTEGER; point: RECORD x,y: INTEGER END; win: Windows.Window;
	BEGIN
		Strings.IntToString(stage,n); name := "drop"+n+".status";
		IF ~waiting THEN
			Windows.dir.Select(targetWin,FALSE); targetWin(StdWindows.Window).backend.Select;
			Gtk.gtk_window_present(backend.wnd); 
			IF GU.GetOrigin(backend.wnd,point.x,point.y) THEN ignored := 1 ELSE ignored := 0 END;
			Strings.IntToString(point.x+30,xs); Strings.IntToString(point.y+50,ys);
			IF stage = 2 THEN mime := "text/plain" ELSE mime := "text/uri-list" END;
			command := '"'+peer+'" "'+base+'/payload'+n+'" "'+base+'/'+name+'" '+xs+' '+ys+' '+mime;
			LinDialog.RunExternal(command); waiting := TRUE; tries := 0
		ELSE
			status := Files.dir.Old(Files.dir.This(base),name,FALSE);
			IF status = NIL THEN
				INC(tries); IF tries > 250 THEN Fail(73) END
			ELSE
				reader := status.NewReader(NIL); reader.ReadByte(byte);
				IF stage = 0 THEN IF byte # 49 THEN Fail(74) END
				ELSE IF byte # 48 THEN Fail(75) END
				END;
				Documents; Kernel.Collect; INC(stage); waiting := FALSE;
				IF stage = 3 THEN
					win := Windows.dir.First();
					WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
					StdCmds.Exit; Console.WriteStr("Linux URI file drag/drop OK"); Console.WriteLn; RETURN
				END
			END
		END;
		Services.DoLater(a,Services.Ticks()+50)
	END Do;

	PROCEDURE Start*;
		VAR res: INTEGER;
	BEGIN
		Kernel.InstallTrapViewer(Trap);
		Dialog.GetPar("BB_DROP_BASE",base,res); IF res # 0 THEN Fail(61) END;
		Dialog.GetPar("BB_DROP_PEER",peer,res); IF res # 0 THEN Fail(62) END;
		StdTiles.Off; target := TextViews.dir.New(TextModels.dir.New()); Views.OpenView(target);
		targetWin := Windows.dir.First();
		WHILE (targetWin # NIL) & (targetWin.doc.ThisView() # target) DO targetWin := Windows.dir.Next(targetWin) END;
		IF targetWin = NIL THEN Fail(63) END;
		backend := targetWin(StdWindows.Window).backend(Host.Backend);
		targetWin.SetSize(500,300); 
		NEW(action); Services.DoLater(action,Services.Ticks()+250)
	END Start;
END CheckLinDrag64.
