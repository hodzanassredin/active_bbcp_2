MODULE LinGtk3Gtk ["libgtk-3.so.0"];

	IMPORT SYSTEM, GLib := LinGtk2GLib, Gdk := LinGtk3Gdk, Pango := LinGtk2Pango;

	TYPE
		String = ARRAY [untagged] OF SHORTCHAR;
		PString* = POINTER TO String;
		gboolean = INTEGER;
		gpointer = SYSTEM.ADDRESS;

	CONST
		(* types for calls to gtk_window_new, these are the possible values of GtkWindowType *)
		GTK_WINDOW_TOPLEVEL* =0;
		GTK_WINDOW_POPUP* = 1;

		(* GTK_STATE_TYPE enum *)
		GTK_STATE_NORMAL* = 0;
		GTK_STATE_ACTIVE* = 1;
		GTK_STATE_PRELIGHT* = 2;
		GTK_STATE_SELECTED* = 3;
		GTK_STATE_INSENSITIVE* = 4;

		(* GtkAccelFlags enum *)
		GTK_ACCEL_VISIBLE* = 0; (* should the accelerator appear in the widget's display? *)
		GTK_ACCEL_SIGNAL_VISIBLE* = 1; (* should the signal associated with this accelerator be also visible? *)
		GTK_ACCEL_LOCKED* = 2; (* may the accelerator be removed again? *)
		GTK_ACCEL_MASK* = {0, 1, 2};

		(* Values for GtkWindowPosition *)
		GTK_WIN_POS_NONE* = 0;
		GTK_WIN_POS_CENTER* = 1;
		GTK_WIN_POS_MOUSE* = 2;

		(* Values for GtkSelectionMode *) 
		GTK_SELECTION_SINGLE* = 0;
		GTK_SELECTION_BROWSE* = 1;
		GTK_SELECTION_MULTIPLE* = 2;
		GTK_SELECTION_EXTENDED* = 3;

		(* Values for GtkPolicyType *)
		GTK_POLICY_ALWAYS* = 0;
		GTK_POLICY_AUTOMATIC* = 1;
		GTK_POLICY_NEVER* = 2;

		(* Values for GtkJustification *)
		GTK_JUSTIFY_LEFT* = 0;
		GTK_JUSTIFY_RIGHT* = 1;
		GTK_JUSTIFY_CENTER* = 2;
		GTK_JUSTIFY_FILL* = 3;

		(* Values for GtkRcFlags *)
		GTK_RC_FG* = 0;
		GTK_RC_BG* = 1;
		GTK_RC_TEXT* = 2;
		GTK_RC_BASE* = 3;

		(* Values for GtkShadowType *) 
		GTK_SHADOW_NONE* = 0;
		GTK_SHADOW_IN* = 1;
		GTK_SHADOW_OUT* = 2;
		GTK_SHADOW_ETCHED_IN* = 3;
		GTK_SHADOW_ETCHED_OUT* = 4;

	CONST
		GTK_DIALOG_MODAL* ={0};
		GTK_DIALOG_DESTROY_WITH_PARENT* = {1};
		(* GTK_DIALOG_NO_SEPARATOR = {2} ; *)
	CONST
		GTK_MESSAGE_INFO*=0;
		GTK_MESSAGE_WARNING*=1;
		GTK_MESSAGE_QUESTION*=2;
		GTK_MESSAGE_ERROR*=3;

	CONST
		GTK_BUTTONS_NONE* =0;
		GTK_BUTTONS_OK*=1;
		GTK_BUTTONS_CLOSE*=2;
		GTK_BUTTONS_CANCEL*=3;
		GTK_BUTTONS_YES_NO*=4;
		GTK_BUTTONS_OK_CANCEL*=5;

	CONST
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


	TYPE
		GtkWidget* = POINTER TO LIMITED RECORD [untagged] END;
		GtkObject* = GtkWidget;
		GtkContainer* = GtkWidget;
		GtkWindow* = GtkWidget;
		GtkDialog* = GtkWidget;
		GtkDrawingArea* = GtkWidget;
		GtkFixed* = GtkWidget;
		GtkMessageDialog* = GtkWidget;
		GtkFileChooserDialog* = GtkWidget;
		GtkFontSelectionDialog* = GtkWidget;
		GtkClipboard* = POINTER TO LIMITED RECORD [untagged] END;
		GtkSelectionData* = RECORD [untagged] END;
		GtkMessageType* = INTEGER;
		GtkButtonsType* = INTEGER;
		GtkStateType* = INTEGER;
		GtkWindowType* = INTEGER;
		GtkSignalFunc* = SYSTEM.ADDRESS;
		GtkAllocation* = RECORD [untagged] x*, y*, width*, height*: INTEGER END;
		RGBA* = RECORD [untagged] red*, green*, blue*, alpha*: REAL END;

	PROCEDURE [ccall16] gtk_clipboard_get* (selection: Gdk.GdkAtom): GtkClipboard;
	PROCEDURE [ccall16] gtk_clipboard_set_text* (clipboard: GtkClipboard; text: PString; len: INTEGER);
	PROCEDURE [ccall16] gtk_clipboard_store* (clipboard: GtkClipboard);
	PROCEDURE [ccall16] gtk_container_add* (container: GtkContainer; widget: GtkWidget);
	PROCEDURE [ccall16] gtk_container_remove* (container: GtkContainer; widget: GtkWidget);
	PROCEDURE [ccall16] gtk_dialog_run *(dialog:GtkDialog ):INTEGER;
	PROCEDURE [ccall16] gtk_drag_dest_add_uri_targets* (widget: GtkWidget);
	PROCEDURE [ccall16] gtk_drag_dest_find_target* (widget: GtkWidget; context, targets: SYSTEM.ADDRESS): Gdk.GdkAtom;
	PROCEDURE [ccall16] gtk_drag_dest_set* (widget: GtkWidget; flags: SET; targets: SYSTEM.ADDRESS; count: INTEGER; actions: SET);
	PROCEDURE [ccall16] gtk_drag_finish* (context: SYSTEM.ADDRESS; success, delete, time: INTEGER);
	PROCEDURE [ccall16] gtk_drag_get_data* (widget: GtkWidget; context: SYSTEM.ADDRESS; target: Gdk.GdkAtom; time: INTEGER);
	PROCEDURE [ccall16] gtk_drawing_area_new* (): GtkDrawingArea;
	PROCEDURE [ccall16] gtk_drawing_area_size* ["gtk_widget_set_size_request"] (darea: GtkDrawingArea; width, height: INTEGER);
	PROCEDURE [ccall16] gtk_file_chooser_dialog_new* (title: PString; parent:GtkWindow; action:INTEGER; 
			b1: PString; r1: INTEGER;
			b2: PString; r2: INTEGER;
			(* b3: PString; r3: INTEGER; *)
			terminator: INTEGER
		): GtkFileChooserDialog;
	PROCEDURE [ccall16] gtk_file_chooser_get_filename* (fc: GtkDialog): PString;
	PROCEDURE [ccall16] gtk_file_chooser_set_current_name* (fc: GtkDialog; IN name: String);
	PROCEDURE [ccall16] gtk_file_chooser_set_filename* (fc: GtkDialog; IN filename: String);
	PROCEDURE [ccall16] gtk_fixed_new* (): GtkFixed;
	PROCEDURE [ccall16] gtk_fixed_put* (fixed:GtkFixed; widget: GtkWidget; x, y: INTEGER);
	PROCEDURE [ccall16] gtk_font_selection_dialog_get_font_name* (fsd: GtkFontSelectionDialog): PString;
	PROCEDURE [ccall16] gtk_font_selection_dialog_new* (title: PString): GtkFontSelectionDialog;
	PROCEDURE [ccall16] gtk_font_selection_dialog_set_font_name* (fsd: GtkFontSelectionDialog; IN fontname: String): INTEGER;
	PROCEDURE [ccall16] gtk_main_iteration* (): INTEGER;
	PROCEDURE [ccall16] gtk_message_dialog_new* (parent: GtkWindow; flags: SET; 
		type: GtkMessageType; buttons: GtkButtonsType; IN message_format: String; str: PString): GtkMessageDialog;
	PROCEDURE [ccall16] gtk_selection_add_target* (widget: GtkWidget; selection, target: Gdk.GdkAtom; info: INTEGER);
	PROCEDURE [ccall16] gtk_selection_convert* (widget: GtkWidget; selection, target: Gdk.GdkAtom; time: INTEGER): BOOLEAN;
	PROCEDURE [ccall16] gtk_selection_data_get_uris* (VAR selection: GtkSelectionData): GLib.PAString;
	PROCEDURE [ccall16] gtk_selection_data_set* (VAR selection_data: GtkSelectionData; type: Gdk.GdkAtom; format: INTEGER; 
														IN data: String; length: INTEGER);
	PROCEDURE [ccall16] gtk_selection_owner_set* (widget: GtkWidget; selection: Gdk.GdkAtom; time: INTEGER): INTEGER;
	PROCEDURE [ccall16] gtk_widget_destroy* (object: GtkWidget);
	PROCEDURE [ccall16] gtk_widget_get_parent_window* (widget: GtkWidget): Gdk.GdkWindow;
	PROCEDURE [ccall16] gtk_widget_get_root_window* (widget: GtkWidget): Gdk.GdkWindow;
	PROCEDURE [ccall16] gtk_widget_get_window* (widget: GtkWidget): Gdk.GdkWindow;
	PROCEDURE [ccall16] gtk_widget_hide* (widget: GtkWidget);
	PROCEDURE [ccall16] gtk_widget_queue_draw_area* (widget: GtkWidget; x, y, width, height: INTEGER);
	PROCEDURE [ccall16] gtk_widget_ref* ["g_object_ref"] (widget: GtkWidget);
	PROCEDURE [ccall16] gtk_widget_set_can_focus* (widget: GtkWidget; can_focus: gboolean);
	PROCEDURE [ccall16] gtk_widget_set_double_buffered* (widget: GtkWidget; can_focus: gboolean);
	PROCEDURE [ccall16] gtk_widget_set_events* (widget: GtkWidget; events: Gdk.GdkEventMask);
	PROCEDURE [ccall16] gtk_widget_set_has_window* (widget: GtkWidget; has_window: gboolean);
	PROCEDURE [ccall16] gtk_widget_show_all* (widget: GtkWidget);
	PROCEDURE [ccall16] gtk_widget_unref* ["g_object_unref"] (widget: GtkWidget);
	PROCEDURE [ccall16] gtk_window_get_size* (window: GtkWindow; VAR width, height: INTEGER);
	PROCEDURE [ccall16] gtk_window_is_active* (window: GtkWindow): gboolean;
	PROCEDURE [ccall16] gtk_window_maximize* (window: GtkWindow);
	PROCEDURE [ccall16] gtk_window_move* (window: GtkWindow; x, y: INTEGER);
	PROCEDURE [ccall16] gtk_window_new* (type: INTEGER): GtkWindow;
	PROCEDURE [ccall16] gtk_window_present* (window: GtkWindow);
	PROCEDURE [ccall16] gtk_window_resize* (window: GtkWindow; width, height: INTEGER);
	PROCEDURE [ccall16] gtk_window_set_focus* (window: GtkWindow; focus: GtkWidget);
	PROCEDURE [ccall16] gtk_window_set_keep_above* (window: GtkWindow; on: gboolean);
	PROCEDURE [ccall16] gtk_window_set_title* (window: GtkWindow; title: PString);

	PROCEDURE [ccall16] gtk_window_set_resizable* (widget: GtkWidget; value: INTEGER);
	PROCEDURE [ccall16] gtk_widget_get_allocated_width* (widget: GtkWidget): INTEGER;
	PROCEDURE [ccall16] gtk_widget_get_allocated_height* (widget: GtkWidget): INTEGER;
	PROCEDURE [ccall16] gtk_widget_get_pango_context* (widget: GtkWidget): Pango.PangoContext;
	PROCEDURE [ccall16] gtk_selection_data_get_target* (VAR data: GtkSelectionData): Gdk.GdkAtom;
	PROCEDURE [ccall16] gtk_selection_data_get_data_type* (VAR data: GtkSelectionData): Gdk.GdkAtom;
	PROCEDURE [ccall16] gtk_selection_data_get_length* (VAR data: GtkSelectionData): INTEGER;
	PROCEDURE [ccall16] gtk_selection_data_get_data* (VAR data: GtkSelectionData): PString;
	PROCEDURE [ccall16] gtk_color_chooser_dialog_new* (IN title: String; parent: GtkWindow): GtkDialog;
	PROCEDURE [ccall16] gtk_color_chooser_get_rgba* (dialog: GtkDialog; VAR color: RGBA);
	PROCEDURE [ccall16] gtk_color_chooser_set_rgba* (dialog: GtkDialog; VAR color: RGBA);
	PROCEDURE [ccall16] g_signal_connect_data* (object: GtkWidget; IN name: String; fn, data, destroy: SYSTEM.ADDRESS; flags: INTEGER): SYSTEM.ADDRESS;

PROCEDURE [ccall16] gtk_widget_get_style_context* (widget: GtkWidget): SYSTEM.ADDRESS;
	PROCEDURE [ccall16] gtk_style_context_get_background_color* (context: SYSTEM.ADDRESS; flags: INTEGER; VAR color: RGBA);

	PROCEDURE [ccall16] gtk_get_major_version* (): INTEGER;

	PROCEDURE [ccall16] gtk_clipboard_wait_for_text* (clipboard: GtkClipboard): PString;
	PROCEDURE [ccall16] gtk_main_iteration_do* (blocking: INTEGER): INTEGER;

	PROCEDURE [ccall16] gtk_color_chooser_dialog_get_type* (): SYSTEM.ADDRESS;
	PROCEDURE [ccall16] gtk_font_selection_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_file_chooser_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_widget_size_allocate* (widget: GtkWidget; VAR allocation: GtkAllocation);
	PROCEDURE [ccall16] gtk_widget_get_preferred_size* (widget: GtkWidget; minimum, natural: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_window_unmaximize* (window: GtkWindow);

	PROCEDURE [ccall16] gtk_bin_get_child* (widget: GtkWidget): GtkWidget;

	PROCEDURE [ccall16] gtk_window_set_default_size* (window: GtkWindow; width,height: INTEGER);

	PROCEDURE [ccall16] g_signal_emit_by_name* (widget: GtkWidget; IN name: String; event: Gdk.GdkEventScroll; VAR handled: INTEGER);

END LinGtk3Gtk.
