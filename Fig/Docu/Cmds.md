FigCmds

DEFINITION FigCmds;

	IMPORT Dialog;
	
	CONST
		mm = 0; inch = 1; point = 2; fill = 3; hairline = 4;
		
	VAR
		TPropDlg: RECORD 
			thickness, type: INTEGER;
			initiated: BOOLEAN
		END;

		arrowPropDlg: RECORD 
			type: INTEGER;
			initiated: BOOLEAN
		END;

		gridDlg: RECORD 
			grid, gridDrawingFactor, unitType: INTEGER
		END;
		
	PROCEDURE SplitPoints;
	PROCEDURE SplitPointsGuard(VAR par: Dialog.Par);
	PROCEDURE JoinPoints;
	PROCEDURE JoinPointsGuard(VAR par: Dialog.Par);
	PROCEDURE SendToBack;
	PROCEDURE SendToBackGuard(VAR par: Dialog.Par);
	PROCEDURE BringToFront;
	PROCEDURE BringToFrontGuard(VAR par: Dialog.Par);
	PROCEDURE AddAnchor;
	PROCEDURE AddAnchorGuard (VAR par: Dialog.Par);
	PROCEDURE RemoveAnchor;
	PROCEDURE RemoveAnchorGuard (VAR par: Dialog.Par);
	PROCEDURE SelectAnchors;
	PROCEDURE SelectAnchorsGuard (VAR par: Dialog.Par);
	PROCEDURE TogglePointhiding;
	PROCEDURE TogglePointhidingGuard (VAR par: Dialog.Par);
	PROCEDURE ToggleGridhiding;
	PROCEDURE ToggleGridhidingGuard (VAR par: Dialog.Par);
	PROCEDURE ToggleGrid;
	PROCEDURE ToggleGridGuard (VAR par: Dialog.Par);
	PROCEDURE InitGridDialog;
	PROCEDURE SetGrid;
	PROCEDURE SetGridGuard (VAR par: Dialog.Par);
	PROCEDURE GridNotifier (op, from, to: INTEGER);
	PROCEDURE ForceToGrid;
	PROCEDURE ForceToGridGuard (VAR par: Dialog.Par);
	PROCEDURE Deposit;
	PROCEDURE FocusGuard (VAR par: Dialog.Par);
	PROCEDURE SelectionGuard (nofPoints: INTEGER; VAR par: Dialog.Par);
	PROCEDURE ArrowDlgGuard (VAR par: Dialog.Par);
	PROCEDURE ThicknessDlgGuard (VAR par: Dialog.Par);
	PROCEDURE InitTPropDialog;
	PROCEDURE SetTProp;
	PROCEDURE InitArrowPropDialog;
	PROCEDURE SetArrowProp;
	PROCEDURE UpDownGuard (VAR par: Dialog.Par);
	PROCEDURE UpDownNotifier (op, from, to: INTEGER);
	PROCEDURE OpenExtensionDocu;
	PROCEDURE OpenExtensionDocuGuard (VAR par: Dialog.Par);
	
END FigCmds.


FigCmds provides basic commands for Fig.

CONST mm, inch, point
Used  in gridDlg.unitType and TPropDlg.type.

CONST fill, hairline
Used  in TPropDlg.type.


VAR TPropDlg: RECORD 
Used together with the /Fig/Rsrc/Thickns dialog.

thickness: INTEGER
The current "thickness" in universal units.

type: INTEGER
The current type.

initiated: BOOLEAN
TRUE if TPropDlg is initiated to meaningful values, FALSE otherwise.

VAR arrowPropDlg: RECORD 
Used together with the /Fig/Rsrc/Lines dialog.

type: INTEGER
Hold the current type of line: FigModels.noArrow, FigModels.beginArrow, FigModels.endArrow or FigModels.doubleArrow. See the documentation for FigModels for more information about those constants.

initiated: BOOLEAN
TRUE if arrowPropDlg is initiated to meaningful values, FALSE otherwise.


VAR gridDlg: RECORD 
Used together with the /Fig/Rsrc/Grid dialog.

grid: INTEGER
This variable holds the current grid size in universal units. 

gridDrawingFactor: INTEGER
This variable hold the current grid drawing factor. A grid drawing factor of 5 will cause a line to be drawn for every 5th "grid unit"

unitType: INTEGER
The current unit type.


PROCEDURE SplitPoints
Guard: FigCmds.SplitPointsGuard
Splits a selected point by creating a new point for each of the point's dependent points and figures.The selected point is kept if it's a FigModels.ConstrainedPoint, otherwise it's deleted from the drawing.
	
PROCEDURE SplitPointsGuard (VAR par: Dialog.Par)
Disables the command if the focus view doesn't have exactly one selected point.
	
PROCEDURE JoinPoints
Guard: FigCmds.JoinPointsGuard
Joins all the selected points into one, restructuring their dependent points and figures in the process.
	
PROCEDURE JoinPointsGuard (VAR par: Dialog.Par)
Disables the command if the focus view doesn't have at least two selected points. A FigModels.ConstrainedPoint is only allowed as the join target (last selected point), and then only if it isn't depending on any other selected point.

PROCEDURE SendToBack
Guard: FigCmds.SendToBackGuard
Moves the selected figures to the back, behind all other figures.

PROCEDURE SendToBackGuard (VAR par: Dialog.Par)
Disables the command if the focus view doesn't have any selected figures.

