StdGrids

DEFINITION StdGrids;

	IMPORT Models, Views, Ports, Properties, StdWindows;

	CONST
		activate = 1;
		append = -1;
		divider = 1;
		horizontal = TRUE;
		remove = 0;
		setTitle = 2;
		stack = 0;
		vertical = FALSE;

	TYPE
		Context = POINTER TO EXTENSIBLE RECORD (GenericContext)
			filler-: View;
			view-: Views.View;
			title-: Views.Title;
			(fc: Context) Consider (VAR p: Models.Proposal);
			(c: Context) GetSize (OUT w, h: UCs)
		END;

		GenericContext = POINTER TO ABSTRACT RECORD (Models.Context)
			(c: GenericContext) Normalize (): BOOLEAN;
			(c: GenericContext) ThisModel (): Models.Model
		END;

		Param = POINTER TO RECORD 
			color, tabHeight: INTEGER;
			bgColor, gutterColor, marksColor, closerColor: Ports.Color;
			closerSide: BOOLEAN;
			closerMark: ARRAY 3 OF CHAR;
			boldenFront: BOOLEAN;
			selectedBgColor, nonselectedBgColor, strokeColor, strokeColorShaded, markColor: Ports.Color;
			markIsBottom: BOOLEAN;
			margins, gutterWidth: INTEGER;
			debug: BOOLEAN;
			InitDefaults-, Init-: PROCEDURE (par: ANYPTR)
		END;

		View = POINTER TO ABSTRACT RECORD (Views.View)
			kind-: BYTE;
			ctx-: POINTER TO ARRAY OF Context;
			canDrop: PROCEDURE (v: View; ind: INTEGER): BOOLEAN;
			transferCopy: PROCEDURE (source: Views.View; toV: View; toF: Views.Frame): Views.View;
			backdrop: Views.View
		END;

		Proposal = RECORD (Models.Proposal)
			op: INTEGER;
			title: Views.Title
		END;

		RemoveMsg = RECORD (Views.CtrlMessage)
			processed: BOOLEAN
		END;

		TabVerb = PROCEDURE (c: Context);

		TrackMsg = RECORD (Views.Message)
			name: Views.Title;
			cnt: INTEGER;
			track: View;
			frame: Views.Frame
		END;

		TrackPropMsg = RECORD (Views.PropMessage)
			done, poll: BOOLEAN;
			name: Views.Title
		END;

		WindowPref = RECORD (Properties.Preference)
			win: StdWindows.Window
		END;

	VAR
		gp: Param;

	PROCEDURE Activate (c: Context);
	PROCEDURE AppendDivTile (f: View; view: Views.View; size: UCs; pSize: INTEGER);
	PROCEDURE AppendStackTab (f: View; view: Views.View; IN title: Views.Title);
	PROCEDURE Focus (v: View): Context;
	PROCEDURE Len (f: View): INTEGER;
	PROCEDURE NewDivider (vertical: BOOLEAN): Divider;
	PROCEDURE NewStack (): Stack;
	PROCEDURE Remove (c: Context);
	PROCEDURE ThisFiller (v: Views.View): View;
	PROCEDURE Transfer (what: Views.View; where: View; pos: INTEGER);

END StdGrids.


Module allows to make composit grids from several views.

CONST horizontal
Possible value for parameter vertical in procedure NewDivider.

CONST vertical
Possible value for parameter vertical in procedure NewDivider.

CONST append
Possible value for parameter pos in procedure Transfer.

CONST divider, stack
Kind of View

CONST remove, activate, setTitle
Possible ... 


PROCEDURE NewDivider (vertical: BOOLEAN): Divider;
Creates new element of grid, which devides area in two.

PROCEDURE NewStack (): Stack;
Creates new element of grid, which puts several views with tabs switching panel.

PROCEDURE AppendDivTile (f: View; view: Views.View; size: UCs; pSize: REAL);
Append grid element to view to f. Make it size in BB points of pSize in percents.


PROCEDURE AppendStackTab (f: View; view: Views.View; IN title: Views.Title);

PROCEDURE Activate (c: Context);

PROCEDURE Remove (c: Context);

PROCEDURE ThisFiller (v: Views.View): View;

PROCEDURE Transfer (what: Views.View; where: View; pos: INTEGER);

PROCEDURE Focus (v: View): Context;

PROCEDURE Len (f: View): INTEGER;