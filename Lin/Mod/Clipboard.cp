MODULE LinClipboard;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		SYSTEM, Utf, GLib := LinGtk2GLib, 
		Gdk := LinGtk4Gdk, Gtk := LinGtk4Gtk, GtkU := LinGtk4Util,
		HostWindows := LinBackends, 
		Log, Strings, Dialog, Views, Controllers,
		TextModels, TextViews, Services,
		StdCmds;

	TYPE
		Clip = POINTER TO RECORD
			view: Views.View;
			w, h: INTEGER;
			isSingle: BOOLEAN;
			type: ARRAY 256 OF CHAR;
		END;
		
		ClipboardHook = POINTER TO RECORD (StdCmds.ClipboardHook) END;
		
	TYPE PasteAction = RECORD (Services.SafeAction) text: POINTER TO ARRAY OF CHAR END;

	VAR curClip: Clip; installed: BOOLEAN;

	PROCEDURE ExportText (v: TextViews.View): POINTER TO ARRAY OF CHAR;
		VAR rd: TextModels.Reader; str: POINTER TO ARRAY OF CHAR; i, len: INTEGER;
	BEGIN
		rd := v.ThisModel().NewReader(NIL);
		len := v.ThisModel().Length();
		IF len > 0 THEN
			NEW(str, len + 1);
			i := 0;
			rd.SetPos(0);
			REPEAT
				rd.Read;
				IF rd.view = NIL THEN
					IF (rd.char = TextModels.line) OR (rd.char = TextModels.para) THEN
						str[i] := 0AX
					ELSE
						str[i] := rd.char
					END;
					INC(i)
				END
			UNTIL rd.eot OR (i >= len);
			str[i] := 0X
		ELSE
			NEW(str, 1); str[0] := 0X
		END;
		RETURN str
	END ExportText;
	
	PROCEDURE ExportUtf8 (IN s: ARRAY OF CHAR): POINTER TO ARRAY OF SHORTCHAR;
		VAR ustr: POINTER TO ARRAY OF SHORTCHAR; res: INTEGER;
	BEGIN
		NEW(ustr, 4 * LEN(s$) + 1);
		Utf.StringToUtf8(s, ustr, res);
		IF res = 0 THEN
			RETURN ustr
		ELSE
			RETURN NIL
		END
	END ExportUtf8;
	
	PROCEDURE Clipboard (): SYSTEM.ADDRESS;
	BEGIN RETURN Gdk.gdk_display_get_clipboard(Gdk.gdk_display_get_default()) END Clipboard;

	PROCEDURE Register* (v: Views.View; w,h: INTEGER; isSingle: BOOLEAN);
		VAR utf: POINTER TO ARRAY OF SHORTCHAR;
	BEGIN
		WITH v: TextViews.View DO utf := ExportUtf8(ExportText(v))
		ELSE NEW(utf,1); utf[0] := 0X END;
		IF utf # NIL THEN Gdk.gdk_clipboard_set_text(Clipboard(),utf^) END;
		NEW(curClip); curClip.view := Views.CopyOf(v,Views.deep);
		curClip.w := w; curClip.h := h; curClip.isSingle := isSingle;
		Services.GetTypeName(v,curClip.type)
	END Register;

	PROCEDURE (hook: ClipboardHook) Register (v: Views.View; w,h: INTEGER; isSingle: BOOLEAN);
	BEGIN Register(v,w,h,isSingle) END Register;

	PROCEDURE [ccall] Changed (clipboard,data: SYSTEM.ADDRESS);
	BEGIN IF Gdk.gdk_clipboard_is_local(clipboard) = 0 THEN curClip := NIL END END Changed;

	PROCEDURE PasteView (view: Views.View; w,h: INTEGER; single: BOOLEAN);
		VAR msg: Controllers.EditMsg; ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.SetCurrentPath(Controllers.targetPath);
		Controllers.PollOps(ops);
		IF (Controllers.paste IN ops.valid) & (view # NIL) THEN
			msg.clipboard := TRUE; msg.view := view; msg.w := w; msg.h := h; msg.isSingle := single;
			msg.op := Controllers.paste; Controllers.Forward(msg)
		END;
		Controllers.ResetCurrentPath
	END PasteView;


	PROCEDURE (VAR a: PasteAction) Do;
	BEGIN PasteView(TextViews.dir.New(TextModels.dir.NewFromString(a.text^)),0,0,FALSE) END Do;

	PROCEDURE [ccall] ReadText (clipboard,result,data: SYSTEM.ADDRESS);
		VAR bytes: GLib.PString; error: SYSTEM.ADDRESS; action: PasteAction; res,i: INTEGER;
	BEGIN
		error := 0; bytes := Gdk.gdk_clipboard_read_text_finish(clipboard,result,error);
		IF bytes # NIL THEN
			NEW(action.text,LEN(bytes$)+1); Utf.Utf8ToString(bytes$,action.text^,res);
			GLib.g_free(SYSTEM.VAL(GLib.gpointer,bytes));
			IF res = 0 THEN
				i := 0; WHILE action.text[i] # 0X DO IF action.text[i] = 0AX THEN action.text[i] := TextModels.para END; INC(i) END;
				Services.Try(action)
			END
		END;
		IF error # 0 THEN GLib.g_error_free(SYSTEM.VAL(GLib.GError,error)) END
	END ReadText;

	PROCEDURE (hook: ClipboardHook) Paste;
	BEGIN
		IF (curClip # NIL) & (Gdk.gdk_clipboard_is_local(Clipboard()) # 0) THEN
			PasteView(Views.CopyOf(curClip.view,Views.deep),curClip.w,curClip.h,curClip.isSingle)
		ELSE Gdk.gdk_clipboard_read_text_async(Clipboard(),0,SYSTEM.ADR(ReadText),0) END
	END Paste;

	PROCEDURE SetSignals*;
		VAR id: SYSTEM.ADDRESS;
	BEGIN
		IF ~installed THEN
			id := GtkU.gtk_signal_connect(SYSTEM.VAL(Gtk.GtkWidget,Clipboard()),"changed",SYSTEM.ADR(Changed),0);
			installed := TRUE
		END
	END SetSignals;

	PROCEDURE Install*;
		VAR hook: ClipboardHook;
	BEGIN NEW(hook); StdCmds.clipboardHook := hook; SetSignals END Install;

END LinClipboard.
