MODULE WinRasters;
(**
	project	= "BlackBox 2.0, new module"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ketmar Dark, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, WinApi, WinPorts,
		Ports, Files, Strings, Views, StdRasters, StdRastersPng, Log;

	TYPE
		Raster = POINTER TO RECORD (StdRasters.Raster)
			previousPortUnit: INTEGER;
			bmp: WinApi.HBITMAP;
			img: StdRasters.Image;
		END;

		Directory = POINTER TO RECORD (StdRasters.Directory) END;

	PROCEDURE (r: Raster) FINALIZE;
		VAR res_: INTEGER;
	BEGIN
		IF r.bmp # 0 THEN res_ := WinApi.DeleteObject(r.bmp); r.bmp := 0 END
	END FINALIZE;

	PROCEDURE CreateBmp (rr: Raster; dc: WinApi.HDC);
		VAR
			bi: WinApi.BITMAPINFO;
			r, g, b, a: SHORTCHAR;
			sa, da: WinApi.PtrVoid;
			f: INTEGER;
	BEGIN
		
		IF (* (rr.bmp = 0) & *)  (rr.img.width > 0) & (rr.img.height > 0) & (rr.img # NIL) THEN
			
			bi.bmiHeader.biSize := SIZE(WinApi.BITMAPINFOHEADER);
			bi.bmiHeader.biWidth := rr.img.width;
			bi.bmiHeader.biHeight := - rr.img.height;
			bi.bmiHeader.biPlanes := 1;
			bi.bmiHeader.biBitCount := 32;
			bi.bmiHeader.biCompression := WinApi.BI_RGB;
			bi.bmiHeader.biSizeImage := 0;
			bi.bmiHeader.biXPelsPerMeter := 0;
			bi.bmiHeader.biYPelsPerMeter := 0;
			bi.bmiHeader.biClrUsed := 0;
			bi.bmiHeader.biClrImportant := 0;
			rr.bmp := WinApi.CreateDIBSection(dc, bi, WinApi.DIB_RGB_COLORS, da, 0, 0);
			IF rr.bmp # 0 THEN
				sa := SYSTEM.ADR(rr.img.data[0]);
				ASSERT(sa # 0, 100);
				(* RGBA → BGRA; premultiply R, G, B *)
				FOR f := 0 TO rr.img.width * rr.img.height - 1 DO
					SYSTEM.GET(sa, r); INC(sa);
					SYSTEM.GET(sa, g); INC(sa);
					SYSTEM.GET(sa, b); INC(sa);
					SYSTEM.GET(sa, a); INC(sa);
					(* convert to premultiplied alpha, because AlphaBlit wants that *)
					r := SHORT(CHR(MAX(0, MIN(ORD(r) * ORD(a) DIV 255, 255))));
					g := SHORT(CHR(MAX(0, MIN(ORD(g) * ORD(a) DIV 255, 255))));
					b := SHORT(CHR(MAX(0, MIN(ORD(b) * ORD(a) DIV 255, 255))));
					SYSTEM.PUT(da, b); INC(da);
					SYSTEM.PUT(da, g); INC(da);
					SYSTEM.PUT(da, r); INC(da);
					SYSTEM.PUT(da, a); INC(da)
				END
			END
		END;
	END CreateBmp;
	
	PROCEDURE PrepareAndScale (r: Raster; rdr: Ports.Rider);
		VAR newImg: POINTER TO StdRasters.RasterData;
			w, h: INTEGER; port: WinPorts.Port; rd: WinPorts.Rider; 
	BEGIN
		rd := rdr(WinPorts.Rider);
		port := rd.port;
		IF (r.img = NIL) OR (r.previousPortUnit # port.unit) THEN
			r.rd.SetPos(r.beg);
			r.img := StdRasters.Load(r);
			r.previousPortUnit := port.unit;
			ASSERT(r.img # NIL, 100);
			ASSERT(r.img.width > 0, 101);
			ASSERT(r.img.height > 0, 102);
			ASSERT(r.img.dpiX > 0, 103);
			ASSERT(r.img.dpiY > 0, 104);
			ASSERT(r.img.data # NIL, 105);
			
			IF (r.img # NIL) & (r.img.data # NIL) THEN
				w := r.img.width * Ports.inch DIV r.img.dpiX DIV port.unit;
				h := r.img.height * Ports.inch DIV r.img.dpiY DIV port.unit;
				IF (r.img.width # w) OR (r.img.height # h) THEN
					ASSERT(r.img.data # NIL, 112); (* this is happening if uncomment r.img.data := NIL bellow *)
					NEW(newImg, w * h);
					StdRasters.Scale(
						r.img.data, r.img.width, r.img.height, 0, 0, r.img.width, r.img.height, 
						newImg, w, h, 0, 0, w, h, StdRasters.noSwap, StdRasters.bilinear
					);
					ASSERT(newImg # NIL, 114);
					r.img.data := newImg;
					r.img.width := w;
					r.img.height := h;
				END
			END
		END;
		
	END PrepareAndScale;

	PROCEDURE (r: Raster) Draw (rdr: Ports.Rider; x, y: INTEGER);
	CONST
			AC_SRC_OVER = 0;
			AC_SRC_ALPHA = 1;
	VAR
		port: WinPorts.Port; res_, w, h: INTEGER; dc: WinApi.HANDLE;
		rd: WinPorts.Rider; 
		point: WinApi.POINT;
		ddc: WinApi.HDC;
		bf: WinApi.BLENDFUNCTION;
	
	BEGIN 
		ASSERT(r # NIL, 20);
		rd := rdr(WinPorts.Rider);
		ASSERT(rd.port # NIL, 100);
		port := rd.port;
		dc := port.dc;
		IF port.wnd = 0 THEN res_ := WinApi.SaveDC(dc)
		ELSE res_ := WinApi.SelectClipRgn(dc, 0);
		END;
		
		res_ := WinApi.IntersectClipRect(dc, rd.l, rd.t, rd.r, rd.b);
		IF (r.bmp = 0) OR (r.previousPortUnit # port.unit) THEN
			IF (r.img = NIL) OR (r.previousPortUnit # port.unit) THEN
				PrepareAndScale(r, rdr)
			ELSE
				w := r.img.width * Ports.inch DIV r.img.dpiX DIV port.unit;
				h := r.img.height * Ports.inch DIV r.img.dpiY DIV port.unit;
				IF (r.img.width # w) OR (r.img.height # h) THEN
					PrepareAndScale(r, rdr)
				END
			END;
			CreateBmp(r, port.dc);
			IF r.bmp # 0 THEN
				(* r.img.data := NIL; *)
			END
		END;
		
		res_ := WinApi.GetWindowOrgEx(port.dc, point);
		IF r.bmp # 0 THEN
			ddc := WinApi.CreateCompatibleDC(rd.port.dc);
			
			IF WinApi.SelectObject(ddc, r.bmp) = 0 THEN HALT(100) END;
			bf.BlendOp := AC_SRC_OVER;
			bf.BlendFlags := 0;
			bf.SourceConstantAlpha := -1;
			bf.AlphaFormat := AC_SRC_ALPHA;
			
			(*
			w := r.img.width * Ports.inch DIV r.img.dpiX DIV port.unit;
			h := r.img.height * Ports.inch DIV r.img.dpiY DIV port.unit;
			res_ := WinApi.AlphaBlend(rd.port.dc, x, y, w, h, ddc, 0, 0, r.img.width, r.img.height, SYSTEM.VAL(INTEGER, bf));
			*)
			
			res_ := WinApi.AlphaBlend(
				rd.port.dc, x, y, r.img.width, r.img.height,
				ddc, 0, 0, r.img.width, r.img.height, SYSTEM.VAL(INTEGER, bf));
							
			res_ := WinApi.DeleteDC(ddc);
		END;
		res_ := WinApi.SetWindowOrgEx(port.dc, point.x, point.y, NIL);
		IF port.wnd = 0 THEN res_ := WinApi.RestoreDC(dc, -1) END
	END Draw;

	PROCEDURE (r: Raster) GetSize (OUT w, h: INTEGER);
	BEGIN
		IF r.img # NIL THEN
			w := r.img.width * Ports.inch DIV r.img.dpiX;
			h := r.img.height * Ports.inch DIV r.img.dpiY;
		ELSIF r.img = NIL THEN
			r.rd.SetPos(r.beg);
			r.img := StdRasters.Load(r);
			w := r.img.width * Ports.inch DIV r.img.dpiX;
			h := r.img.height * Ports.inch DIV r.img.dpiY;
		ELSE
			w := Views.undefined;
			h := Views.undefined;
		END
	END GetSize;

	PROCEDURE (dir: Directory) New (): Raster;
		VAR png: Raster;
	BEGIN
		NEW(png); png.bmp := 0; RETURN png
	END New;

	PROCEDURE (dir: Directory) HandlesFType (IN ftype: Files.Type): BOOLEAN;
		VAR t: Files.Type;
	BEGIN
		Strings.ToUpper(ftype, t); RETURN t = "PNG"
	END HandlesFType;

	PROCEDURE Install*;
		VAR d: Directory; pngLoader: StdRastersPng.ImageLoader;
	BEGIN
		NEW(d);
		NEW(pngLoader);
		StdRasters.AddDirectory(d, "png", pngLoader);
	END Install;

END WinRasters.
