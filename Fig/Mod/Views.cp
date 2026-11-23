MODULE FigViews;

	IMPORT Stores, Ports, Math, Views, Controllers, Properties, Models,
		FigModels, Containers;

	CONST
		initialGrid* = 3 * Ports.mm;
		initialGridDrawingFactor* = 3;
		pntSize = 4 * Ports.point;
		windowsAltKey = 28; macCommandKey = 27;
		rightDeleteKey = 07X; leftDeleteKey = 08X;
		arrowLeft = 1CX; arrowRight = 1DX; arrowUp = 1EX; arrowDown = 1FX; tab = 9X;
		minVersion = 3; maxVersion = 4; currentVersion = 4;
		points* = 1; figures* = 2; both* = 3;

	TYPE
		ObjectList = POINTER TO RECORD
			this: FigModels.Object;
			next: ObjectList
		END;
	
		View* = POINTER TO ABSTRACT RECORD (Views.View) END;
	
		Directory* = POINTER TO ABSTRACT RECORD END;
	
		StdView = POINTER TO RECORD (View)
			m: FigModels.Model;
			selectedObjects: ObjectList;
			nofSelectedPoints: INTEGER;
			nofSelectedFigures: INTEGER;
			showPoints: BOOLEAN;
			showGrid: BOOLEAN;
			grid: INTEGER;
			gridDrawingFactor: INTEGER;
			gridOn: BOOLEAN;
			editLock: BOOLEAN;
			noCaretContaier: BOOLEAN;
		END;
		
		StdDirectory = POINTER TO RECORD (Directory) END;

	VAR
		dir-, stdDir-: Directory;


	(* Misc. *)

	PROCEDURE AlienDependencies (pnt: FigModels.Point): BOOLEAN;
		VAR points: FigModels.PointList;
	BEGIN
		IF ~(pnt IS FigModels.AlienPoint) THEN
			points := pnt.DependingPoints();
			WHILE (points # NIL) & ~(points.This() IS FigModels.AlienPoint) & ~AlienDependencies(points.This()) DO
				points := points.Next()
			END;
			RETURN points # NIL
		ELSE RETURN FALSE
		END
	END AlienDependencies;

	PROCEDURE Focus* (): View;
		VAR v: Views.View;
	BEGIN
		v := Controllers.FocusView();
		IF (v # NIL) & (v IS View) THEN RETURN v(View) ELSE RETURN NIL END
	END Focus;

	(* View *)
	
	PROCEDURE (v: View) RoundToGrid* (VAR x, y: INTEGER), NEW, ABSTRACT;
	PROCEDURE (v: View) SetGrid* (grid, gridDrawingFactor: INTEGER), NEW, ABSTRACT;
	PROCEDURE (v: View) GetGrid* (OUT grid, gridDrawingFactor: INTEGER), NEW, ABSTRACT;
	PROCEDURE (v: View) GridOn* (): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (v: View) ShowingGrid* (): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (v: View) ShowingPoints* (): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (v: View) EditLockOn* (): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (v: View) ToggleGrid*, NEW, ABSTRACT;
	PROCEDURE (v: View) TogglePointhiding*, NEW, ABSTRACT;
	PROCEDURE (v: View) ToggleGridhiding*, NEW, ABSTRACT;
	PROCEDURE (v: View) ToggleEditLock*, NEW, ABSTRACT;
	PROCEDURE (v: View) SelectObject* (o: FigModels.Object), NEW, ABSTRACT;
	PROCEDURE (v: View) DeselectObject* (o: FigModels.Object), NEW, ABSTRACT;
	PROCEDURE (v: View) SelectAll*, NEW, ABSTRACT;
	PROCEDURE (v: View) DeselectAll*, NEW, ABSTRACT;
	PROCEDURE (v: View) NofSelectedObjects* (type: INTEGER): INTEGER, NEW, ABSTRACT;
	PROCEDURE (v: View) LatestSelectedObject* (type: INTEGER): FigModels.Object, NEW, ABSTRACT;
	PROCEDURE (v: View) GetSelections* (n, type: INTEGER; OUT sel: ARRAY OF FigModels.Object),
																NEW, ABSTRACT;
	PROCEDURE (v: View) IsSelected* (o: FigModels.Object): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (v: View) PopPoints* (n: INTEGER; OUT plist: FigModels.PointList), NEW, ABSTRACT;
	PROCEDURE (v: View) PopFigures* (n: INTEGER; OUT flist: FigModels.FigureList), NEW, ABSTRACT;
	PROCEDURE (v: View) SelectedPoints* (): FigModels.PointList, NEW, ABSTRACT;
	PROCEDURE (v: View) SelectedFigures* (): FigModels.FigureList, NEW, ABSTRACT;
	PROCEDURE (v: View) SelectPoints* (plist: FigModels.PointList), NEW, ABSTRACT;
	PROCEDURE (v: View) SelectFigures* (flist: FigModels.FigureList), NEW, ABSTRACT;
	
	PROCEDURE (v: View) ThisModel* (): FigModels.Model, EXTENSIBLE;
	BEGIN
		RETURN NIL
	END ThisModel;

	(* Directory *)
	
	PROCEDURE (d: Directory) New* (m: FigModels.Model): View, NEW, ABSTRACT;

	(* StdView *)

	PROCEDURE (v: StdView) SelectObject (o: FigModels.Object);
		VAR olist: ObjectList;
	BEGIN
		ASSERT(o # NIL, 20);
		IF o IS FigModels.Point THEN ASSERT(o(FigModels.Point).Model() = v.ThisModel(), 21)
		ELSIF o IS FigModels.Figure THEN ASSERT(o(FigModels.Figure).Model() = v.ThisModel(), 22)
		END;
		NEW(olist); olist.this := o; olist.next := v.selectedObjects; v.selectedObjects := olist;
		IF o IS FigModels.Point THEN INC(v.nofSelectedPoints)
		ELSIF o IS FigModels.Figure THEN INC(v.nofSelectedFigures)
		END;
		Views.Update(v, Views.keepFrames)
	END SelectObject;
	
	PROCEDURE (v: StdView) DeselectObject (o: FigModels.Object);
		VAR tmp, prev: ObjectList;
	BEGIN
		ASSERT(o # NIL, 20);
		IF o IS FigModels.Point THEN ASSERT(o(FigModels.Point).Model() = v.ThisModel(), 21)
		ELSIF o IS FigModels.Figure THEN ASSERT(o(FigModels.Figure).Model() = v.ThisModel(), 22)
		END;
		tmp := v.selectedObjects; prev := NIL;
		WHILE (tmp # NIL) & (tmp.this # o) DO prev := tmp; tmp := tmp.next END;
		IF tmp # NIL THEN
			IF prev = NIL THEN v.selectedObjects := tmp.next
			ELSE prev.next := tmp.next
			END;
			IF o IS FigModels.Point THEN DEC(v.nofSelectedPoints)
			ELSIF o IS FigModels.Figure THEN DEC(v.nofSelectedFigures)
			END
		END;
		Views.Update(v, Views.keepFrames)
	END DeselectObject;

	PROCEDURE (v: StdView) SelectAll;
		VAR plist: FigModels.PointList;
			p: FigModels.Point;
			flist: FigModels.FigureList;
			f: FigModels.Figure;
	BEGIN
		v.selectedObjects := NIL; v.nofSelectedPoints := 0; v.nofSelectedFigures := 0;
		plist := v.m.Points(); WHILE plist # NIL DO p := plist.This(); v.SelectObject(p); plist := plist.Next() END;
		flist := v.m.Figures(); WHILE flist # NIL DO f := flist.This(); v.SelectObject(f); flist := flist.Next() END
	END SelectAll;
	
	PROCEDURE  GetBoundingBox (obj: ObjectList; OUT l, t, r, b: INTEGER);
		VAR fl, ft, fr, fb, x, y: INTEGER; p: FigModels.Point;
	BEGIN
		ASSERT(obj # NIL, 20);
		IF obj.this IS FigModels.Point THEN
			p := obj.this(FigModels.Point); x := p.X(); y := p.Y();
			l := x - pntSize; r := x + pntSize;
			t :=y - pntSize; b := y + pntSize
		ELSE
			obj.this(FigModels.Figure).GetBoundingBox(fl, ft, fr, fb);
			l := MIN(l, fl); r := MAX(r, fr); t := MIN(t, ft); b := MAX(b, fb)
		END;
		obj := obj.next;
		WHILE obj # NIL DO
			IF obj.this IS FigModels.Point THEN
				p := obj.this(FigModels.Point); x := p.X(); y := p.Y();
				l := MIN(l, x - pntSize); r := MAX(r, x + pntSize);
				t := MIN(t, y - pntSize); b := MAX(b, y + pntSize)
			ELSE
				obj.this(FigModels.Figure).GetBoundingBox(fl, ft, fr, fb);
				l := MIN(l, fl); r := MAX(r, fr); t := MIN(t, ft); b := MAX(b, fb)
			END;
			obj := obj.next
		END
	END GetBoundingBox;
	
	PROCEDURE (v: StdView) DeselectAll;
		VAR l, t, r, b: INTEGER;
	BEGIN
		IF v.selectedObjects # NIL THEN
			GetBoundingBox(v.selectedObjects, l, t, r, b);
			v.selectedObjects := NIL; v.nofSelectedPoints := 0; v.nofSelectedFigures := 0;
			Views.UpdateIn(v, l - Ports.mm, t - Ports.mm, r + Ports.mm, b + Ports.mm, Views.keepFrames)
		END
	END DeselectAll;

	PROCEDURE (v: StdView) NofSelectedObjects (type: INTEGER): INTEGER;
	BEGIN
		IF type = points THEN RETURN v.nofSelectedPoints
		ELSIF type = figures THEN RETURN v.nofSelectedFigures
		ELSIF type = both THEN RETURN v.nofSelectedPoints + v.nofSelectedFigures
		ELSE RETURN 0
		END
	END NofSelectedObjects;
	
	PROCEDURE (v: StdView) LatestSelectedObject (type: INTEGER): FigModels.Object;
		VAR tmplist: ObjectList;
	BEGIN
		tmplist := v.selectedObjects;
		IF (type = points) & (tmplist # NIL) THEN
			WHILE (tmplist # NIL) & ~(tmplist.this IS FigModels.Point) DO tmplist := tmplist.next END;
			IF tmplist = NIL THEN RETURN NIL
			ELSE RETURN tmplist.this
			END
		ELSIF (type = figures) & (tmplist # NIL) THEN
			WHILE (tmplist # NIL) & ~(tmplist.this IS FigModels.Figure) DO tmplist := tmplist.next END;
			IF tmplist = NIL THEN RETURN NIL
			ELSE RETURN tmplist.this
			END
		ELSIF (type = both) & (tmplist # NIL) THEN RETURN v.selectedObjects.this
		ELSE RETURN NIL
		END
	END LatestSelectedObject;
	
	PROCEDURE (v: StdView) GetSelections (n, type: INTEGER; OUT sel: ARRAY OF FigModels.Object);
		VAR tmp: ObjectList;
			index: INTEGER;
	BEGIN
		ASSERT(n > 0, 20); ASSERT(LEN(sel) >= n, 21);
		tmp := v.selectedObjects; index := 0;
		IF type = points THEN
			WHILE (tmp # NIL) & (n > 0) DO
				IF tmp.this IS FigModels.Point THEN
					sel[index] := tmp.this(FigModels.Point); INC(index); DEC(n)
				END;
				tmp := tmp.next
			END
		ELSIF type = figures THEN
			WHILE (tmp # NIL) & (n > 0) DO
				IF tmp.this IS FigModels.Point THEN
					sel[index] := tmp.this(FigModels.Point); INC(index); DEC(n)
				END;
				tmp := tmp.next
			END
		ELSIF type = both THEN
			WHILE (tmp # NIL) & (n > 0) DO sel[index] := tmp.this; INC(index); DEC(n); tmp := tmp.next END
		ELSE
		END
	END GetSelections;
	
	PROCEDURE (v: StdView) IsSelected (o: FigModels.Object): BOOLEAN;
		VAR tmplist: ObjectList;
	BEGIN
		ASSERT(o # NIL, 20);
		IF o IS FigModels.Point THEN ASSERT(o(FigModels.Point).Model() = v.ThisModel(), 21)
		ELSIF o IS FigModels.Figure THEN ASSERT(o(FigModels.Figure).Model() = v.ThisModel(), 22)
		END;
		tmplist := v.selectedObjects; WHILE (tmplist # NIL) & (tmplist.this # o) DO tmplist := tmplist.next END;
		RETURN tmplist # NIL
	END IsSelected;
	
	PROCEDURE (v: StdView) PopPoints (n: INTEGER; OUT plist: FigModels.PointList);
		VAR a: POINTER TO ARRAY OF FigModels.Point;
			tmp: ObjectList;
			index: INTEGER;
	BEGIN
		ASSERT(n > 0, 20);
		ASSERT(n <= v.NofSelectedObjects(points), 21);
		NEW(a, n); tmp := v.selectedObjects; index := 0;
		WHILE (tmp # NIL) & (n > 0) DO
			IF tmp.this IS FigModels.Point THEN
				a[index] := tmp.this(FigModels.Point);
				v.DeselectObject(tmp.this); INC(index); DEC(n) 
			END;
			tmp := tmp.next
		END;
		plist := v.m.ThisPointList(a);
		Views.Update(v, Views.keepFrames)
	END PopPoints;
	
	PROCEDURE (v: StdView) PopFigures (n: INTEGER; OUT flist: FigModels.FigureList);
		VAR a: POINTER TO ARRAY OF FigModels.Figure;
			tmp: ObjectList;
			index: INTEGER;
	BEGIN
		ASSERT(n > 0, 20);
		ASSERT(n <= v.NofSelectedObjects(figures), 21);
		NEW(a, n); tmp := v.selectedObjects; index := 0;
		WHILE (tmp # NIL) & (n > 0) DO
			IF tmp.this IS FigModels.Figure THEN
				a[index] := tmp.this(FigModels.Figure);
				v.DeselectObject(tmp.this); INC(index); DEC(n) 
			END;
			tmp := tmp.next
		END;
		flist := v.m.ThisFigureList(a);
		Views.Update(v, Views.keepFrames)
	END PopFigures;
	
	PROCEDURE (v: StdView) SelectedPoints (): FigModels.PointList;
		VAR a: POINTER TO ARRAY OF FigModels.Point;
			tmp: ObjectList;
			index, n: INTEGER;
	BEGIN
		n := v.nofSelectedPoints; tmp := v.selectedObjects; index := 0;
		IF n > 0 THEN NEW(a, n) END;
		WHILE (tmp # NIL) & (n > 0) DO
			IF tmp.this IS FigModels.Point THEN
				a[index] := tmp.this(FigModels.Point); INC(index); DEC(n)
			END;
			tmp := tmp.next
		END;
		IF v.nofSelectedPoints > 0 THEN
			RETURN v.m.ThisPointList(a)
		ELSE
			RETURN NIL
		END
	END SelectedPoints;
	
	PROCEDURE (v: StdView) SelectedFigures (): FigModels.FigureList;
		VAR a: POINTER TO ARRAY OF FigModels.Figure;
			tmp: ObjectList;
			index, n: INTEGER;
	BEGIN
		n := v.nofSelectedFigures; tmp := v.selectedObjects; index := 0;
		IF n > 0 THEN NEW(a, n) END;
		WHILE (tmp # NIL) & (n > 0) DO
			IF tmp.this IS FigModels.Figure THEN a[index] := tmp.this(FigModels.Figure); INC(index); DEC(n) END;
			tmp := tmp.next
		END;
		IF v.nofSelectedFigures > 0 THEN RETURN v.m.ThisFigureList(a) ELSE RETURN NIL END
	END SelectedFigures;
	
	PROCEDURE (v: StdView) SelectPoints (plist: FigModels.PointList);
	BEGIN
		WHILE plist # NIL DO v.SelectObject(plist.This()); plist := plist.Next() END
	END SelectPoints;
	
	PROCEDURE (v: StdView) SelectFigures (flist: FigModels.FigureList);
	BEGIN
		WHILE flist # NIL DO v.SelectObject(flist.This()); flist := flist.Next() END
	END SelectFigures;	
	
	PROCEDURE (v: StdView) RoundToGrid (VAR x, y: INTEGER);
	BEGIN
		x := x + v.grid DIV 2; y := y + v.grid DIV 2; x := x - x MOD v.grid; y := y - y MOD v.grid
	END RoundToGrid;

	PROCEDURE (v: StdView) SetGrid (grid: INTEGER; gridDrawingFactor: INTEGER);
	BEGIN
		ASSERT(grid > 0, 20);
		ASSERT(gridDrawingFactor > 0, 21);
		v.grid := grid; v.gridDrawingFactor := gridDrawingFactor;
		Views.Update(v, Views.keepFrames)
	END SetGrid;

	PROCEDURE (v: StdView) GetGrid (OUT grid, gridDrawingFactor: INTEGER);
	BEGIN
		grid := v.grid; gridDrawingFactor := v.gridDrawingFactor
	END GetGrid;

	PROCEDURE (v: StdView) GridOn (): BOOLEAN;
	BEGIN
		RETURN v.gridOn
	END GridOn;

	PROCEDURE (v: StdView) ShowingGrid (): BOOLEAN;
	BEGIN
		RETURN v.showGrid
	END ShowingGrid;

	PROCEDURE (v: StdView) ShowingPoints (): BOOLEAN;
	BEGIN
		RETURN v.showPoints
	END ShowingPoints;

	PROCEDURE (v: StdView) EditLockOn (): BOOLEAN;
	BEGIN
		RETURN v.editLock
	END EditLockOn;

	PROCEDURE (v: StdView) InitModel (m: Models.Model), NEW;
	BEGIN
		WITH m: FigModels.Model DO
			v.m := m; Stores.Join(v, m);
			v.selectedObjects := NIL;
			v.nofSelectedPoints := 0;
			v.nofSelectedFigures := 0;
			v.showPoints := TRUE;
			v.showGrid := TRUE;
			v.editLock := FALSE;
			v.noCaretContaier := FALSE;
			v.grid := initialGrid;
			v.gridDrawingFactor := initialGridDrawingFactor;
			v.gridOn := TRUE
		END
	END InitModel;

	PROCEDURE (v: StdView) ThisModel (): FigModels.Model;
	BEGIN
		RETURN v.m
	END ThisModel;


	PROCEDURE (v: StdView) CopyFromModelView (source: Views.View; model: Models.Model);
	BEGIN
		WITH source: StdView DO
			v.m := model(FigModels.Model);
			v.showPoints := source.showPoints; v.showGrid := source.showGrid;
			v.grid := source.grid; v.gridDrawingFactor := source.gridDrawingFactor;
			v.gridOn := source.gridOn; v.editLock := source.editLock
		END
	END CopyFromModelView;

	PROCEDURE (v: StdView) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
			s: Stores.Store;
	BEGIN
		rd.ReadVersion(minVersion, maxVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadStore(s);
			IF s IS FigModels.Model THEN
				v.InitModel(s(FigModels.Model));
				rd.ReadBool(v.showPoints); rd.ReadBool(v.showGrid);
				rd.ReadInt(v.grid); rd.ReadInt(v.gridDrawingFactor); rd.ReadBool(v.gridOn);
				IF version > 3 THEN
					rd.ReadBool(v.editLock);
				ELSE
					v.editLock := FALSE
				END;
			ELSE rd.TurnIntoAlien(Stores.alienComponent)
			END
		END
	END Internalize;

	PROCEDURE (v: StdView) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentVersion);
		wr.WriteStore(v.m);
		wr.WriteBool(v.showPoints); wr.WriteBool(v.showGrid);
		wr.WriteInt(v.grid); wr.WriteInt(v.gridDrawingFactor); wr.WriteBool(v.gridOn);
		wr.WriteBool(v.editLock);
	END Externalize;

	PROCEDURE DrawPoint (f: Ports.Frame; p: FigModels.Point; x, y, s, color: INTEGER);
		VAR bsize, csize, b: INTEGER; alienColor: Ports.Color;
	BEGIN
		bsize := (pntSize DIV f.unit) * f.unit; csize := (pntSize DIV f.unit) * f.unit; b := f.dot;
		IF Ports.background # Ports.grey50 THEN
			alienColor := Ports.grey50 ELSE alienColor := Ports.grey25
		END;
		IF p IS FigModels.AlienPoint THEN
			f.DrawRect(x - (csize + b), y - (csize + b), x + (csize + b), y + (csize + b), Ports.fill, alienColor);
			f.DrawRect(x - csize, y - csize, x + csize, y + csize, s, Ports.black);
			f.DrawLine(x - f.dot, y - f.dot, x + f.dot, y + f.dot, 0, color)
		ELSIF p IS FigModels.ConstrainedPoint THEN
			f.DrawRect(x - (csize + b), y - (csize + b), x + (csize + b), y + (csize + b), Ports.fill, Ports.background);
			f.DrawRect(x - csize, y - csize, x + csize, y + csize, s, color);
			f.DrawRect(x - f.dot, y - f.dot, x + f.dot, y + f.dot, 0, color)
		ELSE
			f.DrawLine(x - bsize, y - bsize, x + bsize, y + bsize, s + f.dot * 2, Ports.background);
			f.DrawLine(x - bsize, y + bsize, x + bsize, y - bsize, s + f.dot * 2, Ports.background);
			f.DrawLine(x - bsize, y - bsize, x + bsize, y + bsize, s, color);
			f.DrawLine(x - bsize, y + bsize, x + bsize, y - bsize, s, color)
		END
	END DrawPoint;
	
	PROCEDURE Draw (v: StdView; f: Ports.Frame; l, t, r, b: INTEGER; showPoints: BOOLEAN);
		VAR m: FigModels.Model; plist: FigModels.PointList; p: FigModels.Point; bl, bt, br, bb: INTEGER;
			figs: FigModels.FigureList;
	BEGIN
		m := v.ThisModel();
		figs := m.Figures();
		WHILE figs # NIL DO figs.This().Draw(f, l, t, r, b); figs := figs.Next() END;
		figs := v.SelectedFigures();
		WHILE figs # NIL DO
			figs.This().GetBoundingBox(bl, bt, br, bb);
			f.MarkRect(bl - 2 * f.dot, bt - 2 * f.dot, br + 2 * f.dot, bb + 2 * f.dot, 2 * f.unit, Ports.hilite, Ports.show);
			figs := figs.Next()
		END;
		IF showPoints THEN
			plist := m.Points();
			WHILE plist # NIL DO
				p := plist.This(); DrawPoint(f, p, p.X(), p.Y(), 0, Ports.black); plist := plist.Next()
			END
		END;
		plist := v.SelectedPoints();
		WHILE plist # NIL DO
			p := plist.This(); DrawPoint(f, p, p.X(), p.Y(), f.dot * 2, Ports.black); plist := plist.Next()
		END
	END Draw;

	PROCEDURE (v: StdView) Restore (f: Views.Frame; l, t, r, b: INTEGER);
		VAR k, x, y, w, h: INTEGER;
			hostFrame: Views.Frame; msg: Containers.GetOpts;
	BEGIN
		IF ~Views.IsPrinterFrame(f) THEN
			v.noCaretContaier := FALSE;
			hostFrame := Views.HostOf(f);
			IF hostFrame # NIL THEN
				IF (hostFrame.view # NIL) & (hostFrame.view IS Containers.View) THEN
					Views.HandlePropMsg(hostFrame.view, msg);
					v.noCaretContaier := Containers.noCaret IN msg.opts
				END
			END;
			IF v.showGrid & ~v.noCaretContaier THEN
				k := v.grid * v.gridDrawingFactor; v.context.GetSize(w, h);
				x := l - (l MOD k);
				WHILE x <= w  DO f.MarkRect(x, 0, x + f.unit, h, 0, Ports.dim50, Ports.show); INC(x, k) END;
				y := t -  (t MOD k);
				WHILE y <= h DO f.MarkRect(0, y, w, y + f.unit, 0, Ports.dim50, Ports.show); INC(y, k) END
			END;
			
			Draw(v, f, l, t, r, b, v.showPoints & ~v.noCaretContaier)
		ELSE Draw(v, f, l, t, r, b, FALSE)
		END
	END Restore;

	PROCEDURE (v: StdView) ToggleGrid;
	BEGIN
		v.gridOn := ~v.gridOn;
		Views.Update(v, Views.keepFrames)
	END ToggleGrid;

	PROCEDURE (v: StdView) TogglePointhiding;
	BEGIN
		v.showPoints := ~v.showPoints;
		Views.Update(v, Views.keepFrames)
	END TogglePointhiding;

	PROCEDURE (v: StdView) ToggleGridhiding;
	BEGIN
		v.showGrid := ~v.showGrid;
		Views.Update(v, Views.keepFrames)
	END ToggleGridhiding;
	
	PROCEDURE (v: StdView) ToggleEditLock;
	BEGIN
		v.editLock := ~v.editLock;
		Views.Update(v, Views.keepFrames)
	END ToggleEditLock;
	
	(* Model Messages *)

	PROCEDURE (v: StdView) HandleModelMsg (VAR msg: Models.Message);
	BEGIN
		WITH msg: FigModels.InsertMsg DO
			IF ~msg.insert & (msg.figure = NIL) THEN v.DeselectObject(msg.point)
			ELSIF ~msg.insert & (msg.point = NIL) THEN v.DeselectObject(msg.figure)
			END;
			Views.Update(v, Views.keepFrames)
		| msg: FigModels.AnchorMsg DO Views.Update(v, Views.keepFrames)
		| msg: FigModels.ChangeAnchorMsg DO Views.Update(v, Views.keepFrames)
		| msg: FigModels.UpdateMsg DO Views.Update(v, Views.keepFrames)
		| msg: Models.UpdateMsg DO Views.Update(v, Views.keepFrames)
		ELSE
		END
	END HandleModelMsg;


	(* EditMessage Handling *)

	PROCEDURE DeleteSelection (v: StdView);
		VAR m: FigModels.Model;
			flist: FigModels.FigureList;
			plist: FigModels.PointList;
			s: Stores.Operation;

		PROCEDURE DeletePoint (p: FigModels.Point);
			VAR plist: FigModels.PointList;
		BEGIN
			plist := p.DependingPoints(); WHILE plist # NIL DO DeletePoint(plist.This()); plist := plist.Next() END;
			IF (p.DependingPoints() = NIL) & (p.DependingFigures() = NIL) & v.IsSelected(p) THEN
				m.RemoveObject(p)
			END
		END DeletePoint;

	BEGIN
		m := v.m;
		Models.BeginScript(m, "Delete", s);
		flist := v.SelectedFigures(); WHILE flist # NIL DO m.RemoveObject(flist.This()); flist := flist.Next() END;
		plist := v.SelectedPoints(); WHILE plist # NIL DO DeletePoint(plist.This()); plist := plist.Next() END;
		Models.EndScript(m, s)
	END DeleteSelection;

	PROCEDURE CopyToNewView (v: StdView; OUT w, h: INTEGER; OUT new: Views.View);
		VAR l, t, r, b: INTEGER;
			copy: FigModels.Model;
			flist: FigModels.FigureList;
			plist: FigModels.PointList;
	BEGIN
		copy := FigModels.CloneOf(v.m)(FigModels.Model);
		copy.InsertCopy(v.m, v.SelectedPoints(), v.SelectedFigures(), plist, flist);
		IF (copy.Points() # NIL) OR (copy.Figures() # NIL) THEN
			copy.GetBoundingBox(copy.Points(), copy.Figures(), l, t, r, b);
			copy.MovePoints(copy.Points(), -l, -t);
			w := r - l; h := b - t; new := Views.CopyWithNewModel(v, copy)
		END
	END CopyToNewView;

	PROCEDURE Paste (into: StdView; insert: View);
		VAR m0, m: FigModels.Model;
			p: FigModels.Point;
			flist: FigModels.FigureList;
			plist: FigModels.PointList;
			s: Stores.Operation;
	BEGIN
		m0 := into.m; m := insert.ThisModel(); p := into.SelectedPoints().This();
		Models.BeginScript(m0, "Paste", s);
		m0.InsertCopy(m, m.Points(), m.Figures(), plist, flist); m0.MovePoints(plist, p.X(), p.Y());
		Models.EndScript(m0, s);
		into.DeselectAll;
		into.SelectPoints(plist); into.SelectFigures(flist)
	END Paste;

	PROCEDURE MoveByKey (v: StdView; key: CHAR; dist: INTEGER);
		VAR m: FigModels.Model;
	BEGIN
		m := v.m;
		CASE key OF
			arrowLeft: m.MovePoints(v.SelectedPoints(), -dist, 0)
		|	arrowRight: m.MovePoints(v.SelectedPoints(), dist, 0)
		|	arrowUp: m.MovePoints(v.SelectedPoints(), 0, -dist)
		|	arrowDown: m.MovePoints(v.SelectedPoints(), 0, dist)
		END
	END MoveByKey;

	PROCEDURE Edit (v: StdView; f: Views.Frame; VAR msg: Controllers.EditMsg);
		VAR plist: FigModels.PointList;
	BEGIN
		IF msg.op = Controllers.pasteChar THEN
			IF (msg.char = rightDeleteKey) OR (msg.char = leftDeleteKey) THEN DeleteSelection(v)
			ELSIF (msg.char >= arrowLeft) & (msg.char <= arrowDown) THEN
				IF Controllers.modify IN msg.modifiers THEN MoveByKey(v, msg.char, v.grid)
				ELSE MoveByKey(v, msg.char, f.dot)
				END
			ELSIF (msg.char = tab) & (v.NofSelectedObjects(points) # 0) THEN v.PopPoints(1, plist)
			END
		ELSIF (msg.op = Controllers.paste) & (msg.view IS View) THEN Paste(v , msg.view(View))
		ELSIF msg.op = Controllers.copy THEN CopyToNewView (v, msg.w, msg.h, msg.view)
		ELSIF msg.op = Controllers.cut THEN CopyToNewView (v, msg.w, msg.h, msg.view); DeleteSelection(v)
		END
	END Edit;

	(* TrackMessage & PollCursorMessage Handling *)

	PROCEDURE CheckHits (v: StdView; x, y: INTEGER; OUT p: FigModels.Point; OUT f: FigModels.Figure);
		VAR d, dist: REAL;
			pnt, min: FigModels.Point;
			plist: FigModels.PointList;
			flist, q: FigModels.FigureList;
	BEGIN
		plist := v.m.Points(); min := NIL; dist := 2 * Math.IntPower(MAX(INTEGER), 2);
		WHILE plist # NIL DO
			pnt := plist.This(); d := Math.IntPower(pnt.X() - x, 2) + Math.IntPower(pnt.Y() - y, 2);
			IF d <= dist THEN min := pnt; dist := d END;
			plist := plist.Next()
		END;
		IF (min # NIL) &
			((v.gridOn & (2 * ABS(min.X() - x) <= v.grid) & (2 * ABS(min.Y() - y) <= v.grid))
				OR (dist <= Math.IntPower(pntSize, 2)))
		THEN p := min
		ELSE p := NIL
		END;
		flist := v.m.TheseFigures(v.m.Figures(), x, y);
		IF flist # NIL THEN
			q := flist.Next(); WHILE q # NIL DO flist := q; q := flist.Next() END;
			f := flist.This()
		ELSE f := NIL
		END
	END CheckHits;

	PROCEDURE ThisCursor (v: StdView; x, y: INTEGER; frame: Views.Frame): INTEGER;
		VAR modifiers: SET;
			isDown: BOOLEAN;
			p: FigModels.Point;
			f: FigModels.Figure;
	BEGIN
		CheckHits(v, x, y, p, f); frame.Input(x, y, modifiers, isDown);
		IF (p # NIL) OR ((f # NIL) & ~(Controllers.modify IN modifiers)) THEN RETURN Ports.arrowCursor
		ELSE RETURN Ports.graphicsCursor
		END
	END ThisCursor;

	PROCEDURE InvertRect (f: Views.Frame; x0, y0, x1, y1: INTEGER);
		VAR l, t, r, b: INTEGER;
	BEGIN
		IF x0 <= x1 THEN l := x0; r := x1 ELSE l := x1; r := x0 END;
		IF y0 <= y1 THEN t := y0; b := y1 ELSE t := y1; b := y0 END;
		f.MarkRect(l, t, r, b, 0, Ports.dim75, TRUE)
	END InvertRect;

	PROCEDURE TrackOnBackground (v: StdView; f: Views.Frame; x0, y0: INTEGER; modifiers: SET);
		VAR x, y, x1, y1: INTEGER;
			mod: SET;
			isDown: BOOLEAN;
	BEGIN
		f.SetCursor(Ports.graphicsCursor);
		x1 := x0; y1 := y0; InvertRect(f, x0, y0, x1, y1);
		REPEAT
			f.Input(x, y, mod, isDown); modifiers := modifiers + mod;
			IF (x # x1) OR (y # y1) THEN
				InvertRect(f, x0, y0, x1, y1); x1 := x; y1 := y; InvertRect(f, x0, y0, x1, y1)
			END
		UNTIL ~isDown OR ({Controllers.modify, Controllers.doubleClick} * modifiers # {});
		InvertRect(f, x0, y0, x1, y1);
		IF isDown THEN
			REPEAT f.Input(x, y, mod, isDown); modifiers := modifiers + mod UNTIL ~isDown;
			IF ~(Controllers.extend IN modifiers) THEN v.DeselectAll END;
			IF {Controllers.modify, Controllers.doubleClick} * modifiers # {} THEN
				IF v.gridOn THEN v.RoundToGrid(x, y) END;
				IF (x >= f.l) & (x <= f.r) & (y >= f.t) & (y <= f.b) THEN v.SelectObject(v.m.NewFreePoint(x, y)) END
			END
		ELSE
			IF ~(Controllers.extend IN modifiers) THEN v.DeselectAll END;
			IF x0 > x THEN x := x0; x0 := x1 END;
			IF y0 > y THEN y := y0; y0 := y1 END;
			v.SelectPoints(v.m.ThesePoints(v.m.Points(), x0, y0, x, y));
			v.SelectFigures(v.m.IncludedFigures(v.m.Figures(), x0, y0, x, y))
		END
	END TrackOnBackground;

	PROCEDURE TrackMove (v: StdView; f: Views.Frame; x0, y0: INTEGER; modifiers: SET);
		VAR x, y, dx, dy, l, t, r, b, res, col: INTEGER;
			mod: SET;
			isDown: BOOLEAN;
			m: FigModels.Model;
			plist: FigModels.PointList;
			p: FigModels.Point;
			flist: FigModels.FigureList;
	BEGIN
		f.SetCursor(Ports.graphicsCursor);
		f.SaveRect(f.l, f.t, f.r, f.b, res);
		v.m.GetBoundingBox(v.SelectedPoints(), NIL, l, t, r, b);
		IF Ports.background # Ports.grey25 THEN col := Ports.grey25 ELSE col := Ports.grey50 END;
		IF v.gridOn THEN v.RoundToGrid(x0, y0) END;
		dx := 0; dy := 0;
		REPEAT
			f.Input(x, y, mod, isDown); modifiers := modifiers + mod;
			IF v.gridOn THEN v.RoundToGrid(x, y) END;
			IF (dx # MAX(-l, x - x0)) OR (dy # MAX(-t, y - y0)) THEN
				dx := MAX(-l, x - x0); dy := MAX(-t, y - y0);
				f.RestoreRect(f.l, f.t, f.r, f.b, Ports.keepBuffer);
				plist := v.SelectedPoints();
				WHILE plist # NIL DO
					p := plist.This();
					IF ~(p IS FigModels.ConstrainedPoint) THEN
						DrawPoint(f, p, p.X() + dx, p.Y() + dy, f.dot * 2, col)
					END;
					plist := plist.Next()
				END
			END
		UNTIL ~isDown;
		f.RestoreRect(f.l, f.t, f.r, f.b, Ports.disposeBuffer);
		IF (dx # 0) OR (dy # 0) THEN
			IF Controllers.modify IN modifiers THEN
				m := FigModels.CloneOf(v.m)(FigModels.Model);
				m.InsertCopy(v.m, v.SelectedPoints(), v.SelectedFigures(), plist, flist);
				m.MovePoints(m.Points(), dx, dy);
				v.m.InsertCopy(m, m.Points(), m.Figures(), plist, flist);
				v.DeselectAll(); v.SelectPoints(plist); v.SelectFigures(flist)
			ELSE v.m.MovePoints(v.SelectedPoints(), dx, dy)
			END
		END
	END TrackMove;

	PROCEDURE TrackOnPoint (v: StdView; f: Views.Frame; x0, y0: INTEGER; modifiers: SET; p: FigModels.Point);
		VAR x, y: INTEGER;
			mod: SET;
			isDown, isSelected: BOOLEAN;
	BEGIN
		f.SetCursor(Ports.arrowCursor);
		isSelected := v.IsSelected(p);
		REPEAT f.Input(x, y, mod, isDown); modifiers := modifiers + mod
		UNTIL ~isDown OR ((x # x0) OR (y # y0)) & isSelected;
		IF isDown THEN TrackMove(v, f, x0, y0, modifiers)
		ELSIF (p IS FigModels.ConstrainedPoint)
			& ({macCommandKey, windowsAltKey, Controllers.doubleClick} * modifiers # {})
		THEN p(FigModels.ConstrainedPoint).Action
		ELSIF Controllers.extend IN modifiers THEN
			IF v.IsSelected(p) THEN v.DeselectObject(p) ELSE v.SelectObject(p) END
		ELSE v.DeselectAll; v.SelectObject(p)
		END
	END TrackOnPoint;

	PROCEDURE TrackOnFigure (v: StdView; f: Views.Frame; x0, y0: INTEGER; modifiers: SET;
													fig: FigModels.Figure);
		VAR x, y: INTEGER;
			mod: SET;
			isDown: BOOLEAN;
	BEGIN
		f.SetCursor(Ports.arrowCursor);
		REPEAT f.Input(x, y, mod, isDown); modifiers := modifiers + mod
		UNTIL ~isDown OR (Controllers.modify IN modifiers)
			OR (Math.IntPower(x - x0, 2) + Math.IntPower(y - y0, 2) > Math.IntPower(pntSize, 2));
		IF isDown THEN TrackOnBackground (v, f, x0, y0, modifiers)
		ELSIF {macCommandKey, windowsAltKey, Controllers.doubleClick} * modifiers # {} THEN fig.Action
		ELSIF Controllers.extend IN modifiers THEN
			IF v.IsSelected(fig) THEN v.DeselectObject(fig) ELSE v.SelectObject(fig) END
		ELSE v.DeselectAll; v.SelectObject(fig)
		END
	END TrackOnFigure;

	PROCEDURE Track (v: StdView; f: Views.Frame; VAR msg: Controllers.TrackMsg);
		VAR pnt: FigModels.Point;
			fig: FigModels.Figure;
	BEGIN
		CheckHits(v, msg.x, msg.y, pnt, fig);
		IF pnt # NIL THEN TrackOnPoint(v, f, msg.x, msg.y, msg.modifiers, pnt)
		ELSIF fig # NIL THEN TrackOnFigure(v, f, msg.x, msg.y, msg.modifiers, fig)
		ELSE TrackOnBackground(v, f, msg.x, msg.y, msg.modifiers)
		END
	END Track;

	(* ControllersMessage Handling *)

	PROCEDURE (v: StdView) HandleCtrlMsg (f: Views.Frame; VAR msg: Controllers.Message;
																	VAR focus: Views.View);
	BEGIN
		IF v.noCaretContaier THEN
			(* pass *)
		ELSIF v.editLock THEN
			WITH msg: Controllers.PollOpsMsg DO
				msg.type := "FigViews.View";
			ELSE END
		ELSE
			WITH msg: Controllers.PollOpsMsg DO
				msg.type := "FigViews.View";
				msg.selectable := TRUE;
				IF (v.SelectedPoints() = NIL) & (v.SelectedFigures() = NIL) THEN msg.valid :={}
				ELSIF v.NofSelectedObjects(points) = 1 THEN
					msg.valid := {Controllers.paste, Controllers.copy, Controllers.cut}
				ELSE msg.valid := {Controllers.copy, Controllers.cut}
				END
			| msg: Properties.CollectMsg DO Views.HandlePropMsg(v, msg.poll)
			| msg: Properties.EmitMsg DO Views.HandlePropMsg(v, msg.set)
			| msg: Controllers.SelectMsg DO IF msg.set THEN v.SelectAll ELSE v.DeselectAll END
			| msg: Controllers.EditMsg DO Edit(v, f, msg)
			| msg: Controllers.TrackMsg DO Track(v, f, msg)
			| msg: Controllers.PollCursorMsg DO msg.cursor := ThisCursor(v, msg.x, msg.y, f)
			ELSE (* unknown message type, ignore *)
			END
		END
	END HandleCtrlMsg;

	(* Property Messages *)

	PROCEDURE SetBoundsPref (v: StdView; VAR msg: Properties.BoundsPref);
		VAR l, t: INTEGER;
	BEGIN
		IF (v.m.Points() # NIL) OR (v.m.Figures() # NIL) THEN
			v.m.GetBoundingBox(v.m.Points(), v.m.Figures(), l, t, msg.h, msg.w)
		ELSE msg.w := 0; msg.h := 0
		END
	END SetBoundsPref;

	PROCEDURE (v: StdView) HandlePropMsg (VAR msg: Properties.Message);
	BEGIN
		WITH msg: Properties.FocusPref DO
			IF v.noCaretContaier OR v.editLock THEN
				msg.setFocus := FALSE
			ELSE
				msg.setFocus := TRUE
			END
		| msg: Properties.SizePref DO
			IF (msg.w = Views.undefined) OR (msg.h = Views.undefined) THEN
				 msg.w := 60 * Ports.mm; msg.h := 40 * Ports.mm
			END
		| msg: Properties.BoundsPref DO SetBoundsPref(v, msg)
		
		| msg: Properties.ResizePref DO msg.horFitToWin := TRUE; msg.verFitToWin := TRUE
		| msg: Properties.PollMsg DO v.m.CollectProperties(v.SelectedFigures(), v.SelectedPoints(), msg.prop)
		| msg: Properties.SetMsg DO v.m.EmitProperties(v.SelectedFigures(), v.SelectedPoints(), msg.prop)
		ELSE 
		END
	END HandlePropMsg;

	(* StdDirectory *)

	PROCEDURE (d: StdDirectory) New (m: FigModels.Model): View;
		VAR v: StdView;
	BEGIN
		ASSERT(m # NIL, 20);
		NEW(v); v.InitModel(m); RETURN v
	END New;
	
	PROCEDURE SetDir*(d: Directory);
	BEGIN
		ASSERT (d # NIL, 20);
		dir := d
	END SetDir;
	
	PROCEDURE Init;
		VAR d: StdDirectory;
	BEGIN
		NEW(d); stdDir := d; dir := d
	END Init;

BEGIN
	Init
END FigViews.
