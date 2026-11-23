MODULE DevLibrarian;
(**
	project	= "BlackBox 2.0, new module"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, I.A. Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Librarian, Strings, Files;

	TYPE
		DevLibrarian = POINTER TO RECORD (Librarian.Librarian)
			from, to: Files.Locator;
			modDirectory: ARRAY 64 OF CHAR
		END;

	PROCEDURE (lib: DevLibrarian) SplitName (IN name: ARRAY OF CHAR; VAR head, tail: ARRAY OF CHAR);
		VAR i, j: INTEGER; ch, lch: CHAR;
	BEGIN
		i := 0; ch := name[0];
		IF ch # 0X THEN
			REPEAT
				head[i] := ch; lch := ch; INC(i); ch := name[i]
			UNTIL (ch = 0X) OR (ch = ".") OR Strings.IsUpper(ch) & ~Strings.IsUpper(lch);
			IF ch = "." THEN i := 0; ch := name[0] END;
			head[i] := 0X; j := 0;
			WHILE ch # 0X DO tail[j] := ch; INC(i); INC(j); ch := name[i] END;
			tail[j] := 0X;
			IF tail = "" THEN tail := head$; head := "System" END;
		ELSE
			head := "System"; tail := ""
		END
	END SplitName;

	PROCEDURE (lib: DevLibrarian) GetSpec (IN mod, cat: ARRAY OF CHAR; OUT loc: Files.Locator; OUT name: Files.Name);
		VAR head, tail: ARRAY 256 OF CHAR; fi: Files.FileInfo; modLoc: Files.Locator;
		
		PROCEDURE FileExists (filesInfoList: Files.FileInfo; IN name: ARRAY OF CHAR): BOOLEAN;
			VAR fi: Files.FileInfo;
		BEGIN
			fi := filesInfoList;
			WHILE (fi # NIL) & (fi.name # name) DO fi := fi.next END;
			RETURN fi # NIL
		END FileExists;
	
	BEGIN
		lib.SplitName(mod, head, tail);
		
		IF cat = "Code" THEN
			loc := lib.to.This(head);
			loc := loc.This("Code"); name := tail + ".ocf"
		ELSIF cat = "Mod" THEN
			loc := lib.from.This(head);
			modLoc := loc.This(lib.modDirectory);
			fi := Files.dir.FileList(modLoc);
			IF FileExists(fi, tail + ".odc") THEN
				name := tail + ".odc";
				loc := modLoc;
			ELSIF FileExists(fi, tail + ".cp") THEN
				name := tail + ".cp";
				loc := modLoc;
			ELSE
				loc := loc.This("Mod");
				fi := Files.dir.FileList(loc);
				IF FileExists(fi, tail + ".odc") THEN
					name := tail + ".odc"
				ELSIF FileExists(fi, tail + ".cp") THEN
					name := tail + ".cp"
				ELSE
					name := tail + ".odc"
				END
			END
		ELSIF cat = "Sym" THEN
			loc := lib.to.This(head);
			loc := loc.This("Sym"); name := tail + ".osf"
		ELSIF cat = "Docu" THEN
			loc := lib.from.This(head);
			loc := loc.This("Docu"); name := tail + ".odc"
		ELSIF cat = "Rsrc" THEN
			loc := lib.from.This(head);
			loc := loc.This("Rsrc"); name := tail + ".odc"
		ELSIF cat = "Root" THEN
			loc := lib.from; name := "";
		ELSE
			loc := loc.This(cat); name := tail + ".odc"
		END;
		lib.res := 0
	END GetSpec;

	PROCEDURE NewLibrarian* (from, to: Files.Locator; modDirectory: ARRAY OF CHAR);
		VAR librarian: DevLibrarian;
	BEGIN
		IF modDirectory # "" THEN
			NEW(librarian);
			librarian.from := from;
			librarian.to := to;
			librarian.modDirectory := modDirectory$;
			Librarian.SetLib(librarian)
		END
	END NewLibrarian;

	PROCEDURE EnableDevLib* (directory, filter: ARRAY OF CHAR);
	BEGIN
		NewLibrarian(Files.dir.This(""), Files.dir.This(directory), filter);
	END EnableDevLib;

	PROCEDURE DisableDevLib*;
	BEGIN
		Librarian.SetLib(Librarian.stdLib);
	END DisableDevLib;

END DevLibrarian.

