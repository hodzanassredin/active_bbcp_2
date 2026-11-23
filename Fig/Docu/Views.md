FigViews

DEFINITION FigViews;

	IMPORT Views, FigModels;

	CONST 
		initialGrid = 108000;
		initialGridDrawingFactor = 3;
		points = 1; figures = 2; both = 3;

	TYPE
		View = POINTER TO ABSTRACT RECORD (Views.View)
			(v: View) ThisModel (): FigModels.Model, EXTENSIBLE;
			(v: View) GetGrid (OUT grid, gridDrawingFactor: INTEGER), NEW, ABSTRACT;
			(v: View) SetGrid (grid, gridDrawingFactor: INTEGER), NEW, ABSTRACT;
			(v: View) GridOn (): BOOLEAN, NEW, ABSTRACT;
			(v: View) ShowingGrid (): BOOLEAN, NEW, ABSTRACT;
			(v: View) ShowingPoints (): BOOLEAN, NEW, ABSTRACT;
			(v: View) ToggleGrid, NEW, ABSTRACT;
			(v: View) ToggleGridhiding, NEW, ABSTRACT;
			(v: View) TogglePointhiding, NEW, ABSTRACT;
			(v: View) RoundToGrid (VAR x, y: INTEGER), NEW, ABSTRACT;
			(v: View) SelectAll, NEW, ABSTRACT;
			(v: View) DeselectAll, NEW, ABSTRACT;
			(v: View) SelectObject (o: FigModels.Object), NEW, ABSTRACT;
			(v: View) DeselectObject (o: FigModels.Object), NEW, ABSTRACT;
			(v: View) SelectedPoints (): FigModels.PointList, NEW, ABSTRACT;
			(v: View) SelectedFigures (): FigModels.FigureList, NEW, ABSTRACT;
			(v: View) SelectPoints (plist: FigModels.PointList), NEW, ABSTRACT;
			(v: View) SelectFigures (flist: FigModels.FigureList), NEW, ABSTRACT;
			(v: View) PopPoints (n: INTEGER; VAR plist: FigModels.PointList), NEW, ABSTRACT;
			(v: View) PopFigures (n: INTEGER; VAR flist: FigModels.FigureList), NEW, ABSTRACT;
			(v: View) GetSelections (n, type: INTEGER; OUT sel: ARRAY OF FigModels.Object),
													NEW, ABSTRACT;
			(v: View) IsSelected (o: FigModels.Object), NEW, ABSTRACT;
			(v: View) LatestSelectedObject (type: INTEGER): FigModels.Object, NEW, ABSTRACT;
			(v: View) NofSelectedObjects (type: INTEGER): INTEGER, NEW, ABSTRACT
		END;
		
		Directory = POINTER TO ABSTRACT RECORD 
			(d: Directory) New (m: FigModels.Model): View, NEW, ABSTRACT
		END;
		
	VAR 
		dir-: Directory;
		stdDir-: Directory;	
		
	PROCEDURE Focus (): View;
	PROCEDURE SetDir (d: Directory);

END FigViews.


FigView.View is the standard view for models as defined in FigModels.


CONST initialGrid = 108000;
The grid size for new views, in universal units (108000 = 3 * Ports.mm).

CONST initialGridDrawingFactor = 3;
The grid drawing factor for new views.

CONST points = 1;
Used in v.GetSelections, v.LatestSelectedObject and v.NofSelectedObjects as a value for the type parameter. Only points are considered.

CONST figures = 2;
Used in v.GetSelections, v.LatestSelectedObject and v.NofSelectedObjects as a value for the type parameter. Only figures are considered.

CONST both = 3;
Used in v.GetSelections, v.LatestSelectedObject and v.NofSelectedObjects as a value for the type parameter. Both points and figures are considered.


TYPE View
ABSTRACT
View for FigModels.Models.

PROCEDURE (v: View) ThisModel (): FigModels.Model;
EXTENSIBLE
Returns the view's model. Returns NIL by default if not extended.

PROCEDURE (v: View) GetGrid (OUT grid, gridDrawingFactor: INTEGER)
NEW, ABSTRACT
Returns the grid and gridDrawingFactor of the view. grid is in universal units.

PROCEDURE (v: View) SetGrid (grid, gridDrawingFactor: INTEGER)
NEW, ABSTRACT
Sets the view's grid and gridDrawingFactor. grid is in universal units.

Pre
grid > 0	20
gridDrawingFactor > 0	21

