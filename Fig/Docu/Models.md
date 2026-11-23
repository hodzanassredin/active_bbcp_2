FigModels

DEFINITION FigModels; 

	IMPORT Stores, Models, Properties, Ports;

	CONST
		expandable = 0; cyclic = 1; minAnchors = 2; maxAnchors = 3;
		thickness = 0;
		undefined = -1;
		noArrow = 1; beginArrow = 2; endArrow = 3; doubleArrow = 4;
		type = 0;

	
	TYPE 
		Model = POINTER TO LIMITED RECORD (Models.Model)
			base-: ModelBase;
			(m: Model) Points (): PointList, NEW;
			(m: Model) Figures (): FigureList, NEW;
			(m: Model) InsertCopy (source: Model; plist: PointList; flist: FigureList, OUT newPoints: PointList; 
												OUT newFigures: FigureList), NEW;
			(m: Model) InsertObject (o: Object; anchors: PointList; x, y: INTEGER), NEW;
			(m: Model) NewFreePoint (x, y: INTEGER): FreePoint, NEW;
			(m: Model) RemoveObject (o: Object), NEW;
			(m: Model) AddAnchor (o: Object; pos: INTEGER; OUT pnt: Point), NEW;
			(m: Model) RemoveAnchor (o: Object; p: Point), NEW;
			(m: Model) JoinPossible (plist: PointList; target: Point), NEW;
			(m: Model) JoinPoints (plist: PointList; target: Point): BOOLEAN, NEW;
			(m: Model) SplitPoint (p: Point), NEW;			
			(m: Model) MovePoints (plist: PointList; dx, dy: INTEGER), NEW;
			(m: Model) MovePoint (p: Point; dx, dy: INTEGER), NEW;
			(m: Model) MoveToGrid (plist: PointList; grid: INTEGER), NEW
			(m: Model) BringToFront (flist: FigureList), NEW;
			(m: Model) SendToBack (flist: FigureList), NEW;
			(m: Model) CollectProperties (flist: FigureList; plist: PointList; OUT prop: Properties.Property), NEW;
			(m: Model) EmitProperties (flist: FigureList; plist: PointList; prop: Properties.Property), NEW;
			(m: Model) GetBoundingBox (plist: PointList; flist: FigureList; OUT l, t, r, b: INTEGER), NEW;
			(m: Model) IncludedFigures (flist: FigureList; l, t, r, b: INTEGER): FigureList, NEW;
			(m: Model) IntersectingFigures (flist: FigureList; l, t, r, b: INTEGER): FigureList, NEW;
			(m: Model) TheseFigures (flist: FigureList; x, y: INTEGER): FigureList, NEW;
			(m: Model) ThesePoints (plist: PointList; l, t, r, b: INTEGER): PointLisr, NEW;
			(m: Model) ChooseFigures (flist: FigureList; c: FigureChooser): FigureList, NEW;
			(m: Model) ChoosePoints (plist: PointList; c: PointChooser): PointList, NEW;
			(m: Model) ThisFigureList (figs: ARRAY OF Figure): FigureList, NEW;
			(m: Model) ThisPointList (pnts: ARRAY OF Point): PointList, NEW
		END;

		ModelBase = POINTER TO ABSTRACT RECORD (Stores.Store)
			model-: Model;
			(m: ModelBase) InitFrom- (source: ModelBase), NEW, ABSTRACT;
			(m: ModelBase) InitModel- (model: Model), NEW, ABSTRACT;
			(m: ModelBase) NewPointBase- (): PointBase, NEW, ABSTRACT;
			(m: ModelBase) NewFigureBase- (): FigureBase, NEW, ABSTRACT;
			(m: ModelBase) Points- (): PointList, NEW, ABSTRACT;
			(m: ModelBase) Figures- (): FigureList, NEW, ABSTRACT;
			(m: ModelBase) InsertCopy- (source: ModelBase; plist: PointList; flist: FigureList,
														OUT newPoints: PointList; OUT newFigures: FigureList),
														NEW, ABSTRACT;
			(m: ModelBase) InsertObject- (o: Object; anchors: PointList; x, y: INTEGER), NEW, ABSTRACT;
			(m: ModelBase) RemoveObject- (o: Object), NEW, ABSTRACT;
			(m: ModelBase) AddAnchor- (o: Object; pos: INTEGER; p: Point), NEW, ABSTRACT;
			(m: ModelBase) RemoveAnchor- (o: Object; p: Point), NEW, ABSTRACT;
			(m: ModelBase) ChangeAnchor- (o: Object; p: Point; pos: INTEGER), NEW, ABSTRACT;
			(m: ModelBase) MovePoints- (points: PointList; dx, dy: INTEGER), NEW, ABSTRACT;
			(m: ModelBase) MovePoint- (p: Point; dx, dy: INTEGER), NEW, ABSTRACT;
			(m: ModelBase) MoveToGrid- (plist: PointList; grid: INTEGER), NEW, ABSTRACT;
			(m: ModelBase) BringToFront- (flist: FigureList), NEW, ABSTRACT;
			(m: ModelBase) SendToBack- (flist: FigureList), NEW, ABSTRACT;
			(m: ModelBase) ThisFigureList- (figs: ARRAY OF Figure): FigureList, NEW, ABSTRACT;
			(m: ModelBase) ThisPointList- (pnts: ARRAY OF Point): PointList, NEW, ABSTRACT
		END;
			
		Object = POINTER TO ABSTRACT RECORD (Stores.Store) END;
			
		Point = POINTER TO ABSTRACT RECORD (Object)
			base-: PointBase;
			(p: Point) Model (): Model, NEW;
			(p: Point) X (): INTEGER, NEW;
			(p: Point) Y (): INTEGER, NEW;
			(p: Point) DependingFigures (): FigureList, NEW;
			(p: Point) DependingPoints (): PointList, NEW
		END;

		PointBase = POINTER TO ABSTRACT RECORD (Stores.Store)
			ext-: Point;
			(p: PointBase) Model- (): Model, NEW, ABSTRACT;
			(p: PointBase) X- (): INTEGER, NEW, ABSTRACT;
			(p: PointBase) Y- (): INTEGER, NEW, ABSTRACT;
			(p: PointBase) Anchors- (): PointList, NEW, ABSTRACT;
			(p: PointBase) DependingFigures- (): FigureList, NEW, ABSTRACT;
			(p: PointBase) DependingPoints- (): PointList, NEW, ABSTRACT
		END;

		FreePoint = POINTER TO LIMITED RECORD (Point) END;

		ConstrainedPoint = POINTER TO ABSTRACT RECORD (Point)
			(c: ConstrainedPoint) Action (), NEW, EMPTY;
			(c: ConstrainedPoint) Anchors (): PointList, NEW;
			(c: ConstrainedPoint) Calculate (VAR x, y: INTEGER), NEW, EMPTY;
			(c: ConstrainedPoint) PollProp (OUT p: Properties.Property), NEW, EXTENSIBLE;
			(c: ConstrainedPoint) SetProp (p: Properties.Property), NEW, EMPTY
		END;

		Figure = POINTER TO ABSTRACT RECORD (Object)
			base-: FigureBase;
			(fig: Figure) Model (): Model, NEW;
			(fig: Figure) Anchors (): PointList, NEW;
			(fig: Figure) Action, NEW, EMPTY;
			(fig: Figure) GetBoundingBox (OUT l, t, r, b: INTEGER), NEW, EXTENSIBLE;
			(fig: Figure) Draw (f: Ports.Frame; l, t, r, b: INTEGER), NEW, ABSTRACT;
			(fig: Figure) Covers (x, y: INTEGER): BOOLEAN, NEW, EXTENSIBLE;
			(fig: Figure) PollProp (OUT p: Properties.Property), NEW, EXTENSIBLE;
			(fig: Figure) SetProp (p: Properties.Property), NEW, EMPTY
		END;

		FigureBase = POINTER TO ABSTRACT RECORD (Stores.Store)
			ext-: Figure;
			(f: FigureBase) Model- (): Model, NEW, ABSTRACT;
			(f: FigureBase) Anchors- (): PointList, NEW, ABSTRACT
		END;

		AlienFigure = POINTER TO ABSTRACT RECORD (Figure)
			store-: Stores.Alien;
			(fig: AlienFigure) CopyDataFrom- (source: AlienFigure), NEW, EMPTY
		END;

		AlienPoint = POINTER TO ABSTRACT RECORD (ConstrainedPoint)
			store-: Stores.Alien
			(p: AlienPoint) CopyDataFrom- (source: AlienPoint), NEW, EMPTY
		END;

		PointList = POINTER TO ABSTRACT RECORD
			(p: PointList) This (): Point, NEW, ABSTRACT;
			(p: PointList) Next (): PointList, NEW, ABSTRACT
		END;
	
		FigureList = POINTER TO ABSTRACT RECORD
			(f: FigureList) This (): Figure, NEW, ABSTRACT;
			(f: FigureList) Next (): FigureList, NEW, ABSTRACT
		END;
				
		PointChooser = POINTER TO ABSTRACT RECORD
			(c: PointChooser) Choose (p: Point): BOOLEAN, NEW, ABSTRACT
		END;
		
		FigureChooser = POINTER TO ABSTRACT RECORD
			(c: FigureChooser) Choose (f: Figure): BOOLEAN, NEW, ABSTRACT
		END;
		
		Directory = POINTER TO ABSTRACT RECORD
			(d: Directory) New (): Model, NEW;
			(d: Directory) NewModelBase- (): ModelBase, NEW, ABSTRACT
		END;
		
		StdProp = POINTER TO RECORD (Properties.Property)
			minAnchors, maxAnchors: INTEGER;
			expandable, cyclic: BOOLEAN
		END;

		TProp = POINTER TO RECORD (Properties.Property)
			thickness: INTEGER
		END;

		ArrowProp = POINTER TO RECORD (Properties.Property)
			type: INTEGER
		END;

		UpdateMsg = EXTENSIBLE RECORD (Models.UpdateMsg)
			l, t, r, b: INTEGER
		END;
		
		InsertMsg = RECORD (UpdateMsg)
			insert: BOOLEAN;
			figure: Figure;
			point: Point
		END;

		AnchorMsg = RECORD (UpdateMsg)
			add: BOOLEAN;
			figure: Figure;
			comppnt: ConstrainedPoint;
			anchor: Point;
			pos: INTEGER
		END;

		ChangeAnchorMsg = RECORD (UpdateMsg)
			newPoint, oldPoint: Point;
			figure: Figure;
			compPoint: ConstrainedPoint;
			pos: INTEGER
		END;

	VAR
		dir-: Directory;
		stdDir-: Directory;

	PROCEDURE WritePoint (VAR wr: Stores.Writer; p: Point);
	PROCEDURE WriteFigure (VAR wr: Stores.Writer; f: Figure);
	PROCEDURE ReadPoint (VAR rd: Stores.Reader; OUT point: Point);
	PROCEDURE ReadFigure (VAR rd: Stores.Reader; OUT figure: Figure);
	PROCEDURE CloneOf (source: Model): Model;
	PROCEDURE SetDir (d: Directory);

