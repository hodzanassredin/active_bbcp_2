MODULE AitoolsImportCp;

	(*
		"AitoolsImportCp.ConvertCpToOdc('Win/Mod64')"
		"AitoolsImportCp.ConvertCpToOdc('System/Mod64')"
		"AitoolsImportCp.ConvertCpToOdc('Mdi/Mod64')"
	*)

	IMPORT Views, Converters, Stores, TextViews, Files, Dialog, Strings, Log;

	PROCEDURE CutCp (VAR n: ARRAY OF CHAR): BOOLEAN;
		VAR res: BOOLEAN; len: INTEGER;
	BEGIN
		res := FALSE;
		len := LEN(n$);
		IF (n[len-3] = ".") & (n[len-2] = "c") & (n[len-1] = "p") THEN
			n[len - 3] := 0X;
			res := TRUE
		END;
		RETURN res
	END CutCp;

	PROCEDURE ConvertCpToOdc* (path: ARRAY OF CHAR);
		VAR expName, name: Files.Name;
			v: Views.View; mods: Files.FileInfo; store: Stores.Store;
			loc, expLoc: Files.Locator; conv, expConv, impConv: Converters.Converter; 
			len: INTEGER;
	BEGIN
		expConv := Converters.list;
		WHILE (expConv # NIL) & (expConv.exp # "StdDocuments.ExportDocument") DO
			expConv := expConv.next
		END;
		impConv := Converters.list;
		WHILE (impConv # NIL) & (impConv.imp # "StdTextConv.ImportUtf8") DO
			impConv := impConv.next
		END;
		IF (expConv # NIL) & (impConv # NIL) THEN
			expLoc := Files.dir.This(path);
			loc := Files.dir.This(path);
			mods := Files.dir.FileList(loc);
			WHILE mods # NIL DO
				name := mods.name;
				len := LEN(name$);
				IF (name[len-3] = ".") & (name[len-2] = "c") & (name[len-1] = "p") THEN
					name[len-3] := 0X;
					Converters.Import(loc, mods.name, impConv, store);
					WITH store: TextViews.View DO
						Converters.Export(expLoc, name, expConv, store);
					ELSE
						Log.String("error: not a text view"); Log.Ln
					END
				END;
				mods := mods.next
			END
		ELSE
			Log.String("error: converters not found"); Log.Ln
		END
	END ConvertCpToOdc;

	PROCEDURE AllCp*;
		VAR subs: Files.LocInfo; 
	BEGIN
		subs := Files.dir.LocList(Files.dir.This(""));
		WHILE subs # NIL DO
			IF Strings.IsUpper(subs.name[0]) THEN
				ConvertCpToOdc(subs.name + "/Mod64");
				ConvertCpToOdc(subs.name + "/Mod");
			END;
			subs := subs.next
		END
	END AllCp;

END AitoolsImportCp.

