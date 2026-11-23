MODULE WinMechanisms;
	(* AMD64 interaction hooks and explicit Windows OLE drag/drop ABI. *)
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT S := SYSTEM, WinApi, O := WinOle, WinClipboard, StdCmds, 
		Services, Ports, Stores, Views, Controllers, Properties, 
		Dialog, Mechanisms, Containers, Documents, Windows, 
		HostPorts := WinPorts;


	CONST
		handleSize = Ports.point * 11 DIV 2;
		clipMiddleHandle = handleSize DIV 2 + 2 * Ports.point;

		escape = -2;
		
		dataKind = 0; sourceKind = 1; targetKind = 2; enumKind = 3;
		ePointer = 80004003H; eNoInterface = 80004002H; eNotImpl = 80004001H;
		eFormat = 80040064H; eOutOfMemory = 8007000EH;
		dragDrop = 40100H; dragCancel = 40101H;
		fixed = 31;	(* controller option *)


	TYPE
		Hook = POINTER TO RECORD (Mechanisms.Hook) END;
		FormatPtr = POINTER TO O.FORMATETC;
		GetDataProc = PROCEDURE [ms64] (self, format, medium: S.ADDRESS): INTEGER;
		NativeObject = POINTER TO RECORD
			vtable: S.ADDRESS; refs, kind: INTEGER; next, data: NativeObject;
			win: Windows.Window; wnd: S.ADDRESS; index: INTEGER;
			formats: ARRAY 6 OF INTEGER; media: ARRAY 6 OF O.STGMEDIUM;
			key: SET; installed, files, single: BOOLEAN; type: Stores.TypeName;
			view: Views.View; source, dest: Views.Frame; x, y, dx, dy, w, h, rx, ry: INTEGER
		END;
		ImportAction = RECORD (Services.SafeAction)
			format: INTEGER; med: O.STGMEDIUM; view: Views.View; w, h: INTEGER; single: BOOLEAN
		END;
		SourceAction = RECORD (Services.SafeAction)
			data, source: NativeObject; view: Views.View; w, h: INTEGER; single: BOOLEAN;
			mask, effect: SET; result: INTEGER
		END;
		DropAction = RECORD (Services.SafeAction)
			obj: NativeObject; data, point: S.ADDRESS; keys, effect, allowed: SET; op: INTEGER
		END;

	VAR objects: NativeObject;
		dataTable: ARRAY 12 OF S.ADDRESS; sourceTable: ARRAY 5 OF S.ADDRESS;
		targetTable, enumTable: ARRAY 7 OF S.ADDRESS;
		dragFrame, finalFrame: Views.Frame; dragX, dragY, dragW, dragH, dragRX, dragRY, finalX, finalY: INTEGER;
		dragSingle: BOOLEAN; dragType: Stores.TypeName; lastDropError*: INTEGER;

	(** focus borders **)

	PROCEDURE Fixed (host: Views.Frame; v: Views.View): BOOLEAN;
		VAR sp: Properties.ResizePref; c: Containers.Controller;
	BEGIN
		c := host.view(Containers.View).ThisController();
		IF c.opts * {Containers.noCaret, Documents.pageWidth..Documents.winHeight, fixed} # {} THEN
			RETURN TRUE
		END;
		sp.fixed := FALSE; Views.HandlePropMsg(v, sp); RETURN sp.fixed
	END Fixed;

	PROCEDURE PaintFocusBorder (f: Views.Frame; focus: Views.View; l, t, r, b: INTEGER);
		VAR u, s,  w, h,  mx, my,  l0, t0, r0, b0: INTEGER;
		
		PROCEDURE PaintHandle (x, y: INTEGER; l, t, r, b: BOOLEAN);
		BEGIN
			IF l THEN f.DrawRect(x - u, y, x, y + s, Ports.fill, Ports.background) END;
			IF t THEN f.DrawRect(x, y - u, x + s, y, Ports.fill, Ports.background) END;
			IF r THEN f.DrawRect(x + s, y, x + s + u, y + s, Ports.fill, Ports.background) END;
			IF b THEN f.DrawRect(x, y + s, x + s, y + s + u, Ports.fill, Ports.background) END;
			f.DrawRect(x, y, x + s, y + s, Ports.fill, Ports.defaultColor)
		END PaintHandle;
		
	BEGIN
		f.rider.GetRect(l0, t0, r0, b0);
		s := (handleSize - f.dot) DIV f.unit;
		f.rider.SetRect(l0 - s, t0 - s, r0 + s, b0 + s);
		u := f.dot; s := s * f.unit;
		w := r - l; h := b - t;
		f.DrawRect(l, t - s, r, t, Ports.fill, Ports.background);
		f.DrawRect(l, b, r, b + s, Ports.fill, Ports.background);
		f.DrawRect(l - s, t - s, l, b + s, Ports.fill, Ports.background);
		f.DrawRect(r, t - s, r + s, b + s, Ports.fill, Ports.background);
		DEC(s, u);
		f.MarkRect(l, t - s, r, t, Ports.fill, HostPorts.focusPat, Ports.show);
		f.MarkRect(l, b, r, b + s, Ports.fill, HostPorts.focusPat, Ports.show);
		f.MarkRect(l - s, t - s, l, b + s, Ports.fill, HostPorts.focusPat, Ports.show);
		f.MarkRect(r, t - s, r + s, b + s, Ports.fill, HostPorts.focusPat, Ports.show);
		IF ~Fixed(f, focus) THEN
			PaintHandle(l - s, t - s, FALSE, FALSE, TRUE, TRUE);
			PaintHandle(r, t - s, TRUE, FALSE, FALSE, TRUE);
			PaintHandle(l - s, b, FALSE, TRUE, TRUE, FALSE);
			PaintHandle(r, b, TRUE, TRUE, FALSE, FALSE);
			IF w > 2 * clipMiddleHandle THEN
				mx := (l + r - s) DIV 2;
				PaintHandle(mx, t - s, TRUE, FALSE, TRUE, FALSE);
				PaintHandle(mx, b, TRUE, FALSE, TRUE, FALSE)
			END;
			IF h > 2 * clipMiddleHandle THEN
				my := (t + b - s) DIV 2;
				PaintHandle(l - s, my, FALSE, TRUE, FALSE, TRUE);
				PaintHandle(r, my, FALSE, TRUE, FALSE, TRUE)
			END
		END;
		f.DrawRect(l - u, t - u, r + u, b + u, u, Ports.defaultColor);
		f.rider.SetRect(l0, t0, r0, b0)
	END PaintFocusBorder;

	PROCEDURE (hook: Hook) FocusBorderCursor* (f: Views.Frame; view: Views.View; l, t, r, b: INTEGER;
									x, y: INTEGER): INTEGER;
		VAR s, u, w, h, mx, my, cursor: INTEGER;
		
		PROCEDURE CheckHandle (x0, y0: INTEGER; c: INTEGER);
		BEGIN
			IF (x >= x0 - u) & (x <= x0 + s) & (y >= y0 - u) & (y <= y0 + s) THEN
				cursor := c
			END
		END CheckHandle;
		
	BEGIN
		u := f.dot; s := handleSize - 2 * u;
		IF (x < l - s) OR (x > r + s) OR (y < t - s) OR (y > b + s) THEN
			cursor := Mechanisms.outside
		ELSE
			cursor := Mechanisms.inside
		END;
		w := r - l; h := b - t;
		IF ~Fixed(f, view) THEN
			CheckHandle(l - s, t - s, HostPorts.resizeLCursor);
			CheckHandle(r, t - s, HostPorts.resizeRCursor);
			CheckHandle(l - s, b, HostPorts.resizeRCursor);
			CheckHandle(r, b, HostPorts.resizeLCursor);
			IF w > 2 * clipMiddleHandle THEN
				mx := (l + r - s) DIV 2;
				CheckHandle(mx, t - s, HostPorts.resizeVCursor);
				CheckHandle(mx, b, HostPorts.resizeVCursor)
			END;
			IF h > 2 * clipMiddleHandle THEN
				my := (t + b - s) DIV 2;
				CheckHandle(l - s, my, HostPorts.resizeHCursor);
				CheckHandle(r, my, HostPorts.resizeHCursor)
			END
		END;
		RETURN cursor
	END FocusBorderCursor;

	PROCEDURE RestoreBorderArea (f: Views.Frame; l, t, r, b: INTEGER);
	(* restore area under destructive border mark *)
		VAR g: Views.RootFrame; s, dx, dy: INTEGER;
	BEGIN
		g := Views.RootOf(f);
		dx := f.gx - g.gx; dy := f.gy - g.gy;
		s := (handleSize - f.dot) DIV f.unit * f.unit;
		INC(l, dx); INC(t, dy); INC(r, dx); INC(b, dy);