END FigModels.


FigModels are models which contain Points, functioning as anchors to Figures and ConstrainedPoints. 

CONST expandable
Used in StdProp.valid, StdProp.known and StdProp.readOnly. A figure or constrained point with this option set is expandable, which means anchorpoints can be added to and removed from the figure/constrained point.

CONST cyclic
Used in StdProp.valid, StdProp.known and StdProp.readOnly. A figure or constrained point with this option set has a cyclic appearance. Example: a polygon is cyclic.

CONST minAnchors
Used in StdProp.valid, StdProp.known and StdProp.readOnly. A figure or constrained point with this option set has a minimum number of anchorpoints.

CONST maxAnchors
Used in StdProp.valid, StdProp.known and StdProp.readOnly. A figure or constrained point with this option set has a maximum number of anchorpoints.

CONST thickness
Used in TProp.valid, TProp.known and TProp.readOnly.

CONST undefined
Used in different situations:

-	If StdProp.maxAnchorpoints is set to this value, then the figure or constrained point can be expanded 
	 indefinitely with new anchorpoints.
-	In InsertObject: If the object is a Figure, then the x and y parameters have to be set to this value. A 		
	 constrained point can have this value as it's coordinates. Then the points Calculate procedure must set the 
	 coordinates to real values.
-	In Calculate: If a constrained point's coodinates have this value, they have to be initialized to "real" values.

