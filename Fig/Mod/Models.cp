MODULE FigModels;

	IMPORT Dialog, Ports, Stores, Models, Properties;

	CONST
		undefined* = -1;
		last = -1;
		ok = 1; toBeMoved = 2;
		currentStdVersion = 3; minStdVersion = 3; maxStdVersion = 3;
		currentPBVersion = 1; minPBVersion = 1; maxPBVersion = 1;

		(* StdProp *)
		expandable* = 0; cyclic* = 1; minAnchors* = 2; maxAnchors* = 3;

		(* TProp *)
		thickness* = 0;
		
		(* ArrowProp *)
		noArrow* = 1; beginArrow* = 2; endArrow* = 3; doubleArrow* = 4;
		type* = 0;

		currentFWVersion = 4; minFWVersion = 4; maxFWVersion = 4;
		
	TYPE 
		Model* = POINTER TO LIMITED RECORD (Models.Model)
			base-: ModelBase
		END;

		ModelBase* = POINTER TO ABSTRACT RECORD (Stores.Store)
			model-: Model
		END;

		Directory* = POINTER TO ABSTRACT RECORD END;

		PointBase* = POINTER TO ABSTRACT RECORD (Stores.Store)
			ext-: Point
		END;

		Object* = POINTER TO ABSTRACT RECORD (Stores.Store) END;

		Point* = POINTER TO ABSTRACT RECORD (Object)
			base-: PointBase
		END;

		FreePoint* = POINTER TO LIMITED RECORD (Point) END;

		ConstrainedPoint* = POINTER TO ABSTRACT RECORD (Point) END;

		AlienPoint* = POINTER TO LIMITED RECORD (ConstrainedPoint)
			store-: Stores.Alien
		END;
	
		PointList* = POINTER TO ABSTRACT RECORD END;
		
		(*
		StdPointList = POINTER TO RECORD (PointList)
			p: Point;
			next: StdPointList
		END;
		*)
		
		FigureBase* = POINTER TO ABSTRACT RECORD (Stores.Store)
			ext-: Figure
		END;
		
		Figure* = POINTER TO ABSTRACT RECORD (Object)
			base-: FigureBase
		END;
		
		AlienFigure* = POINTER TO LIMITED RECORD (Figure)
			store-: Stores.Alien
		END;

		FigureList* = POINTER TO ABSTRACT RECORD END;
		
		(*
		StdFigureList = POINTER TO RECORD (FigureList)
			f: Figure;
			next: StdFigureList
		END;
		*)
		
		PointChooser* = POINTER TO ABSTRACT RECORD END;
		
		FigureChooser* = POINTER TO ABSTRACT RECORD END;
		
		StdProp* = POINTER TO RECORD (Properties.Property)
			minAnchors*, maxAnchors*: INTEGER;
			expandable*, cyclic*: BOOLEAN
		END;

		TProp* = POINTER TO RECORD (Properties.Property)
			thickness*: INTEGER
		END;
		
		ArrowProp* = POINTER TO RECORD (Properties.Property)
			type*: INTEGER
		END;

		UpdateMsg* = EXTENSIBLE RECORD (Models.UpdateMsg)
			l*, t*, r*, b*: INTEGER
		END;	

		InsertMsg* = RECORD (UpdateMsg)
			insert*: BOOLEAN;
			figure*: Figure;
			point*: Point
		END;

		AnchorMsg* = RECORD (UpdateMsg)
			add*: BOOLEAN;
			figure*: Figure;
			comppnt*: ConstrainedPoint;
			anchor*: Point;
			pos*: INTEGER
		END;

		ChangeAnchorMsg* = RECORD (UpdateMsg)
			newPoint*: Point;
			oldPoint*: Point;
			figure*: Figure;
			compPoint*: ConstrainedPoint;
			pos*: INTEGER
		END;

		ThesePointsChooser = POINTER TO RECORD (PointChooser)
			l, t, r, b: INTEGER
		END;
		
		TheseFiguresChooser = POINTER TO RECORD (FigureChooser)
			x, y: INTEGER
		END;
		
		IntersectingFiguresChooser = POINTER TO RECORD (FigureChooser)
			l, t, r, b: INTEGER
		END;

		IncludedFiguresChooser = POINTER TO RECORD (FigureChooser)
			l, t, r, b: INTEGER
		END;
		
	TYPE
		StdPointBase = POINTER TO RECORD (PointBase)
			m: Model;
			x, y: INTEGER;
			anchors: StdPointList;
			dependingPoints: StdPointList;
			dependingFigures: StdFigureList;
			id: INTEGER		
		END;

		StdPointList = POINTER TO RECORD (PointList)
			p: Point;
			next: StdPointList;
			id: INTEGER
		END;

		StdFigureBase = POINTER TO RECORD (FigureBase)
			m: Model;
			anchors: StdPointList;
			id: INTEGER
		END;

		StdFigureList = POINTER TO RECORD (FigureList)
			f: Figure;
			next: StdFigureList;
			id: INTEGER
		END;

		StdModelBase = POINTER TO RECORD (ModelBase)
			points: StdPointList;
			figures: StdFigureList
		END;

		StdDirectory = POINTER TO RECORD (Directory) END;

		MoveList = POINTER TO RECORD
			p: Point;
			x, y: INTEGER;
			next: MoveList
		END;

		PointOp = POINTER TO RECORD (Stores.Operation)
			insert: BOOLEAN;
			model: StdModelBase;
			point: Point;
			pos: INTEGER
		END;

		FigureOp = POINTER TO RECORD (Stores.Operation)
			insert: BOOLEAN;
			model: StdModelBase;
			figure: Figure;
			pos: INTEGER
		END;
		
		FigureAnchorOp = POINTER TO RECORD (Stores.Operation)
			add: BOOLEAN;
			depID, anchorID: INTEGER;
			model: StdModelBase;
			anchor: Point;
			fig: Figure
		END;
		
		PointAnchorOp = POINTER TO RECORD (Stores.Operation)
			add: BOOLEAN;
			depID, anchorID: INTEGER;
			model: StdModelBase;
			anchor: Point;
			deppnt: ConstrainedPoint
		END;

		MoveOp = POINTER TO RECORD (Stores.Operation)
			m: StdModelBase;
			first: BOOLEAN;
			list: MoveList
		END;

		SwapOp = POINTER TO RECORD (Stores.Operation)
			model: StdModelBase;
			flist: StdFigureList;
			do, front: BOOLEAN
		END;

		LinkOp = POINTER TO RECORD (Stores.Operation)
			model: StdModelBase;
			target: Point;
			source: Point;
			sourceList: StdPointList;
			figure: Figure;
			point: ConstrainedPoint
		END;
		

	VAR
		dir-, stdDir-: Directory;
		alienFigures: StdFigureList;
		alienPoints: StdPointList;

	(* FigureList *)
	
	PROCEDURE (f: FigureList) This* (): Figure, NEW, ABSTRACT;
	PROCEDURE (f: FigureList) Next* (): FigureList, NEW, ABSTRACT;

	(* PointList *)
	
	PROCEDURE (p: PointList) This* (): Point, NEW, ABSTRACT;
	PROCEDURE (p: PointList) Next* (): PointList, NEW, ABSTRACT;

	(* StdFigureList
	
	PROCEDURE (f: StdFigureList) This (): Figure;
	BEGIN
		RETURN f.f
	END This;
	
	PROCEDURE (f: StdFigureList) Next (): FigureList;
	BEGIN
		RETURN f.next
	END Next;
	
	 *)

	(* StdPointList 
	
	PROCEDURE (p: StdPointList) This (): Point;
	BEGIN
		RETURN p.p
	END This;
	
	PROCEDURE (p: StdPointList) Next (): PointList;
	BEGIN
		RETURN p.next
	END Next;
	*)

	(* PointBase *)
	
	PROCEDURE (p: PointBase) Model- (): Model, NEW, ABSTRACT;
	PROCEDURE (p: PointBase) X- (): INTEGER, NEW, ABSTRACT;
	PROCEDURE (p: PointBase) Y- (): INTEGER, NEW, ABSTRACT;
	PROCEDURE (p: PointBase) Anchors- (): PointList, NEW, ABSTRACT;
	PROCEDURE (p: PointBase) DependingPoints- (): PointList, NEW, ABSTRACT;
	PROCEDURE (p: PointBase) DependingFigures- (): FigureList, NEW, ABSTRACT;

	(* FigureBase *)
	
	PROCEDURE (f: FigureBase) Model- ():Model, NEW, ABSTRACT;
	PROCEDURE (f: FigureBase) Anchors- (): PointList, NEW, ABSTRACT;

	(* Point *)

	PROCEDURE (p: Point) Model* (): Model, NEW;
	BEGIN
		RETURN p.base.Model()
	END Model;

	PROCEDURE (p: Point) X* (): INTEGER, NEW;
	BEGIN
		RETURN p.base.X()
	END X;

	PROCEDURE (p: Point) Y* (): INTEGER, NEW;
	BEGIN
		RETURN p.base.Y()
	END Y;

	PROCEDURE (p: Point) DependingPoints* (): PointList, NEW;
	BEGIN
		RETURN p.base.DependingPoints()
	END DependingPoints;

	PROCEDURE (p: Point) DependingFigures* (): FigureList, NEW;
	BEGIN
		RETURN p.base.DependingFigures()
	END DependingFigures;
	
	(* Figure *)
	
	PROCEDURE (fig: Figure) Model* (): Model, NEW;
	BEGIN
		RETURN fig.base.Model()
	END Model;

	PROCEDURE (fig: Figure) Anchors* (): PointList, NEW;
	BEGIN
		RETURN fig.base.Anchors()
	END Anchors;

	PROCEDURE (fig: Figure) Draw* (f: Ports.Frame; l, t, r, b: INTEGER), NEW, ABSTRACT;

	PROCEDURE (fig: Figure) Action*, NEW, EMPTY;
		
	PROCEDURE (fig: Figure) GetBoundingBox* (OUT l, t, r, b: INTEGER), NEW, EXTENSIBLE;
		VAR plist : PointList; p: Point; x: INTEGER;
	BEGIN
		plist := fig.Anchors();	
		IF plist # NIL THEN
			p := plist.This(); l := p.X(); r := p.X(); t := p.Y(); b := p.Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				p := plist.This(); l := MIN(l, p.X()); r := MAX(r, p.X()); t := MIN(t, p.Y()); b := MAX(b, p.Y());
				plist := plist.Next()
			END
		END;
		IF (r - l) < Ports.mm * 3 THEN x := (Ports.mm  * 3 - (r - l)) DIV 2; DEC(l, x); INC(r, x) END;
		IF (b - t) < Ports.mm * 3 THEN x := (Ports.mm * 3 - (b - t)) DIV 2; DEC(t, x); INC(b, x) END
	END GetBoundingBox;

	PROCEDURE (fig: Figure) Covers* (x, y: INTEGER): BOOLEAN, NEW, EXTENSIBLE;
		VAR l, t, r, b: INTEGER;
	BEGIN
		fig.GetBoundingBox(l, t, r, b);
		RETURN (l <= x) & (x <= r) & (t <= y) & (y <= b)
	END Covers;

	PROCEDURE (fig: Figure) PollProp* (OUT p: Properties.Property), NEW, EXTENSIBLE;
	BEGIN
		p := NIL
	END PollProp;

	PROCEDURE (fig: Figure) SetProp* (p: Properties.Property), NEW, EMPTY;

	(* FreePoint *)
			
	(* ConstrainedPoint *)
	
	PROCEDURE (c: ConstrainedPoint) Anchors* (): PointList, NEW;
	BEGIN
		RETURN c.base.Anchors()
	END Anchors;

	PROCEDURE (c: ConstrainedPoint) PollProp* (OUT p: Properties.Property), NEW, EXTENSIBLE;
	BEGIN
		p := NIL
	END PollProp;

	PROCEDURE (c: ConstrainedPoint) SetProp* (p: Properties.Property), NEW, EMPTY;

	PROCEDURE (c: ConstrainedPoint) Action*, NEW, EMPTY;

	PROCEDURE (c: ConstrainedPoint) Calculate* (VAR x, y: INTEGER), NEW, EMPTY;
	
	(* PointChooser *)
	
	PROCEDURE (c: PointChooser) Choose* (p: Point): BOOLEAN, NEW, ABSTRACT;
	
	(* FigureChooser *)

	PROCEDURE (c: FigureChooser) Choose* (f: Figure): BOOLEAN, NEW, ABSTRACT;

	(* Directory *)
	
	PROCEDURE (d: Directory) NewModelBase- (): ModelBase, NEW, ABSTRACT;	
	
	PROCEDURE (d: Directory) New* (): Model, NEW;
		VAR m: Model; base: ModelBase;
	BEGIN
		NEW(m); base := d.NewModelBase(); m.base := base; base.model := m;
		Stores.Join(m, m.base);
		RETURN m
	END New;
			
	(* ModelBase *)
	
	PROCEDURE (m: ModelBase) InitFrom- (source: ModelBase), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) Points- (): PointList, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) Figures- (): FigureList, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) InsertObject- (o: Object; anchors: PointList; x, y: INTEGER), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) RemoveObject- (o: Object), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) NewPointBase- (): PointBase, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) NewFigureBase- (): FigureBase, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) AddAnchor- (o: Object; pos: INTEGER; p: Point), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) RemoveAnchor- (o: Object; p: Point), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) ChangeAnchor- (o: Object; p: Point; pos: INTEGER), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) BringToFront- (flist: FigureList), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) SendToBack- (flist: FigureList), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) ThisPointList- (pnts: ARRAY OF Point): PointList, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) ThisFigureList- (figs: ARRAY OF Figure): FigureList, NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) MovePoint- (p: Point; dx, dy: INTEGER), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) MovePoints- (plist: PointList; dx, dy: INTEGER), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) MoveToGrid- (plist: PointList; grid: INTEGER), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) InsertCopy- (source: ModelBase; plist: PointList; flist: FigureList;
																		OUT newPoints: PointList; OUT newFigures: FigureList),
																		NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) WriteData- (VAR wr: Stores.Writer), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) ReadData- (VAR rd: Stores.Reader), NEW, ABSTRACT;
	PROCEDURE (m: ModelBase) CopyDataFrom- (source: Stores.Store), NEW, ABSTRACT;

	PROCEDURE (m: ModelBase) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentFWVersion);
		wr.WriteStore(m.model);
		m.WriteData(wr)
	END Externalize;

	PROCEDURE (m: ModelBase) Internalize- (VAR rd: Stores.Reader);
		VAR version: INTEGER; s: Stores.Store;
	BEGIN
		rd.ReadVersion(minFWVersion, maxFWVersion, version);
		IF ~rd.cancelled THEN
			rd.ReadStore(s);
			IF s IS Model THEN
				m.model := s(Model);
				m.ReadData(rd)
			ELSE
				rd.TurnIntoAlien(Stores.alienComponent)
			END
		END
	END Internalize;
	
	PROCEDURE (m: ModelBase) CopyFrom- (source: Stores.Store);
	END CopyFrom;
	
	(* Model *)
	
	PROCEDURE (m: Model) Points* (): PointList, NEW;
	BEGIN
		RETURN m.base.Points()
	END Points;
	
	PROCEDURE (m: Model) Figures* (): FigureList, NEW;
	BEGIN
		RETURN m.base.Figures()
	END Figures;

	PROCEDURE (m: Model) CopyFrom- (source: Stores.Store);
		VAR s: Stores.Store;
	BEGIN
		WITH source: Model DO
			s := Stores.CopyOf(source.base);
			m.base := s(ModelBase); s(ModelBase).model := m;
			m.base.CopyDataFrom(source.base)
		END
	END CopyFrom;
	
	PROCEDURE (m: Model) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentFWVersion);
		wr.WriteStore(m.base)
	END Externalize;
	
	PROCEDURE (m: Model) Internalize- (VAR rd: Stores.Reader);
		VAR s: Stores.Store; version: INTEGER;
	BEGIN
		rd.ReadVersion(minFWVersion, maxFWVersion, version);
		IF ~rd.cancelled THEN
			alienFigures := NIL; alienPoints := NIL;
			rd.ReadStore(s);
			IF s IS ModelBase THEN
				m.base := s(ModelBase)
			ELSE
				rd.TurnIntoAlien(Stores.alienComponent)
			END
		END
	END Internalize;	
	
	PROCEDURE (m: Model) MovePoint* (p: Point; dx, dy: INTEGER), NEW;
	BEGIN
		m.base.MovePoint(p, dx, dy)
	END MovePoint;
	
	PROCEDURE (m: Model) MovePoints* (plist: PointList; dx, dy: INTEGER), NEW;
	BEGIN
		m.base.MovePoints(plist, dx, dy)
	END MovePoints;
	
	PROCEDURE (m: Model) MoveToGrid* (plist: PointList; grid: INTEGER), NEW;
	BEGIN
		m.base.MoveToGrid(plist, grid)
	END MoveToGrid;
	
	PROCEDURE (m: Model) InsertCopy* (source: Model; plist: PointList; flist: FigureList;
																OUT newPoints: PointList; OUT newFigures: FigureList
	), NEW;
		VAR script: Stores.Operation;
	BEGIN
		Models.BeginScript(m, "Copy", script);
		m.base.InsertCopy(source.base, plist, flist, newPoints, newFigures);
		Models.EndScript(m, script)
	END InsertCopy;
	
	PROCEDURE (m: Model) BringToFront* (flist: FigureList), NEW;
	BEGIN
		m.base.BringToFront(flist)
	END BringToFront;
	
	PROCEDURE (m: Model) SendToBack* (flist: FigureList), NEW;
	BEGIN
		m.base.SendToBack(flist)
	END SendToBack;
		
	PROCEDURE (m: Model) ThisPointList* (pnts: ARRAY OF Point): PointList, NEW;
	BEGIN
		RETURN m.base.ThisPointList(pnts)
	END ThisPointList;
	
	PROCEDURE (m: Model) ThisFigureList* (figs: ARRAY OF Figure): FigureList, NEW;
	BEGIN
		RETURN m.base.ThisFigureList(figs)
	END ThisFigureList;

	PROCEDURE (m: Model) ChoosePoints* (plist: PointList; c: PointChooser): PointList, NEW;
		VAR tmplist: PointList; a, b: POINTER TO ARRAY OF Point; n, id: INTEGER; p: Point;
	BEGIN
		ASSERT(c # NIL, 20);
		IF plist # NIL THEN ASSERT(plist.This().Model() = m, 21) END;
		tmplist := plist; n := 0; WHILE tmplist # NIL DO tmplist := tmplist.Next(); INC(n) END;
		IF n > 0 THEN
			NEW(a, n); id := 0; tmplist := plist;
			WHILE plist # NIL DO
				p := plist.This();
				IF c.Choose(p) THEN a[id] := p; INC(id) END;
				plist := plist.Next()
			END;
			IF id > 0 THEN
				NEW(b, id); FOR n := 0 TO id - 1 DO b[n] := a[n] END;
				RETURN m.ThisPointList(b)
			ELSE RETURN NIL
			END
		ELSE RETURN NIL
		END
	END ChoosePoints;
	
	PROCEDURE (m: Model) ChooseFigures* (flist: FigureList; c: FigureChooser): FigureList, NEW;
		VAR tmplist: FigureList; a, b: POINTER TO ARRAY OF Figure; n, id: INTEGER; f: Figure;
	BEGIN
		ASSERT(c # NIL, 20);
		IF flist # NIL THEN ASSERT(flist.This().Model() = m, 21) END;
		tmplist := flist; n := 0; WHILE tmplist # NIL DO tmplist := tmplist.Next(); INC(n) END;
		IF n > 0 THEN
			NEW(a, n); id := 0; tmplist := flist;
			WHILE flist # NIL DO
				f := flist.This();
				IF c.Choose(f) THEN a[id] := f; INC(id) END;
				flist := flist.Next()
			END;
			IF id > 0 THEN
				NEW(b, id); FOR n := 0 TO id - 1 DO b[n] := a[n] END;
				RETURN m.ThisFigureList(b)
			ELSE RETURN NIL
			END
		ELSE RETURN NIL
		END
	END ChooseFigures;
	
	PROCEDURE (m: Model) InsertObject* (o: Object; anchors: PointList; x, y: INTEGER), NEW;
		VAR figbase: FigureBase; pntbase: PointBase; script: Stores.Operation;
	BEGIN
		ASSERT(o # NIL, 20);
		ASSERT(anchors # NIL, 21);
		ASSERT(anchors.This().Model() = m, 22);
		ASSERT((o IS ConstrainedPoint) OR (o IS Figure), 23);
		IF o IS ConstrainedPoint THEN
			ASSERT((x >= 0) OR (x = undefined), 24);
			ASSERT((y >= 0) OR (y = undefined), 25);
			Models.BeginScript(m, "Insert Point", script);
			pntbase := m.base.NewPointBase(); o(Point).base := pntbase; pntbase.ext := o(Point);
			Stores.Join(o, pntbase); Stores.Join(m, o);
			m.base.InsertObject(o, anchors, x, y);
			Models.EndScript(m, script)
		ELSIF o IS Figure THEN
			ASSERT((x = undefined) & (y = undefined), 26);
			Models.BeginScript(m, "Insert Figure", script);
			figbase := m.base.NewFigureBase(); o(Figure).base := figbase; figbase.ext := o(Figure);
			Stores.Join(o, figbase); Stores.Join(m, o);
			m.base.InsertObject(o, anchors, undefined, undefined);
			Models.EndScript(m, script)
		END
	END InsertObject;

	PROCEDURE (m: Model) RemoveObject* (o: Object), NEW;
		VAR script: Stores.Operation;
	BEGIN
		ASSERT(o # NIL, 20);
		ASSERT((o IS Point) OR (o IS Figure), 21);
		IF o IS Point THEN
			ASSERT(o(Point).Model() = m, 22);
			Models.BeginScript(m, "Remove Point", script);
			m.base.RemoveObject(o(Point));
			Models.EndScript(m, script)
		ELSIF o IS Figure THEN
			ASSERT(o(Figure).Model() = m, 23);
			Models.BeginScript(m,"Remove Figure", script);
			m.base.RemoveObject(o(Figure));
			Models.EndScript(m, script)	
		END
	END RemoveObject;

	PROCEDURE (m: Model) NewFreePoint* (x, y: INTEGER): FreePoint, NEW;
		VAR base: PointBase; pnt: FreePoint; script: Stores.Operation;
	BEGIN
		ASSERT(x >= 0, 21);
		ASSERT(y >= 0, 22);
		Models.BeginScript(m, "New Free Point", script);
		base := m.base.NewPointBase(); NEW(pnt); pnt.base := base; base.ext := pnt;
		Stores.Join(pnt, base); Stores.Join(m, pnt);
		m.base.InsertObject(pnt, NIL, x, y);
		Models.EndScript(m, script);
		RETURN pnt
	END NewFreePoint;
		
	PROCEDURE PollProp (o: Object; OUT prop: StdProp);
		VAR p: Properties.Property;
	BEGIN
		IF o IS Figure THEN o(Figure).PollProp(p)
		ELSIF o IS ConstrainedPoint THEN o(ConstrainedPoint).PollProp(p)
		END;
		WHILE (p # NIL) & ~(p IS StdProp) DO p := p.next END;
		IF p # NIL THEN prop := p(StdProp) ELSE prop := NIL END
	END PollProp;

	PROCEDURE CountAnchors (o: Object): INTEGER;
		VAR plist: PointList;
			n: INTEGER;
	BEGIN
		IF o IS ConstrainedPoint THEN plist := o(ConstrainedPoint).Anchors()
		ELSIF o IS Figure THEN plist := o(Figure).Anchors()
		END;
		n := 0; WHILE plist # NIL DO INC(n); plist := plist.Next() END;
		RETURN n
	END CountAnchors;

	PROCEDURE (m: Model) AddAnchor* (o: Object; pos: INTEGER; OUT pnt: Point), NEW;
		VAR x, y, n, id: INTEGER; p0, p1: PointList; prop: StdProp; script: Stores.Operation;
	BEGIN
		ASSERT(o # NIL, 20);
		ASSERT(pos >= 0, 21);
		ASSERT((o IS ConstrainedPoint) OR (o IS Figure), 22);
		IF o IS ConstrainedPoint THEN
			ASSERT(o(ConstrainedPoint).Model() = m, 24);
			p0 := o(ConstrainedPoint).Anchors(); p1 := o(ConstrainedPoint).Anchors()
		ELSIF o IS Figure THEN
			ASSERT(o(Figure).Model() = m, 25);
			p0 := o(Figure).Anchors(); p1 := o(Figure).Anchors()
		END;
		n := CountAnchors(o); PollProp(o, prop);
		IF prop # NIL THEN
			IF (expandable IN prop.valid) & prop.expandable THEN
				IF (maxAnchors IN prop.valid) & ((prop.maxAnchors = undefined)
					OR (n < prop.maxAnchors)) THEN 
					IF  pos = 0 THEN
						WHILE p0.Next() # NIL DO p0 := p0.Next() END;
						IF ((cyclic IN prop.valid) & prop.cyclic) OR (n = 1) THEN
							x := (p0.This().X() + p1.This().X()) DIV 2; y := (p0.This().Y() + p1.This().Y()) DIV 2
						ELSE
							x := p1.This().X() + (p1.This().X() - p1.Next().This().X());
							y := p1.This().Y() + (p1.This().Y() - p1.Next().This().Y())
						END
					ELSIF pos >= n THEN
						IF p0.Next() # NIL THEN
							WHILE (p0.Next().Next() # NIL) DO p0 := p0.Next() END;
							IF (cyclic IN prop.valid) & prop.cyclic THEN
								x := (p0.Next().This().X() + p1.This().X()) DIV 2;
								y := (p0.Next().This().Y() + p1.This().Y()) DIV 2
							ELSE
								x := p0.Next().This().X() + (p0.Next().This().X() - p0.This().X());
								y := p0.Next().This().Y() + (p0.Next().This().Y() - p0.This().Y())
							END
						ELSE x := p0.This().X() + Ports.mm * 10; y := p0.This().Y() + Ports.mm * 10
						END
					ELSE
						id := 1; WHILE id # pos DO p0 := p0.Next(); INC(id) END;
						x := (p0.This().X() + p0.Next().This().X()) DIV 2;
						y := (p0.This().Y() + p0.Next().This().Y()) DIV 2
					END;
					Models.BeginScript(m, "Add Anchorpoint", script);
					pnt := m.NewFreePoint(x, y);
					m.base.AddAnchor(o, pos, pnt);
					Models.EndScript(m, script)
				END
			END
		END		
	END AddAnchor;

	PROCEDURE (m: Model) RemoveAnchor* (o: Object; p: Point), NEW;
		VAR n: INTEGER;
			prop: StdProp;
			plist: PointList;
	BEGIN
		ASSERT(o # NIL, 20);
		ASSERT(p # NIL, 21);
		ASSERT(p.Model() = m, 22);
		ASSERT((o IS ConstrainedPoint) OR (o IS Figure), 23);
		IF o IS ConstrainedPoint THEN
			ASSERT(o(ConstrainedPoint).Model() = m, 24); plist := o(ConstrainedPoint).Anchors()
		ELSIF o IS Figure THEN
			ASSERT(o(Figure).Model() = m, 25); plist := o(Figure).Anchors()
		END;
		n := CountAnchors(o); PollProp(o, prop);
		WHILE (plist # NIL) & (plist.This() # p) DO plist := plist.Next() END;
		IF (prop # NIL) & (expandable IN prop.valid) & prop.expandable & (plist # NIL) & (minAnchors IN prop.valid)
			& (n > prop.minAnchors)
		THEN m.base.RemoveAnchor(o, p)
		END
	END RemoveAnchor;

	PROCEDURE (m: Model) CollectProperties* (flist: FigureList; plist: PointList; OUT prop: Properties.Property), NEW;
		VAR equal: BOOLEAN; p: Properties.Property;
			figs: FigureList;
			pnts: PointList;
	BEGIN
		figs := flist; pnts := plist;
		IF figs # NIL THEN
			figs.This().PollProp(prop); figs := figs.Next();
			WHILE figs # NIL DO figs.This().PollProp(p); Properties.Intersect(prop, p, equal); figs := figs.Next() END
		END;
		IF pnts # NIL THEN
			IF flist = NIL THEN
				IF pnts.This() IS ConstrainedPoint THEN pnts.This()(ConstrainedPoint).PollProp(prop) END;
				pnts := pnts.Next()
			END;
			WHILE pnts # NIL DO
				IF pnts.This() IS ConstrainedPoint THEN
					pnts.This()(ConstrainedPoint).PollProp(p); Properties.Intersect(prop, p, equal)
				END;
				pnts := pnts.Next()
			END
		END
	END CollectProperties;

	PROCEDURE (m: Model) EmitProperties* (flist: FigureList; plist: PointList; prop: Properties.Property), NEW;
		VAR s: Stores.Operation;
	BEGIN
		ASSERT(m # NIL, 20);
		Models.BeginScript(m, "Set Properties", s);
		WHILE flist # NIL DO flist.This().SetProp(prop); flist := flist.Next() END;
		WHILE plist # NIL DO
			IF plist.This() IS ConstrainedPoint THEN plist.This()(ConstrainedPoint).SetProp(prop) END;
			plist := plist.Next()
		END;
		Models.EndScript(m, s)
	END EmitProperties;

	PROCEDURE (m: Model) GetBoundingBox* (plist: PointList; flist: FigureList; OUT l, t, r, b: INTEGER), NEW;
		VAR fl, ft, fr, fb: INTEGER;
	BEGIN
		ASSERT((plist # NIL) OR (flist # NIL), 20);
		IF plist # NIL THEN
			l := plist.This().X(); r := plist.This().X(); t := plist.This().Y(); b := plist.This().Y();
			plist := plist.Next();
			WHILE plist # NIL DO
				l := MIN(l, plist.This().X()); r := MAX(r, plist.This().X()); t := MIN(t, plist.This().Y()); b := MAX(b, plist.This().Y());
				plist := plist.Next()
			END
		ELSE flist.This().GetBoundingBox(l, t, r, b); flist := flist.Next()
		END;
		WHILE flist # NIL DO
			flist.This().GetBoundingBox(fl, ft, fr, fb); l := MIN(l, fl); r := MAX(r, fr); t := MIN(t, ft); b := MAX(b, fb);
			flist := flist.Next()
		END
	END GetBoundingBox;

	PROCEDURE (m: Model) IncludedFigures* (flist: FigureList; l, t, r, b: INTEGER): FigureList, NEW;
		VAR c: IncludedFiguresChooser;
	BEGIN
		NEW(c); c.l := l; c.t := t; c.r := r; c.b := b;
		RETURN m.ChooseFigures(flist, c)
	END IncludedFigures;

	PROCEDURE (m: Model) IntersectingFigures* (flist: FigureList; l, t, r, b: INTEGER): FigureList, NEW;
		VAR c: IntersectingFiguresChooser;
	BEGIN
		NEW(c); c.l := l; c.t := t; c.r := r; c.b := b;
		RETURN m.ChooseFigures(flist, c)
	END IntersectingFigures;

	PROCEDURE (m: Model) TheseFigures* (flist: FigureList; x, y: INTEGER): FigureList, NEW;
		VAR c: TheseFiguresChooser;
	BEGIN
		NEW(c); c.x := x; c.y := y;
		RETURN m.ChooseFigures(flist, c)			
	END TheseFigures;

	PROCEDURE (m: Model) ThesePoints* (plist: PointList; l, t, r, b: INTEGER): PointList, NEW;
		VAR c: ThesePointsChooser;
	BEGIN
		NEW(c); c.l := l; c.t := t; c.r := r; c.b := b;
		RETURN m.ChoosePoints(plist, c)
	END ThesePoints;

	PROCEDURE (m: Model) JoinPossible* ( plist: PointList; target: Point): BOOLEAN, NEW;
		VAR jnp: BOOLEAN;	(* Join NotPossble *)
			tmplist: PointList;
		
		PROCEDURE InList (p: Point; plist: PointList): BOOLEAN;
		BEGIN
			WHILE (plist # NIL) & (plist.This() # p) DO plist := plist.Next() END;
			RETURN plist = NIL
		END InList;

		PROCEDURE DependingOnSelected (p: Point): BOOLEAN;
			VAR q: PointList;
		BEGIN
			IF (p IS ConstrainedPoint) THEN
				q := p(ConstrainedPoint).Anchors();
				WHILE (q # NIL) & InList(q.This(), plist) & ~DependingOnSelected(q.This()) DO q := q.Next() END;
				RETURN q # NIL
			ELSE
				RETURN FALSE
			END
		END DependingOnSelected;

	BEGIN
		ASSERT(plist # NIL, 21);
		ASSERT(target # NIL, 22);
		ASSERT(plist.This().Model() = m, 23);
		ASSERT(target.Model() = m, 24);
		jnp := FALSE;
		tmplist := plist;
		WHILE tmplist # NIL DO
			IF tmplist.This() IS ConstrainedPoint THEN jnp := TRUE END;
			tmplist := tmplist.Next()
		END;
		IF (target IS ConstrainedPoint) & ~(jnp) THEN jnp := DependingOnSelected(target) END;
		tmplist := plist;
		WHILE plist # NIL DO
			IF plist.This() = target THEN jnp := TRUE END;
			plist := plist.Next()
		END;
		RETURN ~jnp
	END JoinPossible;
	
	PROCEDURE (m: Model) JoinPoints* (plist: PointList; target: Point), NEW;
		VAR tmppnt: Point;
			tmplist, tmplist2, deppnts: PointList;
			depfigs: FigureList;
			script: Stores.Operation;
			id: INTEGER;
	BEGIN
		ASSERT(plist # NIL, 21);
		ASSERT(target # NIL, 22);
		ASSERT(plist.This().Model() = m, 23);
		ASSERT(target.Model() = m, 24);
		IF m.JoinPossible(plist, target) THEN
			Models.BeginScript(m, "Join Points", script);
			tmplist := plist;
			WHILE tmplist # NIL DO
				m.MovePoint(tmplist.This(), target.X() - tmplist.This().X(), target.Y() - tmplist.This().Y());
				depfigs := tmplist.This().DependingFigures();
				WHILE depfigs # NIL DO
					tmplist2 := depfigs.This().Anchors(); id := 1;
					WHILE tmplist2.This() # tmplist.This() DO tmplist2 := tmplist2.Next(); INC(id) END;
					m.base.ChangeAnchor(depfigs.This(), target, id);
					depfigs := depfigs.Next()
				END;
				deppnts := tmplist.This().DependingPoints();
				WHILE deppnts # NIL DO
					tmplist2 := deppnts.This()(ConstrainedPoint).Anchors(); id := 1;
					WHILE tmplist2.This() # tmplist.This() DO tmplist2 := tmplist2.Next(); INC(id) END;
					m.base.ChangeAnchor(deppnts.This()(ConstrainedPoint), target, id);
					deppnts := deppnts.Next()
				END;
				tmplist := tmplist.Next()
			END;
			tmplist := plist;
			WHILE tmplist # NIL DO
				tmppnt := tmplist.This(); tmplist := tmplist.Next(); m.base.RemoveObject(tmppnt)
			END;
			Models.EndScript(m, script)
		END
	END JoinPoints;
	
	PROCEDURE (m: Model) SplitPoint* (p: Point), NEW;
		VAR depfigs: FigureList; 
			deppnts, tmplist: PointList; 
			newpnt: Point;
			offset, id: INTEGER;
			script: Stores.Operation;
	BEGIN
		ASSERT(p # NIL, 21);
		ASSERT(p.Model() = m, 22);
		Models.BeginScript(m, "Split points", script);  
		offset := Ports.mm * 3;
		depfigs := p.DependingFigures();
		WHILE (depfigs # NIL) DO
			tmplist := depfigs.This().Anchors(); id := 1;
			WHILE tmplist.This() # p DO tmplist := tmplist.Next(); INC(id) END;
			newpnt := m.NewFreePoint(p.X(), p.Y());
			m.base.ChangeAnchor(depfigs.This(), newpnt, id);
			m.MovePoint(newpnt, offset, 0); offset := offset + Ports.mm * 3;
			depfigs := depfigs.Next()
		END;
		deppnts := p.DependingPoints();
		WHILE (deppnts # NIL) DO
			tmplist := deppnts.This()(ConstrainedPoint).Anchors(); id := 1;
			WHILE tmplist.This() # p DO tmplist := tmplist.Next(); INC(id) END;
			newpnt := m.NewFreePoint(p.X(), p.Y());
			m.base.ChangeAnchor(deppnts.This()(ConstrainedPoint), newpnt, id);
			m.MovePoint(newpnt, offset, 0);offset := offset + Ports.mm * 3;
			deppnts := deppnts.Next()
		END;
		IF ~(p IS ConstrainedPoint) THEN m.base.RemoveObject(p) END;
		Models.EndScript(m, script) 
	END SplitPoint;

	(* StdProp *)

	PROCEDURE (p: StdProp) IntersectWith* (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: StdProp DO
			p.known := p.known + q.known; valid := p.valid * q.valid;
			IF p.expandable # q.expandable THEN EXCL(valid, expandable) END;
			IF p.cyclic # q.cyclic THEN EXCL(valid, cyclic) END;
			IF p.minAnchors # q.minAnchors THEN EXCL(valid, minAnchors) END;
			IF p.maxAnchors # q.maxAnchors THEN EXCL(valid, maxAnchors) END;
			equal := p.valid = valid; p.valid := valid
		END
	END IntersectWith;

	(* TProp *)
	
	PROCEDURE (p: TProp) IntersectWith* (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: TProp DO
			p.known := p.known + q.known; valid := p.valid * q.valid;
			IF p.thickness # q.thickness THEN EXCL(valid, thickness) END;
			equal := p.valid = valid; p.valid := valid
		END
	END IntersectWith;

	(* ArrowProp *)
	
	PROCEDURE (p: ArrowProp) IntersectWith*  (q: Properties.Property; OUT equal: BOOLEAN);
		VAR valid: SET;
	BEGIN
		WITH q: ArrowProp DO
			valid := p.valid * q.valid;
			IF p.type # q.type THEN EXCL(valid, type) END;
			equal := valid = p.valid; p.valid := valid
		END
	END IntersectWith;

	(* ThesePointsChooser *)

	PROCEDURE (c: ThesePointsChooser) Choose (p: Point): BOOLEAN;
		VAR x, y: INTEGER;
	BEGIN
		ASSERT(p # NIL, 20);
		x := p.X(); y := p.Y();
		RETURN (c.l <= x) & (x <= c.r) & (c.t <= y) & (y <= c.b)
	END Choose;

	(* TheseFiguresChooser *)

	PROCEDURE (c: TheseFiguresChooser) Choose (f: Figure): BOOLEAN;
	BEGIN
		ASSERT(f # NIL, 20);
		RETURN f.Covers(c.x, c.y)
	END Choose;

	(* IntersectingFiguresChooser *)

	PROCEDURE (c: IntersectingFiguresChooser) Choose (f: Figure): BOOLEAN;
		VAR l, t, r, b: INTEGER;
	BEGIN
		ASSERT(f # NIL, 20);
		f.GetBoundingBox(l, t, r, b);
		RETURN (c.r >= l) & (r >= c.l) & (c.b >= t) & (b >= c.t)
	END Choose;

	(* IncludedFiguresChooser *)

	PROCEDURE (c: IncludedFiguresChooser) Choose (f: Figure): BOOLEAN;
		VAR l, t, r, b: INTEGER;
	BEGIN
		ASSERT(f # NIL, 20);
		f.GetBoundingBox(l, t, r, b);
		RETURN (c.l <= l) & (c.r >= r) & (c.t <= t) & (c.b >= b)
	END Choose;

	(* AlienFigure *)

	PROCEDURE (fig: AlienFigure) ExternalizeAs- (VAR s1: Stores.Store);
	BEGIN
		s1 := fig.store
	END ExternalizeAs;
	
	PROCEDURE (fig: AlienFigure) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: AlienFigure DO
			fig.store := source.store
		ELSE
		END
	END CopyFrom;
		
	PROCEDURE (fig: AlienFigure) Draw* (f: Ports.Frame; l, t, r, b: INTEGER);
		VAR left, top, right, bottom: INTEGER; alienColor: Ports.Color;
	BEGIN
		fig.Model().GetBoundingBox(fig.Anchors(), NIL, left, top, right, bottom);
		IF Ports.background # Ports.grey50 THEN alienColor := Ports.grey50 ELSE alienColor := Ports.grey25 END;
		f.DrawRect(left, top, right, bottom, Ports.fill, alienColor);  
		f.DrawRect(left, top, right, bottom, 0, Ports.black);
		f.DrawLine(left, top, right, bottom, 0, Ports.black);
		f.DrawLine(left, bottom, right, top, 0, Ports.black)		
	END Draw;

	PROCEDURE (fig: AlienFigure) GetBoundingBox* (OUT l, t, r, b: INTEGER);
	BEGIN
		fig.Model().GetBoundingBox(fig.Anchors(), NIL, l, t, r, b)
	END GetBoundingBox;

	(* AlienPoint *)

	PROCEDURE (p: AlienPoint) ExternalizeAs- (VAR s1: Stores.Store);
	BEGIN
		s1 := p.store
	END ExternalizeAs;

	PROCEDURE (p: AlienPoint) CopyFrom- (source: Stores.Store);
	BEGIN
		WITH source: AlienPoint DO
			p.store := source.store
		END
	END CopyFrom;

	(* Proper procedures *)

	PROCEDURE WritePoint* (VAR wr: Stores.Writer; p: Point);
	BEGIN
		ASSERT(p # NIL, 20);
		wr.WriteStore(p); wr.WriteStore(p.base)
	END WritePoint;
	
	PROCEDURE WriteFigure* (VAR wr: Stores.Writer; f: Figure);
	BEGIN
		ASSERT(f # NIL, 20);
		wr.WriteStore(f); wr.WriteStore(f(Figure).base)
	END WriteFigure;
	
	PROCEDURE ReadPoint* (VAR rd: Stores.Reader; OUT point: Point);
		VAR s: Stores.Store; p: StdPointList; alienPnt: AlienPoint;
	BEGIN
		rd.ReadStore(s);
		IF s IS Point THEN point := s(Point)
		ELSE
			p := alienPoints;
			WHILE (p # NIL) & (p.p(AlienPoint).store # s) DO p := p.next END;
			IF p # NIL THEN alienPnt := p.p(AlienPoint)
			ELSE
				NEW(alienPnt); alienPnt.store := s(Stores.Alien); Stores.Join(alienPnt, s);
				NEW(p); p.p := alienPnt; p.next := alienPoints; alienPoints := p
			END;
			point := alienPnt
		END;
		rd.ReadStore(s);
		IF ~rd.cancelled THEN point.base := s(PointBase); s(PointBase).ext := point END
	END ReadPoint;
	
	PROCEDURE ReadFigure* (VAR rd: Stores.Reader; OUT figure: Figure);
		VAR s: Stores.Store; f: StdFigureList; alienFig: AlienFigure;
	BEGIN
		rd.ReadStore(s);
		IF s IS Figure THEN figure := s(Figure)
		ELSE
			f := alienFigures;
			WHILE (f # NIL) & (f.f(AlienFigure).store # s) DO f := f.next END;
			IF f # NIL THEN alienFig := f.f(AlienFigure)
			ELSE
				NEW(alienFig); alienFig.store := s(Stores.Alien); Stores.Join(alienFig, s);
				NEW(f); f.f := alienFig; f.next := alienFigures; alienFigures := f
			END;
			figure := alienFig
		END;
		rd.ReadStore(s);
		IF ~rd.cancelled THEN figure.base := s(FigureBase); s(FigureBase).ext := figure END
	END ReadFigure;
	
	PROCEDURE CloneOf*(source: Model): Model;
		VAR m: Model; s: Stores.Store;
	BEGIN
		ASSERT(source # NIL, 20);
		NEW(m);
		s := Stores.CopyOf(source.base);
		m.base := s(ModelBase); s(ModelBase).model := m; Stores.Join(m, s);
		m.base.InitFrom(source.base);
		RETURN m
	END CloneOf;

	PROCEDURE SetDir*(d: Directory);
	BEGIN
		ASSERT (d # NIL, 20);
		dir := d
	END SetDir;


	
	(* StdModel *)
	
	(* Misc. procedures *)

	PROCEDURE GiveFigureID (f: StdFigureList);
		VAR i: INTEGER;
	BEGIN
		i := 1; WHILE (f # NIL) DO f.id := i; f.f.base(StdFigureBase).id := i; INC(i); f := f.next END
	END GiveFigureID;

	PROCEDURE GivePointID (p: StdPointList);
		VAR i: INTEGER;
	BEGIN
		i := 1; WHILE (p # NIL) DO p.id := i; p.p.base(StdPointBase).id := i; INC(i); p := p.next END
	END GivePointID;

	PROCEDURE AlienDependencies (pnt: Point): BOOLEAN;
		VAR points: PointList;
	BEGIN
		IF ~(pnt IS AlienPoint) THEN
			points := pnt.DependingPoints();
			WHILE (points # NIL) & ~(points.This() IS AlienPoint) & ~AlienDependencies(points.This()) DO
				points := points.Next()
			END;
			RETURN points # NIL
		ELSE RETURN FALSE
		END
	END AlienDependencies;

	PROCEDURE ReadPointList (VAR rd: Stores.Reader; m: StdModelBase; OUT list: StdPointList);
		VAR i: INTEGER; p: StdPointList; o: Point; (* alien: StdAlienPoint; *)
	BEGIN
		rd.ReadInt(i); p := NIL; list := NIL;
		WHILE i # 0 DO
			ReadPoint(rd, o);
			IF p # NIL THEN NEW(p.next); p := p.next ELSE NEW(list); p := list END;
			p.p := o;
			DEC(i)
		END
	END ReadPointList;

	PROCEDURE ReadFigureList (VAR rd: Stores.Reader; m: StdModelBase; OUT list: StdFigureList);
		VAR i: INTEGER; p: StdFigureList; o: Figure; (* alien: StdAlienFigure; *)
	BEGIN
		rd.ReadInt(i); p := NIL; list := NIL;
		WHILE i # 0 DO
			ReadFigure(rd, o);
			IF p # NIL THEN NEW(p.next); p := p.next ELSE NEW(list); p := list END;
			p.f := o;
			DEC(i)
		END
	END ReadFigureList;

	PROCEDURE WritePointList (VAR wr: Stores.Writer; m: StdModelBase; list: StdPointList);
		VAR i: INTEGER; p: StdPointList;
	BEGIN
		p := list; i := 0; WHILE p # NIL DO INC(i); p := p.next END;
		wr.WriteInt(i);
		p := list;
		WHILE p # NIL DO
			WritePoint(wr, p.p);
			p := p.next
		END
	END WritePointList;

	PROCEDURE WriteFigureList (VAR wr: Stores.Writer; m: StdModelBase; list: StdFigureList);
		VAR i: INTEGER; p: StdFigureList;
	BEGIN
		p := list; i := 0; WHILE p # NIL DO INC(i); p := p.next END;
		wr.WriteInt(i);
		p := list;
		WHILE p # NIL DO
			WriteFigure(wr, p.f);
			p := p.next
		END
	END WriteFigureList;

	(* PointOp *)

	PROCEDURE (op: PointOp) Do;
		VAR m: StdModelBase;
			pnt, plist, prev, tmppnt: StdPointList;
			msg: InsertMsg;
	BEGIN
		m := op.model; GivePointID(m.points);
		NEW(pnt); pnt.p := op.point;
		IF op.insert THEN
			plist := m.points;
			IF plist = NIL THEN pnt.next := NIL; m.points := pnt
			ELSE
				IF op.pos = 1 THEN pnt.next := m.points; m.points := pnt
				ELSE
					WHILE plist.id < op.pos - 1 DO plist := plist.next END; 
					pnt.next := plist.next; plist.next := pnt
				END
			END;
			IF pnt.p IS ConstrainedPoint THEN
				plist := pnt.p.base(StdPointBase).anchors;
				WHILE plist # NIL DO
					NEW(tmppnt); tmppnt.p := pnt.p; tmppnt.next := plist.p.base(StdPointBase).dependingPoints;
					plist.p.base(StdPointBase).dependingPoints := tmppnt; plist := plist.next
				END
			END
		ELSE
			prev := NIL; plist := m.points;
			WHILE plist.id # op.pos DO prev := plist; plist := plist.next END;
			IF prev # NIL THEN prev.next := plist.next ELSE m.points := plist.next END;
			IF pnt.p IS ConstrainedPoint THEN
				plist := pnt.p.base(StdPointBase).anchors;
				WHILE plist # NIL DO
					tmppnt := plist.p.base(StdPointBase).dependingPoints; prev := NIL;
					WHILE tmppnt.p # op.point DO prev := tmppnt; tmppnt := tmppnt.next END;
					IF prev # NIL THEN prev.next := tmppnt.next
					ELSE plist.p.base(StdPointBase).dependingPoints := tmppnt.next
					END;
					plist := plist.next
				END
			END
		END;
		msg.l := op.point.X(); msg.t := op.point.Y(); msg.r := op.point.X(); msg.b := op.point.Y();
		msg.point := op.point; msg.figure := NIL; msg.insert := op.insert;
		op.insert := ~op.insert;
		Models.Broadcast(m.model, msg)
	END Do;	

	PROCEDURE NewPointOp (model: StdModelBase; point: Point; id: INTEGER; insert: BOOLEAN): PointOp;
		VAR op: PointOp;
	BEGIN
		NEW(op); op.point := point; op.model := model; op.insert := insert; op.pos := id;
		RETURN op
	END NewPointOp;

	(* FigureOp *)

	PROCEDURE (op: FigureOp) Do;
		VAR m: StdModelBase;
			fig, flist, prev, tmpfig, tmplist: StdFigureList;
			plist: StdPointList;
			msg: InsertMsg;
	BEGIN
		m := op.model; GiveFigureID(m.figures);
		NEW(fig); fig.f := op.figure;
		IF op.insert THEN
			flist := m.figures;
			IF flist = NIL THEN fig.next := NIL; m.figures := fig
			ELSE
				IF op.pos = 1 THEN fig.next := m.figures; m.figures := fig
				ELSE
					WHILE flist.id < op.pos - 1 DO flist := flist.next END; 
					fig.next := flist.next; flist.next := fig
				END
			END;
			plist := fig.f.base(StdFigureBase).anchors;
			WHILE plist # NIL DO
				NEW(tmpfig); tmpfig.f := fig.f; tmpfig.next := plist.p.base(StdPointBase).dependingFigures;
				plist.p.base(StdPointBase).dependingFigures := tmpfig; plist := plist.next
			END
		ELSE
			fig := NIL; prev := NIL; flist := m.figures;
			WHILE flist.id # op.pos DO prev := flist; flist := flist.next END;
			IF prev # NIL THEN prev.next := flist.next ELSE m.figures := flist.next END;
			plist := op.figure.base(StdFigureBase).anchors;
			WHILE plist # NIL DO
				tmpfig := plist.p.base(StdPointBase).dependingFigures; prev := NIL;
				WHILE tmpfig.f # op.figure DO prev := tmpfig; tmpfig := tmpfig.next END;
				IF prev # NIL THEN prev.next := tmpfig.next
				ELSE plist.p.base(StdPointBase).dependingFigures := tmpfig.next
				END;
				plist := plist.next
			END
		END;
		NEW(tmplist); tmplist.f := op.figure; tmplist.next := NIL;
		m.model.GetBoundingBox(NIL, tmplist, msg.l, msg.t, msg.r, msg.b);
		msg.insert := op.insert; msg.figure := op.figure; msg.point := NIL;
		op.insert := ~op.insert;
		Models.Broadcast(m.model, msg)
	END Do;	

	PROCEDURE NewFigureOp (m: StdModelBase; f: Figure; id: INTEGER; insert: BOOLEAN): FigureOp;
		VAR op: FigureOp;
	BEGIN
		NEW(op); op.model := m; op.figure := f; op.pos := id; op.insert := insert;
		RETURN op
	END NewFigureOp; 

	(* FigureAnchorOp *)
	
	PROCEDURE (op: FigureAnchorOp) Do;
		VAR m: StdModelBase;
			msg: AnchorMsg;
			tmppnt, plist, prevpnt: StdPointList;
			tmpfig, flist, prevfig, tmplist: StdFigureList;
			l, t, r, b, ll, tt, rr, bb: INTEGER;
	BEGIN
		m := op.model;
		NEW(tmplist); tmplist.f := op.fig; tmplist.next := NIL;
		m.model.GetBoundingBox(NIL, tmplist, l, t, r, b);
		IF op.add THEN
			NEW(tmppnt); tmppnt.p := op.anchor;
			GivePointID(op.fig.base(StdFigureBase).anchors);
			plist := op.fig.base(StdFigureBase).anchors;
			IF plist = NIL THEN tmppnt.next := NIL; op.fig.base(StdFigureBase).anchors := tmppnt
			ELSE
				IF op.anchorID = 1 THEN
					tmppnt.next := op.fig.base(StdFigureBase).anchors;
					op.fig.base(StdFigureBase).anchors := tmppnt
				ELSE
					WHILE plist.id < op.anchorID -1 DO plist := plist.next END;
					tmppnt.next := plist.next; plist.next := tmppnt
				END
			END;
			NEW(tmpfig); tmpfig.f := op.fig;
			GiveFigureID(op.anchor.base(StdPointBase).dependingFigures);
			flist := op.anchor.base(StdPointBase).dependingFigures;
			IF flist = NIL THEN tmpfig.next := NIL; op.anchor.base(StdPointBase).dependingFigures := tmpfig
			ELSE
				IF op.depID = 1 THEN
					tmpfig.next := op.anchor.base(StdPointBase).dependingFigures;
					op.anchor.base(StdPointBase).dependingFigures := tmpfig
				ELSE
					WHILE flist.id < op.depID -1 DO flist := flist.next END;
					tmpfig.next := flist.next; flist.next := tmpfig
				END
			END
		ELSE
			GivePointID(op.fig.base(StdFigureBase).anchors);
			plist := op.fig.base(StdFigureBase).anchors; prevpnt := NIL;
			WHILE plist.id # op.anchorID DO prevpnt := plist; plist := plist.next END;
			IF prevpnt # NIL THEN prevpnt.next := plist.next
			ELSE op.fig.base(StdFigureBase).anchors := plist.next
			END;
			GiveFigureID(op.anchor.base(StdPointBase).dependingFigures);
			flist := op.anchor.base(StdPointBase).dependingFigures; prevfig := NIL;
			WHILE flist.id # op.depID DO prevfig := flist; flist := flist.next END;
			IF prevfig # NIL THEN prevfig.next := flist.next
			ELSE op.anchor.base(StdPointBase).dependingFigures := flist.next
			END
		END;
		m.model.GetBoundingBox(NIL, tmplist, ll, tt, rr, bb);
		msg.l := MIN(l, ll); msg.t := MIN(t, tt); msg.r := MAX(r, rr); msg.b := MAX(b, bb);
		msg.add := op.add; msg.anchor := op.anchor; msg.figure := op.fig;
		msg.comppnt := NIL; msg.pos := op.anchorID;
		op.add := ~op.add;
		Models.Broadcast(m.model, msg)
	END Do;

	PROCEDURE NewFigureAnchorOp (m: StdModelBase; anchor: Point; f: Figure; 
												anchorID, depID: INTEGER; add: BOOLEAN): FigureAnchorOp;
		VAR op: FigureAnchorOp;
			plist: StdPointList;
			flist: StdFigureList;
	BEGIN
		NEW(op); op.add := add; op.model := m; op.anchor := anchor; op.fig := f;
		IF anchorID = last THEN
			GivePointID(f.base(StdFigureBase).anchors);
			plist := f.base(StdFigureBase).anchors;
			IF plist = NIL THEN op.anchorID := 1
			ELSE
				WHILE plist.next # NIL DO plist := plist.next END;
				op.anchorID := plist.id + 1
			END
		ELSE op.anchorID := anchorID
		END;
		IF depID = last THEN
			GiveFigureID(anchor.base(StdPointBase).dependingFigures);
			flist := anchor.base(StdPointBase).dependingFigures;
			IF flist = NIL THEN op.depID := 1
			ELSE
				WHILE flist.next # NIL DO flist := flist.next END;
				op.depID := flist.id + 1
			END
		ELSE op.depID := depID
		END;
		RETURN op
	END NewFigureAnchorOp;

	(* PointAnchorOp *)
	
	PROCEDURE (op: PointAnchorOp) Do;
		VAR m: StdModelBase; msg: AnchorMsg; tmppnt, plist, prevpnt, tmplist: StdPointList;
			l, t, r, b, ll, tt, rr, bb: INTEGER;
	BEGIN
		m := op.model;
		NEW(tmplist); tmplist.p := op.deppnt; tmplist.next := NIL;
		m.model.GetBoundingBox(tmplist, NIL, l, t, r, b);
		IF op.add THEN
			NEW(tmppnt); tmppnt.p := op.anchor;
			GivePointID(op.deppnt.base(StdPointBase).anchors);
			plist := op.deppnt.base(StdPointBase).anchors;
			IF plist = NIL THEN tmppnt.next := NIL; op.deppnt.base(StdPointBase).anchors := tmppnt
			ELSE
				IF op.anchorID = 1 THEN
					tmppnt.next := op.deppnt.base(StdPointBase).anchors;
					op.deppnt.base(StdPointBase).anchors := tmppnt
				ELSE
					WHILE plist.id < op.anchorID -1 DO plist := plist.next END;
					tmppnt.next := plist.next; plist.next := tmppnt
				END
			END;
			NEW(tmppnt); tmppnt.p := op.deppnt;
			GivePointID(op.anchor.base(StdPointBase).dependingPoints);
			plist := op.anchor.base(StdPointBase).dependingPoints;
			IF plist = NIL THEN tmppnt.next := NIL; op.anchor.base(StdPointBase).dependingPoints := tmppnt
			ELSE
				IF op.depID = 1 THEN
					tmppnt.next := op.anchor.base(StdPointBase).dependingPoints;
					op.anchor.base(StdPointBase).dependingPoints := tmppnt
				ELSE
					WHILE plist.id < op.depID -1 DO plist := plist.next END;
					tmppnt.next := plist.next; plist.next := tmppnt
				END
			END
		ELSE
			GivePointID(op.deppnt.base(StdPointBase).anchors);
			plist := op.deppnt.base(StdPointBase).anchors; prevpnt := NIL;
			WHILE plist.id # op.anchorID DO prevpnt := plist; plist := plist.next END;
			IF prevpnt # NIL THEN prevpnt.next := plist.next
			ELSE op.deppnt.base(StdPointBase).anchors := plist.next
			END;			
			GivePointID(op.anchor.base(StdPointBase).dependingPoints);
			plist := op.anchor.base(StdPointBase).dependingPoints; prevpnt := NIL;
			WHILE plist.id # op.depID DO prevpnt := plist; plist := plist.next END;
			IF prevpnt # NIL THEN prevpnt.next := plist.next
			ELSE op.anchor.base(StdPointBase).dependingPoints := plist.next
			END
		END;
		m.model.GetBoundingBox(tmplist, NIL, ll, tt, rr, bb);
		msg.l := MIN(l, ll); msg.t := MIN(t, tt); msg.r := MAX(r, rr); msg.b := MAX(b, bb);
		msg.add := op.add; msg.anchor := op.anchor; msg.comppnt := op.deppnt;
		msg.figure := NIL; msg.pos := op.anchorID;
		op.add := ~op.add;
		Models.Broadcast(m.model, msg)
	END Do;

	PROCEDURE NewPointAnchorOp (m: StdModelBase; anchor: Point; deppnt: ConstrainedPoint;
														anchorID, depID: INTEGER; add: BOOLEAN): PointAnchorOp;
		VAR op: PointAnchorOp;
			plist: StdPointList;
			flist: StdFigureList;
	BEGIN
		NEW(op); op.add := add; op.model := m; op.anchor := anchor; op.deppnt := deppnt;
		IF anchorID = last THEN
			GivePointID(deppnt.base(StdPointBase).anchors);
			plist := deppnt.base(StdPointBase).anchors;
			IF plist = NIL THEN op.anchorID := 1
			ELSE
				WHILE plist.next # NIL DO plist := plist.next END;
				op.anchorID := plist.id + 1
			END
		ELSE op.anchorID := anchorID
		END;
		IF depID = last THEN
			GiveFigureID(anchor.base(StdPointBase).dependingFigures);
			flist := anchor.base(StdPointBase).dependingFigures;
			IF flist = NIL THEN op.depID := 1
			ELSE
				WHILE flist.next # NIL DO flist := flist.next END;
				op.depID := flist.id + 1
			END
		ELSE op.depID := depID
		END;
		RETURN op
	END NewPointAnchorOp;

	(* MoveOp *)

	PROCEDURE (op: MoveOp) Do; 
		VAR tmp: INTEGER; list: MoveList; msg: UpdateMsg;
			l, t, r, b, ll, tt, rr, bb: INTEGER;
	BEGIN
		op.m.model.GetBoundingBox(op.m.points, op.m.figures, l, t, r, b);
		IF op.first THEN op.first := FALSE
		ELSE
			list := op.list;
			WHILE list # NIL DO
				tmp := list.x; list.x := list.p.base(StdPointBase).x; list.p.base(StdPointBase).x := tmp;
				tmp := list.y; list.y := list.p.base(StdPointBase).y; list.p.base(StdPointBase).y := tmp;
				list := list.next
			END
		END;
		op.m.model.GetBoundingBox(op.m.points, op.m.figures, ll, tt, rr, bb);
		msg.l := MIN(l, ll); msg.t := MIN(t, tt); msg.r := MAX(r, rr); msg.b := MAX(b, bb);
		Models.Broadcast(op.m.model, msg)
	END Do;

	(* SwapOp *)
	
	PROCEDURE (op: SwapOp) Do;
		VAR msg: UpdateMsg; p, prev, tmp, oplist: StdFigureList;
	BEGIN
		IF op.do THEN
			oplist := op.flist; prev := NIL; p := op.model.figures;
			WHILE oplist # NIL DO
				WHILE p.f # oplist.f DO prev := p; p := p.next END;
				IF prev = NIL THEN op.model.figures := p.next ELSE prev.next := p.next END;
				p := p.next; oplist := oplist.next
			END;
			oplist := op.flist; p := NIL;
			WHILE oplist # NIL DO
				NEW(tmp); tmp.f := oplist.f; tmp.next := NIL;
				IF p = NIL THEN p := tmp; prev := tmp ELSE prev.next := tmp; prev := tmp END;
				oplist := oplist.next
			END;
			IF op.front THEN
				prev := op.model.figures; 
				IF prev # NIL THEN
					WHILE  prev.next # NIL DO prev := prev.next END;
					prev.next := p
				ELSE op.model.figures := p
				END
			ELSE tmp.next := op.model.figures; op.model.figures := p
			END
		ELSE
			IF op.front THEN
				oplist := op.flist; p := op.model.figures; prev := NIL;
				WHILE oplist.f # p.f DO prev := p; p := p.next END;
				IF prev = NIL THEN op.model.figures := NIL ELSE prev.next := NIL END
			ELSE
				oplist := op.flist; prev := NIL; p := op.model.figures;
				WHILE oplist # NIL DO prev := oplist; oplist := oplist.next END;
				WHILE p.f # prev.f DO p := p.next END;
				op.model.figures := p.next		
			END;
			oplist := op.flist;
			WHILE oplist # NIL DO
				GiveFigureID(op.model.figures); NEW(tmp); tmp.f := oplist.f; p := op.model.figures;
				IF p = NIL THEN tmp.next := NIL; op.model.figures := tmp
				ELSE
					IF oplist.id = 1 THEN tmp.next := op.model.figures; op.model.figures := tmp
					ELSE
						WHILE p.id < oplist.id - 1 DO p := p.next END; 
						tmp.next := p.next; p.next := tmp
					END
				END;
				oplist := oplist.next		
			END
		END;
		op.model.model.GetBoundingBox(NIL, op.flist, msg.l, msg.t, msg.r, msg.b);
		op.do := ~op.do;
		Models.Broadcast(op.model.model, msg)
	END Do;
	
	PROCEDURE NewSwapOp (m: StdModelBase; flist: StdFigureList; do, front: BOOLEAN): SwapOp;
		VAR op: SwapOp;
	BEGIN
		NEW(op); op.model := m; op.flist := flist; op.do := do; op.front := front;
		RETURN op
	END NewSwapOp;	

	(* LinkOp *)

	PROCEDURE (op: LinkOp) Do;
		VAR msg: ChangeAnchorMsg; tmppnt: Point;
			anchors, pntlist, tmppntlist, tmppntlist2: StdPointList;
			finished: BOOLEAN;
			figlist, tmpfiglist, tmpfiglist2: StdFigureList;
			l, t, r, b, ll, tt, rr, bb: INTEGER;
	BEGIN
		op.model.model.GetBoundingBox(op.model.points, op.model.figures, l, t, r, b);
		IF op.figure # NIL THEN
			anchors := op.figure.base(StdFigureBase).anchors; finished := FALSE;
			WHILE (anchors # NIL) & (~finished) DO
				IF anchors = op.sourceList THEN
					anchors.p := op.target;
					NEW(figlist); figlist.f := op.figure;
					figlist.next := op.target.base(StdPointBase).dependingFigures;
					op.target.base(StdPointBase).dependingFigures := figlist;
					tmpfiglist := op.source.base(StdPointBase).dependingFigures; tmpfiglist2 := NIL;
					WHILE tmpfiglist.f # op.figure DO tmpfiglist2 := tmpfiglist; tmpfiglist := tmpfiglist.next END;
					IF tmpfiglist2 = NIL THEN op.source.base(StdPointBase).dependingFigures := tmpfiglist.next
					ELSE tmpfiglist2.next := tmpfiglist.next
					END;
					finished := TRUE
				ELSE anchors := anchors.next
				END
			END
		ELSE
			anchors := op.point.base(StdPointBase).anchors; finished := FALSE;
			WHILE (anchors # NIL) & (~finished) DO
				IF anchors = op.sourceList THEN
					anchors.p := op.target;
					NEW(pntlist); pntlist.p := op.point;
					pntlist.next := op.target.base(StdPointBase).dependingPoints;
					op.target.base(StdPointBase).dependingPoints := pntlist;
					tmppntlist := op.source.base(StdPointBase).dependingPoints; tmppntlist2 := NIL;
					WHILE tmppntlist.p(ConstrainedPoint) # op.point DO
						tmppntlist2 := tmppntlist; tmppntlist := tmppntlist.next
					END;
					IF tmppntlist2 = NIL THEN op.source.base(StdPointBase).dependingPoints := tmppntlist.next
					ELSE tmppntlist2.next := tmppntlist.next
					END;
					op.point(ConstrainedPoint).Calculate(
						op.point.base(StdPointBase).x, op.point.base(StdPointBase).y);
					finished := TRUE
				ELSE anchors := anchors.next
				END
			END
		END;
		op.model.model.GetBoundingBox(op.model.points, op.model.figures, ll, tt, rr, bb);
		msg.l := MIN(l, ll); msg.t := MIN(t, tt); msg.r := MAX(r, rr); msg.b := MAX(b, bb);
		msg.newPoint := op.target; msg.oldPoint := op.source;
		msg.figure := op.figure; msg.compPoint := op.point;
		msg.pos := op.sourceList.id;
		tmppnt := op.target; op.target := op.source; op.source := tmppnt;
		Models.Broadcast(op.model.model, msg)
	END Do;

	PROCEDURE NewLinkOp (m: StdModelBase; target, source: Point;
												sourceList: StdPointList; f: Figure; p: ConstrainedPoint): LinkOp;
		VAR op: LinkOp;
	BEGIN
		NEW(op); op.model := m; op.target := target; op.source := source;
		op.sourceList := sourceList; op.figure := f;op.point := p;
		RETURN op
	END NewLinkOp;

	(* StdFigureList *)
	
	PROCEDURE (f: StdFigureList) This (): Figure;
	BEGIN
		RETURN f.f
	END This;
	
	PROCEDURE (f: StdFigureList) Next (): FigureList;
	BEGIN
		RETURN f.next
	END Next;

	(* StdPointList *)
	
	PROCEDURE (p: StdPointList) This (): Point;
	BEGIN
		RETURN p.p
	END This;
	
	PROCEDURE (p: StdPointList) Next (): PointList;
	BEGIN
		RETURN p.next
	END Next;

	(* StdPointBase *)
	
	PROCEDURE (p: StdPointBase) Model (): Model;
	BEGIN
		RETURN p.m
	END Model;
		
	PROCEDURE (p: StdPointBase) X (): INTEGER;
	BEGIN
		RETURN p.x
	END X;
	
	PROCEDURE (p: StdPointBase) Y (): INTEGER;
	BEGIN
		RETURN p.y
	END Y;
	
	PROCEDURE (p: StdPointBase) Anchors (): PointList;
	BEGIN
		RETURN p.anchors
	END Anchors;
	
	PROCEDURE (p: StdPointBase) DependingPoints (): PointList;
	BEGIN
		RETURN p.dependingPoints
	END DependingPoints;
	
	PROCEDURE (p: StdPointBase) DependingFigures (): FigureList;
	BEGIN
		RETURN p.dependingFigures
	END DependingFigures;

	PROCEDURE (p: StdPointBase) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentPBVersion);
		wr.WriteInt(p.x);
		wr.WriteInt(p.y);
		wr.WriteStore(p.m);
		WritePointList(wr, NIL, p.dependingPoints);
		WriteFigureList(wr, NIL, p.dependingFigures);
		WritePointList(wr, NIL, p.anchors)
	END Externalize;

	PROCEDURE (p: StdPointBase) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
			s: Stores.Store;
	BEGIN
		rd.ReadVersion(minPBVersion, maxPBVersion, version); 
		IF ~rd.cancelled THEN
			rd.ReadInt(p.x);
			rd.ReadInt(p.y);
			rd.ReadStore(s);
			IF s IS Model THEN p.m := s(Model) END;
			ReadPointList(rd, NIL, p.dependingPoints);
			ReadFigureList(rd, NIL, p.dependingFigures);
			ReadPointList(rd, NIL, p.anchors)
		END
	END Internalize;

	PROCEDURE (p: StdPointBase) CopyFrom (source: Stores.Store);
	BEGIN
		WITH source: StdPointBase DO
			p.x := source.x; p.y := source.y
		END
	END CopyFrom;
	
	(* StdFigureBase *)
	
	PROCEDURE (f: StdFigureBase) Model (): Model;
	BEGIN
		RETURN f.m
	END Model;
	
	PROCEDURE (f: StdFigureBase) Anchors (): PointList;
	BEGIN
		RETURN f.anchors
	END Anchors;

	PROCEDURE (f: StdFigureBase) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(0);
		wr.WriteStore(f.m);
		WritePointList(wr, NIL, f.anchors)
	END Externalize;

	PROCEDURE (f: StdFigureBase) Internalize (VAR rd: Stores.Reader);
		VAR version: INTEGER;
			s: Stores.Store;
	BEGIN
		rd.ReadVersion(0, 0, version); 
		IF ~rd.cancelled THEN
			rd.ReadStore(s);
			IF s IS Model THEN f.m := s(Model) END;
			ReadPointList(rd, NIL, f.anchors)
		END
	END Internalize;

	PROCEDURE (f: StdFigureBase) CopyFrom (source: Stores.Store);
	BEGIN
		WITH source: StdFigureBase DO
		ELSE
		END
	END CopyFrom;

	
	(* StdModelBase *)

	PROCEDURE (m: StdModelBase) InitFrom (source: ModelBase);
	BEGIN
		m.points := NIL; m.figures := NIL
	END InitFrom;

	PROCEDURE (m: StdModelBase) InsertCopyFrom (source: ModelBase;
		plist: PointList; flist: FigureList;
		OUT newPoints: PointList;
		OUT newFigures: FigureList
	), NEW;
		VAR stores: ARRAY 5000 OF Stores.Store; index: INTEGER;
			tmppnts, anchors, oldAnchors: PointList; tmpfigs: FigureList;
			lastinsertedpnt, newAnchors, prevpnt, tmpstdpnts, newPlist, allPoints,stdpntlist, tmpplist: StdPointList;
			prevfig, tmpstdfigs, allFigures, stdfiglist, tmpflist: StdFigureList;
			point: Point; figure: Figure;
			belongsTo: BOOLEAN; base: ModelBase;

		PROCEDURE PointInList (pnt: Point; plist: PointList): BOOLEAN;
		BEGIN
			WHILE (plist # NIL) & (plist.This() # pnt) DO plist := plist.Next() END;
			RETURN plist # NIL
		END PointInList;

		PROCEDURE SetPointID (pnt: Point; plist: StdPointList; id: INTEGER);
			VAR tmp: StdPointList;
		BEGIN
			tmp := plist; WHILE (tmp # NIL) & (tmp.p # pnt) DO tmp := tmp.next END;
			IF tmp # NIL THEN tmp.id := id END
		END SetPointID;

		PROCEDURE SetFigureID (fig: Figure; flist: StdFigureList; id: INTEGER);
			VAR tmp: StdFigureList;
		BEGIN
			tmp := flist; WHILE (tmp # NIL) & (tmp.f # fig) DO tmp := tmp.next END;
			IF tmp # NIL THEN tmp.id := id END
		END SetFigureID;

		PROCEDURE FindPointID (pnt: Point; plist: StdPointList): INTEGER;
			VAR tmp: StdPointList;
		BEGIN
			tmp := plist; WHILE (tmp # NIL) & (tmp.p # pnt) DO tmp := tmp.next END;
			RETURN tmp.id
		END FindPointID;

		PROCEDURE PointAnchorsAreInList (cpnt: ConstrainedPoint): BOOLEAN;
			VAR plist: PointList;
		BEGIN
			plist := cpnt.Anchors();
			WHILE (plist # NIL) & (plist.This().base(StdPointBase).id # 0) DO plist := plist.Next() END;
			RETURN plist = NIL
		END PointAnchorsAreInList;

		PROCEDURE PointAnchorsAreInList2 (cpnt: ConstrainedPoint): BOOLEAN;
			VAR plist: PointList;
		BEGIN
			plist := cpnt.Anchors();
			WHILE (plist # NIL) & (FindPointID(plist.This(), allPoints) # 0) DO plist := plist.Next() END;
			RETURN plist = NIL
		END PointAnchorsAreInList2;

		PROCEDURE ClonePoint (pnt: Point): Point;
			VAR pnt2, anchors, lastinserted, tmppnts, p: StdPointList;
				comppnt: ConstrainedPoint;
				baspnt: FreePoint;
				point: Point;
		BEGIN
			IF pnt.base(StdPointBase).id = 1 THEN
				IF (pnt IS ConstrainedPoint) & PointAnchorsAreInList(pnt(ConstrainedPoint)) THEN
					pnt.base(StdPointBase).id := index; INC(index);
					comppnt := Stores.CopyOf(pnt)(ConstrainedPoint);
					tmppnts := pnt(ConstrainedPoint).base(StdPointBase).anchors; anchors := NIL;
					WHILE tmppnts # NIL DO
						point := ClonePoint(tmppnts.This());
						NEW(pnt2); pnt2.p := point; pnt2.next := NIL;
						IF anchors = NIL THEN anchors := pnt2; lastinserted := pnt2
						ELSE lastinserted.next := pnt2; lastinserted := pnt2
						END;
						tmppnts := tmppnts.next
					END;
					m.model.InsertObject(comppnt, anchors, pnt.X(), pnt.Y());
					comppnt.base(StdPointBase).id := pnt.base(StdPointBase).id;
					stores[comppnt.base(StdPointBase).id] := comppnt;
					NEW(p); p.p := comppnt; p.next := NIL;
					IF newPoints = NIL THEN
						newPoints := p; prevpnt := p ELSE prevpnt.next := p; prevpnt := p
					END;
					RETURN comppnt
				ELSE
					pnt.base(StdPointBase).id := index; INC(index);
					baspnt := m.model.NewFreePoint(pnt.X(), pnt.Y());
					baspnt.base(StdPointBase).id := pnt.base(StdPointBase).id;
					stores[baspnt.base(StdPointBase).id] := baspnt;
					NEW(p); p.p := baspnt; p.next := NIL;
					IF newPoints = NIL THEN
						newPoints := p; prevpnt := p ELSE prevpnt.next := p; prevpnt := p
					END;
					RETURN baspnt
				END
			ELSE RETURN stores[pnt.base(StdPointBase).id](Point)
			END
		END ClonePoint;

		PROCEDURE ClonePoint2 (pnt: Point): Point;
			VAR pnt2, anchors, lastinserted, tmppnts: StdPointList;
				points: PointList;
				comppnt: ConstrainedPoint;
				baspnt: FreePoint;
				point: Point;
				id: INTEGER;
		BEGIN
			id := FindPointID(pnt, allPoints);
			IF id = 1 THEN
				IF (pnt IS ConstrainedPoint) & PointAnchorsAreInList2(pnt(ConstrainedPoint)) THEN
					SetPointID(pnt, allPoints, index); INC(index);
					comppnt := Stores.CopyOf(pnt)(ConstrainedPoint);
					points := pnt(ConstrainedPoint).Anchors(); anchors := NIL;
					WHILE points # NIL DO
						point := ClonePoint2(points.This());
						NEW(pnt2); pnt2.p := point; pnt2.next := NIL;
						IF anchors = NIL THEN anchors := pnt2; lastinserted := pnt2
						ELSE lastinserted.next := pnt2; lastinserted := pnt2
						END;
						points := points.Next()
					END;
					m.model.InsertObject(comppnt, anchors, pnt.X(), pnt.Y());
					stores[FindPointID(pnt, allPoints)] := comppnt;
					NEW(tmppnts); tmppnts.p := comppnt; tmppnts.next := NIL;
					IF newPoints = NIL THEN newPoints := tmppnts; prevpnt := tmppnts
					ELSE prevpnt.next := tmppnts; prevpnt := tmppnts
					END;
					RETURN comppnt
				ELSE
					SetPointID(pnt, allPoints, index); INC(index);
					baspnt := m.model.NewFreePoint(pnt.X(), pnt.Y());
					stores[FindPointID(pnt, allPoints)] := baspnt;
					NEW(tmppnts); tmppnts.p := baspnt; tmppnts.next := NIL;
					IF newPoints = NIL THEN newPoints := tmppnts; prevpnt := tmppnts
					ELSE prevpnt.next := tmppnts; prevpnt := tmppnts
					END;
					RETURN baspnt
				END
			ELSE RETURN stores[FindPointID(pnt, allPoints)](Point)
			END
		END ClonePoint2;
				
	BEGIN
		belongsTo := TRUE; tmppnts := plist;
		WHILE (tmppnts # NIL) & belongsTo DO
			belongsTo := tmppnts.This().Model().base = source; tmppnts := tmppnts.Next()
		END;
		ASSERT(belongsTo, 20);
		belongsTo := TRUE; tmpfigs := flist;
		WHILE (tmpfigs # NIL) & belongsTo DO
			belongsTo := tmpfigs.This().Model().base = source; tmpfigs := tmpfigs.Next()
		END;
		ASSERT(belongsTo, 21);
		
		newPoints := NIL; newFigures := NIL; prevfig := NIL; prevpnt := NIL; index := 2;
		base := source;
		
		WITH base: StdModelBase DO
			
				(* Initialize the id of all points and figures in source to 0. *)
			tmpstdpnts := base.points;
			WHILE tmpstdpnts # NIL DO tmpstdpnts.p.base(StdPointBase).id := 0; tmpstdpnts := tmpstdpnts.next END;
			tmpstdfigs := base.figures;
			WHILE tmpstdfigs # NIL DO
				tmpstdfigs.f.base(StdFigureBase).id := 0; tmpstdfigs := tmpstdfigs.next
			END;
			
			(* Include  anchorpoints for figures in flist, if they're not in plist. *)
			IF plist # NIL THEN newPlist := plist(StdPointList) ELSE newPlist := NIL END;
			IF flist # NIL THEN tmpstdfigs := flist(StdFigureList) ELSE tmpstdfigs := NIL END;
			WHILE tmpstdfigs # NIL DO
				anchors := tmpstdfigs.This().Anchors();
				WHILE anchors # NIL DO
					IF ~PointInList(anchors.This(), newPlist) THEN
						NEW(stdpntlist);
						stdpntlist.p := anchors.This(); stdpntlist.next := newPlist; newPlist := stdpntlist
					END;
					anchors := anchors.Next()
				END;
				tmpstdfigs :=tmpstdfigs.next
			END;
			
				(* Set the id of all points and figures to be copied to 1. *)
			IF flist # NIL THEN tmpstdfigs := flist(StdFigureList) ELSE tmpstdfigs := NIL END;
			WHILE tmpstdfigs # NIL DO
				tmpstdfigs.f.base(StdFigureBase).id := 1; tmpstdfigs := tmpstdfigs.next
			END;
			tmpstdpnts := newPlist;
			WHILE tmpstdpnts # NIL DO
				tmpstdpnts.p.base(StdPointBase).id := 1; tmpstdpnts := tmpstdpnts.next
			END;
					
			(* Copy the points in newPList. *)
			tmpstdpnts := newPlist;
			WHILE tmpstdpnts # NIL DO point := ClonePoint(tmpstdpnts.p); tmpstdpnts := tmpstdpnts.next END;
				
			(* Copy the figures in flist. *)			
			tmpstdfigs := base.figures;
			WHILE tmpstdfigs # NIL DO
				IF (tmpstdfigs.f.base(StdFigureBase).id = 1) THEN
					figure := Stores.CopyOf(tmpstdfigs.f)(Figure);
					newAnchors := NIL; oldAnchors := tmpstdfigs.f.base(StdFigureBase).anchors;
					
					(* Copy their anchorpoints. *)
					WHILE oldAnchors # NIL DO
						point := ClonePoint(oldAnchors.This());
						NEW(stdpntlist); stdpntlist.p := point; stdpntlist.next := NIL;
						IF newAnchors = NIL THEN newAnchors := stdpntlist; lastinsertedpnt := stdpntlist
						ELSE lastinsertedpnt.next := stdpntlist; lastinsertedpnt := stdpntlist
						END;
						oldAnchors := oldAnchors.Next()
					END;
					
					m.model.InsertObject(figure, newAnchors, undefined, undefined);
					figure.base(StdFigureBase).id := 0;
					NEW(stdfiglist); stdfiglist.f := figure; stdfiglist.next := NIL;
					IF newFigures = NIL THEN newFigures := stdfiglist; prevfig := stdfiglist
					ELSE prevfig.next := stdfiglist; prevfig := stdfiglist
					END
				END;
				tmpstdfigs :=  tmpstdfigs.next
			END
			
		| base: ModelBase DO

			(* Copy all points in source to a StdPointList. *)
			tmppnts := source.model.Points(); allPoints := NIL;
			WHILE tmppnts # NIL DO
				NEW(stdpntlist); stdpntlist.id := 0; stdpntlist.p := tmppnts.This(); stdpntlist.next := NIL;
				IF allPoints = NIL THEN allPoints := stdpntlist; tmpplist := stdpntlist
				ELSE tmpplist.next := stdpntlist; tmpplist := tmpplist.next
				END;
				tmppnts := tmppnts.Next()
			END;

				(* Copy all figures in source to a StdFigureList. *)
			tmpfigs := source.model.Figures(); allFigures := NIL;
			WHILE tmpfigs # NIL DO
				NEW(stdfiglist); stdfiglist.id := 0; stdfiglist.f := tmpfigs.This(); stdfiglist.next := NIL;
				IF allFigures = NIL THEN allFigures := stdfiglist; tmpflist := stdfiglist
				ELSE tmpflist.next := stdfiglist; tmpflist := tmpflist.next
				END;
				tmpfigs := tmpfigs.Next()
			END;

				(* Include  anchorpoints for figures in flist, if they're not in plist. *)
			IF plist # NIL THEN newPlist := plist(StdPointList) ELSE newPlist := NIL END;
			tmpfigs := flist;
			WHILE tmpfigs # NIL DO
				anchors := tmpfigs.This().Anchors();
				WHILE anchors # NIL DO
					IF ~PointInList(anchors.This(), newPlist) THEN
						NEW(stdpntlist);
						stdpntlist.p := anchors.This(); stdpntlist.next := newPlist; newPlist := stdpntlist
					END;
					anchors := anchors.Next()
				END;
				tmpfigs :=tmpfigs.Next()
			END;
			
				(* Set the id of all points and figures to be copied to 1. *)
			tmpfigs := flist;
			WHILE tmpfigs # NIL DO SetFigureID(tmpfigs.This(), allFigures, 1); tmpfigs := tmpfigs.Next() END;
			tmppnts := newPlist;
			WHILE tmppnts # NIL DO SetPointID(tmppnts.This(), allPoints, 1); tmppnts := tmppnts.Next() END;
			
				(* Copy the points in plist. *)
			tmppnts := newPlist;
			WHILE tmppnts # NIL DO point := ClonePoint2(tmppnts.This()); tmppnts := tmppnts.Next() END;
			
				(* Copy the figures in flist. *)
			tmpstdfigs := allFigures;
			WHILE tmpstdfigs # NIL DO
				IF (tmpstdfigs.id = 1) THEN
					figure := Stores.CopyOf(tmpstdfigs.f)(Figure);
					newAnchors := NIL; oldAnchors := tmpstdfigs.f.Anchors();
					
						(* Copy their anchorpoints. *)
					WHILE oldAnchors # NIL DO
						point := ClonePoint2(oldAnchors.This());
						NEW(stdpntlist); stdpntlist.p := point; stdpntlist.next := NIL;
						IF newAnchors = NIL THEN newAnchors := stdpntlist; lastinsertedpnt := stdpntlist
						ELSE lastinsertedpnt.next := stdpntlist; lastinsertedpnt := stdpntlist
						END;
						oldAnchors := oldAnchors.Next()
					END;
					
					m.model.InsertObject(figure, newAnchors, undefined, undefined);
					tmpstdfigs.id := 0;
					NEW(stdfiglist); stdfiglist.f := figure; stdfiglist.next := NIL;
					IF newFigures = NIL THEN newFigures := stdfiglist; prevfig := stdfiglist
					ELSE prevfig.next := stdfiglist; prevfig := stdfiglist
					END
				END;
				tmpstdfigs := tmpstdfigs.next
			END

		ELSE
		END
	END InsertCopyFrom;

	PROCEDURE (m: StdModelBase) InsertCopy (source: ModelBase;
																			plist: PointList; flist: FigureList;
																			OUT newPoints: PointList;
																			OUT newFigures: FigureList);
	BEGIN
		m.InsertCopyFrom(source, plist, flist, newPoints, newFigures)
	END InsertCopy;

	PROCEDURE (m: StdModelBase) CopyDataFrom (source: Stores.Store);
		VAR tmpfigs: FigureList;
			tmppnts: PointList;
	BEGIN
		WITH source: StdModelBase DO
			m.InsertCopyFrom(source, source.points, source.figures, tmppnts, tmpfigs)
		| source: ModelBase DO
			m.InsertCopyFrom(source, source.model.Points(), source.model.Figures(), tmppnts, tmpfigs)		
		ELSE
		END
	END CopyDataFrom;

	PROCEDURE (m: StdModelBase) WriteData (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteVersion(currentStdVersion);		
		WritePointList(wr, m, m.points);
		WriteFigureList(wr, m, m.figures)
	END WriteData;

	PROCEDURE (m: StdModelBase) ReadData (VAR rd: Stores.Reader);
		VAR version: INTEGER;
	BEGIN
		rd.ReadVersion(minStdVersion, maxStdVersion, version);
		IF ~rd.cancelled THEN
			(* alienFigures := NIL; alienPoints := NIL; *)
			ReadPointList(rd, m, m.points);
			ReadFigureList(rd, m, m.figures)
		END
	END ReadData;

	PROCEDURE (m: StdModelBase) Points (): PointList;
	BEGIN
		RETURN m.points
	END Points;

	PROCEDURE (m: StdModelBase) Figures (): FigureList;
	BEGIN
		RETURN m.figures
	END Figures;

	PROCEDURE (m: StdModelBase) InsertObject (
		o: Object; anchors: PointList; x, y: INTEGER
	);
		VAR plist, tmplist, newlist, prev: StdPointList; flist: StdFigureList; i: INTEGER;
	BEGIN
		newlist := NIL; prev := NIL;
		WHILE anchors # NIL DO
			NEW(tmplist); tmplist.p := anchors.This(); tmplist.next := NIL;
			IF newlist = NIL THEN
				newlist := tmplist; prev := tmplist
			ELSE
				prev.next := tmplist; prev := tmplist
			END;
			anchors := anchors.Next()
		END;
		IF o IS Figure THEN
			o(Figure).base(StdFigureBase).m := m.model;
			GiveFigureID(m.figures); flist := m.figures;
			IF flist = NIL THEN i := 1
			ELSE
				WHILE flist.next # NIL DO flist := flist.next END;
				i := flist.id + 1
			END;
			o(Figure).base(StdFigureBase).anchors := newlist;
			Models.Do(m.model, "Insert Figure", NewFigureOp(m, o(Figure), i, TRUE))
		ELSIF o IS Point THEN
			o(Point).base(StdPointBase).m := m.model;
			o(Point).base(StdPointBase).x := x; o(Point).base(StdPointBase).y := y;
			GivePointID(m.points); plist := m.points;
			IF plist = NIL THEN
				i := 1
			ELSE
				WHILE plist.next # NIL DO plist := plist.next END;
				i := plist.id + 1
			END;
			o(Point).base(StdPointBase).anchors := newlist;
			Models.Do(m.model, "Insert Point", NewPointOp(m, o(Point), i, TRUE));
			IF o IS ConstrainedPoint THEN
				o(ConstrainedPoint).Calculate(
					o(Point).base(StdPointBase).x, o(Point).base(StdPointBase).y
				)
			END
		END
	END InsertObject;

	PROCEDURE (m: StdModelBase) RemoveObject (o: Object);
		VAR plist: StdPointList; flist: StdFigureList;
	BEGIN
		IF o IS Figure THEN
			flist := m.figures; GiveFigureID(flist); WHILE flist.f # o(Figure) DO flist := flist.next END;
			Models.Do(m.model, "Remove Figure", NewFigureOp(m, o(Figure), flist.id, FALSE))	
		ELSIF o IS Point THEN
			IF (o(Point).DependingPoints() = NIL) & (o(Point).DependingFigures() = NIL) THEN
				plist := m.points; GivePointID(plist); WHILE plist.p # o(Point) DO plist := plist.next END;
				Models.Do(m.model, "Remove Point", NewPointOp(m, o(Point), plist.id, FALSE))	
			END
		END			
	END RemoveObject;
	
	PROCEDURE (m: StdModelBase) MovePoints (plist: PointList; dx, dy: INTEGER);
		VAR l, t, r, b: INTEGER;
			tmplist: PointList;
			deptbm, last: StdPointList;
			op: MoveOp;
			lastmove: MoveList;
	
		PROCEDURE IsInList (p: Point; plist: PointList): BOOLEAN;
		BEGIN
			WHILE (plist # NIL) & (plist.This() # p) DO plist := plist.Next() END;
			RETURN plist # NIL
		END IsInList;
	
		PROCEDURE Handle (p: Point);
			VAR anchors, deppnts: PointList;
				pnts: StdPointList;
				movelist: MoveList;
				isinlist: BOOLEAN;
		BEGIN
			IF p.base(StdPointBase).id # ok THEN
				IF p IS ConstrainedPoint THEN 
					anchors := p(ConstrainedPoint).Anchors();
					WHILE anchors # NIL DO Handle(anchors.This()); anchors := anchors.Next() END
				END;
				IF p.base(StdPointBase).id = toBeMoved THEN
					p.base(StdPointBase).id := ok;
					IF ~(p IS AlienPoint) & ~AlienDependencies(p) THEN
						NEW(movelist); movelist.next := NIL; movelist.p := p;
						IF op.list = NIL THEN op.list := movelist ELSE lastmove.next := movelist END;
						lastmove := movelist; isinlist := IsInList(p, plist);
						IF (p IS ConstrainedPoint) & isinlist THEN
							movelist.x := p.X(); movelist.y := p.Y();
							p.base(StdPointBase).x := p.X() + dx; p.base(StdPointBase).y := p.Y() + dy;
							p(ConstrainedPoint).Calculate(p.base(StdPointBase).x, p.base(StdPointBase).y)
						ELSIF (p IS ConstrainedPoint) & ~isinlist THEN
							movelist.x := p.X(); movelist.y := p.Y();
							p(ConstrainedPoint).Calculate(p.base(StdPointBase).x, p.base(StdPointBase).y)
						ELSE
							movelist.x := p.X(); movelist.y := p.Y();
							p.base(StdPointBase).x := p.X() + dx; p.base(StdPointBase).y := p.Y() + dy
						END;
						deppnts := p.DependingPoints();
						WHILE deppnts # NIL DO
							IF deppnts.This().base(StdPointBase).id # toBeMoved THEN
								NEW(pnts); pnts.p := deppnts.This(); pnts.next := NIL;
								IF deptbm = NIL THEN deptbm := pnts ELSE last.next := pnts END;
								last := pnts; pnts.p.base(StdPointBase).id := toBeMoved
							END;
							deppnts := deppnts.Next()
						END
					END
				ELSE p.base(StdPointBase).id := ok
				END
			END
		END Handle;
	
	BEGIN
		IF plist # NIL THEN
			ASSERT(plist.This().Model() = m.model, 20);
			m.model.GetBoundingBox(plist, NIL, l, t, r, b);
			IF l + dx < 0 THEN dx := - l END;
			IF t + dy < 0 THEN dy := - t END; 
			deptbm := NIL; last := NIL; lastmove := NIL; NEW(op); op.m := m; op.first := TRUE;
			tmplist := m.model.Points();
			WHILE tmplist # NIL DO tmplist.This().base(StdPointBase).id := 0; tmplist := tmplist.Next() END;
			tmplist := plist;
			WHILE tmplist # NIL DO
				tmplist.This().base(StdPointBase).id := toBeMoved;
				tmplist := tmplist.Next()
			END;
			tmplist := plist; WHILE tmplist # NIL DO Handle(tmplist.This()); tmplist := tmplist.Next() END;
			tmplist := deptbm; WHILE tmplist # NIL DO Handle(tmplist.This()); tmplist := tmplist.Next() END;
			IF op.list # NIL THEN Models.Do(m.model, " Move", op) END
		END
	END MovePoints;

	PROCEDURE (m: StdModelBase) MovePoint (p: Point; dx, dy: INTEGER);
		VAR plist: StdPointList;
	BEGIN
		ASSERT(p # NIL, 20);
		NEW(plist); plist.p := p; plist.next := NIL;
		m.model.MovePoints(plist, dx, dy)
	END MovePoint;

	PROCEDURE (m: StdModelBase) MoveToGrid (plist: PointList; grid: INTEGER);
		VAR x0, y0: INTEGER;
			s: Stores.Operation;
	BEGIN
		ASSERT(grid > 0, 20);
		Models.BeginScript(m.model, "Move To Grid", s);
		WHILE plist # NIL DO
			IF ~(plist.This() IS ConstrainedPoint) THEN
				x0 := plist.This().X() + grid DIV 2; x0 := x0 - x0 MOD grid;
				y0 := plist.This().Y() + grid DIV 2; y0 := y0 - y0 MOD grid;
				m.model.MovePoint(plist.This(), x0 - plist.This().X(), y0 - plist.This().Y())
			END;
			plist := plist.Next()
		END;
		Models.EndScript(m.model, s)
	END MoveToGrid;

	PROCEDURE (m: StdModelBase) BringToFront (flist: FigureList);
		VAR figlist, oplist, node, prev: StdFigureList;
			tmplist: FigureList;
			i: INTEGER;
	BEGIN
		i := 1; oplist := NIL; prev := NIL; figlist := m.figures;
		WHILE figlist # NIL DO
			figlist.f.base(StdFigureBase).id := 0; figlist.id := i; INC(i); figlist := figlist.next
		END;
		tmplist := flist;
		WHILE tmplist # NIL DO
			tmplist.This().base(StdFigureBase).id := 1; tmplist := tmplist.Next()
		END;
		figlist := m.figures;
		WHILE figlist # NIL DO
			IF figlist.f.base(StdFigureBase).id = 1 THEN
				NEW(node); IF oplist = NIL THEN oplist := node ELSE prev.next  := node END;
				prev := node; node.f := figlist.f; node.id := figlist.id; node.next := NIL
			END;	
			figlist := figlist.next
		END;
		Models.Do(m.model, "Bring To Front", NewSwapOp(m, oplist, TRUE, TRUE))
	END BringToFront;

	PROCEDURE (m: StdModelBase) SendToBack (flist: FigureList);
		VAR figlist, oplist, node, prev: StdFigureList;
			tmplist: FigureList;
			i: INTEGER;
	BEGIN
		i := 1; oplist := NIL; prev := NIL; figlist := m.figures;
		WHILE figlist # NIL DO
			figlist.f.base(StdFigureBase).id := 0; figlist.id := i; INC(i); figlist := figlist.next
		END;
		tmplist := flist;
		WHILE tmplist # NIL DO tmplist.This().base(StdFigureBase).id := 1; tmplist := tmplist.Next() END;
		figlist := m.figures;
		WHILE figlist # NIL DO
			IF figlist.f.base(StdFigureBase).id = 1 THEN
				NEW(node); IF oplist = NIL THEN oplist := node ELSE prev.next  := node END;
				prev := node; node.f := figlist.f; node.id := figlist.id; node.next := NIL
			END;	
			figlist := figlist.next
		END;
		Models.Do(m.model, "Send To Back", NewSwapOp(m, oplist, TRUE, FALSE))
	END SendToBack;

	PROCEDURE (m: StdModelBase) ChangeAnchor (o: Object; p: Point; pos: INTEGER);
		VAR anchors: StdPointList;
	BEGIN
		IF o IS ConstrainedPoint THEN anchors := o(ConstrainedPoint).base(StdPointBase).anchors
		ELSIF o IS Figure THEN anchors := o(Figure).base(StdFigureBase).anchors
		END;
		GivePointID(anchors); WHILE (anchors # NIL) & (anchors.id # pos) DO anchors := anchors.next END;
		IF anchors # NIL THEN
			IF o IS Figure THEN
				Models.Do(m.model, "Change of Anchorpoint", NewLinkOp(m, p, anchors.p, anchors, o(Figure), NIL))
			ELSIF o IS ConstrainedPoint THEN
				Models.Do(m.model, "Change of Anchorpoint",
									NewLinkOp(m, p, anchors.p, anchors, NIL, o(ConstrainedPoint)))	
			END
		END		
	END ChangeAnchor;

	PROCEDURE (m: StdModelBase) AddAnchor (o: Object; pos: INTEGER; p: Point);
	BEGIN
		WITH o : ConstrainedPoint DO
			Models.Do(m.model, "Add Anchorpoint", NewPointAnchorOp(m, p, o, pos + 1, last, TRUE))
		| o : Figure DO 
			Models.Do(m.model, "Add Anchorpoint", NewFigureAnchorOp(m, p, o, pos + 1, last, TRUE))
		ELSE
		END		
	END AddAnchor;
		
	PROCEDURE (m: StdModelBase) RemoveAnchor (o: Object; p: Point);
		VAR plist1, plist2: StdPointList;
			flist: StdFigureList;
			tmpaID, tmpdID: INTEGER;
	BEGIN
		tmpaID := 0; tmpdID := 0;
		IF o IS Figure THEN
			plist1 := o(Figure).base(StdFigureBase).anchors; flist := p.base(StdPointBase).dependingFigures;
			GivePointID(plist1); GiveFigureID(flist);
			WHILE (flist # NIL) & (flist.f # o(Figure)) DO flist := flist.next END;
			IF flist # NIL THEN tmpdID := flist.id END
		ELSIF o IS ConstrainedPoint THEN
			plist1 := o(ConstrainedPoint).base(StdPointBase).anchors; plist2 := p.base(StdPointBase).dependingPoints;
			GivePointID(plist1); GivePointID(plist2);
			WHILE (plist2 # NIL) & (plist2.p # o(ConstrainedPoint)) DO plist2 := plist2.next END;
			IF plist2 # NIL THEN tmpdID := plist2.id END
		END;
		WHILE (plist1 # NIL) & (plist1.p # p) DO plist1 := plist1.next END;
		IF plist1 # NIL THEN tmpaID := plist1.id END;
		IF (tmpaID # 0) & (tmpdID # 0) THEN
			IF o IS ConstrainedPoint THEN
				Models.Do(m.model, "Remove Anchorpoint",
									NewPointAnchorOp(m, p, o(ConstrainedPoint), tmpaID, tmpdID, FALSE))
			ELSIF o IS Figure THEN
				Models.Do(m.model, "Remove Anchorpoint",
									NewFigureAnchorOp(m, p, o(Figure), tmpaID, tmpdID, FALSE))
			END
		END		
	END RemoveAnchor;
	
	PROCEDURE (m: StdModelBase) NewPointBase (): PointBase;
		VAR base: StdPointBase;
	BEGIN
		NEW(base); base.m := NIL; base.x := 0; base.y := 0; base.id := 0;
		base.anchors := NIL; base.dependingPoints := NIL; base.dependingFigures := NIL;
		RETURN base
	END NewPointBase;

	PROCEDURE (m: StdModelBase) NewFigureBase (): FigureBase;
		VAR base: StdFigureBase;
	BEGIN
		NEW(base); base.m := NIL; base.anchors := NIL; base.id := 0;
		RETURN base
	END NewFigureBase;
	
	PROCEDURE (m: StdModelBase) ThisPointList (pnts: ARRAY OF Point): PointList;
		VAR n, i : INTEGER;
			plist, tmp: StdPointList;
			allBelongs: BOOLEAN;
	BEGIN
		n := LEN(pnts); i := 0; allBelongs := TRUE; plist := NIL;
		WHILE i < n DO
			IF pnts[i].Model() # m.model THEN allBelongs := FALSE; i := n ELSE INC(i) END
		END;
		IF allBelongs THEN
			i := n - 1; WHILE i >= 0 DO NEW(tmp); tmp.p := pnts[i]; tmp.next := plist; plist := tmp; DEC(i) END
		END;
		RETURN plist
	END ThisPointList;
	
	PROCEDURE (m: StdModelBase) ThisFigureList (figs: ARRAY OF Figure): FigureList;
		VAR n, i : INTEGER; flist, tmp: StdFigureList; allBelongs: BOOLEAN;
	BEGIN
		n := LEN(figs); i := 0; allBelongs := TRUE; flist := NIL;
		WHILE i < n DO
			IF figs[i].Model() # m.model THEN allBelongs := FALSE; i := n ELSE INC(i) END
		END;
		IF allBelongs THEN
			i := n - 1; WHILE i >= 0 DO NEW(tmp); tmp.f := figs[i]; tmp.next := flist; flist := tmp; DEC(i) END
		END;
		RETURN flist
	END ThisFigureList;
	
	(* StdDirectory *)

	PROCEDURE (d: StdDirectory) NewModelBase (): ModelBase;
		VAR m: StdModelBase;
	BEGIN
		NEW(m); m.points := NIL; m.figures := NIL;
		RETURN m
	END NewModelBase;
	
	PROCEDURE Init*;
		VAR d: StdDirectory;
	BEGIN
		NEW(d); SetDir(d)
	END Init;

BEGIN
	Init
END FigModels. 