(*
		Views.UpdateRoot(g, l - s, t - s, r + s, b + s, FALSE);
*)
		Views.ValidateRoot(g);
		Views.RestoreRoot(g, l - s, t - s, r + s, b + s);
(*
		Views.RestoreRoot(g, l - s, t - s, r + s, t);
		Views.RestoreRoot(g, l - s, t, l, b);
		Views.RestoreRoot(g, r, t, r + s, b);
		Views.RestoreRoot(g, l - s, b, r + s, b + s)
*)
	END RestoreBorderArea;
	
	PROCEDURE (hook: Hook) MarkFocusBorder* (
		host: Views.Frame; focus: Views.View; l, t, r, b: INTEGER; show: BOOLEAN
	);
	BEGIN
		IF focus # NIL THEN
			IF show THEN
				PaintFocusBorder(host, focus, l, t, r, b)
			ELSE
				RestoreBorderArea(host, l, t, r, b)
			END
		END
	END MarkFocusBorder;
	

	(** selection borders **)

	PROCEDURE PaintSelBorder (f: Views.Frame; view: Views.View; l, t, r, b: INTEGER);
		VAR u, d, w, h,  mx, my, l0, t0, r0, b0: INTEGER; sizeable: BOOLEAN;
		
		PROCEDURE PaintHandle (x, y: INTEGER);
			VAR s: INTEGER; ci, co: Ports.Color;
		BEGIN
			DEC(x, d); DEC(y, d); s := d * 2 + u;
			IF sizeable THEN ci := HostPorts.selBackground; co := HostPorts.selTextCol
			ELSE ci := HostPorts.selTextCol; co := HostPorts.selBackground
			END;
			f.DrawRect(x, y, x + s, y + s, Ports.fill, co);
			INC(x, u); INC(y, u); DEC(s, 2 * u);
			f.DrawRect(x, y, x + s, y + s, Ports.fill, ci);
(*			
			f.DrawRect(x, y, x + s, y + s, Ports.fill, ci);
			f.DrawRect(x, y, x + s, y + s, 0, co)
*)
		END PaintHandle;
		
	BEGIN
		d := (handleSize - f.dot) DIV f.unit DIV 2;
		f.rider.GetRect(l0, t0, r0, b0);
		f.rider.SetRect(l0 - d - 1, t0 - d - 1, r0 + d + 1, b0 + d + 1);
		d := d * f.unit; u := f.dot;
		w := r - l; h := b - t; sizeable := ~Fixed(f, view);
		DEC(l, u); DEC(t, u);
