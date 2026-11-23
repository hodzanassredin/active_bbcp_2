MODULE LinGtk4Gtk ["libgtk-4.so.1"];

	IMPORT SYSTEM, GLib := LinGtk2GLib, Gdk := LinGtk4Gdk, Pango := LinGtk2Pango;

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
		RGBA* = RECORD [untagged] red*, green*, blue*, alpha*: SHORTREAL END;

	PROCEDURE [ccall] gtk_drawing_area_new* (): GtkDrawingArea;

	PROCEDURE [ccall] gtk_drawing_area_size* ["gtk_widget_set_size_request"] (darea: GtkDrawingArea; width, height: INTEGER);

	PROCEDURE [ccall] gtk_file_chooser_dialog_new* (title: PString; parent:GtkWindow; action:INTEGER; 
			b1: PString; r1: INTEGER;
			b2: PString; r2: INTEGER;
			(* b3: PString; r3: INTEGER; *)
			terminator: INTEGER
		): GtkFileChooserDialog;
	

	PROCEDURE [ccall] gtk_file_chooser_set_current_name* (fc: GtkDialog; IN name: String);
	

	PROCEDURE [ccall] gtk_fixed_new* (): GtkFixed;

	PROCEDURE [ccall] gtk_fixed_put* (fixed:GtkFixed; widget: GtkWidget; x, y: REAL);

	PROCEDURE [ccall] gtk_message_dialog_new* (parent: GtkWindow; flags: SET; 
		type: GtkMessageType; buttons: GtkButtonsType; IN message_format: String; str: PString): GtkMessageDialog;

	PROCEDURE [ccall] gtk_widget_hide* (widget: GtkWidget);
	

	PROCEDURE [ccall] gtk_widget_ref* ["g_object_ref"] (widget: GtkWidget);

	PROCEDURE [ccall] gtk_widget_set_can_focus* (widget: GtkWidget; can_focus: gboolean);

	PROCEDURE [ccall] gtk_widget_unref* ["g_object_unref"] (widget: GtkWidget);
	

	PROCEDURE [ccall] gtk_window_is_active* (window: GtkWindow): gboolean;

	PROCEDURE [ccall] gtk_window_maximize* (window: GtkWindow);
	

	PROCEDURE [ccall] gtk_window_new* (): GtkWindow;

	PROCEDURE [ccall] gtk_window_present* (window: GtkWindow);
	

	PROCEDURE [ccall] gtk_window_set_focus* (window: GtkWindow; focus: GtkWidget);
	

	PROCEDURE [ccall] gtk_window_set_title* (window: GtkWindow; title: PString);

	PROCEDURE [ccall] gtk_window_set_resizable* (widget: GtkWidget; value: INTEGER);

	PROCEDURE [ccall] gtk_widget_get_allocated_width* (widget: GtkWidget): INTEGER;

	PROCEDURE [ccall] gtk_widget_get_allocated_height* (widget: GtkWidget): INTEGER;

	PROCEDURE [ccall] gtk_widget_get_pango_context* (widget: GtkWidget): Pango.PangoContext;

	PROCEDURE [ccall] gtk_color_chooser_dialog_new* (IN title: String; parent: GtkWindow): GtkDialog;

	PROCEDURE [ccall] gtk_color_chooser_get_rgba* (dialog: GtkDialog; VAR color: RGBA);

	PROCEDURE [ccall] gtk_color_chooser_set_rgba* (dialog: GtkDialog; VAR color: RGBA);

	PROCEDURE [ccall] g_signal_connect_data* (object: GtkWidget; IN name: String; fn, data, destroy: SYSTEM.ADDRESS; flags: INTEGER): SYSTEM.ADDRESS;