PROCEDURE (v: View) GridOn (): BOOLEAN
NEW, ABSTRACT
Returns TRUE is the grid is enabled.

PROCEDURE (v: View) ShowingGrid (): BOOLEAN
NEW, ABSTRACT
Returns TRUE is the grid is shown.

PROCEDURE (v: View) ShowingPoints (): BOOLEAN
NEW, ABSTRACT
Returns TRUE if non-selected points are shown.

PROCEDURE (v: View) ToggleGrid
NEW, ABSTRACT
Toggles the grid on and off.

PROCEDURE (v: View) ToggleGridhiding
NEW, ABSTRACT
Toggles the grid showing on and off.

PROCEDURE (v: View) TogglePointhiding
NEW, ABSTRACT
Toggle the point showing on and off.

PROCEDURE (v: View) RoundToGrid (VAR x, y: INTEGER)
NEW, ABSTRACT
Rounds x and y to nearest "grid point".

PROCEDURE (v: View) SelectAll
NEW, ABSTRACT
Selects all points and figures from the model.

PROCEDURE (v: View) DeselectAll
NEW, ABSTRACT
Deselects all points and figures.

PROCEDURE (v: View) SelectObject (o: FigModels.Object)
NEW, ABSTRACT
Selects the object o.

Pre
o # NIL	20
o IS FigModels.Point
	o(FigModels.Point).Model() = v.ThisModel()	21
o IS FigModels.Figure
	o(FigModels.Figure).Model() = v.ThisModel()	22

PROCEDURE (v: View) DeselectObject (o: FigModels.Object)
NEW, ABSTRACT
Deselects the object o.

Pre
o # NIL	20
o IS FigModels.Point
	o(FigModels.Point).Model() = v.ThisModel()	21
o IS FigModels.Figure
	o(FigModels.Figure).Model() = v.ThisModel()	22

PROCEDURE (v: View) SelectedPoints (): FigModels.PointList
NEW, ABSTRACT
Returns a list of all selected points.

PROCEDURE (v: View) SelectedFigures (): FigModels.FigureList
NEW, ABSTRACT
Returns a list of all selected figures.

PROCEDURE (v: View) SelectPoints (plist: FigModels.PointList)
NEW, ABSTRACT
Selects all points in plist.

PROCEDURE (v: View) SelectFigures (flist: FigModels.FigureList)
NEW, ABSTRACT
Selects all figures in flist.

PROCEDURE (v: View) PopPoints (n: INTEGER; OUT plist: FigModels.PointList)
NEW, ABSTRACT
plist contains a list of the n last selected points. The points returned in plist are deselected.

PRE
n > 0	20
n <= v.NofSelectedObjects(points)	21

PROCEDURE (v: View) PopFigures (n: INTEGER; OUT flist: FigModels.FigureList)
NEW, ABSTRACT
flist contains a list of the n last selected figures. The figures returned in flist are deselected.

PRE
n > 0	20
n <= v.NofSelectedObjects(figures)	21

PROCEDURE (v: View) GetSelections (n, type: INTEGER; OUT sel: ARRAY OF FigModels.Object)
NEW, ABSTRACT
Returns in sel the n latest selected objects of type.

PRE
n > 0	20
LEN(sel) >= n	21

PROCEDURE (v: View) IsSelected (o: FigModels.Object)
NEW, ABSTRACT
Returns TRUE if o is selected, otherwise FALSE.

Pre
o # NIL	20
o IS FigModels.Point
	o(FigModels.Point).Model() = v.ThisModel()	21
o IS FigModels.Figure
	o(FigModels.Figure).Model() = v.ThisModel()	22

PROCEDURE (v: View) LatestSelectedObject (type: INTEGER): FigModels.Object
NEW, ABSTRACT
Returns the latest selected object of type.

PROCEDURE (v: View) NofSelectedObjects (type: INTEGER): INTEGER
NEW, ABSTRACT
Returns the amount of selected objects of type.


TYPE Directory
ABSTRACT
Directory for views.

PROCEDURE (d: Directory) New (m: FigModels.Model): View
NEW, ABSTRACT
Returns a new view with m as its model.

Pre
m # NIL	20

VAR dir-: Directory
Holds directory object for views.

VAR stdDir-: Directory
Holds standard implementation of directory object for views.


PROCEDURE Focus (): View
Returns the View with the focus, or NIL if there is none.

PROCEDURE SetDir (d: Directory)
Sets the variable dir to d.

Pre
d # NIL	20
