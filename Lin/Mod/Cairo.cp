MODULE LinCairo ["libcairo.so.2"];

IMPORT SYSTEM, LinGtk2Pango;

CONST
	CAIRO_FORMAT_ARGB32* = 0;

	(* cairo_line_cap_t *)
	CAIRO_LINE_CAP_BUTT* = 0;
	CAIRO_LINE_CAP_ROUND* = 1;
	CAIRO_LINE_CAP_SQUARE* = 2;

	(* cairo_line_join_t *)
	CAIRO_LINE_JOIN_MITER* = 0;
	CAIRO_LINE_JOIN_ROUND* = 1;
	CAIRO_LINE_JOIN_BEVEL* = 2;

TYPE
	cairo_t* = POINTER TO LIMITED RECORD [untagged] END;
	cairo_surface_t* = POINTER TO RECORD [untagged] END;

	cairo_format_t* = INTEGER;
	cairo_line_cap_t* = INTEGER;
	cairo_line_join_t* = INTEGER;

PROCEDURE [ccall16] cairo_create* (surface: cairo_surface_t): cairo_t;

PROCEDURE [ccall16] cairo_destroy* (cr: cairo_t);

PROCEDURE [ccall16] cairo_save* (cr: cairo_t);

PROCEDURE [ccall16] cairo_restore* (cr: cairo_t);

PROCEDURE [ccall16] cairo_image_surface_create* (
	format: cairo_format_t;
	width, height: INTEGER
): cairo_surface_t;

PROCEDURE [ccall16] cairo_surface_destroy* (s: cairo_surface_t);

PROCEDURE [ccall16] cairo_translate* (cr: cairo_t; tx, ty: REAL);

PROCEDURE [ccall16] cairo_rotate* (cr: cairo_t; angle: REAL);

PROCEDURE [ccall16] cairo_scale* (cr: cairo_t; sx, sy: REAL);

PROCEDURE [ccall16] cairo_rectangle* (cr: cairo_t; x, y, w, h: REAL);

PROCEDURE [ccall16] cairo_arc* (cr: cairo_t; xc, yc, radius, angle1, angle2: REAL);

PROCEDURE [ccall16] cairo_clip* (cr: cairo_t);

PROCEDURE [ccall16] cairo_set_source_rgb* (cr: cairo_t; r, g, b: REAL);

PROCEDURE [ccall16] cairo_move_to* (cr: cairo_t; x, y: REAL);

PROCEDURE [ccall16] cairo_line_to* (cr: cairo_t; x, y: REAL);

PROCEDURE [ccall16] cairo_close_path* (cr: cairo_t);

PROCEDURE [ccall16] cairo_new_path* (cr: cairo_t);

PROCEDURE [ccall16] cairo_set_line_width* (cr: cairo_t; width: REAL);

PROCEDURE [ccall16] cairo_set_line_cap* (cr: cairo_t; line_cap: cairo_line_cap_t);

PROCEDURE [ccall16] cairo_set_line_join* (cr: cairo_t; line_join: cairo_line_join_t);

PROCEDURE [ccall16] cairo_stroke* (cr: cairo_t);

PROCEDURE [ccall16] cairo_fill* (cr: cairo_t);

PROCEDURE [ccall16] cairo_pdf_surface_create* (IN filename: ARRAY [untagged] OF SHORTCHAR; width, height: REAL): cairo_surface_t;
PROCEDURE [ccall16] cairo_pdf_surface_set_size* (surface: cairo_surface_t; width, height: REAL);
PROCEDURE [ccall16] cairo_surface_finish* (surface: cairo_surface_t);
PROCEDURE [ccall16] cairo_surface_status* (surface: cairo_surface_t): INTEGER;
PROCEDURE [ccall16] cairo_status* (cr: cairo_t): INTEGER;
PROCEDURE [ccall16] cairo_show_page* (cr: cairo_t);
PROCEDURE [ccall16] cairo_curve_to* (cr: cairo_t; x1, y1, x2, y2, x3, y3: REAL);
PROCEDURE [ccall16] cairo_paint* (cr: cairo_t);


PROCEDURE [ccall16] cairo_set_source_surface* (cr: cairo_t; surface: cairo_surface_t; x, y: REAL);
PROCEDURE [ccall16] cairo_surface_flush* (surface: cairo_surface_t);
PROCEDURE [ccall16] cairo_surface_mark_dirty* (surface: cairo_surface_t);
PROCEDURE [ccall16] cairo_image_surface_get_data* (surface: cairo_surface_t): SYSTEM.ADDRESS;
PROCEDURE [ccall16] cairo_image_surface_get_stride* (surface: cairo_surface_t): INTEGER;
PROCEDURE [ccall16] cairo_clip_extents* (cr: cairo_t; VAR x1, y1, x2, y2: REAL);

END LinCairo.