PROCEDURE [ccall] gtk_widget_get_style_context* (widget: GtkWidget): SYSTEM.ADDRESS;

	PROCEDURE [ccall] gtk_get_major_version* (): INTEGER;

	PROCEDURE [ccall] gtk_color_chooser_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall] gtk_file_chooser_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall] gtk_widget_size_allocate* (widget: GtkWidget; VAR allocation: GtkAllocation; baseline: INTEGER);

	PROCEDURE [ccall] gtk_widget_get_preferred_size* (widget: GtkWidget; minimum, natural: SYSTEM.ADDRESS);

	PROCEDURE [ccall] gtk_window_unmaximize* (window: GtkWindow);

	PROCEDURE [ccall] gtk_window_set_default_size* (window: GtkWindow; width,height: INTEGER);

	PROCEDURE [ccall16] gtk_init_check* (): INTEGER;

	PROCEDURE [ccall16] gtk_window_destroy* (window: GtkWindow);

	PROCEDURE [ccall16] gtk_window_set_child* (window: GtkWindow; child: GtkWidget);

	PROCEDURE [ccall16] gtk_window_get_child* (window: GtkWindow): GtkWidget;

	PROCEDURE [ccall16] gtk_widget_set_focusable* (widget: GtkWidget; focusable: INTEGER);

	PROCEDURE [ccall16] gtk_widget_queue_draw* (widget: GtkWidget);

	PROCEDURE [ccall16] gtk_widget_get_native* (widget: GtkWidget): GtkWidget;

	PROCEDURE [ccall16] gtk_native_get_surface* (native: GtkWidget): Gdk.GdkWindow;

	PROCEDURE [ccall16] gtk_widget_set_cursor_from_name* (widget: GtkWidget; IN name: String);

	PROCEDURE [ccall16] gtk_widget_add_controller* (widget, controller: GtkWidget);

	PROCEDURE [ccall16] gtk_event_controller_legacy_new* (): GtkWidget;

	PROCEDURE [ccall16] gtk_event_controller_get_current_event_state* (controller: GtkWidget): SET;

	PROCEDURE [ccall16] gtk_event_controller_focus_new* (): GtkWidget;

	PROCEDURE [ccall16] gtk_drawing_area_set_draw_func* (area: GtkWidget; draw, data, destroy: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gtk_font_chooser_dialog_new* (title: PString; parent: GtkWindow): GtkDialog;

	PROCEDURE [ccall16] gtk_font_chooser_set_font* (dialog: GtkDialog; IN font: String);

	PROCEDURE [ccall16] gtk_font_chooser_get_font* (dialog: GtkDialog): PString;

	PROCEDURE [ccall16] gtk_font_chooser_dialog_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_file_chooser_get_file* (dialog: GtkDialog): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_file_chooser_set_file* (dialog: GtkDialog; file: SYSTEM.ADDRESS; error: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gtk_dialog_response* (dialog: GtkDialog; response: INTEGER);

	PROCEDURE [ccall16] gtk_window_get_toplevels* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_window_set_modal* (dialog: GtkWindow; modal: INTEGER);

	PROCEDURE [ccall16] gtk_window_set_transient_for* (dialog, parent: GtkWindow);

	PROCEDURE [ccall16] gtk_drop_target_new* (type: SYSTEM.ADDRESS; actions: SET): GtkWidget;

	PROCEDURE [ccall16] gtk_widget_set_cursor* (widget: GtkWidget; cursor: Gdk.GdkCursor);

	PROCEDURE [ccall16] gtk_widget_get_first_child* (widget: GtkWidget): GtkWidget;

	PROCEDURE [ccall16] gtk_widget_get_next_sibling* (widget: GtkWidget): GtkWidget;

	PROCEDURE [ccall16] gtk_check_button_get_type* (): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gtk_check_button_set_group* (button, group: GtkWidget);

	PROCEDURE [ccall16] gtk_check_button_set_active* (button: GtkWidget; active: INTEGER);
	PROCEDURE [ccall16] gtk_widget_get_visible* (widget: GtkWidget): INTEGER;
END LinGtk4Gtk.
