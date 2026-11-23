MODULE CheckCharts;

	IMPORT Views, Properties, Fonts, Stores, Ports, Controls;
	
	CONST 
		version = 1;
		teal = 7F7F00H;
		arbitrary* = 0; vertical* = 1; horizontal* = 2; northwest* = 3; northeast* = 4;	(* box kinds *)
		lineKinds* = { arbitrary, vertical, horizontal, northwest, northeast };
		stringpr = 0; 	(* property identifiers *)
		sizepr = Properties.size; weightpr = Properties.weight; stylepr = Properties.style;
		thickness* = 0; kind* = 1;
	
	TYPE
		Line = POINTER TO RECORD (Views.View)
			kind, color, thickness: INTEGER;
		END;
		LineProp* = POINTER TO RECORD (Properties.Property)
			thickness*, kind*: INTEGER
		END;
		
		LogosStr = ARRAY 64 OF CHAR;
		Logos = POINTER TO RECORD (Views.View)
			str: LogosStr;
			font: Fonts.Font;
			color: INTEGER;
		END;
		
	(* Line *)
	
	PROCEDURE (p: LineProp) IntersectWith* (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: LineProp DO
			valid := p.valid * q.valid; equal := TRUE;
			IF p.kind # q.kind THEN EXCL(valid, kind) END;
			IF p.thickness # q.thickness THEN EXCL(valid, thickness) END;
			IF p.valid # valid THEN p.valid := valid; equal := FALSE END
		END
	END IntersectWith;
	
	PROCEDURE (v: Line) CopyFromSimpleView- (s: Views.View);
	BEGIN WITH s: Line DO v.color := s.color; v.kind := s.kind; v.thickness := s.thickness END
	END CopyFromSimpleView;

	PROCEDURE (v: Line) Restore (f: Views.Frame; l, t, r, b: INTEGER);
	BEGIN 
		v.context.GetSize(r, b);
		CASE v.kind OF 
		| vertical: r := v.thickness; f.DrawRect(0, 0, r, b, Ports.fill, v.color)
		| horizontal: b := v.thickness; f.DrawRect(0, 0, r, b, Ports.fill, v.color)
		| northeast: f.DrawLine(0, b, r, 0, v.thickness, v.color)
		| northwest: f.DrawLine(0, 0, r, b, v.thickness, v.color)
		| arbitrary: f.DrawRect(0, 0, r, b, v.thickness, v.color)
		ELSE 
		HALT(20) 
		END
	END Restore;
	
	PROCEDURE (v: Line) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(version);
		wr.WriteInt(v.color);
		wr.WriteByte(SHORT(SHORT(v.kind)));
		wr.WriteInt(v.thickness)
	END Externalize;
	
	PROCEDURE (b: Line) Internalize- (VAR rd: Stores.Reader);
		VAR _: INTEGER; byte: BYTE;
	BEGIN
		rd.ReadVersion(version, version, _);
		rd.ReadInt(b.color);
		rd.ReadByte(byte); b.kind := byte; ASSERT(byte IN lineKinds);
		rd.ReadInt(b.thickness) 
	END Internalize;
	
	PROCEDURE (v: Line) HandlePropMsg (VAR msg: Properties.Message);
		VAR p: Properties.Property; std: Properties.StdProp; w, h: INTEGER; upd: BOOLEAN;
			lp: LineProp;
	BEGIN upd := FALSE;
		WITH 
		| msg: Properties.SizePref DO
			IF v.kind = vertical THEN msg.w := v.thickness
			ELSIF v.kind = horizontal THEN msg.h := v.thickness 
			END;
			IF msg.w = Views.undefined THEN msg.w := 20 * Ports.mm END;
			IF msg.h = Views.undefined THEN msg.h := 20 * Ports.mm END
		| msg: Properties.SetMsg DO
			p := msg.prop;
			WHILE p # NIL DO
				WITH 
				| p: Properties.StdProp DO
					IF Properties.color IN p.valid THEN v.color := p.color.val; upd := TRUE END
				| p: LineProp DO
					IF thickness IN p.valid THEN v.thickness := p.thickness; upd := TRUE END;
					IF kind IN p.valid THEN 
						ASSERT(p.kind IN lineKinds); v.kind := p.kind; upd := TRUE 
					END
				ELSE
				END;
				IF upd THEN Views.Update(v, Views.keepFrames) END;
				p := p.next
			END
		| msg: Properties.PollMsg DO
			NEW(std); std.known := { Properties.color }; std.valid := std.known; std.color.val := v.color;
			Properties.Insert(msg.prop, std);
			NEW(lp); lp.valid := { thickness, kind }; lp.known := lp.valid; 
			lp.thickness := v.thickness; lp.kind := v.kind;
			Properties.Insert(msg.prop, lp)
		ELSE
		END
	END HandlePropMsg;

	PROCEDURE NewLine* (vert: BOOLEAN; thickness: INTEGER): Line;
		VAR v: Line;
	BEGIN 
		NEW(v); v.color := 28051978; IF vert THEN v.kind := vertical ELSE v.kind := horizontal END;
		v.thickness := thickness; 
		RETURN v
	END NewLine;
	
	PROCEDURE DepositLine*; BEGIN Views.Deposit(NewLine(TRUE, Ports.mm)) END DepositLine;
	
	PROCEDURE DepositThisLine* (vert: BOOLEAN; thickness: INTEGER);
	BEGIN Views.Deposit(NewLine(vert, thickness)) 
	END DepositThisLine;
	
	
	(* Logos *)
	
	(*PROCEDURE (p: LogosProp) IntersectWith (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: LogosProp DO
			valid := p.valid * q.valid; equal := TRUE;
			IF p.string # q.string THEN EXCL(valid, stringpr) END;
			IF p.valid # valid THEN p.valid := valid; equal := FALSE END
		END
	END IntersectWith;*)
	
	PROCEDURE (v: Logos) CopyFromSimpleView- (source: Views.View);
	BEGIN
		WITH source: Logos DO v.str := source.str; v.font := source.font; v.color := source.color END
	END CopyFromSimpleView;
	
	PROCEDURE (v: Logos) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(version);
		wr.WriteString(v.str);
		wr.WriteString(v.font.typeface);
		wr.WriteInt(v.font.size); wr.WriteInt(v.font.weight); wr.WriteSet(v.font.style);
		wr.WriteInt(v.color)
	END Externalize;
	
	PROCEDURE (v: Logos) Internalize- (VAR rd: Stores.Reader);
		VAR _: INTEGER; typeface: Fonts.Typeface; size, weight: INTEGER; style: SET;
	BEGIN
		rd.ReadVersion(version, version, _);
		rd.ReadString(v.str);
		rd.ReadString(typeface); rd.ReadInt(size); rd.ReadInt(weight); rd.ReadSet(style);
		v.font := Fonts.dir.This(typeface, size, style, weight);
		rd.ReadInt(v.color)
	END Internalize;

	PROCEDURE (lg: Logos) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		VAR asc, dsc, wid: INTEGER;
	BEGIN
		lg.context.GetSize(r, b);
		wid := lg.font.StringWidth(lg.str);
		l := (r - wid) DIV 2;
		lg.font.GetBounds(asc, dsc, wid); 
		t := (b - (asc + dsc)) DIV 2 + asc;
		f.DrawString(l, t, lg.color, lg.str, lg.font)
	END Restore;
	
	PROCEDURE (v: Logos) HandlePropMsg (VAR msg: Properties.Message);
		CONST term = LEN(LogosStr);
		VAR upd: BOOLEAN; dsc, size, weight: INTEGER; style: SET; (*prop: LogosProp; *)
			ctrlProp: Controls.Prop;
			p: Properties.Property; ch: CHAR; std: Properties.StdProp;
	BEGIN upd := FALSE;
		WITH
		| msg: Properties.SetMsg DO
			p := msg.prop;
			WHILE p # NIL DO
				WITH 
				(*| p: LogosProp DO 
					IF stringpr IN p.valid THEN v.str := p.string$; upd := TRUE END;*)
				| p: Properties.StdProp DO
					IF { sizepr, weightpr, stylepr, Properties.color } * p.valid # {} THEN
						IF sizepr IN p.valid THEN size := p.size ELSE size := v.font.size END;
						IF weightpr IN p.valid THEN weight := p.weight ELSE weight := v.font.weight END;
						IF stylepr IN p.valid THEN style := (v.font.style - p.style.mask) + (p.style.mask * p.style.val)
						ELSE style := v.font.style 
						END;
						IF Properties.color IN p.valid THEN v.color := p.color.val END;
						v.font := Fonts.dir.This(v.font.typeface, size, style, weight);
						upd := TRUE
					END
				| p: Controls.Prop DO
					IF Controls.label IN p.valid THEN ch := p.label[term]; p.label[term] := 0X;
						v.str := p.label$; p.label[term] := ch; upd := TRUE
					END
				ELSE
				END;
				p := p.next
			END;
			IF upd THEN Views.Update(v, Views.keepFrames) END
		| msg: Properties.PollMsg DO
			(*NEW(prop); prop.known := { stringpr }; prop.valid := { stringpr }; prop.readOnly := {};
			prop.string := v.str$; *)
			NEW(std); std.known := { sizepr, stylepr, weightpr, Properties.color }; std.valid := std.known;
			std.size := v.font.size; std.weight := v.font.weight; std.color.val := v.color;
			std.style.val := v.font.style; std.style.mask := { Fonts.italic, Fonts.strikeout, Fonts.underline }; 
			NEW(ctrlProp); 
			ctrlProp.known := { Controls.label }; ctrlProp.valid := { Controls.label }; ctrlProp.readOnly := {};
			ctrlProp.label := v.str$;
			(*Properties.Insert(msg.prop, prop); *)Properties.Insert(msg.prop, ctrlProp); Properties.Insert(msg.prop, std)
		| msg: Properties.BoundsPref DO
			v.font.GetBounds(msg.h, dsc, msg.w); INC(msg.h, dsc); msg.w := v.font.StringWidth(v.str)
		| msg: Properties.SizePref DO
			IF msg.h = Views.undefined THEN
				v.font.GetBounds(msg.h, dsc, msg.w); INC(msg.h, dsc); msg.w := v.font.StringWidth(v.str)
			END
		ELSE
		END
	END HandlePropMsg;
	
	PROCEDURE NewLogos* (): Logos;
		VAR lg: Logos;
	BEGIN NEW(lg); lg.font := Fonts.dir.Default(); lg.color := 28051978; lg.str := 'λόγος'; RETURN lg
	END NewLogos;
	
	PROCEDURE DepositLogos*; BEGIN Views.Deposit(NewLogos()) END DepositLogos;
	


END CheckCharts.