CONST noArrow
Used in ArrowProp.type to indicate that the figure has no arrow heads.

CONST beginArrow
Used in ArrowProp.type to indicate that the figure has one arrow head, pointing at it's first anchorpoint.

CONST endArrow
Used in ArrowProp.type to indicate that the figure has one arrow head, pointing at it's last anchorpoint.
 
CONST doubleArrow
Used in ArrowProp.type to indicate that the figure has two arrow heads, pointing at it's first and last anchorpoint.

CONST type
Used in ArrowProp.valid, ArrowProp.known and ArrowProp.readOnly.


TYPE Model
LIMITED
The model represents the client's interface of a drawing. The model is connected to a ModelBase that can be implemented by a third party. The model is represented graphically by a FigViews.View.

base-: ModelBase
The ModelBase that is connected to the model.

PROCEDURE (m: Model) Points (): PointList
NEW
Returns a list of all the points in the model.

PROCEDURE (m: Model) Figures (): FigureList
NEW
Returns a list of all the figures in the model.

PROCEDURE (m: Model) InsertCopy (source: Model; plist: PointList; flist: FigureList,
															OUT newPoints: PointList; OUT newFigures: FigureList)
NEW
Copies the points and figures in plist and flist into m. plist and flist must belong to source. The points added to m are returned in newPoints, and the figures added to m are returned in newFigures (these points and figures belong to m). Constrained points that have all their anchorpoints in plist will be copied as ConstrainedPoints, otherwise they'll be transformed into FreePoints. Figures in flist will have their anchorpoints copied also, these points don't have to be in plist from the beginning.

