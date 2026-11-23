DevReferences

DEFINITION DevReferences;

	IMPORT TextMappers, Files;

	PROCEDURE ResolveImportAlias (VAR mod: TextMappers.String; t: TextModels.Model);
	PROCEDURE ShowDocu;
	PROCEDURE ShowSource;
	PROCEDURE ShowText (module, ident: TextMappers.String; category: Files.Name);

END DevReferences.


This module provides two commands which, given the name of a module or of a qualified identifier, look up the corresponding documentation or source text.

Typical menu:

MENU
	"&Source"	""	"DevReferences.ShowSource"	"TextCmds.SelectionGuard"
	"&Documentation"	""	"DevReferences.ShowDocu"	"TextCmds.SelectionGuard"
END


PROCEDURE ResolveImportAlias (VAR mod: TextMappers.String; t: TextModels.Model)
On input mod is assumed to contain either the name of a Module, or a Qualident, where the Module referenced in mod is imported by another Module whose source code is in t.
For example, if t contains the line "IMPORT Ref := DevReferences;" mod may be "Ref" or "DevReferences".
On output, in this example, mod would be set to "DevReferences".

PROCEDURE ShowDocu
Guard: TextCmds.SelectionGuard
Looks up the documentation text of the module whose name is selected. If a qualified identifier is selected, i.e., "module.ident", the corresponding item is searched. It must be written in boldface and in a smaller than 14 point type. The document must be located in the Docu directory of the module's subsystem.
The selected module name or qualident may be either given in full, or by alias if an alias has been defined in the enclosing module's IMPORT statement.

PROCEDURE ShowSource
Guard: TextCmds.SelectionGuard
Looks up the source text of the module whose name is selected. If a qualified identifier is selected, i.e., "module.ident", the corresponding item is searched. It must be written in boldface and in a smaller than 14 point type. The document must be located in the Mod directory of the module's subsystem.
The selected module name or qualident may be either given in full, or by alias if an alias has been defined in the enclosing module's IMPORT statement.

PROCEDURE  ShowText (module, ident: TextMappers.String; category: Files.Name)
Used internally.
