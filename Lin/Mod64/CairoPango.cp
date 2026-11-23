MODULE LinCairoPango ["libpangocairo-1.0.so.0"];

IMPORT SYSTEM, LinGtk2Pango, LinCairo;

PROCEDURE [ccall] pango_cairo_create_layout* (cr: LinCairo.cairo_t): LinGtk2Pango.PangoLayout;

PROCEDURE [ccall] pango_cairo_show_layout* (cr: LinCairo.cairo_t; layout: LinGtk2Pango.PangoLayout);

PROCEDURE [ccall] pango_cairo_show_layout_line* (cr: LinCairo.cairo_t; line: LinGtk2Pango.PangoLayoutLine);

	PROCEDURE [ccall16] pango_cairo_font_map_get_default* (): LinGtk2Pango.PangoFontMap;
END LinCairoPango.