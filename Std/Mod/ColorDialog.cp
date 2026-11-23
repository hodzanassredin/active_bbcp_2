MODULE StdColorDialog;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""
**)

	IMPORT
		Views, Dialog, Ports, Math, Controllers, Properties, Fonts,
		StdDialog, StdCFrames, StdCmds;

	CONST
		sliderHeight = 14 * Ports.mm;
		margin = 2 * Ports.mm;
		
	TYPE
		DialogGetColorHook = POINTER TO RECORD (Dialog.GetColorHook) END;
		
		View = POINTER TO RECORD (Views.View)
			hue, sat, volume: REAL;
			action: Dialog.GetColorAction
		END;

	PROCEDURE HSVtoRGB (h, s, v: REAL; VAR red, green, blue: INTEGER);
		VAR
			i: INTEGER;
			f, p, q, tt: REAL;
	BEGIN
		h := h * 6.0;
		i := SHORT(ENTIER(h));
		f := h - i;

		p := v * (1.0 - s);
		q := v * (1.0 - s * f);
		tt := v * (1.0 - s * (1.0 - f));

		CASE i MOD 6 OF
		| 0:
			red := SHORT(ENTIER(255.0 * v));
			green := SHORT(ENTIER(255.0 * tt));
			blue := SHORT(ENTIER(255.0 * p))
		| 1:
			red := SHORT(ENTIER(255.0 * q));
			green := SHORT(ENTIER(255.0 * v));
			blue := SHORT(ENTIER(255.0 * p))
		| 2:
			red := SHORT(ENTIER(255.0 * p));
			green := SHORT(ENTIER(255.0 * v));
			blue := SHORT(ENTIER(255.0 * tt))
		| 3:
			red := SHORT(ENTIER(255.0 * p));
			green := SHORT(ENTIER(255.0 * q));
			blue := SHORT(ENTIER(255.0 * v))
		| 4:
			red := SHORT(ENTIER(255.0 * tt));
			green := SHORT(ENTIER(255.0 * p));
			blue := SHORT(ENTIER(255.0 * v))
		| 5:
			red := SHORT(ENTIER(255.0 * v));
			green := SHORT(ENTIER(255.0 * p));
			blue := SHORT(ENTIER(255.0 * q))
		END
	END HSVtoRGB;

	PROCEDURE (v: View) Restore (f: Views.Frame; l_, t_, r_, b_: INTEGER);
		VAR
			x, y, w, h, cx, cy, rad, asc, dsc, sw: INTEGER;
			dx, dy, rr, hue, sat: REAL;
			col: Ports.Color;
			step: INTEGER;
			font: Fonts.Font;
			red, green, blue: INTEGER;
			str: ARRAY 256 OF CHAR;
	BEGIN
		font := StdCFrames.defaultFont;
		v.context.GetSize(w, h);
		f.DrawRect(0, 0, w, h, Ports.fill, Ports.dialogBackground);
		
		rad := (w - margin * 2) DIV 2;
		cx := rad + margin;
		cy := cx;
		
		step := Ports.point;
		y := cy - rad - step;
		WHILE y <= cy + rad + step DO
			x := cx - rad - step;
			WHILE x <= cx + rad + step DO
				dx := (x - cx) / rad;
				dy := (cy - y) / rad;
				rr := Math.Sqrt(dx * dx + dy * dy);
				IF rr <= 1.0 THEN
					sat := rr;
					(* angle 0..1 *)
					hue := Math.ArcTan2(dy, dx) / (2.0 * Math.Pi());
					IF hue < 0.0 THEN
						hue := hue + 1.0
					END;
					HSVtoRGB(hue, sat, 1.0, red, green, blue);
					col := Ports.RGBColor(red, green, blue);
					f.DrawRect(x - step, y - step, x + step, y + step, Ports.fill, col)
				END;
				INC(x, step * 2)
			END;
			INC(y, step * 2)
		END;
		sw := w DIV 2;
		f.DrawOval(
			cx - sw + margin - Ports.mm DIV 2,
			cy - sw + margin - Ports.mm DIV 2,
			cx + sw - margin + Ports.mm DIV 2,
			cy + sw - margin + Ports.mm DIV 2, Ports.mm DIV 2 * 3, Ports.dialogBackground);
		
		(* marker of selected color *)
		dx := Math.Cos(v.hue * 2.0 * Math.Pi()) * v.sat;
		dy := Math.Sin(v.hue * 2.0 * Math.Pi()) * v.sat;
		x := cx + SHORT(ENTIER(dx * rad));
		y := cy - SHORT(ENTIER(dy * rad));
		f.DrawOval(
			x - 2 * Ports.mm, y - 2 * Ports.mm,
			x + 2 * Ports.mm, y + 2 * Ports.mm,
			f.dot * 2, Ports.black
		);
		
		y := w;
		x := margin;
		WHILE x < w - margin DO
			rr := (x - margin) / (w - margin * 2);
			HSVtoRGB(v.hue, v.sat, rr, red, green, blue);
			col := Ports.RGBColor(red, green, blue);
			f.DrawLine(x, y, x, y + sliderHeight, Ports.point * 3, col);
			INC(x, Ports.point * 2);
		END;
		f.DrawRect(
			margin - Ports.point * 2,
			w - Ports.point * 2,
			w - margin + Ports.point * 2,
			y + sliderHeight + Ports.point * 2,
			Ports.point * 3, Ports.dialogBackground);
		x := margin + SHORT(ENTIER((w - margin * 2) * v.volume));
		f.DrawRect(
			x - 2 * f.dot, y,
			x + 2 * f.dot, y + sliderHeight,
			f.dot * 2, Ports.black
		);
		f.DrawRect(
			x - f.dot, y + f.dot,
			x + f.dot, y + sliderHeight - f.dot,
			f.dot * 2, Ports.white
		);
		
		(* Confirm button *)
		HSVtoRGB(v.hue, v.sat, v.volume, red, green, blue);
		col := Ports.RGBColor(red, green, blue);
		f.DrawRect(margin, h - sliderHeight, w - margin, h - margin, Ports.fill, col);
		f.DrawRect(margin, h - sliderHeight, w - margin, h - margin, f.dot, Ports.black);

		Dialog.MapString("#System:Confirm", str);
		x := font.StringWidth(str);

		(* choose readable text color *)
		IF red * 299 + green * 587 + blue * 114 > 140000 THEN
			col := Ports.black
		ELSE
			col := Ports.white
		END;
		font.GetBounds(asc, dsc, sw);
		f.DrawString(
			(w - x) DIV 2,
			h - margin - sliderHeight DIV 2 + (asc + dsc) DIV 2,
			col, str, font
		);
	END Restore;

	PROCEDURE (v: View) HandleCtrlMsg (f: Views.Frame; VAR msg: Controllers.Message; VAR focus: Views.View);
		VAR
			w, h, cx, cy, rad, red, green, blue: INTEGER;
			dx, dy, rr: REAL; x, y: INTEGER; modifiers: SET; isDown: BOOLEAN;
	BEGIN
		WITH msg: Controllers.TrackMsg DO
			v.context.GetSize(w, h);
			IF msg.y > h - sliderHeight THEN
				HSVtoRGB(v.hue, v.sat, v.volume, red, green, blue);
				v.action.out := Ports.RGBColor(red, green, blue);
				v.action.cancel := FALSE;
				v.action.Do;
				StdCmds.CloseDialog;
			END
		| msg: Controllers.PollCursorMsg DO
			v.context.GetSize(w, h);
			IF (msg.y > w + margin * 2) & (msg.y < w + margin * 2 + sliderHeight) THEN
				f.Input(x, y, modifiers, isDown);
				IF isDown THEN
					v.volume := (msg.x - margin) / (w - margin * 2);
					IF v.volume < 0 THEN v.volume := 0 END;
					IF v.volume > 1 THEN v.volume := 1 END;
					Views.Update(v, Views.keepFrames)
				END
			ELSIF (msg.y < w + margin) THEN
				f.Input(x, y, modifiers, isDown);
				IF isDown THEN
					rad := (w - margin * 2) DIV 2;
					cx := rad + margin;
					cy := cx;
					dx := (msg.x - cx) / rad;
					dy := (cy - msg.y) / rad;
					rr := Math.Sqrt(dx * dx + dy * dy);
					IF rr <= 1.0 THEN
						v.sat := rr;
						v.hue  := Math.ArcTan2(dy, dx) / (2.0 * Math.Pi());
						IF v.hue < 0.0 THEN
							v.hue := v.hue + 1.0
						END;
						HSVtoRGB(v.hue, v.sat, v.volume, red, green, blue);
						Views.Update(v, Views.keepFrames)
					END
				END
			END
		ELSE END
	END HandleCtrlMsg;

	PROCEDURE (v: View) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.SizePref DO
			IF (msg.w = Views.undefined) OR (msg.h = Views.undefined) THEN
				 msg.w := 60 * Ports.mm; msg.h := 90 * Ports.mm
			END
		ELSE END
	END HandlePropMsg;


	PROCEDURE RGBtoHSV (red, green, blue: INTEGER; OUT h, s, v: REAL);
		VAR r, g, b, max, min, d: REAL;
	BEGIN
		r := red / 255.0;
		g := green / 255.0;
		b := blue / 255.0;
		
		max := r;
		IF g > max THEN max := g END;
		IF b > max THEN max := b END;

		min := r;
		IF g < min THEN min := g END;
		IF b < min THEN min := b END;

		d := max - min;

		v := max;

		IF max = 0.0 THEN
			s := 0.0
		ELSE
			s := d / max
		END;

		IF d = 0.0 THEN
			h := 0.0
		ELSIF max = r THEN
			h := (g - b) / d / 6.0;
			IF h < 0.0 THEN
				h := h + 1.0
			END
		ELSIF max = g THEN
			h := ((b - r) / d + 2.0) / 6.0
		ELSE
			h := ((r - g) / d + 4.0) / 6.0
		END
	END RGBtoHSV;

	PROCEDURE (hook: DialogGetColorHook) GetColor (a: Dialog.GetColorAction);
		VAR v: View; red, green, blue: INTEGER; title: Dialog.String;
	BEGIN
		NEW(v);
		v.action := a;
		red := (a.in MOD 256);
		green := ((a.in DIV 256) MOD 256);
		blue := ((a.in DIV 65536) MOD 256);
		RGBtoHSV(red, green, blue, v.hue, v.sat, v.volume);
		IF a.title = "" THEN
			Dialog.MapString("#System:SelectColor", title)
		ELSE
			Dialog.MapString(a.title, title)
		END;
		StdDialog.Open(v, title, NIL, "", NIL, TRUE, FALSE, FALSE, TRUE, TRUE);
			(* asTool, asAux, noResize, allowDuplicates, neverDirty *)
	END GetColor;

	PROCEDURE Init*;
		VAR dh: DialogGetColorHook;
	BEGIN
		NEW(dh);
		Dialog.SetGetColorHook(dh);
	END Init;

END StdColorDialog.
