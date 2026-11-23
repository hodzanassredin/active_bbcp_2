MODULE CheckLibrarian;

	(*
		Check if BlackBox can use another location for take sources and put results.
	*)

	IMPORT Files, TextModels, DevCommanders, DevCompiler, Librarian;

	PROCEDURE Compile*;
	VAR p: DevCommanders.Par;
	BEGIN
		Librarian.SetLib(Librarian.NewStdLibrarian(Files.dir.This("Check/Extra")));
		NEW(p);
		p.text := TextModels.dir.NewFromString(" TestLibrarianTest");
		p.beg := 0;
		p.end := p.text.Length();
		DevCommanders.par := p;
		DevCompiler.CompileThis;
		Librarian.SetLib(Librarian.stdLib)
	END Compile;

END CheckLibrarian.

CheckLibrarian.Compile

