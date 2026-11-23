MODULE LinGui;
	(**
		Version for Linux 
		This module initializes the Gtk environment, makes sure that all libraries are loaded in the correct order, and installes a message box hook in Kernel to allow graphical message boxes.
		Any application using the graphical user interface on Linux must have this module linked.
	**)

	IMPORT LinGtk4Bootstrap, SYSTEM, Kernel, Utf, 
		Gdk := LinGtk4Gdk, Gtk := LinGtk4Gtk, GU := LinGtk4Util, Fenv := LinFenv, Dl := LinDl, Libc := LinLibc, HostKernel := LinKernel, HostFiles := LinFiles;
	
	CONST
		(* constants for the message boxes *)
		mbClose = -1; mbOk = 0; mbCancel =1;
		mbRetry = 2; mbIgnore = 3; mbYes = 4; mbNo = 5;
		
	VAR
	(* debugging functions *)
	
		errString: PROCEDURE (IN string: ARRAY OF CHAR);
		errLn: PROCEDURE;
	
	PROCEDURE SetErr* (string: PROCEDURE(IN msg: ARRAY OF CHAR); ln: PROCEDURE);
	BEGIN errString := string; errLn := ln
	END SetErr;

	PROCEDURE ErrString (IN msg: ARRAY OF CHAR);
	BEGIN IF errString # NIL THEN errString(msg) END;
	END ErrString;

	PROCEDURE ErrLn;
	BEGIN IF errLn # NIL THEN errLn END;
	END ErrLn;

	PROCEDURE Msg (IN msg: ARRAY OF CHAR);
	BEGIN ErrString(msg); ErrLn
	END Msg;
	
	PROCEDURE DefErrLn;
		VAR res_: INTEGER;
	BEGIN
		res_ := Libc.printf0(0AX);
		res_ := Libc.fflush(0)
	END DefErrLn;

	PROCEDURE DefErrString (IN str: ARRAY OF CHAR);
		CONST max = 1024;
		VAR ss: ARRAY max + 2 OF SHORTCHAR; i, j, res_, len: INTEGER;
	BEGIN
		len := LEN(str); ss[max + 1] := 0X;
		i := 0; j := 0;
		WHILE (i < len) & (str[i] # 0X) DO ss[j] := SHORT(str[i]); INC(i);
			IF j = max THEN res_ := Libc.printfs("%s", ss); j := 0 ELSE INC(j) END
		END;
		IF j > 0 THEN ss[j] := 0X; res_ := Libc.printfs("%s", ss) END
	END DefErrString;
	
	PROCEDURE IntToString (x: LONGINT; OUT a: ARRAY OF CHAR);
		CONST max = 25;
		VAR b: ARRAY max OF CHAR; i, j: INTEGER; y: LONGINT;
	BEGIN
		IF x < 0 THEN x := -x; a[0] := '-'; j := 1 ELSIF x > 0 THEN j := 0 ELSE j := 1; a[0] := '0' END;
		i := 0;
		WHILE x # 0 DO y := x MOD 10; b[i] := SHORT(CHR(y + ORD('0'))); x := x DIV 10; INC(i) END;
		DEC(i); WHILE (i >= 0) & (j < max) DO a[j] := b[i]; INC(j); DEC(i) END;
		ASSERT(j < max, 20);
		a[j] := 0X; 
	END IntToString;
	
	PROCEDURE Int (x: LONGINT);
		VAR buf: ARRAY 16 OF CHAR;
	BEGIN
		IntToString(x, buf); ErrString(buf)
	END Int;

	(* end of debugging functions *)
	
	
	PROCEDURE MessageBox* (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
		VAR
			res, bt: INTEGER; dlg: SYSTEM.ADDRESS;
			uTitle, uMsg: ARRAY 1024 OF SHORTCHAR;
	BEGIN
		Utf.StringToUtf8(title, uTitle, res);
		Utf.StringToUtf8(msg, uMsg, res);
		
		(*
		GTK_BUTTONS_NONE* =0;
		GTK_BUTTONS_OK*=1;
		GTK_BUTTONS_CLOSE*=2;
		GTK_BUTTONS_CANCEL*=3;
		GTK_BUTTONS_YES_NO*=4;
		GTK_BUTTONS_OK_CANCEL*=5;
		*)
		
		bt := 2; (* close default *)
		IF (mbOk IN buttons) & (mbCancel IN buttons) THEN
			bt := 5 
		ELSIF mbOk IN buttons THEN
			bt := 1
		ELSIF mbCancel IN buttons THEN
			bt := 3
		ELSIF (mbYes IN buttons) & (mbNo IN buttons) THEN
			bt := 4
		END;
		
		
		dlg := SYSTEM.VAL(SYSTEM.ADDRESS, Gtk.gtk_message_dialog_new (NIL, {0, 1}, 1, bt, "%s", uMsg));
		Gtk.gtk_window_set_title(SYSTEM.VAL(Gtk.GtkWindow,dlg), uTitle);
		(* dialogIsOpen := TRUE; *)
		res := GU.RunDialog(SYSTEM.VAL(Gtk.GtkDialog,dlg));
		(* dialogIsOpen := FALSE; *)
		Gtk.gtk_window_destroy(SYSTEM.VAL(Gtk.GtkWindow,dlg));
		
		(*
		GTK_RESPONSE_NONE* = -1;
		GTK_RESPONSE_REJECT* = -2;
		GTK_RESPONSE_ACCEPT* = -3;
		GTK_RESPONSE_DELETE_EVENT* = -4;
		GTK_RESPONSE_OK*     = -5;
		GTK_RESPONSE_CANCEL* = -6;
		GTK_RESPONSE_CLOSE*  = -7;
		GTK_RESPONSE_YES*    = -8;
		GTK_RESPONSE_NO*     = -9;
		GTK_RESPONSE_APPLY  = -10;
		GTK_RESPONSE_HELP   = -11;
		*)
		
		CASE res OF
			| -5: res := mbOk
			| -6: res := mbCancel
			| -7: res := mbClose
			| -8: res := mbYes
			| -9: res := mbNo
		ELSE
			res := mbClose
		END;
		RETURN res
	END MessageBox;

	PROCEDURE Beep*;
	BEGIN
		Gdk.gdk_display_beep(Gdk.gdk_display_get_default())
	END Beep;

	PROCEDURE Init;
		VAR result: INTEGER; environment: Fenv.Environment;
	BEGIN
		GU.SaveManagedFpu;
		result := Fenv.feholdexcept(environment);
		result := Gtk.gtk_init_check();
		IF result = 0 THEN Msg("LinGui: GTK4 initialization failed") END;
		result := Fenv.fesetenv(environment);
		HostFiles.MessageBox := NIL;
		HostKernel.SetMsgBoxProc(MessageBox); HostKernel.InstallSignals
	END Init;

BEGIN
	SetErr(DefErrString, DefErrLn); (* set debug out *)
	Init
END LinGui.