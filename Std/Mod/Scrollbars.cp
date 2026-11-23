MODULE StdScrollbars;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Anton Dmitriev, Ketmar Dark, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Views, Ports, Models, Controllers, Services, Properties, Dialog;

	CONST
		left = 16; middle = 17; right = 18; (* HostPorts.left, .middle, .right *)
		vertical* = TRUE;	(* scrollbar orientation *)
		minThumbSize = 9;

	TYPE
		UCs = INTEGER;
		Pixels = INTEGER;

		Scrollbar* = POINTER TO ABSTRACT RECORD (Views.View)
			wholeSize*, partSize*, partPos*, bottomMargin*: INTEGER;
			isVertical*, pressed: BOOLEAN;
			lastDot-: INTEGER;
			(* the following is set by Recalc *)
			w, h: Pixels;	(* last known width and height *)
			size: Pixels;	(* last known scrollbar size with arrows if they exists *)
			thPos, thSize, arrowSize: Pixels	(* last known thumbnail position and size *)
		END;
		StdScrollbar = POINTER TO RECORD (Scrollbar) END;

		ScrollbarDirectory* = POINTER TO ABSTRACT RECORD END;
		StdScrollbarDirectory = POINTER TO RECORD (ScrollbarDirectory) END;

		(** a proposal indicating to the context that the scrollbar has been
			scrolled by the user. It is up to the receiver of the proposal to request
			(thru Views.Update) or force (thru Views.RestoreRoot) the
			restoration of the scrollbar. *)
		Proposal* = RECORD (Models.Proposal)
			sb*: Scrollbar;
			frame*: Views.Frame;	(** the sender may specify .sb's frame to allow optimizations in the receiver.
															ASSERT((.frame = NIL) OR (.frame.view = .sb)) *)
			op*: INTEGER	(** same constants as for Controllers.ScrollMsg.op *)
		END;

		Param* = POINTER TO RECORD
			thumbColor*: Ports.Color;
			thumbColorPressed*: Ports.Color;
			thumbMin*: INTEGER;
			shaftColor*: Ports.Color;
			backScrollbarButtons*, arrowsColor*: Ports.Color;
			arrowsSize*: INTEGER;
			disabledColor*: Ports.Color;
			leftGoto*: BOOLEAN;
			thumbMargin*: INTEGER;
			arrows*: BOOLEAN;
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;

		FindFrameMsg = RECORD (Views.Message)
			frame: Views.Frame
		END;


	VAR
		sbDir*: ScrollbarDirectory;
		stdSbDir-: StdScrollbarDirectory;
		param*: Param;

	(** Scrollbar **)

	(* sb.thSize should be already set,  calculate part of scrollbar without thumb *)
	PROCEDURE GetPartSize (sb: Scrollbar): INTEGER;
	VAR res, sbsize: INTEGER;
	BEGIN
		res := sb.partSize;
		IF res <= 0 THEN
			ASSERT(sb.thSize >= minThumbSize, 100);
			sbsize := sb.size - param.thumbMargin * 2 - sb.arrowSize * 2;
			IF sb.thSize >= sbsize THEN
				res := 0
			ELSE
				res := SHORT(LONG(sb.wholeSize) DIV (sbsize - sb.thSize))
			END
		END;
		RETURN MAX(0, res)
	END GetPartSize;

	PROCEDURE Recalc (sb: Scrollbar; dot: UCs);
	VAR pos, sz, sbsize: INTEGER;
	BEGIN
		sb.lastDot := dot;
		sb.context.GetSize(sb.w, sb.h);
		sb.w := sb.w DIV dot; sb.h := sb.h DIV dot;
		sb.size := 0; sb.thPos := 0; sb.thSize := 0;
		IF (sb.wholeSize > 0) & (sb.w > 0) & (sb.h > 0) THEN
			IF sb.isVertical THEN
				sb.size := sb.h - sb.bottomMargin DIV dot
			ELSE
				sb.size := sb.w
			END;
			IF sb.size < minThumbSize + 3 THEN sb.size := 0
			ELSE
				(* calculate thumb size  *)
				sbsize := sb.size - param.thumbMargin * 2 - sb.arrowSize * 2;
				IF (sb.wholeSize <= 0) OR (sb.partSize > 0) & (sb.wholeSize <= sb.partSize) THEN
					HALT(0)
				ELSE
					sb.thSize := MAX(minThumbSize, param.thumbMin);
					(* StdDocument often doesn't set part size *)
					IF sb.partSize > 0 THEN
						sb.thSize := MAX(sb.thSize, MIN(sbsize, SHORT(LONG(sbsize) * sb.partSize DIV sb.wholeSize)))
					END
				END;
				
				IF (sbsize <= 0) OR (sb.wholeSize <= 0) OR (sb.wholeSize <= sb.partSize) THEN
					sb.thPos := 0
				ELSIF sb.thSize >= sbsize THEN
					sb.thPos := 0
				ELSE
					ASSERT(sb.thSize <= sbsize, 100);
					sz := sb.wholeSize - GetPartSize(sb);
					IF sz <= 0 THEN sz := 1 END;
					pos := MAX(0, MIN(sb.partPos, sz));
					sb.thPos := SHORT(LONG(sbsize - sb.thSize) * pos DIV sz)
				END
			END
		END
	END Recalc;

	(* tp is thumb position (top/left); result is the position in the sb range.
		should be called after Recalc. *)
	PROCEDURE Thumb2Pos (sb: Scrollbar; tp: Pixels): INTEGER;
	VAR res, sbsize, partSize: INTEGER;
	BEGIN
		sbsize := sb.size - param.thumbMargin * 2 - sb.arrowSize * 2;
		IF (tp <= 0) OR (sb.wholeSize <= 0) OR (sb.thSize = 0) THEN
			res := 0
		ELSIF tp >= sbsize - sb.thSize THEN
			partSize := 0; (* GetPartSize(sb); *)
			res := sb.wholeSize - partSize;
		ELSE
			res := SHORT(LONG(sb.wholeSize - GetPartSize(sb)) * tp DIV (sbsize - sb.thSize))
		END;
		RETURN res
	END Thumb2Pos;

	(* arrows *)
	
	
	PROCEDURE DrawArrowUp (c: Views.Frame; x, y: INTEGER);
		VAR s: INTEGER;
	BEGIN
		s := param.arrowsSize;
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y + s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y + s, c.dot, param.arrowsColor);
	END DrawArrowUp;

	PROCEDURE DrawArrowDown (c: Views.Frame; x, y: INTEGER);
		VAR s: INTEGER;
	BEGIN
		s := param.arrowsSize;
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y - s, c.dot, param.arrowsColor);
	END DrawArrowDown;

	PROCEDURE DrawArrowLeft (c: Views.Frame; x, y: INTEGER);
		VAR s: INTEGER;
	BEGIN
		s := param.arrowsSize;
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x + s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y + s, c.dot, param.arrowsColor);
	END DrawArrowLeft;

	PROCEDURE DrawArrowRight (c: Views.Frame; x, y: INTEGER);
		VAR s: INTEGER;
	BEGIN
		s := param.arrowsSize;
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x - s, y + s, c.dot, param.arrowsColor);
	END DrawArrowRight;

	PROCEDURE (sb: StdScrollbar) Restore* (f: Views.Frame; l, t, r, b: INTEGER);
	VAR w, h, dot: UCs; color, arrowSizeUCs: INTEGER;
	BEGIN
		dot := f.dot;
		sb.context.GetSize(w, h);
		IF param.arrows THEN
			IF sb.isVertical THEN
				sb.arrowSize := w DIV dot; arrowSizeUCs := w
			ELSE
				sb.arrowSize := h DIV dot; arrowSizeUCs := h
			END;
		ELSE
			sb.arrowSize := 0; arrowSizeUCs := 0;
		END;
		Recalc(sb, dot);
		IF sb.size - param.thumbMargin * 2 - sb.arrowSize * 2 <= 0 THEN
			f.DrawRect(l, t, r, b, Ports.fill, param.disabledColor)
		ELSE
			f.DrawRect(l, 0, r, b, Ports.fill, param.backScrollbarButtons);
			
			IF sb.bottomMargin > 0 THEN
				f.DrawRect(0, h - sb.bottomMargin, w, h, Ports.fill, param.shaftColor);
			END;
			
			IF sb.isVertical THEN
				IF arrowSizeUCs < h - arrowSizeUCs THEN
					f.DrawRect(0, arrowSizeUCs, r, h - arrowSizeUCs - sb.bottomMargin, Ports.fill, param.shaftColor)
				END
			ELSE
				IF arrowSizeUCs < w - arrowSizeUCs THEN
					f.DrawRect(arrowSizeUCs, 0, w - arrowSizeUCs, b, Ports.fill, param.shaftColor)
				END
			END;
			IF param.arrows THEN
				IF sb.isVertical THEN
					DrawArrowUp(f, w DIV 2, w DIV 2 - param.arrowsSize DIV 2);
					DrawArrowDown(f, w DIV 2, h - (w DIV 2 - param.arrowsSize DIV 2) - sb.bottomMargin);
				ELSE
					DrawArrowLeft(f, h DIV 2 - param.arrowsSize DIV 2, h DIV 2);
					DrawArrowRight(f, w - (h DIV 2 - param.arrowsSize DIV 2), h DIV 2);
				END;
			END;
			
			IF sb.pressed THEN
				color := param.thumbColorPressed
			ELSE
				color := param.thumbColor
			END;
			l := dot * param.thumbMargin;
			t := dot * param.thumbMargin;
			IF sb.isVertical THEN
				r := w - dot * param.thumbMargin;
				INC(t, sb.thPos * dot);
				IF param.arrows THEN
					INC(t, arrowSizeUCs)
				END;
				b := t + sb.thSize * dot;
				IF l < r THEN
					f.DrawRect(l, t, r, b, Ports.fill, color)
				END
			ELSE
				b := h - dot * param.thumbMargin;
				INC(l, sb.thPos * dot);
				IF param.arrows THEN
					INC(l, arrowSizeUCs)
				END;
				r := l + sb.thSize * dot;
				IF t < b THEN
					f.DrawRect(l, t, r, b, Ports.fill, color)
				END
			END
		END
	END Restore;

	PROCEDURE UpdateRoot (c: Views.Frame);
	VAR
		root: Views.RootFrame;
		l, t, r, b: INTEGER;
	BEGIN
		IF (c = NIL) OR (c.rider = NIL) THEN RETURN END;
		root := Views.UltimateRootOf(Views.RootOf(c));
		IF root # NIL THEN
			l := root.l + root.gx;
			t := root.t + root.gy;
			r := root.r + root.gx;
			b := root.b + root.gy;
			Views.RestoreRoot(root, l, t, r, b)
		END
	END UpdateRoot;

	PROCEDURE TrackThumb (sb: StdScrollbar; f: Views.Frame; msg: Controllers.TrackMsg);
	VAR
		p: Proposal;
		delta, q, x, y, ox, oy, partPos: INTEGER;
		ticks: LONGINT;
		mod: SET;
		isDown: BOOLEAN;
		ffm: FindFrameMsg;
		root: Views.RootFrame;
	BEGIN
		root := Views.UltimateRootOf(Views.RootOf(f));
		IF sb.size - param.thumbMargin * 2 - sb.arrowSize * 2 > 0 THEN
			x := msg.x; y := msg.y; ox := x; oy := y;
			IF sb.isVertical THEN delta := y DIV f.dot - sb.thPos
			ELSE delta := x DIV f.dot - sb.thPos
			END;
			INC(delta, param.thumbMargin + sb.arrowSize);
			ticks := Services.Ticks(); sb.pressed := TRUE; UpdateRoot(root);
			LOOP
				IF (f = NIL) OR (f.rider = NIL) THEN
					ffm.frame := NIL; Views.Broadcast(sb, ffm); f := ffm.frame
				END;
				IF (f = NIL) OR (f.rider = NIL) THEN EXIT END;
				f.Input(x, y, mod, isDown);
				IF f.rider = NIL THEN
					UpdateRoot(root); ffm.frame := NIL; Views.Broadcast(sb, ffm); f := ffm.frame
				END;
				IF (f = NIL) OR (f.rider = NIL) THEN EXIT END;
				IF (sb.isVertical & (y # oy)) OR (~sb.isVertical & (x # ox)) THEN
					IF sb.isVertical THEN q := y DIV f.dot ELSE q := x DIV f.dot END;
					INC(q, param.thumbMargin + sb.arrowSize);
					partPos := Thumb2Pos(sb, q - delta);
					IF (partPos # sb.partPos) & (isDown OR (Services.Ticks() - ticks > 10)) THEN
						sb.partPos := partPos;
						p.sb := sb; p.op := Controllers.gotoPos; p.frame := f;
						sb.context.Consider(p);
						(* Scrolling may close the old scrollbar frame. Restore the saved
						   root before looking for its replacement: it may not exist yet. *)
						UpdateRoot(root);
						IF f.rider = NIL THEN
							ffm.frame := NIL; Views.Broadcast(sb, ffm); f := ffm.frame
						END;
						ticks := Services.Ticks()
					END;
					ox := x; oy := y
				END;
				IF ~isDown THEN EXIT END
			END;
			sb.pressed := FALSE; UpdateRoot(root)
		ELSE
			REPEAT
				IF f.rider = NIL THEN RETURN END;
				f.Input(x, y, mod, isDown)
			UNTIL (f.rider = NIL) OR ~isDown
		END
	END TrackThumb;

	(** w, h - dimensions of sb; ltrb - thumb bounding box *)
	PROCEDURE TrackShaft (sb: StdScrollbar; f: Views.Frame; msg: Controllers.TrackMsg);
	CONST
		interval = 500;
		none = -1;
		up = Controllers.decPage;
		down = Controllers.incPage;
	VAR
		p: Proposal;
		mod: SET;
		isDown: BOOLEAN;
		x, y, tp: INTEGER;
		dir: INTEGER;
		ticks, oticks: LONGINT;
	BEGIN
		p.sb := sb;
		x := msg.x;
		y := msg.y;
		oticks := 0;
		REPEAT
			ticks := Services.Ticks();
			IF (ticks - oticks > interval) & (f.l <= x) & (x < f.r) & (f.t <= y) & (y < f.b) THEN
				Recalc(sb, f.dot);
				IF sb.size - param.thumbMargin * 2 - sb.arrowSize * 2 > 0 THEN
					IF sb.isVertical THEN
						tp := y DIV f.dot
					ELSE
						tp := x DIV f.dot
					END;
					INC(tp, param.thumbMargin + sb.arrowSize);
					IF tp < sb.thPos THEN
						dir := up
					ELSIF tp > sb.thPos + sb.thSize THEN
						dir := down
					ELSE
						dir := none
					END;
					IF dir # none THEN p.op := dir; p.frame := f; sb.context.Consider(p) END
				END;
				oticks := ticks
			END;
			IF f.rider # NIL THEN f.Input(x, y, mod, isDown) END;
		UNTIL (f.rider = NIL) OR ~isDown
	END TrackShaft;

	PROCEDURE Goto (sb: StdScrollbar; f: Views.Frame; msg: Controllers.TrackMsg);
		VAR p: Proposal; q: INTEGER; partPos: INTEGER;
	BEGIN
		(* IF sb.size - param.thumbMargin * 2 > 0 THEN *)
			IF sb.isVertical THEN
				q := msg.y DIV f.dot
			ELSE
				q := msg.x DIV f.dot
			END;
			DEC(q, param.thumbMargin + sb.arrowSize);
			DEC(q, sb.thSize DIV 2);
			partPos := Thumb2Pos(sb, q);
			IF partPos # sb.partPos THEN
				sb.partPos := partPos;
				p.sb := sb;
				p.op := Controllers.gotoPos;
				p.frame := f;
				sb.context.Consider(p)
			END
		(* END *)
	END Goto;

	PROCEDURE (sb: StdScrollbar) HandleCtrlMsg* (f: Views.Frame; VAR msg: Views.CtrlMessage; VAR focus: Views.View);
	VAR
		sbpos: Pixels; p: Proposal; partPos, sbsize: INTEGER;
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			Recalc(sb, f.dot);
			IF sb.isVertical THEN
				sbpos := msg.y DIV f.dot
			ELSE
				sbpos := msg.x DIV f.dot
			END;
			IF param.arrows & (sbpos < sb.arrowSize) THEN
				(* up arrow *)
				sbsize := sb.size - param.thumbMargin * 2 - sb.arrowSize * 2;
				partPos := sb.thPos - sbsize DIV 10;
				IF partPos < 0 THEN
					partPos := 0
				ELSE
					partPos := Thumb2Pos(sb, partPos);
				END;
				IF partPos # sb.partPos THEN
					sb.partPos := partPos; 
					p.sb := sb;
					p.op := Controllers.gotoPos; p.frame := f;
					sb.context.Consider(p)
				END
			ELSIF param.arrows & (sbpos > sb.size - sb.arrowSize) &
				(sbpos < sb.size - sb.arrowSize) THEN
				(* down arrow *)
				sbsize := sb.size - param.thumbMargin * 2 - sb.arrowSize * 2;
				partPos := sb.thPos + sbsize DIV 10;
				partPos := Thumb2Pos(sb, partPos);
				IF partPos # sb.partPos THEN
					sb.partPos := partPos; 
					p.sb := sb;
					p.op := Controllers.gotoPos; p.frame := f;
					sb.context.Consider(p)
				END
			ELSE
				DEC(sbpos, param.thumbMargin + sb.arrowSize);
				IF middle IN msg.modifiers THEN
					Goto(sb, f, msg)
				ELSIF (sbpos >= sb.thPos) & (sbpos < sb.thPos + sb.thSize) THEN
					TrackThumb(sb, f, msg);
				ELSIF param.leftGoto & (left IN msg.modifiers) THEN
					Goto(sb, f, msg)
				ELSIF msg.modifiers * {left, right} # {} THEN
					TrackShaft(sb, f, msg)
				END
			END
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE (sb: StdScrollbar) HandlePropMsg- (VAR msg: Properties.Message);
	BEGIN WITH msg: Properties.FocusPref DO msg.hotFocus := TRUE ELSE END
	END HandlePropMsg;

	PROCEDURE (sb: StdScrollbar) HandleViewMsg- (f: Views.Frame; VAR msg: Views.Message);
	BEGIN WITH msg: FindFrameMsg DO msg.frame := f ELSE END
	END HandleViewMsg;

	PROCEDURE (sb: StdScrollbar) GetBackground* (VAR col: Ports.Color);
	BEGIN col := Views.transparent
	END GetBackground;


	(** Directory **)

	PROCEDURE (d: ScrollbarDirectory) New* (): Scrollbar, NEW, ABSTRACT;


	(** StdDirectory **)

	PROCEDURE (d: StdScrollbarDirectory) New (): Scrollbar;
	VAR sb: StdScrollbar;
	BEGIN
		NEW(sb);
		sb.isVertical := TRUE;
		sb.wholeSize := 0;
		sb.bottomMargin := 0;
		RETURN sb
	END New;

	PROCEDURE InitParam (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			
		END
	END InitParam;

	PROCEDURE InitDefaults (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			p.thumbColor := Ports.RGBColor(0BBH, 0BBH, 0BBH);
			p.thumbColorPressed := Ports.RGBColor(090H, 090H, 090H);
			p.thumbMin := 30;
			p.shaftColor := Ports.RGBColor(0DDH, 0DDH, 0DDH);
			p.backScrollbarButtons := Ports.RGBColor(0CCH, 0CCH, 0CCH);
			p.arrowsColor := Ports.RGBColor(022H, 022H, 022H);
			p.arrowsSize := 3 * Ports.point;
			p.disabledColor := Ports.white;
			p.leftGoto := TRUE;
			p.thumbMargin := 2;
			p.arrows := TRUE;
			p.InitDefaults := InitDefaults;
			p.Init := InitParam;
			p.Init(p);
		END
	END InitDefaults;

BEGIN
	NEW(stdSbDir);
	sbDir := stdSbDir;
	NEW(param);
	InitDefaults(param);
END StdScrollbars.