Pre
For all points in plist:
	plist.This().Model() = source	20
For all figures in flist:
	flist.This().Model() = source	21

PROCEDURE (m: Model) InsertObject (o: Object; anchors: PointList; x, y: INTEGER)
NEW
Inserts o into m. anchors is the list of anchorpoints, if o is a Figure or a ConstrainedPoint. x and y are the coordinates if o is a ConstrainedPoint.

Pre
f # NIL	20
anchors # NIL	21
anchors.This().Model() = m	22

PROCEDURE (m: Model) NewFreePoint (x, y: INTEGER): FreePoint
NEW
Inserts a FreePoint in m at x, y. The newly inserted point is returned.

PROCEDURE (m: Model) RemoveObject (o: Object)
NEW
Removes o from m.

Pre
f # NIL	20
f.Model() = m	21

PROCEDURE (m: Model) AddAnchor (o: Object; pos: INTEGER; OUT pnt: Point)
NEW
Adds a new FreePoint as an anchorpoint to o. pos indicates the point's place in o's list of anchorpoints. The newly inserted point is returned in pnt.

Pre
p # NIL	20
f # NIL	21
p.Model() = m	22
f.Model() = m	23
pos >= 0	24

PROCEDURE (m: Model) RemoveAnchor (o: Object; p: Point)
NEW
Removes the point p from o's list of anchorpoints. p isn't removed from m.

Pre
p # NIL	20
f # NIL	21
p.Model() = m	22
f.Model() = m	23
	
PROCEDURE (m: Model) JoinPossible (plist: PointList; target: Point): BOOLEAN
NEW
Returns TRUE if the points in plist can be joined into target, otherwise FALSE is returned. The criteria for a join is the following:

- target may not be included in plist.
- No constrained points in plist.
- If target is a constrained point then target may not depend on any of the points in plist.

Pre
m # NIL	20
plist # NIL	21
target # NIL	22
plist.This().Model() = m	23
target.Model() = m	24

PROCEDURE (m: Model) JoinPoints (plist: PointList; target: Point)
NEW
Joins the points in plist into target if JoinPossible(m, plist, target) = TRUE. All figures and points connected to the points in plist will become connected to target instead, and the points in plist will be removed from m.

Pre
m # NIL	20
plist # NIL	21
target # NIL	22
plist.This().Model() = m	23
target.Model() = m	24

PROCEDURE (m: Model) SplitPoint (p: Point)
NEW
Splits p into new FreePoints. One FreePoint for each figure or constrained point connected to p will be created, and then the figures and constrained points will become connected to the new FreePoints instead.
If p is a ConstrainedPoint then it will become deselected, otherwise it will be removed from m.

Pre
m # NIL	20
p # NIL	21
p.Model() = m	22

PROCEDURE (m: Model) MovePoints (plist: PointList; dx, dy: INTEGER)
NEW
Moves all points in plist according the vector dx, dy.

Pre
plist.This().Model() = m	20

PROCEDURE (m: Model) MovePoint (p: Point; dx, dy: INTEGER)
NEW
Moves p according to the vector dx, dy.

Pre
p # NIL	20

