MODULE WinClipboard;
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


	IMPORT
		COM, WinApi, WinOle,
		Services, Stores, Views, Documents, Controllers, OleData, (*, HostWindows := WinWindows*)
		StdCmds;
	
	TYPE
		EmptyView = POINTER TO RECORD (Views.View) END;
		ClipboardHook = POINTER TO RECORD (StdCmds.ClipboardHook) END;
	
	VAR
		cloneAttributes*: BOOLEAN;	(* set by HostCmds, used by Importers *)
		isText*: BOOLEAN;	(* set by HostCmds, used by GetClipView *)

		doc: Documents.Document;	(* contents of clipboard window *)
		isSingleton: BOOLEAN;	(* contents of clipboard is singleton view *)
		width, height: INTEGER;	(* size of clipboard content *)
		empty: Views.View;	(* contents of empty clipboard *)
		cbdata: WinOle.IDataObject;	(* data object registred via OLE *)
		(* doc.ThisView() is BB's internal clipboard: it holds the view placed into the clipboard thru Cut/Copy commands, or a representation of a Windows's clipboard as a BB view after a Paste command.
		It's not clear to me why a view copy of a pasted Windows clipboard has to be kept around
		It's also not clear to me why the clipboard view has to be held in a document, and the document - in a special window .cbviewer that is not in the list of windows in WinWindows. Maybe it is needed down the road somewhere where the BB view is converted to some host format in HostTextConv?
		width and height hold the doc.ThisView()'s size
		cbdata is an OLE wrapper around doc.ThisView(); it is used to represent the BB clipboard to the rest of Windows*)
	
	PROCEDURE (hook: ClipboardHook) Register* (v: Views.View; w, h: INTEGER; isSingle: BOOLEAN);
		VAR res: COM.RESULT;
	BEGIN
		(*ASSERT(doc.context # NIL, 20 (* InitClipboardDoc called *));*)
		doc.SetView(v, w, h);
		width := w; height := h;
		isSingleton := isSingle;
		cbdata := OleData.ViewData(v, w, h, isSingle);
		res := WinOle.OleSetClipboard(cbdata)
	END Register;
	
	PROCEDURE GetClipView* (
		IN type: Stores.TypeName; OUT v: Views.View; OUT w, h: INTEGER; OUT isSingle: BOOLEAN
	);
		VAR dobj: WinOle.IDataObject; res: COM.RESULT;
	BEGIN
		(*ASSERT(doc.context # NIL, 20 (* InitClipboardDoc called *));*)
		v := NIL;
		IF WinOle.OleIsCurrentClipboard(cbdata) = WinApi.S_OK THEN
			v := Views.CopyOf(doc.ThisView(), Views.deep);
			w := width; h := height; isSingle := isSingleton;
			IF (type # "") & ~Services.Is(v, type) THEN v := NIL END
		ELSE
			res := WinOle.OleGetClipboard(dobj);
			IF res >= 0 THEN
				IF isText THEN
					OleData.GetTextDataView(dobj, v, w, h, isSingle)
				ELSE
					OleData.GetDataView(dobj, type, v, w, h, isSingle)
				END;
				IF v # NIL THEN
					doc.SetView(Views.CopyOf(v, Views.deep), w, h);
					width := w; height := h;
					isSingleton := isSingle
				END
			END
		END
	END GetClipView;
	
	PROCEDURE ConvertibleTo* (type: Stores.TypeName): BOOLEAN;
		VAR dobj: WinOle.IDataObject; res: COM.RESULT;
	BEGIN
		IF WinOle.OleIsCurrentClipboard(cbdata) = WinApi.S_OK THEN
			RETURN (type = "") OR Services.Is(doc.ThisView(), type)
		ELSE
			res := WinOle.OleGetClipboard(dobj);
			IF res >= 0 THEN
				RETURN OleData.DataConvTo(dobj, type)
			END
		END;
		RETURN FALSE
	END ConvertibleTo;
	
	PROCEDURE Flush*;
		VAR res: COM.RESULT;
	BEGIN
		IF WinOle.OleIsCurrentClipboard(cbdata) = WinApi.S_OK THEN
			res := WinOle.OleFlushClipboard()
		END
	END Flush;
	
	PROCEDURE Dump*;
		VAR res: COM.RESULT; dobj: WinOle.IDataObject;
	BEGIN
		res := WinOle.OleGetClipboard(dobj);
		OleData.DumpData(dobj)
	END Dump;
	
	
	PROCEDURE (v: EmptyView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
	END Restore;
	

	PROCEDURE Init;
		VAR e: EmptyView;
	BEGIN
		NEW(e); empty := e;
		(* init document to be used to display the clipboard *)
		doc := Documents.dir.New(empty, Views.undefined, Views.undefined);
		(*HostWindows.OpenClipboard(doc)*)
	END Init;
	
	PROCEDURE InitClipboardDoc* (OUT clipDoc: Documents.Document);
	BEGIN ASSERT(doc.context = NIL, 20 (*run once*)); clipDoc := doc
	END InitClipboardDoc;
	
	PROCEDURE (h: ClipboardHook) Paste*;
	(** let focus document insert a copy of the clipboard's contents **)
	(* adapted from WinCmds.Paste; has to go back in some form *)
		VAR ops: Controllers.PollOpsMsg; msg: Controllers.EditMsg;
	BEGIN
		cloneAttributes := TRUE;
		isText := TRUE;
		Controllers.PollOps(ops);
		isText := ops.type = "TextViews.View";
		IF Controllers.paste IN ops.valid THEN
			msg.clipboard := TRUE;
			GetClipView(ops.pasteType, msg.view, msg.w, msg.h, msg.isSingle);
			IF msg.view # NIL THEN
				msg.op := Controllers.paste; Controllers.Forward(msg)
			END
		END;
		cloneAttributes := FALSE;
		isText := TRUE;
	END Paste;
	
	PROCEDURE Install*; 
		VAR h: ClipboardHook;
	BEGIN NEW(h); StdCmds.clipboardHook := h
	END Install;	
	
BEGIN
	Init
END WinClipboard.

	
WinClipboard.Flush

WinClipboard.Dump