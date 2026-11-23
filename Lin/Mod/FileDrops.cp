MODULE LinFileDrops;

	IMPORT SYSTEM, IO := LinGtk4IO, F := LinGtk4Files, Utf, Views, StdApi, Services,
		GLib := LinGtk2GLib, Gtk := LinGtk4Gtk, GtkU := LinGtk4Util;

	TYPE
		OpenAction = RECORD (Services.SafeAction)
			path: ARRAY 4096 OF CHAR; view: Views.View
		END;
		OpenLater = POINTER TO RECORD (Services.Action) path: ARRAY 4096 OF CHAR END;

	PROCEDURE (VAR a: OpenAction) Do;
	BEGIN StdApi.OpenDoc(a.path,a.view) END Do;

	PROCEDURE (a: OpenLater) Do;
		VAR open: OpenAction;
	BEGIN open.path := a.path$; open.view := NIL; Services.Try(open) END Do;

	PROCEDURE [ccall] Drop (target: Gtk.GtkWidget; value: SYSTEM.ADDRESS; x,y: REAL; data: SYSTEM.ADDRESS): INTEGER;
		VAR list: GLib.GSList; file: SYSTEM.ADDRESS; path: GLib.PString; open: OpenAction; later: OpenLater; res,opened: INTEGER;
	BEGIN
		opened := 0; file := F.g_value_get_boxed(value);
		IF file # 0 THEN
			list := F.gdk_file_list_get_files(file);
			WHILE list # NIL DO
				path := IO.g_file_get_path(SYSTEM.VAL(SYSTEM.ADDRESS,list.data));
				IF path # NIL THEN
					IF GLib.g_file_test(path,1) # 0 THEN Utf.Utf8ToString(path$,open.path,res) ELSE res := 1 END;
					GLib.g_free(SYSTEM.VAL(GLib.gpointer,path));
					IF res = 0 THEN
						(* Finish GTK's drop before changing the active BlackBox document. *)
						NEW(later); later.path := open.path$; Services.DoLater(later,Services.now+1); INC(opened)
					END
				END;
				list := list.next
			END
		END;
		IF opened > 0 THEN RETURN 1 ELSE RETURN 0 END
	END Drop;

	PROCEDURE Install* (widget: Gtk.GtkWidget);
		VAR target: Gtk.GtkWidget; id: SYSTEM.ADDRESS;
	BEGIN
		target := Gtk.gtk_drop_target_new(F.gdk_file_list_get_type(),{0});
		id := GtkU.gtk_signal_connect(target,"drop",SYSTEM.ADR(Drop),0);
		Gtk.gtk_widget_add_controller(widget,target)
	END Install;
END LinFileDrops.