(*
		f.SaveRect(l - d, t - d, r + u + d, b + u + d, res);
*)
		f.DrawRect(l, t, r + u, b + u, u, HostPorts.selBackground);
		IF f.front THEN
			IF (w > clipMiddleHandle) & (h > clipMiddleHandle) THEN
				PaintHandle(l, t);
				PaintHandle(r, t);
				PaintHandle(l, b);
				PaintHandle(r, b);
				IF w > 2 * clipMiddleHandle THEN
					mx := (l + r) DIV 2;
					PaintHandle(mx, t);
					PaintHandle(mx, b)
				END;
				IF h > 2 * clipMiddleHandle THEN
					my := (t + b) DIV 2;
					PaintHandle(l, my);
					PaintHandle(r, my)
				END
			ELSIF sizeable THEN
				PaintHandle(r, b)
			END
		END;
		f.rider.SetRect(l0, t0, r0, b0)
	END PaintSelBorder;

	PROCEDURE (hook: Hook) SelBorderCursor* (f: Views.Frame; view: Views.View; l, t, r, b: INTEGER;
									x, y: INTEGER): INTEGER;
		VAR d, u, w, h, mx, my, cursor: INTEGER;
		
		PROCEDURE CheckHandle (x0, y0: INTEGER; c: INTEGER);
		BEGIN
			IF (x >= x0 - d) & (x <= x0 + d) & (y >= y0 - d) & (y <= y0 + d) THEN
				cursor := c
			END
		END CheckHandle;
		
	BEGIN
		IF (x < l) OR (x > r) OR (y < t) OR (y > b) THEN cursor := Mechanisms.outside
		ELSE cursor := Mechanisms.inside
		END;
		IF (view # NIL) & ~Fixed(f, view) THEN
			d := (handleSize - f.dot) DIV f.unit DIV 2 * f.unit;
			w := r - l; h := b - t; u := f.dot;
			DEC(l, u); DEC(t, u);
			IF (w > clipMiddleHandle) & (h > clipMiddleHandle) THEN
				CheckHandle(l, t, HostPorts.resizeLCursor);
				CheckHandle(r, t, HostPorts.resizeRCursor);
				CheckHandle(l, b, HostPorts.resizeRCursor);
				CheckHandle(r, b, HostPorts.resizeLCursor);
				IF w > 2 * clipMiddleHandle THEN
					mx := (l + r) DIV 2;
					CheckHandle(mx, t, HostPorts.resizeVCursor);
					CheckHandle(mx, b, HostPorts.resizeVCursor)
				END;
				IF h > 2 * clipMiddleHandle THEN
					my := (t + b) DIV 2;
					CheckHandle(l, my, HostPorts.resizeHCursor);
					CheckHandle(r, my, HostPorts.resizeHCursor)
				END
			ELSE
				CheckHandle(r, b, HostPorts.resizeLCursor)
			END
		END;
		RETURN cursor
	END SelBorderCursor;

	PROCEDURE RestoreViewArea (f: Views.Frame; l, t, r, b: INTEGER);
	(* restore area under destructive selection mark *)
		VAR g: Views.RootFrame; d, dx, dy: INTEGER;
	BEGIN
(*
		d := (handleSize - f.dot) DIV f.unit DIV 2 * f.unit + f.dot;
		f.RestoreRect(l - d, t - d, r + d, b + d, TRUE)
*)
		g := Views.RootOf(f);
		dx := f.gx - g.gx; dy := f.gy - g.gy;
		d := (handleSize - f.dot) DIV f.unit DIV 2 * f.unit + f.dot;
		INC(l, dx); INC(t, dy); INC(r, dx); INC(b, dy);
		Views.ValidateRoot(g);
		Views.RestoreRoot(g, l - d, t - d, r + d, b + d)
	END RestoreViewArea;

	PROCEDURE (hook: Hook) MarkSingletonBorder* (
		host: Views.Frame; view: Views.View; l, t, r, b: INTEGER; show: BOOLEAN
	);
	BEGIN
		IF view # NIL THEN
			IF show THEN
				PaintSelBorder(host, view, l, t, r, b)
			ELSE
				RestoreViewArea(host, l, t, r, b)
			END
		END
(*		
		IF view # NIL THEN InvertSelBorder(host, view, l, t, r, b, show) END
*)
	END MarkSingletonBorder;


(*
	PROCEDURE MarkBorder* (host: Ports.Frame; view: Stores.Store; l, t, r, b: INTEGER);
		VAR s: INTEGER;
	BEGIN
		IF view # NIL THEN 
			s := markBorderSize * host.dot;
			host.MarkRect(l - s, t - s, r + s, b + s, s, Ports.dim50, Ports.show)
		END
	END MarkBorder;
*)

	PROCEDURE (hook: Hook) TrackToResize* (host: Views.Frame; view: Views.View;
										minW, maxW, minH, maxH: INTEGER;
										VAR l, t, r, b: INTEGER; VAR op: INTEGER;
										VAR x, y: INTEGER; VAR buttons: SET);
		VAR isDown: BOOLEAN; m: SET; p: Properties.SizePref;
			x1, y1,  dx, dy,  dl, dt, dr, db,  l0, t0, r0, b0,  l1, t1, r1, b1,  w, h,  dw, dh: INTEGER;
	BEGIN
		l0 := l; t0 := t; r0 := r; b0 := b;  dl := 0; dt := 0; dr := 0; db := 0;
		x1 := (l + r) DIV 2; y1 := (t + b) DIV 2;
		IF (r - l <= 2 * clipMiddleHandle) OR (ABS(x - x1) > handleSize DIV 2) THEN
			IF x < x1 THEN dl := 1 ELSE dr := 1 END
		END;
		IF (b - t <= 2 * clipMiddleHandle) OR (ABS(y - y1) > handleSize DIV 2) THEN
			IF y < y1 THEN dt := 1 ELSE db := 1 END
		END;
		IF (Controllers.extend IN buttons) & (dl # dr) THEN dl := 1; dr := 1 END;
		IF (Controllers.extend IN buttons) & (dt # db) THEN dt := 1; db := 1 END;
		host.MarkRect(l, t, r, b, 0, Ports.dim50, Ports.show);
		REPEAT
			host.Input(x1, y1, m, isDown);
			IF x1 < host.l THEN x1 := host.l ELSIF x1 > host.r THEN x1 := host.r END;
			IF y1 < host.t THEN y1 := host.t ELSIF y1 > host.b THEN y1 := host.b END;
			dx := x1 - x; dy := y1 - y;
			l1 := l0 + dl * dx; t1 := t0 + dt * dy; r1 := r0 + dr * dx; b1 := b0 + db * dy;
			w := r1 - l1; h := b1 - t1;
			IF (w > 0) & (h > 0) THEN
				p.fixedH := (dl = 0) & (dr = 0); p.fixedW := (dt = 0) & (db = 0);
				p.w := w; p.h := h; Views.HandlePropMsg(view, p); w := p.w; h := p.h;
				IF w < minW THEN w := minW ELSIF w > maxW THEN w := maxW END;
				IF h < minH THEN h := minH ELSIF h > maxH THEN h := maxH END;
				dw := w - (r1 - l1); dh := h - (b1 - t1);
				DEC(l1, dl * dw); DEC(t1, dt * dh);
				IF (dl + dr = 0) & (dw # 0) THEN INC(r1, dw) ELSE INC(r1, dr * dw) END;
				IF (dt + db = 0) & (dh # 0) THEN INC(b1, dh) ELSE INC(b1, db * dh) END;
				IF (l1 # l) OR (t1 # t) OR (r1 # r) OR (b1 # b) THEN
					host.MarkRect(l, t, r, b, 0, Ports.dim50, Ports.hide);
					l := l1; t := t1; r := r1; b := b1;
					host.MarkRect(l, t, r, b, 0, Ports.dim50, Ports.show)
				END
			END
		UNTIL ~isDown;
		host.MarkRect(l, t, r, b, 0, Ports.dim50, Ports.hide);
		x := x1; y := y1; buttons := {};
		IF (l # l0) OR (t # t0) OR (r # r0) OR (b # b0) THEN op := Mechanisms.resize
		ELSE op := Mechanisms.cancelResize
		END
	END TrackToResize;

	
	(* Narrow, explicit COM ABI adapters. CP object/interface code generation is
		not used: a native vtable is the first field, with CP roots until Release. *)

	PROCEDURE Method (object: S.ADDRESS; slot: INTEGER): S.ADDRESS;
		VAR table, method: S.ADDRESS;
	BEGIN S.GET(object, table); S.GET(table+slot*8, method); RETURN method END Method;

	PROCEDURE NewObject (kind: INTEGER): NativeObject;
		VAR obj: NativeObject;
	BEGIN
		NEW(obj); obj.kind := kind; obj.refs := 1; obj.next := objects; objects := obj;
		CASE kind OF
		| dataKind: obj.vtable := S.ADR(dataTable[0])
		| sourceKind: obj.vtable := S.ADR(sourceTable[0])
		| targetKind: obj.vtable := S.ADR(targetTable[0])
		| enumKind: obj.vtable := S.ADR(enumTable[0])
		END;
		RETURN obj
	END NewObject;

	PROCEDURE [ms64] AddRef (self: S.ADDRESS): INTEGER;
		VAR obj: NativeObject;
	BEGIN obj := S.VAL(NativeObject, self); INC(obj.refs); RETURN obj.refs END AddRef;

	PROCEDURE [ms64] Release (self: S.ADDRESS): INTEGER;
		VAR obj, prev: NativeObject; i, ignored: INTEGER;
	BEGIN
		obj := S.VAL(NativeObject, self); DEC(obj.refs);
		IF obj.refs = 0 THEN
			IF obj.kind = dataKind THEN
				FOR i := 0 TO 5 DO WinClipboard.ReleaseMedium(obj.media[i]) END
			ELSIF obj.kind = enumKind THEN
				ignored := Release(S.VAL(S.ADDRESS, obj.data)); obj.data := NIL
			END;
			IF objects = obj THEN objects := obj.next
			ELSE prev := objects; WHILE (prev # NIL) & (prev.next # obj) DO prev := prev.next END;
				IF prev # NIL THEN prev.next := obj.next END
			END;
			obj.next := NIL; obj.win := NIL; obj.view := NIL
		END;
		RETURN obj.refs
	END Release;

	PROCEDURE [ms64] QueryInterface (self, iid, out: S.ADDRESS): INTEGER;
		VAR obj: NativeObject; a, b, c, d, wanted, ignored: INTEGER;
	BEGIN
		IF (out = 0) OR (iid = 0) THEN RETURN ePointer END;
		S.PUT(out, S.VAL(S.ADDRESS, 0)); obj := S.VAL(NativeObject, self);
		S.GET(iid, a); S.GET(iid+4, b); S.GET(iid+8, c); S.GET(iid+12, d);
		CASE obj.kind OF dataKind: wanted := 10EH | sourceKind: wanted := 121H
		| targetKind: wanted := 122H | enumKind: wanted := 103H END;
		IF ((a = 0) OR (a = wanted)) & (b = 0) & (c = 0C0H) & (d = 46000000H) THEN
			S.PUT(out, self); ignored := AddRef(self); RETURN 0
		END;
		RETURN eNoInterface
	END QueryInterface;

	PROCEDURE Format (format: INTEGER; tymed: SET; VAR fmt: O.FORMATETC);
	BEGIN
		fmt.cfFormat := SHORT(format); fmt.ptd := 0; fmt.dwAspect := {0}; fmt.lindex := -1; fmt.tymed := tymed
	END Format;

	PROCEDURE FindFormat (obj: NativeObject; format: S.ADDRESS): INTEGER;
		VAR f: FormatPtr; i, id: INTEGER;
	BEGIN
		IF format = 0 THEN RETURN -1 END; f := S.VAL(FormatPtr, format);
		IF (f.dwAspect # {0}) OR (f.lindex # -1) OR (f.ptd # 0) THEN RETURN -1 END;
		id := f.cfFormat MOD 10000H;
		FOR i := 0 TO 5 DO
			IF (id = obj.formats[i]) & (obj.media[i].data # 0) & (f.tymed * obj.media[i].tymed # {}) THEN RETURN i END
		END;
		RETURN -1
	END FindFormat;

	PROCEDURE [ms64] GetData (self, format, medium: S.ADDRESS): INTEGER;
		VAR obj: NativeObject; med: O.STGMEDIUM; i: INTEGER;
	BEGIN
		IF medium = 0 THEN RETURN ePointer END;
		med.tymed := {}; med.data := 0; med.pUnkForRelease := 0;
		S.MOVE(S.ADR(med), medium, SIZE(O.STGMEDIUM)); obj := S.VAL(NativeObject, self); i := FindFormat(obj, format);
		IF i < 0 THEN RETURN eFormat END;
		IF ~WinClipboard.CloneMedium(obj.formats[i], obj.media[i].data, med) THEN RETURN eOutOfMemory END;
		S.MOVE(S.ADR(med), medium, SIZE(O.STGMEDIUM)); RETURN 0
	END GetData;

	PROCEDURE [ms64] GetDataHere (self, format, medium: S.ADDRESS): INTEGER;
	BEGIN RETURN eNotImpl END GetDataHere;

	PROCEDURE [ms64] QueryGetData (self, format: S.ADDRESS): INTEGER;
	BEGIN IF FindFormat(S.VAL(NativeObject, self), format) >= 0 THEN RETURN 0 ELSE RETURN eFormat END END QueryGetData;

	PROCEDURE [ms64] CanonicalFormat (self, input, output: S.ADDRESS): INTEGER;
		VAR f: FormatPtr;
	BEGIN
		IF (input = 0) OR (output = 0) THEN RETURN ePointer END;
		S.MOVE(input, output, SIZE(O.FORMATETC)); f := S.VAL(FormatPtr, output); f.ptd := 0; RETURN 40130H
	END CanonicalFormat;

	PROCEDURE [ms64] SetData (self, format, medium: S.ADDRESS; release: INTEGER): INTEGER;
	BEGIN RETURN eNotImpl END SetData;

	PROCEDURE [ms64] EnumFormats (self: S.ADDRESS; direction: INTEGER; out: S.ADDRESS): INTEGER;
		VAR en: NativeObject; ignored: INTEGER;
	BEGIN
		IF out = 0 THEN RETURN ePointer END; S.PUT(out, S.VAL(S.ADDRESS, 0));
		IF direction # 1 THEN RETURN eNotImpl END;
		en := NewObject(enumKind); en.data := S.VAL(NativeObject, self); ignored := AddRef(self);
		S.PUT(out, S.VAL(S.ADDRESS, en)); RETURN 0
	END EnumFormats;

	PROCEDURE [ms64] Advise (self, format: S.ADDRESS; flags: INTEGER; sink, connection: S.ADDRESS): INTEGER;
	BEGIN IF connection # 0 THEN S.PUT(connection, 0) END; RETURN 80040003H END Advise;

	PROCEDURE [ms64] Unadvise (self: S.ADDRESS; connection: INTEGER): INTEGER;
	BEGIN RETURN 80040003H END Unadvise;

	PROCEDURE [ms64] EnumAdvise (self, out: S.ADDRESS): INTEGER;
	BEGIN IF out # 0 THEN S.PUT(out, S.VAL(S.ADDRESS, 0)) END; RETURN 80040003H END EnumAdvise;

	PROCEDURE [ms64] NextFormat (self: S.ADDRESS; count: INTEGER; buffer, fetched: S.ADDRESS): INTEGER;
		VAR en: NativeObject; fmt: O.FORMATETC; n: INTEGER;
	BEGIN
		IF fetched # 0 THEN S.PUT(fetched, 0) ELSIF count # 1 THEN RETURN ePointer END;
		IF (count < 0) OR ((count > 0) & (buffer = 0)) THEN RETURN ePointer END;
		en := S.VAL(NativeObject, self); n := 0;
		WHILE (n < count) & (en.index < 6) DO
			IF en.data.media[en.index].data # 0 THEN
				Format(en.data.formats[en.index], en.data.media[en.index].tymed, fmt);
				S.MOVE(S.ADR(fmt), buffer+n*SIZE(O.FORMATETC), SIZE(O.FORMATETC)); INC(n)
			END;
			INC(en.index)
		END;
		IF fetched # 0 THEN S.PUT(fetched, n) END;
		IF n = count THEN RETURN 0 ELSE RETURN 1 END
	END NextFormat;

	PROCEDURE [ms64] SkipFormat (self: S.ADDRESS; count: INTEGER): INTEGER;
		VAR en: NativeObject; n: INTEGER;
	BEGIN
		IF count < 0 THEN RETURN ePointer END; en := S.VAL(NativeObject, self); n := 0;
		WHILE (n < count) & (en.index < 6) DO
			IF en.data.media[en.index].data # 0 THEN INC(n) END; INC(en.index)
		END;
		IF n = count THEN RETURN 0 ELSE RETURN 1 END
	END SkipFormat;

	PROCEDURE [ms64] ResetFormat (self: S.ADDRESS): INTEGER;
		VAR obj: NativeObject;
	BEGIN obj := S.VAL(NativeObject, self); obj.index := 0; RETURN 0 END ResetFormat;

	PROCEDURE [ms64] CloneFormat (self, out: S.ADDRESS): INTEGER;
		VAR en, copy: NativeObject; ignored: INTEGER;
	BEGIN
		IF out = 0 THEN RETURN ePointer END;
		en := S.VAL(NativeObject, self); copy := NewObject(enumKind); copy.data := en.data; copy.index := en.index;
		ignored := AddRef(S.VAL(S.ADDRESS, copy.data)); S.PUT(out, S.VAL(S.ADDRESS, copy)); RETURN 0
	END CloneFormat;

	PROCEDURE [ms64] ContinueDrag (self: S.ADDRESS; escape: INTEGER; key: SET): INTEGER;
		VAR obj: NativeObject;
	BEGIN
		obj := S.VAL(NativeObject, self);
		IF obj.key = {} THEN obj.key := key * {0,1,4} END;
		IF escape # 0 THEN RETURN dragCancel
		ELSIF key * obj.key = {} THEN RETURN dragDrop
		ELSE RETURN 0
		END
	END ContinueDrag;

	PROCEDURE [ms64] Feedback (self: S.ADDRESS; effect: SET): INTEGER;
	BEGIN RETURN 40102H END Feedback;

	PROCEDURE ExternalData (data: S.ADDRESS; format: INTEGER; OUT med: O.STGMEDIUM): BOOLEAN;
		VAR fn: GetDataProc; fmt: O.FORMATETC;
	BEGIN
		med.tymed := {}; med.data := 0; med.pUnkForRelease := 0;
		IF format = WinApi.CF_METAFILEPICT THEN Format(format, O.TYMED_MFPICT, fmt)
		ELSE Format(format, O.TYMED_HGLOBAL, fmt)
		END;
		fn := S.VAL(GetDataProc, Method(data, 3)); RETURN fn(data, S.ADR(fmt), S.ADR(med)) >= 0
	END ExternalData;

	PROCEDURE (VAR action: ImportAction) Do;
	BEGIN
		WinClipboard.ImportMedium(action.format, action.med, action.view, action.w, action.h, action.single)
	END Do;

	PROCEDURE ImportObject (data: S.ADDRESS; OUT view: Views.View; OUT w, h: INTEGER; OUT single: BOOLEAN);
		VAR action: ImportAction; i: INTEGER;
	BEGIN
		view := NIL; w := Views.undefined; h := Views.undefined; single := FALSE;
		FOR i := 0 TO 5 DO
			IF i # 1 THEN
				action.format := WinClipboard.TransferFormat(i); action.view := NIL;
				IF ExternalData(data, action.format, action.med) THEN
					Services.Try(action); O.ReleaseStgMedium(action.med);
					IF ~action.trapped & (action.view # NIL) THEN
						view := action.view; w := action.w; h := action.h; single := action.single; RETURN
					END
				END
			END
		END
	END ImportObject;

	PROCEDURE OpenDropFiles* (drop: S.ADDRESS): BOOLEAN;
		VAR i, n, size, attributes: INTEGER; path: ARRAY 1024 OF CHAR; opened: BOOLEAN;
	BEGIN
		n := WinApi.DragQueryFileW(drop, -1, NIL, 0); opened := FALSE;
		FOR i := 0 TO n-1 DO
			size := WinApi.DragQueryFileW(drop, i, NIL, 0);
			IF (size > 0) & (size < LEN(path)) THEN
				size := WinApi.DragQueryFileW(drop, i, path, LEN(path));
				attributes := WinApi.GetFileAttributesW(S.ADR(path));
				IF (attributes # -1) & ~(4 IN BITS(attributes)) THEN StdCmds.OpenDoc(path); opened := TRUE END
			END
		END;
		RETURN opened
	END OpenDropFiles;

	PROCEDURE DropEffect (allowed, keys: SET): SET;
	BEGIN
		IF keys * {2,3} = {2,3} THEN RETURN {} END;
		IF (3 IN keys) OR (keys * {0,1} = {0,1}) THEN
			IF 0 IN allowed THEN RETURN {0} ELSE RETURN {} END
		END;
		IF 2 IN keys THEN IF 1 IN allowed THEN RETURN {1} ELSE RETURN {} END END;
		IF 1 IN allowed THEN RETURN {1} ELSIF 0 IN allowed THEN RETURN {0} ELSE RETURN {} END
	END DropEffect;

	PROCEDURE PollTarget (obj: NativeObject; show: BOOLEAN);
		VAR msg: Controllers.PollDropMsg;
	BEGIN
		msg.x := obj.x * obj.win.port.unit; msg.y := obj.y * obj.win.port.unit;
		msg.source := obj.source; msg.sourceX := dragX; msg.sourceY := dragY;
		msg.mark := TRUE; msg.show := show; msg.type := obj.type; msg.isSingle := obj.single;
		msg.w := obj.w; msg.h := obj.h; msg.rx := obj.rx; msg.ry := obj.ry; msg.dest := NIL;
		obj.win.ForwardCtrlMsg(msg); obj.dest := msg.dest; obj.dx := msg.x; obj.dy := msg.y;
		IF obj.source # NIL THEN finalFrame := obj.dest; finalX := obj.dx; finalY := obj.dy END
	END PollTarget;

	PROCEDURE TargetPoint (obj: NativeObject; packed: S.ADDRESS);
		VAR pt: WinApi.POINT; ignored: INTEGER;
	BEGIN
		pt.x := SHORT(packed); pt.y := SHORT(packed DIV 100000000L);
		ignored := WinApi.ScreenToClient(obj.wnd, pt); obj.x := pt.x; obj.y := pt.y
	END TargetPoint;

	PROCEDURE ClearTarget (obj: NativeObject);
	BEGIN
		IF (obj.type # "") & ~obj.files & (obj.dest # NIL) THEN PollTarget(obj, FALSE) END;
		obj.type := ""; obj.dest := NIL; obj.source := NIL; obj.view := NIL; obj.files := FALSE;
		obj.rx := 0; obj.ry := 0; obj.w := Views.undefined; obj.h := Views.undefined; obj.single := FALSE
	END ClearTarget;

	PROCEDURE PopupEffect (obj: NativeObject; allowed: SET): SET;
		VAR menu: WinApi.HMENU; pt: WinApi.POINT; result, ignored: INTEGER; text: Dialog.String;
	BEGIN
		menu := WinApi.CreatePopupMenu(); IF menu = 0 THEN RETURN {} END;
		Dialog.MapString("#System:MoveHere",text); ignored := WinApi.AppendMenuW(menu, {}, 1, text);
		Dialog.MapString("#System:CopyHere",text); ignored := WinApi.AppendMenuW(menu, {}, 2, text);
		IF ~(1 IN allowed) THEN ignored := WinApi.EnableMenuItem(menu, 1, {0}) END;
		IF ~(0 IN allowed) THEN ignored := WinApi.EnableMenuItem(menu, 2, {0}) END;
		Dialog.MapString("#System:Cancel",text); ignored := WinApi.AppendMenuW(menu, {}, 3, text);
		ignored := WinApi.GetCursorPos(pt); ignored := WinApi.SetForegroundWindow(obj.wnd);
		result := WinApi.TrackPopupMenu(menu, {1,7,8}, pt.x, pt.y, 0, obj.wnd, NIL); ignored := WinApi.DestroyMenu(menu);
		IF result = 1 THEN RETURN allowed * {1} ELSIF result = 2 THEN RETURN allowed * {0} ELSE RETURN {} END
	END PopupEffect;

	PROCEDURE (VAR action: DropAction) Do;
		VAR obj: NativeObject; med: O.STGMEDIUM; msg: Controllers.DropMsg; keys: SET; ignored: INTEGER;
	BEGIN
		obj := action.obj; keys := action.keys;
		IF action.op = 0 THEN
			ClearTarget(obj); obj.key := keys;
			IF dragFrame # NIL THEN
				obj.source := dragFrame; obj.type := dragType; obj.single := dragSingle;
				obj.w := dragW; obj.h := dragH; obj.rx := dragRX; obj.ry := dragRY
			ELSE
				IF ExternalData(action.data, 15, med) THEN obj.files := TRUE; obj.type := "Files"; O.ReleaseStgMedium(med)
				ELSE ImportObject(action.data, obj.view, obj.w, obj.h, obj.single);
					IF obj.view # NIL THEN Services.GetTypeName(obj.view, obj.type) END
				END
			END;
			TargetPoint(obj, action.point);
			IF (obj.type # "") & ~obj.files THEN PollTarget(obj, TRUE) END
		ELSIF action.op = 1 THEN
			IF (obj.type # "") & ~obj.files THEN
				IF obj.dest # NIL THEN PollTarget(obj, FALSE) END;
				TargetPoint(obj, action.point); PollTarget(obj, TRUE)
			END;
			IF keys * {0,1,4} # {} THEN obj.key := keys END
		ELSIF action.op = 2 THEN ClearTarget(obj); finalFrame := NIL; action.effect := {}; RETURN
		ELSIF action.op = 3 THEN
			IF (obj.type # "") & ~obj.files THEN
				IF obj.dest # NIL THEN PollTarget(obj, FALSE) END;
				TargetPoint(obj, action.point); PollTarget(obj, FALSE)
			END
		END;
		IF obj.files THEN action.effect := action.effect * {0}
		ELSIF obj.dest # NIL THEN action.effect := DropEffect(action.effect, keys)
		ELSE action.effect := {}
		END;
		IF action.op = 3 THEN
			IF (action.effect # {}) & (1 IN obj.key) THEN
				IF obj.files THEN action.effect := PopupEffect(obj, action.allowed * {0})
				ELSE action.effect := PopupEffect(obj, action.allowed * {0,1}) END
			END;
			IF action.effect # {} THEN
				IF obj.files THEN
					IF ExternalData(action.data, 15, med) THEN
						IF ~OpenDropFiles(med.data) THEN action.effect := {} END; O.ReleaseStgMedium(med)
					ELSE action.effect := {}
					END
				ELSE
					Windows.dir.Select(obj.win, Windows.eager);
					IF obj.source = NIL THEN
					msg.x := obj.x*obj.win.port.unit; msg.y := obj.y*obj.win.port.unit;
					msg.source := NIL; msg.sourceX := 0; msg.sourceY := 0; msg.view := obj.view;
					msg.isSingle := obj.single; msg.w := obj.w; msg.h := obj.h; msg.rx := obj.rx; msg.ry := obj.ry;
						obj.win.ForwardCtrlMsg(msg)
					END
				END
			END;
			obj.view := NIL; obj.type := ""; obj.dest := NIL; obj.source := NIL; obj.files := FALSE
		END
	END Do;

	PROCEDURE NativeTarget (self, data: S.ADDRESS; keys: SET; point, effect: S.ADDRESS; op: INTEGER): INTEGER;
		VAR action: DropAction;
	BEGIN
		IF effect = 0 THEN RETURN ePointer END;
		action.obj := S.VAL(NativeObject, self); action.op := op; action.data := data;
		action.keys := keys; action.point := point; S.GET(effect, action.effect); action.allowed := action.effect;
		Services.Try(action);
		IF action.trapped THEN action.effect := {}; finalFrame := NIL END;
		S.PUT(effect, action.effect); RETURN 0
	END NativeTarget;

	PROCEDURE [ms64] DragEnter (self, data: S.ADDRESS; keys: SET; point, effect: S.ADDRESS): INTEGER;
	BEGIN RETURN NativeTarget(self, data, keys, point, effect, 0) END DragEnter;

	PROCEDURE [ms64] DragOver (self: S.ADDRESS; keys: SET; point, effect: S.ADDRESS): INTEGER;
	BEGIN RETURN NativeTarget(self, 0, keys, point, effect, 1) END DragOver;

	PROCEDURE [ms64] DragLeave (self: S.ADDRESS): INTEGER;
		VAR effect: SET;
	BEGIN effect := {}; RETURN NativeTarget(self, 0, {}, 0, S.ADR(effect), 2) END DragLeave;

	PROCEDURE [ms64] NativeDrop (self, data: S.ADDRESS; keys: SET; point, effect: S.ADDRESS): INTEGER;
	BEGIN RETURN NativeTarget(self, data, keys, point, effect, 3) END NativeDrop;

	PROCEDURE InstallDropTarget* (wnd: S.ADDRESS; win: Windows.Window);
		VAR obj: NativeObject; ignored: INTEGER;
	BEGIN
		obj := NewObject(targetKind); obj.win := win; obj.wnd := wnd;
		lastDropError := O.RegisterDragDrop(wnd, S.VAL(S.ADDRESS, obj)); obj.installed := lastDropError >= 0;
		ignored := Release(S.VAL(S.ADDRESS, obj))
	END InstallDropTarget;

	PROCEDURE RemoveDropTarget* (wnd: S.ADDRESS);
		VAR obj: NativeObject; result: INTEGER;
	BEGIN
		obj := objects;
		WHILE obj # NIL DO
			IF (obj.kind = targetKind) & obj.installed & (obj.wnd = wnd) THEN
				obj.installed := FALSE; result := O.RevokeDragDrop(wnd); RETURN
			END;
			obj := obj.next
		END
	END RemoveDropTarget;

	PROCEDURE InitNative;
		VAR med: O.STGMEDIUM; fmt: O.FORMATETC;
	BEGIN
		ASSERT(SIZE(O.STGMEDIUM) = 24); ASSERT(S.ADR(med.data)-S.ADR(med) = 8);
		ASSERT(SIZE(O.FORMATETC) = 32); ASSERT(S.ADR(fmt.ptd)-S.ADR(fmt) = 8);
		dataTable[0] := S.ADR(QueryInterface); dataTable[1] := S.ADR(AddRef); dataTable[2] := S.ADR(Release);
		dataTable[3] := S.ADR(GetData); dataTable[4] := S.ADR(GetDataHere); dataTable[5] := S.ADR(QueryGetData);
		dataTable[6] := S.ADR(CanonicalFormat); dataTable[7] := S.ADR(SetData); dataTable[8] := S.ADR(EnumFormats);
		dataTable[9] := S.ADR(Advise); dataTable[10] := S.ADR(Unadvise); dataTable[11] := S.ADR(EnumAdvise);
		sourceTable[0] := S.ADR(QueryInterface); sourceTable[1] := S.ADR(AddRef); sourceTable[2] := S.ADR(Release);
		sourceTable[3] := S.ADR(ContinueDrag); sourceTable[4] := S.ADR(Feedback);
		targetTable[0] := S.ADR(QueryInterface); targetTable[1] := S.ADR(AddRef); targetTable[2] := S.ADR(Release);
		targetTable[3] := S.ADR(DragEnter); targetTable[4] := S.ADR(DragOver); targetTable[5] := S.ADR(DragLeave); targetTable[6] := S.ADR(NativeDrop);
		enumTable[0] := S.ADR(QueryInterface); enumTable[1] := S.ADR(AddRef); enumTable[2] := S.ADR(Release);
		enumTable[3] := S.ADR(NextFormat); enumTable[4] := S.ADR(SkipFormat); enumTable[5] := S.ADR(ResetFormat); enumTable[6] := S.ADR(CloneFormat)
	END InitNative;

	PROCEDURE (VAR action: SourceAction) Do;
		VAR ignored: INTEGER;
	BEGIN
		WinClipboard.PrepareTransfer(action.view, action.w, action.h, action.single, action.data.formats, action.data.media);
		ignored := WinApi.ReleaseCapture(); action.effect := {};
		action.result := O.DoDragDrop(S.VAL(S.ADDRESS,action.data), S.VAL(S.ADDRESS,action.source), action.mask, action.effect)
	END Do;

	PROCEDURE (hook: Hook) TrackToDrop* (f: Views.Frame; view: Views.View;
			isSingle: BOOLEAN; w, h, rx, ry: INTEGER;
			VAR dest: Views.Frame; VAR destX, destY: INTEGER; VAR op: INTEGER;
			VAR x, y: INTEGER; VAR buttons: SET);
		VAR data, source: NativeObject; export: Views.View; allowedOp: INTEGER; c: Containers.Controller;
			model: Containers.Model; action: SourceAction; ignored: INTEGER; pt: WinApi.POINT;
			port: HostPorts.Port;
	BEGIN
		allowedOp := op; dest := NIL; op := Mechanisms.cancelDrop;
		IF dragFrame # NIL THEN RETURN END;
		export := view;
		IF ~isSingle THEN
			c := view(Containers.View).ThisController(); model := c.SelectionCopy();
			IF model = NIL THEN RETURN END; export := Views.CopyWithNewModel(view, model)
		END;
		dragFrame := f; dragX := x; dragY := y; dragW := w; dragH := h;
		dragRX := rx; dragRY := ry; dragSingle := isSingle; Services.GetTypeName(view, dragType); finalFrame := NIL;
		data := NewObject(dataKind); source := NewObject(sourceKind);
		action.data := data; action.source := source; action.view := export;
		action.w := w; action.h := h; action.single := isSingle;
		action.mask := {0}; IF allowedOp # Mechanisms.copy THEN INCL(action.mask, 1) END;
		Services.Try(action);
		IF action.trapped THEN finalFrame := NIL
		ELSIF action.result = dragDrop THEN
			IF 1 IN action.effect THEN op := Mechanisms.move ELSIF 0 IN action.effect THEN op := Mechanisms.copy END
		END;
		IF finalFrame # NIL THEN
			port := finalFrame.rider(HostPorts.Rider).port; pt.x := (finalX+finalFrame.gx) DIV port.unit;
			pt.y := (finalY+finalFrame.gy) DIV port.unit; ignored := WinApi.ClientToScreen(port.wnd, pt);
			port := f.rider(HostPorts.Rider).port; ignored := WinApi.ScreenToClient(port.wnd, pt);
			x := pt.x*port.unit-f.gx; y := pt.y*port.unit-f.gy
		END;
		dest := finalFrame; destX := finalX; destY := finalY; buttons := {};
		dragFrame := NIL; finalFrame := NIL;
		ignored := Release(S.VAL(S.ADDRESS,source)); ignored := Release(S.VAL(S.ADDRESS,data))
	END TrackToDrop;

	PROCEDURE PickMode (f, dest: Views.Frame; x, y: INTEGER): INTEGER;
		VAR mode, cursor: INTEGER;
	BEGIN
		IF WinApi.GetAsyncKeyState(1BH) < 0 THEN mode := escape; cursor := Ports.arrowCursor
(*
		ELSIF Home(f, x, y) THEN mode := Mechanisms.cancelPick; cursor := Ports.arrowCursor
*)
		ELSIF dest = NIL THEN mode := Mechanisms.cancelPick; cursor := HostPorts.stopCursor
		ELSE
			cursor := HostPorts.pickCursor;
			IF Services.SameType(dest.view, f.view) THEN
				mode := Mechanisms.pick
			ELSE mode := Mechanisms.pickForeign
			END
		END;
		f.SetCursor(cursor);
		RETURN mode
	END PickMode;

	PROCEDURE (hook: Hook) TrackToPick* (f: Views.Frame;
									VAR dest: Views.Frame; VAR destX, destY: INTEGER; VAR op: INTEGER;
									VAR x, y: INTEGER; VAR buttons: SET);
		VAR d, d0: Views.Frame;
			dx, dy,  x0, y0,  x1, y1: INTEGER; isDown: BOOLEAN; m: SET;
	BEGIN
		x0 := x; y0 := y;
		Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.show, dest, destX, destY);
		(* MarkTarget(dest, dest # f); *)
		op := PickMode(f, dest, x, y);
		REPEAT
			(* CheckWindow(TRUE); *)
			f.Input(x1, y1, m, isDown);
			IF (x1 # x) OR (y1 # y) THEN
				Properties.PollPick(x1, y1, f, x0, y0, Properties.noMark, Properties.show, d, dx, dy);
				IF (d # dest) OR (dx # destX) OR (dy # destY) THEN
					d0 := dest;
					(* MarkTarget(dest, (dest # f) & (d # d0)); *)
					Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.hide, dest, destX, destY);
					x := x1; y := y1;
					Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.show, d, dx, dy);
					dest := d; destX := dx; destY := dy;
					(* MarkTarget(dest, (dest # f) (* ~home *) & (d # d0)); *)
				ELSE
					Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.hide, d, dx, dy);
					x := x1; y := y1;
					Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.show, d, dx, dy)
				END
			END;
			op := PickMode(f, dest, x, y)
		UNTIL ~isDown OR (op = escape);
		Properties.PollPick(x, y, f, x0, y0, Properties.mark, Properties.hide, d, dx, dy);
		IF op = escape THEN
			REPEAT f.Input(x, y, m, isDown) UNTIL ~isDown;
			op := Mechanisms.cancelPick
		END;
		(* MarkTarget(dest, dest # f); *)
		(* CheckWindow(FALSE) *)
		buttons := {}
	END TrackToPick;


	PROCEDURE (hook: Hook) PopUpAndSelect* (f: Views.Frame;
										n, this: INTEGER;
										string: ARRAY OF ARRAY OF CHAR;
										enabled, checked: ARRAY OF BOOLEAN;
										VAR i: INTEGER;
										VAR x, y: INTEGER; VAR buttons: SET);
		VAR res, j: INTEGER; menu, wnd: WinApi.HANDLE; pt: WinApi.POINT;
			t: ARRAY 256 OF CHAR; s: SET;
	BEGIN
		ASSERT(0 < n, 20); ASSERT(n <= LEN(string), 21);
		ASSERT(LEN(enabled) = LEN(string), 22);
		ASSERT(LEN(checked) = LEN(string), 23);
		wnd := f.rider(HostPorts.Rider).port.wnd;
		ASSERT(wnd # 0, 100);
		menu := WinApi.CreatePopupMenu(); j := 0;
		WHILE j < n DO
			IF string[j] = "-" THEN
				res := WinApi.AppendMenuW(menu, WinApi.MF_SEPARATOR, 0, NIL)
			ELSE
				Dialog.MapString(string[j], t);
				res := WinApi.AppendMenuW(menu, {}, 32000 + j, t);
				IF ~enabled[j] THEN
					res := WinApi.EnableMenuItem(menu, 32000 + j, WinApi.MF_GRAYED)
				ELSIF checked[j] THEN
					res := WinApi.CheckMenuItem(menu, 32000 + j, WinApi.MF_CHECKED)
				END
			END;
			INC(j)
		END;
		pt.x := (x + f.gx) DIV f.unit; pt.y := (y + f.gy) DIV f.unit;
		res := WinApi.ClientToScreen(wnd, pt);
		s := {1, 2};	(* track right, center align *)
		res := WinApi.TrackPopupMenu(menu, s, pt.x, pt.y + 2, 0, wnd, NIL);
		res := WinApi.DestroyMenu(menu)
	END PopUpAndSelect;


	PROCEDURE Init*;
		VAR h: Hook;
	BEGIN
		InitNative; NEW(h); Mechanisms.SetHook(h)
	END Init;

BEGIN
	Init
END WinMechanisms.
