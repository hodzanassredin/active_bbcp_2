MODULE LinGtk4Print ["libgtk-4.so.1"];

	IMPORT SYSTEM, GLib := LinGtk2GLib;

	TYPE Range* = RECORD first*, last*: INTEGER END;
		Ranges* = POINTER TO ARRAY [untagged] OF Range;

	PROCEDURE [ccall16] gtk_print_settings_new* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_settings_copy* (settings: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_settings_get_n_copies* (settings: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_print_settings_get_print_pages* (settings: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_print_settings_set_print_pages* (settings: SYSTEM.ADDRESS; pages: INTEGER);

	PROCEDURE [ccall16] gtk_print_settings_get_page_ranges* (settings: SYSTEM.ADDRESS; OUT count: INTEGER): Ranges;

	PROCEDURE [ccall16] gtk_page_setup_new* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_page_setup_copy* (setup: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_page_setup_get_paper_width* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_paper_height* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_set_orientation* (setup: SYSTEM.ADDRESS; orientation: INTEGER);

	PROCEDURE [ccall16] gtk_print_run_page_setup_dialog* (parent, setup, settings: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_unix_dialog_new* (IN title: ARRAY [untagged] OF SHORTCHAR; parent: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_settings* (dialog, settings: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_print_unix_dialog_get_settings* (dialog: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_page_setup* (dialog, setup: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_print_unix_dialog_get_page_setup* (dialog: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_embed_page_setup* (dialog: SYSTEM.ADDRESS; embed: INTEGER);

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_support_selection* (dialog: SYSTEM.ADDRESS; support: INTEGER);

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_has_selection* (dialog: SYSTEM.ADDRESS; has: INTEGER);

	PROCEDURE [ccall16] gtk_print_unix_dialog_set_manual_capabilities* (dialog: SYSTEM.ADDRESS; capabilities: SET);

	PROCEDURE [ccall16] gtk_print_unix_dialog_get_selected_printer* (dialog: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_job_new* (IN title: ARRAY [untagged] OF SHORTCHAR; printer, settings, setup: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_print_job_set_source_file* (job: SYSTEM.ADDRESS; IN file: ARRAY [untagged] OF SHORTCHAR; OUT error: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_print_job_send* (job, callback, userData, destroy: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_print_unix_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_page_setup_unix_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_window_list_toplevels* (): GLib.GList;

	PROCEDURE [ccall16] gtk_dialog_response* (dialog: SYSTEM.ADDRESS; response: INTEGER);

	PROCEDURE [ccall16] gtk_print_settings_set* (settings: SYSTEM.ADDRESS; IN key, value: ARRAY [untagged] OF SHORTCHAR);

	PROCEDURE [ccall16] gtk_print_settings_set_printer* (settings: SYSTEM.ADDRESS; IN name: ARRAY [untagged] OF SHORTCHAR);

	PROCEDURE [ccall16] gtk_printer_get_name* (printer: SYSTEM.ADDRESS): GLib.PString;

	PROCEDURE [ccall16] gtk_printer_is_virtual* (printer: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_enumerate_printers* (callback, data, destroy: SYSTEM.ADDRESS; wait: INTEGER);

	PROCEDURE [ccall16] gtk_page_setup_unix_dialog_get_page_setup* (dialog: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_page_setup_unix_dialog_set_page_setup* (dialog, setup: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_page_setup_get_left_margin* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_right_margin* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_top_margin* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_bottom_margin* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_page_width* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_get_page_height* (setup: SYSTEM.ADDRESS; unit: INTEGER): REAL;

	PROCEDURE [ccall16] gtk_page_setup_unix_dialog_new* (IN title: ARRAY [untagged] OF SHORTCHAR; parent: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_page_setup_get_orientation* (setup: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_widget_get_visible* (widget: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_page_setup_unix_dialog_set_print_settings* (dialog,settings: SYSTEM.ADDRESS);
END LinGtk4Print.
