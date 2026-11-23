MODULE CheckWinDrag64;
	IMPORT S := SYSTEM, Kernel, WinKernel, Services, Dialog, StdConfig, StdCmds, StdWindows, StdTiles,
		Views, Windows, TextModels, TextViews, TextControllers, Containers, Controllers,
		Mechanisms, Ports, Files, WinFiles, WinGui, WinPorts, WinMechanisms, A := WinApi, Probe := CheckWinDragProbe64;

	TYPE Test = POINTER TO RECORD (Services.Action) END;

	VAR action: Test; phase: INTEGER;

	PROCEDURE Say (IN text: ARRAY OF CHAR);
	BEGIN WinKernel.WkWrite(S.ADR(text), LEN(text$)) END Say;

	PROCEDURE Fail (code: INTEGER);
	BEGIN Say("WinDrag x64 FAILED"); Kernel.Quit(code) END Fail;

	PROCEDURE Trap;
	BEGIN Fail(100+phase) END Trap;

	PROCEDURE Editor (IN text: ARRAY OF CHAR; top: INTEGER; OUT view: TextViews.View; OUT win: Windows.Window; OUT frame: Views.Frame);
		VAR ignored: INTEGER;
	BEGIN
		view := TextViews.dir.New(TextModels.dir.NewFromString(text)); Views.OpenView(view);
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(61) END;
		Windows.dir.Select(win, FALSE); win(StdWindows.Window).backend.Select;
		ignored := A.SetWindowPos(WinGui.mainWnd, -1, 150, top, 500, 250, {});
		StdWindows.Validate(NIL); ignored := A.UpdateWindow(WinGui.mainWnd);
		frame := Views.ThisFrame(win.frame, view); IF frame = NIL THEN Fail(62) END;
		IF WinMechanisms.lastDropError < 0 THEN Fail(63) END
	END Editor;

	PROCEDURE Point (view: TextViews.View; frame: Views.Frame; pos: INTEGER; OUT x,y: INTEGER; OUT screen: A.POINT);
		VAR loc: TextViews.Location; ignored: INTEGER;
	BEGIN
		view.GetThisLocation(frame, pos, loc); x := loc.x+frame.unit; y := loc.y+loc.asc DIV 2;
		screen.x := (x+frame.gx) DIV frame.unit; screen.y := (y+frame.gy) DIV frame.unit;
		ignored := A.ClientToScreen(frame.rider(WinPorts.Rider).port.wnd, screen)
	END Point;

	PROCEDURE Equal (model: TextModels.Model; IN text: ARRAY OF CHAR);
		VAR rd: TextModels.Reader; ch: CHAR; i: INTEGER; actual: ARRAY 128 OF CHAR;
	BEGIN
		IF model.Length() # LEN(text$) THEN
			rd := model.NewReader(NIL); i := 0;
			WHILE (i < model.Length()) & (i < LEN(actual)-1) DO rd.ReadChar(ch); actual[i] := ch; INC(i) END;
			actual[i] := 0X; Say(actual); Fail(64+phase)
		END;
		rd := model.NewReader(NIL);
		FOR i := 0 TO model.Length()-1 DO rd.ReadChar(ch); IF ch # text[i] THEN Fail(64+phase) END END
	END Equal;

	PROCEDURE Incoming;
		VAR view: TextViews.View; win: Windows.Window; frame: Views.Frame;
			x,y, effect, mode, ignored: INTEGER; pt: A.POINT; rd: TextModels.Reader; child: Views.View; c: Containers.Controller; nativeMode: INTEGER;
	BEGIN
		FOR mode := 0 TO 5 DO
			IF mode # 2 THEN
				phase := mode; Editor("", 120, view, win, frame); Point(view, frame, 0, x,y,pt);
				nativeMode := mode;
				IF mode = 5 THEN nativeMode := 0; c := view.ThisController(); c.SetOpts(c.opts+{Containers.noCaret}) END;
				effect := Probe.IncomingProcess(WinGui.mainWnd, pt.x,pt.y,nativeMode,1,0,0);
				IF mode = 0 THEN
					IF effect # 1 THEN Fail(70) END; Equal(view.ThisModel(), "External drag Ж")
				ELSIF mode = 1 THEN
					IF effect # 1 THEN Fail(71) END; Equal(view.ThisModel(), "Drag Ж");
					rd := view.ThisModel().NewReader(NIL); rd.Read; IF rd.attr.font.weight # 700 THEN Fail(72) END
				ELSIF (mode = 3) OR (mode = 5) THEN
					IF (effect # 0) OR (view.ThisModel().Length() # 0) THEN Fail(73) END
				ELSIF mode = 4 THEN
					IF (effect # 1) OR (view.ThisModel().Length() # 1) THEN Fail(74) END;
					rd := view.ThisModel().NewReader(NIL); rd.ReadView(child); IF child = NIL THEN Fail(75) END
				END;
				win.seq.SetDirty(FALSE); Windows.dir.Close(win)
			END
		END
	END Incoming;

	PROCEDURE RightDrop;
		VAR view: TextViews.View; win: Windows.Window; frame: Views.Frame; x,y,effect: INTEGER; pt: A.POINT;
	BEGIN
		phase := 10; Editor("",120,view,win,frame); Point(view,frame,0,x,y,pt);
		effect := Probe.IncomingProcess(WinGui.mainWnd,pt.x,pt.y,0,3,0,0);
		IF effect # 2 THEN Fail(94) END; Equal(view.ThisModel(),"External drag Ж");
		win.seq.SetDirty(FALSE); Windows.dir.Close(win)
	END RightDrop;

	PROCEDURE FileDrops;
		VAR view: TextViews.View; win, opened: Windows.Window; frame: Views.Frame;
			x,y,res,effect,mode: INTEGER; pt: A.POINT; path: ARRAY 1024 OF CHAR; v: Views.View;
	BEGIN
		phase := 8; Dialog.GetPar("BB_DRAG_FILE",path,res); IF res # 0 THEN Fail(90) END;
		FOR mode := 0 TO 1 DO
			Editor("",120,view,win,frame); Point(view,frame,0,x,y,pt);
			IF mode = 0 THEN
				effect := Probe.IncomingProcess(WinGui.mainWnd,pt.x,pt.y,2,1,0,S.ADR(path)); IF effect # 1 THEN Fail(91) END
			ELSE Probe.LegacyFiles(WinGui.mainWnd,S.ADR(path)) END;
			opened := Windows.dir.First(); IF opened = win THEN Fail(92) END;
			v := opened.doc.ThisView(); IF ~(v IS TextViews.View) THEN Fail(93) END;
			Equal(v(TextViews.View).ThisModel(),"Dropped file");
			Windows.dir.Close(opened); Windows.dir.Close(win)
		END
	END FileDrops;

	PROCEDURE Source (internal, move, cancel: BOOLEAN);
		VAR view, target: TextViews.View; win, targetWin: Windows.Window; frame, targetFrame, dest: Views.Frame;
			x,y,sx,sy,dx,dy,op,ignored, expected, seq, modifier, cancelFlag: INTEGER; pt: A.POINT; buttons: SET;
			model: Containers.Model; copy: Views.View; c: Containers.Controller;
	BEGIN
		phase := 6; Editor("DRAG tail", 100, view, win, frame);
		IF internal THEN Editor("", 400, target, targetWin, targetFrame); Point(target,targetFrame,0,x,y,pt)
		ELSE pt.x := 0; pt.y := 0
		END;
		Windows.dir.Select(win, FALSE); win(StdWindows.Window).backend.Select;
		TextControllers.SetSelection(view.ThisModel(),0,4); Point(view,frame,0,sx,sy,pt);
		IF internal THEN Point(target,targetFrame,0,x,y,pt) ELSE pt.x := 0; pt.y := 0 END;
		IF move THEN expected := 2 ELSE expected := 1 END;
		IF move THEN modifier := 2 ELSE modifier := 1 END;
		IF cancel THEN cancelFlag := 1 ELSE cancelFlag := 0 END;
		Probe.BeginOutgoing(pt.x,pt.y,modifier,cancelFlag);
		x := sx; y := sy; op := 0; buttons := {}; seq := A.GetClipboardSequenceNumber();
		Mechanisms.TrackToDrop(frame, view,FALSE,80*Ports.mm,20*Ports.mm,0,0,dest,dx,dy,op,x,y,buttons);
		IF A.GetClipboardSequenceNumber() # seq THEN Fail(80) END;
		IF internal OR cancel THEN ignored := Probe.EndOutgoing(0) ELSE ignored := Probe.EndOutgoing(expected) END;
		IF cancel THEN IF op # Mechanisms.cancelDrop THEN Fail(81) END
		ELSE
			IF move THEN IF op # Mechanisms.move THEN Fail(82) END
			ELSE IF op # Mechanisms.copy THEN Fail(83) END END;
			c := view.ThisController();
			IF internal THEN
				IF dest # targetFrame THEN Fail(84) END;
				model := c.SelectionCopy(); copy := Views.CopyWithNewModel(view,model);
				IF move THEN c.DeleteSelection END;
				Controllers.Drop(x,y,frame,sx,sy,copy,FALSE,80*Ports.mm,20*Ports.mm,0,0);
				Equal(target.ThisModel(), "DRAG")
			ELSIF move THEN c.DeleteSelection
			END
		END;
		IF move & ~cancel THEN Equal(view.ThisModel(), " tail") ELSE Equal(view.ThisModel(), "DRAG tail") END;
		IF internal THEN targetWin.seq.SetDirty(FALSE); Windows.dir.Close(targetWin) END;
		win.seq.SetDirty(FALSE); Windows.dir.Close(win)
	END Source;

	PROCEDURE Gesture (same, move: BOOLEAN);
		VAR view, target: TextViews.View; win, targetWin: Windows.Window; frame, targetFrame: Views.Frame;
			x,y,modifier: INTEGER; src, dst: A.POINT;
	BEGIN
		phase := 9; Editor("DRAG tail",100,view,win,frame);
		IF same THEN target := view; targetFrame := frame; targetWin := win
		ELSE Editor("",400,target,targetWin,targetFrame) END;
		Windows.dir.Select(win,FALSE); win(StdWindows.Window).backend.Select;
		TextControllers.SetSelection(view.ThisModel(),0,4);
		Point(view,frame,1,x,y,src);
		IF same THEN Point(target,targetFrame,9,x,y,dst) ELSE Point(target,targetFrame,0,x,y,dst) END;
		IF move THEN modifier := 0 ELSE modifier := 1 END;
		Probe.Gesture(src.x,src.y,dst.x,dst.y,modifier);
		IF same THEN
			IF move THEN Equal(view.ThisModel()," tailDRAG") ELSE Equal(view.ThisModel(),"DRAG tailDRAG") END;
			StdCmds.Undo; Equal(view.ThisModel(),"DRAG tail"); StdCmds.Redo;
			IF move THEN Equal(view.ThisModel()," tailDRAG") ELSE Equal(view.ThisModel(),"DRAG tailDRAG") END
		ELSE
			Equal(target.ThisModel(),"DRAG");
			IF move THEN Equal(view.ThisModel()," tail") ELSE Equal(view.ThisModel(),"DRAG tail") END;
			targetWin.seq.SetDirty(FALSE); Windows.dir.Close(targetWin)
		END;
		win.seq.SetDirty(FALSE); Windows.dir.Close(win)
	END Gesture;

	PROCEDURE (a: Test) Do;
		VAR win, next: Windows.Window;
	BEGIN
		StdTiles.Off;
		Incoming; FileDrops; RightDrop;
		Source(FALSE,FALSE,FALSE); Source(FALSE,TRUE,FALSE); Source(FALSE,FALSE,TRUE);
		Source(TRUE,FALSE,FALSE); Source(TRUE,TRUE,FALSE);
		Gesture(FALSE,FALSE); Gesture(FALSE,TRUE); Gesture(TRUE,FALSE); Gesture(TRUE,TRUE);
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); next := Windows.dir.Next(win); win := next END;
		StdCmds.Exit; Say("WinDrag x64 OLE drag/drop OK")
	END Do;

	PROCEDURE Setup*;
	BEGIN StdConfig.Setup; Kernel.InstallTrapViewer(Trap); NEW(action); Services.DoLater(action,Services.Ticks()+10) END Setup;
END CheckWinDrag64.
