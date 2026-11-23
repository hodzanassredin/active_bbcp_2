MODULE PaketFiles;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "BlackBox Cross-Platform Team"
	contributors	= "Anton Shelin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT M := PaketModel, Converters, DevCommanders, DevCompiler, Dialog, Files, StdLog, Strings, TextMappers, TextModels, TextViews, Views;

	PROCEDURE OpenQuickStart* (selected: M.Package);
		VAR loc: Files.Locator; name: Files.Name; conv: Converters.Converter;
			v: Views.View;
	BEGIN
		loc := Files.dir.This(selected.data[M.nameTag] + "/Docu");
		name := "Quick-Start.odc"; conv := NIL; 	(* no defaults for Views.Old *)
		v := Views.Old(FALSE, loc, name, conv); 	(* ask user for a file and open it as a view *)
		IF (v # NIL) & (v IS TextViews.View) THEN		(* make sure it is a text view *)
			Views.OpenView(v)	(* open the text view in a window *)
		END
	END OpenQuickStart;

	PROCEDURE SetIsInstalled* (VAR p: M.Package);
		VAR loc: Files.LocInfo;
	BEGIN
		loc := Files.dir.LocList(Files.dir.This(""));
		WHILE (loc # NIL) & (loc.name # p.data[M.nameTag]) DO loc := loc.next END;
		p.isInstalled := loc # NIL
	END SetIsInstalled;

	PROCEDURE SetDependencies* (lst: M.PackageList; selected: M.Package);
		VAR loc: Files.Locator; conv: Converters.Converter;
			v: Views.View; t: TextModels.Model; sc: TextMappers.Scanner; fName: Files.Name;
			foundLine: BOOLEAN;
			dependency: M.Package;
	BEGIN
		StdLog.String("Getting dependencies from ");
		StdLog.String(selected.data[M.nameTag]);
		StdLog.String("/Docu/Quick-Start.odc");
		StdLog.Ln;
		loc := Files.dir.This(selected.data[M.nameTag] + "/Docu");
		conv := NIL;
		foundLine := FALSE;
		fName := "Quick-Start.odc";
		v := Views.Old(FALSE, loc, fName, conv);
		IF (v # NIL) & (v IS TextViews.View) THEN
			t := v(TextViews.View).ThisModel();
			sc.ConnectTo(t);
			sc.SetOpts(sc.opts + {TextMappers.returnCtrlChars});
			sc.SetPos(0);
			sc.Scan;

			WHILE (sc.type # TextMappers.eot) & ~foundLine DO
				IF (sc.type = TextMappers.string) & (sc.string = "Depends") THEN
					foundLine := TRUE;
				END;
				sc.Scan;
			END;
			IF foundLine THEN
				StdLog.String("Found dependency list: ");
				ASSERT((sc.type = TextMappers.char) & (sc.char = ':'));
				sc.Scan; (* expecting words separated by ,  or space*)
				WHILE (sc.type # TextMappers.line) DO
					IF (sc.type = TextMappers.string) THEN
						StdLog.String(sc.string); StdLog.Ln;
						dependency := M.FindPackageBy(lst, M.nameTag, sc.string);
						M.AddToList(selected.depends, dependency, M.nameTag);
					END;
					sc.Scan;
				END;
			ELSE
				StdLog.String("Not found dependency list.");
				StdLog.Ln;
			END;
		END;
	END SetDependencies;

	PROCEDURE ExecCompile (m: TextModels.Model; beg, end: INTEGER; isCompileSubs: BOOLEAN);
		VAR p: DevCommanders.Par;
	BEGIN
		NEW(p);
		p.text := m;
		p.beg := beg;
		p.end := end;
		DevCommanders.par := p;
		IF isCompileSubs THEN
			DevCompiler.CompileSubs
		ELSE
			DevCompiler.CompileThis
		END
	END ExecCompile;

	PROCEDURE IsCorrectPlatform (prefix: ARRAY OF CHAR): BOOLEAN;
	BEGIN
		Strings.ToLower(prefix, prefix);
		RETURN (LEN(prefix$) = 0) OR
		((prefix = "linux") & (Dialog.platform = Dialog.linux)) OR
		((prefix = "windows") & (Dialog.platform = Dialog.windows));
	END IsCorrectPlatform;

	PROCEDURE CompilePackage* (selected: M.Package);
		VAR loc: Files.Locator; conv: Converters.Converter;
			v: Views.View; t: TextModels.Model; sc: TextMappers.Scanner; fName: Files.Name;
			prefix: ARRAY 256 OF CHAR;
			beg, end: INTEGER;
			tmpView: Views.View;
			isCompileSubs: BOOLEAN;
	BEGIN
		StdLog.String("Trying to compile " + selected.data[M.nameTag]); StdLog.Ln;
		loc := Files.dir.This(selected.data[M.nameTag] + "/Docu");
		conv := NIL;
		beg := 0;
		prefix := "";
		isCompileSubs := FALSE;
		fName := "Compile-List.odc";
		v := Views.Old(FALSE, loc, fName, conv);
		IF (v # NIL) & (v IS TextViews.View) THEN
			t := v(TextViews.View).ThisModel();
			sc.ConnectTo(t);
			sc.SetOpts(sc.opts + {TextMappers.returnViews, TextMappers.returnQualIdents});
			sc.SetPos(0);
			sc.Scan;

			WHILE (sc.type # TextMappers.eot) DO
				IF sc.type = TextMappers.string THEN
					IF sc.string = "DevCompiler.CompileThis" THEN
						beg := sc.Pos();
						isCompileSubs := FALSE
					ELSIF sc.string = "DevCompiler.CompileSubs" THEN
						beg := sc.Pos();
						isCompileSubs := TRUE
					ELSIF beg = 0 THEN
						prefix := sc.string$;
					END;
				ELSIF (sc.type = TextMappers.view) THEN
					tmpView := sc.view;
					WITH tmpView: DevCommanders.EndView DO
						end := sc.Pos();

						IF IsCorrectPlatform(prefix) THEN
							StdLog.String("Compiling list");
							IF LEN(prefix$) # 0 THEN
								StdLog.String(" with prefix "); StdLog.Char('"'); StdLog.String(prefix); StdLog.Char('"');
							END;
							StdLog.Ln;
							ExecCompile(t, beg, end, isCompileSubs);
						ELSE
							StdLog.String("Ignoring compile list with prefix ");
							StdLog.Char('"'); StdLog.String(prefix); StdLog.Char('"');
							StdLog.Ln;
						END;

						prefix := "";
						beg := 0;
					ELSE
					END
				END;
				sc.Scan;
			END;
		ELSE
			StdLog.String("File not found ");
			StdLog.String(fName);
			StdLog.Ln;
		END;
	END CompilePackage;

END PaketFiles.


