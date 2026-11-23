MODULE CheckSandBox;

	IMPORT
		Librarian, Kernel, Files, Strings;

	TYPE
		Sandbox = POINTER TO RECORD (Librarian.Librarian)
			from, to: Files.Locator
		END;

	(* SandBox Librarian *)

	PROCEDURE (lib: Sandbox) SplitName (IN name: ARRAY OF CHAR; VAR head, tail: ARRAY OF CHAR);
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

	PROCEDURE (lib: Sandbox) GetSpec (IN mod, cat: ARRAY OF CHAR; OUT loc: Files.Locator; OUT name: Files.Name);
		VAR head, tail: Kernel.Name;
		
		PROCEDURE StartsWith (IN str, patt: ARRAY OF CHAR): BOOLEAN;
			VAR i, k: INTEGER; res: BOOLEAN;
		BEGIN
			i := LEN(patt$); k := LEN(str$);
			IF k >= i THEN
				k := 0; 
				REPEAT res := patt[k] = str[k]; INC(k) UNTIL ~res OR (k = i);
			ELSE res := FALSE
			END;
			RETURN res
		END StartsWith;
		
	BEGIN
		lib.SplitName(mod, head, tail);
		
		IF (cat = "Code") THEN
			loc := lib.to.This(head);
			loc := loc.This("Code"); name := tail + ".ocf"
		
		ELSIF (cat = "Sym") THEN
			loc := lib.to.This(head);
			loc := loc.This("Sym"); name := tail + ".osf"
		
		ELSIF (cat = "Mod") THEN
			loc := lib.from.This(head);
			loc := loc.This("Mod"); name := tail + ".odc"
		
		ELSIF StartsWith(cat, "Docu") THEN
			loc := lib.from.This(head);
			loc := loc.This("Docu"); name := tail + ".odc"
		
		ELSIF StartsWith(cat, "Rsrc") THEN
			loc := lib.from.This(head);
			loc := loc.This("Rsrc"); name := tail + ".odc"
		
		ELSE
			HALT(20)
		END;
		lib.res := 0
	END GetSpec;

	PROCEDURE Set* (from, to: ARRAY 256 OF CHAR);
		VAR s: Sandbox;
	BEGIN
		NEW(s);
		s.from := Files.dir.This(from);
		s.to := Files.dir.This(to);
		Librarian.SetLib(s);
	END Set;

	PROCEDURE Reset*;
	BEGIN
		Librarian.SetLib(Librarian.stdLib);
	END Reset;

END CheckSandBox.

"CheckSandBox.Set('', 'SandBox')"

"CheckSandBox.Reset"

DevCompiler.CompileSubs Lin System Std Cons Text Form Dev Comm Obx Xhtml

DevLinker1.LinkElfExe Linux blackbox2 := Kernel$+ Utf LinKernel Files LinEnv LinFiles LinGui Librarian StdLoader LinLoader 