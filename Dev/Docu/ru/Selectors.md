DevSelectors

DEFINITION DevSelectors;

	IMPORT TextModels, Models, Stores, Views;

	CONST
		left = 1;
		middle = 2;
		right = 3;

	TYPE
		Directory = POINTER TO ABSTRACT RECORD 
			(d: Directory) New (position: INTEGER): Selector, NEW, ABSTRACT
		END;

		Selector = POINTER TO RECORD (Views.View)
			position-: INTEGER;
		END;

	VAR
		dir-: Directory;
		stdDir-: Directory;

	PROCEDURE Change (text: TextModels.Model; title: ARRAY OF CHAR; selection: INTEGER);
	PROCEDURE ChangeTo (text: TextModels.Model; title, entry: ARRAY OF CHAR);
	PROCEDURE DepositLeft;
	PROCEDURE DepositMiddle;
	PROCEDURE DepositRight;
	PROCEDURE SetDir (d: Directory);

END DevSelectors.


Introduction

Selector Views serve to specify conditional parts of a text document. The idea is that the document can be configured into one of two, or more, variants, and each variant may involve several changes spread throughout the document.

A simple example is to change the language of comments in a source file.

When any of the changes is selected anywhere in the Document, all the related changes are automatically made consistently.


Selector Views should be inserted into a Text in sets comprising a left View "", 0 or more middle Views "", and a right View "". These Views then identify short stretches of text. The first stretch is called the selector title and the subsequent stretches are called its selections.

For example:
	MyTitleSelection 1Selection 2Selection 3
	MyTitleValue 1Value 2Value 3

If you click on either the "" or "" Views the selector opens to display all its fields. If you click on one of the "" Views it collapses to show only the immediately following selection.

Normally one would have a group (or groups) of several selectors, each selector would have two or more middle Views, and all the selectors in the same group would have the same number of middle Views.

Different selectors are in the same group if they have the same (case-sensitive) title.

When you open or collapse any selector, all the selectors in the same group (ie with the same title) open or collapse together.
(An exception is when you click on the n'th middle View in a selector, other selectors in that group with less than n middle Views do not respond.)


Changes to selections are recorded on the Edit -> Undo/Redo stack, but do not make the Text dirty.


Selectors in the source code

Some suggested uses for Selectors are:

	→	During development when there is a desire to experiment with different options
	→	To insert comments in source code in different languages
	→	To translate 'c' header files by simulating conditional compilation. 

It can then be convenient to have control selectors in comments in the module header such as:

	(*  FeatureOnOff  *)
	(*  Commentenrufr  *)
	(*  Macro Idvalue 1 value 2value 3  *) 

and corresponding selectors in each group to implement the selected functionality.


The Compiler also responds directly to selectors so, for example, one can call at the command line

	DevCompiler.CompileThis MyModule ("Feature A": "On", "Feature B": "Off")

and the source file is configured appropriately if it includes selectors with matching titles and selections.


Installation

It may be convenient to include the Menu items:
	SEPARATOR
	"Insert Left"	"*F5"	"DevSelectors.DepositLeft; StdCmds.PasteView"	"StdCmds.PasteViewGuard"
	"Insert Middle"	"*F6"	"DevSelectors.DepositMiddle; StdCmds.PasteView"	"StdCmds.PasteViewGuard"
	"Insert Right"	"*F7"	"DevSelectors.DepositRight; StdCmds.PasteView"	"StdCmds.PasteViewGuard"

A suitable location would be after the "Fold" Menu items in the Tools section of (Dev)Menus.odc.


Reference Manual


CONST left, middle, right

Constants used by the Directory New procedure position parameter to define which kind of Selector will be returned.


TYPE Directory
ABSTRACT
Directory for selector Views.


PROCEDURE (d: Directory) New (position: INTEGER): Selector
NEW, ABSTRACT
Returns a new selector View.
position should be one of left, middle, or right (not explicitly checked).


TYPE Selector (Views.View)
View type of a selector.


position-: INTEGER
Indicates if the selector is a left, middle, or right selector.


VAR dir-, stdDir-: Directory
Directory and standard directory for selectors.


PROCEDURE Change (text: TextModels.Model; title: ARRAY OF CHAR; selection: INTEGER)
Collapses the selectors in text with title title to show the specified selection.
The first selection corresponds to number selection = 1.
(If the selector has less than selection selections it is unchanged.)
If selection = 0 all the selectors in this group opened.


PROCEDURE ChangeTo (text: TextModels.Model; title, entry: ARRAY OF CHAR)
If any selector in text has title = text, and a selection = entry then all the selectors with title text are collapsed to show only the corresponding selection.
Both comparisons are case sensitive.


PROCEDURE DepositLeft
Creates a new left selector and deposits it in the Views queue.


PROCEDURE DepositMiddle
Creates a new middle selector and deposits it in the Views queue.


PROCEDURE DepositRight
Creates a new right selector and deposits it in the Views queue.


PROCEDURE SetDir (d: Directory)
Sets the selector directory.

Pre
d # NIL	20
