MODULE CheckCompiler64;

	IMPORT Files, Librarian, DevLibrarian;

	PROCEDURE EnableDevLib* (directory: ARRAY OF CHAR);
	BEGIN
		DevLibrarian.NewLibrarian(Files.dir.This(""), Files.dir.This(directory), "Mod64");
	END EnableDevLib;

	PROCEDURE DisableDevLib*;
	BEGIN
		Librarian.SetLib(Librarian.stdLib);
	END DisableDevLib;

END CheckCompiler64.
