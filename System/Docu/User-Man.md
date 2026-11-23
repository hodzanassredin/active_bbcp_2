BlackBox Component Builder
User Manual


Contents

1 Overview
2 Installation
3 Server installation
4 Deinstallation
5 Document Windows, Tool Windows, Auxiliary Windows
6 Document Size
7 Menu Configuration
8 String Resources
9 Standard Commands
	9.1 File Menu
	9.2 Edit Menu
	9.3 Attributes Menu
	9.4 Window Menu
10 Custom Commands

Further user manuals
Text Subsystem
Form Subsystem
Dev Subsystem

1 Overview

This document serves as a user's guide for BlackBox programmers. It is not intended as a manual for the end user of software written with the BlackBox Component Builder. Knowledge of the underlying platform's user interface guidelines is assumed.
With the BlackBox Component Builder, there are no separate environments for developing programs, for testing and debugging programs, or for the distribution of programs. Instead there is only one, truly integrated, environment for all these purposes. A distribution version of a BlackBox application can be created simply by stripping away all tools which are specific to the development process.
Such a customized BlackBox environment always has the basic capabilities of the BlackBox Component Framework's compound document architecture, and of the standard text and form subsystems. Furthermore, it constitutes a standard Windows application, where the (boot) application can be double-clicked, where documents can be dropped onto the application icon to open them, etc.
"Native" applications which are only developed under, but not based on, the BlackBox Component Builder can be created as well. For this purpose, a linker tool is provided. Normally, Component Pascal modules are linked and loaded dynamically, such that a separate linker is not strictly necessary. However, it is possible even for pure BlackBox applications to link or pack all or some of their modules together, in order to reduce the number of files to distribute. For further information about this topic, refer to the document Platform-Specific Issues.
On-line documentation:
Text stretches which are blue and underlined are hyperlinks, and can be followed by clicking on them.
When working in the BlackBox Component Builder environment for the first time, the following may be helpful to remember: almost all modifications to BlackBox documents are undoable, making it quite safe to try out a feature. In general, multi-level undo/redo is available, i.e., not only one, but several commands can be undone; as many as memory permits.
An endless loop can be terminated with ctrl-break.

2 Installation

The installation requires a PC with an x86 and Windows XP/2003/Vista/7/8/10/11.

The resulting BlackBox directory contains several files and directories:

Files:
BlackBox.exe	The BlackBox Component Framework boot application.
BlackBox.exe.manifest	Manifest file for BlackBox.exe.
	Remove this file if you want to use Windows XP style GUI controls.
Empty.odc	Empty BlackBox Component Builder document.
unins000.dat	Data file for BlackBox Component Builder Uninstall.
unins000.exe	BlackBox Component Builder Uninstall.

Directories:
Com	Direct-To-COM Compiler.
Comm	Communications subsystem.
Cons	Modules for console applications.
Ctl	OLE Automation support.
Dev	Development subsystem.
Docu	On-line documentation not specific to a particular module or subsystem.
Form	Form subsystem with the visual designer.
Obx	Obx subsystem, a collection of examples.
Ole	OLE compound document support.
Sql	Sql subsystem for accessing relational databases.
Std	A number of command packages available to the user.
System	Core of the BlackBox Component Framework.
Text	Text subsystem, with the standard document/program editor.
Win	Interface modules for direct Windows API access.
Xhtml	Exporter for text to HTML conversion.

3 Server installation

If several developers use BlackBox on the same machine(s), it becomes cumbersome to save one's work and clean up after the end of each session, so that the next developer gets a "clean" system again. It is even more cumbersome to start working with a copy that hasn't been cleaned up correctly by the previous user. To solve this problem, server support is available for BlackBox.
BlackBox can be installed and maintained on one central server, while developers use an arbitrary number of client workstations on a local-area network. Preferably, each developer has his or her own working directory on the server (account).
For the installation, follow these steps:
1) Install BlackBox in a directory on the server machine, using Setup.exe. The directory must be shared on the network but the access may be restricted to read only.
2) For each workstation / user, create a working directory (either on the server or on the client). The user should have read/write access to this directory. The directory may be empty.
3) For each workstation, create a shortcut with the following contents:

  Command Line (Target): <BlackBoxDir>\BlackBox.exe /Use <WorkDir>
  Working Directory (Start in): <WorkDir>

