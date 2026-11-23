MODULE FigCmds;

	IMPORT
		Services, Ports, Properties, Stores, Models, Views, Dialog, StdCmds,
		FigModels, FigViews;

	CONST
		mm* = 0; inch* = 1; point* = 2; fill* = 3; hairline* = 4;

	VAR
		gridDlg*: RECORD
			grid*, gridDrawingFactor*: INTEGER;
			unitType*: INTEGER
		END;
		TPropDlg*: RECORD
			thickness*: INTEGER;
			type*: INTEGER;
			initiated*: BOOLEAN
		END;
		arrowPropDlg*: RECORD
			type*: INTEGER;
			initiated*: BOOLEAN
		END;

	PROCEDURE SelectAnchors*;
		VAR i, nofSel: INTEGER; v: FigViews.View;
			s: Stores.Store; sel: POINTER TO ARRAY OF FigModels.Object;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			nofSel := v.NofSelectedObjects(FigViews.both);
			IF nofSel # 0 THEN
				NEW(sel, nofSel); v.GetSelections(nofSel, FigViews.both, sel);
				i := 0;
				WHILE i # nofSel DO
					s := sel[i];
					WITH s: FigModels.ConstrainedPoint DO v.SelectPoints(s.Anchors())
					| s: FigModels.Figure DO v.SelectPoints(s.Anchors())
					ELSE
					END;
					INC(i)
				END
			END
		END
	END SelectAnchors;

	PROCEDURE SelectAnchorsGuard*(VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			par.disabled := v.NofSelectedObjects(FigViews.both) = 0
		ELSE par.disabled := TRUE
		END
	END SelectAnchorsGuard;

	PROCEDURE PollFigureProp (f: FigModels.Figure; OUT prop: FigModels.StdProp);
		VAR p: Properties.Property;
	BEGIN
		f.PollProp(p); WHILE (p # NIL) & ~(p IS FigModels.StdProp) DO p := p.next END;
		IF p # NIL THEN prop := p(FigModels.StdProp) ELSE prop := NIL END
	END PollFigureProp;

	PROCEDURE PollPointProp (c: FigModels.ConstrainedPoint; OUT prop: FigModels.StdProp);
		VAR p: Properties.Property;
	BEGIN
		c.PollProp(p); WHILE (p # NIL) & ~(p IS FigModels.StdProp) DO p := p.next END;
		IF p # NIL THEN prop := p(FigModels.StdProp) ELSE prop := NIL END
	END PollPointProp;

	PROCEDURE CanBeExtended (anchors: FigModels.PointList; prop: FigModels.StdProp): BOOLEAN;

		PROCEDURE ListLength (l: FigModels.PointList): INTEGER;
			VAR n: INTEGER;
		BEGIN
			n := 0; WHILE l # NIL DO INC(n); l := l.Next() END;
			RETURN n
		END ListLength;

	BEGIN
		RETURN (prop # NIL) & (FigModels.expandable IN prop.valid) & prop.expandable
			& (FigModels.maxAnchors IN prop.valid)
			& ((prop.maxAnchors = FigModels.undefined) OR (prop.maxAnchors > ListLength(anchors)))
	END CanBeExtended;

	PROCEDURE GetAddAnchorPar (OUT model: FigModels.Model; OUT view: FigViews.View; 
														OUT fig: FigModels.Figure; OUT pnt: FigModels.ConstrainedPoint;
														OUT pos: INTEGER);
		VAR nofP, nofF: INTEGER; v: FigViews.View;
			m: FigModels.Model; prop: FigModels.StdProp; plist, q: FigModels.PointList;
			p1, p2, q1, q2: FigModels.Point; f: FigModels.Figure; p: FigModels.ConstrainedPoint;
	BEGIN
		model := NIL; fig := NIL; pnt := NIL; v := FigViews.Focus(); view := v;
		IF v # NIL THEN
			m := v.ThisModel();
			nofP := v.NofSelectedObjects(FigViews.points); nofF := v.NofSelectedObjects(FigViews.figures);
			IF (nofF = 0) & (nofP = 1) THEN
				p1 := v.SelectedPoints().This();
				IF p1 IS FigModels.ConstrainedPoint THEN
					p := p1(FigModels.ConstrainedPoint); PollPointProp(p, prop);
					IF CanBeExtended(p.Anchors(), prop) THEN model := m; pnt := p; pos := 0 END
				END
			ELSIF (nofF = 1) & ((nofP = 1) OR (nofP = 2)) THEN
				f := v.SelectedFigures().This(); PollFigureProp(f, prop);
				IF CanBeExtended(f.Anchors(), prop) THEN
					IF (nofP = 1) & (FigModels.cyclic IN prop.valid) & ~prop.cyclic THEN
						plist := v.SelectedPoints(); p1 := plist.This(); plist := f.Anchors();
						IF plist.This() = p1 THEN model := m; fig := f; pos := 0
						ELSE
							q := plist.Next(); pos := 1; WHILE q # NIL DO INC(pos); plist := q; q := plist.Next() END;
							IF plist.This() = p1 THEN model := m; fig := f END
						END
					ELSIF nofP = 2 THEN
						plist := v.SelectedPoints(); p1 := plist.This(); p2 := plist.Next().This();
						plist := f.Anchors(); q := plist.Next();
						IF q # NIL THEN
							q1 := plist.This(); q2 := q.This(); pos := 1;
							WHILE (q # NIL) & ((q1 # p1) OR (q2 # p2)) & ((q1 # p2) OR (q2 # p1)) DO
								plist := q; q := plist.Next(); q1 := q2;
								IF q # NIL THEN q2 := q.This() END;
								INC(pos)
							END;
							IF q # NIL THEN model := m; fig := f
							ELSIF (FigModels.cyclic IN prop.valid) & prop.cyclic
								& ((q1 = p1) & (f.Anchors().This() = p2) OR (q1 = p2) & (f.Anchors().This() = p1))
							THEN model := m; fig := f; pos := 0
							END
						END
					END
				END
			END
		END
	END GetAddAnchorPar;

	PROCEDURE AddAnchorGuard* (VAR par: Dialog.Par);
		VAR pos: INTEGER; m: FigModels.Model; f: FigModels.Figure; p: FigModels.ConstrainedPoint;
			v: FigViews.View;
	BEGIN
		GetAddAnchorPar(m, v, f, p, pos);
		par.disabled := m = NIL;
		IF f # NIL THEN par.label := "Add Anchor to Figure"
		ELSIF p # NIL THEN par.label := "Add Anchor to Point"
		ELSE par.label := "Add Anchor"
		END
	END AddAnchorGuard;

	PROCEDURE AddAnchor*;
		VAR pos: INTEGER; m: FigModels.Model; v: FigViews.View;
			f: FigModels.Figure; new: FigModels.Point; p: FigModels.ConstrainedPoint;
	BEGIN
		GetAddAnchorPar(m, v, f, p, pos);
		IF f # NIL THEN
			m.AddAnchor(f, pos, new); v.DeselectAll; v.SelectObject(new); v.SelectObject(f)
		ELSIF p # NIL THEN
			m.AddAnchor(p, pos, new); m.MovePoint(p, 0, 0); v.DeselectAll; v.SelectObject(new)
		END
	END AddAnchor;

	PROCEDURE CanBeReduced (anchors: FigModels.PointList; prop: FigModels.StdProp): BOOLEAN;

		PROCEDURE ListLength (l: FigModels.PointList): INTEGER;
			VAR n: INTEGER;
		BEGIN
			n := 0; WHILE l # NIL DO INC(n); l := l.Next() END;
			RETURN n
		END ListLength;

	BEGIN
		RETURN (prop # NIL) & (FigModels.expandable IN prop.valid) & prop.expandable
			& (FigModels.minAnchors IN prop.valid)
			& ((prop.minAnchors = FigModels.undefined) OR (prop.minAnchors < ListLength(anchors)))
	END CanBeReduced;

	PROCEDURE GetRemoveAnchorPar (OUT model: FigModels.Model; OUT view: FigViews.View; 
			OUT fig: FigModels.Figure; OUT pnt: FigModels.ConstrainedPoint;
			OUT this: FigModels.Point);
		VAR nofP, nofF: INTEGER; v: FigViews.View;
			m: FigModels.Model; prop: FigModels.StdProp; plist: FigModels.PointList;
			p1, p2: FigModels.Point; f: FigModels.Figure; p: FigModels.ConstrainedPoint;
	BEGIN
		model := NIL; fig := NIL; pnt := NIL; v := FigViews.Focus(); view := v;
		IF v # NIL THEN
			m := v.ThisModel();
			nofP := v.NofSelectedObjects(FigViews.points); nofF := v.NofSelectedObjects(FigViews.figures);
			IF (nofF = 0) & (nofP = 2) THEN
				plist := v.SelectedPoints(); p1 := plist.This(); p2 := plist.Next().This();
				IF p1 IS FigModels.ConstrainedPoint THEN
					p := p1(FigModels.ConstrainedPoint); PollPointProp(p, prop);
					IF CanBeReduced(p.Anchors(), prop) THEN
						plist := p.Anchors(); WHILE (plist # NIL) & (plist.This() # p2) DO plist := plist.Next() END;
						IF plist # NIL THEN model := m; pnt := p; this := p2 END
					END
				END
			ELSIF (nofF = 1) & (nofP = 1) THEN
				f := v.SelectedFigures().This(); PollFigureProp(f, prop);
				IF CanBeReduced(f.Anchors(), prop) THEN
					p1 := v.SelectedPoints().This(); plist := f.Anchors();
					WHILE (plist # NIL) & (plist.This() # p1) DO plist := plist.Next() END;
					IF plist # NIL THEN model := m; fig := f; this := p1 END
				END
			END
		END
	END GetRemoveAnchorPar;

	PROCEDURE RemoveAnchorGuard* (VAR par: Dialog.Par);
		VAR m: FigModels.Model; f: FigModels.Figure; this: FigModels.Point;
			p: FigModels.ConstrainedPoint; v: FigViews.View;
	BEGIN
		GetRemoveAnchorPar(m, v, f, p, this);
		par.disabled := m = NIL;
		IF f # NIL THEN par.label := "Remove Anchor from Figure"
		ELSIF p # NIL THEN par.label := "Remove Anchor from Point"
		ELSE par.label := "Remove Anchor"
		END
	END RemoveAnchorGuard;

	PROCEDURE RemoveAnchor*;
		VAR m: FigModels.Model; v: FigViews.View;
			f: FigModels.Figure; this: FigModels.Point; p: FigModels.ConstrainedPoint;
			s: Stores.Operation;
	BEGIN
		GetRemoveAnchorPar(m, v, f, p, this);
		IF f # NIL THEN
			Models.BeginScript(m, "Remove Anchor from Figure", s);
			m.RemoveAnchor(f, this);
			IF this IS FigModels.FreePoint THEN m.RemoveObject(this) END;
			Models.EndScript(m, s)
		ELSIF p # NIL THEN
			Models.BeginScript(m, "Remove Anchor from Point", s);
			m.RemoveAnchor(p, this); m.MovePoint(p, 0, 0);
			IF this IS FigModels.FreePoint THEN m.RemoveObject(this) END;
			Models.EndScript(m, s)
		END
	END RemoveAnchor;

	PROCEDURE SplitPoints*;
		VAR v: FigViews.View;
			m: FigModels.Model;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN m := v.ThisModel(); m.SplitPoint(v.SelectedPoints().This()) END
	END SplitPoints;

	PROCEDURE SplitPointsGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL) OR (v.NofSelectedObjects(FigViews.points) # 1)
	END SplitPointsGuard;

	PROCEDURE JoinPoints*;
		VAR v: FigViews.View;
			m: FigModels.Model;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN m := v.ThisModel(); m.JoinPoints(v.SelectedPoints().Next(), v.SelectedPoints().This()) END
	END JoinPoints;

	PROCEDURE JoinPointsGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
			m: FigModels.Model;
	BEGIN
		v := FigViews.Focus(); IF v # NIL THEN m := v.ThisModel() END;
		par.disabled := (v = NIL) OR (v.NofSelectedObjects(FigViews.points) < 2)
								OR ~(m.JoinPossible(v.SelectedPoints().Next(), v.SelectedPoints().This()))
	END JoinPointsGuard;

	PROCEDURE SendToBack*;
		VAR v: FigViews.View;
			m: FigModels.Model;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN m := v.ThisModel(); m.SendToBack(v.SelectedFigures()) END
	END SendToBack;

	PROCEDURE SendToBackGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL) OR (v.SelectedFigures() = NIL)
	END SendToBackGuard;

	PROCEDURE BringToFront*;
		VAR v: FigViews.View;
			m: FigModels.Model;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN m := v.ThisModel(); m.BringToFront(v.SelectedFigures()) END
	END BringToFront;

	PROCEDURE BringToFrontGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL) OR (v.SelectedFigures() = NIL)
	END BringToFrontGuard;
	
	PROCEDURE SetGrid*;
		VAR v: FigViews.View;
			unit: INTEGER;
	BEGIN
		IF gridDlg.unitType = mm THEN unit := Ports.mm
		ELSIF gridDlg.unitType = inch THEN unit := Ports.inch DIV 16
		ELSIF gridDlg.unitType = point THEN unit := Ports.point
		ELSE
		END;
		v := FigViews.Focus();
		IF v # NIL THEN v.SetGrid(gridDlg.grid * unit, gridDlg.gridDrawingFactor) END
	END SetGrid;

	PROCEDURE SetGridGuard*(VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL)
	END SetGridGuard;

	PROCEDURE ToggleGrid*;
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN v.ToggleGrid END
	END ToggleGrid;

	PROCEDURE ToggleGridGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			par.checked := v.GridOn()
		END;
		par.disabled := (v = NIL)
	END ToggleGridGuard;

	PROCEDURE TogglePointhiding*;
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN v.TogglePointhiding END
	END TogglePointhiding;

	PROCEDURE TogglePointhidingGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			par.checked := ~v.ShowingPoints()
		END;
		par.disabled := (v = NIL)
	END TogglePointhidingGuard;

	PROCEDURE ToggleGridhiding*;
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN v.ToggleGridhiding END
	END ToggleGridhiding;

	PROCEDURE ToggleGridhidingGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			par.checked := ~v.ShowingGrid()
		END;
		par.disabled := (v = NIL)
	END ToggleGridhidingGuard;

	PROCEDURE ToggleEditLock*;
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN v.ToggleEditLock END
	END ToggleEditLock;

	PROCEDURE ToggleEditLockGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			par.checked := v.EditLockOn()
		END;
		par.disabled := (v = NIL)
	END ToggleEditLockGuard;

	PROCEDURE ForceToGrid*;
		VAR m: FigModels.Model; v: FigViews.View; grid, factor: INTEGER;
	BEGIN
		v := FigViews.Focus();
		IF v # NIL THEN
			m:= v.ThisModel(); v.GetGrid(grid, factor); m.MoveToGrid(v.SelectedPoints(), grid)
		END
	END ForceToGrid;

	PROCEDURE ForceToGridGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL) OR ~(v.GridOn()) OR (v.SelectedPoints() = NIL)
	END ForceToGridGuard;

	PROCEDURE InitGridDialog*;
		VAR v: FigViews.View; unit: INTEGER;
	BEGIN
		v := FigViews.Focus();
		IF gridDlg.unitType = mm THEN unit := Ports.mm
		ELSIF gridDlg.unitType = inch THEN unit := Ports.inch DIV 16
		ELSIF gridDlg.unitType = point THEN unit := Ports.point
		ELSE
		END;
		IF v # NIL THEN
			v.GetGrid(gridDlg.grid, gridDlg.gridDrawingFactor);
			gridDlg.grid := gridDlg.grid DIV unit
		END
	END InitGridDialog;

	PROCEDURE GetExtensionType (OUT typename: ARRAY OF CHAR; OUT isAlien, isFree: BOOLEAN);
		VAR i, j: INTEGER; v: FigViews.View; object: FigModels.Object;
	BEGIN
		typename := "";
		v := FigViews.Focus();
		IF v # NIL THEN
			object := v.LatestSelectedObject(FigViews.both);
			IF object # NIL THEN
				WITH object: FigModels.AlienFigure DO
					isAlien := TRUE; isFree := FALSE; typename := "Alien Figure"
				| object: FigModels.AlienPoint DO
					isAlien := TRUE; isFree := FALSE; typename := "Alien Point"
				| object: FigModels.ConstrainedPoint DO
					isAlien := FALSE; isFree := FALSE; Services.GetTypeName(object, typename)
				| object: FigModels.FreePoint DO
					isAlien := FALSE; isFree := TRUE; typename := "Free Point"
				| object: FigModels.Figure DO
					isAlien := FALSE; isFree := FALSE;  Services.GetTypeName(object, typename)
				END;
			END;
			IF ~isAlien & ~isFree & (typename # "") THEN
				i := 0; j := 5; WHILE typename[j] # "." DO typename[i] := typename[j]; INC(i); INC(j) END;
				typename[i] := 0X
			END
		END
	END GetExtensionType;

	
	PROCEDURE Deposit*;
	VAR v: FigViews.View;
	BEGIN
		v := FigViews.stdDir.New(FigModels.dir.New());
		Views.Deposit(v)
	END Deposit;

	PROCEDURE SelectionGuard* (nofPoints: INTEGER; VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL) OR (v.NofSelectedObjects(FigViews.points) < nofPoints)
	END SelectionGuard;

	PROCEDURE FocusGuard* (VAR par: Dialog.Par);
		VAR v: FigViews.View;
	BEGIN
		v := FigViews.Focus();
		par.disabled := (v = NIL)
	END FocusGuard;

	PROCEDURE UpDownGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := (TPropDlg.type = fill) OR (TPropDlg.type = hairline) OR ~TPropDlg.initiated
	END UpDownGuard;

	PROCEDURE GridNotifier* (op, from_, to_: INTEGER);
	BEGIN
		IF op = Dialog.changed THEN
			IF gridDlg.grid < 1 THEN gridDlg.grid := 1 END;
			IF gridDlg.gridDrawingFactor < 1 THEN gridDlg.gridDrawingFactor := 1 END;
			Dialog.Update(gridDlg)
		END
	END GridNotifier;
	
	PROCEDURE UpDownNotifier* (op, from_, to_: INTEGER);
	BEGIN
		IF op = Dialog.changed THEN
			IF TPropDlg.thickness < 1 THEN TPropDlg.thickness := 1 END;
			Dialog.Update(TPropDlg)
		END
	END UpDownNotifier;
	
	PROCEDURE InitTPropDialog*;
		VAR p: Properties.Property;
	BEGIN
		TPropDlg.thickness := 0; TPropDlg.type := mm; TPropDlg.initiated := FALSE;
		Properties.CollectProp(p); WHILE (p # NIL) & ~(p IS  FigModels.TProp) DO p := p.next END;
		IF p # NIL THEN
			WITH p: FigModels.TProp DO
				IF FigModels.thickness IN p.valid THEN
					IF p.thickness = Ports.fill THEN TPropDlg.thickness := 1; TPropDlg.type := fill
					ELSIF p.thickness = 0 THEN TPropDlg.thickness := 1; TPropDlg.type := hairline
					ELSIF p.thickness MOD Ports.mm = 0 THEN
						TPropDlg.thickness := p.thickness DIV Ports.mm; TPropDlg.type := mm
					ELSIF p.thickness MOD (Ports.inch DIV 16) = 0 THEN
						TPropDlg.thickness := p.thickness DIV (Ports.inch DIV 16); TPropDlg.type := inch
					ELSIF p.thickness MOD Ports.point = 0 THEN
						TPropDlg.thickness := p.thickness DIV Ports.point; TPropDlg.type := point
					END;
					TPropDlg.initiated := TRUE
				END
			END
		END;
		Dialog.Update(TPropDlg)
	END InitTPropDialog;
	
	PROCEDURE SetTProp*;
		VAR p: FigModels.TProp; unit: INTEGER;
	BEGIN
		IF TPropDlg.type = mm THEN unit := Ports.mm
		ELSIF TPropDlg.type = inch THEN unit := Ports.inch DIV 16
		ELSIF TPropDlg.type = point THEN unit := Ports.point
		ELSE
		END;
		NEW(p);
		IF TPropDlg.type = fill THEN p.thickness := Ports.fill
		ELSIF TPropDlg.type = hairline THEN p.thickness := 0
		ELSE p.thickness := TPropDlg.thickness * unit
		END;
		p.known := {FigModels.thickness}; p.valid := p.known;
		IF p.valid # {} THEN Properties.EmitProp(NIL, p) END
	END SetTProp;

	PROCEDURE ThicknessDlgGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := ~TPropDlg.initiated
	END ThicknessDlgGuard;
	
	PROCEDURE InitArrowPropDialog*;
		VAR p: Properties.Property;
	BEGIN
		arrowPropDlg.type := 0; arrowPropDlg.initiated := FALSE;
		Properties.CollectProp(p);
		WHILE (p # NIL) & ~(p IS FigModels.ArrowProp) DO p := p.next END;
		IF p # NIL THEN
			WITH p: FigModels.ArrowProp DO
				IF FigModels.type IN p.valid THEN
					arrowPropDlg.type := p.type;
					arrowPropDlg.initiated := TRUE
				END
			ELSE
			END
		END;
		Dialog.Update(arrowPropDlg)
	END InitArrowPropDialog;

	PROCEDURE SetArrowProp*;
		VAR p: FigModels.ArrowProp;
	BEGIN
		NEW(p); p.type := arrowPropDlg.type; p.known := {FigModels.type}; p.valid := p.known;
		IF arrowPropDlg.type = 0 THEN EXCL(p.valid, FigModels.type) END;
		IF p.valid # {} THEN Properties.EmitProp(NIL, p) END
	END SetArrowProp;

	PROCEDURE ArrowDlgGuard* (VAR par: Dialog.Par);
	BEGIN
		par.disabled := ~arrowPropDlg.initiated
	END ArrowDlgGuard;

BEGIN
	gridDlg.grid := FigViews.initialGrid; gridDlg.gridDrawingFactor := FigViews.initialGridDrawingFactor;
	gridDlg.unitType := mm;
	TPropDlg.type := hairline; TPropDlg.initiated := FALSE;
	arrowPropDlg.type := FigModels.noArrow; arrowPropDlg.initiated := FALSE
END FigCmds.
