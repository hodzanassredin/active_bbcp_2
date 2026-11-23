StdLinks

DEFINITION StdLinks;

	IMPORT Dialog, Views;

	TYPE
		Link = POINTER TO RECORD (Views.View)
			leftSide-: BOOLEAN;
			(v: Link) GetCmd (OUT cmd: ARRAY OF CHAR), NEW
		END;

		Target = POINTER TO RECORD (Views.View)
			leftSide-: BOOLEAN;
			(t: Target) GetIdent (OUT ident: ARRAY OF CHAR), NEW
		END;

		Directory = POINTER TO ABSTRACT RECORD 
			(d: Directory) NewLink (IN cmd: ARRAY OF CHAR): Link, NEW, ABSTRACT;
			(d: Directory) NewTarget (IN ident: ARRAY OF CHAR): Target, NEW, ABSTRACT
		END;

	VAR
		par-: Link;
		dir-, stdDir-: Directory;

	PROCEDURE CreateGuard (VAR par: Dialog.Par);
	PROCEDURE CreateLink;
	PROCEDURE CreateTarget;
	PROCEDURE ShowTarget (ident: ARRAY OF CHAR);
	PROCEDURE SetDir (d: Directory);

END StdLinks.


Link views, also called links, are views that always appear in pairs. They are only meaningful when embedded in texts. A pair brackets a stretch of text and contains a command. This an example of a link:

	A link to the user manual

If you do not see anything special to the left and right of the above text stretch, use Text->Show Marks to make the views visible.

If you click on either the left or right link view, the command associated with the link is executed. Moreover, the entire text stretch between the link pair is active also. The mouse cursor changes its shape when it points to the active stretch.

If you hold down the Ctrl key when clicking the right mouse button on a link, a dialog is opened that allows for inspecting and editing the link's properties. The Link property contains the command (or command sequence) of the link. The Close property defines if the document that contains the link is closed when the link is executed. This dialog is described more fully in Links & Targets.

If you hold down the Ctrl key when clicking the left mouse button on a link, the two views are transformed into a textual form:

	<StdCmds.OpenBrowser('System/Docu/User-Man', 'User Manual')>A link to the user manual<>

The syntax is "<" command sequence ">" text stretch "<" ">". The command sequence usually consists of a StdCmds.OpenBrowser command. However, any command may be used, e.g., like in the following specification:

	<Dialog.Beep; Dialog.Beep>beep beep<>

To turn this specification into an active text stretch, select it (from and including the first "<", to and including the last ">") and then execute Tools->Create Link.
A possibly simpler way to create this link is to enter, then select, the text "beep beep" (without the quotes), then use the menu option Tools->Create Link; then type into the Link: field "Dialog.Beep; Dialog.Beep" (without the quotes); then click Apply. The blue coloring and underlining are applied automatically.

You may have noticed that the name "link" thus just denotes the most typical use of link views. They are not inherently specialized for text linking. The behavior is completely determined by the command sequence associated with them.

In order to use link views for hypertext linking, it must be possible to use link commands which open a particular piece of text. The standard command for this purpose is StdCmds.OpenBrowser. For example, the command "StdCmds.OpenBrowser('Obx/Docu/Sys-Map', 'Map to the Obx Subsystem')" opens the Obx map text in browser mode.

Sometimes it is useful to have a command which, as a reaction to activating it via a link view, scrolls the text in which the link view is embedded, to a certain target position. This target position can be marked with a pair of target views, which are created and handled in a similar way as link views. The command to scroll to a certain target view is ShowTarget. To determine which target to show, a target contains an arbitrary identifier name.

For example, the link view with the specification
	<StdLinks.ShowTarget('first target')>show target<>
creates a link to a target given by the specification
	<first target>this is the first target<>

MENU
	"Create Link"	""	"StdLinks.CreateLink"	"StdLinks.CreateGuard"
	"Create Target"	""	"StdLinks.CreateTarget"	"StdLinks.CreateGuard"
END


TYPE Link (Views.View)
View type for links.

leftSide-: BOOLEAN
Tells whether it is a left or a right view.

PROCEDURE (v: Link) GetCmd (OUT cmd: ARRAY OF CHAR)
NEW
Returns the link's command.

Post
leftSide = (cmd # "")


TYPE Target (Views.View)
View type for targets.

leftSide-: BOOLEAN
Tells whether it is a left or a right view.

PROCEDURE (t: Target) GetIdent (OUT ident: ARRAY OF CHAR)
NEW
Returns the target's identifier.

Post
leftSide = (ident # "")

TYPE Directory
ABSTRACT 
Directory type for link/target views.

PROCEDURE (d: Directory) NewLink (IN cmd: ARRAY OF CHAR): Link
NEW, ABSTRACT
Returns a new link view with cmd as command string. It is a left view if cmd # "", otherwise a right view.

PROCEDURE (d: Directory) NewTarget (IN ident: ARRAY OF CHAR): Target
NEW, ABSTRACT
Returns a new target view with ident as identifier string. It is a left view if ident # "", otherwise a right view.


VAR par-: Link	par # NIL exactly during the currently executed link command
A command in a link can get access to its (left) link view, and thus to its context, via this variable during the execution of the command.

VAR dir-, stdDir-: Directory
Link/target directories.


PROCEDURE CreateGuard (VAR par: Dialog.Par)
Menu guard procedure used for CreateLink and CreateTarget. par.disabled remains FALSE (i.e. the menu entry is enabled) if the focus view is a text view and has a selection.

PROCEDURE CreateLink
Insert a link into the focus text. If the selected text does not follow the special syntax described below, it is converted into a link (blue and underlined and embedded within a left and a right link view) and the link's property editor is opened.
If the selected text follows the syntax  "<" command sequence ">" arbitrary text "<>", were the command sequence must not contain a ">" character, a link including the command sequence is inserted. The stretch "<" command sequence ">" is replaced with the left link view, the stretch "<>" is replaced with the right link view.
The link views can be shown/hidden with Text->Show Marks and Text->Hide Marks, respectively. To inspect or edit the command sequence of a link, click with the right mouse button on a link. Alternatively, click on one of the link views with the Ctrl key pressed. This replaces the views with the special syntax for creating the link.

PROCEDURE CreateTarget
Insert a target into the focus text. If the selected text does not follow the special syntax described below, it is converted into a target (embedded within a left and a right target view) and the target's property editor is opened.
If the selected text follows the syntax  "<" target identifier ">" arbitrary text "<>", were the target identifier must not contain a ">" character, a target including the target identifier is inserted. The stretch "<" target identifier ">" is replaced with the left target view, the stretch "<>" is replaced with the right target view.
Target views can be shown/hidden with Text->Show Marks and Text->Hide Marks, respectively. To inspect or edit the identifier of a target, hold down the Ctrl key and click with the right mouse button on a target. Alternatively, click the left mouse button on one of the target views with the Ctrl key pressed. This replaces the views with the special syntax for creating the target.

PROCEDURE ShowTarget (ident: ARRAY OF CHAR)
Searches the first target view in the focus text whose target identifier equals ident. If one is found, the text is scrolled such that the target view is shown on the first line, and the text stretch between the left and right target views is selected. (Note: if the text is opened in mask mode, the selection is not visible.)

PROCEDURE SetDir (d: Directory)
Sets the link/target view directory.
