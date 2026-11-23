MODULE WinClipboard;
	(* Win32 clipboard, using the active i386 formats without COM data objects. *)

	IMPORT S := SYSTEM, Services, Stores, Views, Models, Properties, Ports, Files, Meta,
		Dialog, Controllers, StdCmds, WinPorts, WinGui, A := WinApi, O := WinOle;

	CONST obfTag = 6F4F4443H;

	TYPE
		ClipboardHook = POINTER TO RECORD (StdCmds.ClipboardHook) END;
		Importer = PROCEDURE (VAR med: O.STGMEDIUM; OUT v: Views.View; OUT w, h: INTEGER; OUT single: BOOLEAN);
		Exporter = PROCEDURE (v: Views.View; w, h, x, y: INTEGER; single: BOOLEAN; VAR med: O.STGMEDIUM);
		ImpVal = RECORD (Meta.Value) p: Importer END;
		ExpVal = RECORD (Meta.Value) p: Exporter END;
		InfoDesc = RECORD [untagged] type: Stores.TypeName; w, h, rx, ry: INTEGER; single: BOOLEAN END;
		InfoPtr = POINTER TO InfoDesc;
		PaintContext = POINTER TO RECORD (Models.Context) w, h: INTEGER END;
		PaintView = POINTER TO RECORD (Views.View) view: Views.View END;

	VAR cloneAttributes*, isText*: BOOLEAN;
		lastError*: INTEGER;
		content: Views.View; width, height: INTEGER; singleton, registering: BOOLEAN;
		owner: A.HWND; sequence, native, info, rtf: INTEGER;

	PROCEDURE Command (IN name: ARRAY OF CHAR; VAR val: Meta.Value; OUT ok: BOOLEAN);
		VAR item: Meta.Item;
	BEGIN
		Meta.LookupPath(name, item); ok := FALSE;
		IF item.obj = Meta.procObj THEN item.GetVal(val, ok) END
	END Command;

	PROCEDURE Own (): BOOLEAN;
	BEGIN
		RETURN (content # NIL) & (owner # 0) & (A.GetClipboardOwner() = owner)
			& (A.GetClipboardSequenceNumber() = sequence)
	END Own;

	(* Locally allocated media; incoming COM media use WinOle.ReleaseStgMedium. *)
	PROCEDURE ReleaseMedium* (VAR med: O.STGMEDIUM);
		VAR p: A.PtrMETAFILEPICT; ignored: INTEGER; handle: S.ADDRESS;
	BEGIN
		IF med.data # 0 THEN
			IF med.tymed = O.TYMED_MFPICT THEN
				p := S.VAL(A.PtrMETAFILEPICT, A.GlobalLock(med.data));
				IF p # NIL THEN
					ignored := A.DeleteMetaFile(p.hMF); ignored := A.GlobalUnlock(med.data)
				END
			END;
			handle := A.GlobalFree(med.data); med.data := 0
		END;
		med.tymed := {}; med.pUnkForRelease := 0
	END ReleaseMedium;

	PROCEDURE FileMemory (f: Files.File): A.HGLOBAL;
		VAR handle, p: S.ADDRESS; data: ARRAY 4096 OF BYTE; r: Files.Reader; n, pos, ignored: INTEGER;
	BEGIN
		handle := A.GlobalAlloc({1}, f.Length()); IF handle = 0 THEN RETURN 0 END;
		p := A.GlobalLock(handle); IF p = 0 THEN p := A.GlobalFree(handle); RETURN 0 END;
		r := f.NewReader(NIL); pos := 0;
		WHILE pos < f.Length() DO
			n := MIN(LEN(data), f.Length()-pos); r.ReadBytes(data, 0, n);
			S.MOVE(S.ADR(data), p+pos, n); INC(pos, n)
		END;
		ignored := A.GlobalUnlock(handle); RETURN handle
	END FileMemory;

	PROCEDURE NativeData (v: Views.View; w, h: INTEGER; single: BOOLEAN): A.HGLOBAL;
		VAR f: Files.File; wr: Stores.Writer; handle: A.HGLOBAL;
	BEGIN
		f := Files.dir.Temp(); wr.ConnectTo(f); wr.WriteInt(obfTag); wr.WriteInt(0);
		wr.WriteInt(w); wr.WriteInt(h); IF single THEN wr.WriteSChar(1X) ELSE wr.WriteSChar(0X) END;
		wr.WriteStore(v); handle := FileMemory(f); f.Close; RETURN handle
	END NativeData;

	PROCEDURE NativeInfo (v: Views.View; w, h: INTEGER; single: BOOLEAN): A.HGLOBAL;
		VAR handle: A.HGLOBAL; p: InfoPtr; ignored: INTEGER;
	BEGIN
		handle := A.GlobalAlloc({1,6}, SIZE(InfoDesc)); IF handle = 0 THEN RETURN 0 END;
		p := S.VAL(InfoPtr, A.GlobalLock(handle));
		IF p = NIL THEN handle := A.GlobalFree(handle); RETURN 0 END;
		Services.GetTypeName(v, p.type); p.w := w; p.h := h; p.single := single;
		ignored := A.GlobalUnlock(handle); RETURN handle
	END NativeInfo;

	PROCEDURE (c: PaintContext) ThisModel (): Models.Model;
	BEGIN RETURN NIL END ThisModel;

	PROCEDURE (c: PaintContext) GetSize (OUT w, h: INTEGER);
	BEGIN w := c.w; h := c.h END GetSize;

	PROCEDURE (c: PaintContext) SetSize (w, h: INTEGER);
	END SetSize;

	PROCEDURE (c: PaintContext) Normalize (): BOOLEAN;
	BEGIN RETURN TRUE END Normalize;

	PROCEDURE (v: PaintView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
	BEGIN Views.InstallFrame(f, v.view, 0, 0, 0, FALSE) END Restore;

	PROCEDURE (v: PaintView) GetNewFrame (VAR frame: Views.Frame);
		VAR f: Views.RootFrame;
	BEGIN NEW(f); frame := f END GetNewFrame;

	PROCEDURE Picture (v: Views.View; w, h: INTEGER; VAR med: O.STGMEDIUM);
		VAR bp: Properties.BoundsPref; sp: Properties.SizePref;
			p: WinPorts.Port; c: PaintContext; d: PaintView; f: Views.RootFrame; g: Views.Frame;
			dc, mf, handle: S.ADDRESS; mp: A.PtrMETAFILEPICT; ignored, unit: INTEGER;
	BEGIN
		IF (w = Views.undefined) OR (h = Views.undefined) THEN
			bp.w := Views.undefined; bp.h := Views.undefined; Views.HandlePropMsg(v, bp); w := bp.w; h := bp.h
		END;
		IF (w = Views.undefined) OR (h = Views.undefined) THEN
			sp.w := Views.undefined; sp.h := Views.undefined; sp.fixedW := FALSE; sp.fixedH := FALSE;
			Views.HandlePropMsg(v, sp); w := sp.w; h := sp.h
		END;
		IF (w <= 0) OR (h <= 0) THEN RETURN END;
		unit := MAX(1, WinGui.unit); dc := A.CreateMetaFileW(NIL); IF dc = 0 THEN RETURN END;
		ignored := A.SetMapMode(dc, 8); ignored := A.SetWindowOrgEx(dc, 0, 0, NIL);
		ignored := A.SetWindowExtEx(dc, MAX(1,w DIV unit), MAX(1,h DIV unit), NIL);
		NEW(p); p.Init(unit, Ports.screen); p.SetSize(MAX(1,w DIV unit), MAX(1,h DIV unit)); p.SetDC(dc, 0);
		NEW(c); c.w := w; c.h := h; NEW(d); d.view := Views.CopyOf(v, Views.shallow);
		Stores.Join(d, d.view); d.InitContext(c); d.view.InitContext(c); Stores.InitDomain(d);
		d.GetNewFrame(g); f := g(Views.RootFrame); f.ConnectTo(p);
		Views.SetRoot(f, d, FALSE, {}); Views.AdaptRoot(f); Views.RestoreRoot(f, 0, 0, w, h);
		mf := A.CloseMetaFile(dc); IF mf = 0 THEN RETURN END;
		handle := A.GlobalAlloc({1,6}, SIZE(A.METAFILEPICT));
		IF handle = 0 THEN ignored := A.DeleteMetaFile(mf); RETURN END;
		mp := S.VAL(A.PtrMETAFILEPICT, A.GlobalLock(handle));
		IF mp = NIL THEN ignored := A.DeleteMetaFile(mf); handle := A.GlobalFree(handle); RETURN END;
		mp.mm := 8; mp.xExt := w DIV (Ports.mm DIV 100); mp.yExt := h DIV (Ports.mm DIV 100); mp.hMF := mf;
		ignored := A.GlobalUnlock(handle); med.tymed := O.TYMED_MFPICT; med.data := handle
	END Picture;

	PROCEDURE Export (IN command: ARRAY OF CHAR; v: Views.View; w, h: INTEGER; single: BOOLEAN; VAR med: O.STGMEDIUM);
		VAR val: ExpVal; ok: BOOLEAN;
	BEGIN Command(command, val, ok); IF ok THEN val.p(v, w, h, 0, 0, single, med) END END Export;

	PROCEDURE PrepareTransfer* (v: Views.View; w, h: INTEGER; isSingle: BOOLEAN;
		OUT formats: ARRAY OF INTEGER; OUT med: ARRAY OF O.STGMEDIUM);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 5 DO med[i].tymed := {}; med[i].data := 0; med[i].pUnkForRelease := 0 END;
		formats[0] := native; formats[1] := info; formats[2] := rtf;
		formats[3] := A.CF_UNICODETEXT; formats[4] := A.CF_TEXT; formats[5] := A.CF_METAFILEPICT;
		med[0].tymed := O.TYMED_HGLOBAL; med[0].data := NativeData(v, w, h, isSingle);
		med[1].tymed := O.TYMED_HGLOBAL; med[1].data := NativeInfo(v, w, h, isSingle);
		IF Services.Is(v, "TextViews.View") THEN
			Export("WinTextConv.ExportDRichText", v, w, h, isSingle, med[2]);
			Export("WinTextConv.ExportDUnicode", v, w, h, isSingle, med[3]);
			Export("WinTextConv.ExportDText", v, w, h, isSingle, med[4])
		END;
		IF Services.Is(v, "WinPictures.StdView") THEN Export("WinPictures.ExportDPict", v, w, h, isSingle, med[5]) END;
		IF med[5].data = 0 THEN Picture(v, w, h, med[5]) END;
	END PrepareTransfer;

	PROCEDURE (hook: ClipboardHook) Register (v: Views.View; w, h: INTEGER; isSingle: BOOLEAN);
		VAR med: ARRAY 6 OF O.STGMEDIUM; formats: ARRAY 6 OF INTEGER;
			copy: Views.View; i, ignored: INTEGER; result: S.ADDRESS; ok: BOOLEAN;
	BEGIN
		IF registering THEN RETURN END; registering := TRUE; lastError := 0;
		copy := Views.CopyOf(v, Views.deep); PrepareTransfer(copy, w, h, isSingle, formats, med);
		IF owner = 0 THEN owner := A.CreateWindowExW({}, "STATIC", "BlackBox clipboard", {}, 0, 0, 0, 0, 0, 0, 0, 0) END;
		ok := (owner # 0) & (med[0].data # 0);
		IF ok THEN ok := A.OpenClipboard(owner) # 0 END;
		IF ok THEN
			IF A.EmptyClipboard() # 0 THEN
				FOR i := 0 TO 5 DO
					IF med[i].data # 0 THEN
						result := A.SetClipboardData(formats[i], med[i].data);
						IF result # 0 THEN med[i].data := 0 ELSE lastError := A.GetLastError() END
					END
				END;
				content := copy; width := w; height := h; singleton := isSingle
			ELSE lastError := A.GetLastError()
			END;
			ignored := A.CloseClipboard(); sequence := A.GetClipboardSequenceNumber()
		ELSE lastError := A.GetLastError()
		END;
		FOR i := 0 TO 5 DO ReleaseMedium(med[i]) END;
		registering := FALSE;
		IF lastError # 0 THEN Dialog.ShowStatus("Cannot access the Windows clipboard") END
	END Register;

	PROCEDURE CloneMedium* (format: INTEGER; source: S.ADDRESS; OUT med: O.STGMEDIUM): BOOLEAN;
		VAR target, src, dst, size: S.ADDRESS; ignored: INTEGER;
			from, to: A.PtrMETAFILEPICT;
	BEGIN
		med.tymed := {}; med.data := 0; med.pUnkForRelease := 0;
		IF source # 0 THEN
			size := A.GlobalSize(source);
			IF (size > 0) & (size <= MAX(INTEGER)-2) THEN
				src := A.GlobalLock(source);
				IF src # 0 THEN
					target := A.GlobalAlloc({1,6}, size+2);
					IF target # 0 THEN
						dst := A.GlobalLock(target);
						IF dst # 0 THEN
							S.MOVE(src, dst, size);
							IF format = A.CF_METAFILEPICT THEN
								IF size >= SIZE(A.METAFILEPICT) THEN
									from := S.VAL(A.PtrMETAFILEPICT, src); to := S.VAL(A.PtrMETAFILEPICT, dst);
									to.hMF := 0;
									IF (from.mm >= 1) & (from.mm <= 8) THEN
										to.hMF := A.CopyMetaFileW(from.hMF, NIL);
										IF to.hMF # 0 THEN med.tymed := O.TYMED_MFPICT END
									END
								END
							ELSE med.tymed := O.TYMED_HGLOBAL
							END;
							ignored := A.GlobalUnlock(target)
						END;
						IF med.tymed # {} THEN med.data := target ELSE target := A.GlobalFree(target) END
					END;
					ignored := A.GlobalUnlock(source)
				END
			END
		END;
		RETURN med.data # 0
	END CloneMedium;

	PROCEDURE ReadMedium (format: INTEGER; OUT med: O.STGMEDIUM): BOOLEAN;
		VAR ok: BOOLEAN; ignored: INTEGER;
	BEGIN
		med.tymed := {}; med.data := 0; med.pUnkForRelease := 0;
		IF A.OpenClipboard(owner) = 0 THEN RETURN FALSE END;
		ok := CloneMedium(format, A.GetClipboardData(format), med);
		ignored := A.CloseClipboard(); RETURN ok
	END ReadMedium;

	PROCEDURE ImportNative (VAR med: O.STGMEDIUM; OUT v: Views.View; OUT w, h: INTEGER; OUT single: BOOLEAN);
		VAR f: Files.File; wr: Files.Writer; rd: Stores.Reader; store: Stores.Store;
			p, size: S.ADDRESS; data: ARRAY 4096 OF BYTE; pos, n, tag, version, ignored: INTEGER; ch: SHORTCHAR;
	BEGIN
		v := NIL; size := A.GlobalSize(med.data); p := A.GlobalLock(med.data);
		IF (p = 0) OR (size < 17) THEN IF p # 0 THEN ignored := A.GlobalUnlock(med.data) END; RETURN END;
		S.GET(p, tag); S.GET(p+4, version);
		IF (tag # obfTag) OR (version # 0) THEN ignored := A.GlobalUnlock(med.data); RETURN END;
		f := Files.dir.Temp(); wr := f.NewWriter(NIL); pos := 0;
		WHILE pos < size DO
			n := MIN(LEN(data), SHORT(size)-pos); S.MOVE(p+pos, S.ADR(data), n); wr.WriteBytes(data, 0, n); INC(pos, n)
		END;
		ignored := A.GlobalUnlock(med.data);
		rd.ConnectTo(f); rd.ReadInt(tag); rd.ReadInt(version); rd.ReadInt(w); rd.ReadInt(h); rd.ReadSChar(ch);
		single := ch # 0X; rd.ReadStore(store);
		IF (store # NIL) & (store IS Views.View) THEN v := store(Views.View) END
	END ImportNative;

	PROCEDURE ImportMedium* (format: INTEGER; VAR med: O.STGMEDIUM;
		OUT v: Views.View; OUT w, h: INTEGER; OUT single: BOOLEAN);
		VAR command: ARRAY 64 OF CHAR; val: ImpVal; ok: BOOLEAN;
	BEGIN
		v := NIL; w := Views.undefined; h := Views.undefined; single := FALSE;
		IF format = native THEN ImportNative(med, v, w, h, single); RETURN
		ELSIF format = rtf THEN command := "WinTextConv.ImportDRichText"
		ELSIF format = A.CF_UNICODETEXT THEN command := "WinTextConv.ImportDUnicode"
		ELSIF format = A.CF_TEXT THEN command := "WinTextConv.ImportDText"
		ELSIF format = A.CF_METAFILEPICT THEN command := "WinPictures.ImportDPict"
		ELSE RETURN
		END;
		Command(command, val, ok); IF ok THEN val.p(med, v, w, h, single) END
	END ImportMedium;

	PROCEDURE TransferFormat* (index: INTEGER): INTEGER;
	BEGIN
		CASE index OF 0: RETURN native | 1: RETURN info | 2: RETURN rtf
		| 3: RETURN A.CF_UNICODETEXT | 4: RETURN A.CF_TEXT | 5: RETURN A.CF_METAFILEPICT
		ELSE RETURN 0
		END
	END TransferFormat;

	PROCEDURE Import (IN command: ARRAY OF CHAR; format: INTEGER; OUT v: Views.View; OUT w, h: INTEGER; OUT single: BOOLEAN);
		VAR med: O.STGMEDIUM; val: ImpVal; ok: BOOLEAN;
	BEGIN
		v := NIL;
		IF ReadMedium(format, med) THEN
			Command(command, val, ok); IF ok THEN val.p(med, v, w, h, single) END;
			ReleaseMedium(med)
		END
	END Import;

	PROCEDURE InfoType (OUT type: Stores.TypeName): BOOLEAN;
		VAR med: O.STGMEDIUM; p: InfoPtr; ignored: INTEGER;
	BEGIN
		type := "";
		IF ReadMedium(info, med) THEN
			IF A.GlobalSize(med.data) >= SIZE(InfoDesc) THEN
				p := S.VAL(InfoPtr, A.GlobalLock(med.data));
				IF p # NIL THEN type := p.type; type[LEN(type)-1] := 0X; ignored := A.GlobalUnlock(med.data) END
			END;
			ReleaseMedium(med)
		END;
		RETURN type # ""
	END InfoType;

	PROCEDURE ConvertibleTo* (type: Stores.TypeName): BOOLEAN;
		VAR nativeType: Stores.TypeName;
	BEGIN
		IF Own() THEN RETURN (type = "") OR Services.Is(content, type) END;
		IF A.IsClipboardFormatAvailable(native) # 0 THEN
			IF type = "" THEN RETURN TRUE END;
			IF InfoType(nativeType) & Services.Extends(nativeType, type) THEN RETURN TRUE END
		END;
		IF (type = "") OR Services.Extends("TextViews.View", type) THEN
			IF (A.IsClipboardFormatAvailable(rtf) # 0) OR (A.IsClipboardFormatAvailable(A.CF_UNICODETEXT) # 0)
				OR (A.IsClipboardFormatAvailable(A.CF_TEXT) # 0) THEN RETURN TRUE END
		END;
		RETURN ((type = "") OR Services.Extends("WinPictures.StdView", type))
			& (A.IsClipboardFormatAvailable(A.CF_METAFILEPICT) # 0)
	END ConvertibleTo;

	PROCEDURE GetClipView* (IN type: Stores.TypeName; OUT v: Views.View; OUT w, h: INTEGER; OUT isSingle: BOOLEAN);
		VAR med: O.STGMEDIUM; nativeType: Stores.TypeName; canText: BOOLEAN;
	BEGIN
		v := NIL; w := Views.undefined; h := Views.undefined; isSingle := FALSE;
		IF Own() THEN
			IF (type = "") OR Services.Is(content, type) THEN
				v := Views.CopyOf(content, Views.deep); w := width; h := height; isSingle := singleton
			END;
			RETURN
		END;
		IF (type = "") OR InfoType(nativeType) & Services.Extends(nativeType, type) THEN
			IF ReadMedium(native, med) THEN ImportNative(med, v, w, h, isSingle); ReleaseMedium(med) END
		END;
		canText := (type = "") OR Services.Extends("TextViews.View", type);
		IF (v = NIL) & canText THEN Import("WinTextConv.ImportDRichText", rtf, v, w, h, isSingle) END;
		IF (v = NIL) & canText THEN Import("WinTextConv.ImportDUnicode", A.CF_UNICODETEXT, v, w, h, isSingle) END;
		IF (v = NIL) & canText THEN Import("WinTextConv.ImportDText", A.CF_TEXT, v, w, h, isSingle) END;
		IF (v = NIL) & ((type = "") OR Services.Extends("WinPictures.StdView", type)) THEN
			Import("WinPictures.ImportDPict", A.CF_METAFILEPICT, v, w, h, isSingle)
		END;
		IF (v # NIL) & (type # "") & ~Services.Is(v, type) THEN v := NIL END
	END GetClipView;

	PROCEDURE (h: ClipboardHook) Paste;
		VAR ops: Controllers.PollOpsMsg; msg: Controllers.EditMsg;
	BEGIN
		cloneAttributes := TRUE; isText := TRUE; Controllers.PollOps(ops); isText := ops.type = "TextViews.View";
		IF Controllers.paste IN ops.valid THEN
			msg.clipboard := TRUE; GetClipView(ops.pasteType, msg.view, msg.w, msg.h, msg.isSingle);
			IF msg.view # NIL THEN msg.op := Controllers.paste; Controllers.Forward(msg) END
		END;
		cloneAttributes := FALSE; isText := TRUE
	END Paste;

	PROCEDURE Flush*;
	BEGIN (* All formats are rendered eagerly; Windows already owns their handles. *) END Flush;

	PROCEDURE Install*;
		VAR hook: ClipboardHook;
	BEGIN NEW(hook); StdCmds.clipboardHook := hook END Install;

BEGIN
	isText := TRUE;
	native := A.RegisterClipboardFormatW("BlackBox Data"); info := A.RegisterClipboardFormatW("BlackBox Info");
	rtf := A.RegisterClipboardFormatW("Rich Text Format")
CLOSE
	IF owner # 0 THEN IF A.DestroyWindow(owner) # 0 THEN owner := 0 END END
END WinClipboard.
