MODULE FigBasic;

	IMPORT
		Ports, Stores, Math, Properties, Models, Dialog, Fonts, 
		FigModels, FigViews;

	CONST
		string* = 0; vpos* = 1; hpos* = 2;
		below* = 0; center* = 1; above* = 2;
		
		currentVersion = 2; minVersion = 0; maxVersion = 2;

	TYPE

	(* Caption *)
	
		Caption* = POINTER TO LIMITED RECORD (FigModels.Figure)
			string: Dialog.String;
			fnt: Fonts.Font;
			col: Ports.Color;
			l, t, r, b, baseline, vpos, hpos: INTEGER
		END;

		CaptionProp* = POINTER TO RECORD(Properties.Property)
			string*: Dialog.String;
			vpos*, hpos*: INTEGER
		END;

		SetCaptionPropOp = POINTER TO RECORD(Stores.Operation)
			caption: Caption;
			old, prop: Properties.Property
		END;

	(* Circle *)
	
		Circle* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			thickness: INTEGER
		END;
		
		SetCirclePropOp = POINTER TO RECORD (Stores.Operation)
			c: Circle;
			prop, old: Properties.Property
		END;

	(* Line *)
	
		Line* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			type: INTEGER;
			thickness: INTEGER
		END;
		
		SetLinePropOp = POINTER TO RECORD (Stores.Operation)
			line: Line;
			old, prop: Properties.Property
		END;

	(* Oval *)
	
		Oval* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			thickness: INTEGER
		END;

		SetOvalPropOp = POINTER TO RECORD (Stores.Operation)
			o: Oval;
			prop, old: Properties.Property
		END;

	(* OpenBezier *)
	
		OpenBezier* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			type: INTEGER;
			thickness: INTEGER
		END;
		
		SetOpenBezierPropOp = POINTER TO RECORD (Stores.Operation)
			fig: OpenBezier;
			old, prop: Properties.Property
		END;
		
	(* ClosedBezier *)
	
		ClosedBezier* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			thickness: INTEGER
		END;

		SetClosedBezierPropOp = POINTER TO RECORD (Stores.Operation)
			f: ClosedBezier;
			prop, old: Properties.Property
		END;
	
	(* Polygon *)

		Polygon* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			thickness: INTEGER
		END;

		SetPolygonPropOp = POINTER TO RECORD (Stores.Operation)
			p: Polygon;
			prop, old: Properties.Property
		END;

	(* Rectangle *)

		Rectangle* = POINTER TO LIMITED RECORD (FigModels.Figure)
			color: Ports.Color;
			thickness: INTEGER
		END;

		SetPropOp = POINTER TO RECORD (Stores.Operation)
			r: Rectangle;
			prop, old: Properties.Property
		END;
		
		
	VAR
		dlgCaption*: RECORD
			string*: Dialog.String;
			pos*: INTEGER
		END;


	(* Caption *)

	PROCEDURE UpdateBox (c: Caption);
		VAR asc, dsc, h, w: INTEGER; str: Dialog.String;
	BEGIN
		Dialog.MapString(c.string, str);
		c.fnt.GetBounds(asc, dsc, w);  h := asc + dsc; w := c.fnt.StringWidth(str);
		IF c.vpos = center THEN c.b := h DIV 2 ELSIF c.vpos = above THEN c.b := 0 ELSE c.b := h END;
		c.t := c.b - h; c.baseline := c.b - dsc;
		IF c.hpos = center THEN c.r := w DIV 2 ELSIF c.hpos = above THEN c.r := w ELSE c.r := 0 END;
		c.l := c.r - w
	END UpdateBox;

	PROCEDURE (p: CaptionProp) IntersectWith* (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: CaptionProp DO
			valid := p.valid * q.valid;
			IF p.string # q.string THEN EXCL(valid, string) END;
			IF p.vpos # q.vpos THEN EXCL(valid, vpos) END;
			IF p.hpos # q.hpos THEN EXCL(valid, hpos) END;
			equal := valid = p.valid; p.valid := valid
		END
	END IntersectWith;

	PROCEDURE GetCaptionProperties (c: Caption; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp; prop: CaptionProp;
	BEGIN
		NEW(stdProp);
		stdProp.valid := {Properties.typeface, Properties.size, Properties.style, Properties.weight, Properties.color};
		stdProp.known := stdProp.valid;
		stdProp.typeface := c.fnt.typeface; stdProp.size := c.fnt.size; stdProp.style.val := c.fnt.style;
		stdProp.style.mask := {Fonts.italic, Fonts.underline, Fonts.strikeout};
		stdProp.weight := c.fnt.weight; stdProp.color.val := c.col;
		p := stdProp;
		NEW(prop); prop.string := c.string; prop.vpos := c.vpos; prop.hpos := c.hpos;
		prop.valid := {string, vpos, hpos}; prop.known := p.valid;
		Properties.Insert(p, prop)
	END GetCaptionProperties;

	PROCEDURE SetCaptionProperties (c: Caption; p: Properties.Property);
		VAR size, weight: INTEGER; style: SET; typeface: Fonts.Typeface;
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.typeface IN p.valid THEN
					typeface := p.typeface
				ELSE
					typeface := c.fnt.typeface
				END;
				IF Properties.size IN p.valid THEN size := p.size ELSE size := c.fnt.size END;
				IF Properties.style IN p.valid THEN style := c.fnt.style - p.style.mask + (p.style.mask * p.style.val)
				ELSE style := c.fnt.style
				END;
				IF Properties.weight IN p.valid THEN weight := p.weight ELSE weight := c.fnt.weight END;
				IF (typeface # c.fnt.typeface) OR (size # c.fnt.size) OR (style # c.fnt.style) OR (weight # c.fnt.weight) THEN
					c.fnt := Fonts.dir.This(typeface, size, style, weight)
				END;
				IF Properties.color IN p.valid THEN c.col := p.color.val END
			| p: CaptionProp DO
				IF string IN p.valid THEN c.string := p.string END;
				IF vpos IN p.valid THEN c.vpos := p.vpos END;
				IF hpos IN p.valid THEN c.hpos := p.hpos END
			ELSE
			END;
			p := p.next
		END
	END SetCaptionProperties;
	
	(* SetPropOp *)

	PROCEDURE (op: SetCaptionPropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetCaptionProperties(op.caption, p); UpdateBox(op.caption);
		Models.Broadcast(op.caption.Model(), msg)
	END Do;
	

	PROCEDURE (c: Caption) Internalize- (VAR rd: Stores.Reader);
		VAR version, size, weight: INTEGER; style: SET; typeface: Fonts.Typeface;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadString(c.string);
			rd.ReadString(typeface); rd.ReadInt(size); rd.ReadSet(style); rd.ReadInt(weight);
			c.fnt := Fonts.dir.This(typeface, size, style, weight);
			rd.ReadInt(c.col); rd.ReadInt(c.vpos); rd.ReadInt(c.hpos);
			UpdateBox(c)
		END
	END Internalize;

	PROCEDURE (c: Caption) Externalize- (VAR wr: Stores.Writer);
		VAR f: Fonts.Font;
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteString(c.string);
		f := c.fnt;
		wr.WriteString(f.typeface); wr.WriteInt(f.size); wr.WriteSet(f.style); wr.WriteInt(f.weight);
		wr.WriteInt(c.col); wr.WriteInt(c.vpos); wr.WriteInt(c.hpos)
	END Externalize;

	PROCEDURE (c: Caption) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Caption DO
			c.string := source.string; c.fnt := source.fnt; c.col := source.col;
			c.l := source.l; c.t := source.t; c.r := source.r; c.b := source.b; c.baseline := source.baseline;
			c.vpos := source.vpos; c.hpos := source.hpos
		END
	END CopyFrom;

	PROCEDURE (c: Caption) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR fnt: Fonts.Font; str: Dialog.String;
	BEGIN
		fnt := c.fnt;
		Dialog.MapString(c.string, str);
		f.DrawString(c.l + c.Anchors().This().X(), c.baseline + c.Anchors().This().Y(), c.col, str, fnt)
	END Draw;

	PROCEDURE (c: Caption) GetBoundingBox* (OUT l, t, r, b: INTEGER);
	BEGIN
		l := c.Anchors().This().X() + c.l; t := c.Anchors().This().Y() + c.t;
		r := c.Anchors().This().X() + c.r; b := c.Anchors().This().Y() + c.b
	END GetBoundingBox;

	PROCEDURE (c: Caption) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetCaptionProperties(c, p)
	END PollProp;

	PROCEDURE (c: Caption) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN; op: SetCaptionPropOp; prop, old: Properties.Property;
	BEGIN
		GetCaptionProperties(c, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.caption := c; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(c.Model(), "Set Properties", op)
		END
	END SetProp;
	
	PROCEDURE (c: Caption) Action*;
		VAR res: INTEGER; cmd: Dialog.String;
	BEGIN
		Dialog.MapString("#Fig:Captions.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;
	

	PROCEDURE NewCaption* (string: Dialog.String; fnt: Fonts.Font; col: Ports.Color; vpos, hpos: INTEGER): Caption;
		VAR c: Caption;
	BEGIN
		NEW(c); c.string := string; c.fnt := fnt; c.col := col; c.vpos := vpos; c.hpos := hpos; UpdateBox(c);
		RETURN c
	END NewCaption;

	PROCEDURE InsertCaption*;
		VAR m: FigModels.Model; p: FigModels.PointList; v: FigViews.View; c: Caption;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) # 0 THEN
				v.PopPoints(1, p);
				c := NewCaption("Caption", Fonts.dir.Default(), Ports.defaultColor, center, center);
				m.InsertObject(c, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(c)
			END
		END
	END InsertCaption;

	(* Property Editor *)

	PROCEDURE InitCaptionDialog*;
		VAR p: Properties.Property;
	BEGIN
		dlgCaption.string := ""; dlgCaption.pos := -1;
		Properties.CollectProp(p);
		WHILE (p # NIL) & ~(p IS CaptionProp) DO p := p.next END;
		IF p # NIL THEN
			WITH p: CaptionProp DO
				IF string IN p.valid THEN dlgCaption.string := p.string END;
				IF (vpos IN p.valid) & (hpos IN p.valid) THEN dlgCaption.pos := p.vpos + 4 * p.hpos END
			END
		END;
		Dialog.Update(dlgCaption)
	END InitCaptionDialog;

	PROCEDURE SetCaption*;
		VAR p: CaptionProp;
	BEGIN
		NEW(p);
		p.string := dlgCaption.string; p.vpos := dlgCaption.pos MOD 4; p.hpos := dlgCaption.pos DIV 4;
		p.known := {string, vpos, hpos}; p.valid := p.known;
		IF dlgCaption.string = "" THEN EXCL(p.valid, string) END;
		IF ((p.vpos # below) & (p.vpos # center) & (p.vpos # above))
			OR ((p.hpos # below) & (p.hpos # center) & (p.hpos # above))
		THEN EXCL(p.valid, vpos); EXCL(p.valid, hpos)
		END;
		IF p.valid # {} THEN Properties.EmitProp(NIL, p) END
	END SetCaption;
	
	
	
	
	(* Circle *)
				
	PROCEDURE (c: Circle) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Circle DO
			c.color := source.color;
			c.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (c: Circle) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(c.color);
		wr.WriteInt(c.thickness)
	END Externalize;

	PROCEDURE (c: Circle) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(c.color);
			IF version > 0 THEN rd.ReadInt(c.thickness) ELSE c.thickness := 0 END
		END
	END Internalize;

	PROCEDURE (c: Circle) Covers* (x, y: INTEGER): BOOLEAN;
		VAR radius, radius2, x0, x1, y0, y1, t: INTEGER;
	BEGIN
		x0 := c.Anchors().Next().This().X(); y0 := c.Anchors().Next().This().Y();
		x1 := c.Anchors().This().X(); y1 := c.Anchors().This().Y();
		radius := SHORT(ENTIER(Math.Sqrt(Math.IntPower(x0 - x1, 2) + Math.IntPower(y0 - y1, 2))));
		x0 := c.Anchors().Next().This().X(); y0 := c.Anchors().Next().This().Y();
		x1 := x; y1 := y;
		radius2 := SHORT(ENTIER(Math.Sqrt(Math.IntPower(x0 - x1, 2) + Math.IntPower(y0 - y1, 2))));
		IF c.thickness = Ports.fill THEN RETURN radius2 <= radius + Ports. mm
		ELSE
			t := c.thickness DIV 2; radius := radius - t;
			RETURN (radius2 <= radius + t + Ports.mm) & (radius2 >= radius - t - Ports.mm)
		END
	END Covers;

	PROCEDURE (c: Circle) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR radius, x0, x1, y0, y1: INTEGER;
	BEGIN
		x0 := c.Anchors().Next().This().X(); y0 := c.Anchors().Next().This().Y();
		x1 := c.Anchors().This().X(); y1 := c.Anchors().This().Y();
		radius := SHORT(ENTIER(Math.Sqrt(Math.IntPower(x0 - x1, 2) + Math.IntPower(y0 - y1, 2))));
		f.DrawOval(x0 - radius, y0 - radius, x0 + radius, y0 + radius, c.thickness, c.color)  
	END Draw;
	
	PROCEDURE (c: Circle) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR radius, tmp: INTEGER;
	BEGIN
		radius := SHORT(ENTIER(Math.Sqrt(Math.IntPower(c.Anchors().Next().This().X() - c.Anchors().This().X(), 2) + Math.IntPower(c.Anchors().Next().This().Y() - c.Anchors().This().Y(), 2))));
		l := c.Anchors().Next().This().X() - radius; t := c.Anchors().Next().This().Y() - radius;
		r := c.Anchors().Next().This().X() + radius; b := c.Anchors().Next().This().Y() + radius;
		IF (r - l) < Ports.mm * 3 THEN tmp := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, tmp); INC(r, tmp) END;
		IF (b - t) < Ports.mm * 3 THEN tmp := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, tmp); INC(b, tmp) END
	END GetBoundingBox;

	PROCEDURE GetCircleProperties (c: Circle; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp;
			TProp: FigModels.TProp;
	BEGIN
		NEW(stdProp); stdProp.color.val := c.color; stdProp.known := {Properties.color}; stdProp.valid := stdProp.known;
		Properties.Insert(p, stdProp);
		NEW(TProp); TProp.thickness := c.thickness; TProp.known := {FigModels.thickness}; TProp.valid := TProp.known;
		Properties.Insert(p, TProp)
	END GetCircleProperties;

	PROCEDURE (c: Circle) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetCircleProperties(c, p)
	END PollProp;

	PROCEDURE SetCircleProperties (c: Circle; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN c.color := p.color.val END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN c.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetCircleProperties;
	
	PROCEDURE (op: SetCirclePropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetCircleProperties(op.c, p);
		Models.Broadcast(op.c.Model(), msg)
	END Do;
	
	PROCEDURE (c: Circle) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetCirclePropOp;
			prop, old: Properties.Property;
	BEGIN
		GetCircleProperties(c, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.c := c; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(c.Model(), "SetProperties", op)
		END
	END SetProp;

	PROCEDURE (c: Circle) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Thickness.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;
	
	PROCEDURE NewCircle* (col: Ports.Color; thickness: INTEGER): Circle;
		VAR c: Circle;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(c); c.color := col; c.thickness := thickness; RETURN c
	END NewCircle;

	PROCEDURE  InsertCircle*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; c: Circle;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF (v.NofSelectedObjects(FigViews.points) >= 2) THEN
				v.PopPoints(2, p); c := NewCircle(Ports.defaultColor, 0);
				m.InsertObject(c, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(c)
			END	
		END
	END InsertCircle;
	
	
	
	
	(* Line *)
	
	PROCEDURE GetLineProperties (line: Line; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp;
			braheProp: FigModels.StdProp;
			prop:FigModels.ArrowProp;
			TProp: FigModels.TProp;
	BEGIN
		NEW(stdProp);
		stdProp.color.val := line.color;
		stdProp.valid := {Properties.color}; stdProp.known := {Properties.color};
		Properties.Insert(p, stdProp);
		NEW(braheProp);
		braheProp.maxAnchors := FigModels.undefined; braheProp.minAnchors := 2;
		braheProp.expandable := TRUE; braheProp.cyclic := FALSE;
		braheProp.valid := {FigModels.expandable, FigModels.cyclic, FigModels.minAnchors, 
										FigModels.maxAnchors};
		braheProp.known := braheProp.valid;
		Properties.Insert(p, braheProp);
		NEW(prop); prop.type := line.type; prop.valid := {FigModels.type}; prop.known := prop.valid;
		Properties.Insert(p, prop);
		NEW(TProp); TProp.thickness := line.thickness;
		TProp.known := {FigModels.thickness}; TProp.valid := TProp.known;
		Properties.Insert(p, TProp)
	END GetLineProperties;

	PROCEDURE SetLineProperties (line: Line; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN line.color := p.color.val END
			| p: FigModels.ArrowProp DO
				IF FigModels.type IN p.valid THEN line.type := p.type END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN line.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetLineProperties;
	
	(* SetPropOp *)
	
	PROCEDURE (op: SetLinePropOp) Do;
		VAR p: Properties.Property;
			msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetLineProperties(op.line, p);
		Models.Broadcast(op.line.Model(), msg)
	END Do;
	
	PROCEDURE (line: Line) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Line DO
			line.color := source.color;
			line.type := source.type;
			line.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (line: Line) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(line.color);
		wr.WriteInt(line.type);
		wr.WriteInt(line.thickness)
	END Externalize;

	PROCEDURE (line: Line) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(line.color);
			IF version = 0 THEN
				line.type := FigModels.noArrow;
				line.thickness := 0
			ELSIF version = 1 THEN
				rd.ReadInt(line.type);
				line.thickness := 0
			ELSIF version > 1 THEN
				rd.ReadInt(line.type);
				rd.ReadInt(line.thickness)
			END
		END
	END Internalize;

	PROCEDURE (line: Line) Covers* (x, y: INTEGER): BOOLEAN;
		VAR r2: REAL; p: FigModels.PointList; a, b: FigModels.Point;

		PROCEDURE IsClose (x0, y0: INTEGER; a, b: FigModels.Point; r2: REAL): BOOLEAN;
			VAR x, y, dx, dy, ax, ay, bx, by, f, d: REAL;
		BEGIN
			ax := a.X(); ay := a.Y(); bx := b.X(); by := b.Y();
			dx := bx - ax; dy := by - ay;
			IF (dx # 0) OR (dy # 0) THEN
				f := (dx * (x0 - ax) + dy * (y0 - ay)) / (dx * dx + dy * dy);
				x := ax + dx * f; y := ay + dy * f
			ELSE x := ax; y := ay
			END;
			d := Math.IntPower(x0 - x, 2) + Math.IntPower(y0 - y, 2);
			IF d < r2 THEN
				IF bx # x THEN RETURN (x - ax) / (bx - x) >= 0
				ELSIF by # y THEN RETURN (y - ay) / (by - y) >= 0
				ELSE RETURN TRUE
				END
			ELSE RETURN FALSE
			END
		END IsClose;

	BEGIN
		r2 := Math.IntPower(line.thickness DIV 2 + 2 * Ports.mm, 2);
		p := line.Anchors(); a := p.This(); p := p.Next(); b := p.This();
		WHILE (p # NIL) & ~IsClose(x, y, a, b, r2) DO
			a := b; p := p.Next();
			IF p # NIL THEN b := p.This() END
		END;
		RETURN p # NIL
	END Covers;

	PROCEDURE (line: Line) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR p: FigModels.PointList;
			arrLen, i, n, newX, newY, dx, dy: INTEGER;
			arrAngle, angle: REAL;
			A: POINTER TO ARRAY OF Ports.Point;
			
		PROCEDURE Sign (x: INTEGER ) : INTEGER;
		BEGIN IF x < 0 THEN RETURN - 1 ELSE RETURN 1 END
		END Sign;

		PROCEDURE GetPoint (x, y, dx, dy_: INTEGER; angle: REAL; len: INTEGER; VAR aX, aY: INTEGER);
		BEGIN
			aX := SHORT(x - ENTIER(Math.Cos(angle) * len) * Sign(dx));
			aY := SHORT(y - ENTIER(Math.Sin(angle) * len) * Sign(dx))
		END GetPoint;

		PROCEDURE DrawArrow (f: Ports.Frame; x1, y1, x2, y2, len: INTEGER; col: Ports.Color);
			VAR angle: REAL;
				dx, dy: INTEGER;
				ax1, ay1, ax2, ay2: INTEGER;
				A: ARRAY 3 OF Ports.Point;
		BEGIN
			dx := x2 - x1; dy := y2 - y1;
			IF dx # 0 THEN angle := Math.ArcTan (dy / dx) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint (x2, y2, dx, dy, angle - arrAngle / 2, len, ax1, ay1);
			GetPoint (x2, y2, dx, dy, angle + arrAngle / 2, len, ax2, ay2);
			A[0].x := ax1; A[0].y := ay1; A[1].x := x2; A[1].y := y2; A[2].x := ax2; A[2].y := ay2;
			f.DrawPath(A, 3, Ports.fill, col, Ports.closedPoly)
		END DrawArrow;

	BEGIN
		arrLen := line.thickness * 2; arrAngle := Math.Pi() / 3;
		IF arrLen < Ports.mm * 4 THEN arrLen := Ports.mm * 4 END;
		p := line.Anchors(); i := 0; n := 0;
		WHILE p # NIL DO INC(n); p := p.Next() END;
		p := line.Anchors(); NEW(A, n);
		WHILE p # NIL DO A[i].x := p.This().X(); A[i].y :=p.This().Y(); p := p.Next(); INC(i) END;
		IF (line.type = FigModels.beginArrow) OR (line.type = FigModels.doubleArrow) THEN
			DrawArrow(f, A[1].x, A[1].y, A[0].x, A[0].y, arrLen, line.color);
			dx := A[0].x - A[1].x; dy := A[0].y - A[1].y;
			IF dx # 0 THEN angle := Math.ArcTan (dy / dx) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint(A[0].x, A[0].y, dx, dy, angle, arrLen - Ports.mm * 2, newX, newY);
			A[0].x := newX; A[0].y := newY
		END;
		IF (line.type = FigModels.endArrow) OR (line.type = FigModels.doubleArrow) THEN
			DrawArrow(f, A[n - 2].x, A[n - 2].y, A[n - 1].x, A[n - 1].y, arrLen, line.color);
			dx := A[n - 1].x - A[n - 2].x; dy := A[n - 1].y - A[n - 2].y;
			IF dx # 0 THEN angle := Math.ArcTan (dy / dx) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint(A[n - 1].x, A[n - 1].y, dx, dy, angle, arrLen - Ports.mm * 2, newX, newY);
			A[n - 1].x := newX; A[n - 1].y := newY
		END;
		f.DrawPath(A, i, line.thickness, line.color, Ports.openPoly)
	END Draw;

	PROCEDURE (line: Line) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR x: INTEGER;
			plist: FigModels.PointList;
	BEGIN
		plist := line.Anchors();	
		IF plist # NIL THEN
			l := plist.This().X(); r := plist.This().X(); t := plist.This().Y(); b := plist.This().Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				l := MIN(l, plist.This().X()); r := MAX(r, plist.This().X());
				t := MIN(t, plist.This().Y()); b := MAX(b, plist.This().Y());
				plist := plist.Next()
			END
		END;
		IF line.thickness > 0 THEN x := line.thickness DIV 2; DEC(l, x); DEC(t, x); INC(r, x); INC(b, x) END;
		IF line.type # FigModels.noArrow THEN
			x := line.thickness DIV 2;
			IF x < Ports.mm * 2 THEN x := Ports.mm *2 END;
			DEC(l, x); DEC(t, x); INC(r, x); INC(b, x)
		END;
		IF (r - l) < Ports.mm * 3 THEN x := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, x); INC(r, x) END;
		IF (b - t) < Ports.mm * 3 THEN x := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, x); INC(b, x) END
	END GetBoundingBox;

	PROCEDURE (line: Line) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetLineProperties(line, p)
	END PollProp;
	
	PROCEDURE (line: Line) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetLinePropOp;
			prop, old: Properties.Property;
	BEGIN
		GetLineProperties(line, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.line := line; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(line.Model(), "Set Properties", op)
		END
	END SetProp;

	PROCEDURE (line: Line) Action*;
		VAR res: INTEGER; cmd: Dialog.String;
	BEGIN
		Dialog.MapString("#Fig:Line.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE NewLine* (col: Ports.Color; type, thickness: INTEGER): Line;
		VAR l: Line;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(l); l.color := col; l.type := type; l.thickness := thickness; RETURN l
	END NewLine;

	PROCEDURE  InsertLine*;
		VAR nrofsel: INTEGER; v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; l: Line;
	BEGIN
		v:=FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			nrofsel := v.NofSelectedObjects(FigViews.points);
			IF nrofsel >= 2 THEN
				v.PopPoints(nrofsel, p);
				l := NewLine(Ports.defaultColor, FigModels.noArrow, 0);
				m.InsertObject(l, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(l)
			END
		END
	END InsertLine;
	


	(* Oval *)


	PROCEDURE (o: Oval) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Oval DO
			o.color := source.color; o.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (o: Oval) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(o.color); wr.WriteInt(o.thickness)
	END Externalize;

	PROCEDURE (o: Oval) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(o.color); rd.ReadInt(o.thickness)
		END
	END Internalize;

	PROCEDURE GetBounds (o: Oval; OUT l, t, r, b: INTEGER);
		VAR h: INTEGER; p: FigModels.Point;
	BEGIN
		p := o.Anchors().This(); l := p.X(); t := p.Y();
		p := o.Anchors().Next().This(); r := p.X(); b := p.Y();
		IF l > r THEN h := l; l := r; r := h END;
		IF t > b THEN h := t; t := b; b := h END
	END GetBounds;

	PROCEDURE (o: Oval) Covers* (x, y: INTEGER): BOOLEAN;
		VAR l, t, r, b, d: INTEGER;
	BEGIN
		GetBounds(o, l, t, r, b); x := x - (l + r) DIV 2; y := y - (t + b) DIV 2;
		IF o.thickness # Ports.fill THEN
			d := o.thickness + 2 * Ports.mm;
			RETURN (Math.IntPower(2 * x / (r - l - 2 * d), 2) + Math.IntPower(2 * y / (b - t - d), 2) >= 1)
						& (Math.IntPower(2 * x / (r - l + 2 * Ports.mm), 2) + Math.IntPower(2 * y / (b - t + 2 * Ports.mm), 2) <= 1)
		ELSE
			RETURN (Math.IntPower(2 * x / (r - l + 2 * Ports.mm), 2) + Math.IntPower(2 * y / (b - t + 2 * Ports.mm), 2) <= 1)
		END
	END Covers;

	PROCEDURE (o: Oval) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR x0, y0, x1, y1: INTEGER;
	BEGIN
		GetBounds(o, x0, y0, x1, y1); f.DrawOval(x0, y0, x1, y1, o.thickness, o.color)
	END Draw;
	
	PROCEDURE (o: Oval) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR h: INTEGER;
	BEGIN
		GetBounds(o, l, t, r, b);
		IF r - l < Ports.mm * 3 THEN h := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, h); INC(r, h) END;
		IF b - t < Ports.mm * 3 THEN h := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, h); INC(b, h) END
	END GetBoundingBox;

	PROCEDURE GetOvalProperties (o: Oval; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp; tProp: FigModels.TProp;
	BEGIN
		NEW(stdProp);
		stdProp.color.val := o.color; stdProp.known := {Properties.color}; stdProp.valid := stdProp.known;
		Properties.Insert(p, stdProp);
		NEW(tProp);
		tProp.thickness := o.thickness; tProp.known := {FigModels.thickness}; tProp.valid := tProp.known;
		Properties.Insert(p, tProp)
	END GetOvalProperties;

	PROCEDURE (o: Oval) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetOvalProperties(o, p)
	END PollProp;
	
	PROCEDURE SetOvalProperties (o: Oval; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN o.color := p.color.val END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN o.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetOvalProperties;
	
	PROCEDURE (op: SetOvalPropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetOvalProperties(op.o, p);
		Models.Broadcast(op.o.Model(), msg)
	END Do;
	
	PROCEDURE (o: Oval) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN; op: SetOvalPropOp; prop, old: Properties.Property;
	BEGIN
		GetOvalProperties(o, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.o := o; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(o.Model(), "Set Properties", op)
		END
	END SetProp;

	PROCEDURE (o: Oval) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Thickness.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE NewOval* (col: Ports.Color; thickness: INTEGER): Oval;
		VAR o: Oval;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(o); o.color := col; o.thickness := thickness; RETURN o
	END NewOval;

	PROCEDURE  InsertOval*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; o: Oval;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel(); 		
			IF v.NofSelectedObjects(FigViews.points) >= 2 THEN
				v.PopPoints(2, p); o := NewOval(Ports.defaultColor, 0);
				m.InsertObject(o, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(o)
			END
		END
	END InsertOval;


	(* OpenBezier *)
	
	PROCEDURE GetOpenBezierProperties (fig: OpenBezier; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp;
			prop: FigModels.ArrowProp;
			TProp: FigModels.TProp;
	BEGIN
		NEW(stdProp); stdProp.color.val := fig.color;
		stdProp.valid := {Properties.color}; stdProp.known := {Properties.color};
		Properties.Insert(p, stdProp);
		NEW(prop); prop.type := fig.type; prop.valid := {FigModels.type}; prop.known := prop.valid;
		Properties.Insert(p, prop);
		NEW(TProp); TProp.thickness := fig.thickness;
		TProp.known := {FigModels.thickness}; TProp.valid := TProp.known;
		Properties.Insert(p, TProp)
	END GetOpenBezierProperties;

	PROCEDURE SetOpenBezierProperties (fig: OpenBezier; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN fig.color := p.color.val END
			| p: FigModels.ArrowProp DO
				IF FigModels.type IN p.valid THEN fig.type := p.type END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN fig.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetOpenBezierProperties;
	
	(* SetOpenBezierPropOp *)
	
	PROCEDURE (op: SetOpenBezierPropOp) Do;
		VAR p: Properties.Property;
			msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetOpenBezierProperties(op.fig, p);
		Models.Broadcast(op.fig.Model(), msg)
	END Do;
	
	PROCEDURE (fig: OpenBezier) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: OpenBezier DO
			fig.color := source.color;
			fig.type := source.type;
			fig.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (fig: OpenBezier) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(fig.color);
		wr.WriteInt(fig.type);
		wr.WriteInt(fig.thickness)
	END Externalize;

	PROCEDURE (fig: OpenBezier) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(fig.color);
			rd.ReadInt(fig.type);
			rd.ReadInt(fig.thickness)
		END
	END Internalize;


	PROCEDURE (fig: OpenBezier) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR p: FigModels.PointList;
			arrLen, i, n, newX, newY, dx, dy: INTEGER;
			arrAngle, angle: REAL;
			A: POINTER TO ARRAY OF Ports.Point;
			
		PROCEDURE Sign (x: INTEGER ) : INTEGER;
		BEGIN IF x < 0 THEN RETURN - 1 ELSE RETURN 1 END
		END Sign;

		PROCEDURE GetPoint (x, y, dx, dy_: INTEGER; angle: REAL; len: INTEGER; VAR aX, aY: INTEGER);
		BEGIN
			aX := SHORT(x - ENTIER(Math.Cos(angle) * len) * Sign(dx));
			aY := SHORT(y - ENTIER(Math.Sin(angle) * len) * Sign(dx))
		END GetPoint;

		PROCEDURE DrawArrow (f: Ports.Frame; x1, y1, x2, y2, len: INTEGER; col: Ports.Color);
			VAR angle: REAL;
				dx, dy: INTEGER;
				ax1, ay1, ax2, ay2: INTEGER;
				A: ARRAY 3 OF Ports.Point;
		BEGIN
			dx := x2 - x1; dy := y2 - y1;
			IF dx # 0 THEN angle := Math.ArcTan ( dy / dx ) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint ( x2, y2, dx, dy, angle - arrAngle / 2, len, ax1, ay1);
			GetPoint ( x2, y2, dx, dy, angle + arrAngle / 2, len, ax2, ay2);
			A[0].x := ax1; A[0].y := ay1; A[1].x := x2; A[1].y := y2; A[2].x := ax2; A[2].y := ay2;
			f.DrawPath(A, 3, Ports.fill, col, Ports.closedPoly)
		END DrawArrow;

	BEGIN
		arrLen := fig.thickness * 2; arrAngle := Math.Pi() / 3;
		IF arrLen < Ports.mm * 4 THEN arrLen := Ports.mm * 4 END;
		p := fig.Anchors(); i := 0; n := 0;
		WHILE p # NIL DO INC(n); p := p.Next() END;
		p := fig.Anchors(); NEW(A, n);
		WHILE p # NIL DO A[i].x := p.This().X(); A[i].y :=p.This().Y(); p := p.Next(); INC(i) END;
		IF (fig.type = FigModels.beginArrow) OR (fig.type = FigModels.doubleArrow) THEN
			DrawArrow(f, A[1].x, A[1].y, A[0].x, A[0].y, arrLen, fig.color);
			dx := A[0].x - A[1].x; dy := A[0].y - A[1].y;
			IF dx # 0 THEN angle := Math.ArcTan ( dy / dx ) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint(A[0].x, A[0].y, dx, dy, angle, arrLen - Ports.mm * 2, newX, newY);
			A[0].x := newX; A[0].y := newY
		END;
		IF (fig.type = FigModels.endArrow) OR (fig.type = FigModels.doubleArrow) THEN
			DrawArrow(f, A[n - 2].x, A[n - 2].y, A[n - 1].x, A[n - 1].y, arrLen, fig.color);
			dx := A[n - 1].x - A[n - 2].x; dy := A[n - 1].y - A[n - 2].y;
			IF dx # 0 THEN angle := Math.ArcTan ( dy / dx ) ELSE angle := Sign (dy) * (Math.Pi() / 2) END;
			GetPoint(A[n - 1].x, A[n - 1].y, dx, dy, angle, arrLen - Ports.mm * 2, newX, newY);
			A[n - 1].x := newX; A[n - 1].y := newY
		END;
		f.DrawPath(A, i, fig.thickness, fig.color, Ports.openBezier)
	END Draw;

	PROCEDURE (fig: OpenBezier) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR x: INTEGER;
			plist: FigModels.PointList;
	BEGIN
		plist := fig.Anchors();	
		IF plist # NIL THEN
			l := plist.This().X(); r := plist.This().X(); t := plist.This().Y(); b := plist.This().Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				l := MIN(l, plist.This().X()); r := MAX(r, plist.This().X());
				t := MIN(t, plist.This().Y()); b := MAX(b, plist.This().Y());
				plist := plist.Next()
			END
		END;
		IF fig.thickness > 0 THEN x := fig.thickness DIV 2; DEC(l, x); DEC(t, x); INC(r, x); INC(b, x) END;
		IF (r - l) < Ports.mm * 3 THEN x := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, x); INC(r, x) END;
		IF (b - t) < Ports.mm * 3 THEN x := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, x); INC(b, x) END
	END GetBoundingBox;

	PROCEDURE (fig: OpenBezier) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetOpenBezierProperties(fig, p)
	END PollProp;
	
	PROCEDURE (fig: OpenBezier) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetOpenBezierPropOp;
			prop, old: Properties.Property;
	BEGIN
		GetOpenBezierProperties(fig, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.fig := fig; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(fig.Model(), "Set Properties", op)
		END
	END SetProp;

	PROCEDURE (fig: OpenBezier) Action*;
		VAR res: INTEGER; cmd: Dialog.String;
	BEGIN
		Dialog.MapString("#Fig:Bezier.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE NewOpenBezier* (col: Ports.Color; type, thickness: INTEGER): OpenBezier;
		VAR f: OpenBezier;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(f); f.color := col; f.type := type; f.thickness := thickness; RETURN f
	END NewOpenBezier;

	PROCEDURE  InsertOpenBezier*;
		VAR nrofsel: INTEGER;
			v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; f: OpenBezier;
	BEGIN
		v:=FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			nrofsel := v.NofSelectedObjects(FigViews.points);
			IF nrofsel >= 2 THEN
				v.PopPoints(nrofsel, p); f := NewOpenBezier(Ports.defaultColor, FigModels.noArrow, 0);
				m.InsertObject(f, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(f)
			END
		END
	END InsertOpenBezier;

	PROCEDURE InsertOpenBezierGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
			n: INTEGER;
	BEGIN
		v := FigViews.Focus(); IF v # NIL THEN n := v.NofSelectedObjects(FigViews.points) END;
		par.disabled := (v = NIL) OR (n < 4) OR (n MOD 3 # 1)
	END InsertOpenBezierGuard;


	(* ClosedBezier *)

	PROCEDURE (fig: ClosedBezier) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: ClosedBezier DO
			fig.color := source.color;
			fig.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (fig: ClosedBezier) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(fig.color);
		wr.WriteInt(fig.thickness)
	END Externalize;

	PROCEDURE (fig: ClosedBezier) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(fig.color);
			rd.ReadInt(fig.thickness)
		END
	END Internalize;
		
	PROCEDURE (fig: ClosedBezier) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR p: FigModels.PointList;
			A: POINTER TO ARRAY OF Ports.Point;
			i, n: INTEGER;
	BEGIN
		p := fig.Anchors(); i := 0; n := 0;
		WHILE p # NIL DO INC(n); p := p.Next() END;
		p := fig.Anchors(); NEW(A, n);
		WHILE p # NIL DO A[i].x := p.This().X(); A[i].y :=p.This().Y(); p := p.Next(); INC(i) END;
		f.DrawPath(A, i, fig.thickness, fig.color, Ports.closedBezier)
	END Draw;

	PROCEDURE (fig: ClosedBezier) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR x: INTEGER;
			plist: FigModels.PointList;
	BEGIN
		plist := fig.Anchors();
		IF plist # NIL THEN
			l := plist.This().X(); r := plist.This().X();
			t := plist.This().Y(); b := plist.This().Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				l := MIN(l, plist.This().X()); r := MAX(r, plist.This().X());
				t := MIN(t, plist.This().Y()); b := MAX(b, plist.This().Y());
				plist := plist.Next()
			END
		END;
		IF fig.thickness > 0 THEN x := fig.thickness DIV 2; DEC(l, x); DEC(t, x); INC(r, x); INC(b, x) END;
		IF (r - l) < Ports.mm * 3 THEN x := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, x); INC(r, x) END;
		IF (b - t) < Ports.mm * 3 THEN x := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, x); INC(b, x) END
	END GetBoundingBox;

	PROCEDURE GetProperties (fig: ClosedBezier; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp;
			TProp: FigModels.TProp;
	BEGIN
		NEW(stdProp);
		stdProp.color.val := fig.color;
		stdProp.known := {Properties.color}; stdProp.valid := stdProp.known;
		Properties.Insert(p, stdProp);
		NEW(TProp);
		TProp.thickness := fig.thickness;
		TProp.known := {FigModels.thickness}; TProp.valid := TProp.known;
		Properties.Insert(p, TProp)
	END GetProperties;

	PROCEDURE (fig: ClosedBezier) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetProperties(fig, p)
	END PollProp;

	PROCEDURE SetProperties (fig: ClosedBezier; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN fig.color := p.color.val END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN fig.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetProperties;

	PROCEDURE (op: SetClosedBezierPropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetProperties(op.f, p);
		Models.Broadcast(op.f.Model(), msg)
	END Do;

	PROCEDURE (fig: ClosedBezier) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetClosedBezierPropOp;
			prop, old: Properties.Property;
	BEGIN
		GetProperties(fig, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.f := fig; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(fig.Model(), "SetProperties", op)
		END
	END SetProp;

	PROCEDURE (fig: ClosedBezier) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Thickness.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE NewClosedBezier* (col: Ports.Color; thickness: INTEGER): ClosedBezier;
		VAR f: ClosedBezier;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(f); f.color := col; f.thickness := thickness; RETURN f
	END NewClosedBezier;

	PROCEDURE  InsertClosedBezier*;
		VAR nrofsel: INTEGER;
			v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; f: ClosedBezier;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel(); nrofsel := v.NofSelectedObjects(FigViews.points);
			IF nrofsel >= 3 THEN
				v.PopPoints(nrofsel, p); f := NewClosedBezier(Ports.defaultColor, 0);
				m.InsertObject(f, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(f)
			END
		END
	END InsertClosedBezier;

	PROCEDURE InsertClosedBezierGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
			n: INTEGER;
	BEGIN
		v := FigViews.Focus(); IF v # NIL THEN n := v.NofSelectedObjects(FigViews.points) END;
		par.disabled := (v = NIL) OR (n < 3) OR (n MOD 3 # 0)
	END InsertClosedBezierGuard;


	(* Polygon *)

	PROCEDURE (fig: Polygon) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Polygon DO
			fig.color := source.color;
			fig.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (fig: Polygon) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(fig.color);
		wr.WriteInt(fig.thickness)
	END Externalize;

	PROCEDURE (fig: Polygon) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(fig.color);
			IF version > 0 THEN rd.ReadInt(fig.thickness) ELSE fig.thickness := 0 END
		END
	END Internalize;

	PROCEDURE (fig: Polygon) Covers * (x, y: INTEGER): BOOLEAN;
		VAR r2: REAL; p: FigModels.PointList; a, b: FigModels.Point;
			l, t, r, bot: INTEGER;

		PROCEDURE IsClose (x0, y0: INTEGER; a, b: FigModels.Point; r2: REAL): BOOLEAN;
			VAR x, y, dx, dy, f, d: REAL;
		BEGIN
			dx := b.X() - a.X(); dy := b.Y() - a.Y();
			IF (dx # 0) OR (dy # 0) THEN
				f := (dx * (x0 - a.X()) + dy * (y0 - a.Y())) / (dx * dx + dy * dy);
				x := a.X() + dx * f; y := a.Y() + dy * f
			ELSE x := a.X(); y := a.Y()
			END;
			d := Math.IntPower(x0 - x, 2) + Math.IntPower(y0 - y, 2);
			IF d < r2 THEN
				IF b.X() # x THEN RETURN (x - a.X()) / (b.X() - x) >= 0
				ELSIF b.Y() # y THEN RETURN (y - a.Y()) / (b.Y() - y) >= 0
				ELSE RETURN TRUE
				END
			ELSE RETURN FALSE
			END
		END IsClose;

	BEGIN
		IF fig.thickness # Ports.fill THEN
			r2 := Math.IntPower(fig.thickness DIV 2 + 2 * Ports.mm, 2);
			p := fig.Anchors(); a := p.This(); p := p.Next(); b := p.This();
			WHILE (p # NIL) & ~IsClose(x, y, a, b, r2) DO
				a := b; p := p.Next(); IF p # NIL THEN b := p.This() END
			END;
			IF p # NIL THEN
				RETURN p # NIL
			ELSE
				a := fig.Anchors().This(); RETURN IsClose(x, y, a, b, r2)
			END
		ELSE
			fig.Model().GetBoundingBox(fig.Anchors(), NIL, l, t, r, bot);
			RETURN (l <= x) & (x <= r) & (t <= y) & (y <= bot)
		END
	END Covers;
		
	PROCEDURE (fig: Polygon) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR p: FigModels.PointList;
			A: POINTER TO ARRAY OF Ports.Point;
			i, n: INTEGER;
	BEGIN
		p := fig.Anchors(); i := 0; n := 0;
		WHILE p # NIL DO INC(n); p := p.Next() END;
		p := fig.Anchors(); NEW(A, n);
		WHILE p # NIL DO A[i].x := p.This().X(); A[i].y :=p.This().Y(); p := p.Next(); INC(i) END;
		f.DrawPath(A, i, fig.thickness, fig.color, Ports.closedPoly)
	END Draw;

	PROCEDURE (fig: Polygon) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR x: INTEGER;
			plist: FigModels.PointList;
	BEGIN
		plist := fig.Anchors();	
		IF plist # NIL THEN
			l := plist.This().X(); r := plist.This().X(); t := plist.This().Y(); b := plist.This().Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				l := MIN(l, plist.This().X()); r := MAX(r, plist.This().X());
				t := MIN(t, plist.This().Y()); b := MAX(b, plist.This().Y());
				plist := plist.Next()
			END
		END;
		IF fig.thickness > 0 THEN x := fig.thickness DIV 2; DEC(l, x); DEC(t, x); INC(r, x); INC(b, x) END;
		IF (r - l) < Ports.mm * 3 THEN x := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, x); INC(r, x) END;
		IF (b - t) < Ports.mm * 3 THEN x := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, x); INC(b, x) END
	END GetBoundingBox;
	
	PROCEDURE GetPolygonProperties (fig: Polygon; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp;
			braheProp: FigModels.StdProp;
			TProp: FigModels.TProp;
	BEGIN
		NEW(stdProp); stdProp.color.val := fig.color; stdProp.known := {Properties.color}; stdProp.valid := stdProp.known;
		Properties.Insert(p, stdProp);
		NEW(braheProp);
		braheProp.maxAnchors := FigModels.undefined; braheProp.minAnchors := 3;
		braheProp.expandable := TRUE; braheProp.cyclic := TRUE;
		braheProp.valid := {FigModels.expandable, FigModels.cyclic,
			FigModels.minAnchors, FigModels.maxAnchors};
		braheProp.known := braheProp.valid;
		Properties.Insert(p, braheProp);
		NEW(TProp);
		TProp.thickness := fig.thickness;
		TProp.known := {FigModels.thickness};
		TProp.valid := TProp.known;
		Properties.Insert(p, TProp)
	END GetPolygonProperties;

	PROCEDURE (fig: Polygon) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetPolygonProperties(fig, p)
	END PollProp;
	
	PROCEDURE SetPolygonProperties (fig: Polygon; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN fig.color := p.color.val END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN fig.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetPolygonProperties;
	
	PROCEDURE (op: SetPolygonPropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetPolygonProperties(op.p, p);
		Models.Broadcast(op.p.Model(), msg)
	END Do;
	
	PROCEDURE (fig: Polygon) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetPolygonPropOp;
			prop, old: Properties.Property;
	BEGIN
		GetPolygonProperties(fig, old);
		prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.p := fig; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(fig.Model(), "SetProperties", op)
		END
	END SetProp;

	PROCEDURE (fig: Polygon) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Thickness.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE NewPolygon* (col: Ports.Color; thickness: INTEGER): Polygon;
		VAR p: Polygon;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(p); p.color := col; p.thickness := thickness; RETURN p
	END NewPolygon;

	PROCEDURE  InsertPolygon*;
		VAR nrofsel: INTEGER;
			v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; fig: Polygon;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel(); nrofsel := v.NofSelectedObjects(FigViews.points);
			IF nrofsel >= 3 THEN
				v.PopPoints(nrofsel, p); fig := NewPolygon(Ports.defaultColor, 0);
				m.InsertObject(fig, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(fig)
			END
		END
	END InsertPolygon;
	

	(* Rectangle *)

	PROCEDURE (rect: Rectangle) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: Rectangle DO
			rect.color := source.color;
			rect.thickness := source.thickness
		END
	END CopyFrom;

	PROCEDURE (rect: Rectangle) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(rect.color);
		wr.WriteInt(rect.thickness)
	END Externalize;

	PROCEDURE (rect: Rectangle) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(rect.color);
			IF version > 0 THEN rd.ReadInt(rect.thickness) ELSE rect.thickness := 0 END
		END
	END Internalize;

	PROCEDURE GetRectangleBounds (rect: Rectangle; OUT l, t, r, b: INTEGER);
		VAR h: INTEGER; p: FigModels.Point;
	BEGIN
		p := rect.Anchors().This(); l := p.X(); t := p.Y();
		p := rect.Anchors().Next().This(); r := p.X(); b := p.Y();
		IF l > r THEN h := l; l := r; r := h END;
		IF t > b THEN h := t; t := b; b := h END
	END GetRectangleBounds;

	PROCEDURE (rect: Rectangle) Covers* (x, y: INTEGER): BOOLEAN;
		VAR l, t, r, b, th, d: INTEGER;
	BEGIN
		GetRectangleBounds(rect, l, t, r, b); d := Ports.mm * 2;
		IF rect.thickness # Ports.fill THEN
			th := rect.thickness;
			RETURN ((l-d <= x) & (l+d+th >= x) OR (r-d-th <= x) & (r+d >= x)) & (t-d <= y) & (b+d >= y)
				OR ((t-d <= y) & (t+d+th >= y) OR (b-d-th <= y) & (b+d >= y)) & (l-d <= x) & (r+d >= x)
		ELSE
			RETURN (l - d <= x) & (r + d >= x) & (t - d <= y) & (b + d >= y)
		END
	END Covers;

	PROCEDURE (rect: Rectangle) Draw* (f: Ports.Frame; l_, t_, r_, b_: INTEGER);
		VAR x0, y0, x1, y1: INTEGER;
	BEGIN
		GetRectangleBounds(rect, x0, y0, x1, y1);
		f.DrawRect(x0, y0, x1, y1, rect.thickness, rect.color)
	END Draw;

	PROCEDURE (rect: Rectangle) GetBoundingBox* (OUT l, t, r, b: INTEGER);
		VAR tmp: INTEGER;
	BEGIN
		GetRectangleBounds(rect, l, t, r, b);
		IF (r - l) < Ports.mm * 3 THEN tmp := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, tmp); INC(r, tmp) END;
		IF (b - t) < Ports.mm * 3 THEN tmp := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, tmp); INC(b, tmp) END
	END GetBoundingBox;

	PROCEDURE GetRectangleProperties (r: Rectangle; OUT p: Properties.Property);
		VAR stdProp: Properties.StdProp; tProp: FigModels.TProp;
	BEGIN
		NEW(stdProp);
		stdProp.color.val := r.color; stdProp.known := {Properties.color}; stdProp.valid := stdProp.known;
		Properties.Insert(p, stdProp);
		NEW(tProp);
		tProp.thickness := r.thickness; tProp.known := {FigModels.thickness}; tProp.valid := tProp.known;
		Properties.Insert(p, tProp)
	END GetRectangleProperties;

	PROCEDURE (rect: Rectangle) PollProp* (OUT p: Properties.Property);
	BEGIN
		GetRectangleProperties(rect, p)
	END PollProp;
	
	PROCEDURE SetRectangleProperties (r: Rectangle; p: Properties.Property);
	BEGIN
		WHILE p # NIL DO
			WITH p: Properties.StdProp DO
				IF Properties.color IN p.valid THEN r.color := p.color.val END
			| p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN r.thickness := p.thickness END
			ELSE
			END;
			p := p.next
		END
	END SetRectangleProperties;
	
	PROCEDURE (op: SetPropOp) Do;
		VAR p: Properties.Property; msg: Models.UpdateMsg;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		SetRectangleProperties(op.r, p);
		Models.Broadcast(op.r.Model(), msg)
	END Do;
	
	PROCEDURE (rect: Rectangle) SetProp* (p: Properties.Property);
		VAR equal: BOOLEAN;
			op: SetPropOp;
			prop, old: Properties.Property;
	BEGIN
		GetRectangleProperties(rect, old); prop := Properties.CopyOf(p); Properties.Intersect(prop, old, equal);
		IF ~equal THEN
			NEW(op); op.r := rect; op.prop := Properties.CopyOf(p); op.old := old;
			Models.Do(rect.Model(), "Set Properties", op)
		END
	END SetProp;
	
	PROCEDURE (rect: Rectangle) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Thickness.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;
	
	PROCEDURE NewRectangle* (col: Ports.Color; thickness: INTEGER): Rectangle;
		VAR r: Rectangle;
	BEGIN
		IF thickness # Ports.fill THEN ASSERT(thickness >= 0, 20) END;
		NEW(r); r.color := col; r.thickness := thickness; RETURN r
	END NewRectangle;

	PROCEDURE  InsertRectangle*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; r: Rectangle;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel(); 		
			IF v.NofSelectedObjects(FigViews.points) >= 2 THEN
				v.PopPoints(2, p); r := NewRectangle(Ports.defaultColor, 0);
				m.InsertObject(r, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(r)
			END
		END
	END InsertRectangle;


END FigBasic.
