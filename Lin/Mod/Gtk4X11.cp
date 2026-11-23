MODULE LinGtk4X11 ["libgtk-4.so.1"];

	IMPORT S := SYSTEM;

	PROCEDURE [ccall16] gdk_x11_surface_get_type* (): S.ADDRESS;

	PROCEDURE [ccall16] gdk_x11_surface_get_xid* (surface: S.ADDRESS): S.ADDRESS;

	PROCEDURE [ccall16] gdk_x11_display_get_xdisplay* (display: S.ADDRESS): S.ADDRESS;

END LinGtk4X11.
