MODULE StdStdCFrames;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, Ivan Denisov, Anton Dmitriev, Ketmar Dark"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Dialog, Fonts, Ports,  Views,  Strings, Controllers, Controls, Services, Dates, 
		StdCFrames, StdCmds, TextViews, TextModels, Models, Files, Meta, 
		StdDocuments, StdRasters;

	CONST
		DELETE* = 07X; BACKSPACE* = 08X; TAB* = 09X; LTAB* = 0AX;
		ENTER* = 0DX; ESC* = 1BX;
		PL* = 10X; PR* = 11X; (* ctrl+page up, ctrl+page down *)
		PU* = 12X; PD* = 13X; (* page up, page down *)
		DL* = 14X; DR* = 15X; (* home, end *)
		DU* = 16X; DD* = 17X; (* ctrl+home, ctrl+end *)
		AL* = 1CX; AR* = 1DX; AU* = 1EX; AD* = 1FX; (* arrows *)
		
		(* character classes for word searching *)
		classBlank* = 0; classWord* = 1; classNonWord* = 2;
		
		(* skin states; "selected" is the selection for an editor, or marked item for a list *)
		stNormal* = 0; stFront* = 1; stDisabled* = 2; stReadOnly* = 3;
		stSelectedFlag* = 4; stCursorFlag* = 8; stCursorSelectedFlag* = 12;
		
		doubleclickTimeout = 300;
		
		singleLineSize = 1024;
		
		listTabSize = 12;  (*  for formatted lists *)
		
		maxLayoutColumns = 10;
		
		numColors = 10;
		
		timeSep = ":";
		dateSep = "/";
		
	TYPE
		Storage = POINTER TO EXTENSIBLE RECORD
			view: Views.View;
			next: Storage;
		END;

		Param* = POINTER TO RECORD
		
			backPopup*,
			textPopup*,
		
			(* control (button, checkbox) *)
			back*,
			text*,

			(* control (button, checkbox) hover *)
			backHover*,
			textHover*,

			(* control (button, checkbox) disabled *)
			backDisabled*,
			textDisabled*,
			
			(* control (button, checkbox) disabled *)
			backReadOnly*,
			textReadOnly*,

			(* control (button, checkbox) pressed *)
			backPressed*,
			textPressed*,

			(* active editor *)
			backEditor*,
			textEditor*,

			backEditorSelection*,
			textEditorSelection*,

			(* non-focused editor *)
			backEditorBlured*,
			textEditorBlured*,

			backEditorBluredSelection*,
			textEditorBluredSelection*,

			(* disabled editor *)
			backEditorDisabled*,
			textEditorDisabled*,
			
			backEditorDisabledSelection*,
			textEditorDisabledSelection*,
			
			(* readonly editor *)
			backEditorReadOnly*,
			textEditorReadOnly*,

			backEditorReadOnlySelection*,
			textEditorReadOnlySelection*,

			(* list views *)
			backList*,
			textList*,
			(* selected items *)
			backSelList*,
			textSelList*,
			backListCursor*,
			textListCursor*,
			(* cursor for selected items *)
			backSelListCursor*,
			textSelListCursor*,

			(* disabled list views *)
			backListDisabled*,
			textListDisabled*,
			(* selected items *)
			backSelListDisabled*,
			textSelListDisabled*,
			backListCursorDisabled*,
			textListCursorDisabled*,
			(* cursor for selected items *)
			backSelListCursorDisabled*,
			textSelListCursorDisabled*,

			(* blured list views *)
			backListBlured*,
			textListBlured*,
			(* selected items *)
			backSelListBlured*,
			textSelListBlured*,
			backListCursorBlured*,
			textListCursorBlured*,
			(* cursor for selected items *)
			backSelListCursorBlured*,
			textSelListCursorBlured*,
			
			(* sliders *)
			backSlider*,  (* -slider-background-color- *)
			backPressedSlider*,  (* -slider-background-active-color- *)
			trackSlider*,  (* -slider-track-color- *)
			
			(* frames *)
			frame*,
			frameFocus*,
			frameReadOnly*,
			frameDisabled*,
			
			frameFace*,
			frameLight*,
			frameDark*,
			frameShadow*,
			frameSemi*,
			
			arrowDisabled*,
			arrowReadOnly*,
			
			(* scrollbars *)
			backScrollbar*, backScrollbarButtons*,
			knobScrollbar*, knobScrollbarPressed*,
			arrowsColor*: Ports.Color;
			scrollbarKnobMargin*, arrowsSize*, sbThickness*: INTEGER;
			
			checkBoxFont: Fonts.Font;
			
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;

		(* common tools *)

		(* used for margins, if you didn't figured it; in dot counts *)
		MarginRect = RECORD
			l, t, r, b: INTEGER
		END;

		SBPosChangedCB = PROCEDURE (sb: ScrollBarCommon; par: ANYPTR);

		ScrollBarCommon = POINTER TO ABSTRACT RECORD
			min, max, pos, pageSize, contentSize, arrowStep: INTEGER;
			(* size along the main dimension; calcucated by the owner *)
			length: INTEGER;
			(* render position, in pixels; used to fix mouse coords *)
			x0, y0: INTEGER
		END;

		PopupCommon = POINTER TO RECORD (Views.View)
			owner: StdCFrames.Frame; (* listbox or combobox *)
			monitorAction: MonitorPopupCommonAction;
			lst: ListCommon; (* shared with the parent *)
			dotsize, starty, hoveridx: INTEGER;
			hideCalled: BOOLEAN;
		END;

		InsertPopupCommonAction = POINTER TO RECORD (Services.Action)
			popup: PopupCommon;
			list: ListCommon;
			yofs, len: INTEGER;
		END;
		
		MonitorPopupCommonAction = POINTER TO RECORD (Services.Action)
			popup: PopupCommon;
		END;

		InsertPopupColorAction = POINTER TO RECORD (Services.Action)
			popup: PopupColor;
			cf: ColorField;
			yofs: INTEGER;
		END;

		MonitorPopupColorAction = POINTER TO RECORD (Services.Action)
			popup: PopupColor;
		END;

		PopupColor = POINTER TO RECORD (Views.View)
			monitorAction: MonitorPopupColorAction;
			colorField: ColorField;
			itemHeight, hoveridx, yShift: INTEGER;
			px, py: INTEGER;
			hideCalled: BOOLEAN;
		END;

		EditorCommonText = POINTER TO ARRAY OF CHAR;
		SingleLine = POINTER TO ARRAY singleLineSize OF CHAR;
		EditorCommonExpText = POINTER TO RECORD
			str: SingleLine;
			num, era: INTEGER;
			next: EditorCommonExpText
		END;
		TextStackItem = POINTER TO RECORD
			text: EditorCommonText;
			pos, from, to: INTEGER;
			next: TextStackItem
		END;

		(* common editor engine, shared between various input fields *)
		(* "not yet deremined" caret position is used after setting the selection.
			arrow keys will move the caret to the selection start, or to the selection end respectively. *)
		EditorCommon = POINTER TO RECORD
			ctl: StdCFrames.Frame;
			st: FieldStorage;
			text: EditorCommonText; (* 0-terminated, big *)
			textExp: EditorCommonExpText; (* exploded `text` *)
			textLen: INTEGER; (* cached text length *)
			textStars: EditorCommonText; (* used to replace fields with stars for passwords *)
			pos: INTEGER; (* caret position; -1 means "not yet determined" *)
			from, to: INTEGER; (* selection; from = to means "nothing selected" *)
			(* copy of fields from StdCFrames.Field *)
			maxLen, caretLine, lineHeight: INTEGER;
			left, right, multiLine, password: BOOLEAN;
			noMouseSelect: BOOLEAN; (* if TRUE, hold-and-drag mouse selection is disabled *)
			xoffs: INTEGER; (* render will start at x-xoffs *)
			x0, y0, w, h, pad-: INTEGER; (* render coordinates, don't include margin *)
			caretVisible: BOOLEAN; (* current caret state; used in `BlinkCaret` *)
			caretLastBlinkTime: LONGINT;
			changeCount: INTEGER; (* incremented on each text change; kind of Era *)
			outMargin: MarginRect; (* this is used in `Restore()` to clear "outer" margin; dot count *)
			lastDblTime: LONGINT; (* last doubleclick time, to detect triple click *)
			vsb: VScrollBar;
			vsbPressed: BOOLEAN;
			yoffs: INTEGER;
			shiftToCaret: BOOLEAN;  (* used to shift text to caret *)
		END;
		
		TextChain = POINTER TO RECORD
			text: EditorCommonText;
			next: TextChain
		END;
		
		ListCommonItem = RECORD
			text: EditorCommonText;
			tabbed: TextChain;
			marked: BOOLEAN
		END;

		ListCommonItemArray = POINTER TO ARRAY OF ListCommonItem;

		ListStorage = POINTER TO RECORD (Storage)
			yoffs: INTEGER;
			showOnRestore: BOOLEAN;
		END;
		(* list store and renderer, used by ListBox, ComboBox and SelectionBox *)
		ListCommon = POINTER TO RECORD
			st: ListStorage;
			ctl: StdCFrames.Frame;
			items: ListCommonItemArray;
			map, unmap: POINTER TO ARRAY OF INTEGER;
			widths: ARRAY maxLayoutColumns OF INTEGER;
			pos, movePos: INTEGER; (* cursor position *)
			yoffs, xoffs: INTEGER; (* render will start at y-xoffs *)
			x0, y0, w, h: INTEGER; (* render coordinates, don't include margin *)
			lastSearchTime: LONGINT;
			lastSearchPos, margins: INTEGER;
			searchString: ARRAY 16 OF CHAR;
			outMargin: MarginRect; (* this is used in `Restore()` to clear "outer" margin; dot count *)
			maxWidth, totalHeight: INTEGER; (* cached total width and height *)
			vsb: VScrollBar; hsb: HScrollBar;
			vsbPressed, hsbPressed, sorted, canMove: BOOLEAN;
		END;

		(* handy thing *)
		ClipRect = RECORD
			ox0, oy0, ox1, oy1: INTEGER; (* saved original *)
			f: Views.Frame
		END;

		(** for calling with Meta.CallWith *)
		LenParam = RECORD
			i: INTEGER
		END;

		IndexParam = RECORD
			i: INTEGER
		END;

		VScrollBar = POINTER TO RECORD (ScrollBarCommon)
			trackStartY, trackOfsY, trackSavedPos: INTEGER
		END;
		HScrollBar = POINTER TO RECORD (ScrollBarCommon)
			trackStartX, trackOfsX, trackSavedPos: INTEGER
		END;


		Directory = POINTER TO RECORD (StdCFrames.Directory) END;

		Caption = POINTER TO RECORD (StdCFrames.Caption)
			textExp: EditorCommonExpText; (* exploded `text` *)
			changeCount: INTEGER;
		END;

		Group = POINTER TO RECORD (StdCFrames.Group) END;

		PushButton = POINTER TO RECORD (StdCFrames.PushButton) END;

		CheckBox = POINTER TO RECORD (StdCFrames.CheckBox) END;
		
		RadioButtonStorage = POINTER TO RECORD (Storage)
			hasFocus: BOOLEAN;
		END;
		RadioButton = POINTER TO RECORD (StdCFrames.RadioButton) END;
		
		(* `ctl` in `ed`/`lst` is set on the first update; this is used in the code! *)
		ComboBox = POINTER TO RECORD (StdCFrames.ComboBox)
			ed: EditorCommon;
			lst: ListCommon;
			fixCursor: BOOLEAN; (* TRUE makes the cursor visible on render (and center it) *)
			closedTime: LONGINT;
			popupOverlay: PopupCommon
		END;
		
		FieldStorage = POINTER TO RECORD (Storage)
			textStack, forwardStack: TextStackItem;
		END;
		Field = POINTER TO RECORD (StdCFrames.Field)
			lastAction: LONGINT;
			ed: EditorCommon
		END;

		UpDownField = POINTER TO RECORD (StdCFrames.UpDownField)
			ed: EditorCommon;
			lastval: INTEGER
		END;

		ListBox = POINTER TO RECORD (StdCFrames.ListBox)
			lst: ListCommon;
			fixCursor: BOOLEAN; (* TRUE makes the cursor visible on render (and center it) *)
			closedTime: LONGINT;
			popupOverlay: PopupCommon
		END;
		
		SelectionBox = POINTER TO RECORD (StdCFrames.SelectionBox)
			lst: ListCommon;
			fixCursor: BOOLEAN (* TRUE makes the cursor visible on render (and center it) *)
		END;

		NodeItem = RECORD p: INTEGER; n: Dialog.TreeNode END;
	
		TreeFrame = POINTER TO RECORD (StdCFrames.TreeFrame)
			leftMargin, lineHeight, strHeight, totalHeight, maxWidth, 
			yoffs, xoffs, lines: INTEGER;
			vsb: VScrollBar; hsb: HScrollBar;
			vsbPressed, hsbPressed: BOOLEAN;
			nodesCache: POINTER TO ARRAY OF NodeItem;
			dontOpenFoldersForSelected: BOOLEAN;
		END;
		
		ScrollBar = POINTER TO RECORD (StdCFrames.ScrollBar)
			vsb: VScrollBar; hsb: HScrollBar;
			vsbPressed, hsbPressed: BOOLEAN
		END;

		TimeField = POINTER TO RECORD (StdCFrames.TimeField)
			ed: EditorCommon
		END;

		DateField = POINTER TO RECORD (StdCFrames.DateField)
			ed: EditorCommon;
			dateSep: CHAR;
			yearPart, monthPart, dayPart: INTEGER; (* first = 1, last = 3 *)
			del1, del2: INTEGER (* position of separators *)
		END;

		ColorField = POINTER TO RECORD (StdCFrames.ColorField)
			closedTime: LONGINT;
			popupOverlay: PopupColor
		END;
		
		ColorAction = POINTER TO RECORD (Dialog.GetColorAction)
			colorItem: Meta.Item;
		END;

	VAR
		focusControl*: StdCFrames.Field;
		param*: Param;
		revealPasswords*: BOOLEAN;
		colors*: ARRAY numColors OF Ports.Color;
		
		inHandleMouse*: BOOLEAN; (* for compatibility with Windows version *)
		
		storages: Storage;
	
		folder, folderopen, leaf: StdRasters.Model;

	PROCEDURE (VAR r: MarginRect) SetZero*, NEW;
	BEGIN
		r.l := 0; r.t := 0; r.r := 0; r.b := 0
	END SetZero;

	PROCEDURE (VAR r: MarginRect) SetUni* (size: INTEGER), NEW;
	BEGIN
		r.l := size; r.t := size; r.r := size; r.b := size
	END SetUni;

	(* doesn't include 0X, newline, paragraph, but includes nbsp and such *)
	PROCEDURE IsBlank (ch: CHAR): BOOLEAN;
	BEGIN
		(* 0DX is line, 0EX is para *)
		CASE ch OF
		| 01X .. 0CX, 0FX .. 20X
	 	, 08BX (* zwspace *)
		 , 08FX (* digitspace *)
		 , 0A0X: (* nbspace *)
			RETURN TRUE
		ELSE RETURN FALSE
		END
	END IsBlank;

	PROCEDURE ClassifyChar (ch: CHAR): INTEGER;
		VAR res: INTEGER;
	BEGIN
		CASE ch OF
		| 'A' .. 'Z', 'a' .. 'z', '0' .. '9', '_', '$': res := classWord
		| 0X .. 20X: res := classBlank
		ELSE
			IF IsBlank(ch) THEN res := classBlank
			ELSIF ch >= 80X THEN res := classWord
			ELSE res := classNonWord
			END
		END;
		RETURN res
	END ClassifyChar;

	(* k8: note that callind this will rebuild frames! *)
	PROCEDURE UpdateRoot* (c: StdCFrames.Frame);
		VAR root: Views.RootFrame; l, t, r, b: INTEGER;
	BEGIN
		(* this is wrong, because we don't need to update the whole main frame.
			besides, sometimes there isn't even one (design mode, for example, is *special*)
		(*root := Views.UltimateRootOf(c);*)
		root := Views.RootOf(c);
		IF root # NIL THEN
			l := root.l + root.gx; t := root.t + root.gy; r := root.r + root.gx; b := root.b + root.gy;
			Views.RestoreRoot(root, l, t, r, b)
			(*Views.ValidateRoot(root) — do not use, this may rebuild frames! *)
		END
	END UpdateRoot;

	PROCEDURE GetLen (VAR rec, par: ANYREC);
	BEGIN
		WITH rec: Dialog.Combo DO 
			WITH par: LenParam DO par.i := rec.len END
		| rec: Dialog.List DO 
			WITH par: LenParam DO par.i := rec.len END
		| rec: Dialog.Selection DO 
			WITH par: LenParam DO par.i := rec.len END
		END
	END GetLen;

	PROCEDURE Update* (VAR cb, par: ANYREC);
	BEGIN Dialog.Update(cb)
	END Update;

	PROCEDURE GetSortedInfo (c: StdCFrames.Frame): BOOLEAN;
		VAR res: BOOLEAN;
	BEGIN
		res := FALSE;
		IF c.view # NIL THEN
			WITH c: StdCFrames.ListBox DO res := c(StdCFrames.ListBox).sorted
			| c: StdCFrames.SelectionBox DO  res := c(StdCFrames.SelectionBox).sorted
			| c: StdCFrames.ComboBox DO res := c(StdCFrames.ComboBox).sorted
			END
		END;
		RETURN res
	END GetSortedInfo;
	
	PROCEDURE GetListLength (c: StdCFrames.Frame): INTEGER;
		VAR par: LenParam; con: Controls.Control; doit: BOOLEAN; res: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20);
		res := 0;
		IF c.view # NIL THEN
			WITH c: StdCFrames.ListBox DO doit := TRUE
			| c: StdCFrames.SelectionBox DO doit := TRUE
			| c: StdCFrames.ComboBox DO doit := TRUE
			ELSE doit := FALSE
			END;
			IF doit THEN
				con := c.view(Controls.Control);
				ASSERT(con # NIL);
				IF con.item.Valid() THEN
					con.item.CallWith(GetLen, par);
					res := MAX(0, par.i)
				END
			END
		END;
		RETURN res
	END GetListLength;

	PROCEDURE ThisIndex (VAR rec, par: ANYREC);
		VAR res, i: INTEGER; s: Dialog.String;
	BEGIN
		WITH par: IndexParam DO
			WITH rec: Dialog.Combo DO
				res := -1; i := 0;
				WHILE (res < 0) & (i < rec.len) DO
					rec.GetItem(i, s);
					IF s$ = rec.item$ THEN res := i END;
					INC(i)
				END;
				par.i := res
			END
		END
	END ThisIndex;

	PROCEDURE GetCursorIndex (c: StdCFrames.Frame): INTEGER;
		VAR con: Controls.Control; par: IndexParam; res: INTEGER;
	BEGIN
		res := -1;
		IF (c # NIL) & (c.view # NIL) THEN
			WITH c: StdCFrames.ListBox DO c.Get(c, res)
			| c: SelectionBox DO
				res := c.lst.pos
			| c: StdCFrames.ComboBox DO
				con := c.view(Controls.Control);
				ASSERT(con # NIL);
				IF con.item.Valid() THEN
					con.item.CallWith(ThisIndex, par);
					res := par.i
				END
			ELSE END
		END;
		RETURN res
	END GetCursorIndex;

	PROCEDURE GetItemText* (c: StdCFrames.Frame; idx: INTEGER; OUT s: ARRAY OF CHAR);
	BEGIN
		s := "";
		IF (c # NIL) & (c.view # NIL) THEN
			WITH c: StdCFrames.ListBox DO c.GetName(c, idx, s)
			| c: StdCFrames.SelectionBox DO c.GetName(c, idx, s)
			| c: StdCFrames.ComboBox DO c.GetName(c, idx, s)
			ELSE END
		END
	END GetItemText;


	(** **************** ClipRect **************** **)

	PROCEDURE (VAR c: ClipRect) Init* (f: Views.Frame), NEW;
	BEGIN
		IF (f # NIL) & (f.rider # NIL) THEN
			c.f := f;
			f.rider.GetRect(c.ox0, c.oy0, c.ox1, c.oy1)
		ELSE c.f := NIL
		END
	END Init;

	PROCEDURE (VAR c: ClipRect) Close*, NEW;
	BEGIN
		IF (c.f # NIL) & (c.f.rider # NIL) THEN
			c.f.rider.SetRect(c.ox0, c.oy0, c.ox1, c.oy1)
		END;
		c.f := NIL
	END Close;

	(* `x1` and `y1` are NOT included *)
	PROCEDURE (VAR c: ClipRect) ShrinkToXY* (x0, y0, x1, y1: INTEGER): BOOLEAN, NEW;
		VAR cx0, cy0, cx1, cy1: INTEGER; res: BOOLEAN;
	BEGIN
		res := FALSE;
		IF (c.f # NIL) & (c.f.rider # NIL) & (x0 <= x1) & (y0 <= y1) THEN
			(* transform selection rectangle to pixel coords *)
			x0 := (c.f.gx + x0) DIV c.f.unit;
			y0 := (c.f.gy + y0) DIV c.f.unit;
			x1 := (c.f.gx + x1) DIV c.f.unit;
			y1 := (c.f.gy + y1) DIV c.f.unit;
			c.f.rider.GetRect(cx0, cy0, cx1, cy1);
			IF (cx0 < cx1) & (cy0 < cy1) THEN
				IF (x1 <= cx0) OR (y1 <= cy0) OR
					(x0 >= cx1) OR (y0 >= cy1) THEN
					(* nothing to do *)
				ELSE (* rectangles intersects *)
					cx0 := MAX(cx0, x0);
					cy0 := MAX(cy0, y0);
					cx1 := MIN(cx1, x1);
					cy1 := MIN(cy1, y1);
					IF (cx0 < cx1) & (cy0 < cy1) THEN
						res := TRUE; c.f.rider.SetRect(cx0, cy0, cx1, cy1)
					ELSE
						c.f.rider.SetRect(cx0, cy0, cx0, cy0)
					END
				END
			END
		END;
		RETURN res
	END ShrinkToXY;

	PROCEDURE (VAR c: ClipRect) ShrinkToWH* (x0, y0, w, h: INTEGER): BOOLEAN, NEW;
		VAR res: BOOLEAN;
	BEGIN
		IF c.f # NIL THEN
			res := c.ShrinkToXY(x0, y0, x0 + w, y0 + h)
		ELSE res := FALSE
		END;
		RETURN res
	END ShrinkToWH;


	(** **************** common "onpress" code for buttons **************** **)

	PROCEDURE TrackMouseDown* (c: StdCFrames.Frame): BOOLEAN;
		VAR w, h, x, y: INTEGER; isDown, res: BOOLEAN; modifiers: SET;
	BEGIN
		res := FALSE;
		IF c.rider # NIL THEN
			(* k8: this hack repaints other controls, so they could indicate focus loss *)
			UpdateRoot(c);
			c.view.context.GetSize(w, h);
			c.pressed := TRUE;
			c.ForceUpdate;
			REPEAT
				c.Input(x, y, modifiers, isDown);
				IF c.pressed # ((x >= 0) & (y >= 0) & (x < w) & (y < h)) THEN
					c.pressed := ~c.pressed;
					c.ForceUpdate
				END
			UNTIL ~isDown;
			res := c.pressed;
			IF c.pressed THEN c.pressed := FALSE; c.ForceUpdate END
		END;
		RETURN res
	END TrackMouseDown;


	(** **************** ScrollBarCommon **************** **)

	PROCEDURE (sb: ScrollBarCommon) Init, NEW, EMPTY;

	PROCEDURE (sb: ScrollBarCommon) Draw (frm: Views.Frame; x, y, dim: INTEGER; pressed: BOOLEAN), NEW, ABSTRACT;

	PROCEDURE (sb: ScrollBarCommon) SBMouseDown (x, y: INTEGER): BOOLEAN, NEW, ABSTRACT;

	PROCEDURE (sb: ScrollBarCommon) SBMouseTrack (x, y: INTEGER), NEW, ABSTRACT;

	PROCEDURE (sb: ScrollBarCommon) Width(): INTEGER, NEW;
	BEGIN
		RETURN param.sbThickness
	END Width;

	PROCEDURE (sb: ScrollBarCommon) GetMinKnobSize (): INTEGER, NEW;
	BEGIN
		RETURN sb.Width()
	END GetMinKnobSize;

	PROCEDURE (sb: ScrollBarCommon) GetTotalSize (): INTEGER, NEW;
	BEGIN
		RETURN MAX(0, sb.max - sb.min + 1)
	END GetTotalSize;

	PROCEDURE (sb: ScrollBarCommon) CanFit (): BOOLEAN, NEW;
	BEGIN
		RETURN sb.length - sb.Width() * 2 - sb.GetMinKnobSize() > 0
	END CanFit;

	PROCEDURE (sb: ScrollBarCommon) CalcKnobSize (): INTEGER, NEW;
		VAR res, length: INTEGER;
	BEGIN
		length := sb.length - sb.Width() * 2;
		IF (length > 0) & (sb.pageSize > 0) & (sb.contentSize > 0) THEN
			(*
				knob size shows how much of the total is visible
				i.e. if total is 20, visible is 5 and the main dim is 60, then
				knob size is 5/20*60
			*)
			res := SHORT((LONG(sb.pageSize) * LONG(length)) DIV LONG(sb.contentSize)) 
		END;
		IF res < sb.GetMinKnobSize() THEN
			res := sb.GetMinKnobSize()
		END;
		RETURN res
	END CalcKnobSize;

	(* in pixels, inside the main bar *) 
	PROCEDURE (sb: ScrollBarCommon) CalcKnobPos (pos: INTEGER): INTEGER, NEW;
		VAR res, sz, kSize, scrollSize: INTEGER;
	BEGIN
		scrollSize := sb.length - sb.Width() * 2;
		IF (scrollSize < 1) OR (sb.min >= sb.max) THEN
			res := 0
		ELSE
			kSize := sb.CalcKnobSize();
			IF kSize > scrollSize THEN
				res := 0
			ELSE
				sz := sb.GetTotalSize();
				(* knob position is current visible page position on the length minus knob size *) 
				pos := MAX(sb.min, MIN(sb.max, pos));
				res := SHORT((LONG(scrollSize - kSize) * LONG(pos)) DIV LONG(sz))
			END
		END;
		RETURN res
	END CalcKnobPos;


	(** **************** EditorCommon (engine) **************** **)

	PROCEDURE (ed: EditorCommon) KeepText, NEW;
		VAR item: TextStackItem; i: INTEGER;
	BEGIN
		IF ed.st # NIL THEN
			NEW(item);
			NEW(item.text, LEN(ed.text));
			item.pos := ed.pos;
			item.from := ed.from;
			item.to := ed.to;
			FOR i := 0 TO LEN(ed.text$) - 1 DO
				item.text[i] := ed.text[i]
			END;
			IF ed.st.textStack = NIL THEN
				ed.st.textStack := item
			ELSE
				item.next := ed.st.textStack;
				ed.st.textStack := item
			END;
			ed.st.forwardStack := NIL;
		END
	END KeepText;

	PROCEDURE (ed: EditorCommon) Init, NEW;
	BEGIN
		ed.ctl := NIL;
		NEW(ed.text, 256);
		ed.text[0] := 0X; ed.textLen := 0;
		ed.textStars := NIL;
		ed.pos := -1; ed.from := 0; ed.to := MAX(INTEGER);
		ed.xoffs := 0; ed.x0 := 0; ed.y0 := 0; ed.w := 0; ed.h := 0;
		ed.maxLen := 255;
		ed.password := FALSE; ed.left := TRUE; ed.right := FALSE;
		ed.multiLine := FALSE;
		ed.caretLine := 1;
		ed.lineHeight := -1;  (* should be never used before Restore *)
		ed.caretVisible := FALSE;
		ed.caretLastBlinkTime := Services.Ticks();
		ed.changeCount := 0; ed.outMargin.SetZero;
		ed.lastDblTime := 0;
		ed.noMouseSelect := FALSE;
		ed.shiftToCaret := TRUE;
		ed.pad := 0;
	END Init;

	PROCEDURE (ed: EditorCommon) SetupCtl (ctl: StdCFrames.Frame), NEW;
	BEGIN
		ed.ctl := ctl
	END SetupCtl;

	PROCEDURE (ed: EditorCommon) Setup (ctl: StdCFrames.Frame; x0, y0, w, h: INTEGER), NEW;
	BEGIN
		ed.x0 := x0; ed.y0 := y0; ed.w := w; ed.h := h;
		ed.SetupCtl(ctl)
	END Setup;

	PROCEDURE (ed: EditorCommon) IsValid (): BOOLEAN, NEW;
	BEGIN
		RETURN (ed.ctl # NIL) & (ed.w > 0) & (ed.h > 0)
	END IsValid;

	PROCEDURE (ed: EditorCommon) NeedCaret (): BOOLEAN, NEW;
	BEGIN
		RETURN (ed.ctl # NIL) & ~ed.ctl.disabled & ~ed.ctl.readOnly
	END NeedCaret;

	(* return TRUE if the text was changed *)
	PROCEDURE (ed: EditorCommon) SetText (IN s: ARRAY OF CHAR): BOOLEAN, NEW;
		VAR update, wasAllSelected: BOOLEAN; slen, pos, nsz: INTEGER; nt: EditorCommonText;
	BEGIN
		IF LEN(s) = 0 THEN
			IF ed.textLen # 0 THEN
				IF LEN(ed.text) > 256 THEN NEW(ed.text, 256) END;
				ed.text[0] := 0X; ed.textLen := 0; update := TRUE
			END
		ELSE
			slen := MIN(ed.maxLen, LEN(s$));
			update := slen # ed.textLen;
			IF ~update THEN (* same length *)
				(* `s` can be longer than allowed, so use the loop instead of `s$ # ed.text$` *)
				pos := 0;
				WHILE ~update & (pos < slen) DO
					update := s[pos] # ed.text[pos];
					INC(pos)
				END
			END;
			IF update THEN
				(* need to reallocate the text storage? *)
				IF slen > LEN(ed.text) - 1 THEN
					nsz := MAX(256, MIN(slen * 2 + 1, ed.maxLen + 1));
					NEW(nt, nsz);
					ed.text := nt;
					ed.text[0] := 0X (* just in case *)
				END;
				(* `s` can be longer than allowed, so use the loop instead of `ed.text^ := s$` *)
				pos := 0;
				WHILE (pos < LEN(ed.text)) & (pos < LEN(s$)) DO
					ed.text[pos] := s[pos];
					INC(pos)
				END;
				ed.text[pos] := 0X;
				wasAllSelected := (ed.from = 0) & (ed.to >= ed.textLen) & ed.NeedCaret();
				ed.textLen := slen;
				IF wasAllSelected (* OR ed.ctl.readOnly *) THEN
					ed.pos := -1; ed.from := 0; ed.to := slen
				ELSE
					ed.pos := slen; ed.from := slen; ed.to := slen
				END
			END
		END;
		RETURN update
	END SetText;

	PROCEDURE (ed: EditorCommon) FixSelection, NEW;
		VAR slen: INTEGER;
	BEGIN
		IF ed.textLen # 0 THEN
			slen := ed.textLen;
			ed.from := MAX(0, MIN(slen, ed.from));
			IF (ed.to < 0) OR (ed.to > slen) THEN ed.to := slen END;
			IF ed.to < ed.from THEN slen := ed.from; ed.from := ed.to; ed.to := slen END
		ELSE ed.from := 0; ed.to := 0
		END
	END FixSelection;

	PROCEDURE (ed: EditorCommon) Select (from, to: INTEGER), NEW;
	BEGIN
		ed.from := from; ed.to := to; ed.pos := to;
		ed.FixSelection
	END Select;

	PROCEDURE (ed: EditorCommon) GetSelection (OUT from, to: INTEGER), NEW;
	BEGIN
		ed.FixSelection;
		from := ed.from; to := ed.to
	END GetSelection;
	
	PROCEDURE (ed: EditorCommon) GetVisText (): EditorCommonText, NEW;
		VAR res: EditorCommonText; f: INTEGER;
	BEGIN
		IF (ed.textLen = 0) OR ~ed.password THEN res := ed.text
		ELSE
			IF (ed.textStars = NIL) OR (LEN(ed.textStars) < LEN(ed.text)) THEN
				NEW(ed.textStars, LEN(ed.text))
			END;
			FOR f := 0 TO ed.textLen - 1 DO ed.textStars[f] := '*' END;
			ed.textStars[ed.textLen] := 0X;
			res := ed.textStars
		END;
		RETURN res
	END GetVisText;
	
	(* this doesn't change `ed.pos`, but returns suitable `pos` for rendering *)
	PROCEDURE (ed: EditorCommon) FixSelectionPos (): INTEGER, NEW;
		VAR pos, res: INTEGER; v: REAL;
	BEGIN
		IF ed.textLen # 0 THEN
			ed.FixSelection;
			IF ed.multiLine THEN
				pos := ed.pos;
				IF pos < 0 THEN
					IF ed.from = ed.to THEN pos := ed.textLen ELSE pos := ed.to END
				END;
				pos := MIN(pos, ed.textLen); (* sanitise it, just in case *)
				(* hack for numeric fields *)
				IF (ed.from <= 0) & (ed.to >= ed.textLen) THEN
					Strings.StringToReal(ed.text, v, res);
					IF res = 0 THEN pos := 0 END
				END
			ELSE
				pos := ed.pos;
				IF pos < 0 THEN
					IF ed.from = ed.to THEN pos := ed.textLen ELSE pos := ed.to END
				ELSE pos := MIN(pos, ed.textLen) (* sanitise it, just in case *)
				END;
				(* hack for numeric fields *)
				IF (ed.from <= 0) & (ed.to >= ed.textLen) THEN
					Strings.StringToReal(ed.text, v, res);
					IF res = 0 THEN pos := 0 END
				END
			END
		ELSE
			pos := 0; ed.from := 0; ed.to := 0
		END;
		RETURN pos
	END FixSelectionPos;

	PROCEDURE (ed: EditorCommon) RetainCaret, NEW;
	BEGIN
		ed.caretLastBlinkTime := Services.Ticks();
		ed.caretVisible := TRUE
	END RetainCaret;

	PROCEDURE (ed: EditorCommon) CalcCaretWidth (): INTEGER, NEW;
		VAR sw: INTEGER;
	BEGIN
		IF ed.ctl # NIL THEN
			IF Dialog.thickCaret THEN sw := ed.ctl.dot * 2 ELSE sw := ed.ctl.dot END
		ELSE
			sw := 1
		END;
		RETURN sw
	END CalcCaretWidth;

	PROCEDURE (ed: EditorCommon) CalcTextWidth (): INTEGER, NEW;
		VAR sw, maxWidth: INTEGER; tmp: EditorCommonExpText;
	BEGIN
		sw := 0;
		IF (ed.textLen # 0) & ed.IsValid() THEN
			IF ed.multiLine THEN
				maxWidth := 0;
				tmp := ed.textExp;
				WHILE tmp # NIL DO
					sw := ed.ctl.font.StringWidth(tmp.str);
					IF sw > maxWidth THEN maxWidth := sw END;
					tmp := tmp.next
				END;
				sw := maxWidth
			ELSE
				sw := ed.ctl.font.StringWidth(ed.GetVisText())
			END
		END;
		RETURN sw + ed.CalcCaretWidth()
	END CalcTextWidth;

	PROCEDURE (ed: EditorCommon) CalcXPos (): INTEGER, NEW;
		VAR x, sw: INTEGER;
	BEGIN
		IF ed.IsValid() THEN
			sw := ed.CalcTextWidth();
			IF ed.left THEN
				x := ed.pad
			ELSIF ed.right THEN
				x := (ed.w - sw) - ed.pad
			ELSE
				x := (ed.w - sw) DIV 2
			END
		ELSE x := 0
		END;
		RETURN x
	END CalcXPos;
	
	PROCEDURE (ed: EditorCommon) MakeCaretVisible, NEW;
		VAR pos, sw, x, addwdt: INTEGER;
	BEGIN
		(* for multiline it has no sense to shift in X direction, because text should always be split  *)
		IF ed.multiLine THEN
			ed.xoffs := 0;
			RETURN
		END;
		
		pos := ed.FixSelectionPos();
		IF (ed.textLen # 0) & ed.IsValid() THEN
			IF pos = 0 THEN ed.xoffs := 0
			ELSE
				sw := ed.CalcTextWidth();
				IF sw < ed.w THEN
					ed.xoffs := 0
				ELSE
					addwdt := ed.pad * 2;
					
					x := ed.CalcXPos();
					x := ed.ctl.CharPos(x, pos, ed.GetVisText(), ed.ctl.font);
					DEC(x, ed.xoffs);
					(* now check if the caret is visible *)
					IF x - addwdt < 0 THEN
						INC(ed.xoffs, x - addwdt)
					ELSE
						INC(x, ed.CalcCaretWidth() + addwdt);
						IF x > ed.w THEN
							INC(ed.xoffs, x - ed.w)
						END
					END;
					ed.xoffs := MIN(ed.xoffs, sw - ed.w + addwdt);
					ed.xoffs := MAX(ed.xoffs, 0)
				END
			END
		ELSE ed.xoffs := 0
		END
	END MakeCaretVisible;

	PROCEDURE (ed: EditorCommon) NormaliseXOffsEd, NEW;
		VAR pos, sw: INTEGER;
	BEGIN
		pos := ed.FixSelectionPos();
		IF (ed.textLen # 0) & ed.IsValid() THEN
			IF pos = 0 THEN ed.xoffs := 0
			ELSE
				sw := ed.CalcTextWidth();
				IF sw < ed.w THEN ed.xoffs := 0
				ELSE
					ed.xoffs := MIN(ed.xoffs, sw - ed.w);
					ed.xoffs := MAX(ed.xoffs, 0)
				END
			END
		ELSE ed.xoffs := 0
		END
	END NormaliseXOffsEd;


	PROCEDURE (ed: EditorCommon) PrepareExplodedText, NEW;
		VAR
			pos, pos2, lastSpacePos, lastSpacePos2, line, vsbSize, width: INTEGER;
			str: SingleLine; lineBreak: BOOLEAN;
			tx: EditorCommonText; tmp: EditorCommonExpText;
		
		PROCEDURE AddStringToList (str: SingleLine);
			VAR n: INTEGER;
		BEGIN
			IF ed.textExp = NIL THEN
				NEW(ed.textExp);
				ed.textExp.era := ed.changeCount;
				ed.textExp.str := str;
				ed.textExp.num := 1;
			ELSE
				n := 2;
				tmp := ed.textExp;
				WHILE tmp.next # NIL DO
					INC(n);
					tmp := tmp.next;
				END;
				NEW(tmp.next);
				tmp.next.str := str;
				tmp.next.num := n;
			END			
		END AddStringToList;
		
	BEGIN
		IF (ed.textExp # NIL) & (ed.textExp.era = ed.changeCount) THEN RETURN END;
		ed.textExp := NIL;
		tx := ed.GetVisText();
		IF ed.vsb # NIL THEN
			vsbSize := MAX(0, ed.vsb.Width())
		ELSE
			vsbSize := 0
		END;
		width := ed.w - vsbSize - ed.x0 - ed.pad * 2;
		pos := 0;
		line := 0;
		WHILE (tx[pos] # 0X) & (line < 1024) DO  (* prevent hanging in case of trubles with parsing *) 
			pos2 := 0;
			NEW(str);
			lineBreak := FALSE;
			lastSpacePos := 0; lastSpacePos2 := 0;
			WHILE (tx[pos] # 0X) & (~lineBreak) DO
				IF tx[pos] = " " THEN
					lastSpacePos := pos;
					lastSpacePos2 := pos2;
				END;
				str[pos2] := tx[pos];
				str[pos2+1] := 0X;
				IF ed.ctl.font.StringWidth(str) > width THEN
					lineBreak := TRUE;
					IF lastSpacePos # 0 THEN
						(* forward count untill space *)
						pos := lastSpacePos + 1;
						pos2 := lastSpacePos2 + 1
					END
				ELSIF tx[pos] = ENTER THEN
					lineBreak := TRUE;
					INC(pos); INC(pos2)
				ELSE
					INC(pos); INC(pos2)
				END
			END;
			str[pos2] := 0X;
			AddStringToList(str);
			INC(line)
		END;
		IF (pos >= 1) & (tx[pos-1] = ENTER) THEN
			NEW(str);
			str[0] := 0X;
			AddStringToList(str)
		END
	END PrepareExplodedText;


	(* returns active string and position of caret in it *)
	PROCEDURE (ed: EditorCommon) GetStr (atPos: INTEGER;
		OUT str: EditorCommonExpText; OUT pos: INTEGER), NEW;
		VAR tmp: EditorCommonExpText; len, lenPrev (* , state *) : INTEGER;
	BEGIN
		ed.PrepareExplodedText;
		(* state := -1; *)
		tmp := ed.textExp;
		len := 0; lenPrev := 0;
		WHILE tmp # NIL DO
			lenPrev := len;
			len := len + LEN(tmp.str$);
			IF (len = atPos) & (ed.text[atPos-1] # ENTER) THEN
				(* end of line without return *)
				(* state := 1; *)
				str := tmp;
				pos := LEN(tmp.str$);
				tmp := NIL
			ELSIF (len = atPos) & (ed.text[atPos-1] = ENTER) THEN
				(* end of line with return *)
				(* state := 2; *)
				str := tmp.next;
				pos := 0;
				tmp := NIL
			ELSIF atPos < len THEN
				(* state := 3; *)
				str := tmp;
				pos := atPos - lenPrev;
				tmp := NIL
			ELSE
				(* state := 4; *)
				IF tmp.next = NIL THEN
					(* atPos is more than text length *)
					str := tmp;
					pos := LEN(str.str$)
				END;
				tmp := tmp.next
			END
		END
	END GetStr;

	PROCEDURE (ed: EditorCommon) DefineCaretLine, NEW;
		VAR tmp: EditorCommonExpText; pos: INTEGER;
	BEGIN
		IF ed.textExp = NIL THEN 
			ed.caretLine := 1
		ELSE
			ed.GetStr(ed.pos, tmp, pos);
			IF tmp = NIL THEN
				ed.caretLine := 1
			ELSE
				ed.caretLine := tmp.num
			END
		END
	END DefineCaretLine;

	(* this must remove caret if called second time;
		caret visibility flag is changed accordingly *)
	PROCEDURE (ed: EditorCommon) DrawCaret, NEW;
		VAR asc, dsc, sw, dot, pos, vsbSize, x, y0, y1: INTEGER;
			clip: ClipRect; str: EditorCommonExpText;
	BEGIN
		ed.caretVisible := ~ed.caretVisible;
		ed.ctl.font.GetBounds(asc, dsc, sw);
		dot := ed.ctl.dot;
		
		IF ed.vsb # NIL THEN
			vsbSize := MAX(0, ed.vsb.Width())
		ELSE
			vsbSize := 0
		END;
		
		sw := ed.CalcTextWidth();
		x := ed.CalcXPos();
		DEC(x, ed.xoffs);
		IF ed.multiLine THEN
			y0 := ed.pad + (dsc + asc) + (ed.caretLine - 1) * ed.lineHeight;
			IF ed.vsb # NIL THEN
				y0 := y0 - ed.vsb.pos
			END
		ELSE
			y0 := ed.y0 + (ed.h - dsc + asc) DIV 2
		END;
		DEC(y0, asc);
		y1 := y0 + asc + dsc;
		y0 := MAX(ed.y0, y0);
		y1 := MIN(ed.y0 + ed.h - 2 * dot, y1);
		IF y0 < y1 THEN
			(* caret *)
			clip.Init(ed.ctl);
			IF clip.ShrinkToWH(ed.x0, ed.y0, ed.w - vsbSize, ed.h) THEN
				pos := ed.FixSelectionPos();
				IF pos # 0 THEN
					IF ed.multiLine THEN
						ed.GetStr(ed.pos, str, pos);
						x := ed.ctl.CharPos(x, pos, str.str, ed.ctl.font)
					ELSE
						x := ed.ctl.CharPos(x, pos, ed.GetVisText(), ed.ctl.font)
					END
				END;
				sw := ed.CalcCaretWidth();
				ed.ctl.MarkRect(ed.x0+x, y0, ed.x0+x+sw, y1, Ports.fill, Ports.invert, ed.caretVisible)
			END;
			clip.Close
		END
	END DrawCaret;

	PROCEDURE (ed: EditorCommon) NormaliseYOffsEd(): INTEGER, NEW;
		VAR lineNum: INTEGER; tmp: EditorCommonExpText; textHeight: INTEGER;
	BEGIN
		IF ed.yoffs < 0 THEN
			ed.yoffs := 0
		END;
		lineNum := 0;
		tmp := ed.textExp;
		WHILE tmp # NIL DO
			INC(lineNum);
			tmp := tmp.next
		END;
		textHeight := lineNum * ed.lineHeight + 4 * ed.pad;
		IF textHeight < ed.h THEN
			ed.yoffs := 0
		ELSIF textHeight - ed.h < ed.yoffs THEN
			ed.yoffs :=  textHeight - ed.h
		END;
		RETURN lineNum
	END NormaliseYOffsEd;

	(* background should be already painted by the owner *)
	PROCEDURE (ed: EditorCommon) Restore, NEW;
		VAR c: StdCFrames.Frame; clip: ClipRect;
			dot, asc, dsc, sw, sl, sr, textCol, bgCol, selTextCol, selBgCol, 
			posFrom, posTo, liHei, n, nStrings, len, vsbSize,
			x, y, px0, py0, px1, py1: INTEGER;
			ch: CHAR;
			tx: EditorCommonText; vsb: VScrollBar;
			tmp, strFrom, strTo: EditorCommonExpText; replaced: CHAR;
			updateText: BOOLEAN;
	BEGIN
		IF ed.IsValid() THEN
			
			c := ed.ctl;
			c.font.GetBounds(asc, dsc, sw);
			ed.lineHeight := asc + dsc;
			
			IF ed.multiLine THEN
				ed.xoffs := 0;
				ed.PrepareExplodedText;
				
				IF ed.shiftToCaret THEN
					WHILE ed.caretLine * ed.lineHeight > ed.h + ed.yoffs DO;
						INC(ed.yoffs, ed.lineHeight);
					END;
					WHILE ed.caretLine * ed.lineHeight < ed.yoffs + ed.lineHeight DIV 2 DO;
						DEC(ed.yoffs, ed.lineHeight);
					END;
					ed.shiftToCaret := FALSE;
				END;
			
				nStrings := ed.NormaliseYOffsEd();

				(* check if we need a scrollbar, and prepare it *)
				vsbSize := (nStrings * ed.lineHeight) + ed.pad * 4; (* 2 was not good, so make 4 *)
				IF vsbSize > ed.h THEN
					IF ed.vsb = NIL THEN
						NEW(vsb);
						vsb.Init;
						ed.vsb := vsb;
						updateText := TRUE
					ELSE
						vsb := ed.vsb;
						updateText := FALSE
					END;
					
					vsb.min := 0;
					vsb.contentSize := vsbSize;
					vsb.max := vsb.contentSize - ed.h;
					vsb.pos := ed.yoffs;
					vsb.pageSize := ed.h;
					vsb.arrowStep := ed.lineHeight;
					vsb.length := ed.h;
					IF ~vsb.CanFit() THEN vsb := NIL END;
					
					IF vsb # NIL THEN
						vsbSize := MAX(0, vsb.Width());
						IF (vsbSize = 0) OR ((vsbSize + 4 * Ports.point) >= ed.w) THEN
							vsbSize := 0; vsb := NIL
						END
					END
				ELSE
					ed.vsb := NIL; vsb := NIL; vsbSize := 0
				END;
				IF ed.vsb # NIL THEN
					IF updateText THEN
						(* update text after scrollbar appears *)
						INC(ed.changeCount);
						ed.PrepareExplodedText;
						nStrings := ed.NormaliseYOffsEd();
						ed.vsb.contentSize := (nStrings * ed.lineHeight)  + ed.pad * 4;
					END;
					ed.vsb.length := ed.h
				END
			ELSE
				ed.MakeCaretVisible
			END;
			
			sw := ed.CalcTextWidth();
			x := ed.CalcXPos();
			DEC(x, ed.xoffs);
			IF ed.multiLine THEN
				y := ed.pad + ed.lineHeight - ed.yoffs
			ELSE
				y := ed.y0 + (ed.h - dsc + asc) DIV 2
			END;
			
			(* map skin according coltrol state *)
			IF ed.ctl.disabled THEN
				textCol := param.textEditorDisabled;
				bgCol := param.backEditorDisabled;
				selTextCol := param.textEditorDisabledSelection;
				selBgCol := param.backEditorDisabledSelection;
			ELSIF ed.ctl.readOnly THEN
				textCol := param.textEditorReadOnly;
				bgCol := param.backEditorReadOnly;
				selTextCol := param.textEditorReadOnlySelection;
				selBgCol := param.backEditorReadOnlySelection;
			ELSIF ed.ctl.mark THEN
				textCol := param.textEditor;
				bgCol := param.backEditor;
				selTextCol := param.textEditorSelection;
				selBgCol := param.backEditorSelection;
			ELSE
				textCol := param.textEditorBlured;
				bgCol := param.backEditorBlured;
				selTextCol := param.textEditorBluredSelection;
				selBgCol := param.backEditorBluredSelection;
			END;
			
			dot := ed.ctl.dot;
			
			IF ed.ctl.bgColor # -1 THEN
				bgCol := ed.ctl.bgColor 
			END;
			
			ed.ctl.DrawRect(ed.x0 - ed.outMargin.l * dot, ed.y0 - ed.outMargin.t * dot,
						ed.x0 + ed.w + ed.outMargin.r * dot, ed.y0 + ed.h + ed.outMargin.b * dot,
						Ports.fill, bgCol);
			
			tx := ed.GetVisText();
			clip.Init(ed.ctl);
			IF clip.ShrinkToWH(ed.x0, ed.y0, ed.w, ed.h) THEN
				IF ed.multiLine THEN
					n := y;
					tmp := ed.textExp;
					WHILE tmp # NIL DO
						IF tmp.str$ # "" THEN
							len := LEN(tmp.str$)-1;
							IF tmp.str[len] = 0DX THEN
								tmp.str[len] := 0X;
								c.DrawString(ed.x0 + x, n, textCol, tmp.str, c.font);
								tmp.str[len] := 0DX
							ELSE
								c.DrawString(ed.x0 + x, n, textCol, tmp.str, c.font)
							END
						END;
						INC(n, ed.lineHeight);
						tmp := tmp.next
					END
				ELSE
					c.DrawString(ed.x0 + x, y, textCol, tx, c.font)
				END
			END;
			clip.Close;
			
			(* selection *)
			IF c.front & ~c.disabled & (ed.from # ed.to) & (c.rider # NIL) THEN
				IF ed.multiLine THEN
					ed.GetStr(ed.from, strFrom, posFrom);
					ed.GetStr(ed.to, strTo, posTo);
					
					IF strFrom = strTo THEN
						
						tmp := strFrom;
						IF (tmp # NIL) & (tmp.str$ # "") THEN
							len := LEN(tmp.str$) - 1;
							IF tmp.str[len] = 0DX THEN
								replaced := 0DX;
								tmp.str[len] := 0X
							ELSE
								replaced := 0X
							END;
							ch := tmp.str[posFrom];
							tmp.str[posFrom] := 0X;
							sl := c.font.StringWidth(tmp.str);
							tmp.str[posFrom] := ch;
							(* text length with the selection *)
							ch := tmp.str[posTo];
							tmp.str[posTo] := 0X;
							sr := c.font.StringWidth(tmp.str);
							tmp.str[posTo] := ch;
							IF sr > sl THEN
								clip.Init(c);
								(* transform selection rectangle to the actual coords*)
								px0 := ed.x0 + x + sl;
								px1 := ed.x0 + x + sr + c.dot;
								liHei := (tmp.num - 1) * ed.lineHeight;
								py0 := y + liHei - asc;
								py1 := y + liHei + dsc;
								IF clip.ShrinkToXY(px0, py0, px1, py1) THEN
									(* draw selection bar *)
									c.DrawRect(px0, py0, px1, py1, Ports.fill, selBgCol);
									(* and the text again, with the proper color *)
									c.DrawString(ed.x0 + x, y + liHei, selTextCol, tmp.str, c.font);
								END;
								(* restore old clip rectangle *)
								clip.Close
							END;
							IF replaced = 0DX THEN
								tmp.str[len] := 0DX
							END
						END
						
					ELSIF strFrom.num < strTo.num THEN
						
						tmp := strFrom;
						IF tmp.str$ # "" THEN
							len := LEN(tmp.str$) - 1;
							IF tmp.str[len] = 0DX THEN
								replaced := 0DX;
								tmp.str[len] := 0X
							ELSE
								replaced := 0X
							END;
							ch := tmp.str[posFrom];
							tmp.str[posFrom] := 0X;
							sl := c.font.StringWidth(strFrom.str);
							tmp.str[posFrom] := ch;
							sr := c.font.StringWidth(strFrom.str);
							IF sr > sl THEN
								clip.Init(c);
								(* transform selection rectangle to the actual coords*)
								px0 := ed.x0 + x + sl;
								px1 := ed.x0 + x + sr + c.dot;
								liHei := (tmp.num - 1) * ed.lineHeight;
								py0 := y + liHei - asc;
								py1 := y + liHei + dsc;
								IF clip.ShrinkToXY(px0, py0, px1, py1) THEN
									c.DrawRect(px0, py0, px1, py1, Ports.fill, selBgCol);
									c.DrawString(ed.x0 + x, y + liHei, selTextCol, tmp.str, c.font);
								END;
								(* restore old clip rectangle *)
								clip.Close
							END;
							IF replaced = 0DX THEN
								tmp.str[len] := 0DX
							END
						END;
						
						tmp := strFrom.next;
						FOR n := strFrom.num + 1 TO strTo.num - 1 DO
							IF tmp.str$ # "" THEN
								len := LEN(tmp.str$) - 1;
								IF tmp.str[len] = 0DX THEN
									replaced := 0DX;
									tmp.str[len] := 0X
								ELSE
									replaced := 0X
								END;
								sl := 0;
								sr := c.font.StringWidth(tmp.str);
								IF sr > sl THEN
									clip.Init(c);
									(* transform selection rectangle to the actual coords*)
									px0 := ed.x0 + x + sl;
									px1 := ed.x0 + x + sr + c.dot;
									liHei := (tmp.num - 1) * ed.lineHeight;
									py0 := y + liHei - asc;
									py1 := y + liHei + dsc;
									IF clip.ShrinkToXY(px0, py0, px1, py1) THEN
										c.DrawRect(px0, py0, px1, py1, Ports.fill, selBgCol);
										c.DrawString(ed.x0 + x, y + liHei, selTextCol, tmp.str, c.font);
									END;
									(* restore old clip rectangle *)
									clip.Close
								END;
								IF replaced = 0DX THEN
									tmp.str[len] := 0DX
								END
							END;
							tmp := tmp.next
						END;
						
						tmp := strTo;
						IF tmp.str$ # "" THEN
							len := LEN(tmp.str$) - 1;
							IF tmp.str[len] = 0DX THEN
								replaced := 0DX;
								tmp.str[len] := 0X
							ELSE
								replaced := 0X
							END;
							
							sl := 0;
							ch := tmp.str[posTo];
							tmp.str[posTo] := 0X;
							sr := c.font.StringWidth(tmp.str);
							tmp.str[posTo] := ch;
							IF sr > sl THEN
								clip.Init(c);
								(* transform selection rectangle to the actual coords*)
								px0 := ed.x0 + x + sl;
								px1 := ed.x0 + x + sr + c.dot;
								liHei := (tmp.num - 1) * ed.lineHeight;
								py0 := y + liHei - asc;
								py1 := y + liHei + dsc;
								IF clip.ShrinkToXY(px0, py0, px1, py1) THEN
									c.DrawRect(px0, py0, px1, py1, Ports.fill, selBgCol);
									c.DrawString(ed.x0 + x, y + liHei, selTextCol, tmp.str, c.font);
								END;
								(* restore old clip rectangle *)
								clip.Close
							END;
							IF replaced = 0DX THEN
								tmp.str[len] := 0DX
							END
						END
					END;
					
				ELSE
					(* single line *)
					(* text length before the selection *)
					ch := tx[ed.from];
					tx[ed.from] := 0X;
					sl := c.font.StringWidth(tx);
					tx[ed.from] := ch;
					(* text length with the selection *)
					ch := tx[ed.to];
					tx[ed.to] := 0X;
					sr := c.font.StringWidth(tx);
					tx[ed.to] := ch;
					IF sr > sl THEN
						clip.Init(c);
						(* transform selection rectangle to the actual coords*)
						px0 := ed.x0 + x + sl;
						px1 := ed.x0 + x + sr + c.dot;
						py0 := y - asc;
						py1 := y + dsc;
						IF clip.ShrinkToXY(px0, py0, px1, py1) THEN
							(* draw selection bar *)
							c.DrawRect(px0, py0, px1, py1, Ports.fill, selBgCol);
							(* and the text again, with the proper color *)
							c.DrawString(ed.x0 + x, y, selTextCol, tx, c.font);
						END;
						(* restore old clip rectangle *)
						clip.Close
					END
				END
			END;
			
			(* caret *)
			IF c.front & (ed.from = ed.to) & ed.NeedCaret() THEN
				IF ed.caretVisible THEN
					ed.caretVisible := FALSE; (* hack, because it is changed in `DrawCaret()` *)
					ed.DefineCaretLine;
					ed.DrawCaret
				END
			ELSE
				ed.caretVisible := FALSE;
				ed.caretLastBlinkTime := 0
			END;
			
			IF vsb # NIL THEN
				vsb.x0 := ed.x0 + ed.w - vsbSize;
				vsb.y0 := ed.y0;
				vsb.Draw(c, vsb.x0, vsb.y0, ed.h, ed.vsbPressed)
			END
		END
	END Restore;


	PROCEDURE (ed: EditorCommon) BlinkCaret, NEW;
		VAR newTime: LONGINT;
	BEGIN
		ed.FixSelection;
		IF ed.IsValid() & (ed.from = ed.to) & ed.ctl.front & ed.NeedCaret() THEN
			newTime := Services.Ticks();
			IF newTime > ed.caretLastBlinkTime + Dialog.caretPeriod THEN
				ed.DrawCaret;
				ed.caretLastBlinkTime := newTime
			END
		ELSE
			ed.caretVisible := ed.NeedCaret();
			ed.caretLastBlinkTime := 0
		END
	END BlinkCaret;

	PROCEDURE (ed: EditorCommon) GetSelectedText(): EditorCommonText, NEW;
		VAR res: EditorCommonText; f, pos: INTEGER;
	BEGIN
		IF ed.textLen # 0 THEN
			ed.FixSelection;
			IF ed.from = ed.to THEN
				NEW(res, ed.textLen + 1);
				res^ := ed.text$
			ELSE
				NEW(res, ed.to - ed.from + 1);
				f := 0;
				pos := ed.from + f;
				WHILE (pos < ed.to) & (pos < LEN(ed.text) - 1) DO
					res[f] := ed.text[pos];
					INC(f);
					pos := ed.from + f;
				END;
				res[f] := 0X
			END
		ELSE
			NEW(res, 1);
			res[0] := 0X
		END;
		RETURN res
	END GetSelectedText;

	PROCEDURE (ed: EditorCommon) RemoveSelectedText, NEW;
		VAR f, e: INTEGER;
	BEGIN
		IF ed.textLen # 0 THEN
			ed.FixSelection;
			f := ed.from; e := ed.to;
			IF f # e THEN
				WHILE ed.text[e] # 0X DO
					ed.text[f] := ed.text[e];
					INC(f); INC(e)
				END;
				ed.text[f] := 0X;
				ed.to := ed.from;
				ASSERT(f = LEN(ed.text$));
				ed.textLen := f;
				ed.pos := ed.from;
				(* set update flag *)
				INC(ed.changeCount);
				e := ed.NormaliseYOffsEd()
			END
		END
	END RemoveSelectedText;

	(* return FALSE if there is no room *)
	PROCEDURE (ed: EditorCommon) InsertChar (ch: CHAR): BOOLEAN, NEW;
		VAR nt: EditorCommonText; slen, newlen, pos, f: INTEGER; res: BOOLEAN;
	BEGIN
		res := TRUE;
		IF (ch # 0X) THEN
			slen := ed.textLen;
			IF slen < ed.maxLen THEN
				(* grow the buffer *)
				IF slen + 1 >= LEN(ed.text) THEN
					ASSERT(slen + 1 < 020000000H); (* just in case *)
					newlen := MIN(LEN(ed.text) * 2, ed.maxLen + 1);
					NEW(nt, newlen);
					nt^ := ed.text$;
					ed.text := nt
				END;
				pos := ed.FixSelectionPos();
				(* insert the char *)
				ed.text[slen + 1] := 0X;
				f := slen; WHILE f > pos DO ed.text[f] := ed.text[f - 1]; DEC(f) END;
				ed.text[pos] := ch;
				INC(ed.textLen);
				(* fix selection *)
				IF ed.from # ed.to THEN
					IF (pos >= ed.from) & (pos < ed.to) THEN INC(ed.to) END
				END;
				(* fix position *)
				ed.pos := pos + 1;
				(* set update flag *)
				INC(ed.changeCount)
			ELSE res := FALSE
			END
		END;
		RETURN res
	END InsertChar;

	PROCEDURE (ed: EditorCommon) Edit (op: INTEGER; VAR v: Views.View; VAR clipboard: BOOLEAN), NEW;
		VAR rd: TextModels.Reader; seltext: EditorCommonText; insok: BOOLEAN;
	BEGIN
		IF clipboard THEN
			CASE op OF
			| Controllers.copy:
				IF ~ed.password THEN
					seltext := ed.GetSelectedText();
					StdCmds.clipboardHook.Register(
						TextViews.dir.New(TextModels.dir.NewFromString(seltext)), 0, 0, FALSE);
					seltext := NIL
				END
			| Controllers.cut:
				IF (ed.from # ed.to) & ~ed.password THEN
					seltext := ed.GetSelectedText();
					StdCmds.clipboardHook.Register(
						TextViews.dir.New(TextModels.dir.NewFromString(seltext)), 0, 0, FALSE);
					seltext := NIL;
					ed.RemoveSelectedText
				END
			| Controllers.paste:
				IF v # NIL THEN
					WITH v: TextViews.View DO
						ed.KeepText;
						rd := v.ThisModel().NewReader(NIL); rd.SetPos(0);
						ed.RemoveSelectedText; insok := TRUE;
						REPEAT
							rd.Read;
							IF rd.view = NIL THEN
								IF (rd.char = TextModels.line) OR (rd.char = TextModels.para) THEN
									insok := ed.InsertChar(rd.char)
								ELSIF rd.char = TAB THEN
									insok := ed.InsertChar(" ");
									insok := ed.InsertChar(" ");
									insok := ed.InsertChar(" ");
								ELSE
									insok := ed.InsertChar(rd.char)
								END
							END
						UNTIL insok & rd.eot;
						IF ~insok THEN Dialog.Beep END
					ELSE END
				END
			ELSE END
		END
	END Edit;

	(* return position of the first char of the word *)
	PROCEDURE (ed: EditorCommon) WordBack (pos: INTEGER): INTEGER, NEW;
		VAR cc: INTEGER;
	BEGIN
		pos := MAX(0, MIN(pos, ed.textLen));
		IF pos > 0 THEN
			IF ed.password THEN DEC(pos)
			ELSE
				cc := ClassifyChar(ed.text[pos - 1]);
				DEC(pos);
				WHILE (pos > 0) & (cc = ClassifyChar(ed.text[pos - 1])) DO
					DEC(pos)
				END
			END
		END;
		RETURN pos
	END WordBack;

	(* return position after the last char of the word *)
	PROCEDURE (ed: EditorCommon) WordForward (pos: INTEGER): INTEGER, NEW;
		VAR cc: INTEGER;
	BEGIN
		pos := MAX(0, MIN(pos, ed.textLen));
		IF pos < ed.textLen THEN
			IF ed.password THEN
				INC(pos)
			ELSE
				cc := ClassifyChar(ed.text[pos]);
				INC(pos);
				WHILE (pos < ed.textLen) & (cc = ClassifyChar(ed.text[pos])) DO
					INC(pos)
				END
			END
		END;
		RETURN pos
	END WordForward;
	

	PROCEDURE SelectAll*;
		VAR ed: EditorCommon;
	BEGIN
		IF focusControl # NIL THEN
			WITH focusControl: Field DO
				ed := focusControl.ed;
				IF (ed.from # 0) OR (ed.to # ed.textLen) THEN
					INC(ed.changeCount);
					ed.from := 0; ed.to := ed.textLen;
					ed.ctl.ForceUpdate
				END
			ELSE
				
			END
		END
	END SelectAll;

	PROCEDURE Cut*;
		VAR ed: EditorCommon; seltext: EditorCommonText;
	BEGIN
		IF focusControl # NIL THEN
			WITH focusControl: Field DO
				ed := focusControl.ed;
				IF (ed.from # ed.to) & ~ed.password THEN
					seltext := ed.GetSelectedText();
					StdCmds.clipboardHook.Register(
						TextViews.dir.New(TextModels.dir.NewFromString(seltext)), 0, 0, FALSE);
					seltext := NIL;
					ed.RemoveSelectedText;
					focusControl.Set(focusControl, ed.text);
					ed.ctl.ForceUpdate
				END
			ELSE
			END
		END
	END Cut;

	PROCEDURE Copy*;
		VAR ed: EditorCommon; seltext: EditorCommonText;
	BEGIN
		IF focusControl # NIL THEN
			WITH focusControl: Field DO
				ed := focusControl.ed;
				IF (ed.from # ed.to) & ~ed.password THEN
					seltext := ed.GetSelectedText();
					StdCmds.clipboardHook.Register(
						TextViews.dir.New(TextModels.dir.NewFromString(seltext)), 0, 0, FALSE);
					seltext := NIL
				END
			ELSE
			END
		END
	END Copy;

	PROCEDURE Paste*;
	(*
		VAR ed: EditorCommon; type: Stores.TypeName; v: Views.View; w, h: INTEGER; s: BOOLEAN;
		rd: TextModels.Reader; seltext: EditorCommonText; insok: BOOLEAN;
		oldChangeCount: INTEGER;
	BEGIN
		IF focusControl # NIL THEN
			WITH focusControl: Field DO
				ed := focusControl.ed;
				WinClipboard.GetClipView(type, v, w, h, s);
				IF v # NIL THEN
					WITH v: TextViews.View DO
						rd := v.ThisModel().NewReader(NIL); rd.SetPos(0);
						oldChangeCount := ed.changeCount;
						ed.RemoveSelectedText; insok := TRUE;
						ed.KeepText;
						REPEAT
							rd.Read;
							IF rd.view = NIL THEN
								IF (rd.char = TextModels.line) OR (rd.char = TextModels.para) THEN
									insok := ed.InsertChar(rd.char)
								ELSIF rd.char = TAB THEN
									insok := ed.InsertChar(" ");
									insok := ed.InsertChar(" ");
									insok := ed.InsertChar(" ");
								ELSE
									insok := ed.InsertChar(rd.char)
								END
							END
						UNTIL insok & rd.eot;
						IF ~insok THEN Dialog.Beep END;
						IF ed.changeCount # oldChangeCount THEN
							focusControl.Set(focusControl, ed.text)
						END;
						ed.RetainCaret;
						ed.ctl.ForceUpdate
					ELSE END
				END
			END
		END
		*)
	END Paste;

	PROCEDURE Delete*;
		VAR ed: EditorCommon;
	BEGIN
		IF focusControl # NIL THEN
			WITH focusControl: Field DO
				ed := focusControl.ed;
				IF ed.from # ed.to THEN
					ed.RemoveSelectedText;
					focusControl.Set(focusControl, ed.text);
					ed.RetainCaret;
					ed.ctl.ForceUpdate
				END
			ELSE
			END
		END
	END Delete;


	PROCEDURE SelectAllGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := TRUE;
		IF focusControl # NIL THEN
			par.disabled := ~ (focusControl IS Field)
		END;
	END SelectAllGuard;

	PROCEDURE CutGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := TRUE;
		IF (focusControl # NIL) & ~focusControl.readOnly & ~focusControl.disabled THEN
			par.disabled := ~ (focusControl IS Field) & ~ focusControl(Field).password
		END;
	END CutGuard;

	PROCEDURE CopyGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := TRUE;
		IF focusControl # NIL THEN
			par.disabled := ~ (focusControl IS Field) & ~ focusControl(Field).password
		END;
	END CopyGuard;

	PROCEDURE PasteGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := TRUE;
		IF (focusControl # NIL) & ~focusControl.readOnly & ~focusControl.disabled THEN
			par.disabled := ~ (focusControl IS Field)
		END;
	END PasteGuard;

	PROCEDURE (ed: EditorCommon) KeepTextToForwardStack, NEW;
		VAR item: TextStackItem; i: INTEGER;
	BEGIN
		IF ed.st # NIL THEN
			NEW(item);
			NEW(item.text, LEN(ed.text));
			item.pos := ed.pos;
			item.from := ed.from;
			item.to := ed.to;
			FOR i := 0 TO LEN(ed.text$) - 1 DO
				item.text[i] := ed.text[i]
			END;
			IF ed.st.forwardStack = NIL THEN
				ed.st.forwardStack := item
			ELSE
				item.next := ed.st.forwardStack;
				ed.st.forwardStack := item
			END
		END
	END KeepTextToForwardStack;

	PROCEDURE (ed: EditorCommon) TimeBack, NEW;
	BEGIN
		IF ed.st # NIL THEN
			IF ed.st.textStack # NIL THEN
				ed.KeepTextToForwardStack;
				ed.text := ed.st.textStack.text;
				ed.textLen := LEN(ed.text$);
				INC(ed.changeCount);
				ed.pos := ed.st.textStack.pos;
				ed.from := ed.st.textStack.from;
				ed.to := ed.st.textStack.to;
				ed.st.textStack := ed.st.textStack.next
			END
		END
	END TimeBack;

	PROCEDURE (ed: EditorCommon) TimeForward, NEW;
	BEGIN
		IF ed.st # NIL THEN
			IF ed.st.forwardStack # NIL THEN
				ed.text := ed.st.forwardStack.text;
				ed.textLen := LEN(ed.text$);
				INC(ed.changeCount);
				ed.pos := ed.st.forwardStack.pos;
				ed.from := ed.st.forwardStack.from;
				ed.to := ed.st.forwardStack.to;
				ed.st.forwardStack := ed.st.forwardStack.next
			END
		END
	END TimeForward;

	PROCEDURE (ed: EditorCommon) KeyDown (ch: CHAR; modifiers: SET), NEW;
		VAR pos, npos, p1, p2: INTEGER; tx: EditorCommonText;
			tmp: EditorCommonExpText; ok: BOOLEAN;
	BEGIN
		pos := ed.FixSelectionPos();
		CASE ch OF
		CHR(64977): (* Ctrl + Z *)
			ed.TimeBack
		| 019X: (* Ctrl + Y *)
			ed.TimeForward
		| CHR(64978): (* Ctrl + A *)
			IF (ed.from # 0) OR (ed.to # ed.textLen) THEN
				INC(ed.changeCount);
				ed.from := 0; ed.to := ed.textLen;
			END
		| BACKSPACE: (* backspace *)
			IF ed.from = ed.to THEN
				IF pos > 0 THEN
					ed.to := pos; 
					IF Controllers.modify IN modifiers THEN (* Ctrl *)
						(* word *)
						ed.from := ed.WordBack(pos)
					ELSE ed.from := pos - 1
					END
				END
			END;
			ed.KeepText;
			ed.RemoveSelectedText;
			ed.DefineCaretLine;
			ed.shiftToCaret := TRUE;
		| DELETE: (* delete *)
			IF ed.from = ed.to THEN
				IF pos < ed.textLen THEN ed.to := pos + 1; ed.from := pos END
			END;
			ed.KeepText;
			ed.RemoveSelectedText;
			ed.DefineCaretLine;
			ed.shiftToCaret := TRUE;
		| DL: (* home *)
			IF Controllers.extend IN modifiers THEN (* Shift *)
				IF ed.from = ed.to THEN ed.to := pos END
			ELSE ed.to := 0
			END;
			ed.pos := 0; ed.from := 0
		| DR: (* end *)
			IF ed.multiLine THEN
				IF Controllers.extend IN modifiers THEN (* Shift *)
					IF ed.from = ed.to THEN ed.from := ed.pos END;
					tx := ed.GetVisText();
					WHILE (tx[ed.pos] # ENTER) & (tx[ed.pos] # 0X) DO
						INC(ed.pos)
					END;
					ed.to := ed.pos;
				ELSE
					tx := ed.GetVisText();
					WHILE (tx[ed.pos] # ENTER) & (tx[ed.pos] # 0X) DO
						INC(ed.pos)
					END;
					ed.from := ed.pos; ed.to := ed.pos
				END;
			ELSE
				IF Controllers.extend IN modifiers THEN (* Shift *)
					IF ed.from = ed.to THEN ed.from := pos END
				ELSE
					ed.from := ed.textLen
				END;
				ed.pos := ed.textLen; ed.to := ed.pos
			END
		| AU: (* arrow up *)
			IF ed.multiLine THEN
				IF ed.caretLine > 1 THEN
					(* selection *)
					IF ed.from # ed.to THEN
						IF Controllers.extend IN modifiers THEN 
							npos := ed.to
						ELSE
							ed.from := 0;
							ed.to := 0;
						END
					ELSE
						IF Controllers.extend IN modifiers THEN
							npos := ed.pos
						END
					END;
					(* calculate new position *)
					ed.GetStr(ed.pos, tmp, p1);
					ed.pos := ed.pos - p1 - 1;
					tx := ed.GetVisText();
					ed.GetStr(ed.pos, tmp, p2);
					IF p1 > LEN(tmp.str$) THEN
						ed.pos := ed.pos
					ELSE
						ed.pos := ed.pos - LEN(tmp.str$) + p1 + 1
					END;
					IF Controllers.extend IN modifiers THEN 
						ed.from := ed.pos;
						ed.to := npos
					END;
					ed.DefineCaretLine;
					ed.shiftToCaret := TRUE;
				END
			END
		| AD: (* arrow down *)
			IF ed.multiLine THEN
				tx := ed.GetVisText();
				IF tx[0] # 0X THEN
					ed.GetStr(ed.pos, tmp, p1);
					(* selection *)
					IF ed.from # ed.to THEN
						IF Controllers.extend IN modifiers THEN 
							npos := ed.from;
						ELSE
							ed.from := 0;
							ed.to := 0;
						END
					ELSE
						IF Controllers.extend IN modifiers THEN
							npos := ed.pos
						END
					END;
					(* calculate new position *)
					ed.pos := ed.pos - p1 + LEN(tmp.str$);
					ed.GetStr(ed.pos, tmp, p2);
					IF LEN(tmp.str$) > p1 THEN
						ed.pos := ed.pos + p1
					ELSE
						ed.pos := ed.pos + LEN(tmp.str$) - 1
					END;
					IF Controllers.extend IN modifiers THEN 
						ed.from := npos;
						ed.to := ed.pos
					END;
					IF ed.pos > ed.textLen THEN ed.pos := ed.textLen END;
					ed.DefineCaretLine;
					ed.shiftToCaret := TRUE;
				END
			END
		| AL: (* arrow left *)
			IF (ed.textLen = 0) OR (pos = 0) THEN
				IF ~(Controllers.extend IN modifiers) THEN
					ed.from := 0; ed.to := 0
				END;
				ed.pos := 0
			ELSE
				
				
				IF ed.from = ed.to THEN ed.from := pos; ed.to := pos END;
				(* calculate new cursor position *)
				IF Controllers.modify IN modifiers THEN
					npos := ed.WordBack(pos)
				ELSE
					npos := MAX(0, pos - 1)
				END;
				(* extend selection *)
				IF Controllers.extend IN modifiers THEN
					IF ed.pos = ed.to THEN
						ed.to := npos
					ELSE
						ed.from := npos
					END
				ELSE
					IF ed.from # ed.to THEN
						npos := ed.from
					END;
					ed.from := ed.to
				END;
				ed.pos := npos
			END;
			ed.DefineCaretLine;
			ed.shiftToCaret := TRUE;
		| AR: (* arrow right *)
			IF (ed.textLen = 0) OR (pos >= ed.textLen) THEN
				IF ~(Controllers.extend IN modifiers) THEN
					ed.from := ed.textLen;
					ed.to := ed.textLen
				END;
				ed.pos := ed.textLen
			ELSE
				IF ed.from = ed.to THEN
					ed.from := pos;
					ed.to := pos
				END;
				(* calculate new cursor position *)
				IF Controllers.modify IN modifiers THEN
					npos := ed.WordForward(pos)
				ELSE
					npos := MIN(ed.textLen, pos + 1)
				END;
				(* extend selection *)
				IF Controllers.extend IN modifiers THEN
					IF ed.pos = ed.from THEN
						ed.from := npos
					ELSE
						ed.to := npos
					END
				ELSE
					IF (ed.from # ed.to) & (ed.to # MAX(INTEGER)) & (ed.to # 0) THEN
						npos := ed.to
					END;
					ed.from := ed.to
				END;
				ed.pos := npos
			END;
			IF ed.pos > ed.textLen THEN ed.pos := ed.textLen END;
			ed.DefineCaretLine;
			ed.shiftToCaret := TRUE;
			
		ELSE (* insert char *)
			IF ch = TAB THEN
				ed.KeepText;
				ed.RemoveSelectedText;
				ok := ed.InsertChar(" ");
				ok := ed.InsertChar(" ");
				ok := ed.InsertChar(" ");
				IF ~ok THEN Dialog.Beep END;
				ed.DefineCaretLine;
				ed.shiftToCaret := TRUE;
			ELSIF ed.multiLine & (ch = ENTER) THEN
				tx := ed.GetVisText();
				ed.KeepText;
				ed.RemoveSelectedText;
				npos := ed.pos;
				WHILE (npos > 0) & (tx[npos] # ENTER) DO
					DEC(npos)
				END;
				p1 := 0;
				INC(npos);
				WHILE tx[npos] = " " DO
					INC(p1);
					INC(npos);
				END;
				ok := ed.InsertChar(ch);
				IF ok & (p1 > 0) THEN
					FOR npos := 0 TO p1 - 1 DO
						ok := ed.InsertChar(" ")
					END
				END;
				IF ~ok THEN Dialog.Beep END;
				ed.DefineCaretLine;
				ed.shiftToCaret := TRUE;
			ELSIF ((ch >= 20X) & (ch # 07FX)) THEN
				ed.KeepText;
				ed.RemoveSelectedText;
				IF ~ed.InsertChar(ch) THEN Dialog.Beep END;
				ed.DefineCaretLine;
				ed.shiftToCaret := TRUE;
			END
		END
	END KeyDown;

	(* `x` is in global ctl coords, not in editor coords *)
	PROCEDURE (ed: EditorCommon) CharAtXY (x, y: INTEGER): INTEGER, NEW;
		VAR sx, sw, pos, sl, sr, len, lineNum: INTEGER;
			tx: EditorCommonText; tmp: EditorCommonExpText;
	BEGIN
		IF ed.textLen # 0 THEN
			(* convert `x` to editor coords *)
			DEC(x, ed.x0);
			sw := ed.CalcTextWidth() - ed.CalcCaretWidth();
			sx := ed.CalcXPos();
			DEC(sx, ed.xoffs);
			IF ed.multiLine THEN
				lineNum := 0; len := 0;
				tmp := ed.textExp;
				WHILE (tmp # NIL) & (lineNum < (y + ed.yoffs - ed.lineHeight DIV 2) DIV ed.lineHeight) DO
					INC(lineNum);
					INC(len, LEN(tmp.str$));
					tmp := tmp.next
				END;
				IF tmp = NIL THEN
					pos := ed.textLen
				ELSE
					sx := ed.ctl.CharIndex(sx, x, tmp.str, ed.ctl.font);
					IF (sx > 0) & (tmp.str[sx - 1] = 0DX) THEN 
						pos := len + sx - 1
					ELSE
						pos := len + sx
					END
				END
			ELSE
				IF x <= 0 THEN
					pos := 0
				ELSIF x >= sx + sw THEN
					pos := ed.textLen
				ELSE
					tx := ed.GetVisText();
					pos := ed.ctl.CharIndex(sx, x, tx, ed.ctl.font);
					sl := ed.ctl.CharPos(sx, pos, tx, ed.ctl.font);
					sr := ed.ctl.CharPos(sx, pos + 1, tx, ed.ctl.font);
					IF x >= sl + (sr - sl) DIV 2 THEN INC(pos) END;
					pos := MAX(0, MIN(pos, ed.textLen))
				END
			END
		ELSE pos := 0
		END;
		RETURN pos
	END CharAtXY;


	PROCEDURE (ed: EditorCommon) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN), NEW;
	BEGIN
		IF ed.IsValid() & (x >= ed.x0) & (y >= ed.y0) 
			& (x < ed.x0 + ed.w) & (y < ed.y0 + ed.h) THEN
			done := TRUE;
			IF op = Controllers.decLine THEN nofLines := - nofLines END;
			IF ed.multiLine THEN
				INC(ed.yoffs, ed.lineHeight * nofLines);
				nofLines := ed.NormaliseYOffsEd()
			ELSE
				(* CHECK for varios aligments *)
				INC(ed.xoffs, Ports.mm * 2 * nofLines);
				ed.NormaliseXOffsEd
			END
		END
	END WheelMove;

	PROCEDURE (l: EditorCommon) EdTrackVSBMouse (), NEW;
		VAR c: StdCFrames.Frame; x, y, ox, oy, opos: INTEGER; isDown: BOOLEAN; modifiers: SET;
	BEGIN
		c := l.ctl;
		IF c.rider # NIL THEN
			(* k8: this hack repaints other controls, so they could indicate focus loss *)
			UpdateRoot(c);
			l.vsbPressed := TRUE;
			l.ctl.ForceUpdate;
			opos := l.vsb.pos;
			ox := MIN(INTEGER); oy := MIN(INTEGER);
			REPEAT
				c.Input(x, y, modifiers, isDown);
				x := (x - l.x0) - l.vsb.x0;
				y := (y - l.y0) - l.vsb.y0;
				IF ox = MIN(INTEGER) THEN ox := x END;
				IF oy = MIN(INTEGER) THEN oy := y END;
				IF (ox # x) OR (oy # y) THEN
					ox := x; oy := y;
					l.vsb.SBMouseTrack(x, y);
					IF opos # l.vsb.pos THEN
						opos := l.vsb.pos;
						l.yoffs := opos;
						l.ctl.ForceUpdate
					END
				END
			UNTIL ~isDown;
			l.vsbPressed := FALSE;
			l.ctl.ForceUpdate
		END
	END EdTrackVSBMouse;

	PROCEDURE (ed: EditorCommon) MouseDown (x, y: INTEGER; buttons: SET), NEW;
		VAR isDown, triple: BOOLEAN; modifiers: SET; tstart: LONGINT;
			movedto, sx, pos, cc, ox, oy: INTEGER;
	BEGIN
	
		IF (ed.vsb # NIL) & (ed.vsb.length > 0) & (ed.x0 + ed.w - x < ed.vsb.Width()) THEN
			IF ed.vsb.SBMouseDown((x - ed.x0) - ed.vsb.x0, (y - ed.y0) - ed.vsb.y0) THEN
				ed.EdTrackVSBMouse
			ELSE
				ed.yoffs := ed.vsb.pos
				(* the caller will repaint us *)
			END
			
		ELSIF (ed.textLen # 0) & ed.IsValid() &
				(x >= ed.x0) & (y >= ed.y0) & (x < ed.x0 + ed.w) & (y < ed.y0 + ed.h) THEN
			
			(*sw := ed.CalcTextWidth();*)
			sx := ed.CalcXPos();
			DEC(sx, ed.xoffs);
			
			IF Controllers.extend IN buttons THEN (* Shift Click *)
				
				movedto := ed.CharAtXY(x, y);
				IF movedto # ed.pos THEN
					IF (ed.from = 0) & (ed.to = 0) THEN
						IF movedto > ed.pos THEN
							ed.from := ed.pos;
							ed.pos := movedto;
							ed.to := movedto;
						ELSE
							ed.to := ed.pos;
							ed.pos := movedto;
							ed.from := movedto;
						END
					ELSE
						IF ed.pos = ed.from THEN
							IF movedto < ed.to THEN
								ed.pos := movedto;
								ed.from := ed.pos
							ELSE
								ed.from := ed.to;
								ed.pos := movedto;
								ed.to := ed.pos
							END
						ELSE (* ed.pos = ed.to  *)
							IF movedto > ed.from THEN
								ed.pos := movedto;
								ed.to := ed.pos
							ELSE
								ed.to := ed.from;
								ed.pos := movedto;
								ed.from := ed.pos
							END
						END
						
					END
				END
				
			ELSE
			
				ed.pos := ed.CharAtXY(x, y);
				IF ed.multiLine THEN ed.DefineCaretLine END;
				ed.from := ed.pos; ed.to := ed.pos;
				pos := ed.FixSelectionPos();
				
				IF ~ed.noMouseSelect THEN
					(* select word with double click *)
					IF Controllers.doubleClick IN buttons THEN
						ASSERT(pos <= ed.textLen);
						IF (ed.textLen # 0) & ~ed.password THEN
							IF pos = ed.textLen THEN
								(* from the end of the text *)
								ed.to := pos;
								cc := ClassifyChar(ed.text[pos - 1]);
								DEC(pos);
								WHILE (pos > 0) & (cc = ClassifyChar(ed.text[pos - 1])) DO
									DEC(pos)
								END;
								ed.from := pos; ed.pos := pos
							ELSE (* inside a word or a blank span *)
								ed.from := pos; cc := ClassifyChar(ed.text[pos]);
								WHILE (ed.from > 0) & (cc = ClassifyChar(ed.text[ed.from - 1])) DO
									DEC(ed.from)
								END;
								WHILE (pos < ed.textLen) & (cc = ClassifyChar(ed.text[pos])) DO
									INC(pos)
								END;
								ed.to := pos; ed.pos := pos
							END
						END;
						ed.lastDblTime := Services.Ticks()
					ELSIF ed.ctl.rider # NIL THEN
						(*Views.ValidateRoot(Views.RootOf(ed.ctl));*)
						(* k8: this hack repaints other controls, so they could indicate focus loss *)
						UpdateRoot(ed.ctl);
						ed.pos := pos;
						pos := ed.pos; ox := x - 1; oy := y - 1;
						tstart := Services.Ticks();
						triple := (tstart - ed.lastDblTime <= doubleclickTimeout);
						IF triple THEN
							(* select all *)
							ed.from := 0; ed.to := ed.textLen
						END;
						ed.lastDblTime := 0;
						REPEAT
							ed.ctl.Input(x, y, modifiers, isDown);
							IF (x # ox) OR (y # oy) THEN
								ox := x; oy := y;
								movedto := ed.CharAtXY(x, y);
								IF (movedto = pos) OR (Services.Ticks() - tstart < 150) THEN
									IF ~triple THEN ed.from := 0; ed.to := 0 END
								ELSE
									IF movedto > pos THEN
										ed.from := pos;
										ed.to := movedto;
										IF ed.to > ed.textLen THEN ed.to := ed.textLen END
									ELSE
										ed.from := movedto;
										IF ed.from < 0 THEN ed.from := 0 END;
										ed.to := pos
									END
								END;
								ed.pos := movedto;
								ed.RetainCaret;
								IF ed.multiLine THEN
									ed.DefineCaretLine;
									ed.shiftToCaret := TRUE;
								END;
								ed.ctl.ForceUpdate
							END
						UNTIL (ed.ctl.rider = NIL) OR ~isDown;
					END
				END
			END
		END
	END MouseDown;


	(** **************** ListCommon (engine) **************** **)
	
	
	PROCEDURE (l: ListCommon) Init, NEW;
	BEGIN
		l.ctl := NIL;
		l.items := NIL;
		l.pos := 0;
		l.movePos := -1;
		l.margins := 2;
		l.yoffs := 0; l.x0 := 0; l.y0 := 0; l.w := 0; l.h := 0;
		l.lastSearchTime := 0;
		l.lastSearchPos := 0;
		l.outMargin.SetZero;
		l.maxWidth := 0; l.totalHeight := 0;
		l.vsb := NIL;
		l.sorted := FALSE;
	END Init;

	PROCEDURE (l: ListCommon) Idx (i: INTEGER): INTEGER, NEW;
	BEGIN
		IF l.sorted & (l.map # NIL) THEN
			RETURN l.map[i]
		ELSE
			RETURN i
		END
	END Idx;

	PROCEDURE (l: ListCommon) IdxBack (i: INTEGER): INTEGER, NEW;
	BEGIN
		IF l.sorted & (l.unmap # NIL) THEN
			RETURN l.unmap[i]
		ELSE
			RETURN i
		END
	END IdxBack;

	PROCEDURE (l: ListCommon) GetSelectionBox (): StdCFrames.SelectionBox, NEW;
		VAR sb: StdCFrames.SelectionBox;
	BEGIN
		IF (l.ctl # NIL) & (l.ctl IS StdCFrames.SelectionBox) THEN sb := l.ctl(StdCFrames.SelectionBox)
		ELSE sb := NIL
		END;
		RETURN sb
	END GetSelectionBox;

	PROCEDURE (l: ListCommon) GetLength (): INTEGER, NEW;
		VAR len: INTEGER;
	BEGIN
		IF l.items # NIL THEN len := LEN(l.items) ELSE len := 0 END;
		RETURN len
	END GetLength;

	PROCEDURE (l: ListCommon) RecalcCachedSizes, NEW;
		VAR asc, dsc, sw, f, col: INTEGER; tmp: TextChain; 
	BEGIN
		IF (l.ctl # NIL) & (l.ctl.font # NIL) THEN
			l.maxWidth := 0;
			FOR f := 0 TO l.GetLength() - 1 DO
				IF l.items[f].tabbed # NIL THEN
					sw := 0;
					col := 0;
					tmp := l.items[f].tabbed;
					WHILE tmp # NIL DO
						sw := sw + l.widths[col];
						IF tmp.next # NIL THEN
							sw := sw + listTabSize * Ports.point
						END;
						tmp := tmp.next;
						INC(col)
					END
				ELSE
					sw := l.ctl.font.StringWidth(l.items[f].text);
				END;
				IF sw > l.maxWidth THEN
					l.maxWidth := sw
				END
			END;
			l.ctl.font.GetBounds(asc, dsc, sw);
			l.totalHeight := (asc + dsc) * l.GetLength()
		ELSE
			l.maxWidth := 0;
			l.totalHeight := 0;
		END
	END RecalcCachedSizes;

	PROCEDURE (l: ListCommon) SetupCtl (ctl: StdCFrames.Frame), NEW;
	BEGIN
		IF l.ctl # ctl THEN
			l.ctl := ctl;
			IF ctl # NIL THEN l.RecalcCachedSizes END
		END
	END SetupCtl;

	PROCEDURE (l: ListCommon) SetupList (ctl: StdCFrames.Frame; x0, y0, w, h: INTEGER), NEW;
	BEGIN
		l.x0 := x0; l.y0 := y0; l.w := w; l.h := h;
		l.SetupCtl(ctl)
	END SetupList;

	PROCEDURE (l: ListCommon) IsValidList (): BOOLEAN, NEW;
	BEGIN
		RETURN (l.ctl # NIL) & (l.w > 0) & (l.h > 0)
	END IsValidList;

	PROCEDURE (l: ListCommon) QuickSort, NEW;
	
		PROCEDURE LessThan (ai, bi: INTEGER): BOOLEAN;
		VAR res, equal: BOOLEAN; i: INTEGER; a, b: ListCommonItem;
		BEGIN
			a := l.items[ai];
			b := l.items[bi];
			res := FALSE;
			equal := TRUE;
			i := 0;
			WHILE equal & (i < LEN(a.text$)) & (i < LEN(b.text$)) DO
				IF ORD(CAP(a.text[i])) = ORD(CAP(b.text[i])) THEN
					(* pass *)
				ELSIF ORD(CAP(a.text[i])) < ORD(CAP(b.text[i])) THEN
					res := TRUE;
					equal := FALSE
				ELSE
					equal := FALSE
				END;
				INC(i)
			END;
			RETURN res 
		END LessThan;

		PROCEDURE Sort(L, R: INTEGER);
			VAR i, j: INTEGER;  w, x: INTEGER;
		BEGIN
			i := L;  j := R;
			x := l.map[(L+R) DIV 2];
			REPEAT
				WHILE LessThan(l.map[i], x) DO  i := i + 1;  END;
				WHILE LessThan(x, l.map[j]) DO  j := j - 1;  END;
				IF i <= j THEN
					w := l.map[i];
					l.map[i] := l.map[j];
					l.map[j] := w;
					i := i + 1;
					j := j -1;
				END;
			UNTIL i > j;
			IF L < j THEN Sort(L, j) END;
			IF i < R THEN Sort(i, R) END
		END Sort;
		
	BEGIN
		Sort(0, LEN(l.map) - 1);
	END QuickSort;

	PROCEDURE (l: ListCommon) UpdateFromCtl (): BOOLEAN, NEW;
		VAR f, oldlen, newlen, n: INTEGER;
			s: Dialog.String; res, needUpdate, markup, mark, newSorted: BOOLEAN;
			sb: StdCFrames.SelectionBox;
	BEGIN
		res := FALSE;
		IF l.ctl # NIL THEN
			sb := l.GetSelectionBox();
			
			newSorted := GetSortedInfo(l.ctl);
			newlen := GetListLength(l.ctl);
			oldlen := l.GetLength();
			needUpdate := (oldlen # newlen) OR (newSorted # l.sorted);
			l.sorted := newSorted;
			
			IF l.ctl IS StdCFrames.ListBox THEN 
				l.canMove := l.ctl(StdCFrames.ListBox).canMove
			ELSE
				l.canMove := FALSE;
			END;
			l.movePos := -1;
			
			markup := FALSE;
			IF ~needUpdate THEN
				f := 0; WHILE ~needUpdate & (f < newlen) DO
					ASSERT(f < oldlen);
					GetItemText(l.ctl, f, s);
					Dialog.MapString(s, s);
					needUpdate := (l.items[f].text$ # s$);
					IF ~needUpdate & ~markup & (sb # NIL) THEN
						sb.Get(sb, f, mark);
						markup := (l.items[f].marked # mark)
					END;
					INC(f)
				END
			END;
			
			IF needUpdate OR (newlen # oldlen) THEN
				IF newlen # oldlen THEN
					IF newlen # 0 THEN NEW(l.items, newlen) ELSE l.items := NIL END
				END;
				FOR f := 0 TO newlen - 1 DO
					GetItemText(l.ctl, f, s);
					Dialog.MapString(s, s);
					NEW(l.items[f].text, LEN(s$) + 1);
					l.items[f].text^ := s$;
					IF sb # NIL THEN sb.Get(sb, f, mark)
					ELSE mark := FALSE
					END;
					l.items[f].marked := mark
				END;
				
				IF l.sorted & (newlen # 0) THEN
					IF (l.map = NIL) OR (LEN(l.map) # LEN(l.items)) THEN
						NEW(l.map, LEN(l.items));
						NEW(l.unmap, LEN(l.items))
					END;
					FOR f := 0 TO newlen - 1 DO
						l.map[f] := f
					END;
					l.QuickSort;
					FOR n := 0 TO newlen - 1 DO
						l.unmap[l.map[n]] := n
					END
				END;
				
				l.pos := GetCursorIndex(l.ctl);
				l.pos := MAX(-1, MIN(l.pos, newlen - 1));
				
				l.RecalcCachedSizes;
				res := TRUE
			ELSE
				IF markup THEN
					ASSERT(sb # NIL);
					FOR f := 0 TO oldlen - 1 DO
						sb.Get(sb, f, mark);
						l.items[f].marked := mark
					END
				END;
				f := GetCursorIndex(l.ctl);
				IF f # l.pos THEN
					l.pos := MAX(0, MIN(f, newlen - 1));
					res := TRUE
				END
			END
		END;
		RETURN res
	END UpdateFromCtl;

	PROCEDURE (l: ListCommon) GetItemHeight (): INTEGER, NEW;
		VAR asc, dsc, sw: INTEGER;
	BEGIN
		IF (l.GetLength() # 0) & (l.ctl # NIL) THEN
			l.ctl.font.GetBounds(asc, dsc, sw);
			sw := asc + dsc
		ELSE sw := 1
		END;
		RETURN sw
	END GetItemHeight;

	PROCEDURE (l: ListCommon) GetItemsPerPage (): INTEGER, NEW;
		VAR ih, res: INTEGER;
	BEGIN
		res := 1;
		IF (l.GetLength() # 0) & l.IsValidList() THEN
			ih := l.GetItemHeight();
			res := l.h DIV ih;
			IF res = 0 THEN res := 1 END
		END;
		RETURN res
	END GetItemsPerPage;

	PROCEDURE (l: ListCommon) GetFirstPageItem (): INTEGER, NEW;
		VAR y, ih, res: INTEGER;
	BEGIN
		res := 0;
		IF (l.GetLength() # 0) & l.IsValidList() THEN
			y := 0 - l.yoffs;
			ih := l.GetItemHeight();
			res := l.yoffs DIV ih;
			INC(y, res * ih);
			IF y < 0 THEN INC(res) END;
			res := MAX(0, MIN(l.GetLength() - 1, res))
		END;
		RETURN res
	END GetFirstPageItem;

	PROCEDURE (l: ListCommon) GetItemYOfs (): INTEGER, NEW;
		VAR asc, dsc, sw: INTEGER;
	BEGIN
		IF (l.GetLength() # 0) & l.IsValidList() THEN
			l.ctl.font.GetBounds(asc, dsc, sw);
			sw := l.GetItemHeight();
			IF asc + dsc > sw THEN sw := (sw - asc - dsc) DIV 2
			ELSE sw := 0
			END;
			INC(sw, asc)
		ELSE sw := 0
		END;
		RETURN sw
	END GetItemYOfs;

	PROCEDURE (l: ListCommon) NormaliseYOffsList, NEW;
		VAR ih, hsbWidth: INTEGER;
	BEGIN
		IF (l.GetLength() # 0) & l.IsValidList() THEN
			hsbWidth := 0;
			IF l.hsb # NIL THEN hsbWidth := l.hsb.Width() END;
			IF l.totalHeight <= l.h - hsbWidth THEN
				l.yoffs := 0
			ELSE
				ih := l.GetItemHeight();
				IF l.h < ih * 2 THEN
					l.yoffs := ih * l.pos
				ELSE
					l.yoffs := MIN(l.yoffs, l.totalHeight - (l.h - hsbWidth ));
					l.yoffs := MAX(l.yoffs, 0)
				END
			END
		ELSE
			l.yoffs := 0
		END
	END NormaliseYOffsList;

	PROCEDURE GetStorage (v: Views.View): Storage;
		VAR st: Storage;
	BEGIN
		st := storages;
		WHILE (st # NIL) & (st.view # v) DO st := st.next END;
		RETURN st
	END GetStorage;

	PROCEDURE (l: ListCommon) MakeCursorVisibleWorker (doCenter: BOOLEAN), NEW;
		VAR y, ih, pos, hsbSize: INTEGER; st: Storage;
	BEGIN
		
		IF (l.st = NIL) & (l.ctl # NIL) THEN
			st := GetStorage(l.ctl.view);
			IF (st # NIL) & (st  IS ListStorage) THEN
				l.st := st(ListStorage)
			ELSE END
		END;

		IF (l.GetLength() # 0) & l.IsValidList() & (l.pos > -1) THEN
			IF l.totalHeight = 0 THEN l.RecalcCachedSizes END;
			pos := l.IdxBack(l.pos);
			hsbSize := 0;
			IF l.hsb # NIL THEN
				hsbSize := l.hsb.Width()
			END;
			IF (pos <= 0) OR (l.totalHeight <= l.h - hsbSize) THEN
				l.yoffs := 0;
				IF l.st # NIL THEN
					l.st.yoffs := l.yoffs
				END
			ELSE
				ih := l.GetItemHeight();
				y := ih * pos;
				IF l.h < ih * 2 THEN
					l.yoffs := y
				ELSE
					DEC(y, l.yoffs);
					(* now check if the cursor is visible *)
					IF y < 0 THEN
						INC(l.yoffs, y);
						IF doCenter & (l.h > ih * 4) THEN DEC(l.yoffs, ((l.h - hsbSize) - ih) DIV 2) END;
					ELSIF y + ih > (l.h - hsbSize) THEN
						INC(l.yoffs, y + ih - (l.h  - hsbSize));
						IF doCenter & (l.h > ih * 4) THEN INC(l.yoffs, ((l.h - hsbSize) - ih) DIV 2) END
					END
				END;
				
				l.NormaliseYOffsList;

				IF l.st # NIL THEN
					l.st.yoffs := l.yoffs
				END
			END
		ELSE
			l.yoffs := 0
		END
		
	END MakeCursorVisibleWorker;

	(* should be called from owner's `Restore()` *)
	PROCEDURE (l: ListCommon) MakeCursorVisible, NEW;
	BEGIN l.MakeCursorVisibleWorker(FALSE)
	END MakeCursorVisible;

	(* should be called from owner's `Restore()` *)
	PROCEDURE (l: ListCommon) MakeCursorVisibleCentered, NEW;
	BEGIN l.MakeCursorVisibleWorker(TRUE)
	END MakeCursorVisibleCentered;

	PROCEDURE (l: ListCommon) CharSearch (ch: CHAR), NEW;
		VAR ctime: LONGINT; spos, found: INTEGER;
	
		PROCEDURE Match (IN a, b: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER; match: BOOLEAN;
		BEGIN
			IF LEN(b$) <= LEN(a$) THEN
				match := TRUE;
				i := 0;
				WHILE (b[i] # 0X) & match DO
					match := Strings.Upper(a[i]) = Strings.Upper(b[i]);
					INC(i)
				END
			ELSE
				match := FALSE
			END;
			RETURN match
		END Match;
	
	BEGIN
		IF l.GetLength() > 0 THEN
			ch := Strings.Upper(ch);
			ctime := Services.Ticks();
			IF ctime - l.lastSearchTime > 600 THEN
				l.lastSearchPos := 0;
				l.searchString[0] := ch;
				l.searchString[1] := 0X
			ELSIF l.lastSearchPos < LEN(l.searchString) - 3 THEN
				INC(l.lastSearchPos);
				l.searchString[l.lastSearchPos] := ch;
				l.searchString[l.lastSearchPos+1] := 0X
			END;
			l.lastSearchTime := ctime;
			found := -1;
			IF l.unmap # NIL THEN
				(* sorted *)
				spos := l.unmap[l.pos] + 1;
				WHILE (spos >= 0) & (spos < l.GetLength()) & (found < 0) DO
					IF Match(l.items[l.Idx(spos)].text, l.searchString) THEN
						found := spos
					END;
					INC(spos)
				END;
				IF found = - 1 THEN
					(* cycle search *)
					spos := 0;
					WHILE (spos >= 0) & (spos < l.GetLength()) & (found < 0) DO
						IF Match(l.items[l.Idx(spos)].text, l.searchString) THEN
							found := spos
						END;
						INC(spos)
					END
				END;
				IF found # -1 THEN
					l.pos := l.Idx(found)
				END
			ELSE
				spos := l.pos + 1;
				WHILE (spos >= 0) & (spos < l.GetLength()) & (found < 0) DO
					IF Match(l.items[spos].text, l.searchString) THEN
						found := spos
					END;
					INC(spos)
				END;
				IF found = - 1 THEN
					(* cycle search *)
					spos := 0;
					WHILE (spos >= 0) & (spos < l.GetLength()) & (found < 0) DO
						IF Match(l.items[spos].text, l.searchString) THEN
							found := spos
						END;
						INC(spos)
					END;
				END;
				IF found # -1 THEN
					l.pos := found
				END
			END
		END
	END CharSearch;

	PROCEDURE (l: ListCommon) DefineColors (ff: INTEGER; OUT col, bcol: Ports.Color; singleLine: BOOLEAN; OUT mark: BOOLEAN), NEW;
		VAR c: StdCFrames.Frame; f, selPos: INTEGER;
	BEGIN
		c := l.ctl;
		f := l.Idx(ff);
		IF l.movePos >= 0 THEN
			selPos := l.movePos
		ELSE
			selPos := l.pos
		END;
		mark := FALSE;
		IF c.disabled OR c.readOnly THEN  (* Disabled *)
			IF ~singleLine & (f = selPos) THEN
				IF l.items[f].marked THEN (* Selected + Cursored *)
					bcol := param.backSelListCursorDisabled;
					col := param.textSelListCursorDisabled;
				ELSE (* Cursored *)
					bcol := param.backListCursorDisabled;
					col := param.textListCursorDisabled;
				END
			ELSE
				IF l.items[f].marked THEN (* Selected *)
					bcol := param.backSelListDisabled;
					col := param.textSelListDisabled;
				ELSE
					bcol := param.backListDisabled;
					col := param.textListDisabled;
				END
			END
		ELSIF c.mark THEN 
			IF f = selPos THEN
				IF l.items[f].marked THEN (* Selected + Cursored *)
					bcol := param.backSelListCursor;
					col := param.textSelListCursor;
				ELSE (* Cursored *)
					IF l.GetSelectionBox() # NIL THEN
						bcol := param.backList;
						col := param.textList;
					ELSE
						bcol := param.backListCursor;
						col := param.textListCursor;
					END
				END;
				mark := selPos >= 0
			ELSE
				IF l.items[f].marked THEN (* Selected *)
					bcol := param.backSelList;
					col := param.textSelList;
				ELSE
					bcol := param.backList;
					col := param.textList;
				END
			END
		ELSE (* Blured  *)
			IF ~c.readOnly & (f = selPos) THEN
				IF l.items[f].marked THEN (* Selected + Cursored *)
					bcol := param.backSelListCursorBlured;
					col := param.textSelListCursorBlured;
				ELSE (* Cursored *)
					bcol := param.backListCursorBlured;
					col := param.textListCursorBlured;
				END
			ELSE
				IF l.items[f].marked THEN (* Selected *)
					bcol := param.backSelListBlured;
					col := param.textSelListBlured;
				ELSE
					bcol := param.backListBlured;
					col := param.textListBlured;
				END
			END
		END
	END DefineColors;


	PROCEDURE (l: ListCommon) Restore, NEW;
		VAR
			c: StdCFrames.Frame; bkcol, col, bcol: Ports.Color;
			ih, iofs, len, y, f, xoffs, asc, dsc, sw, i, pos, column, colWidth: INTEGER;
			vsb: VScrollBar; vsbSize: INTEGER; hsb: HScrollBar; hsbSize: INTEGER;
			item, tmp: TextChain; clip: ClipRect; marked, tabbed: BOOLEAN;
		
		PROCEDURE MakeVSB;
		BEGIN
			IF l.vsb = NIL THEN
				NEW(vsb);
				vsb.Init;
				l.vsb := vsb
			ELSE
				vsb := l.vsb
			END;
			vsb.min := 0;
			vsb.contentSize := l.totalHeight;
			vsb.max := vsb.contentSize - l.h;
			vsb.pos := l.yoffs;
			vsb.pageSize := l.h;
			vsb.arrowStep := l.GetItemHeight();
			vsb.length := l.h;
			IF ~vsb.CanFit() THEN
				vsb := NIL
			ELSE
				vsbSize := MAX(0, vsb.Width());
				vsb.length := l.h - vsb.Width();
				IF (vsbSize = 0) OR ((vsbSize + 4 * Ports.point) >= l.w) THEN
					vsb := NIL
				END
			END
		END MakeVSB;
		
		
	BEGIN
		IF l.IsValidList() THEN
			IF l.totalHeight = 0 THEN
				l.RecalcCachedSizes
			END;
			
			IF l.st # NIL THEN
				IF l.st.yoffs > l.totalHeight THEN
					l.yoffs := 0
				ELSE
					l.yoffs := l.st.yoffs
				END
			END;
			
			c := l.ctl;
			
			(* check if we need a scrollbar, and prepare it *)
			IF l.totalHeight > l.h THEN
				MakeVSB
			ELSE
				vsb := NIL
			END;			
			IF vsb = NIL THEN vsbSize := 0; l.vsb := NIL END;
			
			IF l.ctl.disabled OR l.ctl.readOnly THEN
				bkcol := param.backListDisabled
			ELSIF l.ctl.mark THEN
				bkcol := param.backList
			ELSE
				bkcol := param.backListBlured
			END;
			
			c.DrawRect(l.x0 - l.outMargin.l * c.dot, l.y0 - l.outMargin.t * c.dot,
								l.x0 + l.w + l.outMargin.r * c.dot, l.y0 + l.h + l.outMargin.b * c.dot,
								Ports.fill, bkcol);
			
			
			(* analyze list for tabs *)
			len := l.GetLength();
			f := 0;
			WHILE f < LEN(l.widths) DO
				l.widths[f] := 0;
				INC(f)
			END;
			
			f := 0; tabbed := FALSE;
			WHILE f < len DO
				i := 0; column := 0;
				l.items[f].tabbed := NIL;
				REPEAT
					Strings.Find(l.items[f].text, TAB, i, pos);
					IF pos >= 1 THEN
						tabbed := TRUE;
						NEW(item);
						NEW(item.text, pos - i + 1);
						Strings.Extract(l.items[f].text, i, pos - i, item.text);
						IF l.items[f].tabbed = NIL THEN
							l.items[f].tabbed := item
						ELSE
							tmp := l.items[f].tabbed;
							WHILE tmp.next # NIL DO
								tmp := tmp.next
							END;
							tmp.next := item
						END;
						colWidth := c.font.StringWidth(item.text);
						IF l.widths[column] <  colWidth THEN
							l.widths[column] := colWidth
						END;
						INC(column)
					ELSIF l.items[f].tabbed # NIL THEN
						pos := LEN(l.items[f].text$);
						NEW(item);
						NEW(item.text, pos - i + 1);
						Strings.Extract(l.items[f].text, i, pos - i, item.text);
						IF l.items[f].tabbed = NIL THEN
							l.items[f].tabbed := item
						ELSE
							tmp := l.items[f].tabbed;
							WHILE tmp.next # NIL DO
								tmp := tmp.next
							END;
							tmp.next := item
						END;
						colWidth := c.font.StringWidth(item.text);
						IF l.widths[column] < colWidth THEN l.widths[column] := colWidth END;
						INC(column)
					END;
					i := pos + 1
				UNTIL (pos = -1) OR (pos >= LEN(l.items[f].text$));
				INC(f)
			END;
			IF tabbed THEN
				l.RecalcCachedSizes;
			END;
			
			(* check if we need a horisontal scrollbar, and prepare it  *)
			IF (l.maxWidth > (l.w - vsbSize)) & (l.h > l.GetItemHeight() * 2) THEN
				IF l.hsb = NIL THEN
					NEW(hsb);
					hsb.Init;
					l.hsb := hsb
				ELSE
					hsb := l.hsb
				END;
				hsb.min := 0;
				hsb.max := l.maxWidth - l.w + l.x0 + Ports.mm;
				hsb.contentSize := l.maxWidth;
				hsb.pos := l.xoffs;
				hsb.pageSize := l.w;
				hsb.arrowStep := Ports.mm;
				hsb.length := l.w;
				IF ~hsb.CanFit() THEN
					hsb := NIL
				ELSE
					hsbSize := MAX(0, hsb.Width());
					IF (hsbSize = 0) OR ((hsbSize + 4 * Ports.point) >= l.h) THEN
						hsbSize := 0; hsb := NIL
					END
				END;
				
				IF (hsb # NIL) & (vsb = NIL) THEN
					(* RE-check if we need a scrollbar, and prepare it *)
					IF l.totalHeight > l.h - hsbSize THEN
						MakeVSB;
						INC(vsb.contentSize, hsbSize);
					ELSE
						vsb := NIL
					END;
					IF vsb = NIL THEN vsbSize := 0; l.vsb := NIL END;
				END
			ELSE
				l.hsb := NIL; hsbSize := 0; hsb := NIL
			END;
			
			IF (vsb # NIL) & (hsb # NIL) THEN
				(* add rectangle at right bottom corner *)
				DEC(hsb.length, vsbSize);
				DEC(vsb.length, hsbSize);
				(* this to prevent right part of string overlapped by vert scrollbar *)
				INC(hsb.max, vsbSize);
				INC(vsb.max, hsbSize);
			END;
			
			clip.Init(l.ctl);
			IF clip.ShrinkToWH(l.x0, l.y0, l.w, l.h) THEN
				y := 0 - l.yoffs;
				ih := l.GetItemHeight();
				iofs := l.GetItemYOfs();
				f := l.yoffs DIV ih;
								
				len := l.GetLength();
				IF f < len THEN
					IF (hsb # NIL) THEN
						xoffs := - l.xoffs
					END;
					INC(y, f * ih);
					
					IF 2 * ih > l.h THEN  (* !!! same rule should be in NeedPopup *)
						
						IF (f >= 0) & (l.pos # -1) THEN f := l.pos END;
						
						l.DefineColors(f, col, bcol, TRUE, marked);
						IF bcol # bkcol THEN
							c.DrawRect(l.x0, l.y0, l.x0 + l.w, l.y0 + l.h, Ports.fill, bcol)
						END;
						IF marked THEN
							c.MarkRect(l.x0 + 2*c.dot, l.y0 + 2*c.dot, l.x0 + l.w - 2*c.dot, l.y0 + l.h - 2*c.dot, c.dot, Ports.dim50, Ports.show)
						END;
						c.font.GetBounds(asc, dsc, sw);
						
						iofs := (l.h - asc - dsc) DIV 2 - c.dot;
						
						IF (l.items[f].tabbed = NIL) THEN
							c.DrawString(l.x0 + xoffs + 2*c.dot, l.y0 + asc + iofs, col, l.items[f].text, c.font)
						ELSE
							tmp := l.items[f].tabbed;
							column := 0;
							pos := 0;
							WHILE tmp # NIL DO
								c.DrawString(l.x0 + xoffs + pos + 2*c.dot, l.y0 + asc + iofs, col, tmp.text, c.font);
								tmp := tmp.next;
								INC(column);
								pos := pos + l.widths[column-1] + listTabSize * Ports.point;
							END
						END
						
					ELSE
						
						WHILE (f < len) & (f >= 0) & (y < l.h) DO
							l.DefineColors(f, col, bcol, FALSE, marked);
							
							IF bcol # bkcol THEN
								IF l.movePos >= 0 THEN
									INC(y, 2 * c.dot);
									INC(xoffs, 2 * Ports.mm);
									
									c.DrawRect(l.x0, l.y0 + y, l.x0 + l.w, l.y0 + y + ih + 4*c.dot, Ports.fill, Ports.blue);
								ELSE
									c.DrawRect(l.x0, l.y0 + y, l.x0 + l.w, l.y0 + y + ih, Ports.fill, bcol)
								END
							END;
							IF marked THEN
								IF l.movePos >= 0 THEN
									c.MarkRect(l.x0 + c.dot, 
										l.y0 + y + c.dot, 
										l.x0 + l.w - c.dot, 
										l.y0 + y + ih - c.dot + 4*c.dot, c.dot, Ports.dim50, Ports.show);
										INC(y, 2*c.dot);
								ELSE
									c.MarkRect(l.x0 + c.dot, l.y0 + y + c.dot,
										l.x0 + l.w - c.dot, l.y0 + y + ih - c.dot, c.dot, Ports.dim50, Ports.show)
								END
							END;
							
							IF l.items[l.Idx(f)].tabbed = NIL THEN
								c.DrawString(l.x0 + xoffs + 2*c.dot, l.y0 + y + iofs, col, l.items[l.Idx(f)].text, c.font)
							ELSE
								tmp := l.items[l.Idx(f)].tabbed;
								column := 0;
								pos := 0;
								WHILE tmp # NIL DO
									c.DrawString(l.x0 + xoffs + pos  + 2*c.dot, l.y0 + y + iofs, col, tmp.text, c.font);
									tmp := tmp.next;
									INC(column);
									pos := pos + l.widths[column-1] + listTabSize * c.dot
								END
							END;
							
							IF (l.movePos >= 0) & (bcol # bkcol) THEN
								INC(y, 2 * c.dot);
								DEC(xoffs, 2 * Ports.mm)
							END;
							
							INC(y, ih);
							INC(f)
						END
					END
				END
			END;
			
			IF vsb # NIL THEN
				vsb.x0 := l.x0 + l.w - vsbSize;
				vsb.y0 := l.y0;
				vsb.Draw(c, vsb.x0, vsb.y0, l.h - hsbSize, l.vsbPressed)
			END;
			
			IF hsb # NIL THEN
				hsb.x0 := l.x0;
				hsb.y0 := l.y0 + l.h - hsbSize;
				hsb.Draw(c, hsb.x0, hsb.y0, l.w - vsbSize, l.hsbPressed)
			END;
			
			IF (hsbSize > 0) & (vsbSize > 0) THEN
				c.DrawRect(l.x0 + l.w - vsbSize, l.y0 + l.h - hsbSize, l.w + 2*c.dot, l.h+ 2*c.dot, Ports.fill, param.backScrollbar)
			END;
			
			clip.Close
		END
	END Restore;

	PROCEDURE (c: ListCommon) GetCursor (x_, y_: INTEGER; m_: SET; VAR cursor: INTEGER), NEW;
	BEGIN
	END GetCursor;

	PROCEDURE (l: ListCommon) KeyDown (ch: CHAR; modifiers: SET), NEW;
		VAR f, pos, i: INTEGER; mark, doCenter: BOOLEAN; sb: StdCFrames.SelectionBox;
			item: ListCommonItem;
	BEGIN
		IF l.GetLength() > 0 THEN
			doCenter := FALSE;
			sb := l.GetSelectionBox();
			CASE ch OF
			| DL: (* home *) 
				IF l.unmap # NIL THEN
					pos := l.unmap[l.pos];
					IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *) 
						IF pos > 0 THEN
							mark := ~l.items[pos].marked;
							FOR f := 0 TO pos DO l.items[f].marked := mark END;
							IF mark THEN sb.Incl(sb, 0, pos) ELSE sb.Excl(sb, 0, pos) END
						END
					END;
					l.pos := l.Idx(0)
				ELSE
					IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *) 
						IF l.pos > 0 THEN
							mark := ~l.items[l.pos].marked;
							FOR f := 0 TO l.pos DO l.items[f].marked := mark END;
							IF mark THEN sb.Incl(sb, 0, l.pos) ELSE sb.Excl(sb, 0, l.pos) END
						END
					END;
					l.pos := 0
				END
			| DR: (* end *) 
				IF l.unmap # NIL THEN
					pos := l.unmap[l.pos];
					IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *) 
						IF pos < l.GetLength() - 1 THEN
							mark := ~l.items[pos].marked;
							FOR f := pos TO l.GetLength() - 1 DO l.items[f].marked := mark END;
							IF mark THEN
								sb.Incl(sb, pos, l.GetLength() - 1)
							ELSE
								sb.Excl(sb, pos, l.GetLength() - 1)
							END
						END
					END;
					l.pos := MAX(0, l.Idx(l.GetLength() - 1))
				ELSE
					IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *) 
						IF l.pos < l.GetLength() - 1 THEN
							mark := ~l.items[l.pos].marked;
							FOR f := l.pos TO l.GetLength() - 1 DO
								l.items[f].marked := mark
							END;
							IF mark THEN
								sb.Incl(sb, l.pos, l.GetLength() - 1)
							ELSE
								sb.Excl(sb, l.pos, l.GetLength() - 1)
							END
						END
					END;
					l.pos := MAX(0, l.GetLength() - 1)
				END
				
			| AU: (* arrow up *) 
				
				IF l.canMove & ~l.sorted & (Controllers.modify IN modifiers) THEN
					IF l.movePos < 0 THEN
						l.movePos := l.pos
					END;
					IF l.movePos > 0 THEN
						item := l.items[l.movePos];
						l.items[l.movePos] := l.items[l.movePos - 1];
						l.items[l.movePos - 1] := item;
						l.movePos := l.movePos - 1;
						Controls.Notify(
							l.ctl.view(Controls.Control), l.ctl, Dialog.moved, l.movePos + 1, l.movePos
						)
					ELSE
						l.movePos := -1
					END
				ELSE
					IF l.pos < 0 THEN
						l.pos := l.Idx(0);
						IF sb # NIL THEN
							sb.Incl(sb, l.pos, l.pos)
						END;
						l.items[l.pos].marked := TRUE
					ELSE				
						IF l.unmap # NIL THEN
							pos := l.unmap[l.pos]
						ELSE
							pos := l.pos
						END;
						IF pos > 0 THEN
							IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *)
								IF l.items[l.Idx(pos - 1)].marked  THEN
									l.items[l.Idx(pos)].marked := FALSE;
									sb.Excl(sb, l.Idx(pos), l.Idx(pos))
								ELSE
									l.items[l.Idx(pos - 1)].marked := TRUE;
									sb.Incl(sb, l.Idx(pos - 1), l.Idx(pos-1))
								END
							ELSIF sb # NIL THEN
								FOR i := 0 TO l.GetLength() - 1 DO
									IF l.items[i].marked & (i # l.Idx(pos - 1)) THEN
										sb.Excl(sb, i, i);
										l.items[i].marked := FALSE
									END
								END;
								i := l.Idx(pos - 1);
								IF ~l.items[i].marked THEN
									sb.Incl(sb, i, i);
									l.items[i].marked := TRUE
								END;
							END;
							DEC(pos);
							l.pos := l.Idx(pos)
						END
					END;
				END
				
			| AD: (* arrow down *)
				IF l.canMove & ~l.sorted & (Controllers.modify IN modifiers) THEN
					IF l.movePos < 0 THEN
						l.movePos := l.pos
					END;
					IF l.movePos < l.GetLength() - 1 THEN
						item := l.items[l.movePos];
						l.items[l.movePos] := l.items[l.movePos + 1];
						l.items[l.movePos + 1] := item;
						l.movePos := l.movePos + 1;
						Controls.Notify(
							l.ctl.view(Controls.Control), l.ctl, Dialog.moved, l.movePos - 1, l.movePos
						)
					ELSE
						l.movePos := -1
					END;
				ELSE
					
					IF l.pos < 0 THEN
						l.pos := l.Idx(0);
						IF sb # NIL THEN
							sb.Incl(sb, l.pos, l.pos)
						END;
						l.items[l.pos].marked := TRUE 
					ELSE				
						IF l.unmap # NIL THEN
							pos := l.unmap[l.pos]
						ELSE
							pos := l.pos
						END;
						IF pos < l.GetLength() - 1 THEN
							IF (Controllers.extend IN modifiers) & (sb # NIL) THEN (* Shift *)
								IF l.items[l.Idx(pos + 1)].marked  THEN
									l.items[l.Idx(pos)].marked := FALSE;
									sb.Excl(sb, l.Idx(pos), l.Idx(pos))
								ELSE
									l.items[l.Idx(pos + 1)].marked := TRUE;
									sb.Incl(sb, l.Idx(pos + 1), l.Idx(pos + 1))
								END
							ELSIF sb # NIL THEN
								FOR i := 0 TO l.GetLength() - 1 DO
									IF l.items[i].marked & (i # l.Idx(pos + 1)) THEN
										sb.Excl(sb, i, i);
										l.items[i].marked := FALSE
									END
								END;
								i := l.Idx(pos + 1);
								IF ~l.items[i].marked THEN
									sb.Incl(sb, i, i);
									l.items[i].marked := TRUE
								END;
							END;
							INC(pos);
							l.pos := l.Idx(pos)
						END
					END
				END
				
			| AL: (* arrow left *) 
				IF l.xoffs > 8 * Ports.point THEN 
					l.xoffs := l.xoffs - 8 * Ports.point
				ELSE
					l.xoffs := 0
				END
			| AR: (* arrow right *) 
				l.xoffs := l.xoffs + 8 * Ports.point;
				IF l.xoffs > l.maxWidth THEN  (* does not work with tabs TO CHECK *)
					l.xoffs := l.maxWidth
				END
			| PU: (* page up *) 
				IF l.pos > l.GetFirstPageItem() THEN
					l.pos := l.GetFirstPageItem()
				ELSE
					DEC(l.pos, l.GetItemsPerPage())
				END;
				l.pos := MAX(0, MIN(l.GetLength() - 1, l.pos))
			| PD: (* page down *) 
				IF l.pos < l.GetFirstPageItem() + l.GetItemsPerPage() - 1 THEN
					l.pos := l.GetFirstPageItem() + l.GetItemsPerPage() - 1
				ELSE
					INC(l.pos, l.GetItemsPerPage())
				END;
				l.pos := MAX(0, MIN(l.GetLength() - 1, l.pos))
			| 20X: (* toggle mark *)
				
				IF sb # NIL THEN
					mark := ~l.items[l.pos].marked;
					l.items[l.pos].marked := mark;
					IF mark THEN sb.Incl(sb, l.pos, l.pos) ELSE sb.Excl(sb, l.pos, l.pos) END;
					IF l.pos < l.GetLength() - 1 THEN INC(l.pos) END
				ELSE l.CharSearch(ch); doCenter := TRUE
				END
				
			ELSE
				IF (ch >= 20X) & (ch # 7FX) THEN l.CharSearch(ch); doCenter := TRUE END
			END;
			l.MakeCursorVisibleWorker(doCenter)
		END
	END KeyDown;

	PROCEDURE (l: ListCommon) WheelMove (x, y: INTEGER; op, nofLines: INTEGER; VAR done: BOOLEAN), NEW;
	BEGIN
		IF l.IsValidList() & (x >= l.x0) & (y >= l.y0) & (x < l.x0 + l.w) & (y < l.y0 + l.h) THEN
			done := TRUE;
			IF op = Controllers.decLine THEN nofLines := -nofLines END;
			INC(l.yoffs, l.GetItemHeight() * nofLines);
			l.NormaliseYOffsList;
			IF l.st # NIL THEN
				l.st.yoffs := l.yoffs
			END
		END
	END WheelMove;

	PROCEDURE (l: ListCommon) LstTrackVSBMouse (), NEW;
		VAR c: StdCFrames.Frame;
			x, y, ox, oy, opos: INTEGER; isDown: BOOLEAN; modifiers: SET;
	BEGIN
		c := l.ctl;
		IF c.rider # NIL THEN
			(* k8: this hack repaints other controls, so they could indicate focus loss *)
			l.vsbPressed := TRUE;
			UpdateRoot(c);
			l.ctl.ForceUpdate;
			opos := l.vsb.pos;
			ox := MIN(INTEGER); oy := MIN(INTEGER);
			REPEAT
				c.Input(x, y, modifiers, isDown);
				x := (x - l.x0) - l.vsb.x0;
				y := (y - l.y0) - l.vsb.y0;
				IF ox = MIN(INTEGER) THEN ox := x END;
				IF oy = MIN(INTEGER) THEN oy := y END;
				IF (ox # x) OR (oy # y) THEN
					ox := x; oy := y;
					l.vsb.SBMouseTrack(x, y);
					IF opos # l.vsb.pos THEN
						opos := l.vsb.pos;
						l.yoffs := opos;
						IF l.st # NIL THEN
							l.st.yoffs := l.yoffs
						END;
						l.ctl.ForceUpdate
					END
				END
			UNTIL ~isDown;
			l.vsbPressed := FALSE;
			l.ctl.ForceUpdate
		END
	END LstTrackVSBMouse;
	
	PROCEDURE (l: ListCommon) LstTrackHSBMouse (), NEW;
		VAR c: StdCFrames.Frame; x, y, ox, oy, opos: INTEGER; isDown: BOOLEAN; modifiers: SET;
	BEGIN
		c := l.ctl;
		IF c.rider # NIL THEN
			(* k8: this hack repaints other controls, so they could indicate focus loss *)
			l.hsbPressed := TRUE;
			UpdateRoot(c);
			l.ctl.ForceUpdate;
			opos := l.hsb.pos;
			ox := MIN(INTEGER); oy := MIN(INTEGER);
			REPEAT
				c.Input(x, y, modifiers, isDown);
				x := (x - l.x0) - l.hsb.x0;
				y := (y - l.y0) - l.hsb.y0;
				IF ox = MIN(INTEGER) THEN ox := x END;
				IF oy = MIN(INTEGER) THEN oy := y END;
				IF (ox # x) OR (oy # y) THEN
					ox := x; oy := y;
					l.hsb.SBMouseTrack(x, y);
					IF opos # l.hsb.pos THEN
						opos := l.hsb.pos;
						l.xoffs := opos;
						l.ctl.ForceUpdate
					END
				END
			UNTIL ~isDown;
			l.hsbPressed := FALSE;
			l.ctl.ForceUpdate
		END
	END LstTrackHSBMouse;

	PROCEDURE (l: ListCommon) MouseDown (x, y: INTEGER; buttons: SET), NEW;
		VAR id, idx, pos, i, initPos, firstId, w, h, itemHeight: INTEGER;
			makeCursorVisible, isDown, changed, selected: BOOLEAN;
			sb: StdCFrames.SelectionBox; modifiers: SET;
			item: ListCommonItem; initY: INTEGER;
	BEGIN
		makeCursorVisible := TRUE;
		IF (l.GetLength() # 0) & l.IsValidList() & (x >= l.x0) & (y >= l.y0) & (x < l.x0+l.w) & (y < l.y0+l.h) THEN
			
			IF (l.vsb # NIL) & (l.vsb.length > 0) & (l.x0+l.w - x < l.vsb.Width()) THEN
				
				makeCursorVisible := FALSE;
				IF l.vsb.SBMouseDown((x - l.x0) - l.vsb.x0, (y - l.y0) - l.vsb.y0) THEN
					l.LstTrackVSBMouse
				ELSE
					l.yoffs := l.vsb.pos;
					IF l.st # NIL THEN
						l.st.yoffs := l.yoffs
					END
					(* the caller will repaint us *)
				END
				
			ELSIF (l.hsb # NIL) & (l.hsb.length > 0) & (l.y0 + l.h - y < l.hsb.Width()) THEN
				
				makeCursorVisible := FALSE;
				
				IF l.hsb.SBMouseDown((x - l.x0) - l.hsb.x0, (y - l.y0) - l.hsb.y0) THEN
					l.LstTrackHSBMouse
				ELSE
					l.xoffs := l.hsb.pos
					(* the caller will repaint us *)
				END
				
			ELSIF ~ l.ctl.readOnly THEN
				changed := FALSE;
				selected := FALSE;
				initPos := -777;
				initY := -777;
				
				l.ctl.Input(x, y, modifiers, isDown);
				
				IF l.canMove & ~l.sorted THEN
					
					l.ctl.view.context.GetSize(w, h);
					itemHeight := l.GetItemHeight();
					
					REPEAT
						(* INC(l.yoffs, Ports.mm); check *)
						l.ctl.Input(x, y, modifiers, isDown);
						
						DEC(y, l.y0);
						
						id := (y + l.yoffs) DIV itemHeight;
						
						IF (l.movePos > 0) & (l.totalHeight > h) THEN
							IF y > h - itemHeight * 2 THEN
								l.yoffs :=  l.yoffs + (y - (h - itemHeight * 2)) DIV 4;
								IF l.yoffs > l.totalHeight - h + itemHeight THEN
									l.yoffs := l.totalHeight - h + itemHeight
								END;
								IF l.st # NIL THEN
									l.st.yoffs := l.yoffs
								END
							ELSIF y < itemHeight * 2 THEN
								l.yoffs :=  l.yoffs - (itemHeight * 2 - y) DIV 4;
								IF l.yoffs < 0 THEN
									l.yoffs := 0
								END;
								IF l.st # NIL THEN
									l.st.yoffs := l.yoffs
								END
							END;
						END;
						IF (id >= 0) & (id <= l.GetLength() - 1) THEN
							
							IF initPos = -777 THEN
								firstId := id;
								initPos := id;
								initY := y;
								IF l.pos # id THEN
									l.pos := id;
									changed := TRUE
								END;
							END;
							sb := l.GetSelectionBox();
							
							IF (l.movePos # id) & (ABS(initY - y) > 2 * l.ctl.dot) THEN
								IF id # initPos THEN
									item := l.items[id];
									l.items[id] := l.items[initPos];
									l.items[initPos] := item;
									initPos := id;
								END;
								l.movePos := id;
								changed := TRUE
							END;
							IF changed THEN
								l.ctl.ForceUpdate;
								changed := FALSE
							END
							
						END
					UNTIL ~isDown;
					
					IF firstId # initPos THEN
						IF initPos >= 0 THEN
							l.ctl.ForceUpdate;
							Controls.Notify(l.ctl.view(Controls.Control), l.ctl, Dialog.moved, firstId, initPos)
						END
					ELSE
						l.pos := initPos;
						l.movePos := -1;
						l.ctl.ForceUpdate
					END
					
				ELSE 
					
					REPEAT
						l.ctl.Input(x, y, modifiers, isDown);
						
						DEC(y, l.y0);
						id := (y + l.yoffs) DIV l.GetItemHeight();
						IF (id >= 0) & (id <= l.GetLength() - 1) THEN
							idx := l.Idx(id);
							IF initPos = -777 THEN initPos := id END;
							sb := l.GetSelectionBox();
							
							IF (sb # NIL) & ~selected THEN
								
								IF id # initPos THEN
									pos := initPos;
									IF id < pos THEN
										REPEAT
											IF ~l.items[l.Idx(pos - 1)].marked THEN
												l.items[l.Idx(pos - 1)].marked := TRUE;
												sb.Incl(sb, l.Idx(pos - 1), l.Idx(pos-1));
											END;
											DEC(pos)
										UNTIL pos = id
									ELSIF id > pos THEN
										REPEAT
											IF ~l.items[l.Idx(pos + 1)].marked THEN
												l.items[l.Idx(pos + 1)].marked := TRUE;
												sb.Incl(sb, l.Idx(pos + 1), l.Idx(pos + 1));
											END;
											INC(pos)
										UNTIL pos = id;
									END;
									changed := TRUE;
									
								ELSIF idx # l.pos THEN
									IF (Controllers.extend IN buttons) THEN (* Shift *)
										pos := l.IdxBack(l.pos);
										IF id < pos THEN
											REPEAT
												IF ~l.items[l.Idx(pos - 1)].marked THEN
													l.items[l.Idx(pos - 1)].marked := TRUE;
													sb.Incl(sb, l.Idx(pos - 1), l.Idx(pos-1));
												END;
												DEC(pos)
											UNTIL pos = id
										ELSIF id > pos THEN
											REPEAT
												IF ~l.items[l.Idx(pos + 1)].marked THEN
													l.items[l.Idx(pos + 1)].marked := TRUE;
													sb.Incl(sb, l.Idx(pos + 1), l.Idx(pos + 1));
												END;
												INC(pos)
											UNTIL pos = id;
										END;
										changed := TRUE;
										selected := TRUE
									ELSIF Controllers.modify IN buttons THEN (* Ctrl *)
										IF ~l.items[idx].marked THEN
											sb.Incl(sb, idx, idx);
											l.items[idx].marked := TRUE;
											changed := TRUE;
											selected := TRUE
										ELSE
											sb.Excl(sb, idx, idx);
											l.items[idx].marked := FALSE;
											changed := TRUE;
											selected := TRUE
										END
									ELSE
										FOR i := 0 TO l.GetLength() - 1 DO
											IF l.items[i].marked & (i # idx) THEN
												sb.Excl(sb, i, i);
												l.items[i].marked := FALSE;
												changed := TRUE;
											END
										END;
										IF ~ l.items[idx].marked THEN
											sb.Incl(sb, idx, idx);
											l.items[idx].marked := TRUE;
											changed := TRUE;
										END
									END
								ELSE
									IF Controllers.modify IN buttons THEN (* Ctrl *)
										IF ~l.items[idx].marked THEN
											sb.Incl(sb, idx, idx);
											l.items[idx].marked := TRUE;
											changed := TRUE;
											selected := TRUE
										ELSE
											sb.Excl(sb, idx, idx);
											l.items[idx].marked := FALSE;
											changed := TRUE;
											selected := TRUE
										END
									ELSE
										FOR i := 0 TO l.GetLength() - 1 DO
											IF l.items[i].marked & (i # idx) THEN
												sb.Excl(sb, i, i);
												l.items[i].marked := FALSE;
												changed := TRUE;
											END
										END;
										IF ~ l.items[idx].marked THEN
											sb.Incl(sb, idx, idx);
											l.items[idx].marked := TRUE;
											changed := TRUE
										END
									END
								END
							END;
							IF l.pos # idx THEN
								l.pos := idx;
								changed := TRUE
							END;
							IF changed THEN
								l.ctl.ForceUpdate;
								changed := FALSE
							END
						END
					UNTIL ~isDown;
				
				END
			END
		END;
		IF makeCursorVisible THEN l.MakeCursorVisible END
	END MouseDown;


	(** **************** PopupCommon for ComboBox **************** **)

	PROCEDURE (p: PopupCommon) DrawFrame (c: Views.Frame; w, h: INTEGER), NEW;
		VAR x0, y0, x1, y1: INTEGER;
	BEGIN
		IF (w > 2 * c.dot) & (h > 2 * c.dot) THEN
			x1 := w - c.dot;
			y1 := h - c.dot;
			x0 := 0; y0 := 0;
			IF (x1 - x0 > 2 * c.dot) & (y1 - y0 > 2 * c.dot) THEN
				c.DrawLine(x0, y0, x1, y0, 0, param.frameDark);
				c.DrawLine(x0, y0, x0, y1, 0, param.frameDark);
				c.DrawLine(x0, y1, x1 + c.dot, y1, 0, param.frameDark);
				c.DrawLine(x1, y0, x1, y1, 0, param.frameDark);
				c.DrawLine(x0 + 1 * c.dot, y1 - 1 * c.dot, x1, y1 - 1 * c.dot, 0, param.frameShadow);
				c.DrawLine(x1 - 1 * c.dot, y0 + 1 * c.dot, x1 - 1 * c.dot, y1 - 1 * c.dot, 0, param.frameShadow)
			END
		END
	END DrawFrame;
	
	(* margins are in pixels *)
	PROCEDURE (p: PopupCommon) GetFrameMargins (OUT l, t, r, b: INTEGER), NEW;
	BEGIN
		l := 2; t := 1;
		r := 2; b := 2
	END GetFrameMargins;

	PROCEDURE (p: PopupCommon) GetBackground (VAR col: Ports.Color); 
	BEGIN
		col := param.backList
	END GetBackground;

	PROCEDURE (p: PopupCommon) GetItemHeight (): INTEGER, NEW;
		VAR asc, dsc, sw: INTEGER;
	BEGIN
		ASSERT(p.owner # NIL);
		p.owner.font.GetBounds(asc, dsc, sw);
		RETURN asc + dsc + 2 * p.owner.dot
	END GetItemHeight;

	PROCEDURE (p: PopupCommon) ForceUpdate, NEW;
	BEGIN
		Views.Update(p, Views.keepFrames)
		(*
		IF (p.owner # NIL) & (p.owner.view # NIL) THEN
			Views.Update(p.owner.view, Views.keepFrames)
		END
		*)
	END ForceUpdate;

	PROCEDURE (p: PopupCommon) GetItemCount (): INTEGER, NEW;
	BEGIN
		RETURN p.lst.GetLength()
	END GetItemCount;

	PROCEDURE (p: PopupCommon) GetCurrent (): INTEGER, NEW;
	BEGIN
		IF p.lst.unmap # NIL THEN
			IF p.lst.pos >= 0 THEN
				RETURN p.lst.unmap[p.lst.pos]
			ELSE
				RETURN p.lst.pos
			END
		ELSE
			RETURN p.lst.pos
		END
	END GetCurrent;

	PROCEDURE (p: PopupCommon) GetItemsPerPage (): INTEGER, NEW;
		VAR w, h, ih, res: INTEGER;
	BEGIN
		res := 1;
		IF (p.owner # NIL) & (p.context # NIL) THEN
			p.context.GetSize(w, h);
			IF h > 0 THEN
				ih := p.GetItemHeight();
				res := MAX(1, h DIV ih)
			END
		END;
		RETURN res
	END GetItemsPerPage;

	PROCEDURE^ (p: PopupCommon) MakeCursorVisible (doCenter: BOOLEAN), NEW;

	PROCEDURE (p: PopupCommon) NormaliseYOfs, NEW;
		VAR w, h, ih, ph: INTEGER;
	BEGIN
		IF p.starty = MIN(INTEGER) THEN p.MakeCursorVisible(TRUE) END;
		p.starty := MAX(0, p.starty);
		IF (p.owner # NIL) & (p.context # NIL) THEN
			p.context.GetSize(w, h);
			ih := p.GetItemHeight();
			ph := ih * p.GetItemCount();
			IF ph <= h THEN p.starty := 0
			ELSE p.starty := MIN(p.starty, h - ph)
			END
		END
	END NormaliseYOfs;

	PROCEDURE (p: PopupCommon) MakeCursorVisible (doCenter: BOOLEAN), NEW;
		VAR w, h, cy, ih, len, curidx: INTEGER;
	BEGIN
		IF p.starty = MIN(INTEGER) THEN p.starty := 0; doCenter := TRUE END;
		IF (p.owner # NIL) & (p.context # NIL) THEN
			curidx := p.GetCurrent();
			len := p.GetItemCount();
			IF (curidx >= 0) & (curidx < len) THEN
				p.context.GetSize(w, h);
				ih := p.GetItemHeight();
				cy := ih * curidx;
				DEC(cy, p.starty);
				IF cy < 0 THEN
					INC(p.starty, cy);
					IF doCenter THEN DEC(p.starty, (h - ih) DIV 2) END
				ELSIF cy + p.GetItemHeight() > h THEN
					INC(p.starty, cy + p.GetItemHeight() - h);
					IF doCenter THEN INC(p.starty, (h - ih) DIV 2) END
				END
			END;
			p.NormaliseYOfs
		ELSE p.starty := 0
		END
	END MakeCursorVisible;
	
	PROCEDURE (p: PopupCommon) SetCurrent (idx: INTEGER; doCenter: BOOLEAN), NEW;
	BEGIN
		IF (idx >= 0) & (idx < p.GetItemCount()) & (p.lst.Idx(idx) # p.lst.pos) THEN
			p.lst.pos := p.lst.Idx(idx);
			p.MakeCursorVisible(doCenter);
			p.ForceUpdate
		END
	END SetCurrent;

	PROCEDURE (p: PopupCommon) Restore (c: Views.Frame; left, top, right, bottom: INTEGER);
		VAR fnt: Fonts.Font; col: Ports.Color; l: ListCommon; tmp: TextChain;
			w, h, ih, iofs, asc, dsc, sw,
				f, x, y, curidx, len, ml, mt, mr, mb, pos, column: INTEGER;
	BEGIN
		p.GetFrameMargins(ml, mt, mr, mb);
		IF p.starty = MIN(INTEGER)THEN p.MakeCursorVisible(TRUE) END;
		p.dotsize := c.dot;
		p.NormaliseYOfs;
		p.context.GetSize(w, h);
		p.GetBackground(col);
		c.DrawRect(left, top, right, bottom, Ports.fill, col);
		fnt := p.owner.font;
		len := p.GetItemCount();
		curidx := p.GetCurrent();
		ih := p.GetItemHeight();
		fnt.GetBounds(asc, dsc, sw);
		iofs := asc + (ih - asc - dsc) DIV 2;
		x := (ml + 2) * c.dot;
		y := 0 - p.starty + mt * c.dot;
		f := 0; WHILE (f < len) & (y <= bottom) DO
			IF y + ih >= top THEN
				IF f = curidx THEN
					c.DrawRect(c.dot, y, w - c.dot, y + ih, Ports.fill, param.backSelList);
					col := param.textSelList
				ELSIF f = p.hoveridx THEN
					c.DrawRect(c.dot, y, w - c.dot, y + ih, Ports.fill, param.backHover);
					col := param.textHover
				ELSE
					col := param.text
				END;
				l := p.lst;
				IF l.items[l.Idx(f)].tabbed = NIL THEN
					c.DrawString(x, y + iofs, col, p.lst.items[p.lst.Idx(f)].text^, fnt)
				ELSE
					tmp := l.items[l.Idx(f)].tabbed;
					column := 0;
					pos := 0;
					WHILE tmp # NIL DO
						c.DrawString(x + pos, y + iofs, col, tmp.text, fnt);
						tmp := tmp.next;
						INC(column);
						pos := pos + l.widths[column-1] + listTabSize * c.dot
					END
				END
			END;
			INC(y, ih);
			INC(f)
		END;
		p.DrawFrame(c, w, h)
	END Restore;

	PROCEDURE (p: PopupCommon) ClosePopup, NEW;
		VAR op: StdDocuments.OverlayProposal;
	BEGIN
		IF ~p.hideCalled THEN
			p.hideCalled := TRUE;
			op.op := StdDocuments.hide;
			p.context.Consider(op)
		END
	END ClosePopup;

	PROCEDURE (p: PopupCommon) CopyFromModelView- (source: Views.View; model_: Models.Model);
	BEGIN
		WITH source: PopupCommon DO
			p.owner := source.owner;
			p.lst := source.lst;
			p.starty := source.starty;
			p.hoveridx := -1
		ELSE END
	END CopyFromModelView;

	PROCEDURE (p: PopupCommon) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN), NEW;
		VAR w, h, ih: INTEGER;
	BEGIN
		IF (p.owner # NIL) & (p.context # NIL) THEN
			p.context.GetSize(w, h);
			IF (x >= 0) & (y >= 0) & (x < w) & (y < h) THEN
				done := TRUE; (* eat it! *)
				IF op = Controllers.decLine THEN nofLines := -nofLines END;
				ih := p.GetItemHeight();
				INC(p.starty, nofLines * ih);
				p.NormaliseYOfs;
				p.ForceUpdate
			END
		END
	END WheelMove;

	PROCEDURE (p: PopupCommon) ItemAtY (y: INTEGER): INTEGER, NEW;
	BEGIN
		RETURN (y + p.starty + 1 * p.dotsize) DIV p.GetItemHeight()
	END ItemAtY;

	PROCEDURE (p: PopupCommon) MouseDown (x_, y: INTEGER; buttons_: SET), NEW;
	BEGIN
		IF (p.owner # NIL) & (p.context # NIL) THEN
			p.SetCurrent(p.ItemAtY(y), FALSE);
			p.ClosePopup
		END
	END MouseDown;

	PROCEDURE (p: PopupCommon) MouseMoved (x_, y: INTEGER; buttons_: SET), NEW;
		VAR hidx: INTEGER;
	BEGIN
		IF (p.owner # NIL) & (p.context # NIL) THEN
			hidx := p.ItemAtY(y);
			IF hidx # p.hoveridx THEN
				p.hoveridx := hidx;
				p.ForceUpdate
			END
		END
	END MouseMoved;

	PROCEDURE (p: PopupCommon) KeyDown (ch: CHAR; mods_: SET): BOOLEAN, NEW;
		VAR idx, len: INTEGER; res: BOOLEAN;
	BEGIN
		res := TRUE;
		CASE ch OF
		| PU: (* page up *)
			len := p.GetItemCount();
			IF len > 0 THEN
				idx := MAX(0, p.GetCurrent()-p.GetItemsPerPage());
				p.SetCurrent(idx, FALSE)
			END
		| PD: (* page down *)
			len := p.GetItemCount();
			IF len > 0 THEN
				idx := MIN(len - 1, p.GetCurrent()+p.GetItemsPerPage());
				p.SetCurrent(idx, FALSE)
			END
		| DL: (* home *)
			p.SetCurrent(0, FALSE)
		| DR: (* end *)
			p.SetCurrent(p.GetItemCount() - 1, FALSE)
		| AU: (* arrow up *)
			p.SetCurrent(p.GetCurrent() - 1, FALSE)
		| AD: (* arrow down *)
			p.SetCurrent(p.GetCurrent() + 1, FALSE)
		| ENTER:
			p.SetCurrent(p.GetCurrent(), FALSE);
			p.ClosePopup
		| ESC, PL, PR:
			p.ClosePopup
		ELSE res := FALSE
		END;
		RETURN res
	END KeyDown;

	PROCEDURE (p: PopupCommon) HandleCtrlMsg* (c_: Views.Frame; VAR msg: Views.CtrlMessage;
																						VAR focus: Views.View);
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			p.MouseDown(msg.x, msg.y, msg.modifiers)
		|msg: Controllers.WheelMsg DO
			p.WheelMove(msg.x, msg.y, msg.op, msg.nofLines, msg.done)
		| msg: Controllers.EditMsg DO
			IF msg.modifiers * {0..7} = {} THEN
				IF p.KeyDown(msg.char, msg.modifiers) THEN END
			ELSIF Controllers.pick IN msg.modifiers THEN
				IF msg.char = AU THEN p.ClosePopup END
			END
		| msg: Controllers.PollCursorMsg DO
			p.MouseMoved(msg.x, msg.y, msg.modifiers)
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE (p: PopupCommon) HandleViewMsg- (c_: Views.Frame; VAR msg: Views.Message);
		VAR c: StdCFrames.Frame;
	BEGIN
		WITH msg: StdDocuments.RemoveOverlayMsg DO
			c := p.owner;
			IF c # NIL THEN c.PopupClosed(p) END
		ELSE END
	END HandleViewMsg;
	
	PROCEDURE (l: ListCommon) NewPopup (owner: StdCFrames.Frame): PopupCommon, NEW;
		VAR p: PopupCommon;
	BEGIN
		NEW(p);
		p.owner := owner;
		p.lst := l;
		p.dotsize := 0;
		p.starty := MIN(INTEGER);
		p.hoveridx := -1;
		p.hideCalled := FALSE;
		RETURN p
	END NewPopup;


	PROCEDURE (c: ComboBox) UpdateEd, NEW;
		VAR s: Dialog.String;
	BEGIN
		c.ed.SetupCtl(c);
		c.ed.changeCount := 0;
		c.ed.maxLen := 255;
		c.ed.SetupCtl(c);
		c.Get(c, s);
		IF c.ed.SetText(s) THEN INC(c.ed.changeCount) END
	END UpdateEd;

	PROCEDURE (c: ComboBox) UpdateLCtl, NEW;
	BEGIN
		c.lst.SetupCtl(c);
		c.UpdateEd;
		IF c.lst.UpdateFromCtl() THEN c.fixCursor := TRUE END
	END UpdateLCtl;

	PROCEDURE (a: InsertPopupCommonAction) Do;
	VAR doc: Views.View; root: Views.Frame;
		w, h, f, px, py, pw, ph, ml, mt, mr, mb, fw, fh: INTEGER;
		st: Storage;
	BEGIN
		IF a.list.ctl.rider # NIL THEN
			root := Views.UltimateRootOf(a.list.ctl); ASSERT(root # NIL);
			ASSERT(root.view # NIL); doc := root.view;
			
			a.popup.GetFrameMargins(ml, mt, mr, mb);
			fw := MAX(0, (ml + mr) * a.list.ctl.dot);
			fh := MAX(0, (mt + mb) * a.list.ctl.dot);
			a.list.ctl.view.context.GetSize(w, h);
			FOR f := 0 TO a.len - 1 DO
				w := MAX(w, a.list.ctl.font.StringWidth(a.list.items[f].text) + fw)
			END;
			px := a.list.ctl.gx - root.gx; py := a.list.ctl.gy - root.gy + a.yofs;
			pw := MIN(w, root.r - root.l);
			ph := a.popup.GetItemHeight() * a.len  + fh;
			ph := MIN(ph, root.b - root.t);
			px := MAX(0, MIN(px, root.r - pw));
			py := MAX(0, MIN(py, root.b - ph));
			
			WITH doc: StdDocuments.Document DO
				StdDocuments.InsertOverlaid(a.popup, a.popup, doc, px, py, pw, ph, NIL);
				NEW(a.popup.monitorAction);
				a.popup.monitorAction.popup := a.popup;
				Services.DoLater(a.popup.monitorAction,
					Services.Ticks() + Services.resolution DIV 10)
			END
		ELSIF a.list.ctl.view # NIL THEN
			st := GetStorage(a.list.ctl.view);
			IF (st # NIL) & (st IS ListStorage) THEN
				st(ListStorage).showOnRestore := TRUE
			END
		END
	END Do;

	PROCEDURE (l: ListCommon) ShowPopup (yofs: INTEGER): PopupCommon, NEW;
		VAR popup: PopupCommon;
			ia: InsertPopupCommonAction; len: INTEGER;
	BEGIN
		popup := NIL;
		IF (l.ctl # NIL) & ~l.ctl.readOnly & ~l.ctl.disabled THEN
			len := l.GetLength();
			IF len > 0 THEN
				popup := l.NewPopup(l.ctl);
				NEW(ia);
				ia.popup := popup;
				ia.yofs := yofs;
				ia.list := l;
				ia.len := len;
				Services.DoLater(ia, Services.Ticks() + Services.resolution DIV 10)
			END
		END;
		RETURN popup
	END ShowPopup;

	(** **************** PopupCommon for Color **************** **)

	PROCEDURE (p: PopupColor) DrawFrame (c: Views.Frame; w, h: INTEGER), NEW;
		VAR x0, y0, x1, y1: INTEGER;
	BEGIN
		IF (w > 2 * c.dot) & (h > 2 * c.dot) THEN
			x1 := w - c.dot;
			y1 := h - c.dot;
			x0 := 0; y0 := 0;
			IF (x1 - x0 > 2 * c.dot) & (y1 - y0 > 2 * c.dot) THEN
				c.DrawLine(x0, y0, x1, y0, 0, param.frameDark);
				c.DrawLine(x0, y0, x0, y1, 0, param.frameDark);
				c.DrawLine(x0, y1, x1 + c.dot, y1, 0, param.frameDark);
				c.DrawLine(x1, y0, x1, y1, 0, param.frameDark);
				c.DrawLine(x0 + 1 * c.dot, y1 - 1 * c.dot, x1, y1 - 1 * c.dot, 0, param.frameShadow);
				c.DrawLine(x1 - 1 * c.dot, y0 + 1 * c.dot, x1 - 1 * c.dot, y1 - 1 * c.dot, 0, param.frameShadow)
			END
		END
	END DrawFrame;

	(* margins are in pixels *)
	PROCEDURE (p: PopupColor) GetFrameMargins (OUT l, t, r, b: INTEGER), NEW;
	BEGIN
		l := 2; t := 1;
		r := 2; b := 2
	END GetFrameMargins;

	PROCEDURE (p: PopupColor) GetBackground (VAR col: Ports.Color); 
	BEGIN
		col := param.backList
	END GetBackground;

	PROCEDURE (p: PopupColor) ForceUpdate, NEW;
	BEGIN
		Views.Update(p, Views.keepFrames)
		(* 
		IF (p.owner # NIL) & (p.owner.view # NIL) THEN
			Views.Update(p.owner.view, Views.keepFrames)
		END
		*)
	END ForceUpdate;

	PROCEDURE (p: PopupColor) Restore (f: Views.Frame; l_, t_, r_, b_: INTEGER);
		VAR w, h, x, y, i, m: INTEGER; dot: INTEGER;
	BEGIN
		f.view.context.GetSize(w, h);
		y := 0; dot := f.dot;
		FOR i := 0 TO LEN(colors) - 1 DO
			m := Ports.point * 2;
			f.DrawRect(
					m, y + p.yShift + m, w - m - Ports.point, y + p.yShift + p.itemHeight, Ports.fill, colors[i]);
			IF p.hoveridx = i THEN
				INC(m, f.dot);
				f.MarkRect(
					m, y + p.yShift + m, 
					w - m - Ports.point, y + p.yShift + p.itemHeight - f.dot,
					0, Ports.invert, Ports.show)
			END;
			INC(y, p.itemHeight)
		END;
		
		(* ... *)
		x := w DIV 2;
		f.DrawRect(
			x - 4 * Ports.point - dot, y + p.yShift + p.itemHeight,
			x - 4 * Ports.point + dot, y + p.yShift + p.itemHeight + 2 * dot, Ports.fill, Ports.black);
		f.DrawRect(
			x - dot, y + p.yShift + p.itemHeight,
			x + dot, y + p.yShift + p.itemHeight + 2 * dot, Ports.fill, Ports.black);
		f.DrawRect(
			x + 4 * Ports.point - dot, y + p.yShift + p.itemHeight,
			x + 4 * Ports.point + dot, y + p.yShift + p.itemHeight + 2 *dot, Ports.fill, Ports.black);
		
		p.DrawFrame(f, w, h)
	END Restore;

	PROCEDURE (p: PopupColor) ClosePopup, NEW;
		VAR op: StdDocuments.OverlayProposal;
	BEGIN
		IF ~p.hideCalled THEN
			p.hideCalled := TRUE;
			op.op := StdDocuments.hide;
			p.context.Consider(op)
		END
	END ClosePopup;

	PROCEDURE (p: PopupColor) CopyFromModelView- (source: Views.View; model_: Models.Model);
	BEGIN
		WITH source: PopupColor DO
			p.colorField := source.colorField
		END
	END CopyFromModelView;

	PROCEDURE (p: PopupColor) WheelMove (x_, y_, op_, nofLines_: INTEGER; VAR done: BOOLEAN), NEW;
	BEGIN
	END WheelMove;

	PROCEDURE (a: ColorAction) Do;
	BEGIN
		IF ~a.cancel THEN
			a.colorItem.PutIntVal(a.out);
		END
	END Do;

	PROCEDURE (p: PopupColor) MouseDown (x_, y: INTEGER; buttons_: SET), NEW;
		VAR index, col: INTEGER; colorAction: ColorAction;
	BEGIN
		IF (p.colorField # NIL) & (p.context # NIL) THEN
			index := y DIV p.itemHeight;
			IF (index >= 0) & (index < LEN(colors)) THEN
				ASSERT(p.colorField.view # NIL, 21);
				p.colorField.Set(p.colorField, colors[index]);
				p.colorField.Update
			ELSE
				NEW(colorAction);
				colorAction.colorItem := p.colorField.view(Controls.Control).item;
				colorAction.title := p.colorField.view(Controls.Control).label;
				p.colorField.Get(p.colorField, col);
				Dialog.GetCol(col, colorAction)
			END;
			p.ClosePopup
		END
	END MouseDown;

	PROCEDURE (p: PopupColor) MouseMoved (x_, y: INTEGER; buttons_: SET), NEW;
		VAR index: INTEGER;
	BEGIN
		IF (p.colorField # NIL) & (p.context # NIL) THEN
			index := y DIV p.itemHeight;
			IF p.hoveridx # index THEN
				p.hoveridx := index;
				p.ForceUpdate
			END
		END
	END MouseMoved;

	PROCEDURE (p: PopupColor) KeyDown (ch: CHAR; mods_: SET): BOOLEAN, NEW;
		VAR res: BOOLEAN;
	BEGIN
		res := TRUE;
		CASE ch OF
		| AU: (* arrow up *)
			IF p.hoveridx > 0 THEN
				DEC(p.hoveridx);
				p.ForceUpdate
			END
		| AD: (* arrow down *)
			IF p.hoveridx < numColors - 1  THEN
				INC(p.hoveridx);
				p.ForceUpdate
			END
		| ENTER:
			p.colorField.Set(p.colorField, colors[p.hoveridx]);
			p.ClosePopup
		| ESC:
			p.ClosePopup
		ELSE res := FALSE
		END;
		RETURN res 
	END KeyDown;

	PROCEDURE (p: PopupColor) HandleCtrlMsg* (c_: Views.Frame; VAR msg: Views.CtrlMessage;
																						VAR focus: Views.View);
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			p.MouseDown(msg.x, msg.y, msg.modifiers)
		|msg: Controllers.WheelMsg DO
			p.WheelMove(msg.x, msg.y, msg.op, msg.nofLines, msg.done)
		| msg: Controllers.EditMsg DO
			IF msg.modifiers * {0..7} = {} THEN
				IF p.KeyDown(msg.char, msg.modifiers) THEN END
			ELSIF Controllers.pick IN msg.modifiers THEN
				IF msg.char = AU THEN p.ClosePopup END
			END
		| msg: Controllers.PollCursorMsg DO
			p.MouseMoved(msg.x, msg.y, msg.modifiers)
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE (p: PopupColor) HandleViewMsg- (c_: Views.Frame; VAR msg: Views.Message);
		VAR c: StdCFrames.Frame;
	BEGIN
		WITH msg: StdDocuments.RemoveOverlayMsg DO
			c := p.colorField;
			IF c # NIL THEN c.PopupClosed(p) END
		ELSE END
	END HandleViewMsg;

	PROCEDURE (l: ColorField) NewPopup (cf: ColorField): PopupColor, NEW;
		VAR p: PopupColor;
	BEGIN
		NEW(p);
		p.colorField := cf;
		p.hoveridx := -1;
		p.yShift := 0;
		p.hideCalled := FALSE;
		RETURN p
	END NewPopup;

	PROCEDURE (a: MonitorPopupColorAction) Do;
	BEGIN
		IF a.popup.colorField.view = NIL THEN
			a.popup.ClosePopup
		ELSE
			Services.DoLater(a, Services.Ticks() + Services.resolution DIV 5)
		END
	END Do;

	PROCEDURE (a: MonitorPopupCommonAction) Do;
	BEGIN
		IF a.popup.owner.view = NIL THEN
			a.popup.ClosePopup
		ELSE
			Services.DoLater(a, Services.Ticks() + Services.resolution DIV 5)
		END
	END Do;

	PROCEDURE (a: InsertPopupColorAction) Do;
	VAR doc: Views.View; root: Views.Frame;
		w, h, pw, ph, ml, mt, mr, mb, fh: INTEGER;
	BEGIN
		IF a.cf.rider # NIL THEN
			
			root := Views.UltimateRootOf(a.cf);
			
			ASSERT(root # NIL);
			ASSERT(root.view # NIL);
			
			doc := root.view;
			
			a.popup.GetFrameMargins(ml, mt, mr, mb);
			fh := MAX(0, (mt + mb) * a.cf.dot);
			
			a.cf.view.context.GetSize(w, h);
			
			a.popup.px := a.cf.gx - root.gx;
			a.popup.py := a.cf.gy - root.gy + a.yofs;
			
			pw := MIN(w, root.r - root.l);
			a.popup.itemHeight:= 10 * Ports.point;
			ph := (LEN(colors) + 2) * a.popup.itemHeight  + fh;
			ph := MIN(ph, root.b - root.t);
			
			a.popup.px := MAX(0, MIN(a.popup.px, root.r - pw));
			a.popup.py := MAX(0, MIN(a.popup.py, root.b - ph));
			
			WITH doc: StdDocuments.Document DO
				StdDocuments.InsertOverlaid(a.popup, a.popup, doc, a.popup.px, a.popup.py, pw, ph, NIL);
				NEW(a.popup.monitorAction);
				a.popup.monitorAction.popup := a.popup;
				Services.DoLater(a.popup.monitorAction,
					Services.Ticks() + Services.resolution DIV 10);
			ELSE
			END
		END
	END Do;

	PROCEDURE (l: ColorField) ShowPopupColor (yofs: INTEGER): PopupColor, NEW;
		VAR popup: PopupColor; ia: InsertPopupColorAction;
	BEGIN
		popup := NIL;
		IF ~l.readOnly & ~l.disabled & (l.rider # NIL) THEN
			NEW(ia);
			popup := l.NewPopup(l);
			ia.popup := popup;
			ia.yofs := yofs;
			ia.cf := l;
			Services.DoLater(ia, Services.Ticks() + Services.resolution DIV 10);
		END;
		RETURN popup
	END ShowPopupColor;

	PROCEDURE (c: ColorField) PopupClosed (popup_: ANYPTR);
	BEGIN
		c.closedTime := Services.Ticks();
		IF c.popupOverlay # NIL THEN
			Services.RemoveAction(c.popupOverlay.monitorAction);
			c.popupOverlay := NIL;
		END;
		c.Update
	END PopupClosed;



	PROCEDURE DrawArrowUp (c: StdCFrames.Frame; x, y, s: INTEGER);
	BEGIN
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y + s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y + s, c.dot, param.arrowsColor);
	END DrawArrowUp;

	PROCEDURE DrawArrowDown (c: StdCFrames.Frame; x, y, s: INTEGER);
	BEGIN
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y - s, c.dot, param.arrowsColor);
	END DrawArrowDown;

	PROCEDURE DrawArrowLeft (c: StdCFrames.Frame; x, y, s: INTEGER);
	BEGIN
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x + s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x + s, y + s, c.dot, param.arrowsColor);
	END DrawArrowLeft;

	PROCEDURE DrawArrowRight (c: StdCFrames.Frame; x, y, s: INTEGER);
	BEGIN
		s := s - s MOD c.unit; x := x - x MOD c.unit; y := y - y MOD c.unit;
		c.DrawLine(x, y, x - s, y - s, c.dot, param.arrowsColor);
		c.DrawLine(x, y, x - s, y + s, c.dot, param.arrowsColor);
	END DrawArrowRight;

	(* processing '&' and "&&"; return amp position and width *)
	PROCEDURE CleanAmp (VAR s: ARRAY OF CHAR; font: Fonts.Font; OUT awdt: INTEGER): INTEGER;
		VAR tmp: ARRAY 2 OF CHAR; i, j, apos: INTEGER;
	BEGIN
		apos := -1; awdt := -1; i := 0; j := 0;
		WHILE s[i] # 0X DO
			IF (s[i] = '&') & (s[i + 1] # 0X) THEN
				INC(i);
				IF (apos < 0) & (s[i] # '&') THEN apos := j END
			END;
			s[j] := s[i]; INC(i); INC(j)
		END;
		s[j] := 0X;
		IF apos >= 0 THEN
			ASSERT(apos < LEN(s$));
			i := apos;
			tmp[0] := s[i]; tmp[1] := 0X;
			s[apos] := 0X; apos := font.StringWidth(s); s[i] := tmp[0];
			awdt := font.StringWidth(tmp)
		END;
		awdt := MAX(0, awdt);
		RETURN apos
	END CleanAmp;

	PROCEDURE DrawAmpString (c: StdCFrames.Frame; x, y: INTEGER; col: Ports.Color; IN s: ARRAY OF CHAR;
														apos, awdt: INTEGER);
	BEGIN
		IF (LEN(s) > 0) & (s[0] # 0X) THEN
			c.DrawString(x, y, col, s, c.font);
			IF awdt > c.dot THEN
				INC(y, 1 * c.dot);
				c.DrawRect(x + apos, y, x + apos + awdt, y + c.dot, Ports.fill, col)
			END
		END
	END DrawAmpString;
	

	(** **************** Caption **************** **)

	PROCEDURE (ed: Caption) PrepareExplodedCaption (width: INTEGER), NEW;
		VAR
			pos, pos2, lastSpacePos, lastSpacePos2, line: INTEGER;
			str: SingleLine; lineBreak: BOOLEAN;
			tx: ARRAY 2048 OF CHAR; tmp: EditorCommonExpText;
		
		PROCEDURE AddStringToList (str: SingleLine);
		VAR n: INTEGER; 
		BEGIN
			IF ed.textExp = NIL THEN
				NEW(ed.textExp);
				ed.textExp.era := ed.changeCount;
				ed.textExp.str := str;
				ed.textExp.num := 1;
			ELSE
				n := 2;
				tmp := ed.textExp;
				WHILE tmp.next # NIL DO
					INC(n);
					tmp := tmp.next;
				END;
				NEW(tmp.next);
				tmp.next.str := str;
				tmp.next.num := n;
			END
		END AddStringToList;
		
	BEGIN
		IF (ed.textExp # NIL) & (ed.textExp.era = ed.changeCount) THEN RETURN END;
		ed.textExp := NIL;
		Dialog.MapString(ed.label, tx);
		
		pos := 0;
		line := 0;
		WHILE (tx[pos] # 0X) & (line < 1024) DO  (* prevent hanging in case of trubles with parsing *) 
			pos2 := 0;
			NEW(str);
			lineBreak := FALSE;
			lastSpacePos := 0; lastSpacePos2 := 0;
			WHILE (tx[pos] # 0X) & (~lineBreak) DO
				IF tx[pos] = " " THEN
					lastSpacePos := pos;
					lastSpacePos2 := pos2;
				END;
				str[pos2] := tx[pos];
				str[pos2+1] := 0X;
				IF ed.font.StringWidth(str) > width THEN
					lineBreak := TRUE;
					IF lastSpacePos # 0 THEN
						(* forward count untill space *)
						pos := lastSpacePos + 1;
						pos2 := lastSpacePos2 + 1
					END
				ELSIF tx[pos] = ENTER THEN
					lineBreak := TRUE;
					INC(pos); INC(pos2)
				ELSE
					INC(pos); INC(pos2)
				END
			END;
			str[pos2] := 0X;
			AddStringToList(str);
			INC(line)
		END;
		IF (pos >= 1) & (tx[pos-1] = ENTER) THEN
			NEW(str);
			str[0] := 0X;
			AddStringToList(str)
		END
	END PrepareExplodedCaption;
	
	PROCEDURE (c: Caption) Restore (l_, t_, r_, b_: INTEGER);
		VAR x, w, h, col, asc, dsc, sw, apos, awdt, len, n: INTEGER;
			s: Dialog.String; tmp: EditorCommonExpText;
	BEGIN
		c.view.context.GetSize(w, h);
		IF c.disabled THEN col := param.textDisabled
		ELSE col := param.text
		END;
		c.font.GetBounds(asc, dsc, sw);
		Dialog.MapString(c.label, s);
		
		IF c.font.StringWidth(s) > w THEN
			c.PrepareExplodedCaption(w);
			tmp := c.textExp;
			n := asc - dsc;
			tmp := c.textExp;
			WHILE tmp # NIL DO
				IF tmp.str$ # "" THEN
					len := LEN(tmp.str$) - 1;
					IF tmp.str[len] = 0DX THEN
						tmp.str[len] := 0X;
						IF c.left THEN x := c.dot
						ELSIF c.right THEN x := w - c.dot - c.font.StringWidth(tmp.str)
						ELSE x := (w - c.font.StringWidth(tmp.str)) DIV 2
						END;
						c.DrawString(x, n, col, tmp.str, c.font);
						tmp.str[len] := 0DX
					ELSE
						IF c.left THEN x := c.dot
						ELSIF c.right THEN x := w - c.dot - c.font.StringWidth(tmp.str)
						ELSE x := (w - c.font.StringWidth(tmp.str)) DIV 2
						END;
						c.DrawString(x, n, col, tmp.str, c.font)
					END
				END;
				INC(n, dsc + asc);
				tmp := tmp.next
			END
		ELSE
			apos := CleanAmp(s, c.font, awdt);
			IF c.left THEN x := c.dot
			ELSIF c.right THEN x := w - c.dot - c.font.StringWidth(s)
			ELSE x := (w - c.font.StringWidth(s)) DIV 2
			END;
			DrawAmpString(c, x, (h - dsc + asc) DIV 2, col, s, apos, awdt)
		END
	END Restore;

	PROCEDURE (c: Caption) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;
	
	PROCEDURE (f: Caption) Update;
	BEGIN
		f.Update^
	END Update;


	(** **************** Group **************** **)

PROCEDURE (c: Group) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, asc, dsc, sw, tw, apos, awdt, col: INTEGER; s: Dialog.String;
	BEGIN
		IF c.view # NIL THEN
			c.view.context.GetSize(w, h);
			Dialog.MapString(c.label, s);
			apos := CleanAmp(s, c.font, awdt);
			c.font.GetBounds(asc, dsc, sw);
			tw := c.font.StringWidth(s);
			IF c.disabled THEN
				col := param.frameDisabled
			ELSIF c.readOnly THEN
				col := param.frameReadOnly
			ELSE
				col := param.frame
			END;
			IF h - Ports.point > (asc + dsc) * 2 DIV 3 THEN
				h := h - Ports.point
			ELSE
				h := (asc + dsc) * 2 DIV 3 + Ports.point
			END;
			c.DrawRect(
				Ports.point, (asc + dsc) * 2 DIV 3, w - Ports.point, h, Ports.point, col);
			IF LEN(s$) > 0 THEN
				c.DrawRect(Ports.mm * 3 DIV 2, 0, Ports.mm * 3 + tw, asc + dsc, Ports.fill, Ports.dialogBackground);
				IF c.disabled THEN col := param.textDisabled ELSE col := param.text END;
				DrawAmpString(c, Ports.mm * 2 , asc + dsc, col, s, apos, awdt)
			END
		END
	END Restore;

	PROCEDURE (c: Group) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: Group) Update;
	BEGIN
		c.Update^
	END Update;


	(** **************** PushButton **************** **)

	PROCEDURE Execute (c: PushButton);
	BEGIN
		IF c.Do # NIL THEN
			Dialog.ShowStatus("");
			c.Do(c)
		END
	END Execute;

	PROCEDURE (c: PushButton) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, res, asc, dsc, sw, x, y, apos, awdt, col: INTEGER; s: Dialog.String;
	BEGIN
		c.view.context.GetSize(w, h);
		Dialog.MapString(c.label, s);
		apos := CleanAmp(s, c.font, awdt);
		c.font.GetBounds(asc, dsc, sw);
		(* for text *)
		res := c.font.StringWidth(s);
		y := (h - 3 * c.dot - dsc + asc) DIV 2 + c.dot (* h DIV 2 + (asc - dsc) DIV 2 *);
		x := (w - res) DIV 2;
		IF c.disabled THEN
			c.DrawRect(0, 0, w, h, Ports.fill, param.backDisabled);
			c.DrawString(x, y, param.textDisabled, s, c.font);
			c.DrawRect(0, 0, w, h, c.dot, param.frameDisabled);
		ELSIF c.readOnly THEN
			c.DrawRect(0, 0, w, h, Ports.fill, param.backReadOnly);
			c.DrawString(x, y, param.textReadOnly, s, c.font);
			c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly);
		ELSE
			IF c.pressed THEN col := param.backPressed ELSE col := param.back END;
			c.DrawRect(0, 0, w, h, Ports.fill, col);
			IF c.pressed THEN col := param.textPressed ELSE col := param.text END;
			DrawAmpString(c, x, y, col, s, apos, awdt);
			IF c.default THEN
				c.DrawRect(0, 0, w, h, 2 * c.dot, Ports.RGBColor(51, 153, 255))
			ELSIF c.mark THEN
				c.DrawRect(0, 0, w, h, c.dot , param.frameFocus)
			ELSE
				c.DrawRect(0, 0, w, h, c.dot, param.frame)
			END;
			IF c.mark THEN
				c.MarkRect(2 * c.dot, 2 * c.dot, w - 2 * c.dot, h - 2 * c.dot, c.dot, Ports.dim50, Ports.show)
			END
		END
	END Restore;

	PROCEDURE (c: PushButton) MouseDown (x_, y_: INTEGER; buttons_: SET);
	BEGIN
		ASSERT(~c.disabled, 100);
		IF TrackMouseDown(c) THEN Execute(c) END
	END MouseDown;

	PROCEDURE (c: PushButton) KeyDown (ch_: CHAR; modifiers_: SET);
	BEGIN
		ASSERT(~c.disabled, 100);
		Execute(c)
	END KeyDown;

	PROCEDURE (c: PushButton) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: PushButton) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: PushButton) Update;
	BEGIN
		c.Update^
	END Update;


	(** **************** CheckBox **************** **)
	
	PROCEDURE (c: CheckBox) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, h2, res, asc, dsc, sw, awdt, apos, sz, ss: INTEGER;
			col: Ports.Color; s: Dialog.String; active: BOOLEAN; root: Views.Frame;
	BEGIN
		c.noRedraw := FALSE;
		IF c # NIL THEN
			c.Get(c, active);
			c.view.context.GetSize(w, h);
			IF c.disabled THEN col := param.frameDisabled ELSE col := param.frame END;
			h2 := h DIV 2;
			param.checkBoxFont.GetBounds(asc, dsc, sw);
			sz := asc + dsc - c.dot;
			c.font.GetBounds(asc, dsc, sw);
			IF asc + dsc - c.dot > sz THEN sz := asc + dsc - c.dot END;
			IF sz > h THEN sz := h - c.dot END;
			ss := sz DIV 2;
			IF c.disabled THEN
				col := param.textDisabled
			ELSIF c.readOnly THEN
				col := param.textReadOnly
			ELSE
				col := param.text
			END;
			c.DrawRect(c.dot, h2 - ss, sz+c.dot, h2 + ss, c.dot, col);
			Dialog.MapString(c.label, s);
			apos := CleanAmp(s, c.font, awdt);
			DrawAmpString(c, sz + 5 * c.dot, (h - dsc + asc) DIV 2, col, s, apos, awdt);
			IF active & ~ c.disabled THEN
				res := param.checkBoxFont.StringWidth("V");
				c.DrawString((sz - res) DIV 2, (h + asc) DIV 2 - 1 * c.dot, col, "V", param.checkBoxFont);
				root := Views.HostOf(c);
				IF root.view IS TextViews.View THEN
					col := Ports.background 
				ELSE
					col := Ports.dialogBackground
				END;
				c.DrawRect(2 * c.dot, h2-ss+c.dot, sz DIV 2, (h2-ss)+ss-c.dot, Ports.fill, col);
				c.DrawRect(2 * c.dot, h2 - ss + c.dot, sz, h2 + ss - c.dot, c.dot, col);
			END;
			IF c.mark & ~ c.disabled THEN
				res := c.font.StringWidth(s);
				asc := c.dot;
				IF asc < h2 - ss THEN asc := h2 - ss END;
				dsc := h - c.dot;
				IF dsc > h2 + ss THEN dsc := h2 + ss END;
				c.MarkRect(
					sz + 4 * c.dot, asc,
					sz + 7 * c.dot + res, dsc,
					c.dot, Ports.dim50, Ports.show);
			END
		END
	END Restore;

	PROCEDURE (c: CheckBox) MouseDown (x_, y_: INTEGER; buttons_: SET);
		VAR state: BOOLEAN;
	BEGIN
		ASSERT(~c.disabled, 100);
		IF TrackMouseDown(c) THEN
			c.Get(c, state); c.Set(c, ~state); c.Update
		END
	END MouseDown;

	(* `Controls` module guarantees that we'll get only hotkeys and space here *)
	PROCEDURE (c: CheckBox) KeyDown (ch_: CHAR; modifiers_: SET);
		VAR state: BOOLEAN;
	BEGIN
		ASSERT(~c.disabled, 100);
		c.Get(c, state); c.Set(c, ~state); c.Update
	END KeyDown;

	PROCEDURE (c: CheckBox) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update
	END Mark;

	PROCEDURE (c: CheckBox) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;


	PROCEDURE AddStorage (new: Storage);
		VAR st: Storage;
	BEGIN
		IF storages = NIL THEN storages := new ELSE
			st := storages;
			WHILE (st.next # NIL) & (st.view # new.view) DO
				st := st.next
			END;
			IF st.next = NIL THEN
				st.next := new
			END
		END
	END AddStorage;
	


	(** **************** RadioButton **************** **)

	PROCEDURE (c: RadioButton) Add(): Storage, NEW;
		VAR nst: RadioButtonStorage; 
	BEGIN
		NEW(nst);
		nst.view := c.view;
		nst.hasFocus := FALSE;
		AddStorage(nst);
		RETURN nst
	END Add;

	PROCEDURE (c: RadioButton) Restore (l_, t_, r_, b_: INTEGER);
		VAR
			w, h, asc, dsc, sw, res, h2, apos, awdt, rad1, rad2: INTEGER;
			col: Ports.Color; s: Dialog.String;
			active: BOOLEAN;
			st: Storage;
	BEGIN
		st := GetStorage(c.view);
		IF (st = NIL) & (c.view # NIL) THEN st := c.Add() END;
		WITH st : RadioButtonStorage DO
			c.Get(c, active);
			c.view.context.GetSize(w, h);
			IF c.disabled THEN
				col := param.textDisabled
			ELSIF c.readOnly THEN
				col := param.textReadOnly
			ELSE
				col := param.text
			END;
			
			h2 := h DIV 2;
			
			rad1 :=  5 * Ports.point - (5 * Ports.point) MOD c.unit;
			rad2 :=  3 * Ports.point - (3 * Ports.point) MOD c.unit;
			IF c.bgColor # -1 THEN
				c.DrawOval(c.unit, h2 - rad1, rad1 + c.unit + rad1, h2 + rad1, Ports.fill, c.bgColor)
			END;
			c.DrawOval(c.unit, h2 - rad1, rad1 + c.unit + rad1, h2 + rad1, c.dot, col);
			IF active & ~c.disabled THEN
				c.DrawOval(rad1 + c.unit - rad2, h2 - rad2, rad1 + c.unit + rad2, h2 + rad2, Ports.fill , col)
			END;
			Dialog.MapString(c.label, s);
			apos := CleanAmp(s, c.font, awdt);
			c.font.GetBounds(asc, dsc, sw);
			DrawAmpString(c, 16 * Ports.point, (h - dsc + asc) DIV 2, col, s, apos, awdt);
			
			IF c.mark OR st.hasFocus THEN
				res := c.font.StringWidth(s);
				c.DrawLine(
					16 * Ports.point, h DIV 2 + asc DIV 2 + 2 * c.dot,
					16 * Ports.point + res, h DIV 2 + asc DIV 2 + 2 * c.dot, c.dot, col)
			END
			
		END
	END Restore;

	PROCEDURE (c: RadioButton) MouseDown (x_, y_: INTEGER; buttons_: SET);
		VAR state: BOOLEAN;
	BEGIN
		ASSERT(~c.disabled, 100);
		IF TrackMouseDown(c) THEN
			c.Get(c, state);
			IF ~state THEN c.Set(c, TRUE) END
		END
	END MouseDown;

	(* `Controls` module guarantees that we'll get only hotkeys and space here *)
	PROCEDURE (c: RadioButton) KeyDown (ch_: CHAR; modifiers_: SET);
		VAR state: BOOLEAN;
	BEGIN
		ASSERT(~c.disabled, 100);
		c.Get(c, state);
		IF ~state THEN c.Set(c, TRUE) END
	END KeyDown;

	PROCEDURE (c: RadioButton) Mark (on, focus: BOOLEAN);
		VAR st: Storage;
	BEGIN
		st := GetStorage(c.view);
		IF (st = NIL) & (c.view # NIL) THEN st := c.Add() END;
		WITH st : RadioButtonStorage DO
			IF focus THEN
				IF on THEN IF ~ st.hasFocus THEN st.hasFocus := TRUE END
				ELSE IF st.hasFocus THEN st.hasFocus := FALSE END END
			END
		END;
		c.Update^
	END Mark;

	PROCEDURE (c: RadioButton) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: RadioButton) Update;
	BEGIN
		c.Update^
	END Update;


	(** **************** Field **************** **)

	PROCEDURE (c: Field) UpdateEd, NEW;
		VAR s: Dialog.String; ps: EditorCommonText;
	BEGIN
		c.ed.SetupCtl(c);
		(* INC(c.ed.changeCount);  c.ed.changeCount := 0; *)
		c.ed.maxLen := c.maxLen;
		IF c.maxLen > 255 THEN
			ASSERT(c.maxLen < 1024*1024*32);
			NEW(ps, c.maxLen + 1);
			c.Get(c, ps^);
			IF c.ed.SetText(ps) THEN INC(c.ed.changeCount) END
		ELSE
			c.Get(c, s);
			IF c.ed.SetText(s) THEN INC(c.ed.changeCount) END
		END
	END UpdateEd;

	PROCEDURE (c: Field) Add(): Storage, NEW;
		VAR nst: FieldStorage;
	BEGIN
		NEW(nst);
		nst.view := c.view;
		AddStorage(nst);
		RETURN nst
	END Add;

	PROCEDURE (c: Field) Restore (l_, t_, r_, b_: INTEGER);
		VAR asc, dsc, sw, w, h: INTEGER; st: Storage;
	BEGIN
		st := GetStorage(c.view);
		IF (st = NIL) & (c.view # NIL) THEN
			st := c.Add()
		END;
		WITH st : FieldStorage DO
			c.ed.st := st;
			c.view.context.GetSize(w, h);
			IF c.disabled THEN
				c.DrawRect(0, 0, w, h, Ports.fill, param.backEditorDisabled);
				c.DrawRect(0, 0, w, h, c.dot, param.frameDisabled);
			ELSE
				IF c.ed.ctl = NIL THEN c.UpdateEd END; 
				
				c.ed.Setup(c, c.dot, c.dot, w - 2 * c.dot, h - 2 * c.dot);
				IF ~c.readOnly & c.mark THEN
					c.ed.outMargin.SetUni(1)
				ELSE
					c.ed.outMargin.SetUni(2)
				END;
				c.ed.ctl.font.GetBounds(asc, dsc, sw);
				IF c.multiLine THEN
					c.ed.pad := Ports.mm
				ELSE
					c.ed.pad := (h - (asc + dsc)) DIV 3
				END;
				IF c.ed.pad < 2 * Ports.point THEN c.ed.pad := 2 * Ports.point END;
				c.ed.maxLen := c.maxLen;
				c.ed.multiLine := c.multiLine;
				c.ed.password := c.password & ~revealPasswords;
				c.ed.left := c.left;
				c.ed.right := c.right;
				c.ed.Restore;
				IF c.readOnly THEN
					c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
				ELSE
					IF c.mark THEN
						c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
					ELSE
						c.DrawRect(0, 0, w, h, c.dot, param.frame)
					END
				END
			END
		END
	END Restore;

	PROCEDURE (c: Field) GetCursor (x, y_: INTEGER; modifiers_: SET; VAR cursor: INTEGER);
		VAR w, h: INTEGER;
	BEGIN
		c.view.context.GetSize(w, h);
		IF (c.ed.vsb # NIL) & (x > w - c.ed.vsb.Width())THEN
			cursor := Ports.arrowCursor
		ELSE
			cursor := Ports.textCursor
		END
	END GetCursor;

	PROCEDURE (c: Field) Edit (op: INTEGER; VAR v: Views.View; VAR w, h: INTEGER;
													VAR singleton, clipboard: BOOLEAN);
		VAR oldChangeCount: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF (~c.disabled & ~c.readOnly) OR (c.readOnly & (op = Controllers.copy)) THEN
				oldChangeCount := c.ed.changeCount;
				c.ed.Edit(op, v, clipboard);
				IF c.ed.changeCount # oldChangeCount THEN c.Set(c, c.ed.text) END;
				c.Update^
			END
		END
	END Edit;

	PROCEDURE (c: Field) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN);
		VAR oldChangeCount: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			oldChangeCount := c.ed.changeCount;
			c.ed.WheelMove(x, y, op, nofLines, done);
			IF c.ed.changeCount # oldChangeCount THEN c.Set(c, c.ed.text) END;
			IF done OR (c.ed.changeCount # oldChangeCount) THEN
				c.Update^
			END
		END
	END WheelMove;

	PROCEDURE (c: Field) MouseDown (x, y: INTEGER; buttons: SET);
		VAR oldChangeCount: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ~c.disabled THEN
				c.lastAction := Services.Ticks();
				IF Controllers.popup IN buttons THEN
					focusControl := c;
					(* Dialog.Call(" show popup menu TODO ", 'error', res); *)
				ELSE
					oldChangeCount := c.ed.changeCount;
					c.ed.MouseDown(x, y, buttons);
					IF c.ed.changeCount # oldChangeCount THEN c.Set(c, c.ed.text) END;
					c.Update^
				END
			END
		END
	END MouseDown;

	PROCEDURE (c: Field) KeyDown (ch: CHAR; modifiers: SET);
		VAR oldChangeCount: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ch = ENTER THEN
				c.lastAction := 0
			ELSE
				c.lastAction := Services.Ticks()
			END;
			IF ~c.disabled & ~c.readOnly THEN
				oldChangeCount := c.ed.changeCount;
				c.ed.KeyDown(ch, modifiers);
				IF c.ed.changeCount # oldChangeCount THEN c.Set(c, c.ed.text) END;
				c.ed.RetainCaret;
				c.Update^
			END
		END
	END KeyDown;

	PROCEDURE  (c: Field) Select (from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ~c.disabled  & ~c.readOnly THEN
				c.ed.Select(from, to);
				c.Update^
			END
		END
	END Select;

	PROCEDURE  (c: Field) GetSelection (OUT from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN c.ed.GetSelection(from, to)
		ELSE from := 0; to := MAX(INTEGER)
		END
	END GetSelection;

	PROCEDURE  (c: Field) Idle;
	BEGIN
		c.ed.BlinkCaret
	END Idle;

	PROCEDURE  (c: Field) Length (): INTEGER;
	BEGIN
		RETURN c.ed.textLen
	END Length;

	PROCEDURE (c: Field) Mark (on, focus_: BOOLEAN);
	BEGIN
		IF ~ on THEN
			c.lastAction := 0
		END;
		c.Update^
	END Mark;

	PROCEDURE (c: Field) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;

	(* called when the text is updated by the external actor *)
	PROCEDURE (c: Field) Update;
	BEGIN
		IF Services.Ticks() - c.lastAction > Services.resolution * 3 THEN
			c.UpdateEd;
			IF c.ed.changeCount # 0 THEN
				c.ed.RetainCaret;
				c.Update^
			END
		END
	END Update;


	(** **************** ListBox **************** **)


	PROCEDURE NextItem* (l: StdCFrames.ListBox; pos: INTEGER): INTEGER;
	BEGIN
		IF l = NIL THEN
			RETURN -1
		END;
		WITH l: ListBox DO
			IF l.lst.unmap # NIL THEN
				pos := l.lst.unmap[pos]
			ELSE
				pos := pos
			END;
			IF pos < l.lst.GetLength() - 1 THEN
				INC(pos);
				RETURN l.lst.Idx(pos)
			ELSE
				RETURN -1
			END
		ELSE
			RETURN -1
		END
	END NextItem;

	PROCEDURE PrevItem* (l: StdCFrames.ListBox; pos: INTEGER): INTEGER;
	BEGIN
		IF l = NIL THEN
			RETURN -1
		END;
		WITH l: ListBox DO
			IF l.lst.unmap # NIL THEN
				pos := l.lst.unmap[pos]
			ELSE
				pos := pos
			END;
			IF pos > 0 THEN
				DEC(pos);
				RETURN l.lst.Idx(pos)
			ELSE
				RETURN -1
			END
		ELSE
			RETURN -1
		END
	END PrevItem;

	PROCEDURE (c: ListBox) UpdateLCtl, NEW;
	BEGIN
		c.lst.SetupCtl(c);
		IF c.lst.UpdateFromCtl() THEN c.fixCursor := TRUE END
	END UpdateLCtl;

	PROCEDURE (c: ListBox) NeedPopup (): BOOLEAN, NEW;
		VAR w, h: INTEGER;
	BEGIN
		c.view.context.GetSize(w, h);
		(* c.list size less than height by margins defined during setup *)
		RETURN (w > 6 * Ports.point) & (h > 6 *  Ports.point)
			& (h - c.lst.margins * 2 * c.dot < c.lst.GetItemHeight() * 2)
	END NeedPopup;

	PROCEDURE (c: ListBox) Add(): Storage, NEW;
		VAR nst: ListStorage;
	BEGIN
		NEW(nst);
		nst.view := c.view;
		nst.yoffs := 0;
		AddStorage(nst);
		RETURN nst
	END Add;

	PROCEDURE (c: ListBox) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, x, y: INTEGER; st: Storage;
	BEGIN
		IF c.view # NIL THEN
			st := GetStorage(c.view);
			IF (st = NIL) & (c.view # NIL) THEN
				st := c.Add()
			END;
			WITH st : ListStorage DO
				c.lst.st := st;
				IF c.lst.ctl = NIL THEN c.UpdateLCtl END;
				c.view.context.GetSize(w, h);
				
				c.lst.outMargin.SetUni(0);
				
				(* draw arrow *)
				IF c.NeedPopup() THEN
					c.lst.SetupList(c, c.lst.margins * c.dot, c.lst.margins * c.dot, w - 3 * c.dot - h, h - c.lst.margins * c.dot);
					c.DrawRect(w - h, 0, w, h, Ports.fill, param.backScrollbar);
					IF h > 3 * Ports.point THEN
						y := h DIV 2 + param.arrowsSize DIV 2
					ELSE
						y := h DIV 2
					END;
					x := w - h DIV 2 + param.arrowsSize DIV 2 - 2 * Ports.point;
					DrawArrowDown(c, x, y, param.arrowsSize)
				ELSE
					c.lst.SetupList(c, c.lst.margins * c.dot, c.lst.margins * c.dot, w - c.lst.margins * 2 * c.dot, h - c.lst.margins * 2 * c.dot)
				END;
				
				IF c.fixCursor THEN
					c.fixCursor := FALSE;
					c.lst.MakeCursorVisibleCentered
				END;
				
				c.lst.Restore;  (* will set .needPopup variable *)
				
				IF c.readOnly THEN
					c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
				ELSE
					IF c.mark THEN
						c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
					ELSE
						c.DrawRect(0, 0, w, h, c.dot, param.frame)
					END
				END
			END
		END
	END Restore;

	PROCEDURE (c: ListBox) GetCursor (x, y: INTEGER; modifiers: SET; VAR cursor: INTEGER);
	BEGIN
		c.lst.GetCursor(x, y, modifiers, cursor)
	END GetCursor;

	PROCEDURE (c: ListBox) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: ListBox) FixModelCursorPos, NEW;
		VAR idx: INTEGER;
	BEGIN
		IF c.view # NIL THEN
			c.Get(c, idx);
			IF c.lst.GetLength() # 0 THEN
				IF idx # c.lst.pos THEN c.Set(c, c.lst.pos) END
			ELSIF idx # -1 THEN c.Set(c, -1)
			END
		END
	END FixModelCursorPos;

	PROCEDURE (c: ListBox) PopupClosed (popup_: ANYPTR);
	BEGIN
		c.closedTime := Services.Ticks();
		IF c.popupOverlay # NIL THEN
			Services.RemoveAction(c.popupOverlay.monitorAction);
			c.popupOverlay := NIL;
		END;
		c.FixModelCursorPos;
		c.fixCursor := TRUE;
		c.Update
	END PopupClosed;

	PROCEDURE (c: ListBox) ShowPopup, NEW;
		VAR w, h: INTEGER;
	BEGIN
		IF (c.lst.ctl # NIL) & ~c.readOnly & ~c.disabled & (c.rider # NIL) & c.NeedPopup() THEN
			IF (c.popupOverlay = NIL) & (Services.Ticks() - c.closedTime > 100) THEN
				c.view.context.GetSize(w, h);
				c.popupOverlay := c.lst.ShowPopup(h - c.dot)
			END
		END
	END ShowPopup;

	PROCEDURE (c: ListBox) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN);
	BEGIN
		IF c.lst.ctl # NIL THEN
			c.lst.WheelMove(x, y, op, nofLines, done);
			IF done THEN
				c.FixModelCursorPos;
				c.Update^
			END
		END
	END WheelMove;

	PROCEDURE (c: ListBox) MouseDown (x, y: INTEGER; buttons: SET);
	BEGIN
		IF (c.lst.ctl # NIL) & (c.view # NIL) THEN
			IF ~c.disabled THEN
				IF c.NeedPopup() THEN
					c.ShowPopup
				ELSE
					c.lst.MouseDown(x, y, buttons);
					c.FixModelCursorPos;
					c.Update^
				END
			END
		END
	END MouseDown;

	PROCEDURE (c: ListBox) KeyDown (ch: CHAR; modifiers: SET);
	BEGIN
		IF (c.lst.ctl # NIL) & (c.view # NIL) THEN
			IF (ch = PR) & c.NeedPopup() THEN
				c.ShowPopup
			ELSE
				c.lst.KeyDown(ch, modifiers);
				c.FixModelCursorPos;
				c.Update^
			END
		END
	END KeyDown;

	PROCEDURE (c: ListBox) DblClickOk (x, y: INTEGER): BOOLEAN;
		VAR w, h: INTEGER; res: BOOLEAN;
	BEGIN
		res := TRUE;
		c.view.context.GetSize(w, h);
		IF (c.lst.hsb # NIL) & (y > h - c.lst.hsb.Width()) THEN
			res := FALSE
		END;
		IF (c.lst.vsb # NIL) & (x > w - c.lst.vsb.Width()) THEN
			res := FALSE
		END;
		RETURN res
	END DblClickOk;

	PROCEDURE (c: ListBox) UpdateList;
	BEGIN
		IF c.view # NIL THEN
			c.UpdateLCtl;
			c.Update^
		END
	END UpdateList;

	PROCEDURE (c: ListBox) Update;
		VAR cidx: INTEGER;
	BEGIN
		IF c.view # NIL THEN
			IF c.lst.ctl = NIL THEN
				c.UpdateLCtl
			ELSE
				c.Get(c, cidx);
				(* -1 here to keep hidden selection *)
				cidx := MAX(-1, MIN(cidx, c.lst.GetLength() - 1));
				IF cidx # c.lst.pos THEN
					c.lst.pos := cidx;
					c.fixCursor := TRUE
				END
			END;
			c.Update^
		END
	END Update;


	(** **************** ComboBox **************** **)

	(* with frame *)
	PROCEDURE (c: ComboBox) GetEditorHeight (): INTEGER, NEW;
		VAR w, h, asc, dsc, sw: INTEGER;
	BEGIN
		c.font.GetBounds(asc, dsc, sw);
		sw := asc + dsc + 6 * c.dot;
		IF (c.view # NIL) & (c.view.context # NIL) THEN
			c.view.context.GetSize(w, h);
			sw := MIN(sw, h)
		END;
		RETURN sw
	END GetEditorHeight;

	(* k8: if the control is big enough, show popup under the edit line *)
	(* k8: this returns 0, or the popup size *)
	PROCEDURE (c: ComboBox) GetVisPopupHeight (): INTEGER, NEW;
		VAR w, h, asc, dsc, sw, res: INTEGER;
	BEGIN
		res := 0;
		c.view.context.GetSize(w, h);
		IF w > 12 * c.dot THEN
			res := h - c.GetEditorHeight();
			c.font.GetBounds(asc, dsc, sw);
			IF res < asc + dsc + 6 * c.dot THEN res := 0 END
		END;
		RETURN res
	END GetVisPopupHeight;

	PROCEDURE (c: ComboBox) SetFromList, NEW;
		VAR s: Dialog.String; ns: EditorCommonText;
	BEGIN
		IF (c.view # NIL) & (c.lst.ctl # NIL) & (c.lst.GetLength() # 0) & (c.lst.pos >= 0) THEN
			c.Get(c, s);
			ns := c.lst.items[c.lst.pos].text;
			IF s$ # ns$ THEN
				c.Set(c, ns^);
				c.view(Controls.Control).item.CallWith(Update, c.lst^) (* second argument doesn't matter here *)
			END
		END
	END SetFromList;

	PROCEDURE (c: ComboBox) SetFromEd, NEW;
		VAR s: Dialog.String; ns: EditorCommonText;
	BEGIN
		IF (c.view # NIL) & (c.ed.ctl # NIL) & (c.lst.GetLength() # 0) THEN
			c.Get(c, s);
			ns := c.ed.text;
			IF s$ # ns$ THEN
				c.Set(c, ns^);
				c.view(Controls.Control).item.CallWith(Update, c.lst^) (* second argument doesn't matter here *)
			END
		END
	END SetFromEd;

	PROCEDURE (c: ComboBox) SetupCtl, NEW;
		VAR w, h, vh: INTEGER;
	BEGIN
		IF c.view # NIL THEN
			vh := c.GetVisPopupHeight();
			c.view.context.GetSize(w, h);
			c.ed.pad := 2 * Ports.point;
			IF vh = 0 THEN
				c.ed.Setup(c, c.dot, c.dot, w - c.GetEditorHeight(), c.GetEditorHeight() - 3 * c.dot)
			ELSE
				c.ed.Setup(c, c.dot, c.dot, w, c.GetEditorHeight() - 3 * c.dot)
			END;
			c.ed.outMargin.SetUni(1);
			IF vh > 0 THEN
				c.lst.SetupList(c, 2 * c.dot, c.GetEditorHeight() + 3 * c.dot, w - 4 * c.dot, vh - 4 * c.dot);
				c.lst.outMargin.SetUni(0)
			ELSE
				c.lst.SetupList(c, 0, 0, 0, 0);
				c.lst.outMargin.SetUni(0)
			END
		ELSE
			c.ed.Setup(c, 0, 0, 0, 0)
		END
	END SetupCtl;

	PROCEDURE (c: ComboBox) GetCursor (x, y: INTEGER; modifiers_: SET; VAR cursor: INTEGER);
	BEGIN
		IF (x >= c.ed.x0) & (y >= c.ed.y0) & (x < c.ed.x0+c.ed.w) & (y < c.ed.y0+c.ed.h) THEN
			cursor := Ports.textCursor
		ELSE
			cursor := Ports.arrowCursor
		END
	END GetCursor;

	PROCEDURE (c: ComboBox) ShowPopup, NEW;
	BEGIN
		IF (c.ed.ctl # NIL) & ~c.readOnly & ~c.disabled & ~c.lst.IsValidList() THEN
			IF (c.popupOverlay = NIL) & (Services.Ticks() - c.closedTime > 100) THEN
				c.popupOverlay := c.lst.ShowPopup(c.GetEditorHeight() - c.dot)
			END
		END
	END ShowPopup;
	
	PROCEDURE (c: ComboBox) Add(): Storage, NEW;
		VAR nst: ListStorage; 
	BEGIN
		NEW(nst);
		nst.view := c.view;
		AddStorage(nst);
		RETURN nst
	END Add;
	
	PROCEDURE (c: ComboBox) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, x, y: INTEGER; st: Storage;
	BEGIN
		IF c.view # NIL THEN
			st := GetStorage(c.view);
			IF st = NIL THEN
				st := c.Add()
			END;
			
			IF c.ed.ctl = NIL THEN c.UpdateLCtl END;

			c.SetupCtl;
			c.view.context.GetSize(w, h);
			
			(* draw editor *)
			c.ed.Restore;

			h := c.GetEditorHeight();
			IF ~c.lst.IsValidList() THEN
				(* draw arrow *)
				c.DrawRect(w - h, 0, w, h, Ports.fill, param.backScrollbar);
				IF h > 3 * Ports.point THEN
					y := h DIV 2 + param.arrowsSize DIV 2
				ELSE
					y := h DIV 2
				END;
				x := w - h DIV 2 + param.arrowsSize DIV 2 - 2 * Ports.point;
				DrawArrowDown(c, x, y, param.arrowsSize);
				
				IF st(ListStorage).showOnRestore THEN
					c.ShowPopup;
					st(ListStorage).showOnRestore := FALSE
				END
			END;
			
			IF c.readOnly THEN
				c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
			ELSE
				IF c.mark THEN
					c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
				ELSE
					c.DrawRect(0, 0, w, h, c.dot, param.frame)
				END
			END;
			
			(* draw list *)
			IF c.lst.IsValidList() THEN
				IF c.fixCursor THEN c.fixCursor := FALSE; c.lst.MakeCursorVisibleCentered END;
				c.lst.Restore;
				
				h := c.GetEditorHeight() + c.GetVisPopupHeight();
				IF c.readOnly THEN
					c.DrawRect(0, c.GetEditorHeight(), w, h, c.dot, param.frameReadOnly)
				ELSE
					IF c.mark THEN
						c.DrawRect(0, c.GetEditorHeight(), w, h, c.dot * 2, param.frameFocus)
					ELSE
						c.DrawRect(0, c.GetEditorHeight(), w, h, c.dot, param.frame)
					END
				END
			END
		END
	END Restore;

	PROCEDURE (c: ComboBox) PopupClosed (popup_: ANYPTR);
	BEGIN
		c.closedTime := Services.Ticks();
		IF c.popupOverlay # NIL THEN
			Services.RemoveAction(c.popupOverlay.monitorAction);
			c.popupOverlay := NIL;
		END;
		c.SetFromList;
		c.Update
	END PopupClosed;

	PROCEDURE (c: ComboBox) GoOneUpDown (dir: INTEGER), NEW;
		VAR len, idx, old: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			len := c.lst.GetLength();
			IF len > 0 THEN
				idx := GetCursorIndex(c);
				IF idx < 0 THEN idx := c.lst.pos END;
				old := idx;
				IF dir > 0 THEN INC(idx) ELSE DEC(idx) END;
				(* - 1 here to keep hidden selecton *)
				idx := MAX(-1, MIN(idx, len - 1));
				IF old # idx THEN
					c.lst.pos := idx;
					c.SetFromList;
					c.Update^
				END
			END
		END
	END GoOneUpDown;

	PROCEDURE (c: ComboBox) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN);
	BEGIN
		(*k8:TODO: `incPage` and `decPage`*)
		IF (c.ed.ctl # NIL) & ~c.readOnly & ~c.disabled & (c.rider # NIL) THEN
			IF (op = Controllers.incLine) OR (op = Controllers.decLine) THEN
				done := TRUE; (* eat it! *)
				IF c.lst.IsValidList() & (y > c.GetEditorHeight()) THEN
					c.lst.WheelMove(x, y, op, nofLines, done);
					c.Update^
				ELSE
					IF op = Controllers.decLine THEN nofLines := -nofLines END;
					c.GoOneUpDown(nofLines)
				END
			END
		END
	END WheelMove;

	PROCEDURE (c: ComboBox) MouseDown (x, y: INTEGER; buttons: SET);
		VAR w, h: INTEGER;
	BEGIN
		IF (c.ed.ctl # NIL) & ~c.readOnly & ~c.disabled & (c.rider # NIL) THEN
			IF c.lst.IsValidList() & (y > c.GetEditorHeight()) THEN
				c.lst.MouseDown(x, y +c.GetEditorHeight() , buttons);
				c.SetFromList;
				c.Update^
			ELSE
				c.view.context.GetSize(w, h);
				IF x >= w - h THEN
					c.ShowPopup
				ELSE
					c.ed.MouseDown(x, y, buttons);
					c.Update^
				 END
			END
		END
	END MouseDown;

	PROCEDURE (c: ComboBox) Edit (op: INTEGER; VAR v: Views.View; VAR w, h: INTEGER;
																VAR singleton, clipboard: BOOLEAN);
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ~c.disabled & ~c.readOnly THEN
				c.ed.changeCount := 0;
				c.ed.Edit(op, v, clipboard);
				IF c.ed.changeCount # 0 THEN c.SetFromEd END;
				c.Update^
			END
		END
	END Edit;

	PROCEDURE (c: ComboBox) KeyDown (ch: CHAR; modifiers: SET);
	BEGIN
		ASSERT(~c.disabled, 100);
		IF c.ed.ctl # NIL THEN
			CASE ch OF
			| AU: c.GoOneUpDown(-1)
			| AD: c.GoOneUpDown(1)
			| PR: (* ctrl+page down *)
				c.ShowPopup
			ELSE
				c.ed.changeCount := 0;
				c.ed.KeyDown(ch, modifiers);
				IF c.ed.changeCount # 0 THEN c.SetFromEd END;
				c.ed.RetainCaret;
				c.Update^
			END
		END
	END KeyDown;

	PROCEDURE (c: ComboBox) Close;
	BEGIN
		IF c.popupOverlay # NIL THEN c.popupOverlay.ClosePopup; c.popupOverlay := NIL END
	END Close;

	PROCEDURE (c: ComboBox) Select (from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.ed.Select(from, to);
			c.Update^
		END
	END Select;

	PROCEDURE (c: ComboBox) GetSelection (OUT from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN c.ed.GetSelection(from, to)
		ELSE from := 0; to := MAX(INTEGER)
		END
	END GetSelection;

	PROCEDURE (c: ComboBox) Idle;
	BEGIN
		c.ed.BlinkCaret
	END Idle;

	PROCEDURE  (c: ComboBox) Length (): INTEGER;
	BEGIN
		RETURN c.ed.textLen
	END Length;

	PROCEDURE (c: ComboBox) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: ComboBox) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;

	PROCEDURE (c: ComboBox) UpdateList;
	BEGIN
		IF c.view # NIL THEN
			c.UpdateLCtl;
			c.Update^
		END
	END UpdateList;

	PROCEDURE (c: ComboBox) Update;
		VAR cidx: INTEGER;
	BEGIN
		IF c.view # NIL THEN
			IF c.ed.ctl = NIL THEN
				c.UpdateLCtl
			ELSE
				c.UpdateEd;
				c.ed.RetainCaret;
				cidx := GetCursorIndex(c);
				(* - 1 here to keep hidden selection *)
				cidx := MAX(-1, MIN(cidx, c.lst.GetLength() - 1));
				IF cidx # c.lst.pos THEN
					c.lst.pos := cidx;
					c.fixCursor := TRUE
				END
			END;
			c.Update^
		END
	END Update;


	(** **************** SelectionBox **************** **)


	PROCEDURE (c: SelectionBox) UpdateLCtl, NEW;
	BEGIN
		c.lst.SetupCtl(c);
		IF c.lst.UpdateFromCtl() THEN c.fixCursor := TRUE END
	END UpdateLCtl;

	PROCEDURE (c: SelectionBox) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h: INTEGER;
	BEGIN
		IF c.view # NIL THEN

			IF c.lst.ctl = NIL THEN c.UpdateLCtl END;

			c.view.context.GetSize(w, h);
			c.lst.SetupList(c, c.dot, c.dot, w - 2 * c.dot, h - 2 * c.dot);
			c.lst.outMargin.SetUni(0);

			IF c.fixCursor THEN c.fixCursor := FALSE; c.lst.MakeCursorVisibleCentered END;
			c.lst.Restore;
			
			IF c.readOnly THEN
				c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
			ELSE
				IF c.mark THEN
					c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
				ELSE
					c.DrawRect(0, 0, w, h, c.dot, param.frame)
				END
			END
		END
	END Restore;

	PROCEDURE (c: SelectionBox) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: SelectionBox) WheelMove (x, y, op, nofLines: INTEGER; VAR done: BOOLEAN);
	BEGIN
		IF c.lst.ctl # NIL THEN
			c.lst.WheelMove(x, y, op, nofLines, done);
			IF done THEN c.Update^ END
		END
	END WheelMove;

	PROCEDURE (c: SelectionBox) MouseDown (x, y: INTEGER; buttons: SET);
	BEGIN
		IF c.lst.ctl # NIL THEN
			IF ~c.disabled THEN
				c.lst.MouseDown(x, y, buttons);
				c.Update^
			END
		END
	END MouseDown;

	PROCEDURE (c: SelectionBox) KeyDown (ch: CHAR; modifiers: SET);
	BEGIN
		IF c.lst.ctl # NIL THEN
			c.lst.KeyDown(ch, modifiers);
			c.Update^
		END
	END KeyDown;

	PROCEDURE (c: SelectionBox) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;

	PROCEDURE  (c: SelectionBox) GetSelection (OUT from, to: INTEGER);
	BEGIN
		from := 0; to := MAX(INTEGER)
	END GetSelection;

	PROCEDURE  (c: SelectionBox) Select (from_, to_: INTEGER);
	BEGIN
	END Select;

	PROCEDURE (c: SelectionBox) UpdateList;
	BEGIN
		IF c.view # NIL THEN
			c.UpdateLCtl;
			c.Update^
		END
	END UpdateList;

	PROCEDURE (c: SelectionBox) Update;
	BEGIN
		IF c.view # NIL THEN
			c.UpdateLCtl;
			c.Update^
		END
	END Update;


	(** **************** UpDownField **************** **)
	
	PROCEDURE (c: UpDownField) UpdateEd, NEW;
		VAR val: INTEGER; s: Dialog.String;
	BEGIN
		c.Get(c, val); (* differs from field, that here is integer *)
		IF c.ed.ctl = NIL THEN c.lastval := val + 1 END;
		c.ed.SetupCtl(c);
		c.ed.changeCount := 0;
		c.ed.maxLen := 255;
		IF val # c.lastval THEN
			c.lastval := val;
			Strings.IntToString(val, s);
			IF c.ed.SetText(s) THEN INC(c.ed.changeCount) END
		END
	END UpdateEd;

	PROCEDURE (c: UpDownField) SetFromEd (): BOOLEAN, NEW;
		VAR val, res: INTEGER; modified: BOOLEAN;
	BEGIN
		modified := FALSE;
		IF c.ed.ctl # NIL THEN
			IF c.view # NIL THEN
				Strings.StringToInt(c.ed.text^, val, res);
				IF (res = 0) & (val # c.lastval) THEN
					c.lastval := val;
					c.Set(c, val);
					modified := TRUE
				 END
			END
		END;
		RETURN modified
	END SetFromEd;

	PROCEDURE (c: UpDownField) HasArrows (): BOOLEAN, NEW;
		VAR w, h: INTEGER; res: BOOLEAN;
	BEGIN
		IF c.view # NIL THEN
			c.view.context.GetSize(w, h);
			res := w > 12 * Ports.point
		ELSE res := FALSE
		END;
		RETURN res
	END HasArrows;

	PROCEDURE (c: UpDownField) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, x0, y0, ah, asc, dsc, sw: INTEGER;
	BEGIN
		IF c.ed.ctl = NIL THEN c.UpdateEd END;
		c.view.context.GetSize(w, h);
		IF c.ed.pad = 0 THEN
			c.ed.ctl.font.GetBounds(asc, dsc, sw);
			c.ed.pad := (h - (asc + dsc)) DIV 3;
			IF c.ed.pad < 2 * Ports.point THEN c.ed.pad := 2 * Ports.point END;
		END;
		
		IF c.HasArrows() THEN
			c.ed.Setup(c, c.dot, c.dot, w - 5 * c.dot - 11 * Ports.point, h - 2 * c.dot);
			c.DrawRect(w - 5 * c.dot - 11 * Ports.point, 0, w, h, Ports.fill, param.backScrollbar);
			x0 := w - 7 * Ports.point;
			ah := h DIV 2;
			IF ah >= 8 * Ports.point THEN
				y0 := ah - 5 * Ports.point;
				DrawArrowUp(c, x0, y0, 3 * Ports.point);
				y0 := ah + 5 * Ports.point;
				DrawArrowDown(c, x0, y0, 3 * Ports.point)
			END
		ELSE
			c.ed.Setup(c, c.dot, c.dot, w - 5 * c.dot, h - 2 * c.dot);
		END;
		c.ed.outMargin.SetUni(1);
		c.ed.maxLen := 255;
		c.ed.Restore;
		
		IF c.readOnly THEN
			c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
		ELSE
			IF c.mark THEN
				c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
			ELSE
				c.DrawRect(0, 0, w, h, c.dot, param.frame)
			END
		END
	END Restore;

	PROCEDURE (c: UpDownField) GetCursor (x, y: INTEGER; modifiers_: SET; VAR cursor: INTEGER);
	BEGIN
		IF (x >= c.ed.x0) & (y >= c.ed.y0) & (x < c.ed.x0 + c.ed.w) & (y < c.ed.y0 + c.ed.h) THEN
			cursor := Ports.textCursor
		ELSE
			cursor := Ports.arrowCursor
		END
	END GetCursor;

	PROCEDURE (c: UpDownField) IncDec (inc: BOOLEAN), NEW;
		VAR val: INTEGER;
	BEGIN
		val := c.lastval;
		IF inc THEN
			IF val < c.max THEN
				IF LONG(c.max) - val <= c.inc THEN val := c.max ELSE INC(val, c.inc) END
			END
		ELSE
			IF val > c.min THEN
				IF LONG(val) - c.min <= c.inc THEN val := c.min ELSE DEC(val, c.inc) END
			END
		END;
		IF val # c.lastval THEN
			c.Set(c, val);
			c.UpdateEd;
			c.Update^
		END
	END IncDec;

	PROCEDURE (c: UpDownField) WheelMove (x, y, op, nofLines_: INTEGER; VAR done: BOOLEAN);
		VAR w, h: INTEGER;
	BEGIN
		IF c.view # NIL THEN
			c.view.context.GetSize(w, h);
			IF (x >= 0) & (y >= 0) & (x < w) & (y < h) THEN
				done := TRUE;
				c.IncDec(op = Controllers.decLine)
			END
		END
	END WheelMove;

	PROCEDURE (c: UpDownField) MouseDown (x, y: INTEGER; buttons: SET);
		VAR w, h: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ~c.disabled & ~c.readOnly & (c.rider # NIL) THEN
				c.view.context.GetSize(w, h);
				IF c.HasArrows() & (x > w - 3 * c.dot - 11 * Ports.point) THEN
					c.IncDec(y < h DIV 2)
				ELSE
					c.ed.changeCount := 0;
					c.ed.MouseDown(x, y, buttons);
					IF c.ed.changeCount # 0 THEN
						IF c.SetFromEd() THEN END
					END;
					c.Update^
				END
			END
		END
	END MouseDown;

	PROCEDURE (c: UpDownField) KeyDown (ch: CHAR; modifiers: SET);
		VAR int, res: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.ed.changeCount := 0;
			CASE ch OF
			| AU:
				Strings.StringToInt(c.ed.text, int, res);
				Strings.IntToString(int+1, c.ed.text);
				c.ed.textLen := LEN(c.ed.text$);
				c.ed.pos := c.ed.textLen;
				INC(c.ed.changeCount)
			| AD:
				Strings.StringToInt(c.ed.text, int, res);
				Strings.IntToString(int-1, c.ed.text);
				c.ed.textLen := LEN(c.ed.text$);
				c.ed.pos := c.ed.textLen;
				INC(c.ed.changeCount)
			ELSE
				IF (ch > 20X) THEN
					IF ((ch >= '0') & (ch <= '9')) OR (ch = '-') OR (ch = '+') OR (ch = 'E') OR (ch = '.') THEN
						c.ed.KeyDown(ch, modifiers)
					END
				ELSIF (ch = AL) OR (ch = AR) OR (ch = BACKSPACE) OR (ch = DELETE) OR (ch = CHR(64977)) OR (ch = 019X) OR (ch = CHR(64978)) THEN
					c.ed.KeyDown(ch, modifiers)
				END
			END;
			IF c.ed.changeCount # 0 THEN
				IF c.SetFromEd() THEN END
			END;
			c.ed.RetainCaret;
			c.Update^
		END
	END KeyDown;

	PROCEDURE (c: UpDownField) Edit (op: INTEGER; VAR v: Views.View; VAR w, h: INTEGER;
													VAR singleton, clipboard: BOOLEAN);
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF ~c.disabled & ~c.readOnly THEN
				c.ed.changeCount := 0;
				c.ed.Edit(op, v, clipboard);
				IF c.ed.changeCount # 0 THEN
					IF c.SetFromEd() THEN END
				END;
				c.Update^
			END
		END
	END Edit;

	PROCEDURE  (c: UpDownField) Select (from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.ed.Select(from, to);
			c.Update^
		END
	END Select;

	PROCEDURE  (c: UpDownField) GetSelection (OUT from, to: INTEGER);
	BEGIN
		IF c.ed.ctl # NIL THEN c.ed.GetSelection(from, to)
		ELSE from := 0; to := MAX(INTEGER)
		END
	END GetSelection;

	PROCEDURE  (c: UpDownField) Idle;
	BEGIN
		c.ed.BlinkCaret
	END Idle;

	PROCEDURE (c: UpDownField) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: UpDownField) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: UpDownField) Update;
	BEGIN
		IF c.view # NIL THEN
			c.UpdateEd;
			IF c.ed.changeCount # 0 THEN
				c.ed.RetainCaret
			END;
			c.Update^
		END
	END Update;


	(** **************** ColorField **************** **)

	PROCEDURE (c: ColorField) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, x, y, col, m, bw: INTEGER;
	BEGIN
		c.view.context.GetSize(w, h);
		h := h - 2 * Ports.point;
		bw := 12 * Ports.point;
		IF h < bw THEN bw := h END;
		IF c.readOnly OR c.disabled THEN
			c.DrawRect(0, 0, w, h, Ports.fill, param.backDisabled)
		ELSE
			IF c.front THEN col := param.backHover
			ELSE col := param.back
			END;
			c.Get(c, col);
			IF w > h DIV 2 * 3 THEN
				m := h DIV 6;
				c.DrawRect(m + c.dot, m, w - bw - m, h - m - c.dot, Ports.fill, col);
				IF c.front & (w > 8 * c.dot) & (h > 8 * c.dot) THEN
					c.MarkRect(
						m + c.dot * 2, m + c.dot, w - bw - m - c.dot, h - m - 2 * c.dot, 0, Ports.invert, Ports.show)
				END;
				
				(* draw arrow *)
				c.DrawRect(w - bw, 0, w, h, Ports.fill, param.backScrollbar);
				IF h > 3 * Ports.point THEN
					y := h DIV 2 + param.arrowsSize DIV 2
				ELSE
					y := h DIV 2
				END;
				x := w - bw DIV 2 + param.arrowsSize DIV 2 - 2 * Ports.point;
				DrawArrowDown(c, x, y, param.arrowsSize);
			ELSE
				c.DrawRect(0, 0, w, h, Ports.fill, col);
				IF c.front & (w > 8 * c.dot) & (h > 8 * c.dot) THEN
					c.MarkRect(4 * c.dot, 4 * c.dot, w - 4 * c.dot, h - 4 * c.dot, 0, Ports.invert, Ports.show)
				END
			END
		END;
		
		IF c.readOnly THEN
			c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
		ELSE
			IF c.mark THEN
				c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
			ELSE
				c.DrawRect(0, 0, w, h, c.dot, param.frame)
			END
		END
	END Restore;

	PROCEDURE (c: ColorField) MouseDown (x_, y_: INTEGER; buttons_: SET);
		VAR w, h: INTEGER;
	BEGIN
		IF c.popupOverlay = NIL THEN
			c.view.context.GetSize(w, h);
			IF (c.popupOverlay = NIL) & (Services.Ticks() - c.closedTime > 100) THEN
				c.popupOverlay := c.ShowPopupColor(h - 2 * Ports.point)
			END
		ELSE
			c.popupOverlay.ClosePopup
		END
	END MouseDown;

	PROCEDURE (c: ColorField) KeyDown (ch: CHAR; modifiers_: SET);
		VAR w, h: INTEGER;
	BEGIN
		CASE ch OF
		| AD:
			IF (c.popupOverlay = NIL) & (Services.Ticks() - c.closedTime > 100) THEN
				c.view.context.GetSize(w, h);
				c.popupOverlay := c.ShowPopupColor(h - 2 * Ports.point)
			END
		| AU:
			IF c.popupOverlay # NIL THEN
				c.popupOverlay.ClosePopup
			END
		ELSE
		END
	END KeyDown;

	PROCEDURE (c: ColorField) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: ColorField) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: ColorField) Update;
	BEGIN
		c.Update^
	END Update;


	(** **************** VScrollBar **************** **)

	PROCEDURE (sb: VScrollBar) Init;
	BEGIN
	END Init;

	(* draw it; called from `Restore()`; `dim` is main dimension size in pixels *)
	PROCEDURE (sb: VScrollBar) Draw (c: Views.Frame; x, y, length: INTEGER; pressed: BOOLEAN);
		VAR w, h, kpos, ksize, mbo, m: INTEGER; color: Ports.Color;
	BEGIN
		sb.length := length;
		
		mbo := sb.Width();
		w := sb.Width();
		h := length;
		
		IF y + mbo < y + h - mbo THEN
			
			c.DrawRect(x, y, x + w, y + h, Ports.fill, param.backScrollbarButtons);
			c.DrawRect(x, y + mbo, x + w, y + h - mbo, Ports.fill, param.backScrollbar);
			
			DrawArrowUp(c(StdCFrames.Frame), x + w DIV 2, y + (w DIV 2 - param.arrowsSize DIV 2), param.arrowsSize);
			
			IF pressed THEN
				color := param.knobScrollbarPressed
			ELSE
				color := param.knobScrollbar
			END;
			
			kpos := sb.CalcKnobPos(sb.pos) + mbo;
			ksize := sb.CalcKnobSize();
			m :=  param.scrollbarKnobMargin * c.dot;
			c.DrawRect(x+m, y+kpos+m, x+w-m, y+kpos+ksize-m, Ports.fill, color);
			
			DrawArrowDown(c(StdCFrames.Frame), x + w DIV 2, y + h - (w DIV 2 - param.arrowsSize DIV 2), param.arrowsSize)
		
		END
	END Draw;

	(* return TRUE if the caller need to track the mouse and call `MouseTrack()` *)
	PROCEDURE (sb: VScrollBar) SBMouseDown (x_, y: INTEGER): BOOLEAN;
		VAR mbs, kpos, mbo, ksize: INTEGER; res: BOOLEAN;
	BEGIN
		res := FALSE;
		mbs := sb.length - sb.Width() * 2;
		IF mbs > 0 THEN
			mbo := sb.Width();
			kpos := sb.CalcKnobPos(sb.pos) + mbo;
			ksize := sb.CalcKnobSize();
			IF y < mbo THEN
				(* top arrow *)
				DEC(sb.pos, sb.arrowStep);
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos))
			ELSIF (y >= mbo) & (y < kpos) THEN
				(* before the knob *) 
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos - sb.pageSize))
			ELSIF (y >= kpos) & (y < kpos + ksize) THEN
				(* on the knob *)
				sb.trackStartY := y - mbo;
				sb.trackOfsY := y - kpos;
				sb.trackSavedPos := sb.pos;
				res := TRUE
			ELSIF (y >= kpos + ksize) & (y < mbo + mbs) THEN
				(* after the knob *)
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos + sb.pageSize))
			ELSE
				(* bottom arrow *) 
				INC(sb.pos, sb.arrowStep);
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos))
			END
		END;
		RETURN res
	END SBMouseDown;

	PROCEDURE (sb: VScrollBar) SBMouseTrack (x, y: INTEGER);
		CONST area = 40 * Ports.point;
		VAR length, res, ksz: INTEGER;
	BEGIN
		IF (x < -area) OR (y < -area) OR (x > sb.Width() + area) OR (y > sb.length + area) THEN
			sb.pos := sb.trackSavedPos;
			sb.trackStartY := MIN(INTEGER)
		ELSE
			length := sb.length - sb.Width() * 2;
			
			DEC(y, sb.Width());
			
			IF (y < 0) OR (sb.min >= sb.max) THEN
				res := sb.min
			ELSIF y = sb.trackStartY THEN
				res := sb.pos
			ELSIF y >= length THEN
				res := sb.max
			ELSE
				ksz := sb.CalcKnobSize();
				IF ksz < length THEN
					DEC(y, sb.trackOfsY);
					DEC(length, ksz);
					res := MIN(sb.max, SHORT(LONG(sb.GetTotalSize()) * LONG(y) DIV LONG(length)));
					IF res < sb.min THEN res := sb.min END
				END
			END;
			sb.pos := res
		END
	END SBMouseTrack;
	
	
	
	
	(** **************** HScrollBar **************** **)

	PROCEDURE (sb: HScrollBar) Init;
	BEGIN
	END Init;

	(* draw it; called from `Restore()`; `dim` is main dimension size in pixels *)
	PROCEDURE (sb: HScrollBar) Draw (c: Views.Frame; x, y, length: INTEGER; pressed: BOOLEAN);
		VAR w, h, kpos, ksize, mbo, m: INTEGER; color: Ports.Color;
	BEGIN
		sb.length := length;
		mbo := sb.Width();
		h := sb.Width();
		w := length;
		
		c.DrawRect(x, y, x + w, y + h, Ports.fill, param.backScrollbarButtons);
		c.DrawRect(x + mbo, y, x + w - mbo, y + h, Ports.fill, param.backScrollbar);
		
		DrawArrowLeft(c(StdCFrames.Frame), x + (h DIV 2 - param.arrowsSize DIV 2), y + h DIV 2, param.arrowsSize);
		
		IF pressed THEN color := param.knobScrollbarPressed ELSE color := param.knobScrollbar END;
		kpos := sb.CalcKnobPos(sb.pos) + mbo;
		ksize := sb.CalcKnobSize();
		m :=  param.scrollbarKnobMargin * c.dot;
		c.DrawRect(x+kpos+m, y+m, x+kpos+ksize-m, y+h-m, Ports.fill, color);
		
		DrawArrowRight(c(StdCFrames.Frame), x + w - (h DIV 2 - param.arrowsSize DIV 2), y + h DIV 2, param.arrowsSize)
		
	END Draw;

	(* return TRUE if the caller need to track the mouse and call `MouseTrack()` *)
	PROCEDURE (sb: HScrollBar) SBMouseDown (x, y_: INTEGER): BOOLEAN;
		VAR mbs, kpos, mbo, ksz: INTEGER; res: BOOLEAN;
	BEGIN
		res := FALSE;
		mbs := sb.length - sb.Width() * 2;
		IF mbs > 0 THEN
			mbo := sb.Width();
			kpos := sb.CalcKnobPos(sb.pos) + mbo;
			ksz := sb.CalcKnobSize();
			IF x < mbo THEN
				(* left arrow *)
				DEC(sb.pos, sb.arrowStep);
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos))
			ELSIF (x >= mbo) & (x < kpos) THEN
				(* before the knob *)
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos - sb.pageSize))
			ELSIF (x >= kpos) & (x < kpos + ksz) THEN
				(* on the knob *)
				sb.trackStartX := x - mbo;
				sb.trackOfsX := x - kpos;
				sb.trackSavedPos := sb.pos;
				res := TRUE
			ELSIF (x >= kpos + ksz) & (x < mbo + mbs) THEN
				(* after the knob *)
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos + sb.pageSize))
			ELSE
				(* right arrow *)
				INC(sb.pos, sb.arrowStep);
				sb.pos := MAX(sb.min, MIN(sb.max, sb.pos))
			END
		END;
		RETURN res
	END SBMouseDown;

	PROCEDURE (sb: HScrollBar) SBMouseTrack (x, y: INTEGER);
		CONST area = 40 * Ports.point;
		VAR dim, res, ksz: INTEGER;
	BEGIN
		IF (x < -area) OR (y < -area) OR (x > sb.length + area) OR (y > sb.Width() + area) THEN
			sb.pos := sb.trackSavedPos;
			sb.trackStartX := MIN(INTEGER)
		ELSE
			dim :=  sb.length - sb.Width() * 2;
			DEC(x, sb.Width());
			
			IF (x < 0) OR (sb.min >= sb.max) THEN
				res := sb.min
			ELSIF x = sb.trackStartX THEN
				res := sb.pos
			ELSIF x >= dim THEN
				res := sb.max
			ELSE
				ksz := sb.CalcKnobSize();
				IF ksz < dim THEN
					DEC(x, sb.trackOfsX);
					DEC(dim, ksz);
					res := MIN(sb.max, SHORT(LONG(sb.GetTotalSize()) * LONG(x) DIV LONG(dim)));
					IF res < sb.min THEN res := sb.min END;
				END
			END;
			sb.pos := res
		END
	END SBMouseTrack;


	(** **************** ScrollBar **************** **)

	PROCEDURE (c: ScrollBar) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, size, sect, pos: INTEGER; vsb: VScrollBar; hsb: HScrollBar;
	BEGIN
		c.view.context.GetSize(w, h);
		c.Get(c, size, sect, pos);
		(*
			size = size of embedded (wrapped) view
			sect = size of section, which we see in aperture
			pos = position of aperture
		*)
		IF h > w THEN
			(* vertical *)
			IF c.vsb = NIL THEN
				NEW(vsb);
				vsb.Init;
				c.vsb := vsb
			ELSE
				vsb := c.vsb
			END;
			vsb.min := 0;
			vsb.contentSize := size;
			vsb.max := vsb.contentSize - sect;
			vsb.pos := pos;
			vsb.pageSize := sect;
			vsb.arrowStep := 5 * Ports.mm;
			vsb.Draw(c, 0, w - vsb.Width(), h, c.vsbPressed)
		ELSIF h < w THEN
			(* horisontal *)
			IF c.hsb = NIL THEN
				NEW(hsb);
				hsb.Init;
				c.hsb := hsb
			ELSE
				hsb := c.hsb
			END;
			hsb.min := 0;
			hsb.max := size - sect;
			hsb.contentSize := size;
			hsb.pos := pos;
			hsb.pageSize := sect;
			hsb.arrowStep := 5 * Ports.mm;
			hsb.Draw(c, h - hsb.Width(), 0, w, c.hsbPressed)
		END
	END Restore;

	PROCEDURE (c: ScrollBar) MouseDown (x, y: INTEGER; buttons_: SET);
		VAR w, h, size, sect, pos: INTEGER; (* doc: Views.View; *)
			mx, my: INTEGER; isDown: BOOLEAN; modifiers: SET;
			mbs, kpos, mbo, ksize, catchPos: INTEGER;
			vsb: VScrollBar; hsb: HScrollBar;
	BEGIN
		ASSERT(~c.disabled, 100);
		(*
		doc := Views.UltimateRootOf(c).view; 
		WITH doc: Documents.Document DO
			Documents.RemoveOverlay(doc);
		ELSE END;
		*)
		IF c.rider # NIL THEN
			c.view.context.GetSize(w, h);
			IF (h > w) & (c.vsb # NIL) THEN
				(* vertical *)
				vsb := c.vsb;
				mbs := vsb.length - vsb.Width() * 2;
				mbo := vsb.Width();
				vsb.length := h;
				c.Get(c, size, sect, vsb.pos);
				kpos := vsb.CalcKnobPos(vsb.pos) + mbo;
				ksize := vsb.CalcKnobSize();
				IF y < mbo THEN
					(* top arrow *)
					DEC(vsb.pos, vsb.arrowStep);
					vsb.pos := MAX(vsb.min, MIN(vsb.max, vsb.pos));
					c.Set(c, vsb.pos); c.Update
				ELSIF (y >= mbo) & (y < kpos) THEN
					(* before the knob *)
					vsb.pos := MAX(vsb.min, MIN(vsb.max, vsb.pos - vsb.pageSize));
					c.Set(c, vsb.pos); c.Update
				ELSIF (y >= kpos) & (y < kpos + ksize) THEN
					(* on the knob *)
					catchPos := y - kpos; 
					c.vsbPressed := TRUE;
					REPEAT
						c.Input(mx, my, modifiers, isDown);
						c.Get(c, size, sect, pos);
						vsb.pos := SHORT(ENTIER(size * ((my - mbo - catchPos) / (h - mbo * 2))));
						vsb.pos := MAX(vsb.min, MIN(vsb.max, vsb.pos));
						c.Set(c, vsb.pos);
						c.Update
					UNTIL ~isDown OR (c.rider = NIL);
					c.vsbPressed := FALSE;
					c.Update
				ELSIF (y >= kpos + ksize) & (y < mbo + mbs) THEN
					(* after the knob *)
					vsb.pos := MAX(vsb.min, MIN(vsb.max, vsb.pos + vsb.pageSize));
					c.Set(c, vsb.pos); c.Update
				ELSE
					(* bottom arrow *)
					INC(vsb.pos, vsb.arrowStep);
					vsb.pos := MAX(vsb.min, MIN(vsb.max, vsb.pos));
					c.Set(c, vsb.pos); c.Update
				END
			ELSIF (h < w) & (c.hsb # NIL) THEN
				(* horizontal *) 
				hsb := c.hsb;
				mbs := hsb.length -  hsb.Width() * 2;
				mbo := hsb.Width();
				hsb.length := w;
				c.Get(c, size, sect, hsb.pos);
				kpos := hsb.CalcKnobPos(hsb.pos) + mbo;
				ksize := hsb.CalcKnobSize();
				IF x < mbo THEN
					(* top arrow *)
					DEC(hsb.pos, hsb.arrowStep);
					hsb.pos := MAX(hsb.min, MIN(hsb.max, hsb.pos));
					c.Set(c, hsb.pos); c.Update
				ELSIF (x >= mbo) & (x < kpos) THEN
					(* before the knob *)
					hsb.pos := MAX(hsb.min, MIN(hsb.max, hsb.pos - hsb.pageSize));
					c.Set(c, hsb.pos); c.Update
				ELSIF (x >= kpos) & (x < kpos + ksize) THEN
					(* on the knob *)
					catchPos := x - kpos; 
					c.hsbPressed := TRUE;
					REPEAT
						c.Input(mx, my, modifiers, isDown);
						c.Get(c, size, sect, pos);
						hsb.pos := SHORT(ENTIER(size * ((mx - mbo - catchPos) / (w - mbo * 2))));
						hsb.pos := MAX(hsb.min, MIN(hsb.max, hsb.pos));
						c.Set(c, hsb.pos);
						c.Update
					UNTIL ~isDown OR (c.rider = NIL);
					c.hsbPressed := FALSE;
					c.Update;
				ELSIF (x >= kpos + ksize) & (x < mbo + mbs) THEN
					(* after the knob *)
					hsb.pos := MAX(hsb.min, MIN(hsb.max, hsb.pos + hsb.pageSize));
					c.Set(c, hsb.pos); c.Update
				ELSE
					(* bottom arrow *)
					INC(hsb.pos, hsb.arrowStep);
					hsb.pos := MAX(hsb.min, MIN(hsb.max, hsb.pos));
					c.Set(c, hsb.pos); c.Update
				END
			END
		END
	END MouseDown;

	PROCEDURE (c: ScrollBar) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN FALSE
	END DblClickOk;

	PROCEDURE (c: ScrollBar) Update;
	BEGIN
		c.Update^
	END Update;


	(** **************** DateField **************** **)

	PROCEDURE (c: DateField) GetDatePart (VAR date: Dates.Date; part: INTEGER; OUT val, min, max: INTEGER), NEW;
	(* GetDatePart picks the day, month or year part out of a given date and asigns it to the out
		parameter val, together with the min and max possible values for this part
	*)
	BEGIN
		IF part = -1 THEN part := 3 END;
		IF part = c.yearPart THEN val := date.year; min := 1; max := 9999
		ELSIF part = c.monthPart THEN val := date.month; min := 1; max := 12
		ELSE
			val := date.day; min := 1;
			IF date.month = 0 THEN
				max := 31
			ELSIF date.month = 2 THEN
				(* leap year check *)
				IF (date.year MOD 4 = 0) & ((date.year < 1583) OR (date.year MOD 100 # 0) OR (date.year MOD 400 = 0))
				THEN
					max := 29
				ELSE max := 28
				END
			ELSIF date.month IN {1, 3, 5, 7, 8, 10, 12} THEN max := 31
			ELSE max := 30
			END
		END
	END GetDatePart;

	PROCEDURE (c: DateField) SetDatePart (VAR date: Dates.Date; part: INTEGER; val: INTEGER), NEW;
	(* SetDatePart sets the day, month or year part in a given date to the value specivied
		by the parameter val.
		If the month is set, the day is adjusted to the possible range of days in this month.
		If the day is set, then the month may be changed in order to obtain a valid date
	*)
	VAR
		v, min, max: INTEGER;
	BEGIN
		IF part = -1 THEN part := 3 END;
		IF part = c.yearPart THEN date.year := val
		ELSIF part = c.monthPart THEN date.month := val
		ELSE date.day := val
		END;
		c.GetDatePart(date, c.dayPart, v, min, max);
		IF (part = c.monthPart) THEN (* adjust day if month value is set and day > max *)
			IF v > max THEN date.day := max END
		ELSIF part = c.yearPart THEN (* adjust month is day value is set and day > max *)
			IF v > max THEN INC(date.month) END
		END
	END SetDatePart;

	PROCEDURE (c: DateField) DateToString (VAR date: Dates.Date; VAR str: ARRAY OF CHAR), NEW;
		VAR val, min, max, p, i: INTEGER;
	BEGIN
		p := 1; i := 0;
		WHILE p <= 3 DO
			IF p > 1 THEN str[i] := c.dateSep; INC(i) END;
			c.GetDatePart(date, p, val, min, max);
			IF max = 9999 THEN
				str[i] := CHR(val DIV 1000 MOD 10 + ORD("0")); INC(i);
				str[i] := CHR(val DIV 100 MOD 10 + ORD("0")); INC(i)
			END;
			str[i] := CHR(val DIV 10 MOD 10 + ORD("0")); INC(i);
			str[i] := CHR(val MOD 10 + ORD("0")); INC(i);
			INC(p)
		END;
		str[i] := 0X
	END DateToString;

	PROCEDURE (c: DateField) Select, NEW;
		VAR sel, f, t: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.GetSel(c, sel);
			CASE sel OF
			| 1: f := 0; t := c.del1
			| 2: f := c.del1 + 1; t := c.del2
			ELSE f := c.del2 + 1; t := c.del2 + 5
			END;
			c.ed.Select(f, t)
		END
	END Select;

	PROCEDURE (c: DateField) UpdateEd, NEW;
		VAR date: Dates.Date; s: ARRAY 20 OF CHAR;
	BEGIN
		c.ed.SetupCtl(c);
		c.ed.changeCount := 0;
		c.ed.maxLen := 10; c.ed.password := FALSE;
		c.ed.left := TRUE; c.ed.right := FALSE;
		IF c.undef THEN
			s := ""
		ELSE
			c.Get(c, date);
			c.DateToString(date, s)
		END;
		IF c.ed.SetText(s) THEN INC(c.ed.changeCount) END;
		c.Select
	END UpdateEd;

	PROCEDURE (c: DateField) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, asc, dsc, sw: INTEGER;
	BEGIN
		IF c.ed.ctl = NIL THEN c.UpdateEd END;
		c.view.context.GetSize(w, h);
		IF c.ed.pad = 0 THEN
			c.ed.ctl.font.GetBounds(asc, dsc, sw);
			c.ed.pad := (h - (asc + dsc)) DIV 3;
			IF c.ed.pad < 2 * Ports.point THEN c.ed.pad := 2 * Ports.point END;
		END;
		c.ed.Setup(c, c.dot, c.dot, w - 2 * c.dot, h - 2 * c.dot);
		c.ed.outMargin.SetUni(1);
		c.ed.Restore;

		IF c.readOnly THEN
			c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
		ELSE
			IF c.mark THEN
				c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
			ELSE
				c.DrawRect(0, 0, w, h, c.dot, param.frame)
			END
		END
	END Restore;

	PROCEDURE (c: DateField) Edit (op: INTEGER; VAR v: Views.View; VAR w, h: INTEGER;
															VAR singleton, clipboard: BOOLEAN);
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF clipboard & (op = Controllers.copy) THEN
				c.ed.changeCount := 0;
				(* copy the whole string *)
				c.ed.Select(0, 1024);
				c.ed.Edit(op, v, clipboard);
				c.Select
			END
		END
	END Edit;

	PROCEDURE (c: DateField) WheelMove (x, y, op, nofLines_: INTEGER; VAR done: BOOLEAN);
		VAR mods: SET; isDown: BOOLEAN;
	BEGIN
		IF c.ed.ctl # NIL THEN
			done := TRUE; (* eat it! *)
			(* hack! *)
			c.Input(x, y, mods, isDown);
			IF mods * {Controllers.pick, Controllers.extend} # {} THEN INCL(mods, Controllers.modify) END;
			IF op IN {Controllers.incLine, Controllers.incPage} THEN c.KeyDown(AD, mods)
			ELSE c.KeyDown(AU, mods)
			END
		END
	END WheelMove;

	PROCEDURE (c: DateField) MouseDown (x, y: INTEGER; buttons: SET);
		VAR p: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.ed.changeCount := 0;
			c.ed.MouseDown(x, y, buttons);
			(* fix selection *)
			IF c.ed.from < c.ed.to THEN p := c.ed.from ELSE p := c.ed.pos END;
			IF p <= c.del1 THEN p := 1
			ELSIF p <= c.del2 THEN p := 2
			ELSE p := 3
			END;
			c.SetSel(c, p);
			c.Select;
			c.Update^
		END
	END MouseDown;

	PROCEDURE (c: DateField) KeyDown (ch: CHAR; modifiers: SET);
		VAR sel, s, val, v, min, max, dd: INTEGER; date: Dates.Date; setNow: BOOLEAN;
	BEGIN
		IF c.ed.ctl # NIL THEN
			setNow := FALSE;
			c.ed.changeCount := 0;
			c.GetSel(c, sel); s := sel;
			IF s < 1 THEN s := 3 END;
			c.Get(c, date);
			c.GetDatePart(date, s, val, min, max); v := val;
			CASE ch OF
			| TAB, AR, timeSep, dateSep, " ": s := s MOD 3 + 1
			| LTAB, AL: IF s = 0 THEN s := 1 END; s := (s - 2) MOD 3 + 1
			| PL, DL: s := 1
			| PR, DR: s := 3
			| DELETE, BACKSPACE: v := min
			| AU:
				IF Controllers.modify IN modifiers THEN dd := 10 ELSE dd := 1 END;
				v := (v - min - dd) MOD (max - min + 1) + min
			| AD:
				IF Controllers.modify IN modifiers THEN dd := 10 ELSE dd := 1 END;
				v := (v - min + dd) MOD (max - min + 1) + min
			ELSE
				(*
				IF (ch >= "0") & (ch <= "9") THEN
					v := v * 10 MOD 100 + ORD(ch) - ORD("0");
					IF v > max THEN v := v MOD 10 ELSIF v < min THEN v := min END
				ELSIF (ch = 'n') OR (ch = 'N') THEN
					setNow := TRUE
				END
				*)
				setNow := (ch = 'n') OR (ch = 'N');
			END;
			IF setNow THEN
				IF ~c.disabled & ~c.readOnly THEN
					Dates.GetDate(date); c.Set(c, date); c.Update
				END
			ELSIF v # val THEN
				IF ~c.disabled & ~c.readOnly THEN
					c.GetDatePart(date, s, val, min, max);
					IF (min <= v) & (v <= max) THEN
						c.SetDatePart(date, s, v); c.Set(c, date); c.Update
					END
				END
			ELSIF s # sel THEN
				c.SetSel(c, s); c.Select
			END;
			c.ed.RetainCaret;
			c.Update^
		END
	END KeyDown;

	PROCEDURE (c: DateField) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: DateField) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;

	(* called when the text is updated by the external actor *)
	PROCEDURE (c: DateField) Update;
	BEGIN
		c.UpdateEd;
		IF c.ed.changeCount # 0 THEN
			c.ed.RetainCaret;
			c.Update^
		END
	END Update;


	(** **************** TimeField **************** **)

	PROCEDURE GetTimePart (VAR time: Dates.Time; part: INTEGER; VAR val, min, max: INTEGER);
	BEGIN
		IF part = -1 THEN part := 3 END;
		IF part = 4 THEN val := time.hour DIV 12; min := 0; max := 1
		ELSIF part = 3 THEN val := time.second; min := 0; max := 59
		ELSIF part = 2 THEN val := time.minute; min := 0; max := 59
		ELSE val := time.hour; min := 0; max := 23
		END
	END GetTimePart;

	PROCEDURE SetTimePart (VAR time: Dates.Time; part: INTEGER; val: INTEGER);
	BEGIN
		IF part = -1 THEN part := 3 END;
		IF part = 4 THEN time.hour := val * 12 + time.hour MOD 12
		ELSIF part = 3 THEN time.second := val
		ELSIF part = 2 THEN time.minute := val
		ELSE time.hour := val
		END
	END SetTimePart;

	PROCEDURE TimeToString (VAR time: Dates.Time; VAR str: ARRAY OF CHAR);
		VAR val, min, max: INTEGER;
	BEGIN
		GetTimePart(time, 1, val, min, max);
		str[0] := CHR(val DIV 10 + ORD("0"));
		str[1] := CHR(val MOD 10 + ORD("0"));
		str[2] := timeSep;
		GetTimePart(time, 2, val, min, max);
		str[3] := CHR(val DIV 10 + ORD("0"));
		str[4] := CHR(val MOD 10 + ORD("0"));
		str[5] := timeSep;
		GetTimePart(time, 3, val, min, max);
		str[6] := CHR(val DIV 10 + ORD("0"));
		str[7] := CHR(val MOD 10 + ORD("0"));
		str[8] := 0X
	END TimeToString;

	PROCEDURE (c: TimeField) Select, NEW;
		VAR sel: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.GetSel(c, sel);
			IF sel = -1 THEN sel := 3 END;
			c.ed.Select(3 * sel - 3, 3 * sel - 1)
		END
	END Select;

	PROCEDURE (c: TimeField) UpdateEd, NEW;
		VAR time: Dates.Time; s: ARRAY 20 OF CHAR;
	BEGIN
		c.ed.SetupCtl(c);
		c.ed.changeCount := 0;
		c.ed.maxLen := 10; c.ed.password := FALSE;
		c.ed.left := TRUE; c.ed.right := FALSE;
		IF c.undef THEN
			s := ""
		ELSE
			c.Get(c, time);
			TimeToString(time, s)
		END;
		IF c.ed.SetText(s) THEN INC(c.ed.changeCount) END;
		c.Select
	END UpdateEd;

	PROCEDURE (c: TimeField) Restore (l_, t_, r_, b_: INTEGER);
		VAR w, h, asc, dsc, sw: INTEGER;
	BEGIN
		IF c.ed.ctl = NIL THEN c.UpdateEd END;
		c.view.context.GetSize(w, h);
		IF c.ed.pad = 0 THEN
			c.ed.ctl.font.GetBounds(asc, dsc, sw);
			c.ed.pad := (h - (asc + dsc)) DIV 3;
			IF c.ed.pad < 2 * Ports.point THEN c.ed.pad := 2 * Ports.point END;
		END;
		c.ed.Setup(c, c.dot, c.dot, w - 2 * c.dot, h - 2 * c.dot);
		c.ed.outMargin.SetUni(1);
		c.ed.Restore;
		IF c.readOnly THEN
			c.DrawRect(0, 0, w, h, c.dot, param.frameReadOnly)
		ELSE
			IF c.mark THEN
				c.DrawRect(0, 0, w, h, c.dot * 2, param.frameFocus)
			ELSE
				c.DrawRect(0, 0, w, h, c.dot, param.frame)
			END
		END
	END Restore;

	PROCEDURE (c: TimeField) Edit (op: INTEGER; VAR v: Views.View; VAR w, h: INTEGER;
															VAR singleton, clipboard: BOOLEAN);
	BEGIN
		IF c.ed.ctl # NIL THEN
			IF clipboard & (op = Controllers.copy) THEN
				c.ed.changeCount := 0;
				(* copy the whole string *)
				c.ed.Select(0, 1024);
				c.ed.Edit(op, v, clipboard);
				c.Select
			END
		END
	END Edit;

	PROCEDURE (c: TimeField) WheelMove (x, y, op, nofLines_: INTEGER; VAR done: BOOLEAN);
		VAR mods: SET; isDown: BOOLEAN;
	BEGIN
		IF c.ed.ctl # NIL THEN
			done := TRUE; (* eat it! *)
			(* hack! *)
			c.Input(x, y, mods, isDown);
			IF mods * {Controllers.pick, Controllers.extend} # {} THEN INCL(mods, Controllers.modify) END;
			IF op IN {Controllers.incLine, Controllers.incPage} THEN c.KeyDown(AD, mods)
			ELSE c.KeyDown(AU, mods)
			END
		END
	END WheelMove;

	PROCEDURE (c: TimeField) MouseDown (x, y: INTEGER; buttons: SET);
		VAR p: INTEGER;
	BEGIN
		IF c.ed.ctl # NIL THEN
			c.ed.changeCount := 0;
			c.ed.MouseDown(x, y, buttons);
			(* fix selection *)
			IF c.ed.from < c.ed.to THEN p := c.ed.from ELSE p := c.ed.pos END;
			p := MAX(0, p) DIV 3 + 1;
			IF p <= 3 THEN
				c.SetSel(c, p);
				c.Select
			END;
			c.Update^
		END
	END MouseDown;

	PROCEDURE (c: TimeField) KeyDown (ch: CHAR; modifiers: SET);
		VAR sel, s, val, v, min, max, dd: INTEGER; time: Dates.Time; setNow: BOOLEAN;
	BEGIN
		IF c.ed.ctl # NIL THEN
			setNow := FALSE;
			c.ed.changeCount := 0;
			c.GetSel(c, sel); s := sel;
			IF s = -1 THEN s := 3 END;
			c.Get(c, time);
			GetTimePart(time, s, val, min, max); v := val;
			CASE ch OF
			| TAB, AR, timeSep, dateSep, " ": s := s MOD 3 + 1;
			| LTAB, AL: DEC(s); IF s <= 0 THEN s := 3 END
			| PL, DL: s := 1
			| PR, DR: s := 3
			| DELETE, BACKSPACE: v := min
			| AU:
				IF Controllers.modify IN modifiers THEN dd := 10 ELSE dd := 1 END;
				v := (v - min - dd) MOD (max - min + 1) + min
			| AD:
				IF Controllers.modify IN modifiers THEN dd := 10 ELSE dd := 1 END;
				v := (v - min + dd) MOD (max - min + 1) + min
			ELSE
				(*
				IF (ch >= "0") & (ch <= "9") THEN
					v := v * 10 MOD 100 + ORD(ch) - ORD("0");
					IF v > max THEN v := v MOD 10 ELSIF v < min THEN v := min END
				ELSIF (ch = 'n') OR (ch = 'N') THEN
					setNow := TRUE
				END
				*)
				setNow := (ch = 'n') OR (ch = 'N')
			END;
			IF setNow THEN
				IF ~c.disabled & ~c.readOnly THEN
					Dates.GetTime(time); c.Set(c, time); c.Update
				END
			ELSIF v # val THEN
				IF ~c.disabled & ~c.readOnly THEN
					SetTimePart(time, s, v); c.Set(c, time); c.Update
				END
			ELSIF s # sel THEN
				c.SetSel(c, s); c.Select
			END;
			c.ed.RetainCaret;
			c.Update^
		END
	END KeyDown;

	PROCEDURE (c: TimeField) Mark (on_, focus_: BOOLEAN);
	BEGIN
		c.Update^
	END Mark;

	PROCEDURE (c: TimeField) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;

	(* called when the text is updated by the external actor *)
	PROCEDURE (c: TimeField) Update;
	BEGIN
		c.UpdateEd;
		IF c.ed.changeCount # 0 THEN
			c.ed.RetainCaret;
			c.Update^
		END
	END Update;


	(* TreeFrame *)

	PROCEDURE (f: TreeFrame) Update;
		VAR sel: Dialog.TreeNode; i: INTEGER; selPath: ARRAY 16 OF Dialog.TreeNode;
		
		PROCEDURE InSelPath (node: Dialog.TreeNode): BOOLEAN;
			VAR i: INTEGER; res: BOOLEAN;
		BEGIN
			i := 0; res := FALSE;
			WHILE ~ res & (selPath[i] # NIL) DO
				IF selPath[i] = node THEN res := TRUE END;
				INC(i)
			END;
			RETURN res
		END InSelPath;
		
		PROCEDURE CheckLeaves (node: Dialog.TreeNode);
			VAR n: Dialog.TreeNode;
		BEGIN
			n := f.Child(f, node);
			WHILE n # NIL DO
				IF InSelPath(n) THEN
					IF ~n.IsExpanded() THEN
						f.SetExpansion(f, n, TRUE)
					END
				END;
				IF n.IsExpanded() THEN
					CheckLeaves(n)
				END;
				n := f.Next(f, n)
			END
		END CheckLeaves;
		
	BEGIN
		IF ~ f.dontOpenFoldersForSelected THEN
			(* expand folder of selected item *)
			sel := f.Selected(f);
			i := 0;
			WHILE sel # NIL DO
				sel := f.Parent(f, sel);
				selPath[i] := sel;
				INC(i)
			END;
			sel := f.Selected(f);
			CheckLeaves(NIL)
		END;
		f.Update^
	END Update;

	PROCEDURE (f: TreeFrame) DblClickOk (x_, y_: INTEGER): BOOLEAN;
	BEGIN
		RETURN TRUE
	END DblClickOk;
	
	PROCEDURE (c: TreeFrame) TreeTrackVSBMouse (), NEW;
		VAR
			x, y, ox, oy, opos: INTEGER; isDown: BOOLEAN; modifiers: SET;
	BEGIN
		IF c.rider # NIL THEN
			c.vsbPressed := TRUE;
			UpdateRoot(c);
			c.ForceUpdate;
			opos := c.vsb.pos;
			ox := MIN(INTEGER); oy := MIN(INTEGER);
			REPEAT
				c.Input(x, y, modifiers, isDown);
				x := x - c.vsb.x0;
				y := y - c.vsb.y0;
				IF ox = MIN(INTEGER) THEN ox := x END;
				IF oy = MIN(INTEGER) THEN oy := y END;
				IF (ox # x) OR (oy # y) THEN
					ox := x; oy := y;
					c.vsb.SBMouseTrack(x, y);
					IF opos # c.vsb.pos THEN
						opos := c.vsb.pos;
						c.yoffs := opos;			
						c.ForceUpdate
					END
				END
			UNTIL ~isDown;
			c.vsbPressed := FALSE;
			c.ForceUpdate
		END
	END TreeTrackVSBMouse;

	PROCEDURE (c: TreeFrame) TreeTrackHSBMouse (), NEW;
		VAR x, y, ox, oy, opos: INTEGER; isDown: BOOLEAN; modifiers: SET;
	BEGIN
		IF c.rider # NIL THEN
			c.hsbPressed := TRUE;
			UpdateRoot(c);
			c.ForceUpdate;
			opos := c.hsb.pos;
			ox := MIN(INTEGER); oy := MIN(INTEGER);
			REPEAT
				c.Input(x, y, modifiers, isDown);
				x := x - c.hsb.x0;
				y := y - c.hsb.y0;
				IF ox = MIN(INTEGER) THEN ox := x END;
				IF oy = MIN(INTEGER) THEN oy := y END;
				IF (ox # x) OR (oy # y) THEN
					ox := x; oy := y;
					c.hsb.SBMouseTrack(x, y);
					IF opos # c.hsb.pos THEN
						opos := c.hsb.pos;
						c.xoffs := opos;
						c.ForceUpdate
					END
				END
			UNTIL ~isDown;
			c.hsbPressed := FALSE;
			c.ForceUpdate
		END
	END TreeTrackHSBMouse;
	
	PROCEDURE (f: TreeFrame) CheckYPos (h: INTEGER), NEW; 
	VAR treeHeight: INTEGER;
	BEGIN
		IF f.yoffs < 0 THEN
			f.yoffs := 0
		ELSE
			treeHeight := (f.lines  + 1) * f.lineHeight;
			IF treeHeight < h THEN
				f.yoffs := 0
			ELSE
				IF f.yoffs > (treeHeight - h) THEN
					f.yoffs := (treeHeight - h)
				END
			END
		END;
	END CheckYPos;

	PROCEDURE (f: TreeFrame) MouseDown (x, y: INTEGER; buttons: SET);
		VAR w, h, id: INTEGER;
	BEGIN
		IF f.view = NIL THEN RETURN END;
		f.view.context.GetSize(w, h);
		IF (f.vsb # NIL) & (f.vsb.length > 0) & (w - x < f.vsb.Width()) THEN
		
			IF f.vsb.SBMouseDown(x - f.vsb.x0, y - f.vsb.y0) THEN
				f.TreeTrackVSBMouse
			ELSE
				f.yoffs := f.vsb.pos (* the caller will repaint us *)
			END
		ELSIF (f.hsb # NIL) & (f.hsb.length > 0) & (h - y < f.hsb.Width()) THEN
		
			IF f.hsb.SBMouseDown(x - f.hsb.x0, y - f.hsb.y0) THEN
				f.TreeTrackHSBMouse
			ELSE
				f.xoffs := f.hsb.pos (* the caller will repaint us *)
			END
			
		ELSIF ~ f.readOnly & (f.lines > 0) THEN
		
			id := (y + f.yoffs - f.lineHeight DIV 3) DIV f.lineHeight;
			id := MAX(0, MIN(id, f.lines - 1) );
			
			IF f.nodesCache # NIL THEN
				IF x > f.nodesCache[id].p THEN
					f.Select(f, f.nodesCache[id].n);
					IF (Controllers.doubleClick IN buttons) THEN
						IF f.nodesCache[id].n.IsExpanded() THEN
							f.SetExpansion(f, f.nodesCache[id].n, FALSE)
						ELSE
							f.SetExpansion(f, f.nodesCache[id].n, TRUE)
						END
					END;
					f.CheckYPos(h);
					f.Update
				ELSE
					IF f.nodesCache[id].n.IsExpanded() THEN
						f.SetExpansion(f, f.nodesCache[id].n, FALSE)
					ELSE
						f.SetExpansion(f, f.nodesCache[id].n, TRUE)
					END;
					f.CheckYPos(h);
					f.dontOpenFoldersForSelected := TRUE;
					f.Update;
					f.dontOpenFoldersForSelected := FALSE;
				END
			END
		END
	END MouseDown;

	PROCEDURE (f: TreeFrame) WheelMove (x_, y_, op, nofLines: INTEGER; VAR done: BOOLEAN);
		VAR asc, dsc, sw, w, h: INTEGER;
	BEGIN
		f.view.context.GetSize(w, h);
		done := TRUE;
		IF op = Controllers.decLine THEN nofLines := -nofLines END;
		f.font.GetBounds(asc, dsc, sw);
		INC(f.yoffs, (asc + dsc) * nofLines);
		f.CheckYPos(h);		
		f.Update
	END WheelMove;

	PROCEDURE (f: TreeFrame) Restore (l_, t_, r_, b_: INTEGER);
		VAR sel: Dialog.TreeNode; bkcol, w, h, asc, dsc, sw, x, y, i: INTEGER;
			vsb: VScrollBar; vsbSize: INTEGER; hsb: HScrollBar; hsbSize: INTEGER;
			sx1, sy1, sx2, sy2, ry: INTEGER;
			boldFont: Fonts.Font;
			
			PROCEDURE AddNodeToCache (node: Dialog.TreeNode; pos, i: INTEGER);
			VAR nc: POINTER TO ARRAY OF NodeItem;
			BEGIN
				IF f.nodesCache = NIL THEN
					IF i < 128 THEN
						NEW(f.nodesCache, 128)
					ELSE
						NEW(f.nodesCache, i * 2)
					END
				ELSE
					IF i >= LEN(f.nodesCache) THEN
						NEW(nc, LEN(f.nodesCache)*2);
						FOR i := 0 TO LEN(f.nodesCache) - 1 DO
							nc[i] := f.nodesCache[i]
						END;
						f.nodesCache := nc 
					END
				END;
				f.nodesCache[i].n := node;
				f.nodesCache[i].p := pos;
			END AddNodeToCache;
			
			PROCEDURE DrawTriangle(f: Views.Frame; x, y: INTEGER; open: BOOLEAN);
				VAR path: ARRAY 3 OF Ports.Point; point, point2, point3, point4: INTEGER;
			BEGIN
				point := Ports.point;
				point2 := 2 * Ports.point - (2 * Ports.point) MOD f.unit;
				point3 := 3 * Ports.point - (3 * Ports.point) MOD f.unit;
				point4 := 4 * Ports.point - (4 * Ports.point) MOD f.unit;
				IF open THEN
					path[0].x := x - point2;
					path[0].y := y + point;
					path[1].x := x + point3;
					path[1].y := y + point;
					path[2].x := x + point3;
					path[2].y := y - point4;
					f.DrawPath(path, 3, Ports.fill, Ports.black, Ports.closedPoly)
				ELSE
					path[0].x := x;
					path[0].y := y - point3;
					path[1].x := x;
					path[1].y := y + point3;
					path[2].x := x + point3;
					path[2].y := y;
					f.DrawPath(path, 3, f.dot, Ports.grey50, Ports.closedPoly)
				END
			END DrawTriangle;
	
			
			PROCEDURE DrawLeaves (node: Dialog.TreeNode; lev: INTEGER);
				TYPE
					Item = RECORD
						node: Dialog.TreeNode;
						str: Dialog.String; 
					END;
				
				VAR
					tn: Dialog.TreeNode; xShift, w, prevLineSameLevel, n: INTEGER; 
					lvs: POINTER TO ARRAY OF Item; expandable: BOOLEAN;
					font: Fonts.Font;
				
				PROCEDURE SortLeaves;
					
					PROCEDURE LessThan (a, b: Item): BOOLEAN;
						VAR res, equal: BOOLEAN; i: INTEGER;
					BEGIN
						res := FALSE;
						equal := TRUE;
						i := 0;
						WHILE equal & (i < LEN(a.str$)) & (i < LEN(b.str$)) DO
							IF ORD(CAP(a.str[i])) = ORD(CAP(b.str[i])) THEN
								(* pass *)
							ELSIF ORD(CAP(a.str[i])) < ORD(CAP(b.str[i])) THEN
								res := TRUE;
								equal := FALSE
							ELSE
								equal := FALSE
							END;
							INC(i)
						END;
						RETURN res 
					END LessThan;
					
					PROCEDURE Sort(L, R: INTEGER);
						VAR i, j: INTEGER;  w, x: Item;
					BEGIN
						i := L;  j := R;
						x := lvs[(L+R) DIV 2];
						REPEAT
							WHILE LessThan(lvs[i], x) DO  i := i + 1;  END;
							WHILE LessThan(x, lvs[j]) DO  j := j - 1;  END;
							IF i <= j THEN
								w := lvs[i];
								lvs[i] := lvs[j];
								lvs[j] := w;
								i := i + 1;
								j := j -1;
							END;
						UNTIL i > j;
						IF L < j THEN Sort(L, j) END;
						IF i < R THEN Sort(i, R) END
					END Sort;
					
				BEGIN
					Sort(0, LEN(lvs) - 1)
				END SortLeaves;
			
			BEGIN
				
				tn := f.Child(f, node);
				IF tn = NIL THEN RETURN END;
				n := 0;
				WHILE tn # NIL DO
					tn := f.Next(f, tn);
					INC(n)
				END;
				NEW(lvs, n);
				
				tn := f.Child(f, node);
				n := 0;
				WHILE tn # NIL DO
					lvs[n].node := tn;
					tn.GetName(lvs[n].str);
					INC(n);
					tn := f.Next(f, tn)
				END;
				
				IF f.sorted THEN SortLeaves END;
				
				IF f.haslines & (node # NIL) THEN
					IF (LEN(lvs) > 0) & lvs[0].node.IsFolder() THEN
						sw := 4 * Ports.point
					ELSE
						sw := 0
					END;
					(* first vertical line *)
					f.DrawLine(
						f.leftMargin + lev * Ports.point, 
						f.lineHeight * f.lines + f.strHeight DIV 4 - f.yoffs, 
						f.leftMargin + lev * Ports.point, 
						f.lineHeight * (f.lines+1) - f.strHeight DIV 4 - f.yoffs - sw,
						f.dot, Ports.grey25)
				END;
				
				FOR n := 0 TO LEN(lvs) - 1 DO
					
					INC(f.lines);
					
					IF f.haslines THEN
						xShift := 5 * Ports.point;
						prevLineSameLevel := f.lines
					ELSE
						xShift := 0
					END;
					
					x := f.leftMargin + xShift + (lev + 5) * Ports.point;
					y := f.lineHeight * f.lines - f.yoffs;
					
					AddNodeToCache(lvs[n].node, x, f.lines-1);
					
					IF (lvs[n].str[0] = "*") & (lvs[n].str[LEN(lvs[n].str$) - 1] = "*") THEN
						IF boldFont = NIL THEN
							boldFont := Fonts.dir.This(f.font.typeface, f.font.size, f.font.style, Fonts.bold)
						END;
						font := boldFont;
						lvs[n].str[LEN(lvs[n].str$) - 1] := 0X;
						i := 0;
						WHILE i < LEN(lvs[n].str$) DO
							lvs[n].str[i] := lvs[n].str[i+1];
							INC(i)
						END;
						lvs[n].str[i] := 0X
					ELSE
						font := f.font
					END;
					
					w := font.StringWidth(lvs[n].str);
					
					sw := Ports.point;
					sw := sw - sw MOD f.unit;
					IF f.foldericons THEN
						sx1 := x + 13 * Ports.point
					ELSE
						sx1 := x
					END;
					sx1 := sx1 - sx1 MOD f.unit;
					sy1 := y - f.strHeight + 2 * Ports.point;
					sy1 := sy1 - sy1 MOD f.unit;
					IF f.foldericons THEN
						sx2 := x + w + 14 * Ports.point;
					ELSE
						sx2 := x + w + Ports.point;
					END;
					sx2 := sx2 - sx2 MOD f.unit; 
					sy2 := y + 2 * Ports.point;
					sy2 := sy2 - sy2 MOD f.unit;
					ry := y - f.strHeight + Ports.point;
					IF Dialog.isWine THEN
						INC(ry, 2 * Ports.point)
					ELSIF (Dialog.platform # Dialog.windows) THEN
						INC(ry, 3 * Ports.point)
					END;
					ry := ry - ry MOD f.unit;
					
					w := sx1 + w;
					
					DEC(sx1, f.xoffs);
					DEC(sx2, f.xoffs);
					
					IF w > f.maxWidth THEN f.maxWidth := w END;
					
					IF lvs[n].node = sel THEN
						f.DrawRect(sx1 - sw, sy1 - sw, sx2 + sw, sy2 + sw, Ports.fill, param.backListCursor)
					END;
					
					expandable := lvs[n].node.IsFolder() & (lvs[n].node.NofChildren() > 0);
					
					IF f.haslines THEN
						IF expandable THEN
							sw := 4 * Ports.point;
							IF sw < 6 * f.dot THEN
								sw := 6 * f.dot
							END
						ELSE
							sw := 0
						END;
						(* horizontal *)
						f.DrawLine(
							f.leftMargin + lev * Ports.point + sw - f.xoffs,
							f.lineHeight * f.lines - f.strHeight DIV 4 - f.yoffs, 
							f.leftMargin + (lev + 8) * Ports.point - f.xoffs,
							f.lineHeight * f.lines - f.strHeight DIV 4 - f.yoffs, 
							f.dot, Ports.grey25)
					END;
					
					asc := - 2 * Ports.point - f.xoffs;
					IF f.foldericons THEN
						IF lvs[n].node.IsFolder() THEN
							IF lvs[n].node.IsExpanded() THEN
								f.DrawRaster(folderopen.raster, x + asc, ry)
							ELSE
								f.DrawRaster(folder.raster, x + asc, ry)
							END
						ELSE
							(* IF n.leafType = Dialog.leaf_notOk THEN
								f.DrawRaster(leafNotOk.raster, x + asc, ry)
							ELSIF n.leafType = Dialog.leaf_ok THEN
								f.DrawRaster(leafOk.raster, x + asc, ry)
							ELSE
							*)
								f.DrawRaster(leaf.raster, x + asc, ry)
							(* END *)
						END;
					END;
					
					IF expandable THEN
						DrawTriangle(f,
							f.leftMargin + (lev - 1) * Ports.point  - f.xoffs, 
							f.lineHeight * f.lines - f.strHeight DIV 4 - f.yoffs,
							lvs[n].node.IsExpanded())
					END;
					
					IF lvs[n].node = sel THEN
						sw := param.textSelList
					ELSE
						sw := param.textList
					END;
					f.DrawString(sx1, sy2 - 2 * f.dot, sw, lvs[n].str, font);
					
					IF lvs[n].node.IsExpanded() & (lvs[n].node.NofChildren() > 0) THEN
						DrawLeaves(lvs[n].node, lev + 10)
					END;
					
					IF f.haslines & (n < LEN(lvs) - 1) THEN
						IF expandable THEN
							IF lvs[n].node.IsExpanded() THEN
								sw := 3 * Ports.point
							ELSE
								sw := 5 * Ports.point
							END
						ELSE
							sw := 0
						END;
						IF lvs[n+1].node.IsFolder() 
							& (lvs[n+1].node.NofChildren() > 0) THEN
							asc := 4 * Ports.point
						ELSE
							asc := 0
						END;
						f.DrawLine(
							f.leftMargin + lev * Ports.point - f.xoffs, 
							f.lineHeight * prevLineSameLevel - f.strHeight DIV 4 - f.yoffs + sw, 
							f.leftMargin + lev * Ports.point - f.xoffs, 
							f.lineHeight * (f.lines+1) - f.strHeight DIV 4 - f.yoffs - asc,
							f.dot, Ports.grey25)
					END
				END
			END DrawLeaves;

	BEGIN
		
		f.view.context.GetSize(w, h);
		f.font.GetBounds(asc, dsc, sw);
		f.strHeight := asc + dsc;
		f.lineHeight := (f.strHeight DIV 3) * 4;
		
		IF f.disabled OR f.readOnly THEN
			bkcol := param.backListDisabled
		ELSIF f.mark THEN
			bkcol := param.backList
		ELSE
			bkcol := param.backListBlured
		END;
		f.DrawRect(0, 0, w, h, Ports.fill, bkcol);
		
		sel := f.Selected(f);
		
		f.lines := 0;
		f.maxWidth := 0;
		DrawLeaves(NIL, 0);
		f.totalHeight := f.lineHeight * (f.lines + 1);
		
		(* tree scroll bars *)
		
		IF f.totalHeight > h THEN
			IF f.vsb = NIL THEN
				NEW(vsb);
				vsb.Init;
				f.vsb := vsb
			ELSE
				vsb := f.vsb
			END;
			vsb.min := 0;
			vsb.contentSize := f.totalHeight;
			vsb.max := vsb.contentSize - h;
			vsb.pos := f.yoffs;
			vsb.pageSize := h;
			vsb.arrowStep := f.lineHeight;
			vsb.length := h;
			IF ~vsb.CanFit() THEN
				vsb := NIL
			ELSE
				vsbSize := MAX(0, vsb.Width());
				vsb.length := h - vsb.Width();
				IF (vsbSize = 0) OR ((vsbSize + 4 * Ports.point) >= w) THEN
					vsb := NIL
				END
			END
		ELSE
			vsb := NIL
		END;
		IF vsb = NIL THEN vsbSize := 0; f.vsb := NIL END;
		
		IF (f.maxWidth > (w - vsbSize)) & (h > f.lineHeight * 2) THEN
			IF f.hsb = NIL THEN
				NEW(hsb);
				hsb.Init;
				f.hsb := hsb
			ELSE
				hsb := f.hsb
			END;
			hsb.min := 0;
			hsb.contentSize := f.maxWidth + f.leftMargin * 2;
			hsb.max := (hsb.contentSize - w + vsbSize + Ports.mm);
			hsb.pos := f.xoffs;
			hsb.pageSize := w;
			hsb.arrowStep := Ports.mm;
			hsb.length := w;
			IF ~hsb.CanFit() THEN
				hsb := NIL
			ELSE
				hsbSize := MAX(0, hsb.Width());
				hsb.length := w;
				IF vsb # NIL THEN
					DEC(hsb.length, vsbSize);
					DEC(vsb.length, hsbSize)
				END;
				IF (hsbSize = 0) OR ((hsbSize + 4 * Ports.point) >= h) THEN
					hsbSize := 0; hsb := NIL
				END
			END;
			IF vsb # NIL THEN
				INC(vsb.max, hsbSize);
				INC(hsb.max, vsbSize);
			END;
		ELSE
			f.hsb := NIL; hsbSize := 0; hsb := NIL; 
		END;
		
		IF vsb # NIL THEN
			vsb.x0 := w - vsbSize - Ports.point;
			vsb.y0 := 0;
			vsb.Draw(f, vsb.x0, vsb.y0, h - hsbSize, f.vsbPressed);
		END;
		IF hsb # NIL THEN
			hsb.x0 := 0;
			hsb.y0 := h - hsbSize;
			hsb.Draw(f, hsb.x0, hsb.y0, w - vsbSize, f.hsbPressed);
		END;
		
		IF (hsbSize > 0) & (vsbSize > 0) THEN
			f.DrawRect(w - vsbSize, h - hsbSize, w + 2 * f.dot, h + 2 * f.dot, Ports.fill, param.backScrollbar)
		END;
		
		(* outer frame *)
		IF f.readOnly THEN
			f.DrawRect(0, 0, w, h, f.dot, param.frameReadOnly)
		ELSE
			IF f.mark THEN
				f.DrawRect(0, 0, w, h, f.dot * 2, param.frameFocus)
			ELSE
				f.DrawRect(0, 0, w, h, f.dot, param.frame)
			END
		END;
		
	END Restore;

	PROCEDURE (f: TreeFrame) KeyDown (ch: CHAR; modifiers_: SET);
		VAR sel, parent, child, next, prev: Dialog.TreeNode;
		
	BEGIN
		CASE ch OF
		| AL: (* arrow left *) 
			sel := f.Selected(f);
			IF sel # NIL THEN
				IF sel.IsExpanded() THEN
					f.SetExpansion(f, sel, FALSE);
					f.dontOpenFoldersForSelected := TRUE;
					f.Update;
					f.dontOpenFoldersForSelected := FALSE;
				END
			END
		| AR: (* arrow right *) 
			sel := f.Selected(f);
			IF sel # NIL THEN
				IF ~sel.IsExpanded() THEN
					f.SetExpansion(f, sel, TRUE);
					f.dontOpenFoldersForSelected := TRUE;
					f.Update;
					f.dontOpenFoldersForSelected := FALSE;
				END
			END
		| AU: (* arrow up *) 
			sel := f.Selected(f);
			IF sel # NIL THEN
				parent := f.Parent(f, sel);
				prev := NIL;
				next := f.Child(f, parent);
				WHILE (next # NIL) & (sel # next) DO
					prev := next;
					next := f.Next(f, next)
				END;
				IF prev = NIL THEN
					IF parent # NIL THEN
						f.Select(f, parent);
						f.Update
					END
				ELSE
					IF sel = next THEN
						f.Select(f, prev);
						f.Update
					END
				END
			END
		| AD: (* arrow down *) 
			sel := f.Selected(f);
			IF sel # NIL THEN
				IF sel.IsExpanded() THEN
					child := f.Child(f, sel);
					IF child # NIL THEN
						f.Select(f, child);
						f.Update
					ELSE
						sel := f.Next(f, sel);
						IF sel # NIL THEN
							f.Select(f, sel);
							f.Update
						END
					END
				ELSE
					next := f.Next(f, sel);
					IF next # NIL THEN
						f.Select(f, next);
						f.Update
					ELSE
						parent := f.Parent(f, sel);
						IF parent # NIL THEN
							next := f.Next(f, parent);
							IF next # NIL THEN
								f.Select(f, next);
								f.Update
							END
						END
					END
				END
			END
		ELSE
		END
	END KeyDown;

	PROCEDURE (f: TreeFrame) GetSize (OUT w, h: INTEGER);
	BEGIN
		f.view.context.GetSize(w, h)
	END GetSize;


	(* Directory *)

	PROCEDURE (d: Directory) GetPushButtonSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 56 * Ports.point END;
		IF h = Views.undefined THEN h := 18 * Ports.point END
	END GetPushButtonSize;

	PROCEDURE (d: Directory) GetCheckBoxSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 60 * Ports.point END;
		IF h = Views.undefined THEN h := 12 * Ports.point END
	END GetCheckBoxSize;

	PROCEDURE (d: Directory) GetRadioButtonSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 60 * Ports.point END;
		IF h = Views.undefined THEN h := 12 * Ports.point END
	END GetRadioButtonSize;

	PROCEDURE (d: Directory) GetScrollBarSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 120 * Ports.point END;
		IF h = Views.undefined THEN h := 12 * Ports.point END
	END GetScrollBarSize;

	PROCEDURE (d: Directory) GetFieldSize (max: INTEGER; VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN
			IF max = 0 THEN w := 80 * Ports.point
			ELSIF max < 10 THEN w := 32 * Ports.point
			ELSIF max < 15 THEN w := 56 * Ports.point
			ELSIF max < 30 THEN w := 80 * Ports.point
			ELSIF max < 100 THEN w := 120 * Ports.point
			ELSE w := 150 * Ports.point
			END
		END;
		IF h = Views.undefined THEN h := 17 * Ports.point END
	END GetFieldSize;

	PROCEDURE (d: Directory) GetUpDownFieldSize (max_: INTEGER; VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 56 * Ports.point END;
		IF h = Views.undefined THEN h := 17 * Ports.point END
	END GetUpDownFieldSize;

	PROCEDURE (d: Directory) GetDateFieldSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 72 * Ports.point END;
		IF h = Views.undefined THEN h := 17 * Ports.point END
	END GetDateFieldSize;

	PROCEDURE (d: Directory) GetTimeFieldSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 72 * Ports.point END;
		IF h = Views.undefined THEN h := 17 * Ports.point END
	END GetTimeFieldSize;

	PROCEDURE (d: Directory) GetColorFieldSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 36 * Ports.point END;
		IF h = Views.undefined THEN h := 18 * Ports.point END
	END GetColorFieldSize;

	PROCEDURE (d: Directory) GetListBoxSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 100 * Ports.point END;
		IF h = Views.undefined THEN h := 18 * Ports.point END
	END GetListBoxSize;

	PROCEDURE (d: Directory) GetSelectionBoxSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 100 * Ports.point END;
		IF h = Views.undefined THEN h := 54 * Ports.point END
	END GetSelectionBoxSize;

	PROCEDURE (d: Directory) GetComboBoxSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 100 * Ports.point END;
		IF h = Views.undefined THEN h := 18 * Ports.point END
	END GetComboBoxSize;

	PROCEDURE (d: Directory) GetCaptionSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 50 * Ports.point END;
		IF h = Views.undefined THEN h := 12 * Ports.point END
	END GetCaptionSize;

	PROCEDURE (d: Directory) GetGroupSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 100 * Ports.point END;
		IF h = Views.undefined THEN h := 100 * Ports.point END
	END GetGroupSize;

	PROCEDURE (d: Directory) GetTreeFrameSize (VAR w, h: INTEGER);
	BEGIN
		IF w = Views.undefined THEN w := 100 * Ports.point END;
		IF h = Views.undefined THEN h := 100 * Ports.point END
	END GetTreeFrameSize;


	PROCEDURE (d: Directory) NewPushButton* (): StdCFrames.PushButton;
		VAR f: PushButton;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewPushButton;

	PROCEDURE (d: Directory) NewCheckBox* (): StdCFrames.CheckBox;
		VAR f: CheckBox;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewCheckBox;

	PROCEDURE (d: Directory) NewRadioButton* (): StdCFrames.RadioButton;
		VAR f: RadioButton;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewRadioButton;

	PROCEDURE (d: Directory) NewScrollBar* (): StdCFrames.ScrollBar;
		VAR f: ScrollBar;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewScrollBar;

	PROCEDURE (d: Directory) NewField* (): StdCFrames.Field;
		VAR f: Field;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.ed);
		f.ed.Init;
		f.lastAction := 0;
		RETURN f
	END NewField;

	PROCEDURE (d: Directory) NewUpDownField* (): StdCFrames.UpDownField;
		VAR f: UpDownField;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.ed);
		f.ed.Init;
		f.lastval := 0; (* doesn't matter *)
		RETURN f
	END NewUpDownField;

	PROCEDURE (d: Directory) NewDateField* (): StdCFrames.DateField;
		VAR f: DateField;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		f.dateSep := dateSep;
		f.yearPart := 1; f.monthPart := 2; f.dayPart := 3; f.del1 := 4; f.del2 := 7;
		NEW(f.ed);
		f.ed.Init; f.ed.noMouseSelect := TRUE;
		RETURN f
	END NewDateField;

	PROCEDURE (d: Directory) NewTimeField* (): StdCFrames.TimeField;
		VAR f: TimeField;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.ed);
		f.ed.Init; f.ed.noMouseSelect := TRUE;
		RETURN f
	END NewTimeField;

	PROCEDURE (d: Directory) NewColorField* (): StdCFrames.ColorField;
		VAR f: ColorField;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewColorField;

	PROCEDURE (d: Directory) NewListBox* (): StdCFrames.ListBox;
		VAR f: ListBox;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.lst);
		f.lst.Init;
		f.fixCursor := TRUE;
		f.closedTime := 0;
		f.popupOverlay := NIL;
		RETURN f
	END NewListBox;

	PROCEDURE (d: Directory) NewSelectionBox* (): StdCFrames.SelectionBox;
		VAR f: SelectionBox;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.lst);
		f.lst.Init;
		f.fixCursor := TRUE;
		RETURN f
	END NewSelectionBox;

	PROCEDURE (d: Directory) NewComboBox* (): StdCFrames.ComboBox;
		VAR f: ComboBox;
	BEGIN
		NEW(f);
		f.noRedraw := FALSE;
		NEW(f.ed);
		f.ed.Init;
		NEW(f.lst);
		f.lst.Init;
		f.fixCursor := TRUE;
		f.closedTime := 0;
		f.popupOverlay := NIL;
		RETURN f
	END NewComboBox;

	PROCEDURE (d: Directory) NewCaption* (): StdCFrames.Caption;
		VAR f: Caption;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		f.changeCount := 0;
		RETURN f
	END NewCaption;

	PROCEDURE (d: Directory) NewGroup* (): StdCFrames.Group;
		VAR f: Group;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		RETURN f
	END NewGroup;

	PROCEDURE (d: Directory) NewTreeFrame* (): StdCFrames.TreeFrame;
		VAR f: TreeFrame;
	BEGIN
		NEW(f);
		f.noRedraw := TRUE;
		f.yoffs := 0;
		f.dontOpenFoldersForSelected := FALSE;
		f.leftMargin := 6 * Ports.point;
		IF folder = NIL THEN
			folder := StdRasters.NewModelFromSpec(Files.dir.This("Std/Rsrc"), 
				"tree_folder.png", FALSE);
			folderopen := StdRasters.NewModelFromSpec(Files.dir.This("Std/Rsrc"), 
				"tree_folderopen.png", FALSE);
			leaf := StdRasters.NewModelFromSpec(Files.dir.This("Std/Rsrc"), 
				"tree_leaf.png", FALSE);
		END;
		RETURN f
	END NewTreeFrame;

	PROCEDURE InitColors;
	BEGIN
		colors[0] := Ports.RGBColor(0, 0, 0);
		colors[1] := Ports.RGBColor(150, 150, 150);
		colors[2] := Ports.RGBColor(255, 255, 255);
		colors[3] := Ports.RGBColor(37, 78, 118);
		colors[4] := Ports.RGBColor(150, 0, 0);
		colors[5] := Ports.RGBColor(0, 150, 0);
		colors[6] := Ports.RGBColor(0, 128, 255);
		colors[7] := Ports.RGBColor(255, 255, 128);
		colors[8] := Ports.RGBColor(255, 128, 0);
		colors[9] := Ports.RGBColor(255, 0, 255);
	END InitColors;

	PROCEDURE InitParam (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			
		END
	END InitParam;

	PROCEDURE InitDefaults (p: ANYPTR);
		VAR selectColor: INTEGER;
	BEGIN
		WITH p: Param DO
			selectColor := Ports.RGBColor(37, 78, 118);
			
			p.backPopup := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.textPopup := Ports.RGBColor(0, 0, 0);
			
			p.back := Ports.RGBColor(0E5H, 0E5H, 0E5H);
			p.text := Ports.black;
			
			p.backHover := Ports.RGBColor(0CCH, 0CCH, 0CCH);
			p.textHover := Ports.RGBColor(000H, 000H, 000H);
			
			p.backDisabled := Ports.grey25;
			p.textDisabled := Ports.grey50;
			
			p.backReadOnly := Ports.grey25;
			p.textReadOnly := Ports.RGBColor(055H, 055H, 055H);
			
			p.backPressed := Ports.RGBColor(37, 78, 118);
			p.textPressed := Ports.RGBColor(0FFH, 0FFH, 0FFH);
			
			(* Editor *)
			
			p.backEditor := Ports.white;
			p.textEditor := Ports.black;
			p.backEditorSelection := selectColor;
			p.textEditorSelection := Ports.white;
			
			p.backEditorDisabled := Ports.grey25;
			p.textEditorDisabled := Ports.grey50;
			p.backEditorDisabledSelection := Ports.RGBColor(090H, 090H, 090H);
			p.textEditorDisabledSelection := Ports.RGBColor(066H, 066H, 066H);
			
			p.backEditorReadOnly := Ports.grey12;
			p.textEditorReadOnly := Ports.grey75;
			p.backEditorReadOnlySelection := Ports.grey50;
			p.textEditorReadOnlySelection := Ports.grey12;
			
			p.backEditorBlured := Ports.white;
			p.textEditorBlured := Ports.black;
			p.backEditorBluredSelection := Ports.RGBColor(074H, 074H, 073H);
			p.textEditorBluredSelection := Ports.RGBColor(000H, 000H, 000H);
			
			(* List *)
			
			p.backList := Ports.white;
			p.textList := Ports.black;
			p.backSelList := selectColor;
			p.textSelList := Ports.white;
			p.backListCursor := selectColor;
			p.textListCursor := Ports.white;
			p.backSelListCursor := selectColor;
			p.textSelListCursor := Ports.white;
			
			p.backListDisabled := Ports.RGBColor(0E5H, 0E5H, 0E5H);
			p.textListDisabled := Ports.RGBColor(044H, 044H, 044H);
			p.backSelListDisabled := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.textSelListDisabled := Ports.RGBColor(022H, 022H, 022H);
			p.backListCursorDisabled := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.textListCursorDisabled := Ports.RGBColor(022H, 022H, 022H);
			p.backSelListCursorDisabled := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.textSelListCursorDisabled := Ports.RGBColor(066H, 066H, 066H);
			
			p.backListBlured := Ports.RGBColor(0FAH, 0FAH, 0FAH);
			p.textListBlured := Ports.black;
			p.backSelListBlured := Ports.RGBColor(0AAH, 0AAH, 0FFH);
			p.textSelListBlured := Ports.black;
			p.backListCursorBlured := Ports.RGBColor(0DCH, 0DCH, 0DCH);
			p.textListCursorBlured := Ports.black;
			p.backSelListCursorBlured := Ports.RGBColor(0D0H, 0DDH, 0D0H);
			p.textSelListCursorBlured := Ports.RGBColor(000H, 000H, 0FFH);
			
			(* scrollbar *)
			
			p.backSlider := Ports.RGBColor(077H, 077H, 077H);
			p.backPressedSlider := Ports.RGBColor(055H, 055H, 055H);
			p.trackSlider := Ports.RGBColor(055H, 055H, 055H);
			
			p.backScrollbar := Ports.RGBColor(0DDH, 0DDH, 0DDH);
			p.backScrollbarButtons := Ports.RGBColor(0CCH, 0CCH, 0CCH);
			p.knobScrollbar := Ports.RGBColor(0BBH, 0BBH, 0BBH);
			p.knobScrollbarPressed := Ports.RGBColor(090H, 090H, 090H);
			p.scrollbarKnobMargin := 2;
			
			(* selecting controls *) 
			p.frame := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.frameFocus := Ports.RGBColor(099H, 099H, 099H);
			p.frameReadOnly := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.frameDisabled := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			
			p.frameFace := Ports.RGBColor(0AAH, 0AAH, 0AAH);
			p.frameLight := Ports.RGBColor(0FFH, 0FFH, 0FFH);
			p.frameSemi := Ports.RGBColor(0BBH, 0BBH, 0BBH);
			p.frameDark := Ports.RGBColor(000H, 000H, 000H);
			p.frameShadow := Ports.RGBColor(055H, 055H, 055H);
			p.arrowDisabled := Ports.RGBColor(090H, 090H, 090H);
			p.arrowReadOnly := Ports.RGBColor(090H, 090H, 090H);
			
			p.arrowsColor := Ports.RGBColor(022H, 022H, 022H);
			p.arrowsSize := 3 * Ports.point;
			p.sbThickness := 12 * Ports.point;
			
			p.checkBoxFont := Fonts.dir.This("Verdana", 12 * Fonts.point, {}, Fonts.normal);
			
			p.InitDefaults := InitDefaults;
			p.Init := InitParam;
			p.Init(p);
		END
	END InitDefaults;

	PROCEDURE RevealPasswords*;
	BEGIN
		revealPasswords := TRUE
	END RevealPasswords;

	PROCEDURE HidePasswords*;
	BEGIN
		revealPasswords := FALSE
	END HidePasswords;

	PROCEDURE Init;
		VAR dir: Directory;
	BEGIN
		StdCFrames.setFocus := TRUE;
		inHandleMouse := FALSE;
		revealPasswords := FALSE;
		InitColors;
		NEW(dir); StdCFrames.SetDir(dir);
		NEW(param); InitDefaults(param);
	END Init;

BEGIN
	Init
END StdStdCFrames.