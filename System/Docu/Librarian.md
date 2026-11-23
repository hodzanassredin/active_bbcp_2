Librarian

DEFINITION Librarian;

	IMPORT Files;

	CONST
		OK = 0;

	TYPE
		Librarian = POINTER TO ABSTRACT RECORD 
			res: INTEGER;
			(lib: Librarian) GetExtSpec (IN mod, cat: ARRAY OF CHAR;
				OUT loc: Files.Locator; OUT name: Files.Name), NEW, ABSTRACT;
			(lib: Librarian) GetIntSpec (IN mod, cat: ARRAY OF CHAR;
				OUT loc: Files.Locator; OUT name: Files.Name), NEW, ABSTRACT;
			(lib: Librarian) SplitName (IN name: ARRAY OF CHAR;
				VAR head, tail: ARRAY OF CHAR), NEW, ABSTRACT
		END;

	VAR
		lib-: Librarian;
		stdLib-: Librarian;

	PROCEDURE NewStdLibrarian (loc: Files.Locator): Librarian;
	PROCEDURE SetLib (librarian: Librarian);

END Librarian.


Module Librarian defines layer from module names to files specifications of modules, documentation, resources and artefatcs of compilation (code and symbol files).

CONST OK
This constant can be compared with Librarian.res variable to define is last GetSpec proceed correctly.


TYPE Librarian
Librarians providing access to file specifications (locator, file name and file type) based on subsystem and name of the file. The subsystem name and name of the file also can be provided from module bame by SplitName procedure.


PROCEDURE (lib: Librarian) SplitName* (IN name: ARRAY OF CHAR; VAR head, tail: ARRAY OF CHAR), NEW, ABSTRACT;
Return subsystem name head and fine name tail for the module name.


PROCEDURE (lib: Librarian) GetExtSpec* (IN mod, cat: ARRAY OF CHAR; OUT loc: Files.Locator; OUT name: Files.Name), NEW, ABSTRACT;
PROCEDURE (lib: Librarian) GetIntSpec* (IN mod, cat: ARRAY OF CHAR; OUT loc: Files.Locator; OUT name: Files.Name), NEW, ABSTRACT;
Returns locator loc and file name name for module of name mod with required resource categoty cat.
Standard librarian supports "Mod", "Code", "Sym", "Rsrc" for cat argument.

GetExtSpec procedure should be used for saving (extranalizing) and GetIntSpec for loading (internalizing) resource from disk.

Post:
	loc = NIL
		=> .res # 0, constituent not found, fname and ftype undefined
	loc # NIL
		.res = 0
		loc and name identify the file of the requested constituent


PROCEDURE NewStdLibrarian* (loc: Files.Locator): Librarian;
Returns a standard filesystem-based librarian based at loc. This direcrory will be used to obtain source files, documentation and resources and put results of compilation there.

Pre
	loc # NIL, 20
Post
	RESULT = NIL => impossible to create a standard librarian for the given location
	RESULT # NIL => standard librarian for location loc created successfully

