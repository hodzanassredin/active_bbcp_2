MODULE LinGtk4Files ["libgtk-4.so.1"];

	IMPORT S := SYSTEM, G := LinGtk2GLib;

	PROCEDURE [ccall16] gdk_file_list_get_type* (): S.ADDRESS;

	PROCEDURE [ccall16] gdk_file_list_get_files* (list: S.ADDRESS): G.GSList;

	PROCEDURE [ccall16] g_value_get_boxed* (value: S.ADDRESS): S.ADDRESS;

END LinGtk4Files.
