MODULE LinPrintPorts;

	IMPORT SYSTEM, Math, Ports, Fonts, HostFonts := LinFonts,
		C := LinCairo, PC := LinCairoPango, P := LinGtk2Pango, O := LinGtk2GObject;

	TYPE
		Port* = POINTER TO RECORD (Ports.Port)
			cr*: C.cairo_t;
			w, h: INTEGER
		END;
		Rider* = POINTER TO RECORD (Ports.Rider)
			port: Port;
			l*, t*, r*, b*: INTEGER
		END;

	PROCEDURE (p: Port) SetSize* (w, h: INTEGER);
	BEGIN p.w := w; p.h := h END SetSize;

	PROCEDURE (p: Port) GetSize* (OUT w, h: INTEGER);
	BEGIN w := p.w; h := p.h END GetSize;

	PROCEDURE (p: Port) OpenBuffer* (l, t, r, b: INTEGER);
	BEGIN END OpenBuffer;

	PROCEDURE (p: Port) CloseBuffer*;
	BEGIN END CloseBuffer;

	PROCEDURE (p: Port) NewRider* (): Rider;
		VAR rd: Rider;
	BEGIN NEW(rd); rd.port := p; rd.r := p.w; rd.b := p.h; RETURN rd END NewRider;

	PROCEDURE (rd: Rider) Base* (): Ports.Port;
	BEGIN RETURN rd.port END Base;

	PROCEDURE (rd: Rider) Move* (dx, dy: INTEGER);
	BEGIN END Move;

	PROCEDURE (rd: Rider) SetRect* (l, t, r, b: INTEGER);
	BEGIN rd.l := l; rd.t := t; rd.r := r; rd.b := b END SetRect;

	PROCEDURE (rd: Rider) GetRect* (OUT l, t, r, b: INTEGER);
	BEGIN l := rd.l; t := rd.t; r := rd.r; b := rd.b END GetRect;

	PROCEDURE (rd: Rider) Begin* (col: Ports.Color), NEW;
	BEGIN
		C.cairo_save(rd.port.cr);
		C.cairo_rectangle(rd.port.cr, rd.l, rd.t, rd.r-rd.l, rd.b-rd.t);
		C.cairo_clip(rd.port.cr);
		IF col = Ports.defaultColor THEN col := Ports.black END;
		C.cairo_set_source_rgb(rd.port.cr, col MOD 256 / 255, col DIV 256 MOD 256 / 255, col DIV 65536 MOD 256 / 255)
	END Begin;

	PROCEDURE (rd: Rider) End* (), NEW;
	BEGIN C.cairo_restore(rd.port.cr) END End;

	PROCEDURE (rd: Rider) DrawLine* (x0, y0, x1, y1, s: INTEGER; col: Ports.Color);
	BEGIN
		rd.Begin(col); IF s = 0 THEN s := 1 END;
		C.cairo_set_line_width(rd.port.cr, s); C.cairo_set_line_cap(rd.port.cr, C.CAIRO_LINE_CAP_ROUND);
		C.cairo_move_to(rd.port.cr, x0, y0); C.cairo_line_to(rd.port.cr, x1, y1);
		C.cairo_stroke(rd.port.cr); rd.End
	END DrawLine;

	PROCEDURE (rd: Rider) DrawRect* (l, t, r, b, s: INTEGER; col: Ports.Color);
	BEGIN
		rd.Begin(col);
		IF s = Ports.fill THEN
			C.cairo_rectangle(rd.port.cr, l, t, r-l, b-t); C.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			C.cairo_set_line_width(rd.port.cr, s);
			C.cairo_rectangle(rd.port.cr, l+s/2, t+s/2, r-l-s, b-t-s); C.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawRect;

	PROCEDURE (rd: Rider) DrawOval* (l, t, r, b, s: INTEGER; col: Ports.Color);
	BEGIN
		IF (r <= l) OR (b <= t) THEN RETURN END;
		rd.Begin(col); C.cairo_save(rd.port.cr);
		C.cairo_translate(rd.port.cr, (l+r)/2, (t+b)/2);
		C.cairo_scale(rd.port.cr, (r-l)/2, (b-t)/2);
		C.cairo_arc(rd.port.cr, 0, 0, 1, 0, 2*Math.Pi()); C.cairo_restore(rd.port.cr);
		IF s = Ports.fill THEN C.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			C.cairo_set_line_width(rd.port.cr, s); C.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawOval;

	PROCEDURE (rd: Rider) DrawPath* (IN pts: ARRAY OF Ports.Point; n, s: INTEGER; col: Ports.Color; path: INTEGER);
		VAR i: INTEGER;
	BEGIN
		IF n = 0 THEN RETURN END;
		rd.Begin(col); C.cairo_move_to(rd.port.cr, pts[0].x, pts[0].y);
		IF path IN {Ports.openPoly, Ports.closedPoly} THEN
			FOR i := 1 TO n-1 DO C.cairo_line_to(rd.port.cr, pts[i].x, pts[i].y) END
		ELSE
			i := 1;
			WHILE i+2 < n DO
				C.cairo_curve_to(rd.port.cr, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, pts[i+2].x, pts[i+2].y); INC(i, 3)
			END;
			IF (path = Ports.closedBezier) & (i+1 < n) THEN
				C.cairo_curve_to(rd.port.cr, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, pts[0].x, pts[0].y)
			END
		END;
		IF path IN {Ports.closedPoly, Ports.closedBezier} THEN C.cairo_close_path(rd.port.cr) END;
		IF s = Ports.fill THEN C.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			C.cairo_set_line_width(rd.port.cr, s); C.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawPath;

	PROCEDURE (rd: Rider) DrawString* (x, y: INTEGER; col: Ports.Color; IN text: ARRAY OF CHAR; font: Fonts.Font);
		VAR layout: P.PangoLayout;
	BEGIN
		rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
		HostFonts.ShapeString(layout, text, font); C.cairo_move_to(rd.port.cr, x, y);
		PC.pango_cairo_show_layout_line(rd.port.cr, P.layout_get_line(layout, 0));
		O.g_object_unref(layout); rd.End
	END DrawString;

	PROCEDURE (rd: Rider) DrawSString* (x, y: INTEGER; col: Ports.Color; IN text: ARRAY OF SHORTCHAR; font: Fonts.Font);
		VAR layout: P.PangoLayout;
	BEGIN
		rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
		HostFonts.ShapeSString(layout, text, font); C.cairo_move_to(rd.port.cr, x, y);
		PC.pango_cairo_show_layout_line(rd.port.cr, P.layout_get_line(layout, 0));
		O.g_object_unref(layout); rd.End
	END DrawSString;

	PROCEDURE (rd: Rider) DrawSpace* (x, y, w: INTEGER; col: Ports.Color; font: Fonts.Font);
		VAR layout: P.PangoLayout; width,height,i: INTEGER; text: POINTER TO ARRAY OF CHAR;
	BEGIN
		IF (w > 0) & (font.style * {Fonts.underline, Fonts.strikeout} # {}) THEN
			rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
			HostFonts.ShapeString(layout, " ", font);
			P.layout_get_pixel_size(layout, width, height);
			IF width > 0 THEN
				(* Keep the font transform identical to DrawString, including PDF metrics. *)
				NEW(text, w DIV width + 3);
				FOR i := 0 TO LEN(text^)-2 DO text[i] := " " END; text[LEN(text^)-1] := 0X;
				HostFonts.ShapeString(layout, text^, font);
				C.cairo_rectangle(rd.port.cr, x, rd.t, w, rd.b-rd.t); C.cairo_clip(rd.port.cr);
				C.cairo_move_to(rd.port.cr, x, y);
				PC.pango_cairo_show_layout_line(rd.port.cr, P.layout_get_line(layout, 0))
			END;
			O.g_object_unref(layout); rd.End
		END
	END DrawSpace;

	PROCEDURE (rd: Rider) CharPos* (x, index: INTEGER; IN text: ARRAY OF CHAR; font: Fonts.Font): INTEGER;
		VAR prefix: POINTER TO ARRAY OF CHAR; i: INTEGER;
	BEGIN
		NEW(prefix, index+1); FOR i := 0 TO index-1 DO prefix[i] := text[i] END;
		RETURN x+font.StringWidth(prefix^$) DIV rd.port.unit
	END CharPos;

	PROCEDURE (rd: Rider) CharIndex* (x, pos: INTEGER; IN text: ARRAY OF CHAR; font: Fonts.Font): INTEGER;
		VAR i, n: INTEGER;
	BEGIN
		i := 0; n := LEN(text$);
		WHILE (i < n) & (rd.CharPos(x, i+1, text, font) <= pos) DO INC(i) END;
		RETURN i
	END CharIndex;

	PROCEDURE (rd: Rider) SCharPos* (x, index: INTEGER; IN text: ARRAY OF SHORTCHAR; font: Fonts.Font): INTEGER;
		VAR wide: POINTER TO ARRAY OF CHAR; i: INTEGER;
	BEGIN
		NEW(wide, LEN(text)); FOR i := 0 TO LEN(text$)-1 DO wide[i] := text[i] END;
		RETURN rd.CharPos(x, index, wide^$, font)
	END SCharPos;

	PROCEDURE (rd: Rider) SCharIndex* (x, pos: INTEGER; IN text: ARRAY OF SHORTCHAR; font: Fonts.Font): INTEGER;
		VAR wide: POINTER TO ARRAY OF CHAR; i: INTEGER;
	BEGIN
		NEW(wide, LEN(text)); FOR i := 0 TO LEN(text$)-1 DO wide[i] := text[i] END;
		RETURN rd.CharIndex(x, pos, wide^$, font)
	END SCharIndex;

	PROCEDURE (rd: Rider) MarkRect* (l, t, r, b, s, mode: INTEGER; show: BOOLEAN);
	BEGIN END MarkRect;

	PROCEDURE (rd: Rider) SaveRect* (l, t, r, b: INTEGER; VAR res: INTEGER);
	BEGIN res := 1 END SaveRect;

	PROCEDURE (rd: Rider) RestoreRect* (l, t, r, b: INTEGER; dispose: BOOLEAN);
	BEGIN END RestoreRect;

	PROCEDURE (rd: Rider) Scroll* (dx, dy: INTEGER);
	BEGIN END Scroll;

	PROCEDURE (rd: Rider) SetCursor* (cursor: INTEGER);
	BEGIN END SetCursor;

	PROCEDURE (rd: Rider) Input* (OUT x, y: INTEGER; OUT modifiers: SET; OUT isDown: BOOLEAN);
	BEGIN x := 0; y := 0; modifiers := {}; isDown := FALSE END Input;

END LinPrintPorts.