PROCEDURE (m: Model) MoveToGrid (plist: PointList; grid: INTEGER)
NEW
Moves the points in plist so they lay on the grid (in universal units).

Pre
grid > 0	20

PROCEDURE (m: Model) BringToFront (flist: FigureList)
NEW
Moves all figures in flist in front of all other figures. The order of the flist figures will be kept as they are in m. This ordering might be different from the ordering in flist.

Pre
flist # NIL	20
flist.This().Model() = m	21

PROCEDURE (m: Model) SendToBack (flist: FigureList)
NEW
Moves all figures in flist behind all other figures. The order of the flist figures will be kept as they are in m. This ordering might be different from the ordering in flist.

Pre
flist # NIL	20
flist.This().Model() = m	21

PROCEDURE (m: Model) CollectProperties (flist: FigureList; plist: PointList; OUT prop: Properties.Property)
NEW
Collects the properties for the figures in flist and for the constrained points in plist. The properties are returned in prop.

PROCEDURE (m: Model) EmitProperties (flist: FigureList; plist: PointList; prop: Properties.Property)
NEW
Emits prop to all figures in flist and to all constrained points in plist.

Pre
m # NIL	20

PROCEDURE (m: Model) GetBoundingBox (plist: PointList; flist: FigureList; OUT l, t, r, b: INTEGER)
NEW
Get the box that contains all points in plist and all figures in flist. The bounding box is returned in l, t, r, b.

Pre
(plist # NIL) OR (flist # NIL)	20

PROCEDURE (m: Model) IncludedFigures (flist: FigureList; l, t, r, b: INTEGER): FigureList
NEW
Returns a list of the figures in flist that are within the box defined by l, t, r, b.

Pre
m # NIL	20

PROCEDURE (m: Model) IntersectingFigures (flist: FigureList; l, t, r, b: INTEGER): FigureList
NEW
Returns a list of the figures in flist which bounding box touches the box defined by l, t, r, b.

Pre
m # NIL	20

PROCEDURE (m: Model) TheseFigures (flist: FigureList; x, y: INTEGER): FigureList
NEW
Returns the figures from flist that covers x, y.

Pre
m # NIL	20

PROCEDURE (m: Model) ThesePoints (plist: PointList; l, t, r, b: INTEGER): PointList
NEW
Returns the points from plist that lays within l, t, r, b.

Pre
m # NIL	20

PROCEDURE (m: Model) ChooseFigures (flist: FigureList; c: FigureChooser): FigureList
NEW
Returns a list of the figures in flist for which c.Choose = TRUE.

Pre
c # NIL	20
flist.This().Model() = m	21

PROCEDURE (m: Model) ChoosePoints (plist: PointList; c: PointChooser): PointList
NEW
Returns a list of the points in plist for which c.Choose = TRUE.

Pre
c # NIL	20
plist.This().Model() = m	21

PROCEDURE (m: Model) ThisFigureList (figs: ARRAY OF Figure): FigureList
NEW
Returns a list containing the figures in figs, in the same order. The figures in figs must all belong to m.

PROCEDURE (m: Model) ThisPointList (pnts: ARRAY OF Point): PointList
NEW
Returns a list containing the points in pnts, in the same order. The points in pnts must all belong to m.


TYPE ModelBase
ABSTRACT


model-: Model
The Model connected to this ModelBase.

PROCEDURE (m: ModelBase) InitFrom- (source: ModelBase)
NEW, ABSTRACT
Initializes m based on source. source can often be ignored.

PROCEDURE (m: ModelBase) InitModel- (model: Model)
NEW, ABSTRACT
Initializes the model of all figures and points to model.

PROCEDURE (m: ModelBase) NewPointBase- (): PointBase
NEW, ABSTRACT
Returns an empty PointBase.

PROCEDURE (m: ModelBase) NewFigureBase- (): FigureBase
NEW, ABSTRACT
Returns an empty FigureBase.

PROCEDURE (m: ModelBase) Points- (): PointList
NEW, ABSTRACT
Returns a list containing all points in m. The first point in the list is the point that was inserted first in m, and the last in the list is the point inserted last.

PROCEDURE (m: ModelBase) Figures- (): FigureList
NEW, ABSTRACT
Returns a list containing all figures in the ModelBase. The first figure in the list is the figure that was inserted first in m, and the last in the list is the figure inserted last.

PROCEDURE (m: ModelBase) InsertCopy- (source: ModelBase; plist: PointList; flist: FigureList,
																	OUT newPoints: PointList; OUT newFigures: FigureList)
NEW, ABSTRACT
Copies the points and figures in plist and flist into m. The points added to m are returned in newPoints, and the figures added to m are returned in newFigures (these points and figures belong to m). Constrained points that have all their anchorpoints in plist will be copied as ConstrainedPoints, otherwise they'll be transformed into FreePoints. Figures in flist will have their anchorpoints copied also, these points don't have to be in plist from the beginning.

PROCEDURE (m: ModelBase) InsertObject- (o: Object; anchors: PointList; x, y: INTEGER)
NEW, ABSTRACT
Inserts o into m. anchors is the list of anchorpoints, if o is a Figure or a ConstrainedPoint. x and y are the coordinates if o is a Point.

PROCEDURE (m: ModelBase) RemoveObject- (o: Object)
NEW, ABSTRACT
Removes o from m.

PROCEDURE (m: ModelBase) AddAnchor- (o: Object; pos: INTEGER; p: Point)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) RemoveAnchor- (o: Object; p: Point)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) ChangeAnchor- (o: Object; p: Point; pos: INTEGER)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) MovePoints- (points: PointList; dx, dy: INTEGER)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) MovePoint- (p: Point; dx, dy: INTEGER)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) MoveToGrid- (plist: PointList; grid: INTEGER)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) BringToFront- (flist: FigureList)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) SendToBack- (flist: FigureList)
NEW, ABSTRACT