where <BlackBoxDir> stands for the full path name of the directory where BlackBox is installed (on the server), and <WorkDir> is the path of the working directory of the actual machine (on the client or on the server). The latter must not end with a backslash ("\"). Use double quotation marks to delimit path names that contain spaces.

Example:
  Command Line (Target): "C:\Program Files\BlackBox\BlackBox.exe" /Use C:\BlackBox
  Working Directory (Start in): C:\BlackBox

These features, originally introduced for situations where multiple users want to develop with BlackBox, can also be useful in a single-user environment. The idea is to have one directory with the original installation of BlackBox, and a separate directory for all the developer-specific files. If the user opens a file, it is first searched in his or her working directory. If it isn't found there, the corresponding file in the "server directory" is opened. When a file is saved, it is always saved in the working directory. This makes it easy to set up entirely separate projects simply by creating separate working directories for them. When an upgrade of BlackBox comes out, only the central "server directory" needs to be upgraded. If you use the server features in this way, without having multiple developers using the same installation simultaneously, then no special license is required.

4 Deinstallation

Go to the Control Panel, choose Add/Remove Programs. In the displayed list, locate the entry called BlackBox and select the Change/Remove button. Follow the instructions on the screen.

5 Document Windows, Tool Windows, Auxiliary Windows

There are three kinds of windows in BlackBox: document windows, tool windows, and auxiliary windows.
A document window may contain e.g. a text or a form layout, or any other kind of visual object ("view"). When the contents of a document window have been modified (made "dirty") and the user tries to close the window (or quit the application), the system asks whether it should save the document.
A tool window allows to invoke actions on some document window underneath it. Typically, tool windows are used for modeless dialog boxes. Tool windows look the same way as dialog boxes.
Auxiliary windows are used mainly to hold temporary data for information purposes, e.g., the output of a browser. The contents of an auxiliary window may be editable, but the system does not ask whether a modified auxiliary window should be stored, i.e., it is temporary in nature. The Log window is an example of an auxiliary window.
A document window is decorated with the BlackBox document icon, while an auxiliary window is decorated with the BlackBox application icon. A tool window is not decorated with an icon.

6 Document Size

The size of a document, or more exactly of its outermost ("root") view, can be updated in several ways. Its width, or independently its height, can be bound either to a fixed size, to the paper as defined in the Page Setup dialog box, or to the window's current size.
For example, text views by default have a width bound to the paper page size, and a height bound to the window size. Documentation texts are often bound to the current window size in both dimensions, so that they automatically resize with the window. Such bindings can be changed with the Tools->Document Size... dialog box.

7 Menu Configuration

The configurable menus can be inspected using Info->Menus. The displayed text can be edited, and the current menu configuration updated accordingly (Info->Update Menus). To make changes to the menus permanent, the menu text must be saved to disk. The file System/Rsrc/Menus contains the startup menu configuration, i.e., the text which is opened when Info->Menus is executed.
The menu text consists of a sequence of menu definitions, which themselves consist of sequences of menu items. An example is the following extract of a possible Dev menu definition:
 MENU "Dev"
	"Compile"	"K"	"DevCompiler.Compile"	"TextCmds.FocusGuard"
	"Compile Selection"	""	"DevCompiler.CompileSelection"	"TextCmds.SelectionGuard"
	SEPARATOR
	"Unmark Errors"	""	"DevMarkers.UnmarkErrors"	"TextCmds.FocusGuard"
	"Next Error"	"E"	"DevMarkers.NextError"	"TextCmds.FocusGuard"
	"Toggle Error Mark"	""	"DevMarkers.ToggleCurrent"	"TextCmds.FocusGuard"
	SEPARATOR
	"Insert Commander"	"Q"	"DevCommanders.Deposit; StdCmds.PasteView"	"StdCmds.PasteViewGuard"
	"Execute"	""	"DevDebug.Execute"	"TextCmds.SelectionGuard"
	"Unload"	""	"DevDebug.Unload"	"TextCmds.SelectionGuard"
END
Every menu has a name, in this case it is Dev. Optionally, the menu name can be followed by a menu type, e.g.,

MENU "Text" ("TextViews.View")

A typed menu is only installed in the menu bar as long as the current focus has a matching type, i.e., it is context-sensitive. The other menus are always available.
A menu's type is usually simply the name of a view type. This is only a convention, however. It guarantees that menu types are globally unique, so that no clashes occur.
There are two kinds of menu items: normal items and separators. A separator optically organizes a menu into different groups of items. Normal menu items consist of four strings: a label, a keyboard shortcut, an action command, and a guard command. The label is the string presented to the user in the menu.
An "&" character in the menu name or in a menu label indicates which character should be underlined. This character is used for operating the menu with the keyboard. If you want an "&" to appear, you should write "&&".
The keyboard shortcut, which may be empty, allows to associate a keyboard key to the menu item. The action string contains the command sequence which is activated when the menu item is executed. The guard string, which may be empty, contains a command which is called to determine whether the item is currently enabled or disabled, checked or unchecked, or to set up a current item name which overrides the normal name (e.g., to toggle between Show XYZ and Hide XYZ).
Note: the menu guard is executed for example when the user clicks in the menu bar. This causes the guard's module to become loaded, even if the user never invokes the corresponding command.
Note: if the guard's module cannot be loaded, the menu item remains disabled and the guard is not executed again (for performance reasons). If the module's code becomes available later, e.g., because its module was later compiled, the menu item will remain disabled. To force a re-evaluation of the guard, use the Dev->Flush Resources command.
Note: the standard menu configuration uses all letters of the alphabet and the digit "0" as keyboard shortcuts. Digits "1" to "9" are not used. The assignment of keyboard shortcuts, like the whole menu configuration, can easily be adapted to specific needs by appropriately changing the menu text.
The following keyboard shortcuts can be specified in the keyboard shortcut string:
	"A".."Z", "0".."9"	ctrl + key
	"*A".."*Z", "*0".."*9"	shift + ctrl + key
	"F1".."F12"	function key
	"^F1".."^F12"	ctrl + function key
	"*F1".."*F12"	shift + function key
	"*^F1".."*^F12"	shift + ctrl + function key

Context menus (pop-up menus activated by the right mouse button) are specified by giving them the name "*" instead of a true name. At most one context menu may be untyped, all other context menus must be typed.
For example, just add the line

        "Open &Module"  ""      "DevCmds.OpenModuleList"        "TextCmds.SelectionGuard"

to the menu  MENU "*" ("TextViews.View")  in the file Text/Rsrc/Menus. This adds a (text-)context menu item for opening the sources of the module(s) whose name(s) is (are) selected.
It is possible to put all menu specifications in the menu text System/Rsrc/Menus. However, it is a better idea to keep the menu specifications that refer to a subsystem's commands in this subsystem's resource directory. For example, the Text menu could be specified in Text/Rsrc/Menus. In this case, System/Rsrc/Menus needs a so-called include statement that tells the menu configuration mechanism where to look for further menus:

	INCLUDE "Text"

The explicit include statements allow to define the exact order in which menus appear in a menu bar. The command

	INCLUDE "*"

includes all menus that have not been mentioned explicitly before (directly or via an include statement). It is a "catch all" for menus, and it is recommended to put it at the end of the System/Rsrc/Menus text. For example, a typical System/Rsrc/Menus text may look as follows:

	INCLUDE "Dev"
	INCLUDE "Text"
	INCLUDE "Form"
	INCLUDE "Sql"
	INCLUDE "Obx"
	INCLUDE "*"

Note: when you have edited a menu text execute the command Info->Update Menus for re-installing all menus, starting with the System/Rsrc/Menus as it is done when starting up the application.
A submenu is defined very much like a top-level menu except that its name must start with the character "$". In order to reference such a menu in a menu item, use the name of the submenu including the leading "$" as the action string. Menus can be nested at arbitray depth and it is also possible to reference a submenu multiply. The following example shows the structure of a large menu together with a submenu referenced as $submenu1:
MENU "Large Menu"
	...
	"Submenu 1"	""	"$submenu1"	""
END
MENU "$submenu1"
	...
END
Note: if you need a top-level menu name starting with "$" simply put the underlining indicator "&" in front of the name.
For more information see also modules StdMenuTool and StdCmds. The command package modules of all subsystems export commands that may be used in a menu. Consult the various modules' on-line documentation, e.g., Text/Docu/Cmds for module TextCmds.

8 String Resources

String resources are files which define a mapping between strings, e.g., the string "untitled" may be mapped to "sans titre". This is useful to prevent hard-wiring textual messages in the program code, in order to make later editing of these messages possible without requiring a recompilation. From a programmer's point of view, string translation is done in several procedures of module Dialog, e.g., Dialog.MapString. String resource files can be normal BlackBox text documents, which simply consist of the keyword STRINGS followed by a sequence of lines; each line contains a string (the key), a TAB, another string (to which the key is mapped), and a carriage return, e.g.,

STRINGS
untitled	sans titre
open	ouvre
close	ferme

There can be one string resource file per subsystem (cf. Modules and Subsystems).
For example, a call of
    Dialog.MapString("#Form:CntrlInstallFailed", resultString)
in a program maps the string "CntrlInstallFailed" according to the table in the Form/Rsrc/Strings file. In the English version, the mapping is "form controller installation failed". In a German version, "CntrlInstallFailed" might be mapped to "Der Form Controller konnte nicht installiert werden".


9 Standard Commands

In this section, the menu items of the standard menus File, Edit, Attributes, and Window(s) are described. For a menu item which is not permanently enabled, the condition for enabling it is specified. Often, such a guard command is one of the commands exported by module StdCmds.
The standard menu items can be configured in the same way as all other menu items, by editing the System/Rsrc/Menus document. Most standard menu items are calls to a command exported by module StdCmds.

9.1 File Menu

New
Command: StdCmds.New
Guard:
Opens a new document window containing an empty text view.

Open...
Command: StdCmds.OpenDialog
Guard:
Opens the standard file Open dialog box.

Open Stationery...
Command: StdCmds.OpenStationery
Guard:
Opens the standard file Open dialog box, through which a stationery (i.e., template) file can be opened.

Save
Command: StdCmds.SaveDialog
Guard: StdCmds.SaveGuard
Saves the front window's contents to a file. If the window's contents has not yet been saved to a file, the user is asked for a file name.

Save As...
Command: StdCmds.SaveAsDialog
Guard: StdCmds.WindowGuard
Saves the front window's contents to a file. The user is always asked for a file name. After the command, you continue working with the new file.

Save Copy As...
Command: StdCmds.SaveCopyAsDialog
Guard: StdCmds.WindowGuard
Saves the front window's contents to a file. The user is always asked for a file name. After the command, you continue working with the old file.

Close
Command: StdCmds.CloseTopDialog
Guard: StdCmds.WindowGuard
Closes the front window. If the window is a primary document window and its contents has been modified ("dirty"), the user is asked whether to save the window's contents in a file.

Page Setup...
Command: StdDialog.OpenPageSetup
Guard: StdCmds.WindowGuard
Asks the user for the page information of the front window's document, for later printing.

In addition to the data which is specific to the current printer driver, the margins can be set (the distances between the paper's edges and the printed area), and a standard header can be switched on or off. The standard header consists of a page number and a date.

Print...
Command: StdCmds.Print
Guard: StdCmds.PrintGuard
Asks the user for printing information, and then creates a print-out accordingly.

Exit
Command: StdCmds.Exit
Guard:
Terminates the application. If windows with modified contents are open, the user is asked whether to save them in files.

9.2 Edit Menu

Undo [...]
Command: StdCmds.Undo
Guard: StdCmds.UndoGuard
Reverses the effect of the most recent modifying operation. Usually, the kind of operation is given behind the word "Undo", e.g., Undo Paste. Undo can be activated several times, until the opening, creation, or most recent saving of the document. Under low-memory conditions, the number of undoable operations may become reduced.

Redo [...]
Command: StdCmds.Redo
Guard: StdCmds.RedoGuard
Restores the effect of the most recently undone operation. Usually, the kind of operation is given behind the word "Redo", e.g., Redo Paste.

Cut
Command: StdCmds.Cut
Guard: StdCmds.CutGuard
Deletes the selection and puts a copy into the clipboard.

Copy
Command: StdCmds.Copy
Guard: StdCmds.CopyGuard
Puts a copy of the selection into the clipboard.

Paste
Command: StdCmds.Paste
Guard: StdCmds.PasteGuard
Pastes a copy of the clipboard's contents at the caret position. If the focus view contains the same kind of data as the clipboard, the data is inserted directly into the focus view's data. Otherwise, and if the focus view is a container, a copy of the whole view containing the clipboard data is inserted into the focus view's data.

Delete
Command: StdCmds.Clear
Guard: StdCmds.CutGuard
Deletes the selection, without putting it into the clipboard.

Copy Properties
Command: StdCommands.CopyProp
Guard: StdCmds.SelectionGuard
Copies the properties of the current selection. This command has no effect on the clipboard contents.

Paste Properties
Command: StdCommands.PasteProp
Guard: StdCmds.SelectionGuard
Pastes the properties that were copied most recently (see CopyProperties).

Paste Object
Command: StdCmds.PasteObject
Guard: StdCmds.PasteObjectGuard
Pastes a copy of the clipboard's contents at the caret position. If the focus view is a container, a copy of the whole view containing the clipboard data is inserted into the focus view's data.

Paste Special...
Command: OleClient.PasteSpecial
Guard: StdCmds.PasteObjectGuard
Opens a dialog box, which allows to choose the data type of the view in the clipboard, if the view supports several possible types.

Paste to Window
Command: StdCmds.PasteToWindow
Guard: StdCmds.PasteToWindowGuard
Opens a copy of the clipboard's contents into a new document window.

Insert Object...
Command: OleClient.PasteSpecial
Guard: StdCmds.PasteViewGuard
Opens a dialog box which shows all installed OLE servers. When one of them is chosen, an object of this type is allocated and inserted into the front window's contents.

Object Properties...
Command: StdCmds.ShowProp
Guard: StdCmds.ShowPropGuard
Opens an appropriate property sheet for the selected view.

Object
Command: StdCmds.ObjectMenu
Guard: StdCmds.ObjectMenuGuard
Shows a submenu with commands for the selected view. These commands, which are determined by the selected view itself, are called "verbs". Usually, the first two verbs are Edit and Open:

	Edit
	Makes the selected view the current focus view.

	Open
	Opens a new window showing a second view to the selected view.

	+ other verbs defined by the selected view.

Select Document
Command: StdCmds.SelectDocument
Guard: StdCmds.WindowGuard
Selects the root view of the front window's document as a singleton. Note the difference to Select All, which selects the latter's contents instead (or rather the contents of whatever view is currently the focus).

Select All
Command: StdCmds.SelectAll
Guard: StdCmds.SelectAllGuard
Selects the whole focus view's contents.

Select Next Object
Command: StdCmds.SelectNextView
Guard: StdCmds.ContainerGuard
If a view in the container is selected: select the next view.
If the last view is selected or there is no singleton selection: select the first view.

Preferences...
Command: StdDialog.OpenPrefDialog
Guard:
Allows to define several parameters: whether TrueType metrics are used (for best printing results), whether screen updates are performed during scrolling, the font used as default for texts, the font used as default for controls, and whether the status bar is visible or not.

9.3 Attributes Menu

Available: style/size/color-carrying selection or caret in focus view in front window
The following commands work on the selection; if there is no selection, the caret's current attributes are affected instead. These attributes are used as defaults when typing in new text.
For colors, there is a system-wide color (default color) which can be modified by the user. The default color can be changes using an operating-system utility. Everything drawn in the default color will be updated accordingly.

Regular
Command: StdCmds.Plain
Guard: StdCmds.PlainGuard
Checked: if text to the left and to the right of the caret is plain, or if selection is homogeneously plain (i.e., non-bold, non-italicized, non-underlined, and non-striked-out).
Removes all style attributes (bold, italic, underline, strikeout) from the selection.

Bold
Command: StdCmds.Bold
Guard: StdCmds.BoldGuard
Checked: if text to the left and to the right of the caret is bold, or if selection is homogeneously bold.
If the selection is homogeneously bold, it is made non-bold, otherwise it is made bold.

Italic
Command: StdCmds.Italic
Guard: StdCmds.ItalicGuard
Checked: if text to the left and to the right of the caret is italic, or if selection is homogeneously italic.
If the selection is homogeneously italic, it is made non-italic, otherwise it is made italic.

Underline
Command: StdCmds.Underline
Guard: StdCmds.UnderlineGuard
Checked: if text to the left and to the right of the caret is underlined, or if selection is homogeneously underlined.
If the selection is homogeneously underlined, it is made non-underlined, otherwise it is made underlined.

8 point, 9, 10, 12, 16, 20, 24
Command: StdCmds.Size(size)
Guard: StdCmds.SizeGuard(size)
Checked: if text to the left and to the right of the caret has the given size, or if the selection is homogeneously of the given size.
The selection is set to the given point size.

Size...
Command: StdCmds.InitSizeDialog; StdCmds.OpenToolDialog('Std/Rsrc/Cmds', 'Size')
Guard: StdCmds.SizeGuard(-1)
Checked: if none of the other sizes apply
A tool dialog box is opened, which allows to enter a particular font size in points, and then to set the selection to this size.

Default Color
Command: StdCmds.Color(1000000H)
Guard: StdCmds.ColorGuard(1000000H)
Checked: if text to the left and to the right of the caret has the default color, or if the selection is homogeneously of the default color
Sets the selection's color to the default color.

Black
Command: StdCmds.Color(0000000H)
Guard: StdCmds.ColorGuard(0000000H)
Checked: if text to the left and to the right of the caret is black, or if the selection is homogeneously black
Sets the selection's color to black.

Red
Command: StdCmds.Color(00000FFH)
Guard: StdCmds.ColorGuard(00000FFH)
Checked: if text to the left and to the right of the caret is red, or if the selection is homogeneously red
Sets the selection's color to red.

Green
Command: StdCmds.Color(000AF00H)
Guard: StdCmds.ColorGuard(000AF00H)
Checked: if text to the left and to the right of the caret is green, or if the selection is homogeneously green
Sets the selection's color to green.

Blue
Command: StdCmds.Color(0FF0000H)
Guard: StdCmds.ColorGuard(0FF0000H)
Checked: if text to the left and to the right of the caret is blue, or if the selection is homogeneously blue
Sets the selection's color to blue.

Color...
Command: StdDialog.ColorDialog
Guard: StdCmds.ColorGuard(-1)
Checked: if none of the other colors apply
Asks the user for a color, to which it then sets the selection.

Default Font
Command: StdCmds.DefaultFont
Guard: StdCmds.DefaultFontGuard
Sets the selection to the default font.

Font...
Command: StdDialog.FontDialog
Guard: StdCmds.TypefaceGuard
Opens the standard font dialog box and applies the chosen font attributes to the selection.

Typeface...
Command: StdDialog.TypefaceDialog
Guard: StdCmds.TypefaceGuard
Opens the standard font dialog box and applies the chosen font attributes to the selection. In contrast to the Font... command, only the typeface (the name of the font) is changed, but not the other attributes like size or weight (bold/normal).

9.4 Window Menu (for MDI version)

New Window
Command: StdCmds.NewWindow
Guard: StdCmds.WindowGuard
Opens a new window on the same document as the front window. The window is of the same kind as the front window. The window's title is put between "<" and ">" parentheses.

Cascade
Command: MdiMenus.Cascade
Guard: StdCmds.WindowGuard
Arrange document windows in an overlapping fashion.

Tile Horizontal
Command: MdiMenus.TileHorizontal
Guard: StdCmds.WindowGuard
Arrange windows from left to right in a non-overlapping fashion. This command does not affect non-resizable windows, and it ignores some windows when there are too many open windows for a reasonable tiling. The front window becomes the left-most window.

Tile Vertical
Command: MdiMenus.TileVertical
Guard: StdCmds.WindowGuard
Arrange windows from top to bottom in a non-overlapping fashion. This command does not affect non-resizable windows, and it ignores some windows when there are too many open windows for a reasonable tiling. The front window becomes the top-most window.

Arrange Icons
Command: MdiMenus.ArrangeIcons
Guard: StdCmds.WindowGuard
Arrange icons (minimized windows) at the bottom of the application window.

{window}
Command: MdiMenus.WindowList
Guard:
Here the list of open windows is appended. The front window is checked.

10 Custom Commands

Not all commands are visible in the default configuration of the menus. The following is a list of modules from the Std subsystem. These modules contain many useful commands but not all of them appear in the menus. For more information about the commands in the modules, please consult the corresponding module documentation.

StdClocks	analog clock views
StdCmds	cmds of std menus
StdCoder	ASCII coder
StdDebug	minimal debugger
StdFolds	fold views
StdHeaders 	headers / footers
StdLinks	hyperlink views
StdLog	standard output
StdMenuTool	menu tool
StdStamps	date stamp views
StdTables 	table controls
StdTabViews 	tabbed folder views
StdViewSizer 	set size of a view