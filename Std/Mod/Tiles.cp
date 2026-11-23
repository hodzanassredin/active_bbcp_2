MODULE StdTiles;
(**
	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Meta, Models, 
		Controllers, Properties, Ports, Stores, Dialog, Views, Containers,
		Documents, Files, Converters, Fonts, Log, Sequencers, Windows, 
		StdCmds, StdWindows, StdGrids, StdDocuments, StdLog, StdDialog, StdMenus, 
		TextViews, Strings, TextControllers;

	CONST
		version = 1;
		badKind = 1;
		
		(* externalized proxy content kinds *)
		external = 1; log = 2; internal = 3; empty = 4;
		
		stopCursor = 22;	(* keep in sync with HostPorts.stopCursor *)
		
		dark* = 0; light* = 1;

	TYPE
		Param* = POINTER TO RECORD
			closeDialogLocation*,
			closeDialogDocument*: Files.Name;
			Init-, InitDefaults-: PROCEDURE (a: ANYPTR)
		END;
		
		Pixels = INTEGER;	(* for values in pixel coordinates *)
		UCs = INTEGER;	(* used for values in universal coordinates *)
		
		Backend = POINTER TO RECORD (StdWindows.WindowBackend)
			proxy: Proxy
		END;
		BackendDirectory = POINTER TO RECORD (StdWindows.BackendDirectory) 
			nextProxy: Proxy
		END;
		
		Proxy = POINTER TO RECORD (Views.View)
			wb: Backend; 	(* wb = NIL => window was closed; Proxy's host may not have removed Proxy *)
			base: Windows.Window;	(* the non-Tiler window in which .wb is opened *)
			filler: StdGrids.View;	(* param for Backend.Open *)
			delayed: RECORD	(* data for delayed opening of a window with .view *)
				is: BOOLEAN;	(* flags that proxy is delayed; for one of two reasons: after Internalize (=>.delayed.view # NIL) or after CopyFromSimpleView *)
				view: Views.View;
				title: Views.Title;
				flags: SET;
				loc: Files.Locator;
				fname: Files.Name;
				conv: Converters.Converter;
			END
		END;
		
		SeqNotifier = POINTER TO RECORD (Sequencers.Notifier)
			loc: Files.Locator;
			fname: Files.Name;
			view: Views.View;
			win: Windows.Window;
		END;
		
		BeforeCloseMsg = RECORD (Sequencers.Message) END;
		
		FindFrameMsg = RECORD (Views.Message)
			frame: Views.Frame
		END;
		
		ViewHook = POINTER TO RECORD (StdDialog.ViewHook)
			tiled: BOOLEAN;	(* whether tiling is on or off *)
		END;
		
		AttachedCheckHook = POINTER TO RECORD (StdWindows.AttachedCheckHook) END;

	VAR
		bckndDir-: BackendDirectory;
		param*: Param;
		lastWorkspaceName: Files.Name; lastWorkspaceLoc: Files.Locator;
		viewHook: ViewHook;
		attCheckHook: AttachedCheckHook;
		windowsRouter, backUp: PROCEDURE (IN title: ARRAY OF CHAR);
		targetTrack-: Views.Title;


	PROCEDURE SetWindowsRouter* (r: PROCEDURE (IN title: ARRAY OF CHAR));
	BEGIN
		windowsRouter := r
	END SetWindowsRouter;

	PROCEDURE SetTarget* (IN target: ARRAY OF CHAR);
	BEGIN
		targetTrack := target$
	END SetTarget;
	
	PROCEDURE GetInnermostFiller (VAR f: Views.Frame; OUT filler: StdGrids.View);
		VAR msg: StdGrids.TrackPropMsg; v: Views.View;
	BEGIN
		REPEAT
			f := Views.HostOf(f);
			IF f # NIL THEN
				msg.done := FALSE; msg.poll := TRUE;
				Views.HandlePropMsg(f.view, msg)
			END
		UNTIL (f = NIL) OR msg.done;
		IF msg.done THEN
			v := f.view;
			WITH v: StdGrids.View DO filler := v ELSE HALT(126) END
		END
	END GetInnermostFiller;
	
	PROCEDURE GetBase (f: Views.Frame; OUT base: Windows.Window);
	(* since Proxy.base is not eliminatable at this point, is GetBase eliminatable? *)
	(** Get outermost window base that f is installed under *)
	BEGIN
		IF f # NIL THEN f := Views.UltimateRootOf(f);
			base := Windows.dir.First(); 
			WHILE (base # NIL) & (base.frame # f) DO base := Windows.dir.Next(base) END
		END
	END GetBase;
	
	PROCEDURE GetThisFiller (IN name: Views.Title; OUT filler: StdGrids.View; OUT frame: Views.Frame);
		VAR msg: StdGrids.TrackMsg;
	BEGIN
		msg.name := name$; msg.cnt := 0; Views.Omnicast(msg);
		IF msg.cnt = 1 THEN filler := msg.track; frame := msg.frame 
		END
	END GetThisFiller;

	
	PROCEDURE DoOpen (v: Views.View; IN title: Views.Title; loc: Files.Locator;
					IN fname: Files.Name; conv: Converters.Converter; flags: SET; proxy: Proxy;
					forceMain: BOOLEAN; OUT win: StdWindows.Window);
	VAR
		opts, pvopts: SET;
		d: Documents.Document;
		c: Containers.Controller;
		s: ANYPTR;
		seq: Sequencers.Sequencer;
		bd: StdWindows.BackendDirectory;
		view: Views.View;
		ctx: Models.Context;
		gooddoc: BOOLEAN;
	BEGIN
		ASSERT(v # NIL, 20);
		win := NIL;
		IF conv = NIL THEN conv := Converters.list END;	(* use document converter *)
		(* IF proxy # NIL THEN EXCL(flags, Views.modal) END; *)
		WITH v: Documents.Document DO
			d := v; view := v.ThisView()
		ELSE
			view := v;
			ctx := v.context;
			IF ctx # NIL THEN WITH ctx: Documents.Context DO d := ctx.ThisDoc() END END
		END;
		IF (d # NIL) & (d.context # NIL) THEN
			d := Documents.DuplicateAs(d, StdDocuments.dir);
			view := d.ThisView()
		END;
		IF d = NIL THEN d := Documents.dir.New(view, Views.undefined, Views.undefined) END;
		ASSERT(d.Domain() = view.Domain(), 100); ASSERT(d.Domain() # NIL, 101);
		ASSERT(d.context = NIL, 102);
		s := d.Domain().GetSequencer();
		IF s # NIL THEN WITH s: Sequencers.Sequencer DO seq := s END END;
		c := d.ThisController();
		gooddoc := (c # NIL) & (c IS TextControllers.Controller);
		opts := c.opts;
		
		IF { Windows.isTool, Windows.isAux } * flags # {} THEN
			INCL(opts, Containers.noSelection)
		END;
		(* k8: in primary windows too *)
		pvopts := opts;
		IF (Windows.neverDirty IN flags)
			OR ((proxy # NIL) & ~(Windows.noResize IN flags)) THEN
			(* change "fit to page" to "fit to window" in secondary windows *)
			IF Documents.pageWidth IN opts THEN
				opts := opts - {Documents.pageWidth} + {Documents.winWidth};
			END;
			IF Documents.pageHeight IN opts THEN
				opts := opts - {Documents.pageHeight} + {Documents.winHeight};
			END;
			(* k8: fixed size windows should be changed too *)
			IF gooddoc & ~(Containers.noSelection IN opts) THEN
				IF opts * {Documents.pageWidth, Documents.winWidth} = {} THEN
					INCL(opts, Documents.winWidth);
				END;
				IF opts * {Documents.pageHeight, Documents.winHeight} = {} THEN
					INCL(opts, Documents.winHeight);
				END
			END
		END;
		IF opts # c.opts THEN
			c.SetOpts(opts);
			IF (opts # pvopts) & (seq # NIL) THEN seq.SetDirty(FALSE) END
		END;
		IF (Windows.neverDirty IN flags) & (seq # NIL) THEN seq.SetDirty(FALSE) END;
		bd := StdWindows.backendDir;
		IF proxy = NIL THEN
			StdWindows.SetBackendDir(StdWindows.stdBackendDir)
		ELSE
			StdWindows.SetBackendDir(bckndDir);
			bckndDir.nextProxy := proxy
		END;

		win := StdWindows.dir.New()(StdWindows.Window);
		
		StdWindows.SetBackendDir(bd);
		
		IF ~forceMain & (seq # NIL) THEN
			Windows.dir.OpenSubWindow(win, d, flags, title)
		ELSE
			Windows.dir.Open(win, d, flags, title, loc, fname, conv)
		END
	END DoOpen;
	
	
	PROCEDURE ProxyFor (IN title: Views.Title): Proxy;
			VAR filler: StdGrids.View; f: Views.Frame; proxy: Proxy;
	BEGIN
		proxy := NIL;
		WITH StdWindows.backendDir: BackendDirectory DO
			IF windowsRouter # NIL THEN
				windowsRouter(title)
			END;
			GetThisFiller(targetTrack, filler, f);
			IF (filler # NIL) & (f # NIL) THEN
				NEW(proxy);
				GetBase(f, proxy.base);
				proxy.filler := filler
			END	
		ELSE
		END;
		RETURN proxy
	END ProxyFor;
	

	
	(* ViewHook *)
	
	PROCEDURE (h: ViewHook) Open (v: Views.View; title: ARRAY OF CHAR;
								loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
								asTool, asAux, noResize, allowDuplicates, neverDirty: BOOLEAN);
			VAR t: Views.Title; flags: SET; proxy: Proxy; done: BOOLEAN; len: INTEGER;
				w_: StdWindows.Window;  (* changes from StdDialog.Open marked by blue *)
	BEGIN
		IF conv = NIL THEN conv := Converters.list END;	(* use document converter *)
		ASSERT(v # NIL, 20);
		flags := {}; done := FALSE;
		IF noResize THEN
			flags := flags + {Windows.noResize, Windows.noHScroll, Windows.noVScroll}
		END;
		IF asTool THEN INCL(flags, Windows.isTool) END;
		IF asAux THEN INCL(flags, Windows.isAux) END;
		IF neverDirty THEN INCL(flags, Windows.neverDirty) END;
		len := MIN(LEN(title$), LEN(Views.Title) - 1); 
		Strings.Extract(title, 0, len, t);
		IF len = LEN(Views.Title) - 1 THEN
			t[len - 1] := "."; t[len - 2] := "."; t[len - 3] := "."
		END;
		IF ~allowDuplicates THEN
			Windows.SelectBySpec(loc, name, conv, flags, done);
			IF ~done & (title # "") & (loc = NIL) & (name = "") THEN
				Windows.SelectByTitle(v, flags, t, done)
			END
		ELSE
			INCL(flags, Windows.allowDuplicates)
		END;
		IF ~done THEN
			IF asTool OR ~h.tiled THEN
				(* nothing *)
			ELSE
				proxy := ProxyFor(t)
			END;
			DoOpen(v, t, loc, name, conv, flags, proxy, FALSE, w_)
		END
	END Open;
	
	PROCEDURE DelayedOpen (p: Proxy; f: Views.Frame);
		VAR w_: StdWindows.Window; ffm: FindFrameMsg; v: Views.View;
	BEGIN
		ASSERT(p.delayed.is, 22); p.delayed.is := FALSE;
		IF f # NIL THEN ASSERT(f.view = p, 21) 
		ELSE Views.Broadcast(p, ffm); f := ffm.frame
		END;
		v := p.delayed.view; 
		IF v # NIL THEN p.delayed.view := NIL;  (* delayed after Internalize *)
			IF f # NIL THEN
				GetInnermostFiller(f, p.filler); ASSERT(p.filler # NIL, 126);
				GetBase(f, p.base);
				DoOpen(v, p.delayed.title, p.delayed.loc, p.delayed.fname, p.delayed.conv, p.delayed.flags, p, FALSE, w_);
				p.delayed.loc := NIL; p.delayed.conv := NIL
				
			END
		ELSE GetBase(f, p.base)	(* delayed after Proxy.CopyFromSimpleView *)
		END
	END DelayedOpen;

	PROCEDURE (p: Proxy) HandleViewMsg (f: Views.Frame; VAR msg: Views.Message);
	BEGIN
		IF p.delayed.is THEN DelayedOpen(p, f) END;
		WITH msg: FindFrameMsg DO msg.frame := f ELSE END
	END HandleViewMsg;
	
	PROCEDURE DirtyHook* (w: Windows.Window; OUT action: INTEGER);
		VAR loc: Files.Locator; dlg: Views.View; doc: Documents.Document; c: Containers.Controller;
			wid, hei: INTEGER; name: Files.Name;
	BEGIN ASSERT(w # NIL, 20); ASSERT(w.doc # NIL, 21);
		action := StdCmds.default; doc := w.doc;
		WITH doc: StdDocuments.Document DO
			loc := Files.dir.This(param.closeDialogLocation); 
			IF loc # NIL THEN
				name := param.closeDialogDocument;
				dlg := Views.OldView(loc, name);
				IF dlg # NIL THEN action := StdCmds.defer;
					WITH dlg: Containers.View DO 
						c := dlg.ThisController(); IF c # NIL THEN c.SetOpts(c.opts + Containers.mask) END
					ELSE
					END;
					dlg.context.GetSize(wid, hei); 
					StdDocuments.InsertOverlaid(StdDocuments.AddBackground(Views.CopyOf(dlg, Views.deep)), NIL, doc, StdDocuments.center, StdDocuments.center, wid, hei, w)
				END
			END
		ELSE
		END
	END DirtyHook;
	
	PROCEDURE CloseNestedWindows (w: Windows.Window; OUT cancelled, deferred: BOOLEAN);
		CONST maxDeff = 50;
		VAR
			wnd, next: Windows.Window; wb: StdWindows.WindowBackend; defThis: BOOLEAN;
			deff, copy: POINTER TO ARRAY OF StdWindows.Window; i, nDef, len, j: INTEGER;
	BEGIN
		cancelled := FALSE; deferred := FALSE; wnd := Windows.dir.First(); nDef := 0;
		WHILE (wnd # NIL) & ~cancelled DO
			WITH wnd: StdWindows.Window DO
				wb := wnd.backend; 
				IF wnd # w THEN
					WITH wb: Backend DO 
						IF wb.proxy.base = w THEN 
							i := 0; WHILE (i < nDef) & (deff[i] # wnd) DO INC(i) END;
							IF i = nDef THEN
								StdCmds.GuardedClose(wnd, cancelled, defThis); 
								deferred := deferred OR defThis;
								IF defThis THEN 
									IF deff = NIL THEN
										NEW(deff, maxDeff) 
									ELSIF nDef = LEN(deff) THEN
										len := LEN(deff); NEW(copy, len + maxDeff);
										j := 0; WHILE j < len DO copy[j] := deff[j]; INC(j) END; deff := copy
									END;
									deff[nDef] := wnd; INC(nDef) 
								END;
								IF ~cancelled & ~defThis THEN next := Windows.dir.First() 
								ELSIF ~cancelled & defThis THEN next := Windows.dir.Next(wnd)
								ELSE next := NIL 
								END
							END
						END
					ELSE 
					END
				END;
			ELSE
			END;
			IF next # NIL THEN
				wnd := next
			ELSIF ~cancelled THEN
				wnd := Windows.dir.Next(wnd)
			END;
			next := NIL
		END
	END CloseNestedWindows;
	
	
	
	PROCEDURE CloseNow*;
	(** Command for use in a File close dialog. Causes immediate closing of the document losing all modifications. Only works when TilerWindows.par is initialized appropriately - that is, this is actually a private procedure. *)
	BEGIN  
		IF StdDocuments.par # NIL THEN 
			WITH StdDocuments.par: StdWindows.Window DO 
				Windows.dir.Close(StdDocuments.par); Kernel.Cleanup
			ELSE
				Log.String(
					"TW.CloseNow: ~(TilerWindows.par IS StdWindows.Window); cannot Close")
			END
		END
	END CloseNow;
	
	PROCEDURE SaveNClose*; 
	(** Command for use in a File close dialog. Causes the document to be saved and then closed. Only works when TilerWindows.par is initialized appropriately - that is, this is actually a private procedure. *)
	BEGIN 
		IF StdDocuments.par # NIL THEN 
			WITH StdDocuments.par: StdWindows.Window DO 
				StdCmds.SaveDialog;
				Windows.dir.Close(StdDocuments.par);
				Kernel.Cleanup
			ELSE 
				Log.String("StdTiles.SaveNClose: ~(StdDocuments.par IS StdWindows.Window); cannot SaveNClose")
			END
		END
	END SaveNClose;
	
	PROCEDURE ThisFiller* (w: StdWindows.Window): StdGrids.View;
	(** Returns the filler in which w is opened. *)
		VAR res: StdGrids.View;
	BEGIN  ASSERT(w # NIL, 20);
		IF (w.backend # NIL) & (w.backend IS Backend) THEN 
			res := StdGrids.ThisFiller(w.backend(Backend).proxy)
		END;
		RETURN res
	END ThisFiller;
	
	PROCEDURE (wb: Backend) Close;
		VAR prop: StdGrids.Proposal; 
	BEGIN 
		IF wb.proxy # NIL THEN 
			prop.op := StdGrids.remove;
			wb.proxy.context.Consider(prop)	(* this removes the proxy from the filler *)
		END;
		wb.proxy := NIL
	END Close;
	
	(* Proxy view *)
	
	PROCEDURE (dst: Proxy) CopyFromSimpleView (src: Views.View);
	(* Proxies are tricky: they display windows. But a window can only be displayed in one, well, window, one place. This is why, when a copy is made of a proxy, the original is stripped of it's window. I am not sure how well this will work with the rest of the framework, because the framework feels free to make copies of views for various reasons, even for the sake of externalizing a view. Like this: a copy of a view is made, this copy is exteralized, and then the copy is disposed, and what the user sees on the screen is actually the original. But, with a proxy, the original proxy would be stripped of it's window and would display nothing. So, I'm not sure how well this will work. At the time, this does not happen because when a filler is saved, it does not make a copy of it's content. The only time (now) that a copy of a proxy is made is explicitly when the proxy is dragged into another filler *)
	BEGIN 
		WITH src: Proxy DO
			dst.wb := src.wb; src.wb := NIL; dst.wb.proxy := dst; 
			dst.wb.proxy.base := NIL; dst.delayed.is := TRUE
		END
	END CopyFromSimpleView;
	
	PROCEDURE Transfer* (w: StdWindows.Window; to: StdGrids.View);
	(** Transfer tiled window w into another filler to. *)
		VAR prop: StdGrids.Proposal;
	BEGIN ASSERT(w # NIL, 20); ASSERT(to # NIL, 20);
		IF (w.backend # NIL) & (w.backend IS Backend) THEN 
			StdGrids.Transfer(w.backend(Backend).proxy, to, StdGrids.append);
			prop.op := StdGrids.activate; w.backend(Backend).proxy.context.Consider(prop);
		END
	END Transfer;
	
	PROCEDURE TransferTo* (w: StdWindows.Window; IN track: Views.Title);
	(** Transfer tiled window w into track track *)
		VAR prop: StdGrids.Proposal; to: StdGrids.View; _: Views.Frame;
	BEGIN ASSERT(w # NIL, 20); 
		IF (w.backend # NIL) & (w.backend IS Backend) THEN 
			GetThisFiller(track, to, _); ASSERT(to # NIL, 20);
			StdGrids.Transfer(w.backend(Backend).proxy, to, StdGrids.append);
			prop.op := StdGrids.activate; w.backend(Backend).proxy.context.Consider(prop);
		END
	END TransferTo;


	PROCEDURE (proxy: Proxy) HandleCtrlMsg  (f: Views.Frame; VAR msg: Views.CtrlMessage; VAR focus: Views.View );
		VAR w: StdWindows.Window;
	BEGIN
		IF proxy.delayed.is THEN DelayedOpen(proxy, f) END;
		WITH msg: Controllers.PollFocusMsg DO msg.focus := f
		ELSE 
			IF proxy.wb # NIL THEN w := proxy.wb.win;
				IF w # NIL THEN
					WITH msg: Controllers.MarkMsg DO 
						IF msg.show & (w # StdWindows.front) THEN
							Windows.dir.Select(w, Windows.lazy)
						END
					| msg: StdGrids.RemoveMsg DO
						msg.processed := TRUE;
						StdWindows.GuardedClose(w) 
					| msg: StdWindows.PrefocusMsg DO
						msg.win := w
					ELSE
						StdWindows.ForwardCtrlMsg(w, msg)
					END
				END
			ELSE WITH msg: Controllers.PollCursorMsg DO msg.cursor := stopCursor ELSE END
			END
		END
	END HandleCtrlMsg;
	
	PROCEDURE (p: Proxy) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		IF p.delayed.is THEN DelayedOpen(p, NIL) END;
		WITH msg: Properties.FocusPref DO msg.setFocus := p.wb # NIL 
		| msg: StdGrids.WindowPref DO
			IF p.wb # NIL THEN msg.win := p.wb.win END
		ELSE 
		END
	END HandlePropMsg;
	
	PROCEDURE (p: Proxy) GetBackground (VAR col: Ports.Color);
	BEGIN
		IF (p.wb # NIL) & (p.wb.win.doc # NIL) THEN p.wb.win.doc.GetBackground(col) END
	END GetBackground;
	
	PROCEDURE (proxy: Proxy) Restore (f: Views.Frame; l_, t_, r_, b_: UCs);
		VAR wid, hei, asc, dsc, u, dummy: UCs; w: StdWindows.Window; title: Views.Title; fnt: Fonts.Font;
	BEGIN 
		IF proxy.delayed.is THEN DelayedOpen(proxy, f) END;
		proxy.context.GetSize(wid, hei);
		IF proxy.wb # NIL THEN 
			w := proxy.wb.win; 
			IF (w # NIL) & (w.frame # NIL) & (w.backend IS Backend) THEN u := f.unit;
				w.SetSize((f.gx + wid) DIV u - f.gx DIV u, (f.gy + hei) DIV u - f.gy DIV u);
				StdWindows.InstallRoot(w, f, 0, 0, 0, StdWindows.front = w)
			END
		ELSE
			(*
			f.DrawRect(0, 0, wid, hei, Ports.fill, celadon);
			*)
			fnt := Fonts.dir.Default(); fnt := Fonts.dir.This(fnt.typeface, fnt.size, fnt.style, Fonts.bold);
			fnt.GetBounds(asc, dsc, dummy);
			IF w = NIL THEN title := "NIL" ELSE title := w.title END;
			f.DrawString((wid - u) DIV 2, (hei - asc - dsc) DIV 2, Ports.red, title, fnt)
		END
	END Restore;
	
	PROCEDURE Export (f: Files.File; v: Views.View; OUT ok: BOOLEAN);
		VAR val: RECORD (Meta.Value) p: Converters.Exporter END; it: Meta.Item;
	BEGIN
		Meta.LookupPath(Converters.list.exp, it);
		IF (it.obj = Meta.procObj) OR (it.obj = Meta.varObj) & (it.typ = Meta.procTyp) THEN
			it.GetVal(val, ok)
		ELSE ok := FALSE
		END;
		IF ok THEN val.p(v, f) END
	END Export;
	
	PROCEDURE Import (f: Files.File; OUT v: Views.View);
		VAR val: RECORD (Meta.Value) p: Converters.Importer END; it: Meta.Item; ok: BOOLEAN;
			s: Stores.Store;
	BEGIN
		Meta.LookupPath(Converters.list.imp, it);
		IF (it.obj = Meta.procObj) OR (it.obj = Meta.varObj) & (it.typ = Meta.procTyp) THEN
			it.GetVal(val, ok)
		ELSE ok := FALSE
		END;
		IF ok THEN
			val.p(f, s); IF s # NIL THEN WITH s: Views.View DO v := s ELSE END END
		END
	END Import;
	
	PROCEDURE GetRelativeLocation (IN wrt: Files.Name; VAR loc: Files.Name);
	(** wrt: /home/anto/bb; loc: /home/anto/bb/Box	=> Box*)
		VAR j, i: INTEGER; a, b: CHAR;
	BEGIN
		i := -1; REPEAT INC(i); a := wrt[i]; b := loc[i] UNTIL (a = 0X) OR (b = 0X) OR (a # b);
		IF (b = '/') OR (b = '\') THEN INC(i) END;
		j := 0; IF j < i THEN REPEAT b := loc[i]; loc[j] := b; INC(i); INC(j) UNTIL b = 0X END
	END GetRelativeLocation;
	
	PROCEDURE (proxy: Proxy) Externalize (VAR wr: Stores.Writer);
		VAR w: StdWindows.Window; root, location: Files.Name; kind: INTEGER; rd: Files.Reader; b: BYTE;
			tmp: Files.File; ok: BOOLEAN;
	BEGIN
		wr.WriteVersion(version);
		IF proxy.wb # NIL THEN w := proxy.wb.win END;
		IF w # NIL THEN
			IF (w.name # "") & (w.loc # NIL) THEN kind := external
			ELSIF w.doc.ThisView().ThisModel() = StdLog.text THEN kind := log
			ELSIF w.doc.ThisView() IS TextViews.View THEN kind := internal
			ELSE kind := empty
			END;
			wr.WriteByte(SHORT(SHORT(kind)));
			IF kind = external THEN
				wr.WriteString(w.name);
				
				Dialog.GetLocPath(w.loc, location); Dialog.GetLocPath(Files.dir.This(''), root); 
				GetRelativeLocation(root, location);
				wr.WriteString(location); 
				wr.WriteString(w.conv.imp); wr.WriteString(w.title); wr.WriteSet(w.flags)
			ELSIF kind = internal THEN 
				tmp := Files.dir.Temp(); Export(tmp, w.doc.ThisView(), ok);
				IF ~ok THEN wr.WriteInt(0)
				ELSE wr.WriteInt(tmp.Length()); rd := tmp.NewReader(NIL); rd.SetPos(0);
					REPEAT rd.ReadByte(b); IF ~rd.eof THEN wr.WriteByte(b) END UNTIL rd.eof;
					wr.WriteString(w.title); wr.WriteSet(w.flags)
				END
			END
		ELSE
		END
	END Externalize;
	
	PROCEDURE (p: Proxy) Internalize (VAR rd: Stores.Reader);
		VAR location: Files.Name; len, ver: INTEGER; loc: Files.Locator; old: StdWindows.Window;
			conv: Converters.Converter; imp: Dialog.String; s: Stores.Store; b, kind: BYTE;
			tmp: Files.File; wr: Files.Writer; path: Files.Name;
	BEGIN
		rd.ReadVersion(version, version, ver);
		IF ~rd.cancelled THEN
			rd.ReadByte(kind);
			CASE kind OF
			| empty:
			| external:
				rd.ReadString(p.delayed.fname);
				rd.ReadString(location); loc := Files.dir.This(location); 
				rd.ReadString(imp); rd.ReadString(p.delayed.title); rd.ReadSet(p.delayed.flags);
				conv := Converters.list; WHILE (conv # NIL) & (conv.imp # imp) DO conv := conv.next END;
				IF conv # NIL THEN
					old := StdWindows.GetBySpec(loc, p.delayed.fname, conv, p.delayed.flags);
					IF old # NIL THEN s := old.doc.ThisView()
					ELSE Converters.Import(loc, p.delayed.fname, conv, s)
					END;
					IF s # NIL THEN 
						WITH s: Views.View DO
							Dialog.GetLocPath(loc, path);
							p.delayed.view := s; p.delayed.conv := conv; p.delayed.loc := loc;
						ELSE 
						END
					END
				END
			| internal:
				rd.ReadInt(len); 
				IF len > 0 THEN tmp := Files.dir.Temp(); wr := tmp.NewWriter(NIL); wr.SetPos(0);
					REPEAT rd.ReadByte(b); wr.WriteByte(b); DEC(len) UNTIL len = 0;
					Import(tmp, p.delayed.view);
					rd.ReadString(p.delayed.title); rd.ReadSet(p.delayed.flags); p.delayed.fname := ''
				END
			| log:
				p.delayed.view := TextViews.dir.New(StdLog.text); p.delayed.fname := '';
				Dialog.MapString("#Dev:Log", p.delayed.title);
				p.delayed.flags := { Windows.neverDirty, Windows.isAux, Documents.winWidth }
			ELSE rd.TurnIntoAlien(badKind)
			END;
			p.delayed.is := p.delayed.view # NIL;
		END
	END Internalize;
	
	
	
	(* Backend *)
		
	PROCEDURE (wb: Backend) SetSize (w, h: Pixels), EMPTY;
	
	PROCEDURE (wb: Backend) SetTitle (IN title: Views.Title);
		VAR prop: StdGrids.Proposal;
	BEGIN
		IF (wb.proxy # NIL) & (wb.proxy.context # NIL) THEN 
			prop.title := title$; prop.op := StdGrids.setTitle;
			wb.proxy.context.Consider(prop)
		END
	END SetTitle;
	
	PROCEDURE (wb: Backend) Select;
		VAR prop: StdGrids.Proposal;
	BEGIN 
		IF (wb.proxy # NIL) & (wb.proxy.context # NIL) THEN 
			prop.op := StdGrids.activate; wb.proxy.context.Consider(prop);
			
			IF wb.proxy.base IS StdWindows.Window THEN
				wb.proxy.base(StdWindows.Window).backend.Select;
			END;
			
			(* This is the only place where proxy.base is needed. If proxy were guaranteed to have a frame, then this frame could be obtained thru FindFrameMessage, then UltimateRootOf this frame would be the base window's root frame. But the proxy is NOT guaranteed to have a frame at this point: the context may not have granted the activation request, for example; or if it did, the update may have been requested but not validated, so the proxy may not have a frame at this point. *)
			(* this ↓ is done in StdWindows.Open, actually
			StdWindows.Validate(wb.base)
			(* during window initialization (StdWindows.Open), this forces Restore'ation and building of the frame tree -- ensures that proxy gets a frame and wb.win.rootFrame gets properly positioned thru Proxy.Restore->StdWindows.InstallRoot. This way, the same command that opens a window can also operate on it's content (frame tree). *)
			*)
			(* k8: we need proper frame structure to make things like TextCmds.FindFirst work. so let's built it!
				note that only newly created windows has no root host. *)
			IF (wb.win # NIL) & (wb.win.frame # NIL) & (Views.HostOfRoot(wb.win.frame) = NIL) THEN
				Views.BuildFrameTree(wb.win.frame, TRUE)
			END
		END
	END Select;
	
	PROCEDURE (wb: Backend) Open (isDialog: BOOLEAN);
	(* 	VAR w: StdWindows.Window; loc: Files.Locator; fname: Files.Name;
			conv: Converters.Converter; flags: SET; title: Views.Title;
			doc: Documents.Document; dirty: BOOLEAN; proxy: Proxy;
			wasSub: BOOLEAN; *)
		VAR f: StdGrids.View;
	BEGIN
		(* w := wb.win;
		IF isDialog & w.sub THEN
			IF w # NIL THEN
				doc := w.doc; title := w.title; loc := w.loc; fname := w.name$;
				conv := w.conv; flags := w.flags;
				dirty := w.seq.Dirty();
				proxy := NIL;
				(*
				IF ~(w.backend IS Backend) THEN proxy := ProxyFor(title) END;
				*)
				(* Windows.dir.Close(w); *)
				DoOpen(doc, title, loc, fname, conv, flags, proxy, TRUE, w);
				IF ~dirty THEN w.seq.SetDirty(FALSE) END
			END
		ELSE
		*)
			(* at this point, wb.win.port # NIL, but wb.proxy may have no frame *)
			ASSERT(wb.proxy # NIL, 20); ASSERT(wb.win # NIL, 21); ASSERT(wb.proxy.wb = wb, 22);
			ASSERT(wb.win.doc # NIL, 23); ASSERT(wb.win.doc.context # NIL, 24);
			IF wb.proxy.context = NIL THEN 
				f := wb.proxy.filler; wb.proxy.filler := NIL;
				IF f.kind = StdGrids.divider THEN
					StdGrids.AppendDivTile(f, wb.proxy, 0, 0)
				ELSIF f.kind = StdGrids.stack THEN
					StdGrids.AppendStackTab(f, wb.proxy, wb.win.title)
				ELSE HALT(27)
				END
			END;
			Views.Obscure(wb.win.frame);
			(* obscure the root frame until it has a position. It will be unobscured with InstallRoot from Proxy.Restore when wb.proxy is grated a frame (which implies a position for the root frame) *)
		(* END *) 
	END Open;
	
	
	PROCEDURE (d: BackendDirectory) NewBackend (): StdWindows.WindowBackend;
		VAR wb: Backend; res: StdWindows.WindowBackend; p: Proxy; 
	BEGIN
		p := d.nextProxy;
		IF (p = NIL) & (targetTrack # "") THEN 
			p := ProxyFor(targetTrack) 
		END;
		IF p # NIL THEN 
			d.nextProxy := NIL; ASSERT(p.base # NIL, 25); ASSERT(p.filler # NIL, 26);
			NEW(wb); res := wb; wb.proxy := p; p.wb := wb
		ELSE
			res := StdWindows.stdBackendDir.NewBackend()
		END;
		RETURN res
	END NewBackend;

	PROCEDURE (wb: BackendDirectory) GetBounds (OUT w, h: Pixels);
	BEGIN
		StdWindows.stdBackendDir.GetBounds(w, h)
	END GetBounds;

	PROCEDURE (wb: Backend) Port (): Ports.Port;
	BEGIN RETURN wb(Backend).proxy.base.port
	END Port;



	(* Commands *)

	PROCEDURE InstallViewHook*;
	BEGIN
		StdDialog.SetViewHook(viewHook);
	END InstallViewHook;

	PROCEDURE UninstallViewHook*;
	BEGIN
		StdDialog.SetViewHook(NIL);
	END UninstallViewHook;

	PROCEDURE BindCloseNotifier* (w: Windows.Window);
	VAR notifier: SeqNotifier;
	BEGIN
		NEW(notifier);
		notifier.win := w; w.seq.InstallNotifier(notifier);
	END BindCloseNotifier;

	PROCEDURE On*;
	BEGIN
		StdWindows.SetBackendDir(bckndDir); viewHook.tiled := TRUE;
		InstallViewHook
	END On;

	PROCEDURE Off*;
	BEGIN
		StdWindows.SetBackendDir(StdWindows.stdBackendDir); viewHook.tiled := FALSE;
		UninstallViewHook
	END Off;

	PROCEDURE OnOff*;
	(** Toggle Tiler on and off; for use in menus *)
	BEGIN
		IF StdWindows.backendDir = bckndDir THEN Off ELSE On END
	END OnOff;

	PROCEDURE OnOffGuard* (VAR par: Dialog.Par);
	BEGIN
		par.checked := StdWindows.backendDir # bckndDir
	END OnOffGuard;

	PROCEDURE GetThisTrack* (w: StdWindows.Window; OUT track: Views.Title);
		VAR msg: StdGrids.TrackPropMsg; f: Views.Frame;
	BEGIN
		f := Views.HostOfRoot(w.frame); msg.done := FALSE; 
		WHILE (f # NIL) & ~msg.done DO
			msg.poll := TRUE; Views.HandlePropMsg(f.view, msg);
			f := Views.HostOf(f)
		END;
		IF msg.done THEN track := msg.name ELSE track := "" END
	END GetThisTrack;
	
	PROCEDURE (hook: AttachedCheckHook) IsAttached- (w: StdWindows.Window): BOOLEAN;
	BEGIN RETURN (w # NIL) & (w.backend IS Backend)
	END IsAttached;

	(* menu commands *)

	PROCEDURE ToggleAttached*;
	(** Attach (tile)/Detach (untile) target window *)
		VAR w: StdWindows.Window; loc: Files.Locator; fname: Files.Name;
			conv: Converters.Converter; flags: SET; title: Views.Title;
			doc: Documents.Document; dirty: BOOLEAN; proxy: Proxy;
			wasSub: BOOLEAN;
	BEGIN
		w := StdWindows.target;
		IF w # NIL THEN
			doc := w.doc; title := w.title; loc := w.loc; fname := w.name$;
			conv := w.conv; flags := w.flags;
			dirty := w.seq.Dirty();
			wasSub := w.sub;
			IF ~(w.backend IS Backend) THEN proxy := ProxyFor(title) END;
			Windows.dir.Close(w);
			DoOpen(doc, title, loc, fname, conv, flags, proxy, ~wasSub, w);
			IF ~dirty THEN w.seq.SetDirty(FALSE) END
		END
	END ToggleAttached;

	PROCEDURE AttachedGuard* (VAR par: Dialog.Par);
	BEGIN 
		par.disabled := StdWindows.target = NIL;
		par.checked := ~par.disabled & (StdWindows.target.backend IS Backend)
	END AttachedGuard;



	PROCEDURE ToggleSub*;
		VAR w: StdWindows.Window;
	BEGIN
		w := StdWindows.dir.Focus(Controllers.frontPath);
		IF w.link = w THEN
			GetThisTrack(w, targetTrack);
			(* ask router where to shift duplication window *)
			windowsRouter(">>>");
			(* halt windows router during new window open *)
			backUp := windowsRouter;
			windowsRouter := NIL;
			StdCmds.NewWindow;
			windowsRouter := backUp;
		ELSE
			Windows.dir.Select(w.link, Windows.eager)
		END
	END ToggleSub;



	(* experimental workspace section *)

	PROCEDURE SaveAs* (loc: Files.Locator; name: ARRAY OF CHAR);
		VAR f: Views.Frame; w: Windows.Window; file: Files.File; _res: INTEGER;
	BEGIN
		IF StdWindows.target # NIL THEN
			f := Views.UltimateRootOf(StdWindows.target.frame)
		END;
		IF f # NIL THEN 
			IF f = StdWindows.target.frame THEN
			ELSE
				w := Windows.dir.First();
				WHILE w.frame # f DO w := Windows.dir.Next(w) END
			END;
			file := Files.dir.New(loc, Files.dontAsk);
			Documents.ExportDocument(w.doc.OriginalView(), file);
			file.Register(name$, 'odc', ~Files.ask, _res);
		END
	END SaveAs;

	PROCEDURE SaveWorkspace*;
	BEGIN
		SaveAs(lastWorkspaceLoc, lastWorkspaceName);
	END SaveWorkspace;

	PROCEDURE OpenAs* (loc: Files.Locator; name: ARRAY OF CHAR);
	VAR v: Views.View; w: Windows.Window; quit: BOOLEAN;
	BEGIN
		lastWorkspaceLoc := loc;
		lastWorkspaceName := name$;
		v := Views.OldView(lastWorkspaceLoc, lastWorkspaceName);
		IF v # NIL THEN
			quit := TRUE;
			w := Windows.dir.First();
			WHILE (w # NIL) & quit DO
				StdWindows.GuardedClose(w);
				w := Windows.dir.First();
			END;
			IF quit THEN
				Off;
				StdWindows.border := 0;
				Views.OpenAux(v, Dialog.appName$);
				BindCloseNotifier(Windows.dir.First());
				On;
			END;
		END
	END OpenAs;

	PROCEDURE OpenWorkspace*;
	BEGIN
		OpenAs(lastWorkspaceLoc, lastWorkspaceName);
	END OpenWorkspace;

	PROCEDURE SaveGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := lastWorkspaceLoc = NIL
	END SaveGuard;

	PROCEDURE (n: SeqNotifier) Notify (VAR msg: Sequencers.Message);
		VAR file: Files.File; _: INTEGER; cancelled, deferred: BOOLEAN;
	BEGIN
		WITH msg: Sequencers.CloseMsg DO
			IF n.loc # NIL THEN
				file := Files.dir.New(n.loc, Files.dontAsk);
				Documents.ExportDocument(n.view, file);
				file.Register(n.fname, 'odc', ~Files.ask, _)
			END;
			CloseNestedWindows(n.win, cancelled, deferred);
			msg.sticky := cancelled OR deferred
		ELSE
		END
	END Notify;

	(*
	PROCEDURE Autosave* (v: Views.View; IN location, fname: Files.Name);
		VAR s: ANYPTR; n: SeqNotifier;
	BEGIN
		s := v.Domain().GetSequencer();
		WITH s: Sequencers.Sequencer DO 
			NEW(n); n.loc := Files.dir.This(location); n.fname := fname$; n.view := v;
			s.InstallNotifier(n)
		ELSE HALT(20)
		END
	END Autosave;
	
	example:
		StdCmds.OpenDoc('Workspace.odc');
		w := StdWindows.GetBySpec(Files.dir.This(''), 'Workspace.odc', Converters.list, {});
		IF w # NIL THEN  StdTiles.Autosave(w.doc.ThisView(), '', 'Workspace') END;
		StdTiles.On;
	
	*)
	

	(* color themes *)

	PROCEDURE UpdateUIColors*;
	BEGIN
		IF Dialog.colorTheme = dark THEN
			
			IF StdMenus.param.InitDefaults # NIL THEN
				StdMenus.param.InitDefaults(StdMenus.param)
			END;
			IF StdGrids.param.InitDefaults # NIL THEN
				StdGrids.param.InitDefaults(StdGrids.param)
			END;
			
		ELSIF Dialog.colorTheme = light THEN
			
			StdMenus.param.bgColorLine := Ports.RGBColor(212, 208, 200);
			StdMenus.param.statusBg := Ports.RGBColor(212, 208, 200);
			StdMenus.param.bgColor := Ports.RGBColor(212, 208, 200);
			StdMenus.param.color := Ports.black;
			StdMenus.param.colorLine := Ports.black;
			StdMenus.param.focusBg := Ports.RGBColor(10, 36, 106);
			StdMenus.param.focusColor := Ports.white;
			StdMenus.param.focusColorLine := Ports.black;
			StdMenus.param.focusBgLine := Ports.RGBColor(180, 180, 180);
			StdMenus.param.statusColor := Ports.black;
			
			IF StdMenus.param.Init # NIL THEN
				StdMenus.param.Init(StdMenus.param)
			END;
			
			StdGrids.param.bgColor := Ports.RGBColor(128, 128, 128);
			StdGrids.param.gutterColor := Ports.RGBColor(140, 140, 140);
			IF StdGrids.param.Init # NIL THEN
				StdGrids.param.Init(StdGrids.param)
			END;
		
		END;
		IF (StdDocuments.param # NIL) & (StdDocuments.param.Init # NIL) THEN
			StdDocuments.param.Init(StdDocuments.param)
		END;
	END UpdateUIColors;

	PROCEDURE InitDefaults (p: ANYPTR); 
	BEGIN
		WITH p: Param DO
			p.closeDialogLocation := "Std/Rsrc";
			p.closeDialogDocument := "ConfirmCloseOverlaid.odc";
			p.InitDefaults := InitDefaults;
		END
	END InitDefaults;

	PROCEDURE Init;
	BEGIN
		NEW(attCheckHook);
		StdWindows.SetAttachedCheckHook(attCheckHook);
		targetTrack := "";
		NEW(viewHook); viewHook.tiled := TRUE;
		NEW(bckndDir);
		NEW(param);
		InitDefaults(param);
		
		lastWorkspaceLoc := Files.dir.This("");
		lastWorkspaceName := "workspace.odc";
		
		UpdateUIColors;
	END Init;

BEGIN
	Init
CLOSE
	UninstallViewHook
END StdTiles.