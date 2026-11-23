StdTabViews

DEFINITION StdTabViews;

	IMPORT Views, Dialog, StdCFrames;

	CONST
		noTab = -1;
		
	TYPE
		Directory = POINTER TO ABSTRACT RECORD 
			(d: Directory) New (): View, NEW, ABSTRACT
		END;

		Frame = POINTER TO ABSTRACT RECORD (StdCFrames.Frame)
			(f: Frame) GetDispSize (OUT x, y, w, h: INTEGER), NEW, ABSTRACT;
			(f: Frame) InDispArea (x, y: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(f: Frame) SetIndex (i: INTEGER), NEW
		END;

		FrameDirectory = POINTER TO ABSTRACT RECORD 
			(d: FrameDirectory) GetTabSize (VAR w, h: INTEGER), NEW, ABSTRACT;
			(d: FrameDirectory) New (): Frame, NEW, ABSTRACT
		END;

		View = POINTER TO LIMITED RECORD (Views.View)
			(tv: View) GetItem (i: INTEGER; OUT label: Dialog.String; OUT v: Views.View), NEW;
			(tv: View) GetNewFrame (VAR frame: Views.Frame);
			(tv: View) GetNotifier (OUT notifier: Dialog.String), NEW;
			(tv: View) HandleCtrlMsg (f: Views.Frame; VAR msg: Views.CtrlMessage; VAR focus: Views.View);
			(tv: View) Index (): INTEGER, NEW;
			(tv: View) Neutralize;
			(tv: View) NofTabs (): INTEGER, NEW;
			(tv: View) Restore (f: Views.Frame; l, t, r, b: INTEGER);
			(tv: View) SetIndex (i: INTEGER), NEW;
			(tv: View) SetItem (i: INTEGER; label: Dialog.String; v: Views.View), NEW;
			(tv: View) SetNofTabs (nofTabs: INTEGER), NEW;
			(tv: View) SetNotifier (IN notifier: ARRAY OF CHAR), NEW
		END;
		
		NotifierProc = PROCEDURE (tv: View; from, to: INTEGER);

	VAR
		dir-: Directory;
		dlg: RECORD 
			name, notifier: Dialog.String;
			opt: INTEGER
		END;
		frameDir-: FrameDirectory;
		frameStdDir-: FrameDirectory;
		setFocus: BOOLEAN;
		stdDir-: Directory;

	PROCEDURE AddTab;
	PROCEDURE BeginChanges (tv: View);
	PROCEDURE Delete;
	PROCEDURE DeleteGuard (VAR par: Dialog.Par);
	PROCEDURE Deposit;
	PROCEDURE EndChanges (tv: View);
	PROCEDURE Focus (): View;
	PROCEDURE InitDialog;
	PROCEDURE LabelGuard (VAR par: Dialog.Par);
	PROCEDURE LayoutModeGuard (VAR par: Dialog.Par);
	PROCEDURE Left;
	PROCEDURE MaskModeGuard (VAR par: Dialog.Par);
	PROCEDURE ModeNotifier (op, from, to: INTEGER);
	PROCEDURE NewGuard (VAR par: Dialog.Par);
	PROCEDURE NotifierGuard (VAR par: Dialog.Par);
	PROCEDURE Rename;
	PROCEDURE RenameGuard (VAR par: Dialog.Par);
	PROCEDURE Right;
	PROCEDURE SetDir (d: Directory);
	PROCEDURE SetFrameDir (d: FrameDirectory);
	PROCEDURE SetGuard (VAR par: Dialog.Par);
	PROCEDURE SetNotifier;
	PROCEDURE This (v: Views.View): View;

END StdTabViews.


A StdTabViews.View displays a set of tabs to the user. Each tab consists of a label and a view. When the user clicks on the label, the associated view is displayed. Try it on the example below!




This example was created with FormViews but any Views.View can be used in the tabs.

There are two ways of creating StdTabViews: The tab can be created programmatically or it can be created using a graphical user interface. In most cases the graphical user interface is enough, and no programming is needed to create a StdTabViews.View. For more advanced use there exists a programming interface and this will be described further down.

To use the graphical user interface a StdTabViews.View needs to be dropped into a document or a form. This can be done by selecting the option Insert Tab View from the Controls menu. The new StdTabViews.View contains two tabs, called Tab1 and Tab2, and they each have an (empty) FormViews.View associated with them. A newly created tab looks like this:




The StdTabViews.View is fully functional, but a bit boring. If the StdTabViews.View is focused, each tab can be edited just like a normal form. Controls can be dropped into it and moved aound and their properties can be edited. Also this can be tried on the tab above.

To add, remove or edit the name of tabs the StdTabViews property editor needs to be started. This is done by selecting the tab and choosing "Edit->Object Properties...". The property editor looks like this:



The buttons and fields have the following meaning:

<-
Clicking on this button moves the current tab one step to the left. This can be used to changed the order of the tabs.

->
Clicking on this button moves the current tab one step to the right. This can be used to changed the order of the tabs.

Label
Displays the label of the current tab.

Rename
Sets the label of the currrent tab to the text displayed in the label field. An empty label is not allowed.

New Tab
Creats a new tab and adds it to the list. The label of the new tab is set to the text displayed in the label field. If the text field is empty no new tab can be created.

Delete
Removes the current tab from the tab view.

Notifier
Displays the name of the notifier that is associated with the tab view.

Set
Sets the notifier of the the tab view to the name displayed in the notifier field.

All tabs in Layout Mode
Sets all tabs in the Tab View in Layout Mode. Layout Mode is a container mode that allows for editing. For more information about container modes see About Container Modes futher down.

All tabs in Mask Mode
Sets all tabs in the Tab View in Mask Mode. Mask Mode is a container mode that doesn't allow selecting of contained views. This mode is used for displaying documents as dialogs. For more information about container modes see About Container Modes futher down.

About Notifiers
The notifier for StdTabViews is not the same as a notifier for normal Controls, but it works in a similar way. The signarure is different and there is just one type of notification sent. StdTabViews notifiers have the signature described by the type StdTabViews.NotifierProc.

The notifier is only called when the current tab is changed. When called, tv is the StdTabViews.View that the notification concerns, from is the tab that used to be the current tab and to is the new current tab.

When a tab is saved the index of the current tab is saved with it and when the tab is internalized the current tab is set to the index that was saved. This enables the designer of  a dialog with a tab to control which tab is the current tab when the dialog is opened. It also provides a way to find a StdTabViews.View in a dialog. When the current tab is set for the first time during internalization the notifier is called with tv set to the new StdTabViews.View, from set to the constant noTab and to set to the index of the current tab saved in externalize.

This means that whenever a document with a StdTabViews.View inside is opened the notifier is called and this allows the application to bind to the StdTabViews.View if needed. This is an easy way to get to the programming interface described below.

About Container Modes
Containers can have different modes. A FormViews.View is a container and can for example be put in Layout Mode and Mask Mode. In Layout Mode it is possible to select contained views, such as Controls, and move them around. In Mask Mode, on the other hand, it is not possible to select or move around contained views.

Container modes can be changed using the Dev-menu but normally this is not needed. The normal way is to save a document in Edit or Layout Mode and then open it in the desired mode using the commands provided in StdCmds. A dialog, for example, is usually saved in Layout Mode and then opened in Mask Mode by using StdCmds.OpenAuxDialog.

The problem is that StdTabViews breaks this pattern since each tab can contain its own container with its own mode. It is needed to use the Dev-menu to put the tabs in the correct mode before saving a dialog. To use the container modes and the Dev-menu requires some knowledge about Containers. To avoid this requirement the radiobuttons "All tabs in Layout Mode" and "All tabs in Mask Mode" are provided. These buttons make it possible to set all tabs in Mask mode before saving the dialog or setting all tabs in Layout mode to edit the dialog, thus eliminating the need to use the Dev-menu.

CONST noTab
Constant used as from value to the notifier when a StdTabViews.View is internalize. 

TYPE View = POINTER TO LIMITED RECORD (Views.View)
Allows the user to select different views by clicking on tabs.

PROCEDURE (tv: View) SetItem (i: INTEGER; label: Dialog.String; v: Views.View)
NEW
Adds a new tab to tv. The new tab gets label as its label and v as its view. i indicates the position of the tab among the other tabs. IF i is greater than tv.NofTabs() then then number of tabs in tv is increased, if not, the prevous tab at position i is overwritten. A deep copy of v is made before it is added to tv.

Pre
i >= 0
label # ""
v # NIL

PROCEDURE (tv: View) GetItem (i: INTEGER; OUT label: Dialog.String; OUT v: Views.View)
NEW
Retrievs the label and the view for tab i.

Pre
i >= 0
i < tv.NofTabs()

PROCEDURE (tv: View) SetNofTabs (nofTabs: INTEGER)
NEW
Makes sure that nofTabs tabs are available in tv.Note that SetItem also increases the number of tabs of the View if necessary, so SetNofTabs is strictly only necessary to decrease the number of tabs.

Pre
nofTabs >= 0

Post
tv.NofTabs() = nofTabs

PROCEDURE (tv: View) NofTabs (): INTEGER, NEW;
NEW
Retruns the number of tabs in tv.

Post
Returned value >= 0

PROCEDURE (tv: View) SetIndex (i: INTEGER)
NEW
Sets the current tab in tv, updates the view. The notifier is not called when tabs are changed by a call to SetIndex.

Pre
i >= 0
i < tv.NofTabs()

Post
tv.Index() = i

PROCEDURE (tv: View) Index (): INTEGER
NEW
Returns the index of the currently selected tab. The index is updated whenever a user clicks on a tab or whenever SetIndex is called.

Post
Returned value is >= 0 and < tv.NofTabs()

PROCEDURE (tv: View) SetNotifier (IN notifier: ARRAY OF CHAR)
NEW
Sets the notifier of tv to be notifier. notifier = "" is permitted and it is interpreted as meaning that tv has no notifer. For more information about notifiers, see the chapter About Notifiers above.

PROCEDURE (tv: View) GetNotifier (OUT notifier: Dialog.String)
NEW
Sets notifier to the value of the notifier of tv. notifer is the empty string if tv doesn't have a notifier.

TYPE Directory 
ABSTRACT
Directory type for StdTabViews.View.

PROCEDURE (d: Directory) New (): View
NEW, ABSTRACT
Allocates and returns a new View.

TYPE NotifierProc = PROCEDURE (tv: View; from, to: INTEGER)
StdTabViews notification commands must have this signature. Through calls of notification procedures, an application can be notified when the selected tab of a StdTabViews.View is changed due to user interaction. The parameters have the following meaning:
tv 	= the StdTabViews.View where the tab was changed.
from	= the selected tab before the change. Has the value noTab when the notifier is called from Internalize.
to 	= the new selected tab.

VAR dir-, stdDir-: Directory	dir # NIL  &  stdDir # NIL
Directories for creating StdTabViews.Views.

PROCEDURE SetDir (d: Directory)
Set directory.

PROCEDURE Deposit
Allocate a new View using dir.New, adds two tabs to it and deposits it.

PROCEDURE This (v: Views.View): View
Traverserses view v and all its contained views (if any) and returns the first StdTabViews.View that is found. If no StdTabViews.View is found then NIL is returned.

Pre
v # NIL

PROCEDURE Focus (): View
Searches for a StdTabViews.View along the focus path. If one is found it is returned, if not NIL is returned. If more than one StdTabViews.View exists in the focus path it is undefinied which one will be returned.

PROCEDURE BeginChanges (tv: View)
Disables update of tv until a subsequent call to EndChanges. Each call to BeginChanges must be balanced by a call to EndChanges, otherwise a trap will occur (in StdTabViews.BalanceAction.Do). The calls may be nested. BeginChanges and EndChanges are provided to make it possible to do several changes to a StdTabViews.View without intermediate updates.

Pre
Number of calls to BeginChanges >= Number of calls to EndChanges, 20

PROCEDURE EndChanges (tv: View)
Enables update for tv again after a call to BeginChanges.

Pre
Number of calls to BeginChanges > Number of calls to EndChanges, 20


The following types, variables and procedures are only used internally to handle frames of StdTabViews:

TYPE Frame = POINTER TO ABSTRACT RECORD (StdCFrames.Frame)
TYPE FrameDirectory = POINTER TO ABSTRACT RECORD 

VAR frameDir-, frameStdDir-: FrameDirectory 

PROCEDURE SetFrameDir (d: FrameDirectory)
PROCEDURE (f: Frame) GetDispSize (OUT x, y, w, h: INTEGER), NEW, ABSTRACT
PROCEDURE (f: Frame) InDispArea (x, y: INTEGER): BOOLEAN, NEW, ABSTRACT
PROCEDURE (f: Frame) SetIndex (i: INTEGER), NEW
PROCEDURE (d: FrameDirectory) GetTabSize (VAR w, h: INTEGER), NEW, ABSTRACT
PROCEDURE (d: FrameDirectory) New (): Frame, NEW, ABSTRACT


The following types, variables and procedures are only used for the property inspector of the StdTabViews.View:

VAR dlg: RECORD 
			name, notifier: Dialog.String;
			opt: INTEGER
		END;

PROCEDURE AddTab
PROCEDURE Delete
PROCEDURE DeleteGuard (VAR par: Dialog.Par)
PROCEDURE InitDialog
PROCEDURE LabelGuard (VAR par: Dialog.Par)
PROCEDURE LayoutModeGuard (VAR par: Dialog.Par);
PROCEDURE Left
PROCEDURE MaskModeGuard (VAR par: Dialog.Par);
PROCEDURE ModeNotifier (op, from, to: INTEGER);
PROCEDURE NewGuard (VAR par: Dialog.Par)
PROCEDURE NotifierGuard (VAR par: Dialog.Par)
PROCEDURE Rename
PROCEDURE RenameGuard (VAR par: Dialog.Par)
PROCEDURE Right
PROCEDURE SetGuard (VAR par: Dialog.Par)
PROCEDURE SetNotifier

The following variable is only used internally:

VAR setFocus: BOOLEAN
