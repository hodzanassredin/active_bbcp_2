MODULE CheckWheeler;
	
	IMPORT Views, Controllers, Containers, Ports, Fonts, Properties;
	
	TYPE
		View = POINTER TO RECORD (Views.View)
			str: ARRAY 2 OF CHAR;
			col: INTEGER
		END;
	
	VAR colors: ARRAY 3 OF INTEGER;
	
	PROCEDURE (v: View) GetBackground (VAR col: Ports.Color);
	BEGIN
		col := colors[v.col]
	END GetBackground;
	
	PROCEDURE (v: View) Restore (f: Views.Frame; w, h, ww, b: INTEGER);
	BEGIN
		v.context.GetSize(w, h);
		f.DrawString(w DIV 2, h DIV 2, Ports.black, v.str, Fonts.dir.Default())
	END Restore;
	
	PROCEDURE (v: View) HandleCtrlMsg (f: Views.Frame; VAR msg: Controllers.Message; VAR focus: Views.View);
		VAR c: Containers.Controller;
	BEGIN
		WITH msg: Controllers.WheelMsg DO
			c := Containers.Focus();
			IF (c # NIL) & (c.ThisFocus() = v) THEN
				IF (msg.op = Controllers.incLine) OR (msg.op = Controllers.incPage) THEN v.str := '↓'; INC(v.col)
				ELSE v.str := '↑'; DEC(v.col)
				END;
				IF v.col < 0 THEN v.col := LEN(colors) - 1 ELSIF v.col >= LEN(colors) THEN v.col := 0 END;
				msg.done := TRUE;
				Views.Update(v, Views.keepFrames)
			END
		ELSE
		END
	END HandleCtrlMsg;
	
	PROCEDURE (v: View) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.FocusPref DO msg.setFocus := TRUE 
		ELSE
		END
	END HandlePropMsg;
	
	PROCEDURE Deposit*;
		VAR v: View;
	BEGIN
		NEW(v); v.str := ''; v.col := 0; Views.Deposit(v)
	END Deposit;

BEGIN
	colors[0] := Ports.green; colors[1] := Ports.red; colors[2] := Ports.blue
END CheckWheeler.



(* for some reason, asUax = TRUE => (wheel) scrolling enabled!, ~asAux => scrolling disabled!*)

MaskMode => hides (v)scrollbar



Containers

	PROCEDURE (v: View) HandleCtrlMsg* (f: Views.Frame; VAR msg: Controllers.Message; VAR focus: Views.View);
	BEGIN
		IF v.controller # NIL THEN v.controller.HandleCtrlMsg(f, msg, focus) END;
		v.HandleCtrlMsg2(f, msg, focus);
		WITH msg: Controllers.PollSectionMsg DO
			IF ~msg.focus THEN focus := NIL END
		| msg: Controllers.ScrollMsg DO
			IF ~msg.focus THEN focus := NIL END
		ELSE
		END
	END HandleCtrlMsg;

	PROCEDURE (c: Controller) HandleCtrlMsg* (f: Views.Frame;
			VAR msg: Controllers.Message; VAR focus: Views.View), NEW, EXTENSIBLE;
	BEGIN
		focus := c.focus;
		WITH msg: Controllers.PollCursorMsg DO
			PollCursor(c, f, msg, focus)
		| msg: Controllers.PollOpsMsg DO
			PollOps(c, f, msg, focus)
		| msg: PollFocusMsg DO
			IF msg.all OR (c.opts * modeOpts # mask) & (c.focus # NIL) THEN msg.ctrl := c END
		| msg: Controllers.TrackMsg DO
			Track(c, f, msg, focus)
		| msg: Controllers.EditMsg DO
			Edit(c, f, msg, focus)
		| msg: Controllers.TransferMessage DO
			Transfer(c, f, msg, focus)
		| msg: Controllers.SelectMsg DO
			IF focus = NIL THEN c.SelectAll(msg.set) END
		| msg: Controllers.TickMsg DO
			FadeMarks(c, show);
			CheckMaskFocus(c, f, focus)
		| msg: Controllers.MarkMsg DO
			c.bVis := msg.show;
			c.Mark(f, f.l, f.t, f.r, f.b, msg.show)
		| msg: Controllers.ReplaceViewMsg DO
			ReplaceView(c, msg.old, msg.new)
		| msg: Properties.CollectMsg DO
			IF focus = NIL THEN
				msg.poll.prop := ThisProp(c, direct)
			END
		| msg: Properties.EmitMsg DO
			IF focus = NIL THEN
				SetProp(c, msg.set.old, msg.set.prop, direct)
			END
		| msg: Controllers.WheelMsg DO Wheel(c, f, msg, focus)
		ELSE
		END
	END HandleCtrlMsg;



TextViews

	PROCEDURE (v: StdView) HandleCtrlMsg2 (f: Views.Frame;
		VAR msg: Controllers.Message; VAR focus: Views.View
	);
	BEGIN
		WITH msg: Controllers.PollSectionMsg DO
			IF (focus = NIL) OR ~msg.focus THEN
				PollSection(v, f, msg);
				focus := NIL
			END
		| msg: FindFocusFrameMsg DO
			IF (msg.view = v) & (msg.frame = NIL) THEN msg.frame := f END
		| msg: Controllers.ScrollMsg DO
			IF (focus = NIL) OR ~msg.focus THEN
				Scroll(v, f, msg);
				focus := NIL
			END
		| msg: Controllers.PageMsg DO
			Page(v, f.b - f.t, msg.op, msg.pageY, msg.done, msg.eoy);
			focus := NIL
		ELSE
		END
	END HandleCtrlMsg2;




TextControllers

	PROCEDURE (c: StdCtrl) HandleCtrlMsg (f: Views.Frame;
		VAR msg: Controllers.Message; VAR focus: Views.View
	);
		VAR g: Views.Frame; beg, end: INTEGER; done: BOOLEAN;
	BEGIN
		IF (msg IS Controllers.MarkMsg) OR (msg IS Controllers.TickMsg) THEN
			beg := c.autoBeg; end := c.autoEnd;
			c.autoBeg := MAX(INTEGER); c.autoEnd := 0
		END;
		WITH msg: Controllers.TickMsg DO
			IF ~(noAutoScroll IN c.opts)
				& (0 <= beg) & (beg <= end) & (end <= c.text.Length())
				& c.view.context.Normalize()
			THEN
				c.view.ShowRange(beg, end, TextViews.focusOnly)
			END;
			IF focus = NIL THEN
				CheckCaret(c); BlinkCaret(c, f, msg.tick);
				IF (c.selBeg # c.aliasSelBeg) OR (c.selEnd # c.aliasSelEnd) THEN
					(* lazy update of text-synchronous alias marks *)
					c.aliasSelBeg := c.selBeg; c.aliasSelEnd := c.selEnd;
					SetSelection(c.text, c.selBeg, c.selEnd)
				END
			END
		| msg: Controllers.MarkMsg DO
			c.carX := -1;
			IF msg.show THEN c.carVisible := TRUE; c.carTick := 0 END
		| msg: Controllers.TrackMsg DO
			c.insAttr := NIL; c.carX := -1; Models.StopBunching(c.text)
		| msg: Controllers.EditMsg DO
			c.lastX := c.carX; c.carX := -1;
			IF focus = NIL THEN CheckCaret(c) END
		| msg: Controllers.ReplaceViewMsg DO
			c.carX := -1
		| msg: Controllers.TransferMessage DO
			c.carX := -1
		| msg: Properties.EmitMsg DO
			c.carX := -1
		ELSE
		END;
		done := FALSE;
		WITH msg: Controllers.CursorMessage DO
			IF TRUE (* Containers.noCaret IN c.opts *) THEN	(* mask or browser mode *)
				g := Views.FrameAt(f, msg.x, msg.y);
				IF (g = NIL) OR IsFilter(g.view, c, f, msg.x, msg.y) THEN
					WITH msg: Controllers.PollCursorMsg DO
						FilteredPollCursor(c, f, msg, done)
					| msg: Controllers.TrackMsg DO
						FilteredTrack(c, f, msg, done)
					ELSE
					END
				END
			END
		ELSE
		END;
		IF ~done THEN c.HandleCtrlMsg^(f, msg, focus) END
	END HandleCtrlMsg;