PROCEDURE (m: ModelBase) ThisFigureList- (figs: ARRAY OF Figure): FigureList
NEW, ABSTRACT

PROCEDURE (m: ModelBase) ThisPointList- (pnts: ARRAY OF Point): PointList
NEW, ABSTRACT


TYPE Point
ABSTRACT
The base type for FreePoints and ConstrainedPoints. 

base-: PointBase
All points are connected to a PointBase that contains basic information about the point.

PROCEDURE (p: Point) Model (): Model
NEW
Returns the model the point belongs to.

PROCEDURE (p: Point) X (): INTEGER
NEW
Returns the point's x-coordinate.

PROCEDURE (p: Point) Y (): INTEGER
NEW
REturns the point's y-coordinate.

PROCEDURE (p: Point) IsSelected (): BOOLEAN
NEW
Returns TRUE if the point is selected, FALSE if it isn't.

PROCEDURE (p: Point) SelectionTime (): INTEGER
NEW
Returns a timestamp of the point's last selection.

PROCEDURE (p: Point) DependingFigures (): PointList
NEW
Returns a list of all the constrained points that depend on this point.

PROCEDURE (p: Point) DependingPoints (): FigureList
NEW
Returns a list of all the figures that depend on this point.


TYPE PointBase
ABSTRACT
All points are connected to a PointBase containing basic information about the point. The PointBase is extended by the model provider.

ext-: Point
The point connected to this base.

PROCEDURE (p: PointBase) Model- (): Model
NEW, ABSTRACT
Returns the model the point belongs to.

PROCEDURE (p: PointBase) X- (): INTEGER
NEW, ABSTRACT
Returns the point's x-coordinate.

PROCEDURE (p: PointBase) Y- (): INTEGER
NEW, ABSTRACT
Returns the point's y-coordinate.

PROCEDURE (p: PointBase) IsSelected- (): BOOLEAN
NEW, ABSTRACT
Returns TRUE if the point is selected, FALSE if it isn't.

PROCEDURE (p: PointBase) SelectionTime- (): INTEGER
NEW, ABSTRACT
Returns a timestamp of the point's last selection.

PROCEDURE (p: PointBase) Anchors- (): PointList
NEW, ABSTRACT
Returns a list containing the anchorpoints of the point. 

PROCEDURE (p: PointBase) DependingFigures- (): PointList
NEW, ABSTRACT
Returns a list of all the constrained points that depend on this point.

PROCEDURE (p: PointBase) DependingPoints- (): FigureList
NEW, ABSTRACT
Returns a list of all the figures that depend on this point.


TYPE FreePoint
LIMITED
A basic extension of Point. A FreePoint can be used as an anchorpoint to a ConstrainedPoint or a Figure. FreePoints don't have any anchorpoints.


TYPE ConstrainedPoint
ABSTRACT
Functions as a base type for ConstrainedPoint extensions. 

