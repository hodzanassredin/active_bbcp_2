MODULE PaketReader;
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

	IMPORT m := PaketModel, PaketFiles, StdLog, Strings, TextMappers;

	PROCEDURE ReadPackages* (s: TextMappers.Scanner; to: INTEGER): m.PackageList;
		VAR pl, q: m.PackageList; p, dependency: m.Package; i,j: INTEGER;dep : ARRAY 64 OF CHAR;
	BEGIN
		i := 0;
		WHILE ((s.start < to) OR (to =  - 1)) & (s.type # TextMappers.eot) DO
			IF s.type = TextMappers.string THEN
				IF i = 0 THEN
					NEW(p)
				END;
				p.data[i] := s.string;
				IF i = m.nameTag THEN
					PaketFiles.SetIsInstalled(p);
					m.AddToListSorted(pl, p, m.nameTag)
				END;
				INC(i);
			ELSIF s.type = TextMappers.line THEN
				i := 0
			END;
			s.Scan
		END;
		q := pl;
		WHILE q # NIL DO
			IF q.package.isInstalled THEN 
				PaketFiles.SetDependencies(pl, q.package) 
			ELSE
				j := 0;
				FOR i:= 0 TO LEN(q.package.data[m.dependsTag]$) DO
					IF (q.package.data[m.dependsTag][i] = ' ') OR (i = LEN(q.package.data[m.dependsTag]$)) THEN
						IF j # 0 THEN
							dep[j] := 0X;
							dependency := m.FindPackageBy(pl, m.nameTag, dep$);
							IF dependency # NIL THEN
								m.AddToList(q.package.depends, dependency, m.nameTag);
							END;
						END;
						j := 0
					ELSE
						dep[j] := q.package.data[m.dependsTag][i];
						INC(j)
					END
				END;
			END;
			q := q.next;
		END;
		RETURN pl;
	END ReadPackages;

END PaketReader.


