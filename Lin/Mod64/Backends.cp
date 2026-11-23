MODULE LinBackends;
(**	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		SYSTEM, Fenv := LinFenv, IO := LinGtk4IO, O := LinGtk2GObject, Views, Ports, Dialog, Controllers, Meta, Services, 
		Windows, StdWindows, StdDialog, StdMenuTool, 
		HostPorts := LinPorts, LinFileDrops, Cairo := LinCairo, 
		GLib := LinGtk2GLib, Gdk := LinGtk4Gdk, 
		Gtk := LinGtk4Gtk, GtkU := LinGtk4Util, Key := LinGtk2Keysyms;

	CONST
		inPlace* = 31;	(* flag for in place windows *)	(* AD: OLE-related flag: inPlace windows are drawn in another application *)
		
		(* these constants need to be in sync with StdWindows *)
			SCRLCK = 0D801X;
			ENTER = 0DX; ESC = 1BX;
			TAB = 09X; LTAB = 0AX; RDEL = 07X; LDEL = 08X;
			PL = 10X; PR = 11X; PU = 12X; PD = 13X;
			DL = 14X; DR = 15X; DU = 16X; DD = 17X;
			AL = 1CX; AR = 1DX; AU = 1EX; AD = 1FX;
			WL = 5; WR = 6;	(* wheel left/right; picks up numbering after Controllers.gotoPos *)
		
		press = 0; move = 1;	(* values for the type parameter for HandleMouse *)
		debug = FALSE;

	TYPE
		Pixels = INTEGER;
		Backend* = POINTER TO RECORD(StdWindows.WindowBackend)
			wnd-: Gtk.GtkWindow; (* gtk window *)
			da: Gtk.GtkDrawingArea; 	(* gtk drawing area *)
			fixed: Gtk.GtkFixed;	(* .fixed is used in HostCFrames: GTK widgets are inserted into it *)
			scrollX,scrollY: REAL;
			resizing: BOOLEAN;	(* flags an ongoing resize operation; prevents recursive *)
		END;
		BackendDirectory = POINTER TO RECORD (StdWindows.BackendDirectory)
		END;
	
		CounterAction = POINTER TO RECORD (Services.Action) END;
		
		SignalHandler = ABSTRACT RECORD (Services.SafeAction)
			wb: Backend;
			res: INTEGER
		END;
		
		ExposeHandler = RECORD (SignalHandler)
			l, t, r, b: Pixels;
			eventCount: INTEGER
		END;
		
		PaintHandler = RECORD (SignalHandler)
			cr: Cairo.cairo_t;
			width, height: Pixels
		END;

		FocusInHandler = RECORD (SignalHandler) END;
		
		MouseHandler = RECORD (SignalHandler)
			what, type, button, x, y: INTEGER;
			ticks: LONGINT;
			state: SET
		END;
		
		ConfigureHandler = RECORD (SignalHandler)
			width, height: Pixels
		END;
		
		KeyEvent = RECORD type,keyval,hardware_keycode: INTEGER; state: SET END;

		KeyPressHandler = RECORD (SignalHandler)
			state: SET;
			event: POINTER TO KeyEvent
		END;
		
		ScrollHandler = RECORD (SignalHandler)
			direction,steps: INTEGER;
			x, y: Pixels;
			state: SET
		END;
		
		DeleteHandler = RECORD (SignalHandler) END;

		EventHandler = RECORD (SignalHandler) controller: Gtk.GtkWidget; event: SYSTEM.ADDRESS END;
		
		Ext = POINTER TO RECORD (StdWindows.Ext) END;
		
		
		ShortcutMatcher = RECORD (StdMenuTool.ShortcutMatcher)
			mod: SET;
			len: INTEGER;
			keyvals: POINTER TO ARRAY OF INTEGER;
		END;

	VAR
		scW-, scH-: INTEGER;	(** screen width and height *)
		first-: Gtk.GtkWindow;
		unit: INTEGER;
		latter: Backend;
		latterSentinel: CounterAction;
		prevPressTime: LONGINT;
		prevX, prevY: Pixels;
		
	(* Backend *)
	
	PROCEDURE (wb: Backend) SetTitle* (IN title: Views.Title);
		VAR us: GLib.PString;
	BEGIN
		us := GLib.g_utf16_to_utf8(title, -1, NIL, NIL, NIL);
		Gtk.gtk_window_set_title(wb.wnd, us);
		GLib.g_free(SYSTEM.VAL(GLib.gpointer, us));
	END SetTitle;

	PROCEDURE (wb: Backend) GetOrigin* (OUT xRoot, yRoot: Pixels): BOOLEAN;
	BEGIN RETURN GtkU.GetOrigin(wb.da,xRoot,yRoot)
	END GetOrigin;
	
	PROCEDURE (wb: Backend) Close-;
	BEGIN
		IF wb.wnd # NIL THEN
			Gtk.gtk_window_set_focus(wb.wnd, NIL);
			Gtk.gtk_window_destroy(wb.wnd);
			wb.wnd := NIL
		END
	END Close;
	
	PROCEDURE TrySignalHandler (VAR h: SignalHandler): INTEGER;
	VAR native: Fenv.Environment;
	BEGIN
		GtkU.EnterManaged(native);
		h.res := 28051978; Services.Try(h); IF h.res = 28051978 THEN h.res := 1 END;
		GtkU.LeaveManaged(native); RETURN h.res
	END TrySignalHandler;

	PROCEDURE (VAR h: DeleteHandler) Do;
	BEGIN 
		IF (h.wb # NIL) & (h.wb.wnd # NIL) THEN
			ASSERT(StdWindows.guardedCloseHook # NIL, 20);
			StdWindows.guardedCloseHook(h.wb.win)
		END
	END Do;
	
	(* Procedures relating to activation/deactivation of a window *)
	
	PROCEDURE (a: CounterAction) Do;
	BEGIN latter := NIL
	END Do;
	
	PROCEDURE (w: Backend) Select*;
	BEGIN 
		 IF latter # w THEN latter := w; 
			Gtk.gtk_window_present(w.wnd);
			IF 0 = Gtk.gtk_window_is_active(w.wnd) THEN Services.DoLater(latterSentinel, Services.now) END
		END
	END Select;

	PROCEDURE (VAR h: FocusInHandler) Do;
	BEGIN 
		IF latter = NIL THEN 
			latter := h.wb; IF h.wb.win # NIL THEN
				Windows.dir.Select(h.wb.win, Windows.lazy)
			END; latter := NIL
		ELSIF
			latter = h.wb THEN latter := NIL; Services.RemoveAction(latterSentinel)
		END 
	END Do;
	
	PROCEDURE ValidShortcutKey (k: INTEGER): BOOLEAN;
	BEGIN
		RETURN (k >= Key.GDK_F1) & (k <= Key.GDK_F12)
			OR (k = Key.GDK_Insert) OR (k = Key.GDK_Home)
			OR (k = Key.GDK_Favourites) OR (k = Key.GDK_Back)
			OR (k = Key.GDK_Forward) OR (k = Key.GDK_Menu) OR (k = Key.GDK_Delete) 
	END ValidShortcutKey;
	
	PROCEDURE EncodeShortcut (IN shortcut: ARRAY OF CHAR; OUT code: INTEGER);
		VAR ch, nch: CHAR; n, j: INTEGER; 
	BEGIN
		code := -1; ch := shortcut[0]; nch := shortcut[1]; j := 1;
		IF (ch = "F") & ('0' <= nch) & (nch <= '9') THEN
			n := 0;
			WHILE (nch >= "0") & (nch <= "9") DO
				n := 10 * n + ORD(nch) - ORD("0"); INC(j); nch := shortcut[j]
			END;
			IF (n >= 1) & (n <= 16) THEN code := Key.GDK_F1 - 1 + n END
		ELSIF shortcut = "Ins" THEN code := Key.GDK_Insert
		ELSIF shortcut = "Del" THEN code := Key.GDK_Delete
		ELSIF shortcut = "Home" THEN code := Key.GDK_Home
		ELSIF shortcut = "Fav" THEN code := Key.GDK_Favourites
		ELSIF shortcut = "Back" THEN code := Key.GDK_Back
		ELSIF shortcut = "Fwd" THEN code := Key.GDK_Forward
		ELSIF shortcut = "Apps" THEN code := Key.GDK_Menu
		END;
		IF ~ValidShortcutKey(code) THEN code := -1 END
	END EncodeShortcut;

	PROCEDURE (VAR m: ShortcutMatcher) Match (_: StdDialog.Item; code: INTEGER; mod: SET): BOOLEAN;
		VAR i: INTEGER;
	BEGIN 
		i := 0; WHILE (i < m.len) & ~((m.keyvals[i] = code) & (m.mod = mod)) DO INC(i) END;
		RETURN i < m.len
	END Match;
	
	PROCEDURE (VAR h: KeyPressHandler) Do;
		VAR ok, done, modify: BOOLEAN; kv, code: INTEGER; ch: CHAR; it: StdDialog.Item;
			b, b0: SET;	(* ~modify => b0 undefined; modify => b0 = b - {Controls.modify} *)
			m: ShortcutMatcher; keys: SYSTEM.ADDRESS; values: Gdk.NativeKeyvals; count,i,result: INTEGER; 
	BEGIN
		done := FALSE;
		IF h.event.type = Gdk.GDK_KEY_PRESS THEN
			m.mod := {};
			IF Gdk.GDK_SHIFT_BIT IN h.event.state THEN INCL(m.mod, Controllers.extend) END;
			IF Gdk.GDK_CONTROL_BIT IN h.event.state THEN INCL(m.mod, Controllers.modify) END;
			IF Gdk.GDK_MOD1_BIT IN h.event.state THEN INCL(m.mod, Controllers.pick) END;
			kv := h.event.keyval;
			IF (m.mod - { Controllers.extend } # {}) OR ValidShortcutKey(kv) THEN
				keys := 0; values := NIL; count := 0;
				result := Gdk.gdk_display_map_keycode(Gdk.gdk_display_get_default(),h.event.hardware_keycode,keys,values,count);
				IF (result = 0) OR (count < 0) THEN count := 0 END;
				NEW(m.keyvals,count+2);
				FOR i := 0 TO count-1 DO m.keyvals[i] := values[i] END;
				m.keyvals[count] := kv; m.keyvals[count+1] := Gdk.gdk_keyval_to_lower(kv); m.len := count+2;
				IF keys # 0 THEN GLib.g_free(keys) END;
				IF values # NIL THEN GLib.g_free(SYSTEM.VAL(GLib.gpointer,values)) END;
				it := StdMenuTool.dir.FirstMatch(m);
				IF it # NIL THEN StdMenuTool.dir.Execute(it); done := TRUE END
			END
		END;
		IF ~done THEN
			b := {}; 
			IF Gdk.GDK_SHIFT_BIT IN h.state THEN b := b + {HostPorts.shift, Controllers.extend} END;
			IF Gdk.GDK_CONTROL_BIT IN h.state THEN 
				b0 := b + {HostPorts.ctrl}; b := b0 + {Controllers.modify}; modify := TRUE
			ELSE modify := FALSE
			END;
			IF Gdk.GDK_MOD1_BIT IN h.state THEN INCL(b, HostPorts.alt); INCL(b, Controllers.pick) END;
			IF Gdk.GDK_MOD5_BIT IN h.state THEN INCL(b, StdWindows.scrollLock); 
			END;
			
			code := h.event.keyval; ch := 0X;
			CASE code OF 
			| Key.GDK_Pause:
			| Key.GDK_Scroll_Lock: ch := SCRLCK
			| Key.GDK_Shift_L..Key.GDK_Caps_Lock:
			| Key.GDK_F1 .. Key.GDK_F12: 
			| Key.GDK_Delete: ch := RDEL	(* delete -> right delete *)
			| Key.GDK_BackSpace: ch := LDEL	(* backspace -> left delete *)
			| Key.GDK_Tab:  
				IF Controllers.extend IN b THEN ch := LTAB; EXCL(b, Controllers.extend) 	(* left tab *)
				ELSE ch := TAB	(* right tab *)
				END
			| Key.GDK_Return, Key.GDK_KP_Enter, Key.GDK_ISO_Enter: ch := ENTER
			| Key.GDK_Escape: ch := ESC
			| Key.GDK_Page_Up: IF modify THEN ch := PL; b := b0 ELSE ch := PU END
			| Key.GDK_Page_Down: IF modify THEN ch := PR; b := b0 ELSE ch := PD END
			| Key.GDK_End: IF modify THEN ch := DD; b := b0 ELSE ch := DR END
			| Key.GDK_Home: IF modify THEN ch := DU; b := b0 ELSE ch := DL END
			| Key.GDK_Left: ch := AL
			| Key.GDK_Up: ch := AU
			| Key.GDK_Right: ch := AR
			| Key.GDK_Down: ch := AD
			ELSE
				code := Gdk.gdk_keyval_to_unicode(code); 
				IF (code >= 20H) & (code # 7FH) THEN ch := CHR(code) END
			END;
			IF ch # 0X THEN h.wb.win.KeyDown(ch, b) END
		END
	END Do;

	PROCEDURE ToModifiers (gtkState: SET; VAR modifiers: SET); 	(** translate GTK state to BB mdfiers *)
		VAR s, m: SET;
	BEGIN s := gtkState; m := modifiers;	(* AD: looove compact texts ;) *)
		IF Gdk.GDK_SHIFT_BIT IN s THEN INCL(m, HostPorts.shift); INCL(m, Controllers.extend) END;
		IF Gdk.GDK_CONTROL_BIT IN s THEN INCL(m, HostPorts.ctrl); INCL(m, Controllers.modify) END;
		IF Gdk.GDK_MOD1_BIT IN s THEN INCL(m, HostPorts.alt); INCL(m, Controllers.pick) END;
		modifiers := m;
	END ToModifiers;
	
	PROCEDURE UpdateCursor (w: StdWindows.Window; x, y: Pixels; modifiers: SET);
		VAR pw, ph: INTEGER; msg: Controllers.PollCursorMsg; cur: INTEGER;
	BEGIN
		w.GetSize(pw, ph);
		IF (x >= 0) & (x < pw) & (y >= 0) & (y < ph) THEN
			msg.x := x * w.frame.unit - w.frame.gx; 
			msg.y := y * w.frame.unit - w.frame.gy; 
			msg.cursor := Ports.arrowCursor;
			msg.modifiers := modifiers;
			(* does this ? need to be bracketed by Controllers.SetCurrentPath/ResetCurrentPath? *)
			w.ForwardCtrlMsg(msg); cur := msg.cursor
		ELSE cur := Ports.arrowCursor
		END;
		IF cur >= 0 THEN w.frame.SetCursor(cur) END
	END UpdateCursor;

	PROCEDURE (VAR h: MouseHandler) Do;
		VAR isDown: BOOLEAN; x, y: Pixels; b: SET;
	BEGIN
		IF h.what # press THEN HostPorts.PollPointer(h.wb.da,h.x,h.y,b) END;
		b := {};
		IF h.button = 1 THEN INCL(b, HostPorts.left) END;
		IF h.button = 2 THEN INCL(b, HostPorts.middle); INCL(b, Controllers.pick) END;
		IF h.button = 3 THEN INCL(b, HostPorts.right); INCL(b, Controllers.popup) END;
		isDown := b # {};
		IF isDown THEN
			ToModifiers(h.state, b);
			IF (h.ticks - prevPressTime < 300)
				& (ABS(prevX - h.x) < 3 ) & (ABS(prevY - h.y) < 3) THEN
				INCL(b, Controllers.doubleClick);
				prevPressTime := h.ticks - 400 (* delay *)
			ELSE
				prevPressTime := h.ticks
			END;
			prevX := h.x;
			prevY := h.y;
		END;
		
		x := h.x; y := h.y;
		HostPorts.SetMouseState(x, y, b, isDown);
		IF h.what = press THEN StdWindows.Track (h.wb.win, x, y, b) END;
		IF h.wb.win # NIL THEN UpdateCursor(h.wb.win, x, y, b) END; (* window could have been closed by Windows.Track *)
		(* IF ~isDown THEN activated := FALSE END; this looks like part of implementing behaviour when a click over an inactive window would activate the window, but would not be forwarded into the window. This is how it is now in Win32. In effect, you click in an inactive text with selection, and the window is activated, but the selection is not unselected; I personally like it when the click does both: activates and then is forwarded into the window, which makes the selection disappear and caret be positioned at click: it makes for less clicking around :)  *)
	END Do;
	
	PROCEDURE (wb: Backend) SetSize- (width, height: Pixels);
	BEGIN
		wb.win.port.SetSize(width, height);
		IF (width > 0) & (height > 0) & ~wb.resizing THEN
			Gtk.gtk_window_set_default_size(wb.wnd,width,height)
		END
	END SetSize;

	PROCEDURE (VAR h: ConfigureHandler) Do;
		VAR wb: Backend; wid,hei: Pixels;
	BEGIN
		wb := h.wb;
		IF (wb.win # NIL) & (wb.win.port # NIL) THEN
			wb.win.port.GetSize(wid,hei);
			IF (wid # h.width) OR (hei # h.height) THEN
				wb.resizing := TRUE; wb.win.SetSize(h.width,h.height); wb.resizing := FALSE
			END
		END;
		h.res := 0
	END Do;

	PROCEDURE (VAR h: ExposeHandler) Do;
	BEGIN
		(* debug
		Con.String("ExposeHandler.Handle: "); Con.String(h.wb.win.title);
		Con.Int(h.eventCount); Con.Int(h.l); Con.Int(h.t); Con.Int(h.r); Con.Int(h.b); Con.Ln 
		*)
		StdWindows.UpdateIn(h.wb.win, h.l, h.t, h.r, h.b, Views.keepFrames);
		IF h.eventCount = 0 THEN StdWindows.Validate(h.wb.win) END
	END Do;
	
	PROCEDURE (VAR h: ScrollHandler) Do;
		VAR op,i: INTEGER; mod: SET;
	BEGIN 
		CASE h.direction OF Gdk.GDK_SCROLL_UP: op := Controllers.decLine
		| Gdk.GDK_SCROLL_DOWN: op := Controllers.incLine
		| Gdk.GDK_SCROLL_LEFT: op := WL
		| Gdk.GDK_SCROLL_RIGHT: op := WR
		ELSE h.res := 0; RETURN
		END;
		mod := {}; ToModifiers(h.state,mod);
		FOR i := 1 TO h.steps DO StdWindows.Wheel(h.wb.win,op,h.x,h.y,mod) END;
		h.res := 1
	END Do;

	PROCEDURE (VAR paint: PaintHandler) Do;
		VAR h: ExposeHandler; resize: ConfigureHandler; p: HostPorts.Port; res: INTEGER;
	BEGIN
		h.wb := paint.wb;
		IF (h.wb.win = NIL) OR (h.wb.win.port = NIL) THEN RETURN END;
		resize.wb := h.wb; resize.width := paint.width; resize.height := paint.height; res := TrySignalHandler(resize);
		p := h.wb.win.port(HostPorts.Port);
		IF p.TakeInvalid(h.l,h.t,h.r,h.b) THEN
			h.eventCount := 0; p.SetPainting(TRUE); res := TrySignalHandler(h); p.SetPainting(FALSE)
		END;
		p.Paint(paint.cr); paint.res := 1
	END Do;

	PROCEDURE [ccall] DrawSignal (widget: Gtk.GtkWidget; cr: Cairo.cairo_t; width,height: INTEGER; data: SYSTEM.ADDRESS);
		VAR h: PaintHandler; res: INTEGER;
	BEGIN
		(* Paint also changes the i386 FPU control word. Restore the native environment
		   after the complete callback, including presentation of the backing surface. *)
		h.wb := SYSTEM.VAL(Backend,data); h.cr := cr; h.width := width; h.height := height;
		res := TrySignalHandler(h)
	END DrawSignal;

	PROCEDURE HandleEvent (controller: Gtk.GtkWidget; event,data: SYSTEM.ADDRESS): INTEGER;
		VAR wb: Backend; mouse: MouseHandler; key: KeyPressHandler; scroll: ScrollHandler;
			type,res,direction: INTEGER; x,y,dx,dy: REAL; state: SET;
	BEGIN
		wb := SYSTEM.VAL(Backend,data); IF wb.win = NIL THEN RETURN 0 END;
		type := Gdk.gdk_event_get_event_type(event); state := Gdk.gdk_event_get_modifier_state(event);
		x := 0; y := 0;
		IF (type = 1) OR (type = 2) THEN res := Gdk.gdk_event_get_position(event,x,y) END;
		IF HostPorts.tracking THEN
			IF (type = 4) & (Gdk.gdk_key_event_get_keyval(event) = 0FF1BH) THEN HostPorts.CancelTracking END;
			RETURN 1
		END;
		IF type = 2 THEN
			mouse.wb := wb; mouse.what := press; mouse.type := type;
			mouse.button := Gdk.gdk_button_event_get_button(event);
			mouse.x := SHORT(ENTIER(x)); mouse.y := SHORT(ENTIER(y)); mouse.state := state;
			mouse.ticks := Gdk.gdk_event_get_time(event);
			HostPorts.tracking := TRUE; res := TrySignalHandler(mouse); HostPorts.tracking := FALSE;
			RETURN res
		ELSIF type = 1 THEN
			mouse.wb := wb; mouse.what := move; mouse.type := type;
			mouse.x := SHORT(ENTIER(x)); mouse.y := SHORT(ENTIER(y)); mouse.state := state;
			RETURN TrySignalHandler(mouse)
		ELSIF type = 4 THEN
			key.wb := wb; key.state := state; NEW(key.event); key.event.type := Gdk.GDK_KEY_PRESS;
			key.event.state := state; key.event.keyval := Gdk.gdk_key_event_get_keyval(event);
			key.event.hardware_keycode := Gdk.gdk_key_event_get_keycode(event);
			RETURN TrySignalHandler(key)
		ELSIF type = 15 THEN
			HostPorts.PollPointer(wb.da,scroll.x,scroll.y,state);
			dx := 0; dy := 0; direction := Gdk.gdk_scroll_event_get_direction(event);
			IF direction = Gdk.GDK_SCROLL_UP THEN dy := -1
			ELSIF direction = Gdk.GDK_SCROLL_DOWN THEN dy := 1
			ELSIF direction = Gdk.GDK_SCROLL_LEFT THEN dx := -1
			ELSIF direction = Gdk.GDK_SCROLL_RIGHT THEN dx := 1
			ELSE Gdk.gdk_scroll_event_get_deltas(event,dx,dy) END;
			wb.scrollX := wb.scrollX+dx; wb.scrollY := wb.scrollY+dy;
			scroll.wb := wb; scroll.state := state;
			scroll.steps := MIN(32,SHORT(ENTIER(ABS(wb.scrollY))));
			IF scroll.steps > 0 THEN
				IF wb.scrollY < 0 THEN scroll.direction := Gdk.GDK_SCROLL_UP; wb.scrollY := wb.scrollY+scroll.steps
				ELSE scroll.direction := Gdk.GDK_SCROLL_DOWN; wb.scrollY := wb.scrollY-scroll.steps END;
				res := TrySignalHandler(scroll)
			END;
			scroll.steps := MIN(32,SHORT(ENTIER(ABS(wb.scrollX))));
			IF scroll.steps > 0 THEN
				IF wb.scrollX < 0 THEN scroll.direction := Gdk.GDK_SCROLL_LEFT; wb.scrollX := wb.scrollX+scroll.steps
				ELSE scroll.direction := Gdk.GDK_SCROLL_RIGHT; wb.scrollX := wb.scrollX-scroll.steps END;
				res := TrySignalHandler(scroll)
			END;
			RETURN 1
		END;
		RETURN 0
	END HandleEvent;

	PROCEDURE (VAR h: EventHandler) Do;
	BEGIN h.res := HandleEvent(h.controller,h.event,SYSTEM.VAL(SYSTEM.ADDRESS,h.wb)) END Do;

	PROCEDURE [ccall] EventSignal (controller: Gtk.GtkWidget; event,data: SYSTEM.ADDRESS): INTEGER;
		VAR h: EventHandler;
	BEGIN
		h.wb := SYSTEM.VAL(Backend,data); h.controller := controller; h.event := event;
		RETURN TrySignalHandler(h)
	END EventSignal;

	PROCEDURE [ccall] CloseSignal (widget: Gtk.GtkWidget; data: SYSTEM.ADDRESS): INTEGER;
		VAR h: DeleteHandler;
	BEGIN h.wb := SYSTEM.VAL(Backend,data); RETURN TrySignalHandler(h) END CloseSignal;

	PROCEDURE [ccall] FocusSignal (controller: Gtk.GtkWidget; data: SYSTEM.ADDRESS);
		VAR h: FocusInHandler; res: INTEGER;
	BEGIN h.wb := SYSTEM.VAL(Backend,data); res := TrySignalHandler(h) END FocusSignal;

	PROCEDURE ConnectSignals (wb: Backend);
		VAR id,a: SYSTEM.ADDRESS; controller: Gtk.GtkWidget;
	BEGIN
		a := SYSTEM.VAL(SYSTEM.ADDRESS,wb);
		LinFileDrops.Install(wb.da);
		id := GtkU.gtk_signal_connect(wb.wnd,"close-request",SYSTEM.ADR(CloseSignal),a);
		controller := Gtk.gtk_event_controller_focus_new();
		id := GtkU.gtk_signal_connect(controller,"enter",SYSTEM.ADR(FocusSignal),a);
		Gtk.gtk_widget_add_controller(wb.da,controller);
		controller := Gtk.gtk_event_controller_legacy_new();
		id := GtkU.gtk_signal_connect(controller,"event",SYSTEM.ADR(EventSignal),a);
		Gtk.gtk_widget_add_controller(wb.da,controller);
		Gtk.gtk_drawing_area_set_draw_func(wb.da,SYSTEM.ADR(DrawSignal),a,0)
	END ConnectSignals;

	PROCEDURE (wb: Backend) Port* (): Ports.Port;
		VAR p: HostPorts.Port; res: Ports.Port;
	BEGIN 
		IF (wb.win # NIL) & (wb.win.port # NIL) THEN res := wb.win.port
		ELSE NEW(p); p.Init(unit, FALSE); p.SetSize(0, 0); p.SetDA(wb.da); p.SetFW(wb.fixed); res := p
		END;
		RETURN res
	END Port;

	PROCEDURE (wb: Backend) Open- (isDialog: BOOLEAN);
		VAR width,height: INTEGER;
	BEGIN
		ASSERT(wb.win # NIL, 20); ASSERT(wb.win.port # NIL, 21);
		wb.win.port.GetSize(width,height);
		Gtk.gtk_window_set_default_size(wb.wnd,MAX(1,width),MAX(1,height));
		IF isDialog THEN Gtk.gtk_window_set_resizable(wb.wnd,0)
		ELSE Gtk.gtk_window_set_resizable(wb.wnd,1) END;
		Gtk.gtk_window_present(wb.wnd)
	END Open;


	(* BackendDirectory *)
	
	PROCEDURE (d: BackendDirectory) NewBackend* (): Backend;
		VAR wb: Backend; res: INTEGER;
	BEGIN
		NEW(wb); wb.resizing := FALSE; wb.scrollX := 0; wb.scrollY := 0;
		wb.wnd := Gtk.gtk_window_new(); wb.da := Gtk.gtk_drawing_area_new();
		Gtk.gtk_widget_set_focusable(wb.da,1); Gtk.gtk_window_set_child(wb.wnd,wb.da);
		ConnectSignals(wb);
		IF first = NIL THEN first := wb.wnd; Dialog.Call("LinClipboard.SetSignals","",res) END;
		Gtk.gtk_window_set_focus(wb.wnd,wb.da);
		RETURN wb
	END NewBackend;

	PROCEDURE (d: BackendDirectory) GetBounds* (OUT w,h: Pixels);
		VAR monitors,monitor: SYSTEM.ADDRESS; rect: Gdk.GdkRectangle;
	BEGIN
		w := 1024; h := 768; monitors := IO.g_list_model_get_item(Gdk.gdk_display_get_monitors(Gdk.gdk_display_get_default()),0);
		IF monitors # 0 THEN
			Gdk.gdk_monitor_get_geometry(monitors,rect); w := rect.width; h := rect.height;
			O.g_object_unref(SYSTEM.VAL(O.GObject,monitors))
		END;
		scW := w; scH := h
	END GetBounds;

	PROCEDURE (e: Ext) GetNumHeads (VAR count: INTEGER);
	BEGIN count := IO.g_list_model_get_n_items(Gdk.gdk_display_get_monitors(Gdk.gdk_display_get_default())) END GetNumHeads;

	PROCEDURE (e: Ext) ToHead (wb: StdWindows.WindowBackend; head: INTEGER);
	BEGIN
	END ToHead;

	PROCEDURE (e: Ext) Maximize (wb: StdWindows.WindowBackend);
	BEGIN WITH wb: Backend DO Gtk.gtk_window_maximize(wb.wnd) ELSE END
	END Maximize;


	(* Command and aux procedures *)

	PROCEDURE Dump (p: ANYPTR);
		TYPE P = PROCEDURE(a: LONGINT; t: ARRAY OF CHAR);	(* 64-Ð±Ð¸Ñ‚: Ð°Ð´Ñ€ÐµÑ *)
			V = RECORD (Meta.Value) p: P END;
		VAR i: Meta.Item; ok: BOOLEAN; v: V;
	BEGIN
		IF p # NIL THEN Meta.Lookup("DevDebug", i);
			IF i.obj = Meta.modObj THEN i.Lookup("ShowHeapObject", i);
				IF i.obj = Meta.procObj THEN i.GetVal(v, ok);
					IF ok THEN v.p(SYSTEM.VAL(LONGINT, p), "") END
				END
			END
		END
	END Dump;


	PROCEDURE Init;
		VAR dir: BackendDirectory; ext: Ext;
	BEGIN
		NEW(dir); StdWindows.SetBackendDir(dir);
		dir.GetBounds(scW, scH);
		unit := SHORT(ENTIER((Ports.point DIV 4 * 3 * 100) / Dialog.scaleFactor));
		NEW(latterSentinel);
		StdWindows.SetDumper(Dump);
		NEW(ext); StdWindows.ext := ext;
		StdMenuTool.encodeShortcut := EncodeShortcut;
	END Init;

BEGIN
	Init

CLOSE 
	IF SYSTEM.VAL(INTEGER, StdMenuTool.encodeShortcut) = SYSTEM.ADR(EncodeShortcut) THEN
		StdMenuTool.encodeShortcut := NIL 
	END
END LinBackends.
