MODULE FbsdRastersPng;
(**
	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, 
		Ports, Utf, Strings, Views, Files, 
		GLib := FbsdGtk2GLib, Gdk := FbsdGtk2Gdk, Pixbufs := FbsdGdkPixbuf, GObject := FbsdGtk2GObject, 
		FbsdPorts, 
		StdRasters, StdRastersPng;

	CONST
		cap = 4;	(** how many scaled pixbufs are cached by Raster for different target units *)
		defDPI = 96;
		
	TYPE
		Pixels = INTEGER;

		Raster = POINTER TO RECORD (StdRasters.Raster)
			pixbuf: Gdk.GdkPixbuf;
			pwid, phei: Pixels;	(** pixel width and height *)
			xppi, yppi: INTEGER;	(** x- and y-pixels-per-inch *)
			scaled: ARRAY cap OF RECORD 
				unit: INTEGER;
				pixbuf: Gdk.GdkPixbuf
			END;	(** .scaled[i].pixbuf is .pixbuf scaled for resolution of scaled[i].unit per pixel *)
			bytes: POINTER TO ARRAY OF BYTE
				(* raw array of raster, preserved in memory until Gdk allows it to be released, cf. FINALIZE *)
		END;
		
		Directory = POINTER TO RECORD (StdRasters.Directory) END;

	PROCEDURE Dispose (r: SYSTEM.PTR);
		VAR i: INTEGER; p: Gdk.GdkPixbuf;
	BEGIN
		WITH r: Raster DO 
			r.bytes := NIL; GObject.g_object_unref(r.pixbuf); r.pixbuf := NIL;
			i := 0; REPEAT 
				IF i < cap THEN p := r.scaled[i].pixbuf;
					IF p # NIL THEN
						GObject.g_object_unref(p); r.scaled[i].pixbuf := NIL
					END
				ELSE p := NIL
				END;
				INC(i)
			UNTIL p = NIL
		END
	END Dispose;
	
	PROCEDURE (r: Raster) FINALIZE;
	BEGIN Dispose(r)
	END FINALIZE;
	
	PROCEDURE ThisDpi (p: Gdk.GdkPixbuf; x: BOOLEAN): INTEGER;
		VAR a: POINTER TO ARRAY [untagged] OF SHORTCHAR; b: ARRAY 256 OF CHAR; 
			dpi, len, res: INTEGER;
	BEGIN
		IF x THEN a := Pixbufs.get_option(p, "x-dpi") 
		ELSE a := Pixbufs.get_option(p, "y-dpi") 
		END;
		dpi := 0;
		IF a # NIL THEN 
			len := 1 + LEN(a$); 
			IF len <= LEN(b) THEN 
				Utf.Utf8ToString(SYSTEM.THISARRAY(SYSTEM.ADR(a[0]), len), b, res);
				IF res = 0 THEN Strings.StringToInt(b, dpi, res); IF res # 0 THEN dpi := 0 END
				END
			END
		END;
		IF dpi = 0 THEN dpi := defDPI END;
		RETURN dpi
	END ThisDpi;

	PROCEDURE Prepare (r: Raster);
		VAR len: INTEGER; _: GLib.GError; stream: Pixbufs.GInputStream;
			bytes: Pixbufs.GBytes;
	BEGIN
		IF r.pixbuf = NIL THEN
			len := r.end - r.beg;
			ASSERT(r.bytes = NIL, 100); NEW(r.bytes, len); 
			r.rd.SetPos(r.beg); r.rd.ReadBytes(r.bytes, 0, len);
			bytes := Pixbufs.g_bytes_new_with_free_func(r.bytes, len, Dispose, r);
			ASSERT(bytes # NIL);
			stream := Pixbufs.g_memory_input_stream_new_from_bytes(bytes);
			ASSERT(stream # NIL);
			r.pixbuf := Pixbufs.new_from_stream(stream, NIL, _);
			(*GObject.g_object_unref(stream);*) Pixbufs.g_bytes_unref(bytes);
			(* research: does the stream and bytes need to be disposed? *)
			IF r.pixbuf # NIL THEN 
				r.pwid := Pixbufs.get_width(r.pixbuf); r.phei := Pixbufs.get_height(r.pixbuf);
				r.xppi := ThisDpi(r.pixbuf, TRUE); r.yppi := ThisDpi(r.pixbuf, FALSE)
			END;
		END;
		ASSERT(r.pixbuf # NIL)
	END Prepare;
	
	PROCEDURE ThisUnitPixbuf (r: Raster; u: INTEGER): Gdk.GdkPixbuf;
		VAR i, portppi, w, h: INTEGER; res: Gdk.GdkPixbuf;
	BEGIN
		portppi := Ports.inch DIV u;
		IF (r.xppi = portppi) & (r.yppi = portppi) THEN res := r.pixbuf
		ELSE
			i := 0; WHILE (i < cap) & (r.scaled[i].pixbuf # NIL) & (r.scaled[i].unit # u) DO INC(i) END;
			IF i < cap THEN res := r.scaled[i].pixbuf END
		END;
		IF res = NIL THEN 
			w := r.pwid * Ports.inch DIV r.xppi DIV u;
			h := r.phei * Ports.inch DIV r.yppi DIV u;
			res := Pixbufs.scale_simple(r.pixbuf, w, h, Pixbufs.GDK_INTERP_BILINEAR);
			ASSERT(res # NIL, 60);
			IF i < cap THEN r.scaled[i].pixbuf := res; r.scaled[i].unit := u END
		END;
		RETURN res
	END ThisUnitPixbuf;
	
	PROCEDURE (r: Raster) Draw (rdr: Ports.Rider; x, y: INTEGER);
		VAR cr: Gdk.t; p: Gdk.GdkPixbuf; rd: FbsdPorts.Rider;
	BEGIN 
		ASSERT(r # NIL, 20);
		IF r.pixbuf = NIL THEN Prepare(r) END;
		rd := rdr(FbsdPorts.Rider);
		cr := Gdk.gdk_cairo_create(rd.port.da.window);
		Gdk.cairo_rectangle(cr, rd.l, rd.t, rd.r - rd.l + 1, rd.b - rd.t + 1);
		Gdk.cairo_clip(cr);
		p := ThisUnitPixbuf(r, rd.Base().unit);
		Gdk.gdk_cairo_set_source_pixbuf(cr, p, x, y);
		Gdk.cairo_paint(cr);
		Gdk.cairo_destroy(cr)
	END Draw;

	PROCEDURE (r: Raster) GetSize (OUT w, h: INTEGER);
	BEGIN
		w := Views.undefined; h := Views.undefined;
		IF r.pixbuf = NIL THEN Prepare(r) END;
		IF r.pixbuf # NIL THEN
			IF r.xppi # 0 THEN w := r.pwid * Ports.inch DIV r.xppi END;
			IF r.yppi # 0 THEN h := r.phei * Ports.inch DIV r.yppi END
		END
	END GetSize;

	PROCEDURE (dir: Directory) New (): Raster;
		VAR png: Raster;
	BEGIN
		NEW(png); RETURN png
	END New;

	PROCEDURE (dir: Directory) HandlesFType (IN ftype: Files.Type): BOOLEAN;
		VAR t: Files.Type;
	BEGIN
		Strings.ToUpper(ftype, t);
		RETURN t = "PNG"
	END HandlesFType;

	PROCEDURE Install*;
		VAR d: Directory; pngLoader: StdRastersPng.ImageLoader;
	BEGIN
		NEW(d);
		NEW(pngLoader);
		StdRasters.AddDirectory(d, "png", pngLoader);
	END Install;

END FbsdRastersPng.