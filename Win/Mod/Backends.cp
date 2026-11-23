MODULE WinBackends;
	
	IMPORT SYSTEM, Ports, Views, Services, Controllers, Containers, 
		StdWindows, StdMenuTool, StdDialog, WinGui, Files, Converters, 
		Dialog, Windows, WinApi, WinPorts, WinMechanisms, WinDialog, StdLog;
	
	CONST
		inPlace* = 31;	(* flag for OLE in place windows; not sure it's needed or rightly placed *)

		ENTER = 0DX; ESC = 1BX;
		TAB = 09X; LTAB = 0AX; RDEL = 07X; LDEL = 08X;
		PL = 10X; PR = 11X; PU = 12X; PD = 13X;
		DL = 14X; DR = 15X; DU = 16X; DD = 17X;
		AL = 1CX; AR = 1DX; AU = 1EX; AD = 1FX;
		
		dbl = 7; click = 8; hover = 9; wheel = 10;	(* additional constants for MouseHandler.flags *)

		VK_BROWSER_FAVORITES = 0ABH; VK_BROWSER_BACK = 0A6H;
		VK_BROWSER_FORWARD = 0A7H;
		
	TYPE
		Backend = POINTER TO RECORD (StdWindows.WindowBackend)
			wnd: WinApi.HANDLE;
		END;
		
		BackendDirectory = POINTER TO RECORD (StdWindows.BackendDirectory)
		END;
		
		ShortcutMatcher = RECORD (StdMenuTool.ShortcutMatcher)
			mod: SET;
			code: INTEGER;
		END;
		
		Pixels = INTEGER;

		PaintHandler = RECORD (Services.SafeAction) 
			win: StdWindows.Window;
			wnd: WinApi.HANDLE;
		END;
		
		SizeHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window;
			w, h: INTEGER
		END;
		
		CreateHandler = RECORD (Services.SafeAction)
			wnd: WinApi.HANDLE;
			createStruct: WinApi.PtrCREATESTRUCTW
		END;
		
		CharHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window;
			char: CHAR;
			flags: SET
		END;
		
		KeyHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window;
			code: INTEGER;
			keystrokeFlags: SET;
		END;
		
		MouseHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window;
			flags: SET;
			x, y, wheel: INTEGER
		END;
		
		FocusHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window;
			on: BOOLEAN;
		END;
		
		CancelRefocusAction = POINTER TO RECORD (Services.Action)
			in, out: StdWindows.Window
		END;
		
		CloseHandler = RECORD (Services.SafeAction)
			win: StdWindows.Window
		END;
		
		DropFilesHandler = RECORD (Services.SafeAction)
			wnd, drop: WinApi.HANDLE
		END;
		
	VAR
		docIcon, dirtyIcon: WinApi.HANDLE;
		instance: WinApi.HANDLE;	(* application instance *)
		unit-: INTEGER;	(* resolution of main window *)
		scW-, scH-: INTEGER;	(* screen width and height *)
		dir: BackendDirectory;
		cancelRefocus: CancelRefocusAction;

	PROCEDURE (wb: Backend) SetTitle (IN title: Views.Title);
		VAR _: INTEGER;
	BEGIN _ := WinApi.SetWindowTextW(wb.wnd, title)
	END SetTitle;
	
	PROCEDURE GetOrigin (wnd: WinApi.HANDLE; OUT xRoot, yRoot: Pixels): BOOLEAN;
		VAR point: WinApi.POINT; res: BOOLEAN;
	BEGIN 
		point.x := 0; point.y := 0;
		res := 0 # WinApi.ClientToScreen(wnd, point);
		IF res THEN xRoot := point.x; yRoot := point.y END;
		RETURN res
	END GetOrigin;
		
	PROCEDURE (wb: Backend) GetOrigin* (OUT xRoot, yRoot: Pixels): BOOLEAN;
	BEGIN RETURN GetOrigin(wb.wnd, xRoot, yRoot)
	END GetOrigin;
	
	PROCEDURE (wb: Backend) Close;
		VAR _: INTEGER;
	BEGIN
		WinMechanisms.RemoveDropTarget(wb.wnd);
		_ := WinApi.DestroyWindow(wb.wnd);
		wb.wnd := 0
	END Close;
	
	PROCEDURE (wb: Backend) Select;
		VAR _: INTEGER;
	BEGIN
		WinGui.mainWnd := wb.wnd;
		_ := WinApi.SetFocus(wb.wnd)
	END Select;
	
	(* Procedures relating to window resizing *)
	PROCEDURE (wb: Backend) SetSize- (width, height: Pixels);
		VAR res_, x, y, dw, dh: INTEGER; cr, wr: WinApi.RECT; p: WinApi.POINT; w: StdWindows.Window;
	BEGIN
		wb.win.port.SetSize(width, height);
		w := wb.win;
		
		IF (w.port # NIL) & (wb.wnd # 0) THEN
			
			IF (width = 0) & (height = 0) THEN
				(*w.minimized := TRUE*)
				(* no SetSize^ call *)
			ELSE
				(*w.minimized := FALSE;*)
				(*w.SetSize^(width, height);*)
				res_ := WinApi.GetClientRect(wb.wnd, cr);
				dw := width - cr.right; dh := height - cr.bottom;
				IF ~(inPlace IN w.flags) & (*& (w # cbViewer)*)  ((dw # 0) OR (dh # 0)) THEN
				
					res_ := WinApi.GetWindowRect(wb.wnd, wr);
					p.x := wr.left; p.y := wr.top;
					(*IF ~(StdWindows.isTool IN w.flags) THEN res := WinApi.ScreenToClient(client, p) END;*)
					x := p.x; y := p.y;
					IF Windows.isTool IN w.flags THEN DEC(x, dw DIV 2); DEC(y, dh DIV 2) END;
					width := wr.right - wr.left + dw; height := wr.bottom - wr.top + dh;
					(*IF w.child THEN
						res_ := WinApi.GetClientRect(client, cr);
						dw := cr.right; dh := cr.bottom
					ELSE*)
						dw := scW; dh := scH
					(*END*);
					IF x + width > dw THEN x := dw - width END;
					IF y + height > dh THEN y := dh - height END;

					IF x < 0 THEN x := 0 END;
					IF y < 0 THEN y := 0 END;

					res_ := WinApi.SetWindowPos(wb.wnd, 0, x, y, width, height,
						WinApi.SWP_FRAMECHANGED + WinApi.SWP_NOZORDER + WinApi.SWP_NOACTIVATE)
				END
			END
		END
		
	END SetSize;
	
	PROCEDURE (wb: Backend) Port (): Ports.Port;
		VAR res: Ports.Port; p: WinPorts.Port;
	BEGIN 
		IF (wb.win # NIL) & (wb.win.port # NIL) THEN res := wb.win.port
		ELSE NEW(p); p.Init(unit, FALSE); p.SetSize(0, 0); res := p
		END;
		RETURN res
	END Port;
	
	PROCEDURE ThisStyle (base, flags: SET): SET;
	BEGIN
		(*IF ~(StdWindows.noHScroll IN flags) THEN INCL(base, 20) END;	(* hor scrollbar *)
		IF ~(StdWindows.noVScroll IN flags) THEN INCL(base, 21) END;	(* ver scrollbar *)*)
		IF ~(Windows.noResize IN flags) THEN INCL(base, 18) END;	(* sizing border *)
		RETURN base
	END ThisStyle;

	PROCEDURE ThisBackend (wnd: WinApi.HANDLE): Backend;
	(** determine backend by its HANDLE *)
	BEGIN
		(*IF wnd = cbViewer.wnd THEN RETURN cbViewer
		ELSE*) RETURN SYSTEM.VAL(Backend, WinApi.GetWindowLongW(wnd, 0))
		(*END*)
	END ThisBackend;

	PROCEDURE (wb: Backend) Open- (isDialog: BOOLEAN);
	(** First (outer) part of backend creation, second (inner) - in CreateHandler.Do *)
		VAR _, l, t, wid, hei, parent: INTEGER; menu, wnd: WinApi.HANDLE; exstyle, style: SET;
			rect: WinApi.RECT;
	BEGIN
		IF Windows.dir.l = StdWindows.none THEN
			l := WinApi.CW_USEDEFAULT 
		ELSE
			l := Windows.dir.l;
			t := Windows.dir.t
		END;
		IF isDialog THEN
			exstyle := {};
			(* exstyle := WinApi.WS_EX_TOPMOST; *)
			style := {7, 19, 22, 23, 31}; 	(* dialog, sysmenu, border, clipchildren, popup *)
		ELSE
			exstyle := {};
			style := {19, 22, 23, (*25,*) 31} + WinApi.WS_MINIMIZEBOX + WinApi.WS_MAXIMIZEBOX	(* dialog, sysmenu, border, clipchildren, popup *)
		END;
		wb.win.GetSize(wid, hei);
		rect.left := 0; rect.top := 0; rect.right := wid; rect.bottom := hei;
		style := ThisStyle(style, wb.win.flags);  (* critical for correct windows sizes *)
		_ := WinApi.AdjustWindowRectEx(rect, style, WinApi.FALSE, exstyle);
		wid := rect.right - rect.left; hei := rect.bottom - rect.top;
		
		IF isDialog THEN
			parent := WinGui.mainWnd
		ELSE
			parent := 0
		END;
		wnd := WinApi.CreateWindowExW(exstyle, "Oberon Doc", "", ThisStyle(style, wb.win.flags), l, t, wid, hei, parent, 0, instance, SYSTEM.VAL(INTEGER, wb));
		wb.wnd := wnd;
		IF isDialog THEN
			menu := WinApi.GetSystemMenu(wnd, 0);
			_ := WinApi.RemoveMenu(menu, 0F000H, {});	(* SC_SIZE *)
			_ := WinApi.RemoveMenu(menu, 0F020H, {});	(* SC_MINIMIZE *)
			_ := WinApi.RemoveMenu(menu, 0F030H, {});	(* SC_MAXIMIZE *)
			_ := WinApi.RemoveMenu(menu, 0F120H, {});	(* SC_RESTORE *)
		END;
		_ := WinApi.ShowWindow(wnd, WinApi.SW_SHOWNOACTIVATE);
		(* SW_SHOWNOACTIVATE shows but doesn't send WM_SETFOCUS => it is up to StdWindows to set proper focus *)
		WinMechanisms.InstallDropTarget(wnd, wb.win);
		 WinApi.DragAcceptFiles(wnd, WinApi.TRUE);
	END Open;
	
	PROCEDURE (VAR h: CreateHandler) Do;
	(** Second part of backend creation, called from window procedure in response to WM_CREATE *)
		VAR wb: Backend; _: INTEGER; wnd: WinApi.HANDLE; hdc: WinApi.HDC;
	BEGIN 
		wnd := h.wnd;
		wb := SYSTEM.VAL(Backend, h.createStruct.lpCreateParams);
		_ := WinApi.SetWindowLongW(wnd, 0, SYSTEM.VAL(INTEGER, wb));
		hdc := WinApi.GetDC(wnd);
		wb.win.port(WinPorts.Port).SetDC(hdc, wnd);
		(*creatingDoc := TRUE;
		Kernel.Try(CreateDoc, wnd, wParam, lParam);
		creatingDoc := FALSE*)
	END Do;
	
	(* Directory *)

	PROCEDURE (d: BackendDirectory) NewBackend* (): Backend;
		VAR wb: Backend; 
	BEGIN NEW(wb); 
		
		;
		RETURN wb
	END NewBackend;

	PROCEDURE (d: BackendDirectory) GetBounds* (OUT w, h: Pixels);
	BEGIN
		w := scW; h := scH
	END GetBounds;

	PROCEDURE (VAR h: PaintHandler) Do;
		VAR _: INTEGER; hdc: WinApi.HDC; w: StdWindows.Window; wnd: WinApi.HANDLE; 
			ps: WinApi.PAINTSTRUCT;
	BEGIN 
		w := h.win; wnd := h.wnd;
		(* clip region must be reset to ensure correct ps.paint ! *)
		_ := WinApi.SelectClipRgn(w.port(WinPorts.Port).dc, 0);
		hdc := WinApi.BeginPaint(wnd, ps);
		w.port(WinPorts.Port).SetDC(hdc, wnd);
		StdWindows.UpdateIn(w, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom, Views.keepFrames);
		_ := WinApi.EndPaint(wnd, ps);
	END Do;
	
	
	
	PROCEDURE (VAR h: SizeHandler) Do;
	BEGIN
		IF ~(inPlace IN h.win.flags) THEN
			h.win.SetSize(h.w, h.h)
		END;
		IF h.win.hook # NIL THEN (*h.win.hook.Resize(h.w, h.h)*) END
	END Do;
	
	PROCEDURE WmSize (wnd: WinApi.HANDLE; wid, hei: INTEGER);
		VAR h: SizeHandler; b: Backend;
	BEGIN 
		b := ThisBackend(wnd); ASSERT(b # NIL, 60);
		h.win := ThisBackend(wnd).win; h.w := wid; h.h := hei; Services.Try(h)
	END WmSize;
	
	PROCEDURE RecalcMainWindow;
		VAR _: INTEGER; rect: WinApi.RECT;
	BEGIN
		IF WinGui.mainWnd # 0 THEN
			_ := WinApi.GetClientRect(WinGui.mainWnd, rect);
			WmSize(WinGui.mainWnd, rect.right, rect.bottom)
		END;
	END RecalcMainWindow;
	
	PROCEDURE KeyModifiers (keystrokeFlags: SET): SET;
		CONST contextCode = 29;
		VAR b: SET;
	BEGIN
		b := {};
		IF WinApi.GetKeyState(10H) < 0 THEN INCL(b, Controllers.extend); INCL(b, WinPorts.shift) END;
		IF WinApi.GetKeyState(11H) < 0 THEN INCL(b, Controllers.modify); INCL(b, WinPorts.ctrl) END;
		IF contextCode IN keystrokeFlags THEN INCL(b, WinPorts.alt); INCL(b, Controllers.pick) END;
		RETURN b
	END KeyModifiers;
	
	PROCEDURE (VAR h: CharHandler) Do;
		VAR mod: SET; ch: CHAR;
	BEGIN ch := h.char;
		IF (ch >= 20X) & (ch # 7FX) THEN
			mod := KeyModifiers(h.flags); (*w.used := TRUE;*)
			IF WinApi.VkKeyScanW(ch) >= 4 * 256 THEN mod := mod - {WinPorts.alt, Controllers.pick} END;
			h.win.KeyDown(ch, mod)
		END;
		Controllers.ResetCurrentPath();
	END Do;
	
	PROCEDURE EncodeShortcut (IN shortcut: ARRAY OF CHAR; OUT code: INTEGER);
		VAR ch, nch: CHAR; n, j: INTEGER; 
	BEGIN
		code := -1; ch := shortcut[0]; nch := shortcut[1]; j := 1;
		IF (ch = "F") & ('0' <= nch) & (nch <= '9') THEN
			n := 0;
			WHILE (nch >= "0") & (nch <= "9") DO
				n := 10 * n + ORD(nch) - ORD("0"); INC(j); nch := shortcut[j]
			END;
			IF (n >= 1) & (n <= 16) THEN code := WinApi.VK_F1 + n - 1 END
		ELSIF shortcut = "Ins" THEN code := WinApi.VK_INSERT
		ELSIF shortcut = "Del" THEN code := WinApi.VK_DELETE
		ELSIF shortcut = "Home" THEN code := WinApi.VK_HOME
		ELSIF shortcut = "Fav" THEN code := VK_BROWSER_FAVORITES
		ELSIF shortcut = "Back" THEN code := VK_BROWSER_BACK
		ELSIF shortcut = "Fwd" THEN code := VK_BROWSER_FORWARD
		ELSIF shortcut = "Apps" THEN code := WinApi.VK_APPS
		ELSE HALT(0)
		END
	END EncodeShortcut;
	
	PROCEDURE ValidShortcutKey (code: INTEGER): BOOLEAN;
	BEGIN
		RETURN ((code >= WinApi.VK_F1) & (code <= WinApi.VK_F24)) OR (code = WinApi.VK_APPS)
		OR (code = WinApi.VK_INSERT) OR (code = WinApi.VK_DELETE) OR (code = WinApi.VK_HOME) OR (code = VK_BROWSER_FAVORITES) OR (code = VK_BROWSER_BACK) OR (code = VK_BROWSER_FORWARD)
	END ValidShortcutKey;
	
	PROCEDURE (VAR m: ShortcutMatcher) Match (_: StdDialog.Item; code: INTEGER; mod: SET): BOOLEAN;
		CONST shortcutModifiers = { Controllers.extend, Controllers.modify, Controllers.pick };
	BEGIN
		RETURN (m.code = code) & (m.mod = mod * shortcutModifiers)
	END Match;
	
	PROCEDURE (VAR h: KeyHandler) Do;
		CONST shortcutModifiers = { Controllers.extend, Controllers.modify, Controllers.pick };
		VAR b, b0: SET; w: StdWindows.Window; pmsg: Controllers.PollFocusMsg; 
			done, modify, scroll: BOOLEAN; c: Containers.Controller; ch: CHAR; m: ShortcutMatcher;
			it: StdDialog.Item;
	BEGIN
		w := h.win; b := KeyModifiers(h.keystrokeFlags); (*w.used := TRUE;*)
		done := FALSE;
		m.mod := shortcutModifiers * b;
		IF (StdMenuTool.dir # NIL) & (m.mod - { Controllers.extend } # {} (*^ or Alt*)) OR ValidShortcutKey(h.code) THEN
			m.code := h.code;
			it := StdMenuTool.dir.FirstMatch(m);
			IF it # NIL THEN StdMenuTool.dir.Execute(it); done := TRUE END
		END;
		IF ~done THEN
			scroll := ODD(WinApi.GetKeyState(91H)) (* scroll lock *);	(* should this scroll behavior go to StdWindows? *)
			pmsg.focus := NIL; w.ForwardCtrlMsg(pmsg);
			IF (pmsg.focus # NIL) & (pmsg.focus.view IS Containers.View) THEN
				c := pmsg.focus.view(Containers.View).ThisController();
				IF (c # NIL) & (Containers.noCaret IN c.opts) THEN scroll := TRUE END
			END;
			IF scroll THEN INCL(b, StdWindows.scrollLock) END;
			modify := Controllers.modify IN b; IF modify THEN b0 := b - { Controllers.modify } END;
			CASE h.code OF
			| 2EH: ch := RDEL	(* delete -> right delete *)
			| 08H: ch := LDEL	(* backspace -> left delete *)
			| 09H: (* tab *)
				IF Controllers.extend IN b THEN ch := LTAB; EXCL(b, Controllers.extend)	(* left tab *)
				ELSE ch := TAB	(* right tab *)
				END
			| 0DH: ch := ENTER	(* enter *)
			| 1BH: ch := ESC	(* escape *)
			| 21H:	(* page up *) IF modify THEN ch := PL; b := b0 ELSE ch := PU END 
			| 22H: (* page down *) IF modify THEN ch := PR; b := b0 ELSE ch := PD END 
			| 23H: (* end *) IF modify THEN ch := DD; b := b0 ELSE ch := DR END 
			| 24H: (* home *) IF modify THEN ch := DU; b := b0 ELSE ch := DL END 
			| 25H: (* left *) ch := AL 
			| 26H: (* up *) ch := AU 
			| 27H: (* right *) ch := AR 
			| 28H: (* down *) ch := AD 
			ELSE ch := 0X
			END;
			IF ch # 0X THEN h.win.KeyDown(ch, b) END
		END
	END Do;
	
	PROCEDURE WmKeydown (wnd: WinApi.HANDLE; wParam, lParam: INTEGER);
		VAR key: KeyHandler; res_: INTEGER;
	BEGIN
		key.win := ThisBackend(wnd).win; key.code := wParam; key.keystrokeFlags := BITS(lParam);
		Services.Try(key);
		IF (wParam = WinApi.VK_TAB)
			OR (wParam = WinApi.VK_LEFT) OR (wParam = WinApi.VK_UP)
			OR (wParam = WinApi.VK_RIGHT) OR (wParam = WinApi.VK_DOWN)
		THEN
			(* this branch is for obsolete WinApi menus and controls *)
			res_ := WinApi.SendMessageW(wnd, 0127H (* WM_CHANGEUISTATE *),
						2 (* UIS_CLEAR *) + 10000H (* UISF_HIDEFOCUS *), 0)
		END
	END WmKeydown;
	
	PROCEDURE (VAR h: FocusHandler) Do;
	BEGIN 
		IF (h.win = NIL) OR (h.win.doc = NIL) THEN	(* window closed since last click *)
		ELSIF h.on THEN Windows.dir.Select(h.win, Windows.lazy) 
		ELSE
			(* StdWindows.Defocus(h.win) *)
		END
	END Do;
	
	PROCEDURE (a: CancelRefocusAction) Do; 
		VAR h: FocusHandler; _: INTEGER;
	BEGIN 
		IF a.in # NIL THEN 
			h.win := a.out; h.on := FALSE; Services.Try(h);
			h.win := a.in; h.on := TRUE; Services.Try(h) 
		END
	END Do;

	PROCEDURE (VAR h: MouseHandler) Do;
		VAR w: StdWindows.Window; isDown: BOOLEAN; op, x, y: INTEGER; b: SET; f, g: Views.Frame;
	BEGIN
		w := h.win; b := {}; (*w.used := TRUE;*)
		IF 0 IN h.flags THEN INCL(b, WinPorts.left) END;
		IF 4 IN h.flags THEN INCL(b, WinPorts.middle); INCL(b, Controllers.pick) END;
		IF 1 IN h.flags THEN INCL(b, WinPorts.right); INCL(b, Controllers.popup) END;
		isDown := b # {};
		IF 2 IN h.flags THEN INCL(b, WinPorts.shift); INCL(b, Controllers.extend) END;
		IF 3 IN h.flags THEN INCL(b, WinPorts.ctrl); INCL(b, Controllers.modify) END;
		IF WinApi.GetAsyncKeyState(12H) < 0 THEN INCL(b, WinPorts.alt); INCL(b, Controllers.pick) END;
		IF dbl IN h.flags THEN INCL(b, Controllers.doubleClick) END;
		x := h.x; y := h.y;
		WinPorts.SetMouseState(x, y, b, isDown);
		IF click IN h.flags THEN StdWindows.Track(w, x, y, b)
		ELSIF hover IN h.flags THEN StdWindows.UpdateCursor(w, x, y, b)
		ELSIF wheel IN h.flags THEN
			IF h.wheel < 0 THEN op := Controllers.incLine
			ELSIF h.wheel > 0 THEN op := Controllers.decLine 
			ELSE HALT(20)
			END;
			StdWindows.Wheel(w, op, x, y, b)
		END
	END Do;

	PROCEDURE WmMouse (wnd: WinApi.HANDLE; flags: SET; lParam, dir: INTEGER);
		VAR mouse: MouseHandler; _: INTEGER; pt: WinApi.POINT;
	BEGIN
		mouse.win := ThisBackend(wnd).win; mouse.flags := flags; mouse.wheel := dir;
		mouse.x := lParam MOD 10000H; mouse.y := lParam DIV 10000H;
		IF wheel IN flags THEN 
			pt.x := mouse.x; pt.y := mouse.y; _ := WinApi.ScreenToClient(wnd, pt);
			mouse.x := pt.x; mouse.y := pt.y
		END;
		IF click IN flags THEN 
			_ := WinApi.SetCapture(wnd); 
			cancelRefocus.in := NIL; cancelRefocus.out := NIL
		END;
		Services.Try(mouse);
		IF (click IN flags) & (WinApi.GetCapture() = wnd) THEN _ := WinApi.ReleaseCapture() END
	END WmMouse;
	
	PROCEDURE (VAR h: CloseHandler) Do;
	BEGIN StdWindows.GuardedClose(h.win)
	END Do;
	
	PROCEDURE (VAR h: DropFilesHandler) Do;	(* adapted from WinMenus.DropFiles *)
		VAR i, n, res_: INTEGER; path, fname: Files.Name; ok: BOOLEAN; loc: Files.Locator; 
			np: WinApi.PtrWSTR; v: Views.View; conv: Converters.Converter; f: Files.File;	
	BEGIN
		n := WinApi.DragQueryFileW(h.drop, -1, fname, LEN(fname)); i := 0; ok := FALSE;
		WHILE i < n DO
			IF 0 # WinApi.DragQueryFileW(h.drop, i, fname, LEN(fname)) THEN
				IF LEN(path) > WinApi.GetFullPathNameW(fname, LEN(path), path, np) THEN
					IF np # NIL THEN fname := np^$ ELSE fname := '' END;
					IF fname # '' THEN
						DEC(SYSTEM.VAL(INTEGER, np), 2); np^[0] := 0X;
						loc := Files.dir.This(path);
						f := Files.dir.Old(loc, fname, Files.shared);
						IF f # NIL THEN
							conv := Converters.list;
							WHILE (conv # NIL) & (conv.fileType # f.type) DO conv := conv.next END;
							IF conv = NIL THEN
								conv := Converters.list;
								WHILE (conv # NIL) & ~(Converters.importAll IN conv.opts) DO
									conv := conv.next
								END
							END;
							IF f.type = "" THEN fname := fname + "." END;
							v := Views.Old(Views.dontAsk, loc, fname, conv);
							IF v # NIL THEN Views.Open(v, loc, fname, conv); ok := TRUE 
							END
						END
					ELSE	(* silently devour what's probably a folder name *)
					END
				ELSE HALT(21)	(* name too long *)
				END
			ELSE HALT(20)	(* couldn't query dragged files name; name too long? *)
			END;
			INC(i)
		END;
		WinApi.DragFinish(h.drop);
		IF ok THEN res_ := WinApi.SetForegroundWindow(h.wnd) END
	END Do;
	

	PROCEDURE [callback] DocWinHandler (wnd: WinApi.HANDLE; message, wParam, lParam: INTEGER): INTEGER;
		VAR _, x, dx, dy: INTEGER; w: StdWindows.Window; ps: WinApi.PAINTSTRUCT; hdc: WinApi.HDC;
			(* pm: WinApi.PMinMaxInfo; *)
			rec: WinApi.RECT; style, st: SET; wp: WinApi.WINDOWPOS;
			paint: PaintHandler; create: CreateHandler; char: CharHandler; mouse: MouseHandler;
			focus: FocusHandler; close: CloseHandler; drop: DropFilesHandler;
			done: BOOLEAN; delta, res: INTEGER; 
	BEGIN
		done := FALSE;
		(*IF (hookDocWinHandler # NIL) & hookDocWinHandler(wnd, message, wParam, lParam, res) THEN
			Controllers.ResetCurrentPath();
			RETURN res
		END;*)
				CASE message OF
		| WinApi.WM_LBUTTONDOWN, WinApi.WM_RBUTTONDOWN, WinApi.WM_MBUTTONDOWN:
			WmMouse(wnd, BITS(wParam) + { click }, lParam, 0); done := TRUE; 
		| WinApi.WM_LBUTTONDBLCLK, WinApi.WM_RBUTTONDBLCLK, WinApi.WM_MBUTTONDBLCLK:
			WmMouse(wnd, BITS(wParam) + { click, dbl }, lParam, 0); done := TRUE; 
		| WinApi.WM_LBUTTONUP, WinApi.WM_RBUTTONUP, WinApi.WM_MBUTTONUP:
			WmMouse(wnd, BITS(wParam), lParam, 0); done := TRUE; 
		| WinApi.WM_MOUSEMOVE:
			WmMouse(wnd, BITS(wParam) + { hover }, lParam, 0); done := TRUE; 
		| WinApi.WM_KEYDOWN, WinApi.WM_SYSKEYDOWN:  
			IF wParam = WinApi.VK_MENU THEN
				(* this branch is for obsolete WinApi menus and controls *)
				res := WinApi.SendMessageW(wnd, 0127H (* WM_CHANGEUISTATE *),
							2 (* UIS_CLEAR *) + 10000H (* UISF_HIDEFOCUS *) + 20000H (* UISF_HIDEACCEL *), 0)
			ELSE WmKeydown(wnd, wParam, lParam)
			END
		| WinApi.WM_CHAR:
			char.win := ThisBackend(wnd).win; char.char := CHR(wParam); char.flags := BITS(lParam);
			Services.Try(char);
			done := TRUE
			
		| WinApi.WM_SIZE: 
			WmSize(wnd, lParam MOD 65536, lParam DIV 65536)
		| WinApi.WM_PAINT: 
			paint.wnd := wnd; paint.win := ThisBackend(wnd).win; Services.Try(paint); done := TRUE;
		| WinApi.WM_CREATE: 
			create.wnd := wnd; create.createStruct := SYSTEM.VAL(WinApi.PtrCREATESTRUCTW, lParam);
			Services.Try(create)
		| WinApi.WM_CLOSE: close.win := ThisBackend(wnd).win; Services.Try(close); done := TRUE 
		| WinApi.WM_DESTROY: 
		| WinApi.WM_CHILDACTIVATE:	(* Sent to a child window when the user clicks the window's title bar or when the window is activated, moved, or sized.*)
		| WinApi.WM_SETFOCUS: 
			cancelRefocus.in := ThisBackend(wnd).win; Services.DoLater(cancelRefocus, Services.Ticks() + 2);
		| WinApi.WM_KILLFOCUS:
			cancelRefocus.out := ThisBackend(wnd).win; Services.DoLater(cancelRefocus, Services.Ticks() + 2);
		| WinApi.WM_MOUSEWHEEL: 
			delta := wParam DIV 10000H;
			(* weirdly, smtms WM_MOUSEWHEEL arrives that has delta = 0,
				i.e. wheel wasn't rotated? or is it bunching? *)
			IF delta # 0 THEN
				WmMouse(wnd, BITS(wParam MOD 10000H) + { wheel }, lParam, delta)
			END;
			done := TRUE
		| WinApi.WM_DROPFILES:
			drop.drop := wParam; drop.wnd := wnd; Services.Try(drop); done := TRUE
		ELSE
		END;
		IF done THEN res := 0 ELSE res := WinApi.DefWindowProcW(wnd, message, wParam, lParam) END;
		RETURN res
	END DocWinHandler;
	

	PROCEDURE PortOrigin (p: Ports.Port; OUT xRoot, yRoot: Pixels): BOOLEAN;
		VAR res: BOOLEAN;
	BEGIN
		res := FALSE;
		WITH p: WinPorts.Port DO
			IF p.wnd # 0 THEN res := GetOrigin(p.wnd, xRoot, yRoot) END
		ELSE
		END;
		RETURN res
	END PortOrigin;

	(* simulates modal dialog  TEMPORARY SOLUTION FOR SIMPLE MODAL WINDOWS  *)  
	PROCEDURE WaitFrontWindowClose;
		VAR res: INTEGER; w: StdWindows.Window; msg: WinApi.MSG;
	BEGIN
		w := Windows.dir.First()(StdWindows.Window);
		REPEAT
			IF WinApi.PeekMessageW(msg, 0, 0, 0, 1) # 0 THEN
				IF (msg.hwnd = w.backend(Backend).wnd) OR (msg.message = WinApi.WM_PAINT) THEN
					res := WinApi.TranslateMessage(msg);
					res := WinApi.DispatchMessageW(msg)
				END
			END
		UNTIL w.backend = NIL
	END WaitFrontWindowClose;

	PROCEDURE Init;
		VAR _: INTEGER; class: WinApi.WNDCLASSW;
			dc: WinApi.HANDLE;
	BEGIN
		instance := WinApi.GetModuleHandleW(NIL);
		dc := WinApi.GetDC(0);
		unit := SHORT(ENTIER(
			(Ports.inch DIV WinApi.GetDeviceCaps(dc, WinApi.LOGPIXELSY)) * 100 / Dialog.scaleFactor));
		WinGui.unit := unit;
		_ := WinApi.ReleaseDC(0, dc);
		scW := WinApi.GetSystemMetrics(0);	(* screen width *)
		scH := WinApi.GetSystemMetrics(1);	(* screen height *)
	
		class.hCursor := 0;	(* no class cursor *)
		class.lpszMenuName := NIL;
		class.hbrBackground := 0;	(* no background *)
		class.style := (*WinApi.CS_VREDRAW + WinApi.CS_HREDRAW +*) WinApi.CS_DBLCLKS + WinApi.CS_OWNDC;
		(* VREDRAW and HREDRAW result in Windows invalidating the whole window when only it's height or width is changed - I guess without them it only invalidates the newly added area or nothing if the window is shrinked. This is not critical, because WinBackends handles all the resizing and redrawing logic in response to resize messages *)
		
		class.hInstance := instance;
		class.cbClsExtra := 0;
		class.cbWndExtra := 4;
		
		docIcon := WinApi.LoadIconW(instance, SYSTEM.VAL(WinApi.PtrWSTR, 2));
		IF (docIcon = 0) OR (docIcon = 1) THEN
			docIcon := WinApi.LoadIconW(0, SYSTEM.VAL(WinApi.PtrWSTR, WinApi.IDI_APPLICATION))
		END;
		dirtyIcon := WinApi.LoadIconW(instance, SYSTEM.VAL(WinApi.PtrWSTR, 5));
		IF (dirtyIcon = 0) OR (dirtyIcon = 1) THEN dirtyIcon := docIcon END;
		class.hIcon := docIcon;
		class.lpszClassName := "Oberon Doc";
		class.lpfnWndProc := DocWinHandler;
		_ := WinApi.RegisterClassW(class);
		NEW(dir); StdWindows.SetBackendDir(dir);
		StdMenuTool.encodeShortcut := EncodeShortcut;
		WinDialog.WaitFrontWindowClose := WaitFrontWindowClose;
		StdWindows.PortOrigin := PortOrigin;
		StdDialog.RecalcMainWindow := RecalcMainWindow;
		NEW(cancelRefocus)
	END Init;
	
BEGIN
	Init
END WinBackends.
