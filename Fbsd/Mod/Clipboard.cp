MODULE FbsdClipboard;

	(* TODO: Add converters, memory files and other stuff to support multiple BlackBoxes and more formats *)

	IMPORT
		SYSTEM, Utf, 
		Gdk := FbsdGtk2Gdk, Gtk := FbsdGtk2Gtk, GtkU := FbsdGtk2Util,
		HostWindows := FbsdBackends, 
		Log, Strings, Dialog, Views, Controllers,
		TextModels, TextViews, Services,
		StdCmds;
		
	CONST
		string* = 0; view* = 1; (* types for the clipboard *)
		
	TYPE
		Clip = POINTER TO RECORD
			view: Views.View;
			w, h: INTEGER;
			isSingle: BOOLEAN;
			type: ARRAY 256 OF CHAR;
		END;
		
		ClipboardHook = POINTER TO RECORD (StdCmds.ClipboardHook) END;
		
	VAR
		bbAtom-: INTEGER;
		curClip: Clip;
	
	PROCEDURE Register* (v: Views.View; w, h: INTEGER; isSingle: BOOLEAN);
	(* imported directly in StdStdCFrames, which looks bad *)
		VAR c: Gtk.GtkClipboard; ustr: POINTER TO ARRAY OF SHORTCHAR; res, i, len: INTEGER;
			rd: TextModels.Reader; str: POINTER TO ARRAY OF CHAR; 
	BEGIN
		(* text view can be converted to global clipboard *)
		WITH v: TextViews.View DO
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
				str[i] := 0X;
				NEW(ustr, 2 * len + 2);
				Utf.StringToUtf8(str, ustr, res);
				IF res = 0 THEN
					c :=Gtk.gtk_clipboard_get(Gdk.GDK_SELECTION_CLIPBOARD);
					Gtk.gtk_clipboard_set_text(c, ustr^, LEN(ustr$));
					Gtk.gtk_clipboard_store(c);
				ELSE
					Dialog.ShowMsg("utf8 conversion failed: " + str$)
				END
			END;
		ELSE END;
		
		(* local clipboard *)
		NEW(curClip);
		curClip.view := Views.CopyOf(v, Views.deep);
		curClip.w := w; curClip.h := h; curClip.isSingle := isSingle;
		Services.GetTypeName(v, curClip.type);
		IF Gtk.gtk_selection_owner_set(HostWindows.first, Gdk.GDK_SELECTION_PRIMARY, Gdk.GDK_CURRENT_TIME) = 0
		THEN
			Dialog.ShowMsg("HostClipboard.Register failed")
		END
	END Register;
	
	PROCEDURE (hook: ClipboardHook) Register* (v: Views.View; w, h: INTEGER; isSingle: BOOLEAN);
	BEGIN Register(v, w, h, isSingle)
	END Register;
	
	(* "selection-clear-event" *)
	PROCEDURE [ccall] Clear* (w: Gtk.GtkWidget; event, user_data: INTEGER);
	BEGIN
		curClip := NIL
	END Clear;
	
	(* "selection-get" *)
	PROCEDURE [ccall] ConvertCopy* (widget: Gtk.GtkWidget; VAR selection_data: Gtk.GtkSelectionData; info, time: INTEGER; data: INTEGER);
		VAR 
			s: ARRAY 256 OF SHORTCHAR; v: Views.View; i: INTEGER;
			rd: TextModels.Reader; uni: ARRAY 7 OF CHAR;
			ss: POINTER TO ARRAY OF SHORTCHAR; ch: CHAR;
	BEGIN
		IF curClip # NIL THEN
			s := "BlackBox View: " + SHORT(curClip.type);
			IF info = Gdk.GDK_SELECTION_TYPE_STRING THEN 
				(* TODO: Current implementation always does this. *)
				v := curClip.view;
				WITH v: TextViews.View DO
					rd := v.ThisModel().NewReader(NIL);
					rd.SetPos(0);
					i := 0; rd.ReadChar(ch);
					WHILE ~rd.eot DO
						IF ORD(ch) < 256 THEN
							INC(i)
						ELSE
							INC(i, 6)
						END;
						rd.ReadChar(ch)
					END;
					NEW(ss, i + 1);
					
					rd.SetPos(0);
					i := 0; rd.ReadChar(ch);
					WHILE ~rd.eot DO
						IF ORD(ch) < 256 THEN
							ss[i] := SHORT(ch);
							INC(i)
						ELSE
							ss[i] := "\";
							ss[i + 1] := "u";
							Strings.IntToStringForm(ORD(ch), Strings.hexadecimal, 4, "0", FALSE, uni);
							ss[i + 2] := SHORT(Strings.Lower(uni[0]));
							ss[i + 3] := SHORT(Strings.Lower(uni[1]));
							ss[i + 4] := SHORT(Strings.Lower(uni[2]));
							ss[i + 5] := SHORT(Strings.Lower(uni[3]));
							INC(i, 6)
						END;
						rd.ReadChar(ch)
					END;
					ss[i] := 0X;
					Gtk.gtk_selection_data_set(selection_data, Gdk.GDK_SELECTION_TYPE_STRING, 8, ss^, LEN(ss$))
				ELSE
					Gtk.gtk_selection_data_set(selection_data, Gdk.GDK_SELECTION_TYPE_STRING, 8, s, LEN(s$))
				END;
				
			ELSIF info = bbAtom THEN
				(* TODO: This is where memory files and stuff are needed to make it work between two different BB-processes *)
				Gtk.gtk_selection_data_set(selection_data, bbAtom, 8, s, LEN(s$))
			ELSE
				Dialog.ShowMsg("HostClipboard.ConvertCopy: Unknown type")
			END
		END
	END ConvertCopy;
	

	PROCEDURE AtoD (IN ss: ARRAY OF SHORTCHAR): POINTER TO ARRAY OF CHAR;
		VAR s: POINTER TO ARRAY OF CHAR; letter: ARRAY 6 OF CHAR; res, i, j, len: INTEGER; ch: CHAR;
		
		PROCEDURE ReadUnicode (p: INTEGER; OUT ch: CHAR): BOOLEAN;
		VAR int: INTEGER;
		BEGIN
			IF (len - i > 5) & (ss[i] = "\") & (ss[i+1] = "u") &
				(((ss[i+2] >= "0") & (ss[i+2] <= "9")) OR ((ss[i+2] >= "a") & (ss[i+2] <= "f"))) &
				(((ss[i+3] >= "0") & (ss[i+3] <= "9")) OR ((ss[i+3] >= "a") & (ss[i+3] <= "f"))) &
				(((ss[i+4] >= "0") & (ss[i+4] <= "9")) OR ((ss[i+4] >= "a") & (ss[i+4] <= "f"))) &
				(((ss[i+5] >= "0") & (ss[i+5] <= "9")) OR ((ss[i+5] >= "a") & (ss[i+5] <= "f"))) THEN
				letter[0] := Strings.Upper(ss[i+2]);
				letter[1] := Strings.Upper(ss[i+3]);
				letter[2] := Strings.Upper(ss[i+4]);
				letter[3] := Strings.Upper(ss[i+5]);
				letter[4] := "H";
				letter[5] := 0X;
				Strings.StringToInt(letter, int, res);
				IF res = 0 THEN
					ch := CHR(int)
				ELSE
					ch := "?"
				END;
				RETURN TRUE
			ELSE
				ch := ss[i];
				RETURN FALSE
			END
		END ReadUnicode;
	
	BEGIN
		(* count unicode symbols *)
		len := LEN(ss$);
		i := 0; j := 0;
		WHILE ss[i] # 0X DO
			IF ReadUnicode(i, ch) THEN
				INC(i, 6); INC(j)
			ELSE
				INC(i); INC(j)
			END
		END;
		IF i = j THEN (* guess UTF8 string *)
			NEW(s, LEN(ss$) * 4);
			Utf.Utf8ToString(ss, s, res);
			IF res = 0 THEN
				i := 0;
				WHILE ss[i] # 0X DO 
					IF s[i] = 0AX THEN s[i] := 0DX END; INC(i)
				END;
			ELSE
				s^ := "[failed to convert data guessed as utf8 string, printing raw data]  " + ss$
			END;
		ELSE
			(* create new string and convert *)
			NEW(s, j + 1);
			i := 0; j := 0;
			WHILE ss[i] # 0X DO 
				IF ss[i] = 0AX THEN
					s[j] := 0DX
				ELSE
					IF ReadUnicode(i, ch) THEN
						INC(i, 5);
						s[j] := ch
					ELSE
						s[j] := ss[i]
					END
				END;
				INC(i); INC(j)
			END;
			s[j] := 0X;
		END;
		RETURN s
	END AtoD;
	
	(* Must be connected to signal "selection-received" for HostWindows.main. This is done in HostMenus. *)
	
	PROCEDURE [ccall] DoPaste* (widget: Gtk.GtkWidget; 
					VAR selection_data: Gtk.GtkSelectionData; 
					time: INTEGER; 
					data: INTEGER);
	VAR ops: Controllers.PollOpsMsg; 
			msg: Controllers.EditMsg;
	BEGIN
		IF selection_data.length < 0 THEN 
			(* Dialog.ShowMsg("LinClipboard.DoPaste: Selection retrieval failed."); *)
			RETURN
		END;
		
		IF  (selection_data.type = bbAtom) 
		 OR (selection_data.type = Gdk.GDK_SELECTION_TYPE_STRING)
		THEN
			Controllers.PollOps(ops);
			IF Controllers.paste IN ops.valid THEN
				msg.clipboard := TRUE;
				IF curClip = NIL THEN 
					(* TODO: Is this a valid way to figure out if the current copy is a BB view or not? *)
					
					msg.view := TextViews.dir.New(
												TextModels.dir.NewFromString(
														AtoD(selection_data.data$)));
					msg.isSingle := FALSE; 
					msg.w := 0; msg.h := 0
				ELSE
					msg.view := Views.CopyOf(curClip.view, Views.deep);
					msg.isSingle := curClip.isSingle; 
					msg.w := curClip.w; msg.h := curClip.h
				END;
				IF msg.view # NIL THEN
					msg.op := Controllers.paste; Controllers.Forward(msg)
				END
			END
		ELSE
			Dialog.ShowMsg("unknown selection type");
		END
	END DoPaste;

	PROCEDURE (hook: ClipboardHook) Paste; (* was PROCEDURE InitPaste*;*)
	BEGIN
		IF ~Gtk.gtk_selection_convert(HostWindows.first,
					Gdk.GDK_SELECTION_PRIMARY, 											
					Gdk.GDK_TARGET_STRING, Gdk.GDK_CURRENT_TIME) 
		THEN
			Dialog.ShowMsg("gtk_selection_convert failed")
		END
	END Paste;

	PROCEDURE Init;
	BEGIN
		bbAtom := Gdk.gdk_atom_intern("BBView", 0);
	END Init;
	
	PROCEDURE SetSignals*;
	VAR res: INTEGER;
	BEGIN
		IF HostWindows.first = NIL THEN
			Log.String("can't install Clipboard signals:"); Log.Ln;
			Log.String(" ->  LinWindows.last = NIL"); Log.Ln;
		ELSE
		
		(* copy/paste functionallity (selections) *)
		Gtk.gtk_selection_add_target(HostWindows.first, Gdk.GDK_SELECTION_PRIMARY, Gdk.GDK_TARGET_STRING, Gdk.GDK_SELECTION_TYPE_STRING);
		Gtk.gtk_selection_add_target(HostWindows.first, Gdk.GDK_SELECTION_PRIMARY, bbAtom, bbAtom);

		res := GtkU.gtk_signal_connect(HostWindows.first, "selection-received", SYSTEM.ADR(DoPaste), 0);
		res := GtkU.gtk_signal_connect(HostWindows.first, "selection-get", SYSTEM.ADR(ConvertCopy), 0);
		res := GtkU.gtk_signal_connect(HostWindows.first, "selection-clear-event", SYSTEM.ADR(Clear), 0);
		END;
	END SetSignals;
	
	PROCEDURE Install*; 
		VAR h: ClipboardHook;
	BEGIN NEW(h); StdCmds.clipboardHook := h
	END Install;
	
BEGIN
	Init
END FbsdClipboard.