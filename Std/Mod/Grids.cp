MODULE StdGrids;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Anton Dmitriev, Ivan Denisov, Ketmar Dark"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		Views, Controllers, Ports, Models, Fonts, Stores, Services, Properties, Windows, Dialog, StdWindows, StdCFrames, StdDocuments;

	CONST
		version = 1;
		mm = Ports.mm;
		
		magicWidth = -28051978;
		
		fixedPointBase = 32768;
		
		(* keep in synch with HostPorts constants *)
		resizeHCursor = 16; resizeVCursor = 17; stopCursor = 22;
		
		stack* = 0; divider* = 1;	(* kind of View *)
		
		remove* = 0; activate* = 1; setTitle* = 2;
		vertical* = FALSE; horizontal* = ~vertical;
		append* = -1;	(** possible value for PROCEDURE Transfer(... pos: INTEGER...) *)
		
		(* navigation methods *)
		navMethodDefault* = 0; (* Ctrl+PageUp, Ctrl+PageDown *)
		navMethodAltPg* = 1; (* Alt+PageUp, Alt+PageDown *)
		navMethodCtrlAltArr* = 3; (* Ctrl+Alt+Left, Ctrl+Alt+Right *)
		
		(* these constants need to be in sync with Containers docu *)
		PageLeft = 10X;
		PageRight = 11X;
		PageUp = 12X;
		PageDown = 13X;
		LeftArrow = 1CX;
		RightArrow = 1DX;
		
		left = TRUE; right = ~left;
		
		none = -1;

	TYPE
		Pixels = INTEGER;	(* for values in pixel coordinates *)
		UCs = INTEGER;	(* used for values in universal coordinates *)
		
		GenericContext* = POINTER TO ABSTRACT RECORD (Models.Context)	(* revisit *)
			fnt: Fonts.Font;
			asc, dsc: INTEGER;
		END;
		
		View* = POINTER TO ABSTRACT RECORD (Views.View)
			kind-: BYTE;
			ctx-: POINTER TO ARRAY OF Context;
			active: INTEGER;
			canDrop*: PROCEDURE (v: View; ind: INTEGER): BOOLEAN;
			transferCopy*: 	(* may not be necessary, cf. StdTiles.Proxy.CopyFromSimpleView *)
				PROCEDURE (source: Views.View; toV: View; toF: Views.Frame): Views.View;
			backdrop*: Views.View;	(** this view is restored in empty filler views *)
			track: Views.Title;	(* optional track name, a control property *)
		END;
		
		Context* = POINTER TO EXTENSIBLE RECORD (GenericContext)
			filler-: View;	(* = NIL => context removed from filler *)
			view-: Views.View;
			title-: Views.Title;
			w, h: INTEGER
		END;
		
		Divider = POINTER TO RECORD (View)
			(** devide window to several areas *)
			isHorizontal: BOOLEAN
		END;
		
		Tile = POINTER TO RECORD (Context)
			x, 	(** left or top border of tile inside it's divider *)
			size, esize: UCs; (** size in pixels; effective size, used for calculations, ~permanent *)
			pSize: INTEGER;	(** size in percent ( fixed point, [0..1], the base is 32768) *)
		END;
		
		CheckDirtyAction = POINTER TO RECORD (Services.Action)
			stack: Stack
		END;
		
		Stack = POINTER TO RECORD (View)
			tb: Tabulator;
			newTitle: Views.Title;	(** parameter for Insert: title of newly inserted view *)
			dirtyChecker: CheckDirtyAction;
		END;
		
		Tab = POINTER TO RECORD (Context)
			closer: Closer; 
			titleView: TitleView
		END;
		
		TabVerb* = PROCEDURE (c: Context);
		
		Tabulator = POINTER TO RECORD (Views.View)
			(** ribbon of tabs, normally at the top of a Stack *)
			stack: Stack;
			dropMark, actL, actR, scrollShift: INTEGER; (* passed from Restore to RestoreMarks *)
		END;
		
		TabGeometry = RECORD
			(** encapsulates geometry of embedded objects *)
			stack: Stack;
			ind: INTEGER;
			closerWidth, titleWidth, closerLeft, titleLeft, width: INTEGER;
			closerSide: BOOLEAN
		END;
		
		Closer = POINTER TO RECORD (Views.View)
			(** show close button on a tab and closes the tab when clicked *)
			tab: Tab;
			afterTrack: BOOLEAN; (* reduce selection after canceling close dialog *)
		END;
		CloserCtx = POINTER TO RECORD (GenericContext)
			wid: INTEGER;
		END;
		
		TitleView = POINTER TO RECORD (Views.View)
			tab: Tab;
			dirty: BOOLEAN;
			wid, hei: INTEGER
		END;
		TitleCtx = POINTER TO RECORD (GenericContext)
			view: TitleView
		END;
		
		BackdropCtx = POINTER TO RECORD (GenericContext)
			filler: View
		END;
		
		Proposal* = RECORD (Models.Proposal)
			(** a proposal to remove view from View *)
			op*: INTEGER;
			title*: Views.Title
		END;
		
		DropTabPollMsg = RECORD (Controllers.TransferMessage)
			frame: Views.Frame;
			filler: View;
			ind: INTEGER;
		END;
		
		DropFeedbackMsg = RECORD (Controllers.Message)
			x, y, ind: INTEGER;
			show: BOOLEAN;
		END;
		(* DropFeedbackMsg is expected to be delivered directly to the receiver thru Views.ForwardCtrlMsg; this implies that containers do not forward it to focus views or views at certain locations *)
		
		ActivateMsg = RECORD (Views.Message)	(* used by Divider *)
			was, is: INTEGER;	(** active view changes from was to is *)
		END;
		
		FindFrameMsg = RECORD (Views.Message)
			frame: Views.Frame
		END;
		
		RemoveMsg* = RECORD (Controllers.Message)
			(** sent to a view before it is removed from a Divider or Stack. Receiver may have no frame!! *)
			processed*: BOOLEAN	(** callee => caller. Callee may ask to not be removed by resetting .ok. Preset to FALSE *)
		END;
		
		RefocusAction = POINTER TO RECORD (Services.Action)
			frame: Views.Frame;
			view: Divider
		END;
		
		WindowPref* = RECORD (Properties.Preference)
			win*: StdWindows.Window
		END;
		
		StackCreator = POINTER TO RECORD (Services.Action)
			clickTime: LONGINT;
			n: INTEGER;
			d: Divider
		END;
		
		
		Param* = POINTER TO RECORD 
			color*, colorFocus*,
			tabHeight*, minTabWidth*: INTEGER;
			bgColor*, (** background color *)
			gutterColor*, marksColor*, 
			closerColor*: Ports.Color;	(** color to draw close button in *)
			closerSide*: BOOLEAN; (** close button position (left or right) *)
			closerMark*: ARRAY 4 OF CHAR; (** (unicode) character for close button *)
			boldenFront*: BOOLEAN; (** make a tab's title boldface if it's on front *)
			selectedBgColor*: Ports.Color;	(** background of selected tab when it's on the front path *)
			nonselectedBgColor*: Ports.Color;	(** background of selected tab when it's on the front path *)
			strokeColor*, strokeColorShaded*: Ports.Color;
			markColor*: Ports.Color;
			margins*, gutterWidth*: INTEGER;
			debug*, markDirty*: BOOLEAN;
			navigationMethod*: INTEGER;
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;
		
		TrackMsg* = RECORD (Views.Message)
			name*: Views.Title;
			cnt*: INTEGER;
			track*: View;
			frame*: Views.Frame;	(* .frame.view = .track *)
		END;
		
		TrackPropMsg* = RECORD (Properties.Message)
			done*: BOOLEAN;	(* callee => caller *)
			poll*: BOOLEAN;	(* poll or set *)
			name*: Views.Title	(* the track name for the receiver *)
		END;

	VAR
		refocusAction: RefocusAction;
		param*: Param;
		globalStackCreator: StackCreator;
		hotBtn: Views.View;

	PROCEDURE^ GetActiveTabPosition (f: Views.Frame; st: Stack; OUT x0, x1: INTEGER): BOOLEAN;
	
	PROCEDURE Focus* (v: View): Context; 
		VAR c: Context;
	BEGIN
		ASSERT(v # NIL, 20); IF v.active # none THEN c := v.ctx[v.active] END; 
		RETURN c
	END Focus;
	
	PROCEDURE Len* (f: View): INTEGER;
		VAR len: INTEGER;
	BEGIN
		IF f.ctx = NIL THEN len := 0 ELSE len := LEN(f.ctx) END;
		RETURN len
	END Len;
	
	PROCEDURE Recalc (d: Divider; wid, hei, dot: UCs);
		VAR i, len, undef: INTEGER; x, defined, std: UCs; gross: LONGINT;
			ctx: Context; tile: Tile;
	BEGIN 
		IF d.isHorizontal THEN gross := wid ELSE gross := hei END;
		len := Len(d); DEC(gross, dot * MAX(0, len - 1)); defined := 0; undef := 0;
		i := 0;
		WHILE i < len DO
			tile := d.ctx[i](Tile); (* 1st pass:figure out desired e(ffective) size *)
			IF tile.size # 0 THEN
				tile.esize := tile.size 
			ELSIF tile.pSize # 0 THEN
				tile.esize := SHORT(gross * tile.pSize DIV fixedPointBase)
			ELSE
				tile.esize := 0; INC(undef)
			END;
			INC(defined, tile.esize);
			INC(i)
		END;
		IF undef > 0 THEN
			std := MAX(0, (SHORT(gross) - defined) DIV undef)
		ELSE
			std := 0
		END;
		i := 0; x := 0;
		WHILE i < len DO
			tile := d.ctx[i](Tile); (* 2nd pass: shrink tiles that don't fit *)
			tile.x := x;
			IF tile.esize = 0 THEN tile.esize := std END;
			tile.esize := MAX(0, MIN(SHORT(gross - x), tile.esize));
			ASSERT(tile.esize >= 0, 60);
			INC(x, tile.esize); INC(x, dot * param.gutterWidth);
			ctx := d.ctx[i]; 
			IF d.isHorizontal THEN
				ctx.w := tile.esize; ctx.h := hei
			ELSE
				ctx.w := wid; ctx.h := tile.esize
			END;
			INC(i)
		END
	END Recalc;
	
	PROCEDURE CheckTitleWidth (tv: TitleView; force: BOOLEAN);
		VAR ctx: TitleCtx; dummy: UCs; font: Fonts.Font;
	BEGIN
		ctx := tv.context(TitleCtx); 
		IF force OR (ctx.fnt # StdCFrames.defaultFont) THEN
			ctx.fnt := StdCFrames.defaultFont;
			
			IF param.boldenFront THEN
				font := Fonts.dir.This(ctx.fnt.typeface, ctx.fnt.size, ctx.fnt.style, Fonts.bold);
				font.GetBounds(ctx.asc, ctx.dsc, dummy);
				tv.wid := font.StringWidth(tv.tab.title) + param.margins * 3
			ELSE
				ctx.fnt.GetBounds(ctx.asc, ctx.dsc, dummy);
				tv.wid := ctx.fnt.StringWidth(tv.tab.title) + param.margins * 3
			END;
			
			IF param.closerSide = left THEN 
				INC(tv.wid, param.margins);
			END;
			IF tv.wid < param.minTabWidth THEN
				tv.wid := param.minTabWidth
			END;
			INC(tv.wid, mm);
			tv.hei :=  param.tabHeight - param.margins  - Ports.point * 2;
		END
	END CheckTitleWidth;
	
	PROCEDURE InitGenericCtx (c: GenericContext);
		VAR dummy: INTEGER;
	BEGIN
		ASSERT(c # NIL, 20);
		c.fnt := StdCFrames.defaultFont;
		c.fnt.GetBounds(c.asc, c.dsc, dummy)
	END InitGenericCtx;
	
	PROCEDURE NewFillerCtx (f: View): Context;
		VAR c: Context;
	BEGIN NEW(c); InitGenericCtx(c); c.filler := f; c.w := magicWidth; RETURN c
	END NewFillerCtx;
	
	PROCEDURE ThisInd (c: Context): INTEGER;
		VAR i, len: INTEGER; ctx: POINTER TO ARRAY OF Context;
	BEGIN
		i:= 0; ctx := c.filler.ctx; len := LEN(ctx);
		WHILE (i < len) & (ctx[i] # c) DO INC(i) END;
		ASSERT(i < len, 20	(* c expected to be part of c.filler.ctx *) );
		RETURN i
	END ThisInd;
	
	PROCEDURE (f: View) Activate (ind: INTEGER), NEW, EXTENSIBLE;
		VAR msg: ActivateMsg;
	BEGIN
		IF f.active # ind THEN 
			msg.was := f.active; msg.is := ind; f.active := ind; Views.Broadcast(f, msg);
			Views.Update(f, Views.keepFrames);
		END
	END Activate;
	
	PROCEDURE (f: View) Remove (ind: INTEGER), NEW, EXTENSIBLE;
		VAR new, j, k, len: INTEGER; ctx, copy: POINTER TO ARRAY OF Context;
	BEGIN
		ASSERT(f.ctx # NIL, 20); ctx := f.ctx;
		len := LEN(ctx); ASSERT((0 <= ind) & (ind < len), 21 (* w is not in f *) );
		IF ind = f.active THEN
			IF f.active < len - 1 THEN
				new := f.active + 1
			ELSE
				new := f.active - 1
			END;
			f.Activate(new);
			IF new > ind THEN f.active := ind END
		ELSIF ind < f.active THEN DEC(f.active)
		END;
		f.ctx[ind].filler := NIL;
		IF len > 1 THEN
			NEW(copy, len - 1);
			j := 0; k := 0; 
			WHILE j < len DO 
				IF j # ind THEN copy[k] := ctx[j]; INC(k) END;
				INC(j) 
			END;
			f.ctx := copy
		ELSE ASSERT(ind = 0, 60); f.ctx := NIL
		END;
		Views.Update(f, Views.rebuildFrames);
		ASSERT(f.active < Len(f), 60)
	END Remove;
	
	PROCEDURE Remove*  (c: Context);
	BEGIN ASSERT(c # NIL, 20); c.filler.Remove(ThisInd(c))
	END Remove;
	
	PROCEDURE SetTitle (f: View; ind: INTEGER; IN title: ARRAY OF CHAR);
	BEGIN ASSERT((0 <= ind) & (ind < Len(f)), 20); 
		f.ctx[ind].title := title$;
		WITH f: Stack DO CheckTitleWidth(f.ctx[ind](Tab).titleView, TRUE); 
			Views.Update(f.tb, Views.keepFrames)
		ELSE
		END
	END SetTitle;

	PROCEDURE (f: View) Insert (view: Views.View; pos: INTEGER), NEW, EMPTY;

	PROCEDURE Insert (f: View; view: Views.View; pos: INTEGER; IN title: ARRAY OF CHAR; 
		context: Context
	);
		VAR j, k, len: INTEGER; ctx, copy: POINTER TO ARRAY OF Context;
	BEGIN
		ASSERT(f # NIL, 20); ASSERT(context # NIL, 21);
		ASSERT(0 <= pos, 22); ASSERT(pos <= Len(f), 23);
		IF view # NIL THEN
			context.view := view; view.InitContext(context); Stores.Join(f, view)
		END;
		InitGenericCtx(context); context.filler := f; context.w := magicWidth;
		ctx := f.ctx; len := Len(f);
		ASSERT(pos <= len, 20);
		NEW(copy, len + 1);
		j := 0; k := 0; WHILE k <= len DO 
			IF k = pos THEN copy[k] := context ELSE copy[k] := ctx[j]; INC(j) END; 
			INC(k)
		END;
		f.ctx := copy;
		SetTitle(f, pos, title);
		IF pos <= f.active THEN INC(f.active) 
		ELSIF f.active = none THEN f.active := pos
		END
	END Insert;
	
	PROCEDURE (d: Divider) Insert (view: Views.View; pos: INTEGER);
		VAR tile: Tile;
	BEGIN
		ASSERT(0 <= pos, 20); ASSERT(pos <= Len(d), 21);
		NEW(tile); tile.pSize := 0; tile.size := 0; tile.esize := 0;
		IF pos = 0 THEN
			tile.x := 0
		ELSE
			tile.x := d.ctx[pos-1](Tile).x + d.ctx[pos-1](Tile).esize
		END;
		Insert(d, view, pos, '', tile)
	END Insert;
		
	PROCEDURE CheckCloserWidth (c: Closer): INTEGER;
		VAR ctx: CloserCtx;
	BEGIN
		ctx := c.context(CloserCtx);
		IF param.closerMark = "" THEN
			ctx.wid := 11 * Ports.point;
		ELSE
			ctx.wid := ctx.fnt.StringWidth(param.closerMark)
		END;
		RETURN ctx.wid
	END CheckCloserWidth;
	
	PROCEDURE (st: Stack) Insert (view: Views.View; pos: INTEGER);
		VAR tab: Tab; clCtx: CloserCtx; tCtx: TitleCtx; tv: TitleView; r_: INTEGER;
	BEGIN
		ASSERT(0 <= pos, 20); ASSERT(pos <= Len(st), 21);
		NEW(tab);
		NEW(tab.closer); NEW(clCtx); InitGenericCtx(clCtx);
		tab.closer.InitContext(clCtx); Stores.Join(tab.closer, st);
		NEW(tv); tv.dirty := FALSE;
		tab.titleView := tv; tv.tab := tab; NEW(tCtx); tCtx.view := tv;
		InitGenericCtx(tCtx); tv.InitContext(tCtx); Stores.Join(tv, st);
		tab.closer.tab := tab;
		tab.closer.afterTrack := FALSE;
		clCtx.wid := 0; 
		Insert(st, view, pos, st.newTitle, tab);
		r_ := CheckCloserWidth(tab.closer);
		st.newTitle := ''
	END Insert;
	
	PROCEDURE (f: View) Append (view: Views.View), NEW, EXTENSIBLE;
	BEGIN f.Insert(view, Len(f))
	END Append;
	
	PROCEDURE InitFiller (f: View);
	BEGIN ASSERT(f # NIL, 20); f.ctx := NIL; f.active := none
	END InitFiller;
	
	PROCEDURE AppendDivTile* (f: View; view: Views.View; size: UCs; pSize: REAL);
		VAR i: INTEGER; tile: Tile; d: Divider; 
	BEGIN
		ASSERT(f IS Divider, 20); d := f(Divider);
		d.Append(view); i := Len(d) - 1;
		tile := d.ctx[i](Tile); tile.size := size;
		IF pSize > 0 THEN
			tile.pSize := SHORT(ENTIER(pSize * fixedPointBase))
		ELSE
			tile.pSize := 0
		END;
		Views.Update(d, Views.rebuildFrames)
	END AppendDivTile;
	
	PROCEDURE AppendStackTab* (f: View; view: Views.View; IN title: Views.Title);
		VAR st: Stack;
	BEGIN ASSERT((f # NIL) & (f IS Stack), 20); st := f(Stack);
		IF view # NIL THEN ASSERT(view.context = NIL) END;
		st.newTitle := title$;
		st.Append(view);
		ASSERT(st.ctx[LEN(st.ctx) - 1].view = view, 60);
		Views.Update(st, Views.rebuildFrames)
	END AppendStackTab;
	
	PROCEDURE NewDivider* (vertical: BOOLEAN): Divider;
		VAR d: Divider;
	BEGIN
		NEW(d);
		d.kind := divider;
		InitFiller(d);
		d.isHorizontal := vertical;
		RETURN d
	END NewDivider;
		
	PROCEDURE Activate* (c: Context);
	BEGIN ASSERT(c # NIL, 20); ASSERT(c.filler # NIL, 21); c.filler.Activate(ThisInd(c))
	END Activate;
	
	PROCEDURE HandleViewMsg (v: View; f: Views.Frame; VAR msg: Views.Message);
	(** Handle view messages common to any View *)
	BEGIN
		WITH msg: TrackMsg DO 
			IF msg.name = v.track THEN
				INC(msg.cnt);
				IF msg.cnt = 1 THEN msg.track := v; msg.frame := f END
			END
		ELSE
		END
	END HandleViewMsg;
	
	PROCEDURE (d: Divider) HandleViewMsg- (f: Views.Frame; VAR msg: Views.Message);
		CONST focusChange = TRUE;
		VAR mark: Controllers.MarkMsg; g: Views.Frame; 
	BEGIN
		HandleViewMsg(d, f, msg);
		WITH msg: ActivateMsg DO
			mark.focus := focusChange; 
			IF msg.was # none THEN
				g := Views.ThisFrame(f, d.ctx[msg.was].view); 
				IF g # NIL THEN	
					mark.show := Controllers.hide;
					Views.ForwardCtrlMsg(g, mark)
				END
			END;
			IF msg.is # none THEN
				g := Views.ThisFrame(f, d.ctx[msg.is].view);
				IF g # NIL THEN
					mark.show := Controllers.show;
					Views.ForwardCtrlMsg(g, mark)
				END
			END
		| msg: FindFrameMsg DO
			ASSERT(msg.frame = NIL, 126 (* divider in multiple frames NIY*));
			msg.frame := f
		ELSE
		END
	END HandleViewMsg;
	
	PROCEDURE InstallActiveFrame (st: Stack; f: Views.Frame; wid, hei: INTEGER);
	(** Install into f a frame for st's active view, assuming st's  wid and hei *)
		VAR c: Context;
	BEGIN
		IF st.active # none THEN c := st.ctx[st.active]; 
			IF c.view # NIL THEN
				c.w := wid;
				c.h := hei - param.tabHeight;
				Views.InstallFrame(f, c.view, 0, param.tabHeight, 0, TRUE)
			END
		END
	END InstallActiveFrame;
	
	PROCEDURE (st: Stack) HandleViewMsg- (f: Views.Frame; VAR msg: Views.Message);
		VAR wid, hei: INTEGER; g: Views.Frame; mark: Controllers.MarkMsg;
		
		PROCEDURE MakeActiveVisible;
		VAR
			width, height: INTEGER;
			x0, x1: INTEGER;
		BEGIN
			st.tb.context.GetSize(width, height);
			IF (width > Ports.mm) & GetActiveTabPosition(f, st, x0, x1) THEN
				(* k8: check of current tab is visible *)
				INC(x0, st.tb.scrollShift + 2 * mm);
				INC(x1, st.tb.scrollShift + 2 * mm);
				IF x0 < 4 * mm THEN
					DEC(st.tb.scrollShift, x0 - 4 * mm)
				ELSIF x1 + 6 * mm > width THEN
					INC(st.tb.scrollShift, width - x1 - 6 * mm)
				END;
				st.tb.scrollShift := MIN(st.tb.scrollShift, 0); (* left limit *)
				(* k8: FIXME: better right limit processing granularity! *)
				(*
				st.tb.scrollShift := MAX(st.tb.scrollShift, -(totalW - width + Ports.mm * 4)); (* right limit *)
				*)
			END
		END MakeActiveVisible;
		
	BEGIN 
		HandleViewMsg(st, f, msg);
		WITH msg: FindFrameMsg DO
			msg.frame := f
		| msg: ActivateMsg DO 
			(* Cannot call Views.Update from here, so do frame work manually *)
			ASSERT((none <= msg.was) & (msg.was < LEN(st.ctx)), 20);
			ASSERT((none <= msg.is) & (msg.is < LEN(st.ctx)), 21);
			ASSERT(msg.is = st.active, 22);
			IF msg.was # none THEN
				g := Views.ThisFrame(f, st.ctx[msg.was].view);
				IF g # NIL THEN Views.RemoveFrame(f, g) END
			END;
			IF msg.is # none THEN
				st.context.GetSize(wid, hei);
				InstallActiveFrame(st, f, wid, hei);
				MakeActiveVisible;
				g := Views.ThisFrame(f, st.ctx[st.active].view);
				IF g # NIL THEN 
					mark.focus := TRUE; mark.show := Controllers.show; 
					Views.ForwardCtrlMsg(g, mark)
				END
			END
		ELSE 
		END
	END HandleViewMsg;
	
	PROCEDURE ContextAt (d: Divider; x, y: INTEGER): Context;
		VAR len, i: INTEGER; ctx: POINTER TO ARRAY OF Context; res: Context;
	BEGIN
		ctx := d.ctx; len := Len(d) - 1; i := -1;
		IF d.isHorizontal THEN
			WHILE (x > 0) & (i < len) DO INC(i); DEC(x, ctx[i].w) END;
		ELSE
			WHILE (y > 0) & (i < len) DO INC(i); DEC(y, ctx[i].h) END;
		END;
		IF (0 <= i) & (i < len) THEN res := ctx[i] END;
		RETURN res
	END ContextAt;
	
	PROCEDURE HandlePropMsg (v: View; VAR msg: Properties.Message);
	BEGIN
		WITH msg: TrackPropMsg DO msg.done := TRUE;
			IF msg.poll THEN msg.name := v.track$ ELSE v.track := msg.name$ END 
		ELSE 
		END
	END HandlePropMsg;

	PROCEDURE (d: Divider) HandlePropMsg (VAR msg: Properties.Message);
		VAR i, len: INTEGER; ctx: Context;
	BEGIN
		HandlePropMsg(d, msg);
		WITH msg: Properties.FocusPref DO 
			IF msg.atLocation THEN ctx := ContextAt(d, msg.x, msg.y)
			ELSIF d.active # none THEN ctx := d.ctx[d.active]
			END;
			IF ctx # NIL THEN Views.HandlePropMsg(ctx.view, msg) END
		ELSE
		END
	END HandlePropMsg;
	
	PROCEDURE (s: Stack) HandlePropMsg (VAR msg: Properties.Message);
		VAR w, h: INTEGER;
	BEGIN
		HandlePropMsg(s, msg);
		WITH msg: Properties.FocusPref DO msg.setFocus := Len(s) > 0
		ELSE
		END
	END HandlePropMsg;
	
	PROCEDURE DoTransfer (fromV: View; from: INTEGER; toV: View; VAR to: INTEGER; toF: Views.Frame);
	(** Upon return, to may be adjusted *)
		VAR copy: Views.View; c: Context; ctx: POINTER TO ARRAY OF Context; i: INTEGER; focus: Context;
	BEGIN
		ASSERT(from < LEN(fromV.ctx), 21);
		IF toV.ctx = NIL THEN ASSERT(to = 0, 22) ELSE ASSERT(to <= LEN(toV.ctx), 23) END;
		IF (fromV = toV) & (from = to) THEN	(* nothing *)
		ELSIF fromV = toV THEN		(* just reorder contexts *)
			IF from # to THEN
				focus := fromV.ctx[fromV.active];
				c := fromV.ctx[from]; ctx := toV.ctx;
				IF to < from THEN i := from; WHILE i > to DO ctx[i] := ctx[i - 1]; DEC(i) END
				ELSIF from < to THEN DEC(to); i := from; WHILE i < to DO ctx[i] := ctx[i + 1]; INC(i) END
				END;
				ctx[to] := c;
				Activate(focus)
			END
		ELSE
			IF fromV.transferCopy # NIL THEN copy := fromV.transferCopy(fromV.ctx[from].view, toV, toF)
			ELSE copy := Views.CopyOf(fromV.ctx[from].view, Views.shallow)
			END;
			WITH toV: Stack DO toV.newTitle := fromV.ctx[from].title$ ELSE END;
			fromV.Remove(from); 
			toV.Insert(copy, to)
		END;
		Views.Update(fromV, Views.rebuildFrames);
		IF fromV # toV THEN Views.Update(toV, Views.rebuildFrames) END
	END DoTransfer;
	
	PROCEDURE Transfer* (what: Views.View; where: View; pos: INTEGER);
	(** Request to transfer what (embedded into some View) into where at position pos. pos = append => at the end *)
		VAR msg: FindFrameMsg; from, len: INTEGER; c: Models.Context; fromV: View;
	BEGIN ASSERT(what # NIL, 20); ASSERT(where # NIL, 21); ASSERT(pos >= append, 22);
		c := what.context;
		WITH c: Context DO
			fromV := c.filler; from := 0;
			WHILE fromV.ctx[from] # c DO INC(from) END;
			len := Len(where); ASSERT(pos <= len, 23);
			IF pos = append THEN pos := len END;
			Views.Broadcast(where, msg);
			IF msg.frame # NIL THEN DoTransfer(fromV, from, where, pos, msg.frame) END
		ELSE HALT(24)
		END
	END Transfer;
	
	(* Persistency *)
	
	PROCEDURE (s: Stack) Externalize (VAR wr: Stores.Writer);
		VAR i, len: INTEGER; tab: Context;
	BEGIN
		wr.WriteVersion(version);
		len := Len(s); wr.WriteString(s.track); wr.WriteInt(len);
		i := 0;
		WHILE i < len DO
			tab := s.ctx[i];
			wr.WriteString(tab.title);
			Views.WriteView(wr, tab.view);
			INC(i)
		END
	END Externalize;
	
	PROCEDURE InitStack (st: Stack);
	BEGIN
		st.kind := stack; InitFiller(st);
		NEW(st.tb);
		st.tb.stack := st; st.tb.dropMark := -1; 
		st.tb.scrollShift := 0;
		st.tb.InitContext(NewFillerCtx(NIL)); Stores.Join(st.tb, st);
		IF param.markDirty THEN
			NEW(st.dirtyChecker);
			st.dirtyChecker.stack := st;
			Services.DoLater(st.dirtyChecker, Services.now);
		END;
	END InitStack;
	
	PROCEDURE (s: Stack) Internalize (VAR rd: Stores.Reader); 
		VAR i, ver, len: INTEGER; title: Views.Title; v: Views.View;
	BEGIN
		rd.ReadVersion(version, version, ver);
		IF ~rd.cancelled THEN
			InitStack(s); 
			rd.ReadString(s.track); rd.ReadInt(len);
			WHILE len > 0 DO DEC(len); 
				rd.ReadString(title);
				Views.ReadView(rd, v);
				IF v # NIL THEN AppendStackTab(s, v, title) END
			END
		END
	END Internalize;
	
	PROCEDURE (d: Divider) Externalize (VAR wr: Stores.Writer);
		VAR i, len: INTEGER; tile: Tile; 
	BEGIN
		wr.WriteVersion(version);
		len := Len(d);
		wr.WriteString(d.track);
		wr.WriteBool(d.isHorizontal);
		wr.WriteInt(len);
		i := 0;
		WHILE i < len DO
			tile := d.ctx[i](Tile);
			wr.WriteString(tile.title);
			wr.WriteInt(tile.size);
			wr.WriteInt(tile.pSize);
			Views.WriteView(wr, tile.view);
			INC(i)
		END
	END Externalize;
	
	PROCEDURE (d: Divider) Internalize (VAR rd: Stores.Reader); 
		VAR size, pSize, ver, len: INTEGER; v: Views.View; title_: Views.Title; 
	BEGIN
		rd.ReadVersion(version, version, ver);
		IF ~rd.cancelled THEN
			d.kind := divider; InitFiller(d);
			rd.ReadString(d.track);
			rd.ReadBool(d.isHorizontal); 
			rd.ReadInt(len);
			WHILE len > 0 DO DEC(len);
				rd.ReadString(title_);
				rd.ReadInt(size);
				rd.ReadInt(pSize); 
				Views.ReadView(rd, v);
				IF v # NIL THEN AppendDivTile(d, v, size, pSize) END;
			END
			;len := Len(d); WHILE len > 0 DO DEC(len); ASSERT(d.ctx[len] = d.ctx[len].view.context) END
		END
	END Internalize;
	
	PROCEDURE (d: Divider) CopyFromSimpleView (src: Views.View);
		VAR len: INTEGER; tile, style: Tile;
	BEGIN 
		(* this is temporary! Nested views are embeded directly into the divider, which is a no-go. TO make a valid copy of the Divider, you'd need shallow copies of all nested views. But this is just for a persistency experiment, the copy is made, externalized, then disposed. So, it shall go. Otherwise, you'd need actual shallow copies of embedded view, or, alternatively and more BB-way - the Divider should be a model view, and views shall be embedded into a model rather than directly the Divivder. *)
		WITH src: Divider DO 
			d.kind := divider;
			d.ctx := src.ctx;
			d.active := src.active; d.canDrop := src.canDrop; d.transferCopy := src.transferCopy; 
			d.backdrop := src.backdrop; d.track := src.track$;
			d.isHorizontal := src.isHorizontal
		END
		(* debug
		; IF param.debug THEN Con.String("Divider.CopyFromSimpleView"); Con.Ln; END;
		*)
	END CopyFromSimpleView;
	
	(* Stack and Divider *)
	
	PROCEDURE (bc: BackdropCtx) GetSize (OUT w, h: INTEGER);
	BEGIN 
		IF (bc.filler # NIL) & (bc.filler.context # NIL) THEN
			bc.filler.context.GetSize(w, h);
			DEC(h, param.tabHeight)
		END
	END GetSize;

	PROCEDURE (st: Stack) Restore* (f: Views.Frame; l, t, r, b: INTEGER);
		VAR c: Context; wid, hei: UCs; bc: BackdropCtx;
	BEGIN
		st.context.GetSize(wid, hei);
		IF st.ctx # NIL THEN
			c := st.tb.context(Context);
			c.w := wid;
			c.h := param.tabHeight;
			Views.InstallFrame(f, st.tb, 0, 0, 0, FALSE);
			InstallActiveFrame(st, f, wid, hei)
		END;
		IF (st.active = none) & (st.backdrop # NIL) THEN 
			IF st.backdrop.context = NIL THEN NEW(bc); bc.filler := st;
				st.backdrop.InitContext(bc); Stores.Join(st, st.backdrop);
			END;
			ASSERT(Stores.Joined(st, st.backdrop), 20);
			Views.InstallFrame(f, st.backdrop, 0, param.tabHeight, 0, FALSE)
		END
	END Restore;
	
	PROCEDURE StackEditOp (_: Views.Frame; st: Stack; IN msg: Controllers.EditMsg; 
		VAR focus: Views.View
	);
		VAR to: INTEGER;
	BEGIN
		IF (msg.op = Controllers.pasteChar) & (Len(st) > 0) THEN
			to := st.active;
			CASE param.navigationMethod OF
			| navMethodAltPg:
				IF Controllers.pick IN msg.modifiers THEN
					(* Alt *)
					CASE msg.char OF
					| PageUp: DEC(to)
					| PageDown: INC(to)
					ELSE END
				END
			| navMethodCtrlAltArr:
				IF msg.modifiers*{Controllers.pick,Controllers.modify} = {Controllers.pick,Controllers.modify} THEN
					(* Ctrl+Alt *)
					CASE msg.char OF
					| LeftArrow: DEC(to)
					| RightArrow: INC(to)
					ELSE END
				END
			ELSE (* default *)
				IF msg.modifiers*{0..7} = {} THEN
					(*
						Ctrl+PageUp=PageLeft
						Ctrl+PageDown=PageRight
					*)
					CASE msg.char OF
					| PageLeft: DEC(to)
					| PageRight: INC(to)
					ELSE END
				END
			END;
			IF to # st.active THEN focus := NIL END;
			to := MAX(0, MIN(to, Len(st) - 1));
			IF to # st.active THEN st.Activate(to) END
		END
	END StackEditOp;

	PROCEDURE (st: Stack) HandleCtrlMsg* (f: Views.Frame; VAR msg: Views.CtrlMessage;
		VAR focus: Views.View
	);
		VAR g: Views.Frame; ctx: GenericContext; wid, hei, asc, dsc, dummy: INTEGER; fnt: Fonts.Font;
	BEGIN
		WITH msg: DropFeedbackMsg DO
			IF msg.show THEN st.context.GetSize(wid, hei);
				IF st.context IS GenericContext THEN
					ctx := st.context(GenericContext); 
					fnt := ctx.fnt;
					fnt := Fonts.dir.This(fnt.typeface, 10 * mm, fnt.style, Fonts.bold)
				ELSE
					fnt := Fonts.dir.This(StdCFrames.defaultFont.typeface, 10 * mm, fnt.style, Fonts.bold)
				END;
				fnt.GetBounds(asc, dsc, dummy);
				f.DrawString((wid - fnt.StringWidth("*")) DIV 2, (hei - asc - dsc) DIV 2 + asc, param.marksColor, "*", fnt)
			ELSE
				Views.Update(st, Views.keepFrames); Views.ValidateRoot(Views.RootOf(f))
			END
		ELSE
			WITH msg: Controllers.CursorMessage DO
				g := Views.FrameAt(f, msg.x, msg.y)
			ELSE END;
			IF g # NIL THEN focus := g.view
			ELSIF st.active # none THEN focus := st.ctx[st.active].view 
			ELSE focus := NIL
			END;
			WITH msg: Controllers.EditMsg DO StackEditOp(f, st, msg, focus) ELSE END;
			IF focus = NIL THEN
				WITH msg: DropTabPollMsg DO 
					IF st.active = none THEN msg.ind := 0 ELSE msg.ind := st.active END;
					msg.frame := f; msg.filler := st
				ELSE
				END
			END
		END
	END HandleCtrlMsg;
	
	PROCEDURE (d: Divider) GetBackground* (VAR color: Ports.Color);
	BEGIN
		color := param.bgColor
	END GetBackground;
	
	PROCEDURE (d: Divider) Restore* (f: Views.Frame; l, t, r, b: UCs);
		VAR wid, hei, x, y: UCs; i, len: INTEGER; tile: Tile;
	BEGIN
		d.context.GetSize(wid, hei);
		Recalc(d, wid, hei, f.dot);
		len := Len(d);
		IF d.isHorizontal THEN y := 0 ELSE x := 0 END;
		i := 0;
		WHILE i < len DO
			tile := d.ctx[i](Tile);
			IF d.isHorizontal THEN x := tile.x ELSE y := tile.x END;
			IF i > 0 THEN	(* draw gutter *)
				IF d.isHorizontal THEN
					f.DrawRect(x - f.dot * param.gutterWidth, t, x, b, Ports.fill, param.gutterColor)	
				ELSE
					f.DrawRect(l, y - f.dot * param.gutterWidth, r, y, Ports.fill, param.gutterColor)
				END
			END;
			IF tile.view # NIL THEN
				Views.InstallFrame(f, tile.view, x, y, 0, i = d.active)
			END;
			INC(i)
		END
	END Restore;
	
	PROCEDURE (d: Divider) RestoreMarks* (f: Views.Frame; l, t, r, b: INTEGER);
		VAR col: INTEGER; tile: Tile; 
	BEGIN 
		IF param.debug THEN
			IF d.active # none THEN
				tile := d.ctx[d.active](Tile);
				IF d.isHorizontal THEN
					t := 0; l := tile.x; r := l + tile.w; b := tile.h;
					col := Ports.red + Ports.blue
				ELSE
					l := 0; r := tile.w; t := tile.x; b := t + tile.h;
					col := Ports.red + Ports.green
				END
			ELSE l := 0; t := 0; d.context.GetSize(r, b)
			END;
			f.DrawRect(l, t, r, b, 0, col);
			f.DrawLine(l, t, r, b, 0, col)
		END
	END RestoreMarks;
	
	
	PROCEDURE ThisIndex (d: Divider; x, y: UCs): INTEGER;
		VAR i, len, ind: INTEGER; u: UCs; ctx: POINTER TO ARRAY OF Context;
	BEGIN
		ctx := d.ctx; len := Len(d);
		IF d.isHorizontal THEN u := x ELSE u := y END;
		i := 0; WHILE (i < len) & ((ctx[i](Tile).x + ctx[i](Tile).esize) <= u) DO INC(i) END;
		IF (i < len) THEN
			IF u < ctx[i](Tile).x THEN ind := -i - 1 (* falls in the gap *) ELSE ind := i END
		ELSE ind := -1
		END;
		RETURN ind;
	END ThisIndex;
	
	PROCEDURE InResizer (d: Divider; x, y: UCs; OUT ind: INTEGER): BOOLEAN;
		VAR u: UCs; res: BOOLEAN;
	BEGIN
		ind := ThisIndex(d, x, y);
		IF d.isHorizontal THEN u := x ELSE u := y END;
		IF ind < -1 THEN ind := -ind - 2; res := TRUE 
		ELSIF (ind >= 0) & (ind < Len(d) - 1) THEN 
			IF d.isHorizontal THEN u := x ELSE u := y END;
			res := ((d.ctx[ind + 1](Tile).x - u) < Ports.point * 30 DIV 10)
		ELSE res := FALSE END;
		RETURN res
	END InResizer;

	PROCEDURE CursorAt (f: Views.Frame; x, y: UCs): INTEGER;
		VAR cur, dummy: INTEGER; d: Divider;
	BEGIN
		ASSERT(f.view IS Divider, 20);
		d := f.view(Divider);
		IF InResizer(d, x, y, dummy) THEN
			IF d.isHorizontal THEN cur := resizeHCursor ELSE cur := resizeVCursor END
		ELSE cur := Ports.arrowCursor
		END;
		RETURN cur
	END CursorAt;
	
	PROCEDURE TrackResize (f: Views.Frame; xo, yo: UCs);
		VAR min, max, wid, hei: UCs;
			isDown: BOOLEAN; mod: SET; 
			d: Divider;	(* divider in which the resizing should occur *)
			ind: INTEGER;	(* index of tile which should be resized *)
			ind2: INTEGER;	(* index of the adjacent tile which should be resized; IF tracking started on a gutter's boundary, THEN ind + 2 = ind, ELSE ind + 1 = ind END *)
			t1, t2: Tile;	(* Tiles that should be resized *)
			gross: Pixels; (* size of the divider *)
			esize: UCs;	 (* new esize of the view being recalculated *)
			together: UCs;	(* size of the two affected divisions; invariant!! *)
			ox, oy: UCs;	(* old x, old y - pixel coords that have been previousely used in recalc *)
			x, y: UCs;	(* current tracking coordinates *)
			gs: UCs;	(* gutter size *)
			ticks: LONGINT;
			ffm: FindFrameMsg;
	BEGIN
		d := f.view(Divider);
		d.context.GetSize(wid, hei); Recalc(d, wid, hei, f.dot);
		(* seek the tiler whose resizer was clicked *)
		IF InResizer(d, xo, yo, ind) THEN
			gs := f.dot * param.gutterWidth ; ind2 := ind + 1;
			t1 := d.ctx[ind](Tile); t2 := d.ctx[ind2](Tile);
			together := t1.esize + t2.esize;
			min := t1.x + mm; max := t2.x + t2.esize;
			IF d.isHorizontal THEN gross := wid ELSE gross := hei END; 
			x := xo; y := yo; ox := x; oy := y;
			ticks := Services.Ticks();
			REPEAT 
				IF (x < min) OR (x >= max) THEN x := ox END;
				IF (y < min) OR (y >= max) THEN y := oy END;
				esize := -1;
				IF d.isHorizontal & (x # ox) THEN esize := x - t1.x - gs
				ELSIF ~d.isHorizontal & (y # oy) THEN esize := y - t1.x - gs
				END;
				IF esize # -1 THEN 
					IF t1.pSize # 0 THEN t1.pSize := SHORT(LONG(esize) * fixedPointBase DIV gross)
					ELSE t1.size := esize 
					END;
					t1.esize := esize;
					(* now recalculate the next (to the right or to the bottom) divider section *)
					esize := together - esize;
					IF t2.size # 0 THEN
						t2.size := esize
					ELSIF t2.pSize # 0 THEN
						t2.pSize := SHORT(LONG(esize) * fixedPointBase DIV gross)
					END;
					t2.esize := esize;
					ASSERT(together = t1.esize + t2.esize);
					IF Services.Ticks() - ticks > 20 THEN
						Views.Update(f.view, Views.keepFrames);
						Views.ValidateRoot(Views.RootOf(f));
						IF f.rider = NIL (* f closed while validating *) THEN 
							Views.Broadcast(d, ffm); f := ffm.frame 
						END;
						ticks := Services.Ticks()
					END
				END;
				ox := x; oy := y;
				IF f # NIL THEN ASSERT(f.rider # NIL); f.Input(x, y, mod, isDown) END
			UNTIL (f = NIL) OR ~isDown;
			Views.Update(f.view, Views.keepFrames);
			Views.ValidateRoot(Views.RootOf(f));
		END
	END TrackResize;
	
	PROCEDURE ClaimFocus (v: Views.View): BOOLEAN;
		VAR p: Properties.FocusPref;				(* adapted from Containers *)
	BEGIN
		p.atLocation := FALSE;
		p.hotFocus := FALSE; p.setFocus := FALSE;
		Views.HandlePropMsg(v, p);
		RETURN p.setFocus
	END ClaimFocus;
	
	PROCEDURE ClaimFocusAt (v: Views.View; f, g: Views.Frame; x, y: INTEGER): BOOLEAN;
		VAR p: Properties.FocusPref;			(* adapted from Containers *)
	BEGIN
		p.atLocation := TRUE; p.x := x + f.gx - g.gx; p.y := y + f.gy - g.gy;
		p.hotFocus := FALSE; p.setFocus := FALSE;
		Views.HandlePropMsg(v, p);
		RETURN p.setFocus & ~p.hotFocus
	END ClaimFocusAt;

	PROCEDURE SetFocus (d: Divider; focus: Views.View);
		VAR i, len: INTEGER;
	BEGIN
		i := 0; len := Len(d); WHILE (i < len) & (d.ctx[i].view # focus) DO INC(i) END;
		IF i < len THEN
			d.Activate(i)
		END
	END SetFocus;
	
	PROCEDURE ThisTile (d: Divider; x, y: UCs): INTEGER;
		VAR ind: INTEGER;
	BEGIN
		ind := ThisIndex(d, x, y);
		IF ind < -1 THEN ind := -ind - 2 END;
		RETURN ind
	END ThisTile;
	
	PROCEDURE ShowDropMark (f: Views.Frame; d: Divider; VAR msg: DropFeedbackMsg);
		VAR ctx: GenericContext; wid, hei, asc, dsc, x, y, dummy: INTEGER; fnt: Fonts.Font; 
			ch: ARRAY 2 OF CHAR; tile: Tile;
	BEGIN
		ASSERT(msg.ind >= 0, 20);
		IF msg.show THEN d.context.GetSize(wid, hei);
			IF d.ctx = NIL THEN x := 0; y := 0
			ELSE
				IF msg.ind > 0 THEN DEC(msg.ind) END;
				tile := d.ctx[msg.ind](Tile);
				IF d.isHorizontal THEN
					x := tile.x; wid := tile.esize; y := 0
				ELSE
					x := 0; y := tile.x; hei := tile.esize
				END
			END;
			IF d.context IS GenericContext THEN
				ctx := d.context(GenericContext); 
				fnt := ctx.fnt;
				fnt := Fonts.dir.This(fnt.typeface, 10 * mm, fnt.style, Fonts.bold)
			ELSE
				fnt := StdCFrames.defaultFont;
				fnt := Fonts.dir.This(fnt.typeface, fnt.size, fnt.style, Fonts.bold)
			END;
			fnt.GetBounds(asc, dsc, dummy); 
			IF d.isHorizontal THEN
				ch := '▶' (*→*);
				INC(x, wid - fnt.StringWidth(ch) - mm);
				y := (hei - asc - dsc) DIV 2
			ELSE
				ch := '↓' (*▼*);
				x := (wid - fnt.StringWidth(ch)) DIV 2;
				INC(y, hei - dsc - mm)
			END;
			f.DrawString(x, y, param.marksColor, ch, fnt)
		ELSE
			Views.Update(d, Views.keepFrames); Views.ValidateRoot(Views.RootOf(f))
		END
	END ShowDropMark;
	
	PROCEDURE CheckMaskFocus (f: Views.Frame; d: Divider; VAR focus: Views.View);
		VAR i, len: INTEGER;
			msg: Controllers.MarkMsg; g: Views.Frame;
	BEGIN
		IF (*f.mark &*) ((focus = NIL) OR ~ClaimFocus(focus)) THEN
			i := 0; len := Len(d);
			WHILE (i < len) & (d.ctx[i].view # NIL) & ~ClaimFocus(d.ctx[i].view) DO
				INC(i)
			END;
			IF i < len THEN d.Activate(i) END;
			focus := NIL
		ELSIF focus # NIL THEN
			g := Views.ThisFrame(f, focus);
			IF g # NIL THEN
				msg.focus := FALSE; msg.show := TRUE;
				Views.ForwardCtrlMsg(g, msg)
			END
		END
	END CheckMaskFocus;
	
	PROCEDURE (a: RefocusAction) Do;
		VAR _: Views.View; ffm: FindFrameMsg; f: Views.Frame;
	BEGIN 
		f := a.frame; 
		IF f.rider = NIL THEN Views.Broadcast(a.view, ffm); f := ffm.frame END;
		IF f # NIL THEN CheckMaskFocus(f, a.view, _) END
	END Do;
	
	PROCEDURE NewStack* (): Stack;
		VAR st: Stack;
	BEGIN
		NEW(st);
		InitStack(st);
		RETURN st
	END NewStack;
	
	PROCEDURE (sc: StackCreator) Do;
		VAR s: Stack; dev: View; i: INTEGER;
	BEGIN
		IF Services.Ticks() - sc.clickTime > Services.resolution DIV 2 THEN
			IF sc.n > 1 THEN
				dev := NewDivider(vertical);
				FOR i := 1 TO sc.n DO
					s := NewStack();
					AppendDivTile(dev, s, 0, 0);
				END;
				AppendDivTile(sc.d, dev, 0, 0);
			ELSE
				s := NewStack();
				AppendDivTile(sc.d, s, 0, 0);
			END;
			globalStackCreator := NIL;
		ELSE
			Services.DoLater(sc, Services.Ticks() + Services.resolution DIV 5)
		END
	END Do;
	
	PROCEDURE CreateStack(d: Divider);
	BEGIN
		IF globalStackCreator = NIL THEN
			NEW(globalStackCreator);
			globalStackCreator.n := 1;
			globalStackCreator.d := d;
			globalStackCreator.clickTime := Services.Ticks();
			Services.DoLater(globalStackCreator, Services.now);
		ELSE
			INC(globalStackCreator.n);
			globalStackCreator.clickTime := Services.Ticks()
		END
	END CreateStack;
	

	PROCEDURE (d: Divider) HandleCtrlMsg* (f: Views.Frame; VAR msg: Views.CtrlMessage;
		VAR focus: Views.View
	);	
		VAR dummy: INTEGER; g: Views.Frame; dx, dy: UCs; w, h: INTEGER;
	BEGIN
		d.context.GetSize(w, h);
		WITH msg: DropFeedbackMsg DO
			ShowDropMark(f, d, msg)
		| msg: Controllers.MarkMsg DO 
			IF d.active # none THEN focus := d.ctx[d.active].view END;
			IF msg.show & (focus # NIL) & ~ClaimFocus(focus) THEN
				focus := NIL;	(* don't relay the message, because focus will likely change *)
				refocusAction.view := d;
				refocusAction.frame := f;
				Services.DoLater(refocusAction, Services.immediately)
			END
		| msg: Controllers.TickMsg DO 
			IF d.active # none THEN focus := d.ctx[d.active].view END;
			CheckMaskFocus(f, d, focus)
		| msg: Controllers.CursorMessage DO
			IF InResizer(d, msg.x, msg.y, dummy) 
			& ((msg IS Controllers.TrackMsg) OR (msg IS Controllers.PollCursorMsg)) THEN
				WITH
				| msg: Controllers.TrackMsg DO
					TrackResize(f, msg.x, msg.y)
				| msg: Controllers.PollCursorMsg DO
					msg.cursor := CursorAt(f, msg.x, msg.y)
				END
			ELSIF (msg.x > w - f.dot * 10) & (msg.y < param.tabHeight) THEN
				WITH
				| msg: Controllers.TrackMsg DO
					CreateStack(d)
				| msg: Controllers.PollCursorMsg DO
					msg.cursor := Ports.tableCursor
				ELSE END
			ELSE
			
				g := Views.FrameAt(f, msg.x, msg.y);
				IF g # NIL THEN focus := g.view END;
				IF (focus # NIL) & (msg IS Controllers.TrackMsg)
					& ClaimFocusAt(focus, f, g, msg.x, msg.y) THEN
					SetFocus(d, focus)
				END
				
			END
		ELSE
			IF d.active = none THEN
				focus := NIL
			ELSE
				focus := d.ctx[d.active].view
			END
		END;
		WITH msg: DropTabPollMsg DO 
			IF focus # NIL THEN g := Views.ThisFrame(f, focus);
				IF g # NIL THEN
					dx := f.gx - g.gx; dy := f.gy - g.gy;
					INC(msg.x, dx); INC(msg.y, dy);
					Views.ForwardCtrlMsg(g, msg);
					DEC(msg.x, dx); DEC(msg.y, dy)
				END;
				focus := NIL 
			END;
			IF (msg.filler = NIL) & (f.l <= msg.x) & (msg.x < f.r) & (f.t <= msg.y) & (msg.y < f.b) THEN
				msg.ind := ThisTile(d, msg.x, msg.y); 
				IF (msg.ind = -1) & (Len(d) = 0) THEN
					msg.ind := 0
				ELSIF msg.ind >= 0 THEN
					INC(msg.ind)
				END;
				IF (msg.ind # -1) & (d.canDrop # NIL) & ~d.canDrop(d, msg.ind) THEN
					msg.ind := -1
				END;
				IF msg.ind # -1 THEN
					msg.frame := f; msg.filler := d
				END
			END
		ELSE
		END
	END HandleCtrlMsg;
	
	(* GenericContext *)
	
	PROCEDURE (c: GenericContext) Normalize* (): BOOLEAN; BEGIN RETURN TRUE END Normalize;
	PROCEDURE (c: GenericContext) ThisModel* (): Models.Model; BEGIN RETURN NIL END ThisModel;

	PROCEDURE CloseTab (f: Views.Frame; VAR tab: Tab);
	VAR close: RemoveMsg; focus_, v: Views.View;
	BEGIN
		ASSERT(f # NIL, 20); ASSERT(tab # NIL, 21);
		v := tab.view; ASSERT(v # NIL, 22);
		close.processed := FALSE;
		v.HandleCtrlMsg(Views.ThisFrame(f, v), close, focus_);
		IF tab.filler # NIL THEN
			IF close.processed THEN Activate(tab)
			ELSE Remove(tab); tab := NIL
			END
		END
	END CloseTab;
	

	(* Closer context and button *)
	
	PROCEDURE (ctx: CloserCtx) GetSize (OUT w, h: INTEGER);
	BEGIN
		w := ctx.wid;
		IF param.closerMark[0] # 0X THEN
			h := ctx.asc + ctx.dsc
		ELSE
			h := ctx.wid
		END
	END GetSize;

	PROCEDURE (btn: Closer) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		VAR ctx: CloserCtx;
		VAR color, w, w2, nw, s, s2, h: INTEGER;
	BEGIN
		ctx := btn.context(CloserCtx);
		IF param.closerMark[0] # 0X THEN
			f.DrawString(0, ctx.asc, param.closerColor, param.closerMark, ctx.fnt);
		ELSE
			w := 10 * Ports.point;
			nw := ((w DIV f.unit) - (w  DIV f.unit) MOD 2 + 1) * f.unit;
			IF (nw # w) OR (nw # h) THEN btn.context.SetSize(nw, nw); w := nw END;
			w2 := w DIV 2;
			s :=  5 * Ports.point - (5 * Ports.point) MOD f.unit;
			s2 :=  s + f.unit;
			IF (hotBtn # NIL) & (hotBtn = btn) THEN
				color := Ports.black;
				f.DrawRect(w2-s, w2-s, w2+s2, w2+s2, Ports.fill, Ports.RGBColor(200, 200, 200));
				s :=  4 * Ports.point - (4 * Ports.point) MOD f.unit;
				f.DrawLine(w2, w2, w2 + s, w2 + s, f.dot, color);
				f.DrawLine(w2, w2, w2 + s, w2 - s, f.dot, color);
				f.DrawLine(w2, w2, w2 - s, w2 + s, f.dot, color);
				f.DrawLine(w2, w2, w2 - s, w2 - s, f.dot, color);
			ELSE
				IF param.markDirty & btn.tab.titleView.dirty THEN
					f.DrawOval(w2 - s, w2 - s, w2 + s, w2 + s, Ports.fill, param.color)
				ELSE
					color := Ports.grey50;
					s :=  4 * Ports.point - (4 * Ports.point) MOD f.unit;
					f.DrawLine(w2, w2, w2 + s, w2 + s, f.dot, color);
					f.DrawLine(w2, w2, w2 + s, w2 - s, f.dot, color);
					f.DrawLine(w2, w2, w2 - s, w2 + s, f.dot, color);
					f.DrawLine(w2, w2, w2 - s, w2 - s, f.dot, color);
				END
			END
		END
	END Restore;
	
	PROCEDURE (btn: Closer) HandleCtrlMsg (f: Views.Frame; VAR msg: Views.CtrlMessage; 
		VAR focus: Views.View
	);
	BEGIN
		WITH msg: Controllers.TrackMsg DO 
			CloseTab(f, btn.tab);
			btn.afterTrack := TRUE;
				(* just after TrackMsg there is PollCursorMsg called,
				 btn should be marked  for not to be focused then *)
		| msg: Controllers.PollCursorMsg DO
			IF (hotBtn = NIL) & ~ btn.afterTrack THEN
				hotBtn := btn;
				Views.Update(btn, Views.keepFrames)
			END;
			btn.afterTrack := FALSE
		ELSE
		END
	END HandleCtrlMsg;
	
	
	(* TitleView *)
	
	PROCEDURE (a: CheckDirtyAction) Do;
	VAR i: INTEGER; wpref: WindowPref;  state: BOOLEAN; tv: TitleView;
	BEGIN
		FOR i:=0 TO Len(a.stack) - 1 DO
			tv := a.stack.ctx[i](Tab).titleView;
			Views.HandlePropMsg(tv.tab.view, wpref);
			IF (wpref.win # NIL) & ~ (Windows.neverDirty IN wpref.win.flags) THEN
				state := wpref.win.seq.Dirty();
				IF state # tv.dirty THEN
					tv.dirty := state;
					Views.Update(tv.tab.closer, Views.keepFrames)
				END
			END
		END;
		Services.DoLater(a, Services.Ticks() + Services.resolution DIV 4)
	END Do;
	
	PROCEDURE IsFront(view: Views.View): BOOLEAN;
	VAR wpref: WindowPref; msg: StdDocuments.PollOptsMsg; front: BOOLEAN;
	BEGIN
		front := FALSE;
		Views.HandlePropMsg(view, wpref);
		IF wpref.win # NIL THEN
			msg.opts := {};
			Views.Broadcast(wpref.win.doc, msg);
			front := StdDocuments.front IN msg.opts
		END;
		RETURN front
	END IsFront;
	
	PROCEDURE (tv: TitleView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		VAR
			ctx: TitleCtx; fnt: Fonts.Font; w, h: INTEGER; col: Ports.Color;
			name: ARRAY 32 OF CHAR; rf: Views.Frame;
	BEGIN
		tv.context.GetSize(w, h);
		IF param.debug THEN
			f.DrawRect(0, 0, w, h, Ports.fill, Ports.RGBColor(200, 100, 100))
		END;
		CheckTitleWidth(tv, FALSE);
		ctx := tv.context(TitleCtx);
		fnt := ctx.fnt;
		IF IsFront(tv.tab.view) THEN
			col := param.colorFocus;
			IF param.boldenFront THEN 
				fnt := Fonts.dir.This(fnt.typeface, fnt.size, fnt.style, Fonts.bold)
			END
		ELSE
			col := param.color
		END;
		IF col = Ports.defaultColor THEN
			col := Dialog.defaultColor
		END;
		IF param.closerSide = left THEN
			f.DrawString(param.margins, h DIV 2 + (ctx.asc - ctx.dsc)  DIV 2, col, tv.tab.title, fnt)
		ELSE
			f.DrawString(param.margins * 2, h DIV 2 + (ctx.asc-ctx.dsc) DIV 2, col, tv.tab.title, fnt)
		END
	END Restore;
	
	PROCEDURE (tv: TitleView) HandleViewMsg (f: Views.Frame; VAR msg: Views.Message);
	BEGIN
		WITH msg: FindFrameMsg DO
			ASSERT(msg.frame = NIL, 126); msg.frame := f
		ELSE END
	END HandleViewMsg;
	
	PROCEDURE (tv: TitleView) HandleCtrlMsg (f: Views.Frame; VAR msg: Views.CtrlMessage;
		VAR focus: Views.View
	);
		VAR
			poll: DropTabPollMsg; x, y, ox, oy, targInd: INTEGER; ticks: LONGINT; mod: SET; 
			isDown, wasActive: BOOLEAN; tell: DropFeedbackMsg; target: View; tFrame: Views.Frame;
			prop: Proposal; wpref: WindowPref; ffm: FindFrameMsg;
	BEGIN
		WITH 
		| msg: StdWindows.PrefocusMsg DO 
			Views.HandlePropMsg(tv.tab.view, wpref);
			IF wpref.win # NIL THEN msg.win := wpref.win END
		| msg: Controllers.TrackMsg DO 
			x := msg.x; y := msg.y; ticks := Services.Ticks(); ox := x; oy := y; targInd := -1;
			REPEAT
				IF (((Services.Ticks() - ticks) > (Services.resolution DIV 5)) & (x # ox) & (y # oy))
					OR ((Services.Ticks() - ticks) > (Services.resolution DIV 1)) THEN
					f.SetCursor(Ports.refCursor);
					poll.filler := NIL;
					Controllers.Transfer(x, y, f, msg.x, msg.y, poll);
					IF (target # poll.filler) OR (targInd # poll.ind) THEN
						IF target # NIL THEN
							tell.x := ox; tell.y := oy; tell.ind := targInd; tell.show := FALSE;
							IF tFrame.rider = NIL THEN
								ffm.frame := NIL;
								Views.Broadcast(target, ffm); tFrame := ffm.frame
							END;
							Views.ForwardCtrlMsg(tFrame, tell)
						END;
						IF poll.filler # NIL THEN
							f.SetCursor(Ports.refCursor);
							tell.x := poll.x; tell.y := poll.y; tell.ind := poll.ind; tell.show := TRUE;
							IF poll.frame.rider = NIL THEN 
								ffm.frame := NIL;
								Views.Broadcast(poll.filler, ffm); poll.frame := ffm.frame 
							END;
							Views.ForwardCtrlMsg(poll.frame, tell)
						ELSE
							f.SetCursor(stopCursor)
						END
					END;
					ox := x; oy := y; target := poll.filler;
					targInd := poll.ind; tFrame := poll.frame
				END;
				IF f.rider = NIL THEN
					ffm.frame := NIL; Views.Broadcast(tv, ffm); f := ffm.frame
				END;
				IF f # NIL THEN f.Input(x, y, mod, isDown) ;ASSERT(f.rider # NIL) END
			UNTIL (f = NIL) OR ~isDown;
			IF target # NIL THEN 
				tell.x := ox; tell.y := oy; tell.ind := targInd; tell.show := FALSE;
				IF tFrame.rider = NIL THEN
					ffm.frame := NIL;
					Views.Broadcast(target, ffm); tFrame := ffm.frame
				END;
				Views.ForwardCtrlMsg(tFrame, tell)
			END;
			
			IF poll.filler # NIL THEN
				wasActive := tv.tab.filler.active = ThisInd(tv.tab);
				DoTransfer(tv.tab.filler, ThisInd(tv.tab), poll.filler, poll.ind, poll.frame);
				IF wasActive THEN (*poll.filler.Activate(poll.ind);*) 
					prop.op := activate; poll.filler.ctx[poll.ind].Consider(prop) 
				END;
			ELSE
				Activate(tv.tab)
			END
		ELSE
		END
	END HandleCtrlMsg;
	
	PROCEDURE (tctx: TitleCtx) GetSize (OUT wid, hei: INTEGER);
	BEGIN
		CheckTitleWidth(tctx.view, FALSE);
		wid := tctx.view.wid;
		hei := tctx.view.hei
	END GetSize;
	
	(* Tabulator *)
	
	PROCEDURE (tb: Tabulator) GetBackground (VAR bg: Ports.Color);
	BEGIN bg := param.bgColor
	END GetBackground;
	
	PROCEDURE GetNextTabGeometry (VAR tg: TabGeometry);
		VAR tab: Tab; width, height, len, margin: INTEGER;
	BEGIN
		INC(tg.ind); len := Len(tg.stack);
		IF (0 = len) OR (tg.ind >= len) THEN
			tg.ind := -1
		ELSE
			tab := tg.stack.ctx[tg.ind](Tab);
			tg.stack.tb.context.GetSize(width, height);
			CheckTitleWidth(tab.titleView, FALSE);
			tg.titleWidth := tab.titleView.wid;
			
			margin := ((height - param.margins) - tg.closerWidth) DIV 2;
			
			IF tg.closerSide = left THEN
				tg.closerLeft := margin;
				tg.titleLeft := tg.closerLeft + tg.closerWidth;
			ELSE
				tg.closerLeft := tg.titleWidth;
				tg.titleLeft := 0
			END;
			tg.width := tg.titleWidth + tg.closerWidth + margin;
			
		END
	END GetNextTabGeometry;
	
	PROCEDURE GetFirstTabGeometry (st: Stack; closerSide: BOOLEAN; OUT tg: TabGeometry);
		VAR tab: Tab;
	BEGIN
		ASSERT(st # NIL, 20);
		tg.stack := st;
		tg.ind := -1;
		tg.closerSide := closerSide;
		IF Len(st) > 0 THEN
			tab := st.ctx[0](Tab);
			tg.closerWidth := CheckCloserWidth(tab.closer);
			GetNextTabGeometry(tg)
		END
	END GetFirstTabGeometry;
	
	
	(** absolute, i.e. w/o shift *)
	PROCEDURE GetActiveTabPosition (f: Views.Frame; st: Stack; OUT x0, x1: INTEGER): BOOLEAN;
	VAR
		x, margin, cwdt, w, tidx: INTEGER;
		res: BOOLEAN;
		tab: Tab;
	BEGIN
		x0 := 0; x1 := 0;
		IF (st = NIL) OR (st.active < 0) OR (st.active >= LEN(st.ctx)) THEN
			res := FALSE
		ELSE
			x := 0;
			margin := Ports.point *2; 
			margin := margin - margin MOD f.unit;
			FOR tidx := 0 TO st.active DO
				tab := st.ctx[tidx](Tab);
				cwdt := CheckCloserWidth(tab.closer);
				CheckTitleWidth(tab.titleView, FALSE);
				w := tab.titleView.wid + cwdt + margin;
				IF st.tb.dropMark = tidx THEN INC(w, 18 * f.dot) END;
				IF tidx = st.active THEN
					IF st.tb.dropMark = tidx+1 THEN INC(w, 18 * f.dot) END;
					x0 := x; x1 := x + w; res := TRUE
				ELSE
					INC(x, w + param.margins * 4)
				END
			END
		END;
		RETURN res
	END GetActiveTabPosition;

	PROCEDURE (tb: Tabulator) CalcTotalTabWidth (f: Views.Frame): INTEGER, NEW;
	VAR
		tabG: TabGeometry; x, w, h, margin: INTEGER;
	BEGIN
		x := 2 * mm;
		margin := Ports.point * 2; 
		margin := margin - margin MOD f.unit;
		GetFirstTabGeometry(tb.stack, param.closerSide, tabG);
		WHILE (tabG.ind # -1) DO
			INC(x, tabG.width + margin);
			GetNextTabGeometry(tabG)
		END;
		RETURN x
	END CalcTotalTabWidth;
	
	PROCEDURE (tb: Tabulator) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		VAR
			len, width, height, x, posX, posY, col, margin: INTEGER;
			tabG: TabGeometry;
			titleView: TitleView;
		
		PROCEDURE DrawDropMark;
			VAR i: INTEGER;
		BEGIN
			FOR i := 0 TO 7 DO
				f.DrawLine(
					x + (i+1) * f.dot,
					param.margins DIV 2 + height DIV 2 - i * f.dot,
					x + (i+1) * f.dot,
					param.margins DIV 2 + height DIV 2 + i * f.dot, 
					f.dot, param.marksColor);
			END;
			f.DrawRect(
					x + 8 * f.dot,
					param.margins DIV 2 + height DIV 2 - 4 * f.dot,
					x + 14 * f.dot,
					param.margins DIV 2 + height DIV 2 + 5 * f.dot, 
					Ports.fill, param.marksColor);
			INC(x, 18 * f.dot)
		END DrawDropMark;
		
	BEGIN
		
		tb.context.GetSize(width, height);
		
		x := tb.scrollShift + 2 * mm;
				
		margin := Ports.point *2; 
		margin := margin - margin MOD f.unit;
		
		IF tb.stack.ctx # NIL THEN

			IF param.strokeColor # Views.transparent THEN
				f.DrawRect(0, height - f.dot, width, height, Ports.fill, param.strokeColor)
			END;
			len := Len(tb.stack); ASSERT(LEN(tb.stack.ctx) = len, 100);
			
			GetFirstTabGeometry(tb.stack, param.closerSide, tabG);
			WHILE tabG.ind # -1 DO 
				tabG.width := tabG.width - tabG.width MOD f.unit; 
				IF tb.dropMark = tabG.ind THEN DrawDropMark END;
				
				IF tabG.ind = tb.stack.active THEN
					tb.actL := x;
					tb.actR := x + tabG.width;	(* for RestoreMarks *)
					
					col := param.selectedBgColor;
					IF col = Views.transparent THEN
						tb.stack.ctx[tabG.ind].view.GetBackground(col)
					END;
					IF col # Views.transparent THEN 
						f.DrawRect(x, param.margins, x + tabG.width, height, Ports.fill, col)
					END;
					col := param.strokeColor;
					IF col # Views.transparent THEN 
						f.DrawLine(x, param.margins, x, param.margins + height, f.dot, col);
						f.DrawLine(x + tabG.width, param.margins, x + tabG.width, height, f.dot, col)
					END;
				ELSE
					
					col := param.nonselectedBgColor;
					IF col # Views.transparent THEN 
						f.DrawRect(x, param.margins + 2 * Ports.point, x + tabG.width, height, Ports.fill, col)
					END;
					col := param.strokeColorShaded;
					IF col # Views.transparent THEN 
						f.DrawLine(x, param.margins + 3 * f.dot, x, height, f.dot, col);
						f.DrawLine(x + tabG.width, param.margins + 3 * f.dot, x + tabG.width, height, f.dot, col);
						f.DrawLine(x, param.margins + 3 * f.dot, x + tabG.width, param.margins + 3 * f.dot, f.dot, col);
					END
					
				END;
				
				posX := x + tabG.closerLeft;
				posX := posX - posX MOD f.unit;  (* correct position with respect to f.unit *)
				posY := param.margins
					+ (height - param.margins) DIV 2 - tabG.closerWidth DIV 2 + 3 * f.dot;
				(* posY := posY - posY MOD f.unit; *)
				
				Views.InstallFrame(f, tb.stack.ctx[tabG.ind](Tab).closer, posX, posY, 0, FALSE);
				
				titleView := tb.stack.ctx[tabG.ind](Tab).titleView;
				
				posX := x + tabG.titleLeft;
				posX := posX - posX MOD f.unit;  (* correct position with respect to f.unit *)
				posY := param.margins + 3 * f.dot;
				posY := posY - posY MOD f.unit;
				
				Views.InstallFrame(f, titleView, posX, posY, 0, FALSE);
				
				INC(x, tabG.width +  margin);
				
				GetNextTabGeometry(tabG)
			END
		END;
		IF tb.dropMark = len THEN DrawDropMark END
		
	END Restore;
	
	PROCEDURE (tb: Tabulator) RestoreMarks (f: Views.Frame; l, t, r, b: INTEGER);
		VAR width, height, color: INTEGER;
	BEGIN
		tb.context.GetSize(width, height);
		IF (tb.stack.active >= 0) & IsFront(tb.stack.ctx[tb.stack.active].view) THEN
			f.DrawRect(tb.actL, param.margins, tb.actR, 
				param.margins + 2 * f.dot, Ports.fill, param.markColor)
		END
	END RestoreMarks;
	
	
	PROCEDURE (tb: Tabulator) HandleCtrlMsg (f: Views.Frame;
		VAR msg: Views.CtrlMessage; VAR focus: Views.View
	);
		VAR g: Views.Frame; tabG: TabGeometry; x, w, h, margin: INTEGER; tmp: Views.View;
	BEGIN
		WITH
		| msg: DropTabPollMsg DO
			msg.filler := tb.stack;
			x := tb.scrollShift + 2 * mm;
			margin := Ports.point *2; 
			margin := margin - margin MOD f.unit;
			GetFirstTabGeometry(tb.stack, param.closerSide, tabG);
			WHILE (tabG.ind # -1) & (msg.x >= (x + tabG.width)) DO
				INC(x, tabG.width +  margin);
				GetNextTabGeometry(tabG)
			END;
			IF tabG.ind = -1 THEN
				IF tb.stack = NIL THEN msg.ind := 0 ELSE msg.ind := Len(tb.stack)
			END
			ELSIF msg.x < (x + tabG.width DIV 2) THEN msg.ind := tabG.ind
			ELSE msg.ind := tabG.ind + 1
			END;
			msg.frame := f
		| msg: DropFeedbackMsg DO
			IF msg.show THEN
				tb.dropMark := msg.ind
			ELSE
				tb.dropMark := -1
			END;
			Views.Update(tb, Views.keepFrames); Views.ValidateRoot(Views.RootOf(f))
		| msg: Controllers.WheelMsg DO
			
			(* calculate total tab width *)
			x := 2 * mm;
			margin := Ports.point *2; 
			margin := margin - margin MOD f.unit;
			GetFirstTabGeometry(tb.stack, param.closerSide, tabG);
			WHILE (tabG.ind # -1) DO
				INC(x, tabG.width +  margin);
				GetNextTabGeometry(tabG)
			END;
			
			tb.context.GetSize(w, h);
			IF msg.op = Controllers.incLine THEN
				INC(tb.scrollShift, Ports.mm * 10) 
			ELSIF msg.op = Controllers.decLine THEN
				DEC(tb.scrollShift, Ports.mm * 10);
			END;
			IF x < w THEN
				tb.scrollShift := 0
			ELSE
				tb.scrollShift := MIN(tb.scrollShift, 0); (* left limit *)
				tb.scrollShift := MAX(tb.scrollShift, - (x - w + Ports.mm * 4) ); (* right limit *)
			END;
			
			msg.done := TRUE;
			Views.Update(tb, Views.keepFrames)
		| msg: Controllers.CursorMessage DO 
			g := Views.FrameAt(f, msg.x, msg.y); IF g # NIL THEN focus := g.view END;
			IF hotBtn # NIL THEN
				tmp := hotBtn;
				hotBtn := NIL;
				Views.Update(tmp, Views.keepFrames)
			END;
		ELSE
		END
	END HandleCtrlMsg;
	
	(* AD: currently, a click inside a tab that falls in the gap between the Closer and the TitleView is just not handled; it should be as an activation click *)
	
	PROCEDURE (c: Context) GetSize* (OUT w, h: UCs);
	BEGIN 
		IF c.w = magicWidth THEN
			IF (c.filler # NIL) & (c.filler.context # NIL) THEN
				c.filler.context.GetSize(c.w, c.h);
				DEC(c.h, 6 * mm)
			END
		END;
		w := c.w; h := c.h
	END GetSize;
	
	PROCEDURE (fc: Context) Consider* (VAR p: Models.Proposal);
		VAR f: View; len: INTEGER; 
	BEGIN
		f := fc.filler; ASSERT(f # NIL, 20);
		WITH p: Proposal DO
			CASE p.op OF
			| remove:
				len := Len(f); Remove(fc); ASSERT(len = Len(f) + 1, 60)
			| activate:
				Activate(fc); f.context.Consider(p)
			| setTitle:
				IF f IS Stack THEN SetTitle(f(Stack), ThisInd(fc), p.title) END
			ELSE HALT(20)
			END
		ELSE
		END
	END Consider;
	
	PROCEDURE ThisFiller* (v: Views.View): View;
		VAR res: View;
	BEGIN
		ASSERT(v # NIL, 20);
		IF v.context IS Context THEN
			res := v.context(Context).filler
		END;
		RETURN res
	END ThisFiller;

	PROCEDURE SetDivTileSize* (v: View; index, size: INTEGER);
		VAR d: Divider; tile: Tile;
	BEGIN
		ASSERT(v IS Divider, 20);
		ASSERT((index >= 0) & (index < Len(v)), 21);
		ASSERT(size >= 0, 22);
		d := v(Divider);
		tile := d.ctx[index](Tile);
		tile.size := size;
		tile.pSize := 0;
		Views.Update(d, Views.keepFrames)
	END SetDivTileSize;

	PROCEDURE InitParam (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			p.nonselectedBgColor := Ports.grey25;
			p.color := Ports.RGBColor(100, 100, 100);
			p.colorFocus := Ports.black;
			(* TODO: should be calculated from colorTheme settings... *)
		END
	END InitParam;
	
	PROCEDURE InitDefaults (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			p.closerColor := Ports.red;
			p.marksColor := Ports.RGBColor(255, 215, 50);
			p.closerSide := right;
			p.closerMark := "";
			
			p.boldenFront := FALSE;
			
			p.selectedBgColor := Views.transparent;
			
			p.nonselectedBgColor := Ports.grey25;
			
			p.strokeColor := Views.transparent;
			p.strokeColorShaded := Views.transparent;
			
			p.color := Ports.defaultColor;
			p.colorFocus := Ports.defaultColor;
			
			p.markDirty := TRUE;
			
			p.bgColor := Ports.RGBColor(40, 40, 40);
			p.markColor := Ports.RGBColor(255, 215, 50);
			
			p.margins := mm;
			p.gutterColor := Ports.RGBColor(50, 50, 50);
			p.gutterWidth := 3;
			p.tabHeight := 8 * Ports.mm;
			p.minTabWidth := 15 * Ports.mm;
			p.debug := FALSE;
			p.navigationMethod := navMethodDefault;
			p.InitDefaults := InitDefaults;
			p.Init := InitParam;
		END
	END InitDefaults;


BEGIN
	NEW(refocusAction);
	NEW(param);
	InitDefaults(param)
END StdGrids.