PROCEDURE (c: ConstrainedPoint) Action-
NEW, EMPTY
ConstrainedPoints can be given an action, and the function of this procedure may vary. It may for instance open a dialog for changing the point's attributes.

PROCEDURE (c: ConstrainedPoint) Anchors (): PointList
NEW
Returns a list containing the anchorpoints of c. 

PROCEDURE (c: ConstrainedPoint) Calculate (VAR x, y: INTEGER)
NEW, EMPTY
Calculates a new position for c. x, y is the wanted position, but the final position depends on the points freedom of movement. A call to this procedure doesn't change the position of the point.

PROCEDURE (c: ConstrainedPoint) PollProp- (OUT p: Properties.Property)
NEW, EXTENSIBLE
Returns all properties that c knows about. Returns NIL by default.

PROCEDURE (c: ConstrainedPoint) SetProp- (p: Properties.Property)
NEW, EMPTY
c scans p and sets its attributes according to know properties.


TYPE Figure
ABSTRACT
The base type for all Figure extensions.

base-: FigureBase
All figures are connected to a FigureBase that contains basic information about the figure.

PROCEDURE (fig: Figure) Model (): Model
NEW
Returns the model the figure belongs to.

PROCEDURE (fig: Figure) Anchors (): PointList
NEW
Returns a list containing the anchorpoints of fig. 

PROCEDURE (fig: Figure) IsSelected (): BOOLEAN
NEW
Returns TRUE if the figure is selected, FALSE if it isn't.

PROCEDURE (fig: Figure) SelectionTime ():INTEGER
NEW
Returns a timestamp of the figures's last selection.

PROCEDURE (fig: Figure) Action-
NEW, EMPTY
Figures can be given an action, and the function of this procedure may vary. It may for instance open a dialog for changing the figure's attributes.

PROCEDURE (fig: Figure) GetBoundingBox- (OUT l, t, r, b: INTEGER)
NEW, ABSTRACT
This procedure calculates the bounding box of fig, and returns it in l, t, r, b.

PROCEDURE (fig: Figure) Draw- (f: Ports.Frame; l, t, r, b: INTEGER)
NEW, ABSTRACT
This procedure draws fig to the frame f. l, t, r, b is the clipping area. 

PROCEDURE (fig: Figure) Covers- (x, y: INTEGER): BOOLEAN
NEW, EXTENSIBLE
Returns TRUE if fig covers the coordinates x, y. Default implementation returns TRUE if x, y is within the bounding box of the figure. 

PROCEDURE (fig: Figure) PollProp- (OUT p: Properties.Property)
NEW, EXTENSIBLE
Returns all properties that fig knows about. Returns NIL by default.

PROCEDURE (fig: Figure) SetProp- (p: Properties.Property)
NEW, EMPTY
fig scans p and sets its attributes according to know properties.


TYPE FigureBase
ABSTRACT
All figures are connected to a FigureBase containing basic information about the figure. The FigureBase is extended by the model provider.

ext-: Figure
The figure connected to this base.

PROCEDURE (f: FigureBase) Model- (): Model
NEW, ABSTRACT
The model the figure belongs to.

PROCEDURE (f: FigureBase) IsSelected- (): BOOLEAN
NEW, ABSTRACT
Returns TRUE if the figure is selected, FALSE if it isn't.

PROCEDURE (f: FigureBase) SelectionTime- (): INTEGER
NEW, ABSTRACT
Returns a timestamp of the figure's last selection.

PROCEDURE (f: FigureBase) Anchors- (): PointList
NEW, ABSTRACT
Returns a list containing the anchorpoints of the figure. 


TYPE AlienPoint
ABSTRACT
Unknown ConstrainedPoint extensions are made into an AlienPoint. Extended by the model provider.

store- Stores.Alien
The alien store enclosed by the AlienPoint extension.

TYPE AlienFigure
ABSTRACT
Unknown Figure extensions are made into an AlienFigure. Extended by the model provider.

store- Stores.Alien
The alien store enclosed by the AlienFigure extension.


TYPE PointList
ABSTRACT
A list of points. Extended by the model provider.

PROCEDURE (p: PointList) This (): Point
NEW, ABSTRACT
The point at the current node.

PROCEDURE (p: PointList) Next (): PointList
NEW, ABSTRACT
The next node in the list.


