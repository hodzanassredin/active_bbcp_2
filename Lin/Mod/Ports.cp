MODULE LinPorts;
(**
	project	= "BlackBox 2.0"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, A.Shiryaev, A.Dmitriev, I.Denisov, Ketmar Dark"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, IO := LinGtk4IO, GU := LinGtk4Util, GLib := LinGtk2GLib, Math, 
		Gdk := LinGtk4Gdk,
		Gtk := LinGtk4Gtk,
		Pango := LinGtk2Pango,
		Dialog, Ports, Fonts, Cairo := LinCairo,
		HostFonts := LinFonts, GObject := LinGtk2GObject,
		StdWindows, Services, PC := LinCairoPango; 

	CONST
		(** buttons **)
		left* = 16; middle* = 17; right* = 18;
		shift* = 24; ctrl* = 25; opt* = 26; cmd* = 27; alt* = 28;
		focusPat* = 5;
		
		resizeHCursor* = 16; resizeVCursor* = 17; resizeLCursor* = 18; 
		resizeRCursor* = 19; resizeCursor* = 20;
		busyCursor* = 21; stopCursor* = 22;
		moveCursor* = 23; copyCursor* = 24; linkCursor* = 25; pickCursor* = 26;
	
		extend = 1; modify = 2; popup = 3; pick = 4; (* same as Controllers.extend, .modify, .popup, and .pick ! *)

		off=0; on=1;
		
	TYPE
		Pixels = INTEGER;
		Port* = POINTER TO RECORD (Ports.Port)
			w, h: INTEGER;
			da-: Gtk.GtkDrawingArea;
			fixed-: Gtk.GtkFixed;
			cr*: Cairo.cairo_t;
			surface: Cairo.cairo_surface_t;
			painting, invalid: BOOLEAN;
			il,it,ir,ib: INTEGER
		END;
		Rider* = POINTER TO RECORD (Ports.Rider)
			l-, t-, r-, b-: INTEGER;
			dx, dy: INTEGER;
			port-: Port;
			saved: Cairo.cairo_surface_t;
			sl, st, sr, sb: INTEGER
		END;
	VAR
		(* system colors *)
		textCol-, selBackground-, selTextCol-,
		dialogTextCol-, dialogShadowCol-, dialogLightCol-: Ports.Color;
		
		trackingEscape-: BOOLEAN;
		tracking*: BOOLEAN;
		
		mx_, my_: INTEGER;	(* actual mouse coordinates *)
		mb: SET;	(* actual mouse buttons & modifiers *)	
		
		cursors-: ARRAY 32 OF Gdk.GdkCursor;
		
		dim25pat, dim50pat, dim75pat, otherPat: ARRAY [untagged] 8 OF SHORTCHAR;

	
	PROCEDURE (p: Port) OpenBuffer* (l, t, r, b: INTEGER);
	BEGIN END OpenBuffer;

	PROCEDURE (p: Port) CloseBuffer*;
	BEGIN
		IF ~p.painting THEN Gtk.gtk_widget_queue_draw(p.da) END
	END CloseBuffer;

	PROCEDURE (p: Port) SetSize* (w, h: INTEGER);
		VAR surface: Cairo.cairo_surface_t; cr: Cairo.cairo_t;
	BEGIN
		IF (w = p.w) & (h = p.h) & (p.surface # NIL) THEN RETURN END;
		surface := Cairo.cairo_image_surface_create(Cairo.CAIRO_FORMAT_ARGB32, MAX(1,w), MAX(1,h));
		cr := Cairo.cairo_create(surface);
		Cairo.cairo_set_source_rgb(cr, 1, 1, 1); Cairo.cairo_paint(cr);
		IF p.surface # NIL THEN
			Cairo.cairo_set_source_surface(cr, p.surface, 0, 0); Cairo.cairo_paint(cr);
			Cairo.cairo_destroy(p.cr); Cairo.cairo_surface_destroy(p.surface)
		END;
		p.surface := surface; p.cr := cr; p.w := w; p.h := h;
		p.invalid := TRUE; p.il := 0; p.it := 0; p.ir := w; p.ib := h
	END SetSize;

	PROCEDURE (p: Port) GetSize* (OUT w, h: INTEGER);
	BEGIN w := p.w; h := p.h END GetSize;

	PROCEDURE (p: Port) NewRider* (): Rider;
		VAR rd: Rider;
	BEGIN NEW(rd); rd.port := p; RETURN rd END NewRider;

	PROCEDURE (p: Port) SetDA* (da: Gtk.GtkDrawingArea), NEW;
	BEGIN p.da := da END SetDA;

	PROCEDURE (p: Port) SetFW* (fixed: Gtk.GtkFixed), NEW;
	BEGIN p.fixed := fixed END SetFW;

	PROCEDURE (p: Port) Paint* (cr: Cairo.cairo_t), NEW;
	BEGIN
		IF p.surface # NIL THEN
			Cairo.cairo_set_source_surface(cr, p.surface, 0, 0); Cairo.cairo_paint(cr)
		END
	END Paint;

	PROCEDURE (p: Port) Invalidate* (l,t,r,b: INTEGER), NEW;
	BEGIN
		IF p.invalid THEN
			p.il := MIN(p.il,l); p.it := MIN(p.it,t); p.ir := MAX(p.ir,r); p.ib := MAX(p.ib,b)
		ELSE p.il := l; p.it := t; p.ir := r; p.ib := b; p.invalid := TRUE END;
		Gtk.gtk_widget_queue_draw(p.da)
	END Invalidate;

	PROCEDURE (p: Port) TakeInvalid* (OUT l,t,r,b: INTEGER): BOOLEAN, NEW;
		VAR invalid: BOOLEAN;
	BEGIN
		invalid := p.invalid; l := p.il; t := p.it; r := p.ir; b := p.ib;
		p.invalid := FALSE; RETURN invalid
	END TakeInvalid;

	PROCEDURE (p: Port) SetPainting* (painting: BOOLEAN), NEW;
	BEGIN p.painting := painting END SetPainting;

	PROCEDURE (p: Port) FINALIZE-;
	BEGIN
		IF p.cr # NIL THEN Cairo.cairo_destroy(p.cr); p.cr := NIL END;
		IF p.surface # NIL THEN Cairo.cairo_surface_destroy(p.surface); p.surface := NIL END
	END FINALIZE;

	PROCEDURE (rd: Rider) InitPort* (port: Port), NEW;
	BEGIN rd.port := port; rd.dx := 0; rd.dy := 0 END InitPort;

	PROCEDURE (rd: Rider) Move* (dx, dy: INTEGER);
	BEGIN INC(rd.dx, dx); INC(rd.dy, dy) END Move;

	PROCEDURE (rd: Rider) Base* (): Ports.Port;
	BEGIN RETURN rd.port END Base;

	PROCEDURE (rd: Rider) SetRect* (l, t, r, b: INTEGER);
	BEGIN rd.l := l; rd.t := t; rd.r := r; rd.b := b END SetRect;

	PROCEDURE (rd: Rider) GetRect* (OUT l, t, r, b: INTEGER);
	BEGIN l := rd.l; t := rd.t; r := rd.r; b := rd.b END GetRect;
	PROCEDURE (rd: Rider) Begin* (col: Ports.Color), NEW;
	BEGIN
		Cairo.cairo_save(rd.port.cr);
		Cairo.cairo_rectangle(rd.port.cr, rd.l, rd.t, rd.r-rd.l, rd.b-rd.t);
		Cairo.cairo_clip(rd.port.cr);
		IF col = Ports.defaultColor THEN col := Ports.black END;
		Cairo.cairo_set_source_rgb(rd.port.cr, col MOD 256 / 255, col DIV 256 MOD 256 / 255, col DIV 65536 MOD 256 / 255)
	END Begin;

	PROCEDURE (rd: Rider) End* (), NEW;
	BEGIN
		Cairo.cairo_restore(rd.port.cr);
		IF ~rd.port.painting THEN Gtk.gtk_widget_queue_draw(rd.port.da) END
	END End;

	PROCEDURE (rd: Rider) DrawLine* (x0, y0, x1, y1, s: INTEGER; col: Ports.Color);
	BEGIN
		rd.Begin(col); IF s = 0 THEN s := 1 END;
		Cairo.cairo_set_line_width(rd.port.cr, s); Cairo.cairo_set_line_cap(rd.port.cr, Cairo.CAIRO_LINE_CAP_ROUND);
		Cairo.cairo_move_to(rd.port.cr, x0, y0); Cairo.cairo_line_to(rd.port.cr, x1, y1);
		Cairo.cairo_stroke(rd.port.cr); rd.End
	END DrawLine;

	PROCEDURE (rd: Rider) DrawRect* (l, t, r, b, s: INTEGER; col: Ports.Color);
	BEGIN
		rd.Begin(col);
		IF s = Ports.fill THEN
			Cairo.cairo_rectangle(rd.port.cr, l, t, r-l, b-t); Cairo.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			Cairo.cairo_set_line_width(rd.port.cr, s);
			Cairo.cairo_rectangle(rd.port.cr, l+s/2, t+s/2, r-l-s, b-t-s); Cairo.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawRect;

	PROCEDURE (rd: Rider) DrawOval* (l, t, r, b, s: INTEGER; col: Ports.Color);
	BEGIN
		IF (r <= l) OR (b <= t) THEN RETURN END;
		rd.Begin(col); Cairo.cairo_save(rd.port.cr);
		Cairo.cairo_translate(rd.port.cr, (l+r)/2, (t+b)/2);
		Cairo.cairo_scale(rd.port.cr, (r-l)/2, (b-t)/2);
		Cairo.cairo_arc(rd.port.cr, 0, 0, 1, 0, 2*Math.Pi()); Cairo.cairo_restore(rd.port.cr);
		IF s = Ports.fill THEN Cairo.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			Cairo.cairo_set_line_width(rd.port.cr, s); Cairo.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawOval;

	PROCEDURE (rd: Rider) DrawPath* (IN pts: ARRAY OF Ports.Point; n, s: INTEGER; col: Ports.Color; path: INTEGER);
		VAR i: INTEGER;
	BEGIN
		IF n = 0 THEN RETURN END;
		rd.Begin(col); Cairo.cairo_move_to(rd.port.cr, pts[0].x, pts[0].y);
		IF path IN {Ports.openPoly, Ports.closedPoly} THEN
			FOR i := 1 TO n-1 DO Cairo.cairo_line_to(rd.port.cr, pts[i].x, pts[i].y) END
		ELSE
			i := 1;
			WHILE i+2 < n DO
				Cairo.cairo_curve_to(rd.port.cr, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, pts[i+2].x, pts[i+2].y); INC(i, 3)
			END;
			IF (path = Ports.closedBezier) & (i+1 < n) THEN
				Cairo.cairo_curve_to(rd.port.cr, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, pts[0].x, pts[0].y)
			END
		END;
		IF path IN {Ports.closedPoly, Ports.closedBezier} THEN Cairo.cairo_close_path(rd.port.cr) END;
		IF s = Ports.fill THEN Cairo.cairo_fill(rd.port.cr)
		ELSE
			IF s = 0 THEN s := 1 END;
			Cairo.cairo_set_line_width(rd.port.cr, s); Cairo.cairo_stroke(rd.port.cr)
		END;
		rd.End
	END DrawPath;

	PROCEDURE (rd: Rider) DrawString* (x, y: INTEGER; col: Ports.Color; IN text: ARRAY OF CHAR; font: Fonts.Font);
		VAR layout: Pango.PangoLayout;
	BEGIN
		rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
		HostFonts.ShapeString(layout, text, font); Cairo.cairo_move_to(rd.port.cr, x, y);
		PC.pango_cairo_show_layout_line(rd.port.cr, Pango.layout_get_line(layout, 0));
		GObject.g_object_unref(layout); rd.End
	END DrawString;

	PROCEDURE (rd: Rider) DrawSString* (x, y: INTEGER; col: Ports.Color; IN text: ARRAY OF SHORTCHAR; font: Fonts.Font);
		VAR layout: Pango.PangoLayout;
	BEGIN
		rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
		HostFonts.ShapeSString(layout, text, font); Cairo.cairo_move_to(rd.port.cr, x, y);
		PC.pango_cairo_show_layout_line(rd.port.cr, Pango.layout_get_line(layout, 0));
		GObject.g_object_unref(layout); rd.End
	END DrawSString;

	PROCEDURE (rd: Rider) DrawSpace* (x, y, w: INTEGER; col: Ports.Color; font: Fonts.Font);
		VAR layout: Pango.PangoLayout; width,height,i: INTEGER; text: POINTER TO ARRAY OF CHAR;
	BEGIN
		IF (w > 0) & (font.style * {Fonts.underline, Fonts.strikeout} # {}) THEN
			rd.Begin(col); layout := PC.pango_cairo_create_layout(rd.port.cr);
			HostFonts.ShapeString(layout, " ", font);
			Pango.layout_get_pixel_size(layout, width, height);
			IF width > 0 THEN
				(* Keep the font transform identical to DrawString, including PDF metrics. *)
				NEW(text, w DIV width + 3);
				FOR i := 0 TO LEN(text^)-2 DO text[i] := " " END; text[LEN(text^)-1] := 0X;
				HostFonts.ShapeString(layout, text^, font);
				Cairo.cairo_rectangle(rd.port.cr, x, rd.t, w, rd.b-rd.t); Cairo.cairo_clip(rd.port.cr);
				Cairo.cairo_move_to(rd.port.cr, x, y);
				PC.pango_cairo_show_layout_line(rd.port.cr, Pango.layout_get_line(layout, 0))
			END;
			GObject.g_object_unref(layout); rd.End
		END
	END DrawSpace;

	PROCEDURE (rd: Rider) CharPos* (x, index: INTEGER; IN s: ARRAY OF CHAR; font: Fonts.Font): INTEGER;
	VAR layout:Pango.PangoLayout;
			x_pos: INTEGER;
			
	PROCEDURE ShortLen (IN f: ARRAY OF CHAR; fLen: INTEGER): INTEGER;
		VAR i, j: INTEGER;
	BEGIN
		i := 0; j := 0;
		WHILE i < fLen DO
			CASE ORD(f[i]) OF 00H..7FH: INC(j)
			| 80H..7FFH: INC(j, 2)
			ELSE (* | 800H..0FFFFH *) INC(j, 3)
			END;
			INC(i)
		END;
		RETURN j
	END ShortLen;
	
	BEGIN
		ASSERT(rd.port # NIL, 100); ASSERT(index <= LEN(s), 101);

		layout := HostFonts.layout;
		HostFonts.ShapeString(layout, s, font);

		Pango.layout_line_index_to_x(
			Pango.layout_get_line(layout,0), ShortLen(s,index), 0, x_pos);
		RETURN x + x_pos DIV Pango.SCALE;
	END CharPos;

	PROCEDURE (rd: Rider) SCharPos* (x, index: INTEGER; IN s: ARRAY OF SHORTCHAR; font: Fonts.Font): INTEGER;
	VAR layout:Pango.PangoLayout;
			x_pos: INTEGER;
	BEGIN
		ASSERT(rd.port # NIL, 100); ASSERT(index <= LEN(s), 101);

		layout := HostFonts.layout;
		HostFonts.ShapeSString(layout, s, font);

		Pango.layout_line_index_to_x(
			Pango.layout_get_line(layout,0),index,0,x_pos);
		RETURN x + x_pos DIV Pango.SCALE;
	END SCharPos;

	PROCEDURE (rd: Rider) CharIndex* (x, pos: INTEGER; IN s: ARRAY OF CHAR; font: Fonts.Font): INTEGER;
	VAR layout:Pango.PangoLayout;
			x_pos, shortIndex, index, trailing: INTEGER; out_: BOOLEAN;
	
	BEGIN
		ASSERT(rd.port # NIL, 100);

		layout := HostFonts.layout;
		HostFonts.ShapeString(layout, s, font);
		x_pos:=(pos-x) * Pango.SCALE;
		out_ := Pango.layout_line_x_to_index(
			Pango.layout_get_line(layout,0), x_pos, shortIndex, trailing);
		
		(* utf8 *)
		index := 0;
		WHILE shortIndex > 0 DO
			CASE ORD(s[index]) OF 00H..7FH: DEC(shortIndex)
			| 80H..7FFH: DEC(shortIndex, 2)
			ELSE (* | 800H..0FFFFH *) DEC(shortIndex, 3)
			END;
			INC(index)
		END;
		
		RETURN index
	END CharIndex;

	PROCEDURE (rd: Rider) SCharIndex* (x, pos: INTEGER; IN s: ARRAY OF SHORTCHAR; font: Fonts.Font): INTEGER;
	VAR layout:Pango.PangoLayout;  
			x_pos, index,trailing: INTEGER; out_: BOOLEAN;
	BEGIN
		ASSERT(rd.port # NIL, 100);

		layout := HostFonts.layout;
		HostFonts.ShapeSString(layout, s, font);
		x_pos := (pos - x) * Pango.SCALE; 
		out_ := Pango.layout_line_x_to_index(
			Pango.layout_get_line(layout,0),x_pos ,index,trailing);
		RETURN index
	END SCharIndex;

	PROCEDURE SetMouseState* (x, y: INTEGER; but: SET; isDown_: BOOLEAN);
	BEGIN
		mx_ := x; my_ := y; mb := but;
		IF isDown_ THEN trackingEscape := FALSE END
	END SetMouseState;
	
	PROCEDURE (rd: Rider) MarkRect* (l, t, r, b, s, mode: INTEGER; show: BOOLEAN);
		VAR x, y, stride, value, bits, xl, yt, xr, yb: INTEGER; data, adr: SYSTEM.ADDRESS; mark: BOOLEAN;
	BEGIN
		IF rd.port.surface = NIL THEN RETURN END;
		xl := MAX(0, MAX(l,rd.l)); yt := MAX(0, MAX(t,rd.t));
		xr := MIN(rd.port.w, MIN(r,rd.r)); yb := MIN(rd.port.h, MIN(b,rd.b));
		IF (s >= 0) & (MAX(1,s)*2 > MIN(r-l,b-t)) THEN s := Ports.fill END;
		Cairo.cairo_surface_flush(rd.port.surface);
		data := Cairo.cairo_image_surface_get_data(rd.port.surface);
		stride := Cairo.cairo_image_surface_get_stride(rd.port.surface);
		FOR y := yt TO yb-1 DO
			FOR x := xl TO xr-1 DO
				mark := (s <= Ports.fill) OR (x < l+MAX(1,s)) OR (x >= r-MAX(1,s)) OR (y < t+MAX(1,s)) OR (y >= b-MAX(1,s));
				IF (mode # Ports.invert) & (mode # Ports.hilite) THEN
					CASE mode OF
					| Ports.dim25: bits := ORD(dim25pat[(y+rd.dy) MOD 8])
					| Ports.dim50: bits := ORD(dim50pat[(y+rd.dy) MOD 8])
					| Ports.dim75: bits := ORD(dim75pat[(y+rd.dy) MOD 8])
					ELSE bits := ORD(otherPat[(y+rd.dy) MOD 8]) END;
					mark := mark & ~ODD(ASH(bits,-((x+rd.dx) MOD 8)))
				END;
				IF mark THEN
					adr := data+y*stride+x*4; SYSTEM.GET(adr,value);
					value := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET,value) / {0..23}); SYSTEM.PUT(adr,value)
				END
			END
		END;
		Cairo.cairo_surface_mark_dirty(rd.port.surface);
		IF ~rd.port.painting THEN Gtk.gtk_widget_queue_draw(rd.port.da) END
	END MarkRect;

	PROCEDURE (rd: Rider) SaveRect* (l, t, r, b: INTEGER; VAR res: INTEGER);
		VAR cr: Cairo.cairo_t;
	BEGIN
		res := 1; l := MAX(0,l); t := MAX(0,t); r := MIN(rd.port.w,r); b := MIN(rd.port.h,b);
		IF (r <= l) OR (b <= t) THEN RETURN END;
		IF rd.saved # NIL THEN Cairo.cairo_surface_destroy(rd.saved) END;
		rd.saved := Cairo.cairo_image_surface_create(Cairo.CAIRO_FORMAT_ARGB32,r-l,b-t);
		cr := Cairo.cairo_create(rd.saved);
		Cairo.cairo_set_source_surface(cr,rd.port.surface,-l,-t); Cairo.cairo_paint(cr); Cairo.cairo_destroy(cr);
		rd.sl := l; rd.st := t; rd.sr := r; rd.sb := b;
		IF Cairo.cairo_surface_status(rd.saved) = 0 THEN res := 0 END
	END SaveRect;

	PROCEDURE (rd: Rider) RestoreRect* (l, t, r, b: INTEGER; dispose: BOOLEAN);
	BEGIN
		IF rd.saved # NIL THEN
			l := MAX(l,rd.sl); t := MAX(t,rd.st); r := MIN(r,rd.sr); b := MIN(b,rd.sb);
			rd.Begin(Ports.black);
			Cairo.cairo_rectangle(rd.port.cr,l,t,r-l,b-t); Cairo.cairo_clip(rd.port.cr);
			Cairo.cairo_set_source_surface(rd.port.cr,rd.saved,rd.sl,rd.st); Cairo.cairo_paint(rd.port.cr); rd.End;
			IF dispose THEN Cairo.cairo_surface_destroy(rd.saved); rd.saved := NIL END
		END
	END RestoreRect;

	PROCEDURE (rd: Rider) FINALIZE-;
	BEGIN IF rd.saved # NIL THEN Cairo.cairo_surface_destroy(rd.saved); rd.saved := NIL END END FINALIZE;

	PROCEDURE (rd: Rider) Scroll* (dx, dy: INTEGER);
		VAR saved: Cairo.cairo_surface_t; cr: Cairo.cairo_t;
	BEGIN
		saved := Cairo.cairo_image_surface_create(Cairo.CAIRO_FORMAT_ARGB32,rd.port.w,rd.port.h);
		cr := Cairo.cairo_create(saved); Cairo.cairo_set_source_surface(cr,rd.port.surface,0,0); Cairo.cairo_paint(cr); Cairo.cairo_destroy(cr);
		rd.Begin(Ports.black);
		Cairo.cairo_rectangle(rd.port.cr,MAX(rd.l,rd.l+dx),MAX(rd.t,rd.t+dy),MAX(0,rd.r-rd.l-ABS(dx)),MAX(0,rd.b-rd.t-ABS(dy)));
		Cairo.cairo_clip(rd.port.cr); Cairo.cairo_set_source_surface(rd.port.cr,saved,dx,dy); Cairo.cairo_paint(rd.port.cr); rd.End;
		Cairo.cairo_surface_destroy(saved);
		IF dy < 0 THEN rd.port.Invalidate(rd.l,MAX(rd.t,rd.b+dy),rd.r,rd.b)
		ELSIF dy > 0 THEN rd.port.Invalidate(rd.l,rd.t,rd.r,MIN(rd.b,rd.t+dy)) END;
		IF dx < 0 THEN rd.port.Invalidate(MAX(rd.l,rd.r+dx),rd.t,rd.r,rd.b)
		ELSIF dx > 0 THEN rd.port.Invalidate(rd.l,rd.t,MIN(rd.r,rd.l+dx),rd.b) END;
		INC(rd.dx,dx); INC(rd.dy,dy)
	END Scroll;

	PROCEDURE (rd: Rider) SetCursor* (cursor: INTEGER);
	BEGIN
		Gtk.gtk_widget_set_cursor(rd.port.da,cursors[cursor]);
	END SetCursor;

	PROCEDURE Surface* (widget: Gtk.GtkWidget): Gdk.GdkWindow;
		VAR native: Gtk.GtkWidget;
	BEGIN
		native := Gtk.gtk_widget_get_native(widget);
		IF native = NIL THEN RETURN NIL END;
		RETURN Gtk.gtk_native_get_surface(native)
	END Surface;

	PROCEDURE PollPointer* (widget: Gtk.GtkWidget; OUT x,y: INTEGER; OUT state: SET);
		VAR surface: Gdk.GdkWindow; device: SYSTEM.ADDRESS; rx,ry: REAL; res: INTEGER;
	BEGIN
		x := 0; y := 0; rx := 0; ry := 0; state := {}; surface := Surface(widget);
		IF surface # NIL THEN
			device := Gdk.gdk_seat_get_pointer(Gdk.gdk_display_get_default_seat(Gdk.gdk_surface_get_display(surface)));
			IF device # 0 THEN
				res := Gdk.gdk_surface_get_device_position(surface,device,rx,ry,state);
				IF res # 0 THEN x := SHORT(ENTIER(rx)); y := SHORT(ENTIER(ry)) END
			END
		END
	END PollPointer;

	PROCEDURE CancelTracking*;
	BEGIN trackingEscape := TRUE END CancelTracking;

	PROCEDURE (rd: Rider) Input* (OUT x,y: INTEGER; OUT modifiers: SET; OUT isDown: BOOLEAN);
		VAR state: SET; res,i: INTEGER;
	BEGIN
		(* Event callbacks suppress recursive tracking while this nested iteration runs. *)
		res := GU.Iterate(0);
		PollPointer(rd.port.da,x,y,state); mb := {};
		IF Gdk.GDK_SHIFT_BIT IN state THEN mb := mb+{shift,extend} END;
		IF Gdk.GDK_CONTROL_BIT IN state THEN mb := mb+{ctrl,modify} END;
		IF Gdk.GDK_MOD1_BIT IN state THEN INCL(mb,alt) END;
		IF Gdk.GDK_BUTTON1_BIT IN state THEN INCL(mb,left) END;
		IF Gdk.GDK_BUTTON2_BIT IN state THEN mb := mb+{middle,pick} END;
		IF Gdk.GDK_BUTTON3_BIT IN state THEN mb := mb+{right,popup} END;
		modifiers := mb; isDown := mb*{left,middle,right} # {};
		Services.actionHook.StepInsistent;
		GLib.g_usleep(1000)
	END Input;

	PROCEDURE PointerInPort* (p: Ports.Port): BOOLEAN;
		VAR device: SYSTEM.ADDRESS; surface,under: Gdk.GdkWindow; x,y: REAL;
	BEGIN
		WITH p: Port DO
			surface := Surface(p.da);
			IF surface # NIL THEN
				device := Gdk.gdk_seat_get_pointer(Gdk.gdk_display_get_default_seat(Gdk.gdk_surface_get_display(surface)));
				IF device # 0 THEN
					under := Gdk.gdk_device_get_surface_at_position(device,x,y);
					RETURN under = surface
				END
			END
		ELSE END;
		RETURN FALSE
	END PointerInPort;

	PROCEDURE ResetColors* (basewidget: Gtk.GtkWidget);
		VAR color: Gtk.RGBA;
	BEGIN
		textCol := Ports.black; selBackground := Ports.black; selTextCol := Ports.white;
		color.red := 0.94; color.green := 0.94; color.blue := 0.94; color.alpha := 1;
		IF color.alpha > 0 THEN
			Ports.dialogBackground := Ports.RGBColor(SHORT(ENTIER(color.red*255)),SHORT(ENTIER(color.green*255)),SHORT(ENTIER(color.blue*255)))
		ELSE Ports.dialogBackground := Ports.RGBColor(240,240,240) END;
		dialogTextCol := Ports.black; dialogShadowCol := Ports.grey75; dialogLightCol := Ports.grey50
	END ResetColors;

	PROCEDURE PortOrigin (p: Ports.Port; OUT xRoot, yRoot: Pixels): BOOLEAN;
		VAR res: BOOLEAN;
	BEGIN
		res := FALSE;
		WITH p: Port DO 
			IF p.da # NIL THEN
				res := GU.GetOrigin(p.da,xRoot,yRoot)
			END
		ELSE
		END;
		RETURN res
	END PortOrigin;
		
	PROCEDURE Init;
		VAR i: INTEGER; wnd: Gtk.GtkWidget;
	BEGIN
		wnd := Gtk.gtk_window_new();
		
		ResetColors(wnd);
		Gtk.gtk_window_destroy(wnd);
		cursors[Ports.arrowCursor] := Gdk.gdk_cursor_new_from_name("default",NIL);
		cursors[Ports.textCursor] := Gdk.gdk_cursor_new_from_name("text",NIL);
		cursors[Ports.graphicsCursor] := Gdk.gdk_cursor_new_from_name("crosshair",NIL);
		cursors[Ports.bitmapCursor] := Gdk.gdk_cursor_new_from_name("crosshair",NIL);
		cursors[Ports.tableCursor] := Gdk.gdk_cursor_new_from_name("crosshair",NIL);
		cursors[Ports.refCursor] := Gdk.gdk_cursor_new_from_name("pointer",NIL);
		cursors[resizeHCursor] := Gdk.gdk_cursor_new_from_name("ew-resize",NIL);
		cursors[resizeVCursor] := Gdk.gdk_cursor_new_from_name("ns-resize",NIL);
		cursors[resizeLCursor] := Gdk.gdk_cursor_new_from_name("nw-resize",NIL);
		cursors[resizeRCursor] := Gdk.gdk_cursor_new_from_name("ne-resize",NIL);
		cursors[resizeCursor] := Gdk.gdk_cursor_new_from_name("move",NIL);
		cursors[busyCursor] := Gdk.gdk_cursor_new_from_name("wait",NIL);
		cursors[stopCursor] := Gdk.gdk_cursor_new_from_name("not-allowed",NIL);
		(* TODO: On windows these are bitmaps stored as resources... *)
		cursors[moveCursor] := Gdk.gdk_cursor_new_from_name("move",NIL);
		cursors[copyCursor] := Gdk.gdk_cursor_new_from_name("move",NIL);
		cursors[linkCursor] := Gdk.gdk_cursor_new_from_name("move",NIL);
		cursors[pickCursor] := Gdk.gdk_cursor_new_from_name("move",NIL);
		i := 0;
		WHILE i < LEN(cursors) DO
			IF cursors[i] = NIL THEN cursors[i] := cursors[Ports.arrowCursor] END;
			INC(i)
		END;
		StdWindows.PortOrigin := PortOrigin;
		
		dim25pat[0] := 077X; dim25pat[1] := 0DDX; 
		dim25pat[2] := 0BBX; dim25pat[3] := 0EEX; 
		dim25pat[4] := 077X; dim25pat[5] := 0DDX; 
		dim25pat[6] := 0BBX; dim25pat[7] := 0EEX; 
			
		dim50pat[0] := 055X; dim50pat[1] := 0AAX; 
		dim50pat[2] := 055X; dim50pat[3] := 0AAX; 
		dim50pat[4] := 055X; dim50pat[5] := 0AAX;
		dim50pat[6] := 055X; dim50pat[7] := 0AAX; 
		
		dim75pat[0] := 088X; dim75pat[1] := 022X;
		dim75pat[2] := 044X; dim75pat[3] := 011X;
		dim75pat[4] := 088X; dim75pat[5] := 022X;
		dim75pat[6] := 044X; dim75pat[7] := 011X;
		
		otherPat[0] := 077X; otherPat[1] := 0BBX;
		otherPat[2] := 0DDX; otherPat[3] := 0EEX;
		otherPat[4] := 077X; otherPat[5] := 0BBX;
		otherPat[6] := 0DDX; otherPat[7] := 0EEX;
	
	END Init;
	
BEGIN
	Init
END LinPorts.