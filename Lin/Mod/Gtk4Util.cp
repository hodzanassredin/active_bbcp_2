MODULE LinGtk4Util;

	IMPORT S := SYSTEM, Fenv := LinFenv, X := LinX11, GX := LinGtk4X11, O := LinGtk2GObject, Gtk := LinGtk4Gtk, IO := LinGtk4IO, Gdk := LinGtk4Gdk, G := LinGtk2GLib;

	VAR managed: Fenv.Environment; managedReady: BOOLEAN;

	TYPE Response = RECORD done: BOOLEAN; result: INTEGER END;
		TextResponse = RECORD done: BOOLEAN; text: Gdk.PString END;

	PROCEDURE ^ Iterate* (block: INTEGER): INTEGER;

	PROCEDURE gtk_signal_connect* (object: Gtk.GtkWidget; IN name: ARRAY [untagged] OF SHORTCHAR;
		func,data: S.ADDRESS): S.ADDRESS;
	BEGIN RETURN Gtk.g_signal_connect_data(object,name,func,data,0,0) END gtk_signal_connect;

	PROCEDURE gtk_signal_connect_after* (object: Gtk.GtkWidget; IN name: ARRAY [untagged] OF SHORTCHAR;
		func,data: S.ADDRESS): S.ADDRESS;
	BEGIN RETURN Gtk.g_signal_connect_data(object,name,func,data,0,1) END gtk_signal_connect_after;

	PROCEDURE [ccall] Respond (dialog: Gtk.GtkWidget; result: INTEGER; VAR response: Response);
	BEGIN response.result := result; response.done := TRUE END Respond;

	PROCEDURE RunDialog* (dialog: Gtk.GtkDialog): INTEGER;
		VAR response: Response; handler,list,item: S.ADDRESS; parent,window: Gtk.GtkWindow; ignored,i: INTEGER; environment: Fenv.Environment;
	BEGIN
		ignored := Fenv.feholdexcept(environment);
		response.done := FALSE; response.result := -4;
		parent := NIL; list := Gtk.gtk_window_get_toplevels();
		FOR i := 0 TO IO.g_list_model_get_n_items(list)-1 DO
			item := IO.g_list_model_get_item(list,i); window := S.VAL(Gtk.GtkWindow,item);
			IF (window # dialog) & (Gtk.gtk_widget_get_visible(window) # 0) THEN
				IF parent = NIL THEN parent := window END;
				IF Gtk.gtk_window_is_active(window) # 0 THEN parent := window END
			END;
			O.g_object_unref(S.VAL(O.GObject,item))
		END;
		IF parent # NIL THEN Gtk.gtk_window_set_transient_for(dialog,parent) END;
		handler := gtk_signal_connect(dialog,"response",S.ADR(Respond),S.ADR(response));
		Gtk.gtk_window_set_modal(dialog,1); Gtk.gtk_window_present(dialog);
		WHILE ~response.done DO ignored := Iterate(1) END;
		IO.g_signal_handler_disconnect(S.VAL(S.ADDRESS,dialog),handler);
		ignored := Fenv.fesetenv(environment);
		RETURN response.result
	END RunDialog;

	PROCEDURE [ccall] TextReady (clipboard,result: S.ADDRESS; VAR response: TextResponse);
		VAR error: S.ADDRESS;
	BEGIN
		error := 0; response.text := Gdk.gdk_clipboard_read_text_finish(clipboard,result,error);
		IF error # 0 THEN G.g_error_free(S.VAL(G.GError,error)) END;
		response.done := TRUE
	END TextReady;

	PROCEDURE ReadClipboardText* (): Gdk.PString;
		VAR response: TextResponse; res: INTEGER;
	BEGIN
		response.done := FALSE; response.text := NIL;
		Gdk.gdk_clipboard_read_text_async(Gdk.gdk_display_get_clipboard(Gdk.gdk_display_get_default()),0,S.ADR(TextReady),S.ADR(response));
		WHILE ~response.done DO res := Iterate(1) END;
		RETURN response.text
	END ReadClipboardText;

	PROCEDURE GetOrigin* (widget: Gtk.GtkWidget; OUT x,y: INTEGER): BOOLEAN;
		VAR native: Gtk.GtkWidget; surface: Gdk.GdkWindow; display,window,child: S.ADDRESS; result: INTEGER;
	BEGIN
		x := 0; y := 0; native := Gtk.gtk_widget_get_native(widget);
		IF native = NIL THEN RETURN FALSE END;
		surface := Gtk.gtk_native_get_surface(native); IF surface = NIL THEN RETURN FALSE END;
		IF O.g_type_check_instance_is_a(S.VAL(S.ADDRESS,surface),GX.gdk_x11_surface_get_type()) = 0 THEN RETURN FALSE END;
		display := GX.gdk_x11_display_get_xdisplay(S.VAL(S.ADDRESS,Gdk.gdk_surface_get_display(surface)));
		window := GX.gdk_x11_surface_get_xid(S.VAL(S.ADDRESS,surface));
		result := X.XTranslateCoordinates(display,window,X.XDefaultRootWindow(display),0,0,x,y,child);
		RETURN result # 0
	END GetOrigin;

	PROCEDURE SaveManagedFpu*;
		VAR res: INTEGER;
	BEGIN
		res := Fenv.feholdexcept(managed); res := Fenv.fesetenv(managed); managedReady := TRUE
	END SaveManagedFpu;

	PROCEDURE EnterManaged* (VAR native: Fenv.Environment);
		VAR res: INTEGER;
	BEGIN
		res := Fenv.feholdexcept(native);
		IF managedReady THEN res := Fenv.fesetenv(managed) END
	END EnterManaged;

	PROCEDURE LeaveManaged* (VAR native: Fenv.Environment);
		VAR res: INTEGER;
	BEGIN res := Fenv.fesetenv(native) END LeaveManaged;

	PROCEDURE Iterate* (block: INTEGER): INTEGER;
		VAR environment: Fenv.Environment; res,restore: INTEGER;
	BEGIN
		restore := Fenv.feholdexcept(environment); res := IO.g_main_context_iteration(0,block);
		restore := Fenv.fesetenv(environment); RETURN res
	END Iterate;


END LinGtk4Util.
