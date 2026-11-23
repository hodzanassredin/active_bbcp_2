MODULE StdPictures;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Kernel, Dialog,
		Files, Fonts, Ports, Stores, Models, Views, Controllers, Properties;

	CONST
		macPict = 0; winPict = 1;	(* types *)
		minVersion = 0; maxVersion = 0;

	TYPE
		Model = POINTER TO RECORD
			file: Files.File;
			pos, len: INTEGER;
			ref: INTEGER; (* some pointer to resource like WinApi.HANDLE in WinPictures *)
			data: POINTER TO ARRAY OF BYTE
		END;

		StdView = POINTER TO RECORD (Views.View)
			model: Model;
			unit: INTEGER;
			w, h: INTEGER;
			mode: SHORTCHAR;
			type: BYTE
		END;

		POINT = Ports.Point;

		HANDLE = INTEGER;

		FillInfo = RECORD
			pat: ARRAY 8 OF SHORTINT;
			color, brushColor: Ports.Color;
			patFill, patEmpty: BOOLEAN; (* pattern type, for fill optimisations *)
			width: INTEGER (* for pens; <=0 means "no pen" *)
		END;
		
		ExternalDrawer* = POINTER TO ABSTRACT RECORD END;

	VAR
		drawBackRect*: BOOLEAN;
		doDump: BOOLEAN;
		
		externalDrawer: ExternalDrawer;


	PROCEDURE (ex: ExternalDrawer) Prepare* (len: INTEGER; data: POINTER TO ARRAY OF BYTE): INTEGER, NEW, ABSTRACT;
	
	PROCEDURE (ex: ExternalDrawer) GetBytes* (ref, len: INTEGER; data: POINTER TO ARRAY OF BYTE), NEW, ABSTRACT;
	
	PROCEDURE (ex: ExternalDrawer) Finalize* (ref: INTEGER), NEW, ABSTRACT;
	
	PROCEDURE (ex: ExternalDrawer) Draw* (f: Views.Frame; ref, mode, x, y, w, h: INTEGER), NEW, ABSTRACT;
	
	
	PROCEDURE SetExternalDrawer* (d: ExternalDrawer);
	BEGIN
		externalDrawer := d
	END SetExternalDrawer;


	(* Mac Pictures *)

	PROCEDURE ReadWord (rd: Files.Reader; VAR x: INTEGER);
	VAR
		b: BYTE;
	BEGIN
		rd.ReadByte(b); x := 256 * (b MOD 256);
		rd.ReadByte(b); x := x + b MOD 256
	END ReadWord;

	PROCEDURE ReadDWord (rd: Files.Reader; VAR x: INTEGER);
	VAR
		w0, w1: INTEGER;
	BEGIN
		ReadWord(rd, w0); ReadWord(rd, w1);
		x := 65536 * w1 + w0
	END ReadDWord;

	PROCEDURE ReadI16 (rd: Files.Reader; VAR x: INTEGER);
	VAR
		b: BYTE;
	BEGIN
		rd.ReadByte(b); x := 256 * (b MOD 256);
		rd.ReadByte(b); x := x + b MOD 256;
		x := (x + 32768) MOD 65536 - 32768
	END ReadI16;

	PROCEDURE ReadByte (rd: Files.Reader; VAR x: INTEGER);
	VAR
		b: BYTE;
	BEGIN
		rd.ReadByte(b); x := b
	END ReadByte;

	PROCEDURE Skip (rd: Files.Reader; n: INTEGER);
	BEGIN
		rd.SetPos(rd.Pos() + n)
	END Skip;

	PROCEDURE DrawMacPicture (v: StdView; f: Views.Frame; picw, pich: INTEGER);
	CONST
		brushPen = 0; brushFill = 1; brushBack = 2;

	VAR
		rd: Files.Reader; end, len, x, y, z, l, t, r, b: INTEGER; op: INTEGER; v2, clipped: BOOLEAN;
		fgnd, bgnd: Ports.Color; str: ARRAY 256 OF CHAR; size, style: INTEGER;
		poly, ppx: ARRAY 512 OF POINT;
		bt: BYTE;
		rl, rt, rr, rb, dl, dt, dr, db, ol, ot, or, ob, al, at, ar, ab, as, aa, gl, gt, gr, gb, ow, oh, lx, ly, tx, ty, plen: INTEGER;
		brushes: ARRAY 3 OF FillInfo;
		actBrush: INTEGER; actUsePen, actUseBrush: BOOLEAN;
		(*actPen, backBrush, fillBrush: FillInfo;*)
		curX, curY: INTEGER;
		font: Fonts.Font;
		scale: REAL;
		expath: ARRAY 512 OF INTEGER;
		expos: INTEGER;

		PROCEDURE Round (n: INTEGER): INTEGER;
		BEGIN RETURN SHORT(ENTIER(((n * scale) + 0.5) * f.unit));
		END Round;

		PROCEDURE ConvertX (x: INTEGER): INTEGER;
		BEGIN RETURN Round(x)
		END ConvertX;

		PROCEDURE ConvertY (y: INTEGER): INTEGER;
		BEGIN RETURN Round(y)
		END ConvertY;

		PROCEDURE ConvertWidth (w: INTEGER): INTEGER;
		BEGIN
			IF w > 0 THEN w := MAX(f.unit, Round(w)) END;
			RETURN w
		END ConvertWidth;

		PROCEDURE ExInit;
		BEGIN expos := 0
		END ExInit;

		PROCEDURE ExValue (v: INTEGER);
		BEGIN expath[expos] := v; INC(expos)
		END ExValue;

		PROCEDURE ExCommand (v: INTEGER);
		BEGIN ExValue(v)
		END ExCommand;

		PROCEDURE ExCoords (x, y: INTEGER);
		BEGIN ExValue(ConvertX(x)); ExValue(ConvertY(y))
		END ExCoords;

		PROCEDURE ExCoordsNC (x, y: INTEGER);
		BEGIN ExValue(x); ExValue(y)
		END ExCoordsNC;
		
		(*
		PROCEDURE ExPattern (IN brush: FillInfo);
		VAR
			f: INTEGER;
		BEGIN
			IF brush.patFill THEN
				ExCommand(Ports.pathColor); ExValue(Ports.black)
			ELSIF brush.patEmpty THEN
				ExCommand(Ports.pathColor); ExValue(Ports.background)
			ELSE
				ExCommand(Ports.pathPattern);
				ExValue(Ports.black); ExValue(Ports.background);
				FOR f := 0 TO 7 DO ExValue(brush.pat[f]) END
			END
		END ExPattern;
		*)
		
		PROCEDURE Point (VAR x, y: INTEGER);
		BEGIN
			ReadI16(rd, y); ReadI16(rd, x)
		END Point;

		PROCEDURE Data (n: INTEGER);
		VAR
			b: BYTE;
		BEGIN
			WHILE n > 0 DO rd.ReadByte(b); DEC(n) END
		END Data;

		PROCEDURE Region (VAR l, t, r, b: INTEGER);
		VAR
			len: INTEGER;
		BEGIN
			ReadWord(rd, len); ReadI16(rd, l); ReadI16(rd, t);
			ReadI16(rd, r); ReadI16(rd, b); Data(len - 10)
		END Region;

		PROCEDURE Poly (VAR poly: ARRAY OF POINT; VAR len: INTEGER);
		VAR
			l, t, r, b, i: INTEGER;
		BEGIN
			ReadWord(rd, len); len := (len - 10) DIV 4;
			(* this seems to be a polygon bounds *)
			ReadI16(rd, l); ReadI16(rd, t);
			ReadI16(rd, r); ReadI16(rd, b);
			i := 0; WHILE i < len DO Point(poly[i].x, poly[i].y); INC(i) END
		END Poly;

		PROCEDURE Pattern (VAR brush: FillInfo);
		VAR
			i: INTEGER;
			b: BYTE;
			n: SHORTINT;
			isFullPat, isEmptyPat: BOOLEAN;
		BEGIN
			isFullPat := TRUE; isEmptyPat := TRUE;
			i := 0; WHILE i < 8 DO
				rd.ReadByte(b); n := SHORT(b MOD 256);
				brush.pat[i] := n;
				IF n # 0 THEN isEmptyPat := FALSE END;
				IF n # 255 THEN isFullPat := FALSE END;
				INC(i)
			END;
			brush.patFill := isFullPat; brush.patEmpty := isEmptyPat;
			IF isEmptyPat THEN brush.brushColor := Ports.background
			ELSIF isFullPat THEN brush.brushColor := Ports.black
			ELSE brush.brushColor := Ports.RGBColor(255, 128, 0)
			END
		END Pattern;

		PROCEDURE Pen (x, y: INTEGER);
		BEGIN
			IF x < y THEN x := y END;
			(* x is width; style is "inside frame"; zero width is checked in renderers *)
			brushes[brushPen].width := x
		END Pen;

		PROCEDURE String (VAR s: ARRAY OF CHAR);
		VAR
			b: BYTE;
			i, len: INTEGER;
		BEGIN
			rd.ReadByte(b); len := b MOD 256; i := 0;
			WHILE i < len DO
				rd.ReadByte(b);
				(* mac to windows *)
				IF b >= ORD(" ") THEN s[i] := CHR(b MOD 256); INC(i) ELSE DEC(len) END
			END;
			s[i] := 0X
		END String;

		PROCEDURE PixMap (VAR rowBytes, height: INTEGER; VAR v2: BOOLEAN);
		VAR
			x: INTEGER;
		BEGIN
			ReadWord(rd, rowBytes); v2 := rowBytes >= 32768;
			ReadWord(rd, x); height := x;
			ReadWord(rd, x);
			ReadWord(rd, x); height := x - height;
			ReadWord(rd, x);
			IF v2 THEN
				DEC(rowBytes, 32768);
				ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x);
				Point(x, y); Point(x, y);
				Point(x, y); Point(x, y);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x)
			END
		END PixMap;

		PROCEDURE ColorTab;
		VAR
			x, n: INTEGER;
		BEGIN
			ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, n);
			WHILE n >= 0 DO
				ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x);
				DEC(n)
			END
		END ColorTab;

		PROCEDURE PixData (rowBytes, height: INTEGER; v2: BOOLEAN);
		VAR
			n, m: INTEGER;
			b: BYTE;
		BEGIN
			IF rowBytes < 8 THEN
				Skip(rd, rowBytes * height)
			ELSIF v2 THEN
				WHILE height > 0 DO
					IF rowBytes > 250 THEN ReadWord(rd, n) ELSE rd.ReadByte(b); n := b MOD 256 END;
					REPEAT
						ReadByte(rd, m); DEC(n);
						IF m >= 0 THEN
							WHILE m >= 0 DO rd.ReadByte(b); DEC(n); DEC(m) END
						ELSE
							ASSERT(m # -128, 100);
							rd.ReadByte(b); DEC(n)
						END
					UNTIL n <= 0;
					DEC(height)
				END
			ELSE
				WHILE height > 0 DO
					IF rowBytes > 250 THEN ReadWord(rd, n) ELSE rd.ReadByte(b); n := b MOD 256 END;
					Skip(rd, n); DEC(height)
				END
			END
		END PixData;

		PROCEDURE BitMap (isRegion: BOOLEAN);
		VAR
			x, y, w, h, l, t, r, b: INTEGER;
			v2: BOOLEAN;
		BEGIN
			PixMap(w, h, v2);
			IF v2 THEN ColorTab END;
			Point(x, y); Point(x, y); Point(x, y); Point(x, y); ReadWord(rd, x);
			IF isRegion THEN Region(l, t, r, b) END;
			PixData(w, h, v2)
		END BitMap;

		PROCEDURE PixPattern;
		VAR
			type, x, w, h: INTEGER;
			v2: BOOLEAN;
		BEGIN
			ReadWord(rd, type); Data(8);
			IF type = 1 THEN
				PixMap(w, h, v2); ColorTab; PixData(w, h, v2)
			ELSIF type = 2 THEN
				ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x)
			ELSE HALT(100)
			END
		END PixPattern;

		PROCEDURE Setup;
		BEGIN
			CASE op MOD 8 OF
			| 0:	(* line with active pen *)
				actBrush := brushPen;
				actUsePen := TRUE; actUseBrush := FALSE
			| 1:	(* fill with active brush *)
				actBrush := brushPen;
				actUsePen := FALSE; actUseBrush := TRUE
			| 2:	(* fill with background brush *)
				actBrush := brushBack;
				actUsePen := FALSE; actUseBrush := TRUE
			| 4:	(* fill with fill brush *)
				actBrush := brushFill;
				actUsePen := FALSE; actUseBrush := TRUE
			| 3, 5..7:	(* draw nothing *)
				actUsePen := FALSE; actUseBrush := FALSE
			END
		END Setup;

		PROCEDURE SetLn;
		BEGIN
			actBrush := brushPen;
			actUsePen := TRUE; actUseBrush := FALSE
		END SetLn;

		PROCEDURE NewFont;
		VAR
			weight: INTEGER;
			stl: SET;
		BEGIN
			IF ODD(style) THEN weight := Fonts.bold ELSE weight := Fonts.normal END;
			stl := {};
			IF 1 IN BITS(style) THEN INCL(stl, Fonts.italic) END;
			IF 2 IN BITS(style) THEN INCL(stl, Fonts.underline) END;
			font := Fonts.dir.This("Arial", Round(size), stl, weight)
		END NewFont;

		PROCEDURE LineTo (x, y: INTEGER);
		VAR
			w: INTEGER;
		BEGIN
			IF ~clipped & actUsePen & (brushes[actBrush].width > 0) THEN
				w := ConvertWidth(brushes[actBrush].width);
				f.DrawLine(ConvertX(curX), ConvertY(curY),
									ConvertX(x), ConvertY(y), w, brushes[actBrush].color)
				(* draw last pixel *)
				(*
				IF w < f.unit * 2 THEN
					f.DrawLine(ConvertX(lx), ConvertY(ly),
										ConvertX(lx) + f.unit, ConvertY(ly) + f.unit, w, drawWith.color)
				END;
				*)
			END;
			curX := x; curY := y
		END LineTo;

		PROCEDURE Text (x, y: INTEGER);
		VAR
			str: ARRAY 256 OF CHAR;
		BEGIN
			String(str);
			IF ~clipped THEN
				IF font = NIL THEN NewFont END;
				f.DrawString(ConvertX(x), ConvertY(y), Ports.black, str, font)
			END
		END Text;

		PROCEDURE Rectangle (l, t, r, b: INTEGER);
		BEGIN
			IF ~clipped THEN
				IF actUseBrush THEN
					
					(* switch fold if you have LC new `DrawComplexPath()` API *)
					f.DrawRect(ConvertX(l), ConvertY(t), ConvertX(r), ConvertY(b),
										Ports.fill, brushes[actBrush].brushColor)
					
				END;
				IF actUsePen & (brushes[actBrush].width > 0) THEN
					f.DrawRect(ConvertX(l), ConvertY(t), ConvertX(r), ConvertY(b),
										ConvertWidth(brushes[actBrush].width), brushes[actBrush].color)
				END
			END
		END Rectangle;
		
		(*
		PROCEDURE DrawRoundRect (l, t, r, b, rw, rh: INTEGER);
		CONST
			KAPPA90 = 0.5522847493; (* Length proportional to radius of a cubic bezier handle for 90deg arcs. *)

		VAR
			x, y, w, h: INTEGER;

			PROCEDURE Coords (x, y: REAL);
			BEGIN
				ExCoordsNC(SHORT(ENTIER(x + 0.5)), SHORT(ENTIER(y + 0.5)))
			END Coords;

		BEGIN
			l := ConvertX(l); t := ConvertY(t);
			r := ConvertX(r); b := ConvertY(b);
			IF (r - l <= 0) OR (b - t <= 0) THEN RETURN END; (* sorry! *)
			IF (rw <= 0) OR (rh <= 0) THEN
				ExCommand(Ports.pathMoveTo); ExCoordsNC(l, t);
				ExCommand(Ports.pathLineTo); ExCoordsNC(r, t);
				ExCommand(Ports.pathLineTo); ExCoordsNC(r, b);
				ExCommand(Ports.pathLineTo); ExCoordsNC(l, b)
			ELSE
				x := l; y := t; w := r - l; h := b - t;
				ExCommand(Ports.pathMoveTo); Coords(x + rw, y);
				ExCommand(Ports.pathLineTo); Coords(x + w - rw, y);
				ExCommand(Ports.pathBezierTo);
					Coords(x + w - rw*(1 - KAPPA90), y);
					Coords(x + w, y + rh * (1 - KAPPA90));
					Coords(x + w, y + rh);
				ExCommand(Ports.pathLineTo); Coords(x + w, y + h - rh);
				ExCommand(Ports.pathBezierTo);
					Coords(x + w, y + h - rh * (1 - KAPPA90));
					Coords(x + w - rw * (1 - KAPPA90), y + h);
					Coords(x + w - rw, y + h);
				ExCommand(Ports.pathLineTo); Coords(x + rw, y + h);
				ExCommand(Ports.pathBezierTo);
					Coords(x + rw * (1 - KAPPA90), y + h);
					Coords(x, y + h - rh * (1 - KAPPA90));
					Coords(x, y + h - rh);
				ExCommand(Ports.pathLineTo); Coords(x, y + rh);
				ExCommand(Ports.pathBezierTo);
					Coords(x, y + rh * (1 - KAPPA90));
					Coords(x + rw * (1 - KAPPA90), y);
					Coords(x + rw, y)
			END;
			ExCommand(Ports.pathClose);
			f.DrawComplexPath(expath, expos, Ports.fill, brushes[actBrush].brushColor)
		END DrawRoundRect;
		*)

		PROCEDURE RoundRect (l, t, r, b, w, h: INTEGER);
		BEGIN
			
			(* switch fold if you have LC new `DrawRoundRect()` API *)
			Rectangle(l, t, r, b)
			
		END RoundRect;

		PROCEDURE Ellipse (l, t, r, b: INTEGER);
		BEGIN
			IF ~clipped THEN
				IF actUseBrush THEN
					
					(* switch fold if you have LC new `DrawComplexPath()` API *)
					f.DrawOval(ConvertX(l), ConvertY(t), ConvertX(r), ConvertY(b),
										Ports.fill, brushes[actBrush].brushColor)
					
				END;
				IF actUsePen & (brushes[actBrush].width > 0) THEN
					f.DrawOval(ConvertX(l), ConvertY(t), ConvertX(r), ConvertY(b),
										ConvertWidth(brushes[actBrush].width), brushes[actBrush].color)
				END
			END
		END Ellipse;

		PROCEDURE ConvertPath (IN poly: ARRAY OF POINT; len: INTEGER);
		VAR
			f: INTEGER;
		BEGIN
			FOR f := 0 TO len - 1 DO
				ppx[f].x := ConvertX(poly[f].x);
				ppx[f].y := ConvertY(poly[f].y)
			END
		END ConvertPath;

		PROCEDURE Polyline (VAR poly: ARRAY OF POINT; len: INTEGER);
		BEGIN
			IF ~clipped & actUsePen & (brushes[actBrush].width > 0) THEN
				IF len > 1 THEN
					ConvertPath(poly, len);
					f.DrawPath(ppx, len, ConvertWidth(brushes[actBrush].width), brushes[actBrush].color,
										Ports.openPoly)
				END
			END
		END Polyline;

		PROCEDURE Polygon (IN poly: ARRAY OF POINT; len: INTEGER);
		VAR
			c, linecmd: INTEGER;
		BEGIN
			IF ~clipped THEN
				IF actUseBrush THEN
					
					(* switch fold if you have LC new `DrawComplexPath()` API *)
					IF len > 1 THEN
						ConvertPath(poly, len);
						f.DrawPath(ppx, len, Ports.fill, brushes[actBrush].brushColor, Ports.closedPoly)
					END
					
				END;
				IF actUsePen & (brushes[actBrush].width > 0) THEN
					IF len > 1 THEN
						ConvertPath(poly, len);
						f.DrawPath(ppx, len, ConvertWidth(brushes[actBrush].width), brushes[actBrush].color,
											Ports.closedPoly)
					END
				END
			END
		END Polygon;

	BEGIN
		IF (picw < 1) OR (pich < 1) THEN RETURN END; (* sorry! *)
		rd := v.model.file.NewReader(NIL);
		rd.SetPos(v.model.pos); end := v.model.pos + v.model.len;
		ReadWord(rd, x); Point(l, t); Point(r, b); v.w := r - l; v.h := b - t; v2 := FALSE;
		IF (v.w < 1) OR (v.h < 1) THEN RETURN END; (* sorry! *)
		scale := picw / v.w;
		IF pich / v.h < scale THEN scale := pich / v.h END;
		f.rider.GetRect(rl, rt, rr, rb);
		(*f.rider(HostPorts.Rider).FixOrigin;*)
		font := NIL;

		brushes[brushPen].width := 1;
		brushes[brushPen].color := Ports.black; brushes[brushPen].brushColor := Ports.black;
		brushes[brushPen].patFill := TRUE; brushes[brushPen].patEmpty := FALSE;
		FOR z := 0 TO 7 DO brushes[brushPen].pat[z] := 0FFH END;

		brushes[brushFill].width := -1;
		brushes[brushFill].color := Ports.black; brushes[brushFill].brushColor := Ports.black;
		brushes[brushFill].patFill := TRUE; brushes[brushFill].patEmpty := FALSE;
		FOR z := 0 TO 7 DO brushes[brushFill].pat[z] := 0FFH END;

		brushes[brushBack].width := -1;
		brushes[brushBack].color := Ports.background; brushes[brushBack].brushColor := Ports.background;
		brushes[brushBack].patFill := FALSE; brushes[brushBack].patEmpty := TRUE;
		FOR z := 0 TO 7 DO brushes[brushBack].pat[z] := 0 END;

		actBrush := brushPen; actUsePen := FALSE; actUseBrush := FALSE;

		curX := 0; curY := 0;

		tx := 0; ty := 0; lx := 0; ly := 0; size := 10; style := 0; clipped := FALSE;
		REPEAT
			rd.ReadByte(bt); op := bt MOD 256;
			IF v2 THEN rd.ReadByte(bt); op := 256 * op + bt MOD 256 END;
			CASE op OF
			| 0: (* nop *)
			| 1: (* clip *) Region(l, t, r, b); clipped := (l = 0) & (t = 0) & (r = 0) & (b = 0)
			| 2: (* bgnd pattern *) Pattern(brushes[brushBack])
			| 3: (* text font *) ReadWord(rd, x)
			| 4: (* text face *) rd.ReadByte(bt); style := bt; NewFont
			| 5: (* text mode *) ReadWord(rd, x)
			| 6: (* space extra *) Point(x, y)
			| 7: (* pen size *) Point(x, y); Pen(x, y)
			| 8: (* pen mode *) ReadWord(rd, x)
			| 9: (* pen pattern *) Pattern(brushes[brushPen])
			| 0AH: (* fill pattern *) Pattern(brushes[brushFill])
			| 0BH: (* oval size *) Point(ow, oh)
			| 0CH: (* origin *) Point(x, y)
			| 0DH: (* text size *)  ReadWord(rd, size); NewFont
			| 0EH: (* foreground *) ReadWord(rd, x); ReadWord(rd, y); fgnd := x * 65536 + y
			| 0FH: (* background *) ReadWord(rd, x); ReadWord(rd, y); bgnd := x * 65536 + y
			| 10H: (* text ratio *) Point(x, y); Point(x, y)
			| 11H: (* version *) rd.ReadByte(bt); v2 := bt = 2
			| 12H: (* bg pix pattern *) PixPattern
			| 13H: (* pen pix pattern *) PixPattern
			| 14H: (* fill pix pattern *) PixPattern
			| 15H: (* fract pen pos *) ReadWord(rd, x)
			| 16H: (* char extra *) ReadWord(rd, x)
			| 17H..19H: (* ??? *)
			| 1AH: (* rgb fg col *) ReadWord(rd, x); ReadWord(rd, y); ReadWord(rd, z)
			| 1BH: (* rgb bg col *) ReadWord(rd, x); ReadWord(rd, y); ReadWord(rd, z)
			| 1CH: (* hilite mode *)
			| 1DH: (* rgb hl col *) ReadWord(rd, x); ReadWord(rd, y); ReadWord(rd, z)
			| 1EH: (* def hilite *)
			| 1FH: (* rgb op col *) ReadWord(rd, x); ReadWord(rd, y); ReadWord(rd, z)
			| 20H: (* line *) Point(curX, curY); Point(lx, ly); SetLn; LineTo(lx, ly)
			| 21H: (* line from *) Point(lx, ly); SetLn; LineTo(lx, ly)
			| 22H: (* short line *)
				Point(lx, ly); curX := lx; curY := ly;
				ReadByte(rd, z); INC(lx, z); ReadByte(rd, z); INC(ly, z);
				SetLn; LineTo(lx, ly)
			| 23H: (* short line from *) ReadByte(rd, z); INC(lx, z); ReadByte(rd, z); INC(ly, z); SetLn; LineTo(lx, ly)
			| 24H..27H: (* ??? *) ReadWord(rd, len); Data(len)
			| 28H: (* long text *) Point(tx, ty); Text(tx, ty)
			| 29H: (* dh text *) rd.ReadByte(bt); INC(tx, bt MOD 256); Text(tx, ty)
			| 2AH: (* dv text *) rd.ReadByte(bt); INC(ty, bt MOD 256); Text(tx, ty)
			| 2BH: (* dhv text *) rd.ReadByte(bt); INC(tx, bt MOD 256); rd.ReadByte(bt); INC(ty, bt MOD 256); Text(tx, ty)
			| 2CH: (* font typeface ? *) ReadWord(rd, len); ReadWord(rd, x); String(str)
			| 2DH..2FH: (* ??? *) ReadWord(rd, len); Data(len)
			| 30H..37H: (* rect *) Point(rl, rt); Point(rr, rb); Setup; Rectangle(rl, rt, rr, rb)
			| 38H..3FH: (* same rect *) Setup; Rectangle(rl, rt, rr, rb)
			| 40H..47H: (* rrect *) Point(dl, dt); Point(dr, db); Setup; RoundRect(dl, dt, dr, db, ow, oh)
			| 48H..4FH: (* same rrect *) Setup; RoundRect(dl, dt, dr, db, ow, oh)
			| 50H..57H: (* oval *) Point(ol, ot); Point(or, ob); Setup; Ellipse(ol, ot, or, ob)
			| 58H..5FH: (* same oval *) Setup; Ellipse(ol, ot, or, ob)
			| 60H..67H: (* arc *) Point(al, at); Point(ar, ab); Point(aa, as)
			| 68H..6FH: (* same arc *) Point(aa, as)
			| 70H: (* poly *) Poly(poly, plen); SetLn; Polyline(poly, plen)
			| 71H..77H: (* poly *) Poly(poly, plen); Setup; Polygon(poly, plen)
			| 78H: (* same poly *) SetLn; Polyline(poly, plen)
			| 79H..7FH: (* same poly *) Setup; Polygon(poly, plen)
			| 80H..87H: (* rgn *) Region(gl, gt, gr, gb)
			| 88H..8FH: (* same rgn *)
			| 90H: (* bits rect *) BitMap(FALSE)
			| 91H: (* bits rgn *) BitMap(TRUE)
			| 92H..97H: (* ??? *) ReadWord(rd, len); Data(len)
			| 98H: (* packed bits rect *) BitMap(FALSE)
			| 99H: (* packed bits rgn *) BitMap(TRUE)
			| 9AH..9FH: (* ??? *) ReadWord(rd, len); Data(len)
			| 0A0H: (* short comment *) ReadWord(rd, x)
			| 0A1H: (* long comment *) ReadWord(rd, x); ReadWord(rd, len); Data(len)
			| 0A2H..0AFH: (* ??? *) ReadWord(rd, len); Data(len)
			| 0B0H..0CFH: (* ??? *)
			| 0D0H..0FEH: (* ??? *) ReadWord(rd, x); ReadWord(rd, len); Data(x * 65536 + len)
			| 0FFH: (* end *)
			ELSE
				IF op < 8000H THEN (* ??? *) Data(op DIV 256 * 2)
				ELSIF op < 8100H THEN (* ??? *)
				ELSE (* ??? *) ReadWord(rd, x); ReadWord(rd, len); Data(x * 65536 + len)
				END
			END;
			IF v2 & ODD(rd.Pos() - v.model.pos) THEN rd.ReadByte(bt) END
		UNTIL (op = 0FFH) OR (rd.Pos() >= end)
	END DrawMacPicture;

	(*
	PROCEDURE Dump (v: StdView);
	VAR
		rd: Files.Reader;
		op, end, len, x, y: INTEGER;
		v2: BOOLEAN;
		b: BYTE;
		str: ARRAY 256 OF CHAR;

		PROCEDURE DumpBox (l, t, r, b: INTEGER);
		BEGIN
			Log.String("(");
			Log.IntForm(l, 10, 0, ' ', FALSE);
			Log.String(", "); Log.IntForm(t, 10, 0, ' ', FALSE);
			Log.String(", "); Log.IntForm(r, 10, 0, ' ', FALSE);
			Log.String(", "); Log.IntForm(b, 10, 0, ' ', FALSE);
			Log.String(" : "); Log.IntForm(r - l, 10, 0, ' ', FALSE);
			Log.String("x"); Log.IntForm(b - t, 10, 0, ' ', FALSE);
			Log.String(")")
		END DumpBox;

		PROCEDURE DumpPoint (x, y: INTEGER);
		BEGIN
			Log.String("P("); Log.IntForm(x, 10, 0, ' ', FALSE);
			Log.String(", "); Log.IntForm(y, 10, 0, ' ', FALSE);
			Log.String(")")
		END DumpPoint;

		PROCEDURE Box;
		VAR
			l, t, r, b: INTEGER;
		BEGIN
			ReadI16(rd, l); ReadI16(rd, t);
			ReadI16(rd, r); ReadI16(rd, b);
			Log.String("Box"); DumpBox(l, t, r, b)
		END Box;

		PROCEDURE Point;
		VAR
			x, y: INTEGER;
		BEGIN
			ReadI16(rd, y); ReadI16(rd, x);
			DumpPoint(x, y)
		END Point;

		PROCEDURE ShortLine;
		VAR
			x, y: INTEGER;
			b: BYTE;
		BEGIN
			ReadI16(rd, y); ReadI16(rd, x);
			DumpPoint(x, y);
			rd.ReadByte(b); INC(x, b);
			rd.ReadByte(b); INC(y, b);
			Log.String(" TO "); DumpPoint(x, y)
		END ShortLine;

		PROCEDURE ShortLineFrom;
		VAR
			dx, dy: BYTE;
		BEGIN
			rd.ReadByte(dx); rd.ReadByte(dy);
			Log.String("DP("); Log.IntForm(dx, 10, 0, ' ', FALSE);
			Log.String(", "); Log.IntForm(dy, 10, 0, ' ', FALSE);
			Log.String(")")
		END ShortLineFrom;

		PROCEDURE Data (n: INTEGER);
		VAR
			i: INTEGER;
		BEGIN
			WHILE n > 0 DO
				rd.ReadByte(b); DEC(n);
				i := b; IF i < 0 THEN INC(i, 256) END;
				Log.Char(" ");
				Log.IntForm(i, 16, 2, "0", FALSE)
			END
		END Data;

		PROCEDURE Region;
		VAR
			len: INTEGER;
			l, t, r, b: INTEGER;
		BEGIN
			ReadWord(rd, len);
			ReadI16(rd, l); ReadI16(rd, t);
			ReadI16(rd, r); ReadI16(rd, b);
			Log.String("Region"); DumpBox(l, t, r, b);
			Data(len - 10)
		END Region;

		PROCEDURE Text;
			VAR b: BYTE; len: INTEGER;
		BEGIN
			rd.ReadByte(b); len := b MOD 256;
			Log.String(' "');
			WHILE len > 0 DO rd.ReadByte(b); Log.Char(CHR(b MOD 256)); DEC(len) END;
			Log.Char('"')
		END Text;

		PROCEDURE String (VAR s: ARRAY OF CHAR);
		VAR
			b: BYTE;
			i, len: INTEGER;
		BEGIN
			rd.ReadByte(b); len := b MOD 256; i := 0;
			WHILE i < len DO
				rd.ReadByte(b);
				(* mac to windows *)
				IF b >= ORD(" ") THEN s[i] := CHR(b MOD 256); INC(i) ELSE DEC(len) END
			END;
			s[i] := 0X
		END String;

		PROCEDURE PixMap (VAR rowBytes, height: INTEGER; VAR v2: BOOLEAN);
		VAR
			x: INTEGER;
		BEGIN
			ReadWord(rd, rowBytes); v2 := rowBytes >= 32768;
			Log.Int(rowBytes);
			ReadWord(rd, x); Log.Int(x); height := x;
			ReadWord(rd, x); Log.Int(x);
			ReadWord(rd, x); Log.Int(x); height := x - height;
			ReadWord(rd, x); Log.Int(x);
			IF v2 THEN
				DEC(rowBytes, 32768);
				ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x);
				Point; Point;
				Point; Point;
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x);
				ReadWord(rd, x)
			END
		END PixMap;

		PROCEDURE ColorTab;
		VAR
			x, n: INTEGER;
		BEGIN
			ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, n); Log.Int(n);
			WHILE n >= 0 DO
				ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x); ReadWord(rd, x);
				DEC(n)
			END
		END ColorTab;

		PROCEDURE PixData (rowBytes, height: INTEGER; v2: BOOLEAN);
		VAR
			n, m, k: INTEGER;
			b: BYTE;
		BEGIN
			IF rowBytes < 8 THEN
				Skip(rd, rowBytes * height)
			ELSIF v2 THEN
				WHILE height > 0 DO
					IF rowBytes > 250 THEN ReadWord(rd, n) ELSE rd.ReadByte(b); n := b MOD 256 END;
					k := 0;
					REPEAT
						rd.ReadByte(b); DEC(n); m := b;
						IF m >= 0 THEN
							WHILE m >= 0 DO rd.ReadByte(b); DEC(n); DEC(m); INC(k) END
						ELSE
							ASSERT(m # -128, 100);
							rd.ReadByte(b); DEC(n); INC(k, 1 - m)
						END
					UNTIL n <= 0;
					ASSERT(n = 0, 101);
					ASSERT(k = rowBytes, 102);
					(* Skip(rd, n); *) DEC(height)
				END
			ELSE
				WHILE height > 0 DO
					IF rowBytes > 250 THEN ReadWord(rd, n) ELSE rd.ReadByte(b); n := b MOD 256 END;
					Skip(rd, n); DEC(height)
				END
			END
		END PixData;

		PROCEDURE PixPattern;
		VAR
			type, w, h: INTEGER;
			v2: BOOLEAN;
		BEGIN
			ReadWord(rd, type); Data(8);
			IF type = 1 THEN
				PixMap(w, h, v2); ColorTab; PixData(w, h, v2)
			ELSIF type = 2 THEN
				ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x)
			ELSE HALT(100)
			END
		END PixPattern;

		PROCEDURE Bits (region: BOOLEAN);
			VAR w, h: INTEGER; v2: BOOLEAN;
		BEGIN
			PixMap(w, h, v2);
			IF v2 THEN ColorTab END;
			Point; Point; Point; Point; ReadWord(rd, x); Log.Int(x);
			IF region THEN Region END;
			PixData(w, h, v2)
		END Bits;

		PROCEDURE Poly;
		VAR
			i, l, t, r, b, len: INTEGER;
		BEGIN
			ReadWord(rd, len); len := (len - 10) DIV 4;
			ReadI16(rd, l); ReadI16(rd, t);
			ReadI16(rd, r); ReadI16(rd, b);
			Log.String("Region"); DumpBox(l, t, r, b);
			Log.String(" points:"); Log.Int(len); Log.String(" :");
			i := 0; WHILE i < len DO Log.String(" "); Point; INC(i) END
		END Poly;

		PROCEDURE LongComment;
		VAR
			type, len, i: INTEGER;
		BEGIN
			ReadWord(rd, type); Log.Int(type);
			ReadWord(rd, len);
			IF (type = 182) & (len = 1 * 4) THEN
				Log.String(" : "); Point
			ELSIF (type = 150) & (len = 3 * 4) THEN
				Log.String(" :"); ReadDWord(rd, i); Log.Int(i);
				ReadDWord(rd, i); Log.Int(i);
				ReadDWord(rd, i); Log.Int(i)
			ELSIF (type = 154) & (len = 2 * 4) THEN
				Log.String(" : "); Point; Log.String(" "); Point
			ELSE Data(len)
			END
		END LongComment;

	BEGIN
		rd := v.model.file.NewReader(NIL);
		rd.SetPos(v.model.pos); end := v.model.pos + v.model.len;
		Log.String("==== MACPIC: pos:"); Log.Int(v.model.pos); Log.String("; end:"); Log.Int(end);
		Log.String(" ===="); Log.Ln;
		ReadWord(rd, x); Log.Int(x); Box; Log.Ln; v2 := FALSE;
		REPEAT
			Log.IntForm(rd.Pos(), 10, 6, ' ', FALSE); Log.String(": ");
			rd.ReadByte(b); op := b MOD 256;
			IF v2 THEN rd.ReadByte(b); op := 256 * op + b MOD 256 END;
			Log.IntForm(op, 16, 4, "0", FALSE);
			Log.String("("); Log.IntForm(op MOD 8, 10, 1, "0", FALSE);
			Log.String(") ");
			CASE op OF
			| 0: Log.String("nop")
			| 1: Log.String("clip "); Region
			| 2: Log.String("bkd pattern "); Data(8)
			| 3: Log.String("text font "); ReadWord(rd, x); Log.Int(x)
			| 4: Log.String("text face "); rd.ReadByte(b); Log.Int(b MOD 256)
			| 5: Log.String("text mode "); ReadWord(rd, x);  Log.Int(x)
			| 6: Log.String("space extra "); Point
			| 7: Log.String("pen size "); Point
			| 8: Log.String("pen mode "); ReadWord(rd, x); Log.Int(x)
			| 9: Log.String("pen pattern "); Data(8)
			| 0AH: Log.String("pen pattern "); Data(8)
			| 0BH: Log.String("oval size "); Point
			| 0CH: Log.String("origin "); Point
			| 0DH: Log.String("text size "); ReadWord(rd, x); Log.Int(x)
			| 0EH: Log.String("foreground "); ReadWord(rd, x); ReadWord(rd, y); Log.Int(x * 65536 + y)
			| 0FH: Log.String("background "); ReadWord(rd, x); ReadWord(rd, y); Log.Int(x * 65536 + y)
			| 10H: Log.String("text ratio "); Point; Point
			| 11H: Log.String("version "); rd.ReadByte(b); Log.Int(b MOD 256); v2 := b = 2
			| 12H: Log.String("bg pix pattern "); PixPattern
			| 13H: Log.String("pen pix pattern "); PixPattern
			| 14H: Log.String("fill pix pattern "); PixPattern
			| 15H: Log.String("fract pen pos "); ReadWord(rd, x); Log.Int(x)
			| 16H: Log.String("char extra "); ReadWord(rd, x); Log.Int(x)
			| 17H..19H: Log.String("???")
			| 1AH: Log.String("rgb fg col ");
				ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x)
			| 1BH: Log.String("rgb bg col ");
				ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x)
			| 1CH: Log.String("hilite mode")
			| 1DH: Log.String("rgb hl col ");
				ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x)
			| 1EH: Log.String("def hilite")
			| 1FH: Log.String("rgb op col ");
				ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x); ReadWord(rd, x); Log.Int(x)
			| 20H: Log.String("line "); Point; Point
			| 21H: Log.String("line from "); Point
			| 22H: Log.String("short line "); ShortLine
			| 23H: Log.String("short line from "); ShortLineFrom
			| 24H..27H: Log.String("??? "); ReadWord(rd, len); Data(len)
			| 28H: Log.String("long text "); Point; Text
			| 29H: Log.String("dh text "); rd.ReadByte(b); Log.Int(b MOD 256); Text
			| 2AH: Log.String("dv text "); rd.ReadByte(b); Log.Int(b MOD 256); Text
			| 2BH: Log.String("dhv text ");
				rd.ReadByte(b); Log.Int(b MOD 256); rd.ReadByte(b); Log.Int(b MOD 256); Text
			| 2CH: (* font typeface ? *) ReadWord(rd, len); ReadWord(rd, x); String(str);
				Log.String("font face: "); Log.String(str)
			| 2DH..2FH: Log.String("??? "); ReadWord(rd, len); Data(len)
			| 30H..37H: Log.String("rect "); Point; Point
			| 38H..3FH: Log.String("same rect")
			| 40H..47H: Log.String("rrect "); Point; Point
			| 48H..4FH: Log.String("same rrect")
			| 50H..57H: Log.String("oval "); Point; Point
			| 58H..5FH: Log.String("same oval")
			| 60H..67H: Log.String("arc "); Point; Point; Point
			| 68H..6FH: Log.String("same arc "); Point
			| 70H..77H: Log.String("poly "); Poly
			| 78H..7FH: Log.String("same poly")
			| 80H..87H: Log.String("rgn "); Region
			| 88H..8FH: Log.String("same rgn")
			| 90H: Log.String("bits rect "); Bits(FALSE)
			| 91H: Log.String("bits rgn "); Bits(TRUE)
			| 92H..97H: Log.String("??? "); ReadWord(rd, len); Data(len)
			| 98H: Log.String("packed bits rect "); Bits(FALSE)
			| 99H: Log.String("packed bits rgn "); Bits(TRUE)
			| 9AH..9FH: Log.String("??? "); ReadWord(rd, len); Data(len)
			| 0A0H: Log.String("short comment "); ReadWord(rd, x); Log.Int(x)
			| 0A1H: Log.String("long comment "); LongComment
			| 0A2H..0AFH: Log.String("??? "); ReadWord(rd, len); Data(len)
			| 0B0H..0CFH: Log.String("???")
			| 0D0H..0FEH: Log.String("??? "); ReadWord(rd, x); ReadWord(rd, len); Data(x * 65536 + len)
			| 0FFH: Log.String("end")
			ELSE
				IF op < 8000H THEN Log.String("??? "); Data(op DIV 256 * 2)
				ELSIF op < 8100H THEN Log.String("???")
				ELSE Log.String("??? "); ReadWord(rd, x); ReadWord(rd, len); Data(x * 65536 + len)
				END
			END;
			IF v2 & ODD(rd.Pos() - v.model.pos) THEN rd.ReadByte(b) END;
			Log.Ln
		UNTIL (op = 0FFH) OR (rd.Pos() >= end)
	END Dump;
	*)

	PROCEDURE Evaluate (v: StdView);
		VAR len: INTEGER; rd: Files.Reader;
	BEGIN
		len := v.model.len;
		IF v.model.data = NIL THEN
			rd := v.model.file.NewReader(NIL);
			rd.SetPos(v.model.pos);
			NEW(v.model.data, len);
			rd.ReadBytes(v.model.data^, 0, len)
		END;
		IF externalDrawer # NIL THEN
			v.model.ref := externalDrawer.Prepare(len, v.model.data);
			ASSERT(v.model.ref # 0, 100);
		END
	END Evaluate;


	(** **************** Model **************** **)

	PROCEDURE (m: Model) FINALIZE;
	BEGIN
		IF (externalDrawer # NIL) & (m.ref # 0) THEN
			externalDrawer.Finalize(m.ref);
			m.ref := 0
		END
	END FINALIZE;


	(** **************** View **************** **)

	PROCEDURE (v: StdView) Internalize (VAR rd: Stores.Reader);
		VAR m: Model; thisVersion: INTEGER;
	BEGIN
		v.Internalize^(rd);
		IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(minVersion, maxVersion, thisVersion);
		IF rd.cancelled THEN RETURN END;
		rd.ReadByte(v.type);
		IF (v.type # winPict) & (v.type # macPict) THEN rd.TurnIntoAlien(Stores.alienComponent); RETURN END;
		rd.ReadInt(v.unit);
		rd.ReadInt(v.w);
		rd.ReadInt(v.h);
		rd.ReadSChar(v.mode);
		NEW(m); m.file := rd.rider.Base();
		rd.ReadInt(m.len);
		m.pos := rd.Pos();
		m.ref := 0;	(* lazy allocation of metafile *)
		v.model := m;
		rd.SetPos(m.pos + m.len)
	END Internalize;

	PROCEDURE (v: StdView) Externalize (VAR wr: Stores.Writer);
		VAR len: INTEGER; r: Files.Reader; b: BYTE;
	BEGIN
		v.Externalize^(wr);
		wr.WriteVersion(maxVersion);
		wr.WriteByte(v.type);
		wr.WriteInt(v.unit);
		wr.WriteInt(v.w);
		wr.WriteInt(v.h);
		wr.WriteSChar(v.mode);
		len := v.model.len;
		wr.WriteInt(len);
		IF v.model.data # NIL THEN
			wr.rider.WriteBytes(v.model.data^, 0, len)
		ELSIF v.model.file # NIL THEN
			r := v.model.file.NewReader(NIL); r.SetPos(v.model.pos);
			WHILE len # 0 DO r.ReadByte(b); wr.WriteSChar(SHORT(CHR(b))); DEC(len) END
		ELSIF externalDrawer # NIL THEN
			ASSERT(v.model.ref # 0, 100);
			NEW(v.model.data, len);
			externalDrawer.GetBytes(v.model.ref, len, v.model.data);
			wr.rider.WriteBytes(v.model.data^, 0, len)
		END
	END Externalize;

	PROCEDURE (v: StdView) ThisModel (): Models.Model;
	BEGIN
		RETURN NIL
	END ThisModel;

	PROCEDURE (v: StdView) CopyFromSimpleView (source: Views.View);
	BEGIN
		(* v.CopyFrom^(source); *)
		WITH source: StdView DO
			v.model := source.model;
			v.type := source.type;
			v.mode := source.mode;
			v.w := source.w;
			v.h := source.h;
			v.unit := source.unit
		END
	END CopyFromSimpleView;

	PROCEDURE (v: StdView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
	VAR
		w, h: INTEGER;
	BEGIN
		ASSERT(v.model # NIL, 20);
		v.context.GetSize(w, h);
		IF v.type = winPict THEN
			IF v.model.ref = 0 THEN Evaluate(v) END;
			IF externalDrawer # NIL THEN
				externalDrawer.Draw(f, v.model.ref, ORD(v.mode), f.gx, f.gy, w, h)
			END;
		ELSIF v.type = macPict THEN
			IF drawBackRect THEN
				f.DrawRect(l, t, r, b, Ports.fill, Ports.green)
			ELSE
				f.DrawRect(l, t, r, b, Ports.fill, Ports.background)
			END;
			DrawMacPicture(v, f, w DIV f.unit, h DIV f.unit)
		END
	END Restore;

	PROCEDURE (v: StdView) GetBkgndFor (f: Views.Frame; VAR col: Ports.Color;
																	VAR homogeneous: BOOLEAN), NEW;
	BEGIN
		col := Ports.background
	END GetBkgndFor;

	PROCEDURE (v: StdView) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.SizePref DO
			IF (msg.w > Views.undefined) & (msg.h > Views.undefined) THEN
				IF (v.type = macPict) & (v.w # 0) & (v.h # 0) OR (v.mode = 7X) THEN	(* isotropic mode *)
					Properties.ProportionalConstraint(ABS(v.w), ABS(v.h), msg.fixedW, msg.fixedH, msg.w, msg.h)
				END
			ELSE
				IF (v.w > 0) & (v.h > 0) THEN	(* default sizes *)
					msg.w := ABS(v.w); msg.h := ABS(v.h)
				END
			END
		ELSE
		END
	END HandlePropMsg;

	(*
	PROCEDURE (v: StdView) HandleCtrlMsg (f: Views.Frame; VAR msg: Controllers.Message;
																		VAR focus: Views.View);
	BEGIN
		IF doDump THEN
			WITH msg: Controllers.TrackMsg DO
				IF v.type = macPict THEN Dump(v) END
			ELSE
			END
		END
	END HandleCtrlMsg;
	*)

	PROCEDURE PictType* (v: Views.View): INTEGER;
	BEGIN
		WITH v: StdView DO RETURN v.type
		ELSE RETURN -1
		END
	END PictType;

	PROCEDURE SetDrawBackRect*;
	BEGIN
		drawBackRect := TRUE
	END SetDrawBackRect;

	PROCEDURE ResetDrawBackRect*;
	BEGIN
		drawBackRect := FALSE
	END ResetDrawBackRect;


	PROCEDURE SetDoDump*;
	BEGIN
		doDump := TRUE
	END SetDoDump;

	PROCEDURE ResetDoDump*;
	BEGIN
		doDump := FALSE
	END ResetDoDump;

BEGIN
	IF Dialog.platform = Dialog.windows THEN Kernel.LoadMod("WinPictures") END;
	drawBackRect := FALSE;
	doDump := FALSE
END StdPictures.
