MODULE LinGtk3Util;

	IMPORT SYSTEM, Gtk := LinGtk3Gtk;

	PROCEDURE gtk_signal_connect* (object: Gtk.GtkWidget; IN name: ARRAY [untagged] OF SHORTCHAR;
		func, data: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	BEGIN RETURN Gtk.g_signal_connect_data(object, name, func, data, 0, 0) END gtk_signal_connect;

	PROCEDURE gtk_signal_connect_after* (object: Gtk.GtkWidget; IN name: ARRAY [untagged] OF SHORTCHAR;
		func, data: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	BEGIN RETURN Gtk.g_signal_connect_data(object, name, func, data, 0, 1) END gtk_signal_connect_after;

END LinGtk3Util.
