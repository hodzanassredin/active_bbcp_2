MODULE LinGtk4IO ["libgio-2.0.so.0"];

	IMPORT S := SYSTEM, G := LinGtk2GLib;

	PROCEDURE [ccall16] g_file_new_for_path* (IN path: ARRAY [untagged] OF SHORTCHAR): S.ADDRESS;

	PROCEDURE [ccall16] g_file_get_path* (file: S.ADDRESS): G.PString;

	PROCEDURE [ccall16] g_list_model_get_n_items* (list: S.ADDRESS): INTEGER;

	PROCEDURE [ccall16] g_list_model_get_item* (list: S.ADDRESS; index: INTEGER): S.ADDRESS;

	PROCEDURE [ccall16] g_main_context_iteration* (context: S.ADDRESS; block: INTEGER): INTEGER;

	PROCEDURE [ccall16] g_main_context_pending* (context: S.ADDRESS): INTEGER;

	PROCEDURE [ccall16] g_signal_handler_disconnect* (object, handler: S.ADDRESS);

	PROCEDURE [ccall16] g_value_get_string* (value: S.ADDRESS): G.PString;

END LinGtk4IO.