PROCEDURE BringToFront
Guard: FigCmds.BringToFrontGuard
Moves the selected figures to the front, in front of all other figures.
	
PROCEDURE BringToFrontGuard (VAR par: Dialog.Par)
Disables the command if the focus view doesn't have any selected figures.

PROCEDURE AddAnchor
Guard: FigCmds.AddAnchorGuard
Adds an anchorpoint to a figure or a constrained point.

PROCEDURE AddAnchorGuard (VAR par: Dialog.Par)
The command is abled in these situations:
	- If no figure is selected, one constrained point is selected, and the constrained point is expandable and 
	  hasn't reached it's upper limit of anchorpoints.
	- If one figure is selected, one or two points are selected, and the figure has to be expandable and hasn't 
	  reached it's upper limit of anchorpoints. The points have to be anchorpoints to the figure. If only one 
	  point is selected then it has to be a "end-anchorpoint" and the figure mustn't be cyclic. If two points are 
	  selected they have to be adjacent anchorpoints.
		
PROCEDURE RemoveAnchor
Guard: FigCmds.RemoveAnchorGuard
Removes the selected anchorpoint from a figure or constrained point.

PROCEDURE RemoveAnchorGuard (VAR par: Dialog.Par)
The command is abled in these situations:
	- If no figure is selected, and two points are selected. The last selected point is the constrained point 
	  from which the anchorpoint is removed, and it has to be expandable and has more anchorpoints than 
	  it's lower limit. 
	- If one figure and one point is selected, and the figure is expandable and has more anchorpoints than it's
	  lower limit.

PROCEDURE SelectAnchors;
Guard: FigCmds.SelectAnchorsGuard
Selects all the anchorpoints connected to a figure or constrained point.

PROCEDURE SelectAnchorsGuard (VAR par: Dialog.Par);
Disables the command if no objects are selected.

PROCEDURE TogglePointhiding
Guard: FigCmds.TogglePointhidingGuard.
Toggles whether or not points are shown in the focus view.
	
PROCEDURE TogglePointhidingGuard (VAR par: Dialog.Par)
This guard disables the current menu item if no FigViews.View has the focus.

PROCEDURE ToggleGridhiding
Guard: FigCmds.ToggleGridhidingGuard.
Toggles whether or not the grid is shown in the focus view.

PROCEDURE ToggleGridhidingGuard (VAR par: Dialog.Par)
This guard disables the current menu item if no FigViews.View has the focus.

PROCEDURE ToggleGrid
Guard: FigCmds.ToggleGridGuard.
Toggles whether the grid is enabled or disabled in the focus view.

PROCEDURE ToggleGridGuard (VAR par: Dialog.Par)
This guard disables the current menu item if no FigViews.View has the focus.

PROCEDURE InitGridDialog
Initializes the variables gridDlg.grid and gridDlg.gridDrawingFactor to match the focus FigViews.View.

PROCEDURE SetGrid
Guard: FigCmds.SetGridGuard.
Sets the size of the grid based on the variables gridDlg.grid and gridDlg.gridDrawingFactor. This command is used together with InitGridDialog in the Fig/Rsrc/Grid dialog.

PROCEDURE SetGridGuard (VAR par: Dialog.Par)
This guard disables the current menu item if no FigViews.View has the focus.

PROCEDURE GridNotifier (op, from, to: INTEGER)
Used in the /Fig/Rsrc/Grid dialog to prevent the updown controls to go below 1.

PROCEDURE ForceToGrid
Guard: FigCmds.ForceToGridGuard.
Forces all selected points to the grid.

PROCEDURE ForceToGridGuard (VAR par: Dialog.Par)
This guard disables the current menu item if no FigViews.View has the focus, no points are selected or if the grid is disabled.

PROCEDURE Deposit
Deposits a FigView in the view queue.

PROCEDURE FocusGuard (VAR par: Dialog.Par)
This guard disables the menu item if no FigViews.View has focus.

PROCEDURE SelectionGuard (nofPoints: INTEGER; VAR par: Dialog.Par)
This guard disables the menu item if less than nofPoints points are selected.

PROCEDURE ArrowDlgGuard (VAR par: Dialog.Par);
Checks whether arrowPropDlg is initiated or not.

PROCEDURE ThicknessDlgGuard (VAR par: Dialog.Par);
Checks whether TPropDlg is initiated or not.

PROCEDURE InitTPropDialog
This initializes the variable TPropDlg.

PROCEDURE SetTProp
Emits a FigModels.TProp based on TPropDlg.

PROCEDURE InitArrowPropDialog
This initializes the variable arrowPropDlg.

PROCEDURE SetArrowProp
Emits a FigModels.ArrowProp based on arrowPropDlg.

PROCEDURE UpDownGuard (VAR par: Dialog.Par)
Disables the updown control in the /Fig/Rsrc/Thickns dialog if TPropDlg.type is fill or hairline.

PROCEDURE UpDownNotifier (op, from, to: INTEGER)
Prevents the updown control in the /Fig/Rsrc/Thickns dialog to go below 1.

PROCEDURE OpenExtensionDocu
Guard: OpenExtensionDocuGuard
Opens the documentation of the latest figure or constrained Point.

PROCEDURE OpenExtensionDocuGuard (VAR par: Dialog.Par)
Guard for OpenExtensionDocu
Disables the command if no figure or constrained point is selected.