TYPE FigureList
ABSTRACT
A list of figures. Extended by the model provider.

PROCEDURE (f: FigureList) This (): Figure
NEW, ABSTRACT
The figure at the current node.

PROCEDURE (f: FigureList) Next (): FigureList
NEW, ABSTRACT
The next node in the list.


TYPE PointChooser
ABSTRACT
The PointChooser provides a way to find points that satisfies certain criteria. 

PROCEDURE (c: PointChooser) Choose (p: Point): BOOLEAN
NEW, ABSTRACT
Returns TRUE if p satisfies some criteria defined by c, otherwise FALSE is returned.

Pre
p # NIL	20


TYPE FigureChooser
ABSTRACT
The FigureChooser provides a way to find figures that satisfies certain criteria. 

PROCEDURE (c: FigureChooser) Choose (f: Figure): BOOLEAN
NEW, ABSTRACT
Returns TRUE if f satisfies some criteria defined by c, otherwise FALSE is returned.

Pre
f # NIL	20


TYPE Directory
ABSTRACT
Directory for models.

PROCEDURE (d: Directory) New (): Model
NEW, ABSTRACT
Returns a new model.


TYPE StdProp
Properties for figures that are needed by the editor.

minAnchorpoints: INTEGER
The minimum amount of anchorpoints the figure need.

maxAnchorpoints: INTEGER
The maximum amount of anchorpoints the figure can have.

expandable: BOOLEAN
TRUE if the figure is expandable (anchorpoints can be added or removed).

cyclic: BOOLEAN
TRUE if the figure is cyclic.


TYPE TProp
Properties for thickness.

thickness: INTEGER
The thickness in unversal units. 0 = hairline, Ports.fill = figure is filled.


TYPE ArrowProp
Properties for arrow-type figures.

type: INTEGER
The type of arrow. Can be set to noArrow, beginArrow, endArrow or doubleArrow.


TYPE UpdateMsg
Message notifying about changes in the model.

l, t, r, b: INTEGER
The area that needs to be restored.


TYPE InsertMsg
Message notifying about point or figure additions or removals.

insert: BOOLEAN
TRUE if a point or figure was added, FALSE if a point or figure was removed.

figure: Figure
The figure added or removed.

point: Point
The point added or removed.


TYPE SelectMsg
Message notifying about point or figure selections/deselections.

select: BOOLEAN
TRUE if a point or figure was selected, FALSE if a point or figure was deselected.

figure: Figure
The figure selected or deselected.

point: Point
The point selected or deselected.

		
TYPE AnchorMsg
Message notifying that an anchorpoint has been added to or removed from a figure.

add: BOOLEAN
TRUE if an anchorpoint was added, FALSE if an anchorpoint was removed.

figure: Figure
The figure involved in the addition or removal.

comppnt: ConstrainedPoint
The constrained point involved in the addition or removal.

anchor: Point
The point added or removed.

index: INTEGER
The point's index in the figure's list of anchorpoints.


TYPE ChangeAnchorMsg
Message notifying that an anchorpoint of a figure or constrained point was changed.

newPoint: Point
The new anchorpoint.

oldPoint: Point
The old anchorpoint.

figure: Figure
The figure involved.

compPoint: ConstrainedPoint
The constrained point involved.

index: INTEGER
The anchorpoint's index in the figure's or constrained point's list of anchorpoints.


VAR dir-: Directory
Holds directory object for models.

VAR stdDir-: Directory
Holds standard implementation of directory object for models.


PROCEDURE WritePoint (VAR wr: Stores.Writer; p: Point);
Writes to wr the point p (including its base object).

Pre
p # NIL	20

PROCEDURE WriteFigure (VAR wr: Stores.Writer; f: Figure);
Writes to wr the figure f (including its base object).

Pre
f # NIL	20

PROCEDURE ReadPoint (VAR rd: Stores.Reader; OUT point: Point);
Reads using rd the point point (including its base object) or an alien, if the required extension is not available.

PROCEDURE ReadFigure (VAR rd: Stores.Reader; OUT figure: Figure);
Reads using rd the figure figure (including its base object) or an alien, if the required extension is not available.

PROCEDURE CloneOf (source: Model): Model;
Allocates a new model of the same type as source.

Pre
source # NIL	20

PROCEDURE SetDir (d: Directory);
Sets the variable dir to d.

Pre
d # NIL	20
