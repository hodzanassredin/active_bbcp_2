MODULE LinGtk4Gdk ["libgtk-4.so.1"];

	IMPORT SYSTEM, GObject := LinGtk2GObject, GLib := LinGtk2GLib;

	TYPE
		String = ARRAY [untagged] OF SHORTCHAR;
		PString* = POINTER TO String;
		t* = POINTER TO LIMITED RECORD [untagged] END;
		GdkWindow* = POINTER TO LIMITED RECORD (GObject.GObject) END;
		GdkDisplay* = POINTER TO LIMITED RECORD (GObject.GObject) END;
		GdkPixbuf* = POINTER TO RECORD (GObject.GObject) END;
		GdkCursor* = POINTER TO LIMITED RECORD (GObject.GObject) END;
		NativeKeyvals* = POINTER TO ARRAY [untagged] OF INTEGER;
		GdkRectangle* = RECORD [untagged] x*,y*,width*,height*: INTEGER END;

	CONST
		GDK_SHIFT_BIT* = 0; GDK_CONTROL_BIT* = 2; GDK_MOD1_BIT* = 3; GDK_MOD5_BIT* = 7;
		GDK_BUTTON1_BIT* = 8; GDK_BUTTON2_BIT* = 9; GDK_BUTTON3_BIT* = 10;
		GDK_SCROLL_UP* = 0; GDK_SCROLL_DOWN* = 1; GDK_SCROLL_LEFT* = 2; GDK_SCROLL_RIGHT* = 3;
		GDK_KEY_PRESS* = 4;

	PROCEDURE [ccall16] gdk_display_beep*(display: GdkDisplay);

	PROCEDURE [ccall16] gdk_display_get_default*(): GdkDisplay;

	PROCEDURE [ccall16] gdk_event_get_time* (event: SYSTEM.ADDRESS):  INTEGER;

	PROCEDURE [ccall16] gdk_keyval_to_lower* (keyval: INTEGER): INTEGER;

	PROCEDURE [ccall16] gdk_keyval_to_unicode*(keyval: INTEGER): INTEGER;

	PROCEDURE [ccall16] gdk_cairo_set_source_pixbuf* (cr:t; pixbuf: GdkPixbuf; x, y: REAL);

	PROCEDURE [ccall16] gdk_surface_get_device_position* (surface: GdkWindow; device: SYSTEM.ADDRESS; OUT x,y: REAL; OUT state: SET): INTEGER;

	PROCEDURE [ccall16] gdk_surface_get_display* (surface: GdkWindow): GdkDisplay;

	PROCEDURE [ccall16] gdk_display_get_default_seat* (display: GdkDisplay): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gdk_seat_get_pointer* (seat: SYSTEM.ADDRESS): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gdk_device_get_surface_at_position* (device: SYSTEM.ADDRESS; OUT x,y: REAL): GdkWindow;

	PROCEDURE [ccall16] gdk_display_get_monitors* (display: GdkDisplay): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gdk_monitor_get_geometry* (monitor: SYSTEM.ADDRESS; VAR rect: GdkRectangle);

	PROCEDURE [ccall16] gdk_event_get_event_type* (event: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gdk_event_get_modifier_state* (event: SYSTEM.ADDRESS): SET;

	PROCEDURE [ccall16] gdk_event_get_position* (event: SYSTEM.ADDRESS; OUT x,y: REAL): INTEGER;

	PROCEDURE [ccall16] gdk_button_event_get_button* (event: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gdk_key_event_get_keyval* (event: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gdk_key_event_get_keycode* (event: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gdk_scroll_event_get_deltas* (event: SYSTEM.ADDRESS; OUT x,y: REAL);

	PROCEDURE [ccall16] gdk_display_get_clipboard* (display: GdkDisplay): SYSTEM.ADDRESS;

	PROCEDURE [ccall16] gdk_clipboard_set_text* (clipboard: SYSTEM.ADDRESS; IN text: String);

	PROCEDURE [ccall16] gdk_clipboard_is_local* (clipboard: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ccall16] gdk_clipboard_read_text_async* (clipboard, cancellable, callback, data: SYSTEM.ADDRESS);

	PROCEDURE [ccall16] gdk_clipboard_read_text_finish* (clipboard, result: SYSTEM.ADDRESS; OUT error: SYSTEM.ADDRESS): PString;

	PROCEDURE [ccall16] gdk_cursor_new_from_name* (IN name: String; fallback: GdkCursor): GdkCursor;

	PROCEDURE [ccall16] gdk_scroll_event_get_direction* (event: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE [ccall16] gdk_display_map_keycode* (display: GdkDisplay; keycode: INTEGER; OUT keys: SYSTEM.ADDRESS; OUT values: NativeKeyvals; OUT count: INTEGER): INTEGER;
END LinGtk4Gdk.
