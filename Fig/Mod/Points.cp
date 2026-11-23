MODULE FigPoints;

	IMPORT Stores, Math, Properties, Models, Dialog,
		FigModels, FigViews;

	CONST
		currentVersion = 1; minVersion = 0; maxVersion = 1;
		fixed* = 0; proportion* = 1;

	TYPE
		LinePoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint)
			fixed: BOOLEAN;
			proportion: REAL
		END;
		
		LinePointDlg* = RECORD
			fixed*: BOOLEAN;
			percent*: INTEGER
		END;
		
		LinePointProportionProp* = POINTER TO RECORD (Properties.Property)
			fixed*: BOOLEAN;
			proportion*: REAL
		END;
		
		SetLinePointProportionOp = POINTER TO RECORD (Stores.Operation)
			point: LinePoint;
			old, prop: LinePointProportionProp
		END;
		
		CirclePoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint) END;
		
		CenterPoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint) END;
		
		CornerPoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint)
			xi, yi: INTEGER
		END;
		
		ParallelPoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint) END;
		
		RectanglePoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint) END;
		
		ProjectionPoint* = POINTER TO LIMITED RECORD (FigModels.ConstrainedPoint) END;

	VAR
		dlg*: LinePointDlg;

	PROCEDURE Adjust (anchors: FigModels.PointList; fixed: BOOLEAN; proportion: REAL; VAR x, y: INTEGER);
		VAR x0, y0, x1, y1, f : REAL; a: FigModels.Point;
	BEGIN
		a := anchors.This(); x1 := a.X(); y1 := a.Y();
		a := anchors.Next().This(); x0 := a.X(); y0 := a.Y();
		IF (x0 # x1) OR (y0 # y1) THEN
			IF ((x = FigModels.undefined) & (y = FigModels.undefined)) OR fixed THEN f := proportion
			ELSIF x = FigModels.undefined THEN x := SHORT(ENTIER((x1 - x0) / (y1 - y0) * (y - y0) + x0))
			ELSIF y = FigModels.undefined THEN y := SHORT(ENTIER((y1 - y0) / (x1 - x0) * (x - x0) + y0))
			ELSE f := ((x1 - x0) * (x - x0) + (y1 - y0) * (y - y0)) / ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0))
			END;
			x := SHORT(ENTIER(x0 + (x1 - x0) * f + 0.5)); y := SHORT(ENTIER(y0 + (y1 - y0) * f + 0.5))
		ELSIF fixed THEN x := a.X(); y := a.Y()
		END
	END Adjust;

	(* SetProportionOp *)

	PROCEDURE (op: SetLinePointProportionOp) Do;
		VAR p: LinePointProportionProp; point: LinePoint;
	BEGIN
		p := op.prop; op.prop := op.old; op.old := p;
		point := op.point;
		IF fixed IN p.valid THEN point.fixed := p.fixed END;
		IF proportion IN p.valid THEN point.proportion := p.proportion END
	END Do;

	PROCEDURE SetProportionProp (p: LinePoint; old, prop: LinePointProportionProp);
		VAR x0, x1, y0, y1: INTEGER;
			op: SetLinePointProportionOp; s: Stores.Operation; m: FigModels.Model;
	BEGIN
		ASSERT((old # NIL) & (prop # NIL), 20);
		NEW(op); op.point := p; op.old := old; op.prop := prop;
		m := p.Model();
		Models.BeginScript(m, "Set Properties", s);
		Models.Do(m, "Set Properties", op);
		x0 := p.X(); y0 := p.Y();
		x1 := x0; y1 := y0; Adjust(p.Anchors(), TRUE, p.proportion, x1, y1);
		m.MovePoint(p, x1 - x0, y1 - y0);
		Models.EndScript(m, s)
	END SetProportionProp;

	PROCEDURE GetProportionProp (p: LinePoint; OUT prop: LinePointProportionProp);
		VAR x, y, x0, y0, x1, y1: INTEGER; d: REAL; q: FigModels.PointList; a: FigModels.Point;
	BEGIN
		NEW(prop); prop.known := {fixed, proportion}; prop.valid := {fixed};
		prop.fixed := p.fixed;
		IF p.fixed THEN prop.proportion := p.proportion; INCL(prop.valid, proportion)
		ELSE
			x := p.X(); y := p.Y();
			q := p.Anchors();
			a := q.This(); x1 := a.X(); y1 := a.Y();
			a := q.Next().This(); x0 := a.X(); y0 := a.Y();
			d := Math.IntPower(x1 - x0, 2) + Math.IntPower(y1 - y0, 2);
			IF d # 0 THEN
				prop.proportion := Math.Sqrt((Math.IntPower(x - x0, 2) + Math.IntPower(y - y0, 2)) / d);
				IF (x < x0) # (x1 < x0) THEN prop.proportion := -1 * prop.proportion END;
				INCL(prop.valid, proportion)
			END
		END
	END GetProportionProp;

	(* ProportionProp *)

	PROCEDURE (p: LinePointProportionProp) IntersectWith* (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: LinePointProportionProp DO
			p.known := p.known + q.known;
			valid := p.valid * q.valid;
			IF p.fixed # q.fixed THEN EXCL(valid, fixed) END;
			IF p.proportion # q.proportion THEN EXCL(valid, proportion) END;
			equal := p.valid = valid; p.valid := valid
		END
	END IntersectWith;

	(* Point *)

	PROCEDURE (p: LinePoint) Action*;
		VAR res: INTEGER; cmd: ARRAY 256 OF CHAR;
	BEGIN
		Dialog.MapString("#Fig:Linepoint.Prop", cmd); Dialog.Call(cmd, "", res)
	END Action;

	PROCEDURE (p: LinePoint) PollProp* (OUT prop: Properties.Property);
		VAR pprop: LinePointProportionProp;
	BEGIN
		GetProportionProp(p, pprop); prop := pprop
	END PollProp;

	PROCEDURE (p: LinePoint) SetProp* (prop: Properties.Property);
		VAR old: LinePointProportionProp;
	BEGIN
		WHILE (prop # NIL) & ~(prop IS LinePointProportionProp) DO
			prop := prop.next
		END;
		IF prop # NIL THEN
			GetProportionProp(p, old);
			SetProportionProp(p, old, prop(LinePointProportionProp))
		END
	END SetProp;

	PROCEDURE (p: LinePoint) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: LinePoint DO p.fixed := source.fixed; p.proportion := source.proportion END
	END CopyFrom;

	PROCEDURE (p: LinePoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteBool(p.fixed); wr.WriteReal(p.proportion)
	END Externalize;
	
	PROCEDURE (p: LinePoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadBool(p.fixed); rd.ReadReal(p.proportion)
		END
	END Internalize;

	PROCEDURE (p: LinePoint) Calculate* (VAR x, y: INTEGER);
	BEGIN
		Adjust(p.Anchors(), p.fixed, p.proportion, x, y)
	END Calculate;

	PROCEDURE NewLinePoint* (fixed: BOOLEAN; proportion: REAL): LinePoint;
		VAR p: LinePoint;
	BEGIN
		NEW(p); p.fixed := fixed; p.proportion := proportion; RETURN p
	END NewLinePoint;

	PROCEDURE  InsertLinePoint*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: LinePoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewLinePoint(FALSE, 0.5);
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertLinePoint;

	PROCEDURE  InsertLinePointMiddle*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: LinePoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewLinePoint(TRUE, 0.5);
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertLinePointMiddle;

	PROCEDURE  InsertLinePointCont*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: LinePoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewLinePoint(TRUE, 2);
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertLinePointCont;

	(* LinePoint Dialog *)

	PROCEDURE InitLinePointDialog*;
		VAR p: Properties.Property;
	BEGIN
		Properties.CollectProp(p);
		WHILE (p # NIL) & ~(p IS LinePointProportionProp) DO p := p.next END;
		IF p # NIL THEN
			WITH p: LinePointProportionProp DO
				IF fixed IN p.valid THEN dlg.fixed := p.fixed ELSE dlg.fixed := FALSE END;
				IF proportion IN p.valid THEN
					dlg.percent := SHORT(ENTIER(p.proportion * 100 + 0.5))
				ELSE dlg.percent := 0
				END
			END
		ELSE dlg.fixed := FALSE; dlg.percent := 0
		END;
		Dialog.Update(dlg)
	END InitLinePointDialog;

	PROCEDURE SetLinePoint*;
		VAR p: LinePointProportionProp;
	BEGIN
		NEW(p); p.known := {fixed, proportion}; p.valid := p.known;
		p.fixed := dlg.fixed; p.proportion := dlg.percent / 100;
		Properties.EmitProp(NIL, p)
	END SetLinePoint;


	(* CirclePoint *)

	PROCEDURE (p: CirclePoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion)
	END Externalize;
	
	PROCEDURE (p: CirclePoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
			theta: REAL;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			IF version = 0 THEN rd.ReadReal(theta) ELSE END
		END
	END Internalize;

	PROCEDURE (p: CirclePoint) Calculate* (VAR x, y: INTEGER);
		VAR radius, length, length2, theta: REAL;
			x0, y0, x1, y1: INTEGER;
	BEGIN
		x0 := p.Anchors().Next().This().X(); y0 := p.Anchors().Next().This().Y();
		x1 := p.Anchors().This().X(); y1 := p.Anchors().This().Y();
		radius := Math.Sqrt(Math.IntPower(x0 - x1, 2) + Math.IntPower(y0 - y1, 2));
		IF (x = FigModels.undefined) & (y = FigModels.undefined) THEN
			theta := 0.0;
			x := SHORT(ENTIER(x0 + radius * Math.Cos(theta)));
			y := SHORT(ENTIER(y0 - radius * Math.Sin(theta)))
		ELSE
			length := Math.Sqrt(Math.IntPower(x0 - x, 2) + Math.IntPower(y0 - y, 2));
			length2 := Math.Sqrt(Math.IntPower(x0 - x, 2) + Math.IntPower(y0 - y0, 2));
			IF length # 0 THEN theta := Math.ArcCos(length2 / length) ELSE theta := 0.0 END;
			IF (x >= x0) & (y >= y0) THEN
				x := SHORT(ENTIER(x0 + radius * Math.Cos(theta)));
				y := SHORT(ENTIER(y0 + radius * Math.Sin(theta)))
			ELSIF (x < x0) & (y < y0) THEN
				x := SHORT(ENTIER(x0 - radius * Math.Cos(theta))); 
				y := SHORT(ENTIER(y0 - radius * Math.Sin(theta)))
			ELSIF (x < x0) & (y >= y0) THEN
				x := SHORT(ENTIER(x0 - radius * Math.Cos(theta)));
				y := SHORT(ENTIER(y0 + radius * Math.Sin(theta)))
			ELSE
				x := SHORT(ENTIER(x0 + radius * Math.Cos(theta)));
				y := SHORT(ENTIER(y0 - radius * Math.Sin(theta)))
			END
		END
	END Calculate;

	PROCEDURE NewCirclePoint* (): CirclePoint;
		VAR p: CirclePoint;
	BEGIN
		NEW(p); RETURN p
	END NewCirclePoint;

	PROCEDURE  InsertCirclePoint*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: CirclePoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewCirclePoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertCirclePoint;


	(* CenterPoint *)
	
	PROCEDURE (p: CenterPoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion)
	END Externalize;
	
	PROCEDURE (p: CenterPoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version)
	END Internalize;

	PROCEDURE (p: CenterPoint) Calculate* (VAR x, y: INTEGER);
		VAR x0, y0: REAL; n: INTEGER; q: FigModels.PointList; pnt: FigModels.Point;
	BEGIN
		q := p.Anchors(); x0 := 0; y0 := 0; n := 0;
		WHILE q # NIL DO pnt := q.This(); x0 := x0 + pnt.X(); y0 := y0 + pnt.Y(); INC(n); q := q.Next() END;
		x := SHORT(ENTIER(x0 / n + 0.5)); y := SHORT(ENTIER(y0 / n + 0.5))
	END Calculate;

	PROCEDURE (pnt: CenterPoint) PollProp* (OUT p: Properties.Property);
		VAR prop: FigModels.StdProp;
	BEGIN
		NEW(prop);
		prop.known := {FigModels.expandable, FigModels.cyclic,
			FigModels.minAnchors, FigModels.maxAnchors};
		prop.valid := prop.known;
		prop.expandable := TRUE; prop.cyclic := TRUE;
		prop.minAnchors := 2; prop.maxAnchors := FigModels.undefined;
		p := prop
	END PollProp;

	PROCEDURE NewCenterPoint* (): CenterPoint;
		VAR p: CenterPoint;
	BEGIN
		NEW(p); RETURN p
	END NewCenterPoint;

	PROCEDURE  InsertCenterPoint*;
		VAR n: INTEGER;
			v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: CenterPoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel(); n := v.NofSelectedObjects(FigViews.points);
			IF n > 1 THEN
				v.PopPoints(n, p); pnt := NewCenterPoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertCenterPoint;


	(* CornerPoint *)
	
	PROCEDURE (p: CornerPoint) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: CornerPoint DO
			p.xi := source.xi; p.yi := source.yi
		END
	END CopyFrom;

	PROCEDURE (p: CornerPoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteInt(p.xi); wr.WriteInt(p.yi)
	END Externalize;
	
	PROCEDURE (p: CornerPoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadInt(p.xi); rd.ReadInt(p.yi)
		END
	END Internalize;

	PROCEDURE (p: CornerPoint) Calculate* (VAR x, y: INTEGER);
		VAR d1, d2: REAL; anc: ARRAY 2 OF FigModels.Point;
	BEGIN
		anc[0] := p.Anchors().This(); anc[1] := p.Anchors().Next().This();
		d1 := Math.IntPower(anc[0].X() - x, 2) + Math.IntPower(anc[1].Y() - y, 2);
		d2 := Math.IntPower(anc[1].X() - x, 2) + Math.IntPower(anc[0].Y() - y, 2);
		IF d1 < d2 THEN p.xi := 0; p.yi := 1 ELSIF d1 > d2 THEN p.xi := 1; p.yi := 0 END;
		x := anc[p.xi].X(); y := anc[p.yi].Y()
	END Calculate;

	PROCEDURE NewCornerPoint* (): CornerPoint;
		VAR p: CornerPoint;
	BEGIN
		NEW(p); p.xi := 0; p.yi := 1; RETURN p
	END NewCornerPoint;

	PROCEDURE InsertCornerPoint*;
	VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: CornerPoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewCornerPoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertCornerPoint;


	(* ParallelPoint *)

	PROCEDURE (p: ParallelPoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion)
	END Externalize;
	
	PROCEDURE (p: ParallelPoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN END
	END Internalize;

	PROCEDURE (p: ParallelPoint) Calculate* (VAR x, y: INTEGER);
	BEGIN
		x := p.Anchors().Next().Next().This().X() - p.Anchors().Next().This().X() + p.Anchors().This().X();
		y := p.Anchors().Next().Next().This().Y() - p.Anchors().Next().This().Y() + p.Anchors().This().Y();
	END Calculate;

	PROCEDURE NewParallelPoint* (): ParallelPoint;
		VAR p: ParallelPoint;
	BEGIN
		NEW(p); RETURN p
	END NewParallelPoint;

	PROCEDURE  InsertParallelPoint*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: ParallelPoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 2 THEN
				v.PopPoints(3, p); pnt := NewParallelPoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertParallelPoint;


	(* RectanglePoint *)

	PROCEDURE (p: RectanglePoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion)
	END Externalize;

	PROCEDURE (p: RectanglePoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN END
	END Internalize;

	PROCEDURE (p: RectanglePoint) Calculate* (VAR x, y: INTEGER);
		VAR p1, p2: FigModels.Point; l, t, r, b, d: INTEGER;
	BEGIN
		p1 := p.Anchors().This(); p2 := p.Anchors().Next().This();
		l := MIN(p1.X(), p2.X()); r := MAX(p1.X(), p2.X()); t := MIN(p1.Y(), p2.Y()); b := MAX(p1.Y(), p2.Y());
		IF x = FigModels.undefined THEN x := (l + r) DIV 2 END;
		IF y = FigModels.undefined THEN y := (t + b) DIV 2 END;
		x := MAX(l, MIN(r, x)); y := MAX(t, MIN(b, y));
		d := MIN(MIN(x - l, r - x), MIN(y - t, b - y));
		IF d # 0 THEN
			IF d = x - l THEN x := l ELSIF d = r - x THEN x := r ELSIF d = y - t THEN y := t ELSE y := b END
		END
	END Calculate;

	PROCEDURE NewRectanglePoint* (): RectanglePoint;
		VAR p: RectanglePoint;
	BEGIN
		NEW(p); RETURN p
	END NewRectanglePoint;

	PROCEDURE InsertRectanglePoint*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: RectanglePoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 1 THEN
				v.PopPoints(2, p); pnt := NewRectanglePoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertRectanglePoint;
	
	
	(* ProjectionPoint *)
	
	PROCEDURE (p: ProjectionPoint) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion)
	END Externalize;
	
	PROCEDURE (p: ProjectionPoint) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN END
	END Internalize;

	PROCEDURE (p: ProjectionPoint) Calculate* (VAR x, y: INTEGER);
		VAR x0, y0, x1, y1, x2, y2, f, l: REAL; q: FigModels.PointList; a: FigModels.Point;
	BEGIN
		q := p.Anchors(); a := q.This(); x0 := a.X(); y0 := a.Y();	(* point *)
		q := q.Next(); a := q.This(); x1 := a.X(); y1 := a.Y();	(* projection line *)
		q := q.Next(); a := q.This(); x2 := a.X(); y2 := a.Y();	(* projection line *)
		l := ((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
		IF l # 0 THEN f := ((x1 - x2) * (x0 - x2) + (y1 - y2) * (y0 - y2)) / l ELSE f := 0 END;
		x := SHORT(ENTIER(x2 + (x1 - x2) * f)); y := SHORT(ENTIER(y2 + (y1 - y2) * f))
	END Calculate;

	PROCEDURE NewProjectionPoint* (): ProjectionPoint;
		VAR p: ProjectionPoint;
	BEGIN
		NEW(p); RETURN p
	END NewProjectionPoint;

	PROCEDURE  InsertProjectionPoint*;
		VAR v: FigViews.View; m: FigModels.Model; p: FigModels.PointList; pnt: ProjectionPoint;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m := v.ThisModel();
			IF v.NofSelectedObjects(FigViews.points) > 2 THEN
				v.PopPoints(3, p); pnt := NewProjectionPoint();
				m.InsertObject(pnt, p, FigModels.undefined, FigModels.undefined);
				v.SelectObject(pnt)
			END
		END
	END InsertProjectionPoint;


END FigPoints.
