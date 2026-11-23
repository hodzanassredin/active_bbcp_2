MODULE StdWindows;
(**	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Anton Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		Kernel, Strings, Ports, Files, Services, Stores, Sequencers, Models, Views, 
		Controllers, Properties, Dialog, Converters, Containers, Documents, Windows,
		StdDocuments;

	CONST
		notRecorded = 3;
		untitledKey = "#System:untitled";
		
		none* = -1;	(* value for global variable L, indicates LTRB is not set *)
		borderW = 5 * Ports.point;
		scrollLock* = 8;	(* modifier value for ScrollLock *)
		defW = 640; defH = 480;	(* default screen size *)
		
		useSeparators = TRUE; noSeparators = FALSE; 	(* parameters to be used with AppendInt *)
		
		(* these constants need to be in sync with Backends; actually, they are defined in Containers docu *)
			(* ENTER = 0DX; ESC = 1BX; 
			TAB = 09X; LTAB = 0AX; RDEL = 07X; LDEL = 08X;
			PL = 10X; PR = 11X; *) PU = 12X; PD = 13X;
			DL = 14X; DR = 15X; (* DU = 16X; DD = 17X; *)
			AL = 1CX; AR = 1DX; AU = 1EX; AD = 1FX;

			WL = 5; WR = 6;	(* wheel left/right; picks up numbering after Controllers.gotoPos *)
		(**)

	TYPE
		Pixels = INTEGER;	(* make declarations more self-documenting *)
		UCs = INTEGER;
		Ident = Kernel.Name;

		Window* = POINTER TO EXTENSIBLE RECORD (Windows.Window)
			next: Window;
			hook*: Hook;	(** window activity notification *)
			backend-: WindowBackend;
			title-, initialTitle: Views.Title;
			path: Files.Name;
			width, height: Pixels;	(* window size *)
		END;
		(** A window is the cornerstone abstraction in the windowing system. It's main responsibility is to bring together the many abstractions that provide different aspects of the windowing system:
		* the Document - as the standard container for an arbitrary view, responsible for storing, paging, printing;
		* the Port - the bitmap canvas on which the content of the window is restored
		* the Layout - the view responsible for window layout, see below
		* two scrollbars
		* the sequencer - non-visible abstraction responsible for message delivery and model undo/redo operations
		* the backend - abstraction responsible for translating BB windows to underlying host mechanisms
		* the rootFrame and docFrame - connection points between the views framework and windowing system
		* the Locator, filename and Converter, connecting the window's root view to the permanent store (typically the filesystem)
		Another vital responsibility is connecting windows that present the same root view - duplicate windows; this is a distinguishing feature of the BlackBox windowing system.
		And don't forget the window title!
		In this version, Window is marked as EXTENSIBLE; however, the experiment in Tyler project showed that it may not be necessary to extend Window to implement new functionality; in Tyler, addition of features was achieved thru extending WindowBackend and Layout. *)

		(** Directory is responsible for creating and initializing new windows. *)
		StdDirectory = POINTER TO RECORD (Windows.Directory) END;

		WindowBackend* = POINTER TO ABSTRACT RECORD
			win-: Window;	(** the window that this backend is connected to; may be NIL *)
		END;
		(** This type is responsible for translating a window in "host" terms; typically, the host is the underlying host system and it's windowing facilities. However, in Tyler project - the tiling widow manager - a "host" can be another window that serves as the container for "tiled" windows. This type is extended and impelemnted in HostWindows. *)
		BackendDirectory* = POINTER TO ABSTRACT RECORD END;
		(** BackendDirectory is responsible for creating and initializing new backends. *)

		Hook* = POINTER TO ABSTRACT RECORD END;	(** window state notification hook *)

		AttachedCheckHook* = POINTER TO ABSTRACT RECORD END;

		OpElem = POINTER TO RECORD
			next: OpElem;
			st: Stores.Store;
			op: Stores.Operation;
			name: Stores.OpName;
			invisible, transparent: BOOLEAN
		END;

		Script = POINTER TO RECORD (Stores.Operation)
			up: Script;
			list: OpElem;
			level: INTEGER;	(* nestLevel at creation time *)
			name: Stores.OpName
		END;

		StdSequencer = POINTER TO RECORD (Sequencers.Sequencer)
			home: Window;
			trapEra: INTEGER;	(* last observed TrapCount value *)
			modLevel: INTEGER;	(* dirty if modLevel > 0 *)
			entryLevel: INTEGER;	(* active = (entryLevel > 0) *)
			nestLevel: INTEGER;	(* nesting level of BeginScript/Modification *)
			modStack: ARRAY 64 OF RECORD store: Stores.Store; type: INTEGER END;
			lastSt: Stores.Store;
			lastOp: Stores.Operation;
			script: Script;
			undo, redo: OpElem;	(* undo/redo stacks *)
			noUndo: BOOLEAN;	(* script # NIL and BeginModification called *)
			invisibleLevel, transparentLevel, notRecordedLevel: INTEGER;
		END;
		SequencerDirectory = POINTER TO RECORD (Sequencers.Directory) END;

		Forwarder = POINTER TO RECORD (Controllers.Forwarder) END;
		(* Provided by Windows => not needed here any more *)
		
		Context = POINTER TO ABSTRACT RECORD (Models.Context)
			win: Window;
		END;
		DocContext = POINTER TO RECORD (Context) END;
		
		Reducer = POINTER TO RECORD (Kernel.Reducer) END;

		MsgHook = POINTER TO RECORD (Views.MsgHook) END;
		
		PrefocusMsg* = RECORD (Controllers.CursorMessage)
			win*: Window
		END;
		(** Forwarded before a TrackMsg when a click occurs in an unfocussed window. Normally in such case the window is focussed (selected) first, and then the TrackMsg is forwarded. Receiver may set .hotFocus to indicate that it doesn't require containing window to be focussed before tracking the mouse. For use by views that would change focus to another window as a result of TrackMsg - otherwise, there'd be a cascade of unwanted window selections *)
		
		UpdateMsg* = RECORD (Controllers.Message) END;
		(** Forwarded to windows' root views before validation to let them update their state. Used for scrollbars *) 
		
		CheckAction = POINTER TO RECORD (Services.Action) 
			wait: WaitAction
		END;

		WaitAction = POINTER TO RECORD (Services.Action) 
			check: CheckAction
		END;
		
		LangNotifier = POINTER TO RECORD (Dialog.LangNotifier) END;
		
		SafeOpen = RECORD (Services.SafeAction)
			w: Window; doc: Documents.Document; flags: SET; title: Views.Title
		END;
		
		Param* = POINTER TO RECORD
			bgColor*: Ports.Color;
			wheelScrollByPage*: BOOLEAN;
			debug*: BOOLEAN;
			markTitles*: BOOLEAN;
			frontStr*, targetStr*, frontTargetStr*, markStr*: ARRAY 8 OF CHAR;
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;

		Ext* = POINTER TO ABSTRACT RECORD END;
		(** A hook that provides services that are standard (because they are in this module), but may or may not be present in a particular situation. Right now, this is the support for multihead settings: Windows (and it's clients) have an interface to this feature, but the feature may or may not be provided. *)
		StdExt = POINTER TO RECORD (Ext) END;

	VAR 
		dir-, (*Windows.Directory;*)
		stdDir-: StdDirectory;
		backendDir-, stdBackendDir-: BackendDirectory;
		
		backoff* : RECORD	(** after an interaction with a menu, focus should be returned to backoff revisit *)
			front*, target-: Window
		END;
		front-: Window;	(** Front window: the one receiving keyboard input *)
		target-: Window;	(** Target window: the one that tool dialogs (may) operate on, if they so choose *)
		mark-: Window;	(** Marked window: clients may provide commands operating on the marked wnd *)
		
		zList: Window;	(* Z-ordered list of open windows, linked by .next, zList being topmost window *)
		border*: UCs;	(** The document border for the next window to be opened; a request *)
		
		newNumber: INTEGER;	(* number for next untitled document *)	(* AD: from HostWindows *)
		dump-, stdDump-: PROCEDURE (p: ANYPTR);	
			(** called to dump window or frame when ^Sh+LMB/MMB is clicked, see StdTrack *)
		
		guardedCloseHook*: PROCEDURE (w: Windows.Window);
		PortOrigin(*Hook*)*: PROCEDURE (p: Ports.Port; OUT xRoot, yRoot: Pixels): BOOLEAN;
			(** HostWindows should provide this procedure. Returns global screen coordinates of a Port's origin: translates Port's (0,0) to global screen coords. *)
		param*: Param;
		
		ext*: Ext;	(** holder of extended Windows services that may not be provided by the host implementation *)
		attachedCheckHook-: AttachedCheckHook;

	(* Backend Directory *)

	PROCEDURE (bd: BackendDirectory) NewBackend* (): WindowBackend, NEW, ABSTRACT;
	(** Produce a new backend. *)

	PROCEDURE (bd: BackendDirectory) GetBounds* (OUT w, h: INTEGER), NEW, ABSTRACT;
	(** a procedure returning screen dimensions; to be proivided by HostWindows *)


	(** Returns the number of heads (monitors) currently connected to the system.
		ATTN: it's a VAR par, extensions have no obligation to actually fill it!
		In case no Ext is provided, ext may leave count unchanged *)
	PROCEDURE (e: Ext) GetNumHeads* (VAR count: INTEGER), NEW, ABSTRACT;
	
	(** Request to send wb.win to head. May be ignored *)
	PROCEDURE (e: Ext) ToHead* (wb: WindowBackend; head: INTEGER), NEW, ABSTRACT;
	
	(** Request to maximize wb.win. May be ignored *)
	PROCEDURE (e: Ext) Maximize* (wb: WindowBackend), NEW, ABSTRACT;
	
	PROCEDURE (e: StdExt) GetNumHeads (VAR count: INTEGER), EMPTY;
	PROCEDURE (e: StdExt) ToHead (wb: WindowBackend; head: INTEGER), EMPTY;
	PROCEDURE (e: StdExt) Maximize (wb: WindowBackend), EMPTY;


	(* can be called with w = NIL! *)
	PROCEDURE (hook: AttachedCheckHook) IsAttached- (w: Window): BOOLEAN, NEW, ABSTRACT;

	(** Window **)
	
	PROCEDURE First (): Window; 
	(** Returns the first (top) window in Z-order. NIL if no window is open *)
	BEGIN RETURN zList
	END First;
	
	PROCEDURE Next (w: Window): Window;
	(** Returns the window following w in Z-order. NIL if w is the last (bottom) window *)
	BEGIN IF w # NIL THEN w := w.next END; RETURN w
	END Next;
	
	
	PROCEDURE IsAttached* (w: Window): BOOLEAN;
	BEGIN
		RETURN (w # NIL) & (attachedCheckHook # NIL) & attachedCheckHook.IsAttached(w)
	END IsAttached;

	PROCEDURE IsDetached* (w: Window): BOOLEAN;
	BEGIN RETURN ~IsAttached(w)
	END IsDetached;


	(* Scrolling procedures *)

	PROCEDURE ^ (w: Window) ForwardCtrlMsg* (VAR msg: Controllers.Message), EXTENSIBLE;

	(* auxiliary portable prcedures (scrolling) *)

	PROCEDURE GetSection (w: Window; focus, vertical: BOOLEAN;
								VAR size, sect, pos: INTEGER; VAR valid: BOOLEAN);
		VAR msg: Controllers.PollSectionMsg;
	BEGIN	(* portable *)
		msg.focus := focus; msg.vertical := vertical;
		msg.wholeSize := 1; msg.partSize := 0; msg.partPos := 0;
		msg.valid := FALSE; msg.done := FALSE;
		(*w.trapped := TRUE; *)w.ForwardCtrlMsg(msg); (*w.trapped := FALSE;*)
		IF msg.done THEN
			size := msg.wholeSize; sect := msg.partSize; pos := msg.partPos;
			IF size < 0 THEN size := 0 END;
			IF sect < 0 THEN sect := 0 ELSIF sect > size THEN sect := size END;
			IF pos > size - sect THEN pos := size - sect END;
			IF pos < 0 THEN pos := 0 END
		ELSE size := 1; sect := 0; pos := 0
		END;
		valid := msg.valid
	END GetSection;

	PROCEDURE Scroll (w: Window; focus, vertical: BOOLEAN; op, pos: INTEGER);
	(* scroll w by sending a Controllers.ScrollMsg. pos is ignored if op # Controllers.gotoPos. AD: based on Scroll and SetOrigin *)
		VAR msg: Controllers.ScrollMsg; c: Containers.Controller; v: Views.View;
	BEGIN	(* portable *)
		IF op # Controllers.gotoPos THEN	(* AD: not sure why marks aren't faded for op = gotoPos *)
			c := w.doc.ThisController(); v := c.ThisFocus();
			IF (v # NIL) & (v IS Containers.View) THEN
				Containers.FadeMarks(v(Containers.View).ThisController(), FALSE)
			END
		ELSE msg.pos := pos
		END;
		msg.focus := focus; msg.vertical := vertical; msg.op := op; msg.done := FALSE;
		w.ForwardCtrlMsg(msg)
	END Scroll;

	(* Window *)

	PROCEDURE (w: Window) GetSize* (OUT width, height: Pixels), (*NEW, *)EXTENSIBLE;
	BEGIN 
		IF w.port = NIL THEN width := 0; height := 0	(* closed window *)
		ELSE width := w.width; height := w.height
		END
	END GetSize;
	
	PROCEDURE InstallRoot* (w: Window; f: Views.Frame; x, y, level: INTEGER; focus: BOOLEAN);
	(** Installs a root frame for window w into frame f at position x,y and z-order level, marking it as focus frame if focus = TRUE. *)
	BEGIN ASSERT(w # NIL, 20); ASSERT(w.frame # NIL, 21);
		Views.InstallRoot(f, w.frame, x, y, level, focus); 
		IF w.port = NIL THEN w.Init(w.frame.rider.Base())
		ELSE ASSERT(w.port = w.frame.rider.Base(), 90)
		END
	END InstallRoot;

	PROCEDURE UpdateIn* (win: Window; l, t, r, b: Pixels; rebuild: BOOLEAN);
	(** Request delayed update of box l,t,r,b in win with frame rebuild flag rebuild. *)
		VAR f: Views.Frame; w, h, u: UCs;
	BEGIN ASSERT(win # NIL, 20); f := win.frame;
		IF f # NIL THEN win.GetSize(w, h); u := f.unit;
			l := l * u; t := t * u; r := r * u; b := b * u;
			r := MIN(r, w * u); b := MIN(b, h * u);
			Views.UpdateRoot(win.frame, l, t, r, b, rebuild);	(* this does not respect frame scrolling *)
		END
	END UpdateIn;
	
	PROCEDURE Update* (win: Window; rebuild: BOOLEAN);
	(** Request delayed update of win with frame rebuild flag rebuild *)
		VAR f: Views.RootFrame; w, h: UCs;
	BEGIN 
		IF win # NIL THEN f := win.frame;
			IF f # NIL THEN Views.AdaptRoot(f);
				win.GetSize(w, h);
				Views.UpdateRoot(f, 0, 0, w * f.unit, h * f.unit, rebuild) (* this doesn't respect frame scrolling *)
			END
		ELSE win := First(); WHILE win # NIL DO Update(win, rebuild); win := Next(win) END
		END
	END Update;

	
	PROCEDURE (w: Window) RRestore* (l, t, r, b: INTEGER), NEW;
	(** deprecated, use Windows.UpdateIn or Windows.Update with rebuild = Views.KeepFrames *)
	BEGIN
		UpdateIn(w, l, t, r, b, Views.keepFrames)
	END RRestore;

	PROCEDURE ^ ValidateRing* (w: Window);
	
	PROCEDURE ScrollDir (w: Window; op, value: INTEGER; focus, vertical: BOOLEAN);
		VAR  size, sect, pos: INTEGER; valid: BOOLEAN;
	BEGIN
		GetSection(w, focus, vertical, size, sect, pos, valid);
		IF valid THEN Scroll(w, focus, vertical, op, value);
			ValidateRing(w);
			(* why does it have to update all sequencer's windows? replace w/Validate? *)
		END
	END ScrollDir;

	(** Force the restoration of w's update region. Use with care! Validation of a window's (as well as root frame's) update region may result in closing any or all subframes; thus, if Validate is called from a handler that was handed a frame f, after the call f may be closed and no operations on it permitted. A proper use case for Validate is forced restoration during mouse tracking. *)
	
	PROCEDURE Validate* (w: Window);
		VAR d: Documents.Document;
	BEGIN
		IF w # NIL THEN
			IF ~Views.IsObscured(w.frame) THEN
				d := w.doc; 
				WITH d: StdDocuments.Document DO 
					StdDocuments.UpdateScrollbars(w.frame)
				ELSE 
				END;
				Views.ValidateRoot(w.frame) 
			END
		ELSE
			w := First();
			WHILE w # NIL DO
				Validate(w);
				w := Next(w)
			END
		END;
	END Validate;
	
	PROCEDURE (w: Window) UUpdate*, NEW;
	BEGIN Validate(w)
	END UUpdate;
	
	PROCEDURE (w: Window) Init* (port: Ports.Port)(*, NEW*);
	(** Initialize w with port. Pre: w.port = NIL 20, port # NIL 21 *)
	BEGIN
		w.Init^(port); w.title := ""; w.initialTitle := ""
	END Init;

	PROCEDURE (w: Window) InitBackend* (wb: WindowBackend), NEW, EXTENSIBLE;
	(** Initialize w with backend wb (connect w and wb). EXTENSIBLE so that extensions could be notified of receiving a backend - just in case. Extensions have to call the base procedure in order for wb and w to be connected  *)
	BEGIN ASSERT(wb # NIL, 20); ASSERT((w.backend = NIL) OR (w.backend = wb), 21);
		ASSERT(wb.win = NIL, 22);
		w.backend := wb; wb.win := w
	END InitBackend;

	PROCEDURE IsDialog (flags: SET; doc: Documents.Document): BOOLEAN;
		VAR isDialog: BOOLEAN; c: Containers.Controller; v: Views.View;
	BEGIN
		isDialog := Windows.isTool IN flags;
		IF ~isDialog THEN
			v := doc.ThisView();
			WITH v: Containers.View DO c := v.ThisController(); 
				isDialog := (c # NIL) & ({Containers.noCaret, Containers.noSelection} - c.opts = {})
			ELSE 
			END
		END;
		RETURN isDialog
	END IsDialog;

	PROCEDURE (wb: WindowBackend) SetSize- (width, height: Pixels), NEW, ABSTRACT;
	
	PROCEDURE (w: Window) SetSize* (width, height: Pixels);
	(** Set the size of w to width, height in pixels. *)
		VAR w0, h0: Pixels; 
	BEGIN 
		IF w.port # NIL THEN w.GetSize(w0, h0);
			IF (width # w0) OR (height # h0) THEN 
				w.width := width; w.height := height; 
				IF w.backend # NIL THEN w.backend.SetSize(width, height) END;
				IF w.frame # NIL THEN Views.AdaptRoot(w.frame) END;
				Views.Update(w.doc, Views.keepFrames)
			END
		END
	END SetSize;
	
	PROCEDURE GetInitSize (w: Window; doc: Documents.Document; OUT wid, hei: Pixels; flags: SET);
		VAR isDialog: BOOLEAN; u, dl, dt, dr, db, dw, dh, bw: UCs; W, H: Pixels;
	BEGIN
		ASSERT(doc.context = NIL, 20); (* to obtain a doc's desired size, it has to not be embedded *)
		isDialog := IsDialog(flags, doc);
		u := w.port.unit;
		IF isDialog THEN
			bw := 0;
		ELSE
			IF border < 0 THEN
				bw := borderW
			ELSE
				bw := border
			END;
		END;
		border := -1;
		
		IF (dir.l # none) & (dir.r > dir.l) & (dir.b > dir.t) THEN
			wid := dir.r - dir.l; hei := dir.b - dir.t; 
			dr := wid* u - bw; db := hei * u - bw
		ELSE
			doc.PollRect(dl, dt, dr, db);
			dw := dr - dl; dh := db - dt;
			IF backendDir # NIL THEN
				backendDir.GetBounds(W, H)
			ELSE
				W := defW; H := defH
			END;
			IF ~(Windows.noHScroll IN w.flags) & ((dw DIV u) > (W - 40)) THEN
				dw := (W - 80) * u
			END;
			IF ~(Windows.noVScroll IN w.flags) & ((dh DIV u) > (H - 40)) THEN
				dh := (H - 160) * u
			END;
			dr := bw + dw; db := bw + dh;
			wid := (dr + bw) DIV u;
			hei := (db + bw) DIV u;
		END;
		dl := bw; dt := bw;
		IF ~ (Windows.isTool IN flags) THEN
			IF ~(Windows.noHScroll IN flags) THEN
				INC(wid, StdDocuments.param.sbThickness)
			END;
			IF ~(Windows.noVScroll IN flags) THEN
				INC(hei, StdDocuments.param.sbThickness)
			END;
		END;
		IF isDialog THEN
			dir.l := (W - wid) DIV 2;
			dir.t := (H - hei) DIV 2;
		END;
		doc.SetRect(dl, dt, dr, db);
		IF wid < 0 THEN wid := 0 END;
		IF hei < 0 THEN hei := 0 END;
	END GetInitSize;
	
	PROCEDURE (w: Window) BroadcastModelMsg2* (VAR msg: Models.Message), NEW, EMPTY;
	(** This should be implemented by a Window extender in case it wants to add to the standard broadcasting behavior - for example, if it adds it's own (root) frames where model messages have to be broadcast. *)
	
	PROCEDURE (w: Window) BroadcastModelMsg* (VAR msg: Models.Message);
	(** Broadcast msg into w. A Window extender's w.BroadcastModelMsg2 is called, too, to give it a chance to broadcast msg message into it's (possible) structures. *)
	BEGIN 
		Views.BroadcastModelMsg(w.frame, msg);
		w.BroadcastModelMsg2(msg);
	END BroadcastModelMsg;

	PROCEDURE (w: Window) BroadcastViewMsg2* (VAR msg: Views.Message), NEW, EMPTY;
	(** This should be implemented by a Window extender in case it wants to add to the standard broadcasting behavior - for example, if it adds it's own (root) frames where view messages have to be broadcast. *)
	
	PROCEDURE (w: Window) BroadcastViewMsg* (VAR msg: Views.Message);
	(** Broadcast msg into w. A Window extender's w.BroadcastViewMsg2 is called, too, to give it a chance to broadcast msg message into it's (possible) structures *)
		VAR name: ARRAY 256 OF CHAR;
	BEGIN
		Services.GetTypeName(msg, name);
		Views.BroadcastViewMsg(w.frame, msg);
		w.BroadcastViewMsg2(msg);
	END BroadcastViewMsg;
	
	PROCEDURE (w: Window) ForwardCtrlMsg* (VAR msg: Controllers.Message), EXTENSIBLE;
	(** Forward msg into w. An extension may add to this or override *)
	(*  based on Windows.Window.ForwardCtrlMsg & HostWindows.Window.ForwardCtrlMsg *)
		VAR flags: SET;
	BEGIN
		IF (w.frame # NIL) & ~Views.IsObscured(w.frame) THEN
			flags := {};
			IF w = front THEN INCL(flags, StdDocuments.front) END;
			IF w = target THEN INCL(flags, StdDocuments.target) END;
			Views.SetRoot(w.frame, w.frame.view, w = front, w.flags + flags);
			Views.ForwardCtrlMsg(w.frame, msg);
		END
	END ForwardCtrlMsg;

	PROCEDURE ^ Select (w: Window; lazy: BOOLEAN);

	PROCEDURE StdTrack (w: Window; VAR track: Controllers.TrackMsg);
		CONST
			showWindow = {Controllers.extend, Controllers.modify, Controllers.pick};
			showFrame = {Controllers.extend, Controllers.modify, Controllers.popup};
		VAR x, y: UCs; f, g: Views.Frame; mod: SET; pan: StdDocuments.PanWindowPref;
			prefocus: PrefocusMsg;
	BEGIN mod := track.modifiers; x := track.x; y := track.y;
		IF dump # NIL THEN
			(* This was commented out in (Lin)HostWindows.HandleMouse;
				AD: brought it back in and adapted *)
			IF showWindow - mod = {} THEN
				dump(w)
			ELSIF showFrame - mod = {} THEN
				f := w.frame;
				REPEAT g := f; f := Views.FrameAt(g, x - g.gx, y - g.gy) UNTIL f = NIL;
				dump(g)
			END
		END;
		pan.atLocation := TRUE; pan.x := x; pan.y := y; pan.panWindow := FALSE; 
		Views.HandlePropMsg(w.doc, pan);
		IF ~pan.panWindow THEN
			prefocus.x := x; prefocus.y := y; prefocus.win := w;
			w.ForwardCtrlMsg(prefocus);
			IF prefocus.win # NIL THEN
				Select(prefocus.win, Windows.lazy)
			END;
			backoff.front := NIL
		ELSIF pan.panWindow & (w # front) & (backoff.front = NIL) THEN
			backoff.front := front
		END;
		w.ForwardCtrlMsg(track)
	END StdTrack;
	
	PROCEDURE StdWheel (w: Window; VAR msg: Controllers.WheelMsg);
		VAR scroll: Controllers.ScrollMsg; mod: SET; op: INTEGER;
	BEGIN 
		mod := msg.modifiers; op := msg.op;
		IF ~(Controllers.modify IN mod) THEN w.ForwardCtrlMsg(msg) END;
		IF ~msg.done THEN 
			scroll.vertical := ~(Controllers.extend IN mod) OR (op = WL) OR (op = WR);
			scroll.focus := Controllers.modify IN mod;
			IF ~param.wheelScrollByPage & (Controllers.pick IN mod)
			OR param.wheelScrollByPage & ~(Controllers.pick IN mod) THEN
				CASE op OF Controllers.decLine, WL: scroll.op := Controllers.decPage
				| Controllers.incLine, WR: scroll.op := Controllers.incPage
				ELSE scroll.op := op
				END
			ELSE
				IF op = WL THEN scroll.op := Controllers.decLine
				ELSIF op = WR THEN scroll.op := Controllers.incLine
				ELSE scroll.op := op
				END
			END;
			scroll.done := FALSE;
			w.ForwardCtrlMsg(scroll);
			IF scroll.done THEN msg.done := TRUE
			ELSE scroll.focus := ~scroll.focus; w.ForwardCtrlMsg(scroll);
				msg.done := scroll.done
			END
		END
	END StdWheel;

	PROCEDURE StdPasteChar (w: Window; msg: Controllers.EditMsg);
		VAR scroll, scrolled, horizontal: BOOLEAN; ch: CHAR; mod: SET;
			c: Containers.Controller; pmsg: Controllers.PollFocusMsg; 
	BEGIN ASSERT(w # NIL, 20); ASSERT(msg.op = Controllers.pasteChar, 21); 
		ch := msg.char; mod := msg.modifiers;
		scroll := FALSE;
		pmsg.focus := NIL; w.ForwardCtrlMsg(pmsg);
		IF (pmsg.focus # NIL) & (pmsg.focus.view IS Containers.View) THEN
			c := pmsg.focus.view(Containers.View).ThisController();
			IF (c # NIL) & (Containers.noCaret IN c.opts) THEN scroll := TRUE END
		END;
		IF (scrollLock IN mod) OR scroll THEN scrolled := TRUE; horizontal := Controllers.modify IN mod;
			CASE ch OF
			| PU: ScrollDir(w, Controllers.decPage, 0, TRUE, ~horizontal)
			| PD: ScrollDir(w, Controllers.incPage, 0, TRUE, ~horizontal)
			| DR: ScrollDir(w, Controllers.gotoPos, MAX(INTEGER), TRUE, horizontal)
			| DL: ScrollDir(w, Controllers.gotoPos, 0, TRUE, horizontal)
			| AL: ScrollDir(w, Controllers.decLine, 0, TRUE,  FALSE)
			| AU: ScrollDir(w, Controllers.decLine, 0, TRUE,  TRUE)
			| AR: ScrollDir(w, Controllers.incLine, 0, TRUE,  FALSE)
			| AD: ScrollDir(w, Controllers.incLine, 0, TRUE,  TRUE)
			ELSE scrolled := FALSE
			END;
			IF scrolled THEN ch := 0X END
		END;
		IF ch = 0X THEN Dialog.Beep ELSE w.ForwardCtrlMsg(msg) END
	END StdPasteChar;

	PROCEDURE ForwardCtrlMsg* (w: Window; VAR msg: Controllers.Message);
	(** Forward a controller message into the window, providing standard behaviour: wheel scrolling, frame and window dumping, and scroll position respect. If these standard behaviours are to be avoided, call Windows.Window.ForwardCtrlMsg instead *)
	BEGIN
		Controllers.SetCurrentPath(Controllers.targetPath);	(* AD: copypasted from HostWindows; not sure what it's for *)
		WITH
		| msg: Controllers.WheelMsg DO StdWheel(w, msg)
		| msg: Controllers.TrackMsg DO StdTrack(w, msg)
		| msg: Controllers.EditMsg DO 
			IF msg.op = Controllers.pasteChar THEN StdPasteChar(w, msg) ELSE w.ForwardCtrlMsg(msg)
			END
		ELSE w.ForwardCtrlMsg(msg)
		END;
		Controllers.ResetCurrentPath();
		Properties.IncEra
	END ForwardCtrlMsg;
	
	PROCEDURE Wheel* (w: Window; op, x, y: Pixels; mod: SET);
	(** Handle a mouse wheel event in the window.
		Intended for use from platform modules by WindowBackend extensions *)
		VAR msg: Controllers.WheelMsg; u: UCs;
	BEGIN
		u := w.port.unit; msg.modifiers := mod;
		msg.x := x * u; msg.y := y * u; msg.done := FALSE; msg.op := op; msg.nofLines := 1; 
		ForwardCtrlMsg(w, msg)
	END Wheel;
	
	PROCEDURE UpdateCursor* (w: Window; x, y: Pixels; modifiers: SET);
		VAR pw, ph: INTEGER; msg: Controllers.PollCursorMsg; cur: INTEGER;
	BEGIN
		w.GetSize(pw, ph);
		IF (x >= 0) & (x < pw) & (y >= 0) & (y < ph) THEN
			msg.x := x * w.frame.unit - w.frame.gx; 
			msg.y := y * w.frame.unit - w.frame.gy; 
			msg.cursor := Ports.arrowCursor;
			msg.modifiers := modifiers;
			(* does this ↓ need to be bracketed by Controllers.SetCurrentPath/ResetCurrentPath? *)
			w.ForwardCtrlMsg(msg); cur := msg.cursor
		ELSE cur := Ports.arrowCursor
		END;
		IF cur >= 0 THEN w.frame.SetCursor(cur) END
	END UpdateCursor;
	
	PROCEDURE Track* (w: Window; x, y: Pixels; modifiers: SET);
	(** Handle a mouse down event in window. Intended for use in HostWidows by WindowBackend extensions *)
		VAR u: UCs; track: Controllers.TrackMsg;
	BEGIN
		u := w.port.unit; track.modifiers := modifiers; track.x := x * u; track.y := y * u;
		ForwardCtrlMsg(w, track)
	END Track;

	PROCEDURE (w: Window) KeyDown* (ch: CHAR; modifiers: SET);
		VAR msg: Controllers.EditMsg; 
	BEGIN ASSERT(w # NIL, 20);
		msg.op := Controllers.pasteChar; msg.char := ch; msg.modifiers:= modifiers; 
		ForwardCtrlMsg(w, msg)
	END KeyDown;
	
	PROCEDURE (w: Window) MouseDown* (x, y, time: INTEGER; modifiers: SET), EMPTY;

	PROCEDURE (wb: WindowBackend) GetOrigin* (OUT xRoot, yRoot: Pixels
	): BOOLEAN, NEW, EXTENSIBLE;
	(** Translate wb's (0,0) into global screen coordinates, if possible. Result is TRUE if translation was possible/successful, FALSE otherwise. Post: res = FALSE => xRoot, yRoot undefined *)
	BEGIN RETURN FALSE
	END GetOrigin;
	
	PROCEDURE (w: Window) IsTool* (): BOOLEAN, NEW, EXTENSIBLE;
	(** Returns TRUE if w was opened as a Tool dialog, FALSE otherwise. *)
	BEGIN RETURN Windows.isTool IN w.flags
	END IsTool;
	
	PROCEDURE Reset (s: StdSequencer);
	BEGIN
		s.trapEra := Kernel.trapCount;
		IF (s.entryLevel # 0) OR (s.nestLevel # 0) THEN
			s.modLevel := 0;
			s.entryLevel := 0;
			s.nestLevel := 0;
			s.lastSt := NIL;
			s.lastOp := NIL;
			s.script := NIL;
			s.noUndo := FALSE;
			s.undo := NIL; s.redo := NIL;
			s.invisibleLevel := 0;
			s.transparentLevel := 0;
			s.notRecordedLevel := 0
		END
	END Reset;

	PROCEDURE NewSequencer*(): Sequencers.Sequencer;
		(** Creates and returns a new sequencer. *)
		VAR s: StdSequencer;
	BEGIN
		NEW(s); Reset(s); RETURN s
	END NewSequencer;


	
	PROCEDURE AppendInt (VAR s: ARRAY OF CHAR; n: INTEGER; useSeparators: BOOLEAN);
		VAR len: INTEGER; i, j: INTEGER; d: ARRAY 32 OF CHAR;
	BEGIN
		ASSERT(n >= 0, 20);
		i := 0; REPEAT
			d[i] := CHR(30H + n MOD 10); INC(i); n := n DIV 10;
			IF useSeparators & (i MOD 4 = 3) & (n # 0) THEN d[i] := "'"; INC(i) END
		UNTIL n = 0;
		len := LEN(s) - 1;
		j := 0; WHILE s[j] # 0X DO INC(j) END;
		IF j + i < len THEN
			REPEAT DEC(i); s[j] := d[i]; INC(j) UNTIL i = 0;
			s[j] := 0X
		END
	END AppendInt;

	PROCEDURE Append (VAR s: ARRAY OF CHAR; t: ARRAY OF CHAR);
		VAR len: INTEGER; i, j: INTEGER; ch: CHAR;
	BEGIN
		len := LEN(s);
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		j := 0; REPEAT ch := t[j]; s[i] := ch; INC(j); INC(i) UNTIL (ch = 0X) OR (i = len);
		s[len - 1] := 0X
	END Append;

	PROCEDURE StripTitle (VAR s: Views.Title);
		VAR i: INTEGER;
	BEGIN
		IF s[0] = "<" THEN
			i := 1; WHILE (s[i] # ">") & (s[i] # 0X) DO s[i - 1] := s[i]; INC(i) END;
			DEC(i); s[i] := 0X
		END
	END StripTitle;
	
	PROCEDURE GenTitle (w: Window; name: ARRAY OF CHAR; VAR title: ARRAY OF CHAR);
	(* generate window title for a document *)
		VAR newName: ARRAY 64 OF CHAR; i: INTEGER;
	BEGIN
		IF w.sub THEN title[0] := "<"; title[1] := 0X ELSE title[0] := 0X END;
		IF name # "" THEN
			i := 0;
			WHILE name[i] # 0X DO INC(i) END;
			IF (i > 4) & (name[i-4] = ".") & (CAP(name[i-3]) = "O") & (CAP(name[i-2]) = "D") & (CAP(name[i-1]) = "C")
			THEN
				name[i-4] := 0X
			END;
			Append(title, name)
		ELSE
			Dialog.MapString(untitledKey, newName);
			Append(title, newName); AppendInt(title, newNumber, noSeparators);
			INC(newNumber)
		END;
		IF w.sub THEN Append(title, ">") END
	END GenTitle;
	
	PROCEDURE GenPathTitle (w: Window; OUT title: ARRAY OF CHAR);
		CONST mlen = LEN(Views.Title) - 1;
		VAR loc: Files.Locator; path: Files.Name;
			 ch: CHAR; s1, s2: Views.Title; i, j: INTEGER;
	BEGIN
		loc := w.loc;
		title := "";
		Dialog.GetLocPath(loc, path);
		w.path := path$;
		i := 0; ch := path[0]; j := 0; s2 := "";
		WHILE (ch # 0X) & (j < mlen) DO
			IF  (ch = "/") OR (ch = "\") THEN s1[j] := 0X; s2 := s1$; j := 0
			ELSE s1[j] := ch; INC(j)
			END;
			INC(i); ch := path[i]
		END;
		s1[j] := 0X;
		i := 0;
		WHILE (i < 4) & (i < j) DO s1[i] := CAP(s1[i]); INC(i) END;
		s1[i] := 0X;
		IF s1 = "MOD" THEN
			IF LEN(title) <= LEN(s2$) + 2 THEN s2[LEN(title) - 3] := 0X END;
			title := "(" + s2$ + ")" 
		ELSIF s1 = "DOCU" THEN
			IF LEN(title) <= LEN(s2$) + 2 THEN s2[LEN(title) - 3] := 0X END;
			title := "{" + s2$ + "}" 
		ELSIF s1 = "RSRC" THEN
			IF LEN(title) <= LEN(s2$) + 2 THEN s2[LEN(title) - 3] := 0X END;
			title := "[" + s2$ + "]" 
		END;
		Append(title, w.name)
	END GenPathTitle;

	PROCEDURE (wb: WindowBackend) SetTitle* (IN title: Views.Title), NEW, ABSTRACT;

	PROCEDURE (w: Window) SetTitle* (title: Views.Title);
	VAR t0: Views.Title; h: Window;
	BEGIN
		w.initialTitle := title$; Dialog.MapString(title, t0); 
		h := w;
		REPEAT
			GenTitle(w, t0, w.title);
			w.backend.SetTitle(w.title);
			w := w.link(Window)
		UNTIL w = h
	END SetTitle;

	PROCEDURE (w: Window) RefreshTitle* (), EXTENSIBLE;
		VAR title: Views.Title;
	BEGIN
		Dialog.MapString(w.initialTitle, title);
		w.SetTitle(title)
	END RefreshTitle;

	PROCEDURE (w: Window) GetTitle* (OUT title: Views.Title);
	BEGIN
		title := w.initialTitle$;
		IF title = "" THEN
			title := w.title$;
			StripTitle(title)
		END
	END GetTitle;
	
	PROCEDURE (w: Window) SetSpec* (loc: Files.Locator; name: Files.Name; conv: Converters.Converter);
		VAR title: Views.Title;
	BEGIN
		IF name # "" THEN
			Files.dir.GetFileName(name, Files.docType, name)
		END;
		w.SetSpec^ (loc, name, conv);
		IF (loc # NIL) THEN GenPathTitle(w, title); w.SetTitle(title) END
	END SetSpec;

	(* Procedures relating to selecting (activating) a window *)
	
	PROCEDURE MarkTitle (w: Window);
		VAR mark2, mark1: ARRAY 16 OF CHAR;
	BEGIN ASSERT(w # NIL, 20);
		IF w.backend # NIL THEN
			IF (w = front) & (w = target) THEN
				mark2 := param.frontTargetStr$
			ELSIF w = front THEN
				mark2 := param.frontStr$
			ELSIF w = target THEN
				mark2 := param.targetStr$
			ELSE mark2 := ""
			END;
			IF w = mark THEN mark1 := param.markStr$ ELSE mark1 := "" END;
			w.backend.SetTitle(mark1$ + w.title + mark2$) 
		END
	END MarkTitle;

	PROCEDURE SetFrontWindow (w: Window);
		VAR u, v: Window;
	BEGIN ASSERT(w # NIL, 20);
		u := front; v := target;
		IF w.IsTool() & (front # NIL) & ~front.IsTool() THEN target := front
		ELSE target := w END;
		front := w;
		IF param.markTitles THEN 
			MarkTitle(front);
			IF front # target THEN MarkTitle(target) END;
			IF (u # NIL) & (u # front) & (u # target) THEN MarkTitle(u) END;
			IF (v # NIL) & (v # front) & (v # target) THEN MarkTitle(v) END;
		END;
		IF (u # NIL) & (u # front) & (u # target) THEN
			Views.SetRoot(u.frame, u.frame.view, FALSE, u.flags);
		END;
		IF (v # NIL) & (v # front) & (v # target) THEN
			Views.SetRoot(v.frame, v.frame.view, FALSE, v.flags);
		END;
		IF front = target THEN 
			Views.SetRoot(front.frame, front.frame.view, TRUE, 
				front.flags + { StdDocuments.front, StdDocuments.target }
			);
		ELSE 
			Views.SetRoot(front.frame, front.frame.view, TRUE,
				front.flags + { StdDocuments.front }
			);
			Views.SetRoot(target.frame, target.frame.view, TRUE,
				target.flags + { StdDocuments.target }
			);
		END
	END SetFrontWindow;

	PROCEDURE Mark (w: Window; show, focus: BOOLEAN);
		VAR mark: Controllers.MarkMsg;
	BEGIN
		mark.show := show; mark.focus := focus; w.ForwardCtrlMsg(mark); Properties.IncEra
	END Mark;

	PROCEDURE (h: Hook) NotifySelect*, NEW, EMPTY;
	(** Called after the window that h is attached to has been selected (activated). *)

	PROCEDURE (wb: WindowBackend) Select*, NEW, ABSTRACT;
	(** Make wb.win activated (selected) host window (bring it up). If the host procedure(s) imply a callback, then such callback should not eventually call Windows.Select, otherwise unterminated recusion will take place *)
	
	PROCEDURE Select (w: Window; lazy: BOOLEAN);
	(** Select (activate, bring to front) w. *)
		CONST guardCheck = 4;	(* adapted from HostWindows.ChildActivateHandler *)
		VAR u: Window;
	BEGIN
		ASSERT(w # NIL, 20);
		ASSERT(zList # NIL, 21 (* w is selected => w is in zList => zList # NIL *));
		(* This is where the select candidate (the alternative window) should be suggested - cf. the F9 problem *)
		IF w # front THEN 
			IF front # NIL THEN Mark(front, FALSE, FALSE) END;
			IF (target # NIL) & (target # front) THEN Mark(target, FALSE, FALSE) END;
			SetFrontWindow(w);
			(* A tool keeps the target document's selection visible on the inactive path. *)
			IF target # front THEN Mark(target, TRUE, FALSE) END;
			IF zList # w THEN
				u := zList; WHILE (u.next # NIL) & (u.next # w) DO u := u.next END;
				ASSERT(u.next # NIL, 21 (* w is not in Z-list - it hasn't been Open()ed properly *));
				u.next := u.next.next; w.next := zList; zList := w
			END; ASSERT(zList = w, 60);
			IF w.backend # NIL THEN w.backend.Select END;
			(* k8: we need proper frame structure to make things like TextCmds.FindFirst work. so let's built it!
				note that only newly created windows has no root host. *)
				(* HACK: if this is first ever window, don't do it; otherwise main window flashes *)
			IF (zList.next # NIL) & (zList.next # zList) THEN
				IF (w.frame # NIL) & IsDetached(w) & (Views.HostOfRoot(w.frame) = NIL) & ~Views.HasAnyFrames(w.frame) THEN
					Views.BuildFrameTree(w.frame, FALSE)
				END
			END;
			Mark(w, TRUE, FALSE);
			IF w = front THEN
				IF w.hook # NIL THEN w.hook.NotifySelect() END;
				Dialog.Notify(0, 0, {guardCheck}) 	(* adapted from HostWindows.ChildActivateHandler *)
			END
		ELSE IF w.backend # NIL THEN w.backend.Select END
		END;
		backoff.front := NIL;
		IF lazy THEN
			Views.Update(w.doc, Views.keepFrames)
		ELSE   (* experimental, need to be tested  *)
			Validate(w)  (* this ensures w has actualized frame structure right after Select *)
		END
	END Select;
	
	
	(* Procedures relating to closing a window *)
	

	PROCEDURE (wb: WindowBackend) Close-, NEW, ABSTRACT;
	(** Close/Remove host window.
		Pre: .port # NIL, .link = .next = .rootFrame = .frame = .loc = .layout = .hook = NIL *)

	PROCEDURE Close (w: Window);
	(** Close window unconditionally and without dialogueing with the user *)
		VAR t, u, v: Window; s: Sequencers.Sequencer;
	BEGIN
		ASSERT(w # NIL, 20); 
		u := zList; WHILE (u # NIL) & (u # w) DO u := u.next END;
		IF u # NIL THEN
			v := w; 
			IF w.sub THEN WHILE v.link # w DO v := v.link(Window) END END;
			REPEAT
				u := v.link(Window);	(* u is to be removed; u # NIL 'cause .link is a ring, so v.link = v if 1 wnd in ring *)
				IF front = u THEN front := NIL END;
				IF target = u THEN target := NIL END;	(* new values for front and target are not needed at this point: after a window is closed, the underlying host OS or library will activate another window, and that will cause a call to Select, which will assign new values for front and target *)
				IF mark = u THEN mark := NIL END;
				(* remove u from window list *)
				IF u = zList THEN zList := zList.next
				ELSE
					t := zList; WHILE (t.next # NIL) & (t.next # u) DO t := t.next END;
					ASSERT(t.next # NIL, 100 (* t is in window ring but not in window list *) );
					t.next := t.next.next
				END;
				u.next := NIL;
				s := u.seq; WITH s: StdSequencer DO IF s.home = u THEN s.home := NIL END ELSE END;
				(* revisit when the duplicity Windows.StdSequencer <=> StdWindows.StdSequencer is resolved *)
				u.backend.SetSize(0, 0); (* may or may not be necessary *)
				u.Close;
				u.hook := NIL; 
				u.backend.Close; u.next := NIL; 
				u.backend.win := NIL; u.backend := NIL
			UNTIL w.sub OR (v.link = NIL)
		END;
		IF (front = NIL) & (zList # NIL) THEN Select(zList, Windows.lazy) 
		END
	END Close;
	
	PROCEDURE GuardedClose* (w: Windows.Window);
	BEGIN
		ASSERT(guardedCloseHook # NIL, 39);
		guardedCloseHook(w)
	END GuardedClose;
	
	(* Procedures relating to opening a window *)
	
	PROCEDURE (w: Window) NewContext* (): Models.Context, NEW, EXTENSIBLE;
	(** Allows extenders to provide a custom context for the window's document. *)
		VAR c: DocContext;
	BEGIN NEW(c); c.win := w; RETURN c
	END NewContext;

	PROCEDURE (wb: WindowBackend) Open- (isDialog: BOOLEAN), NEW, EMPTY;
	(** Called by Windows to have wb opened as a host window *)

	PROCEDURE (VAR so: SafeOpen) Do;
		VAR w: Window; doc: Documents.Document;
			s: Sequencers.Sequencer; f: Views.Frame; any: ANYPTR; wid, hei: Pixels; dw, dh: UCs;
	BEGIN
		w := so.w;
		doc := so.doc;
		
		IF (doc IS Documents.Document) & ~ (doc IS StdDocuments.Document) THEN
			doc := Documents.DuplicateAs(doc, StdDocuments.dir);
		END;
	
		GetInitSize(w, doc, wid, hei, so.flags);
		
		IF w.seq = NIL THEN
			any := doc.Domain().GetSequencer();
			IF any # NIL THEN
				ASSERT(any IS Sequencers.Sequencer, 26);
				s := any(Sequencers.Sequencer)
			ELSE
				s := NewSequencer();	(* AD: in the future, I think Windows will have to ask Window for a new Sequencer - like it does ask .doc for a new frame *)
				doc.Domain().SetSequencer(s)
			END
		END;
		WITH s: StdSequencer DO
			IF s.home = NIL THEN s.home := w END
		ELSE
		END;
		
		doc.InitContext(w.NewContext());
		doc.GetNewFrame(f); 
		WITH f: Views.RootFrame DO 
			f.ConnectTo(w.port);
			Views.SetRoot(f, doc, FALSE, so.flags);
			Windows.SetupWindow(w, f, doc, s, so.flags)
		END;
		
		w.SetSize(wid, hei); (* SetSize calls Views.AdaptRoot *)
		(* check: *) doc.context.GetSize(dw, dh); ASSERT((dw # 0) & (dh # 0), 61);
		
		w.backend.Open(IsDialog(so.flags, doc));
		
		(* AD: copypasted this section from (Lin)HostWindows.Directory.Open; without this, some documents can't forward messages down to their view; I haven't figured out exact conditions. Also, it's unclear why this had to be done explicitly, and why it's not done somewhere in Documents. *)
		
		w.SetTitle(so.title)
	END Do;
	
	PROCEDURE Open (w: Window; doc: Documents.Document; flags: SET; IN title: Views.Title);
	(** Open doc in w with flags and title. *)
	(* Protected: if any traps occur in extenders' procedures involved in opening a window, the module Windows and it's data structures remain intact. *)
		VAR v: Views.View; so: SafeOpen;
	BEGIN
		ASSERT(w # NIL, 20); ASSERT(doc # NIL, 21); ASSERT(doc.context = NIL, 22);
		v := doc.ThisView(); ASSERT(v # NIL, 23);
		ASSERT(w.doc = NIL, 24); ASSERT(w.port # NIL, 25); ASSERT(doc.Domain() # NIL, 27);
		ASSERT(v.context # NIL, 28);
		so.w := w; so.doc := doc; so.flags := flags; so.title := title;
		Services.Try(so);
		dir.l := none; 
		IF ~so.trapped THEN
			w.next := zList; zList := w; Select(w, Windows.lazy); 
			(* here w is "registered" and becomes known to the rest of module Windows. If a trap occurs before this point, the window is not registered, and a poorly initialized window does not cause further traps. *)
			(* Validate(w)	(* this makes sure w's frame tree is built *)	*)
		ELSE w.next := NIL; w.hook := NIL; w.backend := NIL
		END
	END Open;

	PROCEDURE RestoreSequencer (seq: Sequencers.Sequencer);
		VAR w: Window;
	BEGIN
		w := First();
		WHILE w # NIL DO
			ASSERT(w.frame # NIL, 100);
			IF (seq = NIL) OR (w.seq = seq) THEN
				Validate(w)	(* causes redrawing of BlackBox region *)
			END;
			w := Next(w)
		END
	END RestoreSequencer;

	PROCEDURE ValidateRing* (w: Window);
	(** Validates all windows displaying w's domain, or all windows if w = NIL *)
	BEGIN
		IF w = NIL THEN RestoreSequencer(NIL) 
		ELSE ASSERT(w.seq # NIL, 21); RestoreSequencer(w.seq)
		END
	END ValidateRing;

	PROCEDURE (wb: WindowBackend) IsInside* (xRoot, yRoot: Pixels; OUT inClient: BOOLEAN
	): BOOLEAN, NEW, EXTENSIBLE;
	(** Tells if screen pixel coordinates xRoot, yRoot are inside wb; tell if they are inside wb's client area (as opposed to window decorations and borders)*)
		VAR x, y: Pixels;
	BEGIN 
		IF wb.GetOrigin(x, y) THEN x := xRoot - x; y := yRoot - y;
			inClient := (0 <= x) & (x <= wb.win.width) & (0 <= y) & (y <= wb.win.height)
		ELSE inClient := FALSE
		END;
		RETURN inClient
	END IsInside;


	(* Directory *)

	PROCEDURE (d: StdDirectory) First* (): Window;
	BEGIN RETURN First() END First;

	PROCEDURE (d: StdDirectory) Next* (w: Windows.Window): Window;
	BEGIN RETURN Next(w(Window)) END Next;


	PROCEDURE (wb: WindowBackend) Port* (): Ports.Port, NEW, ABSTRACT;
	(** Returns the port that wb is displayed on. *)

	PROCEDURE (d: StdDirectory) New* (): Window;
	(** Produce a new window. Post: .backend # NIL, .port # NIL *)
		VAR w: Window; wb: WindowBackend; p: Ports.Port;
	BEGIN 
		ASSERT(backendDir # NIL, 20);
		NEW(w); wb := backendDir.NewBackend(); 
		ASSERT(wb # NIL, 60); w.InitBackend(wb);
		p := wb.Port();
		IF p = NIL THEN w := NIL ELSE w.Init(p) END;
		ASSERT(w # NIL, 70);
		RETURN w
	END New;

	PROCEDURE (d: StdDirectory) Open* (w: Windows.Window; doc: Documents.Document; flags: SET; name: Views.Title; loc: Files.Locator; fname: Files.Name; conv: Converters.Converter );
	BEGIN
		ASSERT(w # NIL, 20); ASSERT(doc # NIL, 21); ASSERT(doc.context = NIL, 22); 
		IF (d.l >= 0) & (d.t >= 0) & ~((d.l = 0) & (d.t = 0) & (d.r = 0) & (d.b = 0)) THEN 
			dir.l := d.l; dir.t := d.t; dir.r := d.r; dir.b := d.b 
		END;
		Open(w(Window), doc, flags, name);
		w(Window).SetSpec(loc, fname, conv);
		d.l := none
	END Open;

	PROCEDURE (d: StdDirectory) OpenSubWindow* (w: Windows.Window; doc: Documents.Document; flags: SET; title: Views.Title );
		(** Open w as a subwindow (a duplicate window) displaying doc with flags set to flgs and title set to title *)
		VAR u: Window;
	BEGIN
		ASSERT(w # NIL, 20); ASSERT(doc # NIL, 21);
		u := d.First();
		WHILE (u # NIL) & (u.seq # doc.Domain().GetSequencer()) DO u := d.Next(u) END;
		IF u # NIL THEN
			WHILE u.sub DO u := u.link(Window) END;
			u.GetTitle(title);
			Open(w(Window), doc, flags, title);
			IF w.doc # NIL THEN w.SetSpec(u.loc, u.name, u.conv) END
		ELSE
			Open(w(Window), doc, flags, title)
		END
	END OpenSubWindow;


	PROCEDURE (d: StdDirectory) Focus* (targetPath: BOOLEAN): Window;
	(** Returns the focus window on one of focus paths: Controllers.targetPath or Controllers.frontPath. Preserved for compatibility reasons, now is duplicated by global variables front and target. May be deprecated. *)
		VAR res: Window;
	BEGIN
		IF targetPath = Controllers.targetPath THEN res := target ELSE res := front END;
		RETURN res
	END Focus;

	PROCEDURE (d: StdDirectory) GetThisWindow* (p: Ports.Port; px, py: Pixels; OUT x, y: Pixels; OUT w: Windows.Window );
	(** Get topmost w such that (px,py) on p fall inside w, and translate (px,py) to (x,y) in w's coordinates *)
		VAR xRoot, yRoot: Pixels; found, dummy: BOOLEAN; ww: Window; 
	BEGIN
		IF (PortOrigin # NIL) & PortOrigin(p, xRoot, yRoot) THEN INC(px, xRoot); INC(py, yRoot);
			ww := First(); found := FALSE;
			WHILE ~found & (ww # NIL) DO
				found := ww.backend.GetOrigin(xRoot,yRoot) & ww.backend.IsInside(px,py,dummy);
				IF ~found THEN ww := Next(ww) END
			END;
			IF found THEN x := px - xRoot; y := py - yRoot END
		END;
		w := ww
	END GetThisWindow;

	PROCEDURE (d: StdDirectory) Select* (w: Windows.Window; lazy: BOOLEAN);
	BEGIN Select(w(Window), lazy)
	END Select;

	PROCEDURE (d: StdDirectory) Close* (w: Windows.Window);
	BEGIN Close(w(Window));
	END Close;

	PROCEDURE (d: StdDirectory) UUpdate* (w: Window), NEW;
	(** deprecated, use Windows.UpdateRing instead *)
	BEGIN ValidateRing(w) END UUpdate;

	PROCEDURE (d: StdDirectory) GetBounds* (OUT w, h: INTEGER);
	BEGIN
		backendDir.GetBounds(w, h)
	END GetBounds;


	(* Contexts *)

	PROCEDURE (c: Context) Normalize (): BOOLEAN, EXTENSIBLE;
	BEGIN RETURN TRUE
	END Normalize;

	PROCEDURE (c: Context) ThisModel (): Models.Model, EXTENSIBLE; 
	BEGIN RETURN NIL 
	END ThisModel;

	PROCEDURE (c: DocContext) GetSize (OUT w, h: UCs);
		VAR p: Ports.Port;
	BEGIN
		(* c.win.layout = NIL => c.win has been closed; w=h=0 signals to Views that doc's frame may be Closed as well *)
		p := c.win.port;
		IF p # NIL THEN
			c.win.GetSize(w, h); w := w * p.unit; h := h * p.unit
		ELSE
			w := 0; h := 0
		END
	END GetSize;

	(* sequencing utilities *)

	PROCEDURE Prepend (s: Script; st: Stores.Store; IN name: Stores.OpName; op: Stores.Operation);
		VAR e: OpElem;
	BEGIN
		ASSERT(op # NIL, 20);
		NEW(e); e.st := st; e.op := op; e.name := name;
		e.next := s.list; s.list := e
	END Prepend;

	PROCEDURE Push (VAR list, e: OpElem);
	BEGIN
		e.next := list; list := e
	END Push;

	PROCEDURE Pop (VAR list, e: OpElem);
	BEGIN
		e := list; list := list.next
	END Pop;

	PROCEDURE Reduce (VAR list: OpElem; max: INTEGER);
		VAR e: OpElem;
	BEGIN
		e := list; WHILE (max > 1) & (e # NIL) DO DEC(max); e := e.next END;
		IF e # NIL THEN e.next := NIL END
	END Reduce;
	
	PROCEDURE (r: Reducer) Reduce (full: BOOLEAN);
		VAR e: OpElem; n: INTEGER; w: Window;
	BEGIN
		IF dir # NIL THEN
			w := First();
			WHILE w # NIL DO
				IF w.seq IS StdSequencer THEN
					IF full THEN
						n := 1
					ELSE
						n := 0; e := w.seq(StdSequencer).undo;
						WHILE e # NIL DO INC(n); e := e.next END;
						IF n > 20 THEN n := n DIV 2 ELSE n := 10 END
					END;
					Reduce(w.seq(StdSequencer).undo, n)
				END;
				w := Next(w)
			END
		END;
		Kernel.InstallReducer(r)
	END Reduce;

	PROCEDURE Neutralize (st: Stores.Store);
		VAR neutralize: Models.NeutralizeMsg;
	BEGIN
		IF st # NIL THEN	(* st = NIL for scripts *)
			WITH st: Models.Model DO
				Models.Broadcast(st, neutralize)
			| st: Views.View DO
				st.Neutralize
			ELSE
			END
		END
	END Neutralize;

	PROCEDURE Do (s: StdSequencer; st: Stores.Store; op: Stores.Operation);
	BEGIN
		INC(s.entryLevel); s.lastSt := NIL; s.lastOp := NIL;
		Neutralize(st); op.Do;
		DEC(s.entryLevel)
	END Do;

	PROCEDURE AffectsDoc (s: StdSequencer; st: Stores.Store): BOOLEAN;
		VAR v, w: Windows.Window;
	BEGIN
		w := s.home;
		IF (w = NIL) OR (st = w.doc) OR (st = w.doc.ThisView()) THEN
			RETURN TRUE
		ELSE
			v := w.link;
			WHILE (v # w) & (st # v.doc) & (st # v.doc.ThisView()) DO v := v.link END;
			RETURN v = w
		END
	END AffectsDoc;


	(* Script *)

	PROCEDURE (s: Script) Do;
		VAR e, f, g: OpElem;
	BEGIN
		e := s.list; f := NIL;
		REPEAT
			Neutralize(e.st); e.op.Do;
			g := e.next; e.next := f; f := e; e := g
		UNTIL e = NIL;
		s.list := f
	END Do;


	(* StdSequencer *)

	PROCEDURE (s: StdSequencer) Handle (VAR msg: ANYREC);
	(* send message to all windows attached to s *)
		VAR w: Window; 
	BEGIN 
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		WITH msg: Models.Message DO
			IF msg IS Models.UpdateMsg THEN
				Properties.IncEra;
				IF s.entryLevel = 0 THEN
					(* updates in dominated model bypassed the sequencer *)
					Reset(s);	(* panic reset: clear sequencer *)
					INC(s.modLevel)	(* but leave dirty *)
				END
			END;
			w := First();
			WHILE w # NIL DO
				IF w.seq = s THEN w.BroadcastModelMsg(msg) END;
				w := Next(w)
			END
		| msg: Views.Message DO
			w := First();
			WHILE w # NIL DO
				IF w.seq = s THEN w.BroadcastViewMsg(msg) END;
				w := Next(w)
			END
		ELSE
		END
	END Handle;

	PROCEDURE (s: StdSequencer) Dirty (): BOOLEAN;
	BEGIN
		RETURN s.modLevel > 0
	END Dirty;

	PROCEDURE (s: StdSequencer) SetDirty (dirty: BOOLEAN);
	BEGIN
		IF dirty THEN INC(s.modLevel) ELSE s.modLevel := 0 END
	END SetDirty;

	PROCEDURE (s: StdSequencer) LastOp (st: Stores.Store): Stores.Operation;
	BEGIN
		ASSERT(st # NIL, 20);
		IF s.lastSt = st THEN RETURN s.lastOp ELSE RETURN NIL END
	END LastOp;


	PROCEDURE (s: StdSequencer) BeginScript (IN name: Stores.OpName; VAR script: Stores.Operation);
		VAR sop: Script;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		INC(s.nestLevel);
		IF (s.nestLevel = 1) & (s.invisibleLevel = 0)
		& (s.transparentLevel = 0) & (s.notRecordedLevel = 0) THEN
			INC(s.modLevel)
		END;
		s.lastSt := NIL; s.lastOp := NIL;
		NEW(sop);
		sop.up := s.script; sop.list := NIL; sop.level := s.nestLevel; sop.name := name;
		s.script := sop;
		script := sop
	END BeginScript;

	PROCEDURE (s: StdSequencer) Do (st: Stores.Store; IN name: Stores.OpName; op: Stores.Operation);
		VAR e: OpElem;
	BEGIN
		ASSERT(st # NIL, 20); ASSERT(op # NIL, 21);
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		Do(s, st, op);
		IF s.noUndo THEN	(* cannot undo: unbalanced BeginModification pending *)
			s.lastSt := NIL; s.lastOp := NIL
		ELSIF (s.entryLevel = 0)	(* don't record when called from within op.Do *)
		& AffectsDoc(s, st) THEN	(* don't record when Do affected child window only *)
			s.lastSt := st; s.lastOp := op;
			IF s.notRecordedLevel = 0 THEN
				s.redo := NIL;	(* clear redo stack *)
			END;
			IF s.script # NIL THEN
				Prepend(s.script, st, name, op)
			ELSE
				IF (s.invisibleLevel = 0) & (s.transparentLevel = 0) & (s.notRecordedLevel = 0) THEN
					INC(s.modLevel) 
				END;
				IF s.notRecordedLevel = 0 THEN
					NEW(e); e.st := st; e.op := op; e.name := name;
					e.invisible := s.invisibleLevel > 0; e.transparent := s.transparentLevel > 0;
					Push(s.undo, e)
				END
			END
		END
	END Do;

	PROCEDURE (s: StdSequencer) Bunch (st: Stores.Store);
		VAR lastOp: Stores.Operation;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		ASSERT(st # NIL, 20); ASSERT(st = s.lastSt, 21);
		lastOp := s.lastOp;
		Do(s, st, lastOp);
		IF s.noUndo THEN
			s.lastSt := NIL; s.lastOp := NIL
		ELSIF (s.entryLevel = 0)	(* don't record when called from within op.Do *)
		& AffectsDoc(s, st) THEN	(* don't record when Do affected child window only *)
			s.lastSt := st; s.lastOp := lastOp
		END
	END Bunch;

	PROCEDURE (s: StdSequencer) EndScript (script: Stores.Operation);
		VAR e: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		ASSERT(script # NIL, 20); ASSERT(s.script = script, 21);
		WITH script: Script DO
			ASSERT(s.nestLevel = script.level, 22);
			s.script := script.up;
			IF s.entryLevel = 0 THEN	(* don't record when called from within op.Do *)
				IF script.list # NIL THEN
					IF s.script # NIL THEN
						Prepend(s.script, NIL, script.name, script)
					ELSE	(* outermost scripting level *)
						s.redo := NIL;	(* clear redo stack *)
						IF ~s.noUndo THEN
							NEW(e); e.st := NIL; e.op := script; e.name := script.name;
							e.invisible := s.invisibleLevel > 0; e.transparent := s.transparentLevel > 0;
							IF s.notRecordedLevel=0 THEN Push(s.undo, e) END
						END;
						s.lastSt := NIL; s.lastOp := NIL
					END
				ELSE
					IF (s.script = NIL) & (s.modLevel > 0)
						& (s.invisibleLevel = 0) & (s.transparentLevel = 0) THEN 
						DEC(s.modLevel)
					END
				END
			END
		END;
		DEC(s.nestLevel);
		IF s.nestLevel = 0 THEN ASSERT(s.script = NIL, 22); s.noUndo := FALSE END
	END EndScript;

	PROCEDURE (s: StdSequencer) StopBunching;
	BEGIN
		s.lastSt := NIL; s.lastOp := NIL
	END StopBunching;

	PROCEDURE (s: StdSequencer) BeginModification (type: INTEGER; st: Stores.Store);
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		IF s.nestLevel < LEN(s.modStack) THEN
			s.modStack[s.nestLevel].store := st; s.modStack[s.nestLevel].type := type
		END;
		INC(s.nestLevel);
		IF type = Sequencers.notUndoable THEN
			INC(s.modLevel);	(* unbalanced! *)
			s.noUndo := TRUE; s.undo := NIL; s.redo := NIL;
			s.lastSt := NIL; s.lastOp := NIL;
			INC(s.entryLevel)	(* virtual entry of modification "operation" *)
		ELSIF type = Sequencers.invisible THEN
			INC(s.invisibleLevel)
		ELSIF type = Sequencers.clean THEN
			INC(s.transparentLevel)
		ELSIF type = notRecorded THEN
			INC(s.notRecordedLevel)
		END
	END BeginModification;

	PROCEDURE (s: StdSequencer) EndModification (type: INTEGER; st: Stores.Store);
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		ASSERT(s.nestLevel > 0, 20);
		IF s.nestLevel <= LEN(s.modStack) THEN
			ASSERT((s.modStack[s.nestLevel - 1].store = st) & (s.modStack[s.nestLevel - 1].type = type), 21)
		END;
		DEC(s.nestLevel);
		IF type = Sequencers.notUndoable THEN
			DEC(s.entryLevel)
		ELSIF type = Sequencers.invisible THEN
			DEC(s.invisibleLevel)
		ELSIF type = Sequencers.clean THEN
			DEC(s.transparentLevel)
		ELSIF type = notRecorded THEN
			DEC(s.notRecordedLevel)
		END;
		IF s.nestLevel = 0 THEN ASSERT(s.script = NIL, 22); s.noUndo := FALSE END
	END EndModification;

	PROCEDURE (s: StdSequencer) CanUndo (): BOOLEAN;
		VAR op: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		op := s.undo;
		WHILE (op # NIL) & op.invisible DO op := op.next END;
		RETURN op # NIL
	END CanUndo;

	PROCEDURE (s: StdSequencer) CanRedo (): BOOLEAN;
		VAR op: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		op := s.redo;
		WHILE (op # NIL) & op.invisible DO op := op.next END;
		RETURN op # NIL
	END CanRedo;

	PROCEDURE (s: StdSequencer) GetUndoName (VAR name: Stores.OpName);
		VAR op: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		op := s.undo;
		WHILE (op # NIL) & op.invisible DO op := op.next END;
		IF op # NIL THEN name := op.name$ ELSE name[0] := 0X END
	END GetUndoName;

	PROCEDURE (s: StdSequencer) GetRedoName (VAR name: Stores.OpName);
		VAR op: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		op := s.redo;
		WHILE (op # NIL) & op.invisible DO op := op.next END;
		IF op # NIL THEN name := op.name$ ELSE name[0] := 0X END
	END GetRedoName;

	PROCEDURE (s: StdSequencer) Undo;
		VAR e: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		IF s.undo # NIL THEN
			REPEAT
				Pop(s.undo, e); Do(s, e.st, e.op); Push(s.redo, e)
			UNTIL ~e.invisible OR (s.undo = NIL);
			IF ~e.transparent THEN
				IF s.modLevel > 0 THEN DEC(s.modLevel) END
			END
		END
	END Undo;

	PROCEDURE (s: StdSequencer) Redo;
		VAR e: OpElem;
	BEGIN
		IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
		IF s.redo # NIL THEN
			Pop(s.redo, e); Do(s, e.st, e.op); Push(s.undo, e);
			WHILE (s.redo # NIL) & s.redo.invisible DO
				Pop(s.redo, e); Do(s, e.st, e.op); Push(s.undo, e)
			END;
			IF ~e.transparent THEN
				INC(s.modLevel)
			END
		END
	END Redo;


	(* Forwarder *)

	PROCEDURE (f: Forwarder) Forward (target: BOOLEAN; VAR msg: Controllers.Message);
		VAR w: Window;
	BEGIN
		w := stdDir.Focus(target); 
		IF w # NIL THEN ForwardCtrlMsg(w, msg) END
	END Forward;

	PROCEDURE (f: Forwarder) Transfer (VAR msg: Controllers.TransferMessage);
		VAR w: Windows.Window; h: Views.Frame; p: Ports.Port; sx, sy, tx, ty, pw, ph: INTEGER;
	BEGIN 
		h := msg.source; p := h.rider.Base();
		(* (msg.x, msg.y) is point in local coordinates of source frame *)
		sx := (msg.x + h.gx) DIV h.unit;
		sy := (msg.y + h.gy) DIV h.unit;
		(* (sx, sy) is point in global coordinates of source port *)
		stdDir.GetThisWindow(p, sx, sy, tx, ty, w);
		IF w # NIL THEN
			(* (tx, ty) is point in global coordinates of target port *)
			w.port.GetSize(pw, ph);	(* what's this for? *)
			msg.x := tx * w.port.unit;
			msg.y := ty * w.port.unit;
			(* (msg.x, msg.y) is point in coordinates of target window *)
			w.ForwardCtrlMsg(msg)
		END
	END Transfer;
	
	PROCEDURE GetByTitle* (IN title: Views.Title): Window;
		(** Get topmost window that has .title = title$ *)
		VAR w: Window;
	BEGIN
		w := zList;
		WHILE (w # NIL) & (w.title$ # title$) DO w := w.next END;
		RETURN w
	END GetByTitle;
	
	PROCEDURE MarkWindow*;
	(** Mark window at pointer: save it in mark. It can be used in subsequent commands. Post: IFF mouse pointer is over w: Window THEN mark = w ELSE mark = NIL. *)
		VAR w, u: Window; dummy: SET; found, dummy2: BOOLEAN;
			x, y, xRoot, yRoot: Pixels;
	BEGIN
		w := First(); 
		WHILE (w # NIL) & ~w.backend.GetOrigin(xRoot, yRoot) DO w := Next(w) END;
		IF w # NIL THEN w.port.NewRider().Input(x, y, dummy, dummy2); 
			INC(xRoot, x); INC(yRoot, y); found := FALSE;
			WHILE ~found & (w # NIL) DO 
				found := w.backend.IsInside(xRoot, yRoot, dummy2);
				IF ~found THEN w := Next(w) END
			END;
			u := mark; mark := w;
			IF w # NIL THEN MarkTitle(w) END; IF u # NIL THEN MarkTitle(u) END
		END
	END MarkWindow;

	PROCEDURE MarkGuard* (VAR par: Dialog.Par);	(** Menu guard for MarkWindow *)
	BEGIN
		par.disabled := mark = NIL
	END MarkGuard;

	(** miscellaneous **)

	PROCEDURE SetBackendDir* (d: BackendDirectory);
	BEGIN
		ASSERT(d # NIL, 20);
		IF stdBackendDir = NIL THEN
			stdBackendDir := d
		END;
		backendDir := d
	END SetBackendDir;

	PROCEDURE SetDumper* (d: PROCEDURE (p: ANYPTR));
	(** Sets the dumper procedure, cf. ForwardCtrlMsg *)
	BEGIN IF stdDump = NIL THEN stdDump := dump END; dump := d 
	END SetDumper;

	PROCEDURE GetBySpec* (loc: Files.Locator; name: Files.Name;
														conv: Converters.Converter; flags: SET): Window;
	(** Reuse of open documents; IF ~(allowDuplicates IN flags), THEN returns the topmost window that displays the permanent store, if any, identified by loc, name, internalized with conv, and has (isAux IN .flags) = (isAux IN flags) and (isTool IN .flags) = (isTool IN flags) ELSE returns NIL. *)
		VAR found, tool, aux: BOOLEAN; w: Window; 
	BEGIN
		IF ~(Windows.allowDuplicates IN flags) & (loc # NIL) & (loc.res # 77) & (name # "") THEN
			Files.dir.GetFileName(name, Files.docType, name); 	(* appends .odc if no extension *)
			IF conv = NIL THEN conv := Converters.list END;
			tool := Windows.isTool IN flags; aux := Windows.isAux IN flags;
			w := First();
			REPEAT 
				found := (w # NIL) & 
				~(Windows.allowDuplicates IN w.flags) & (w.loc # NIL) & (w.loc.res # 77) & (w.name # "")
				& Files.dir.SameFile(loc, name, w.loc, w.name) & (w.conv = conv)
				& (tool = (Windows.isTool IN w.flags)) & (aux = (Windows.isAux IN w.flags));
				IF ~found THEN w := Next(w) END
			UNTIL (w = NIL) OR found
		END;
		RETURN w
	END GetBySpec;

	PROCEDURE SelectBySpec* (loc: Files.Locator; name: Files.Name; conv: Converters.Converter; flags: SET; VAR done: BOOLEAN);
	(** A convenience procedure, selects the window returned by GetBySpec, if any. Post: done = TRUE => window was found and selected; done = FALSE => window was not found *)
		VAR w: Window;
	BEGIN
		w := GetBySpec(loc, name, conv, flags);
		IF w # NIL THEN Select(w, Windows.lazy); done := TRUE ELSE done := FALSE END
	END SelectBySpec;

	PROCEDURE SelectByTitle* (v: Views.View; flags: SET; title: Views.Title; VAR done: BOOLEAN);
	(** Select the window with title that displays a view of the same (dynamic) type as v (if specified) or v(Documents.Document).ThisView() and has (.flags / flags) * {isAux, isTool} = {} and ~(allowDuplicates in .flags). 
	Post: 
		done = TRUE => window was found; 
		done = FALSE window was not found. *)
		VAR w: Window; n1, n2: ARRAY 64 OF CHAR;
	BEGIN
		done := FALSE;
		IF v # NIL THEN
			IF v IS Documents.Document THEN v := v(Documents.Document).ThisView() END;
			Services.GetTypeName(v, n1)
		ELSE n1 := ""
		END;
		w := First();
		WHILE w # NIL DO
			IF ((w.flags / flags) * {Windows.isAux, Windows.isTool} = {})
				& ~(Windows.allowDuplicates IN w.flags) & (w.initialTitle$ = title$)
			THEN
				Services.GetTypeName(w.doc.ThisView(), n2);
				IF (n1 = "") OR (n1 = n2) THEN Select(w, Windows.lazy); done := TRUE; RETURN END
			END;
			w := Next(w)
		END
	END SelectByTitle;
	
	PROCEDURE (h: MsgHook) Omnicast (VAR msg: ANYREC);
		VAR w: Window;
	BEGIN
		w := First();
		WHILE w # NIL DO
			IF ~w.sub THEN w.seq.Handle(msg) END;
			w := Next(w)
		END
	END Omnicast;

	PROCEDURE (h: MsgHook) RestoreDomain (d: Stores.Domain);
		VAR seq: ANYPTR;
	BEGIN
		IF d = NIL THEN
			RestoreSequencer(NIL)
		ELSE
			seq := d.GetSequencer();
			IF seq # NIL THEN
				RestoreSequencer(seq(Sequencers.Sequencer))
			END
		END
	END RestoreDomain;


	(* SequencerDirectory *)
	
	PROCEDURE (d: SequencerDirectory) New (): Sequencers.Sequencer;
	BEGIN
		RETURN NewSequencer()
	END New;

	(** CheckAction **)
	
	PROCEDURE (a: CheckAction) Do;
		VAR w: Window; s: Sequencers.Sequencer;
	BEGIN
		Services.DoLater(a.wait, Services.resolution);
		w := First();
		WHILE w # NIL DO
			s := w.seq(*(StdSequencer)*);
			WITH s: StdSequencer DO
				IF s.trapEra # Kernel.trapCount THEN Reset(s) END;
				ASSERT(s.nestLevel = 0, 100);
				(* unbalanced calls of Views.BeginModification/EndModification or Views.BeginScript/EndScript *)
			ELSE
			END;
			w := Next(w)
		END
	END Do;
	
	PROCEDURE (a: WaitAction) Do;
	BEGIN
		Services.DoLater(a.check, Services.immediately)
	END Do;


	PROCEDURE SetAttachedCheckHook* (hook: AttachedCheckHook);
	BEGIN attachedCheckHook := hook
	END SetAttachedCheckHook;

	PROCEDURE (n: LangNotifier) Notify;
		VAR w: Window; 
	BEGIN
		w := First();
		WHILE w # NIL DO
			Update(w, Views.keepFrames);
			w.SetTitle(w.initialTitle);
			w := Next(w)
		END
	END Notify;
	
	PROCEDURE InitDefaults (p: ANYPTR);
	BEGIN
		WITH p: Param DO
			p.wheelScrollByPage := FALSE; (* monitored in StdWheel *)
			p.markTitles := FALSE;
			p.bgColor := Ports.white;
			p.frontStr := " ⌨";
			p.targetStr := " ⦿";
			p.frontTargetStr := " ⌨ ⦿";
			p.markStr := "⚹";
			p.InitDefaults := InitDefaults;
		END
	END InitDefaults;
	
	PROCEDURE Init*;
		VAR f: Forwarder; r: Reducer; sdir: SequencerDirectory;
			a: CheckAction; w: WaitAction; h: MsgHook;  ln: LangNotifier; stdExt: StdExt;
	BEGIN
		NEW(sdir); Sequencers.SetDir(sdir);
		NEW(h); Views.SetMsgHook(h);
		NEW(f); Controllers.Register(f);
		NEW(r); Kernel.InstallReducer(r);
		NEW(a); NEW(w); a.wait := w; w.check := a; Services.DoLater(a, Services.immediately);
		NEW(ln); Dialog.RegisterLangNotifier(ln);
		NEW(stdDir); dir := stdDir; border := -1;
		Windows.SetDir(stdDir);
		dir.l := none;
		NEW(param);
		InitDefaults(param);
		newNumber := 1;
		NEW(stdExt); ext := stdExt;
	END Init;

END StdWindows.



Closing windows 
