MODULE AitoolsExportBlackBox;
	
	(*
		"AitoolsExportBlackBox.To('/home/dia/oberonai')"
	*)

	IMPORT Views, Converters, Files, Dialog, Strings, Log;

	PROCEDURE CopyBytes (from: Files.Reader; to: Files.Writer; len: INTEGER);
		VAR buf: ARRAY 4096 OF BYTE; n: INTEGER;
	BEGIN
		ASSERT(from.Pos() + len <= from.Base().Length(), 20);
		ASSERT(len >= 0, 21);
		WHILE len > 0 DO
			n := SHORT(MIN(LEN(buf), len));
			from.ReadBytes(buf, 0, n);
			to.WriteBytes(buf, 0, n);
			DEC(len, n)
		END
	END CopyBytes;

	PROCEDURE CopyFile (fromLoc: Files.Locator; IN fromName: ARRAY OF CHAR; IN toPath: ARRAY OF CHAR; IN toName: ARRAY OF CHAR);
		VAR toLoc: Files.Locator; fromF, toF: Files.File; rd: Files.Reader; wr: Files.Writer;
			res: INTEGER; toNm: ARRAY 256 OF CHAR; exclusive: BOOLEAN;
	BEGIN
		toLoc := Files.dir.This(toPath);
		fromF := Files.dir.Old(fromLoc, fromName$, Files.exclusive);
		exclusive := fromF # NIL;
		IF fromF = NIL THEN
			fromF := Files.dir.Old(fromLoc, fromName$, Files.shared)
		END;
		IF toName # "" THEN
			toNm := toName$
		ELSE
			toNm := fromName$
		END;
		IF fromF = NIL THEN
			Log.String("Couldn't open file ..../" + fromName + ". Error = ");
			Log.Int(fromLoc.res);
			Log.Ln
		ELSE
			toF := Files.dir.New(toLoc, Files.dontAsk);
			IF toF = NIL THEN
				Log.String("Couldn't create file at loc " + toPath + ". Error = ");
				Log.Int(toLoc.res);
				Log.Ln
			ELSE
				rd := fromF.NewReader(NIL);
				wr := toF.NewWriter(NIL);
				CopyBytes(rd, wr, fromF.Length());
				toF.Register(toNm$, "", Files.dontAsk, res);
				IF res # 0 THEN
					Log.String("Couldn't register file " + toPath + "/" + toNm + ". Error = ");
					Log.Int(res);
					Log.Ln
				END
			END;
			IF exclusive THEN
				fromF.Close
			END
		END
	END CopyFile;

	PROCEDURE CopyFiles (loc: Files.Locator; destPath: ARRAY OF CHAR);
		VAR flist: Files.FileInfo; i: INTEGER; locl: Files.LocInfo;
	BEGIN
		IF Dialog.platform = Dialog.linux THEN
			FOR i := 0 TO LEN(destPath$) - 1 DO 
				IF destPath[i] = "\" THEN destPath[i] := "/" END
			END
		ELSIF Dialog.platform = Dialog.windows THEN
			FOR i := 0 TO LEN(destPath$) - 1 DO 
				IF destPath[i] = "/" THEN destPath[i] := "\" END
			END
		END;
		flist := Files.dir.FileList(loc);
		WHILE flist # NIL DO
			CopyFile(loc, flist.name, destPath, flist.name);
			flist := flist.next
		END;
		locl := Files.dir.LocList(loc);
		WHILE locl # NIL DO
			CopyFiles(loc.This(locl.name), destPath + "/" + locl.name);
			locl := locl.next
		END
	END CopyFiles;

	PROCEDURE CutOdc (VAR n: ARRAY OF CHAR): BOOLEAN;
		VAR res: BOOLEAN; len: INTEGER;
	BEGIN
		res := FALSE;
		len := LEN(n$);
		IF (n[len-4] = ".") & (n[len-3] = "o") & (n[len-2] = "d") & (n[len-1] = "c") THEN
			n[len - 4] := 0X;
			res := TRUE
		END;
		RETURN res
	END CutOdc;

	PROCEDURE ExportModules (path, name: ARRAY OF CHAR);
		VAR expConv: Converters.Converter;
		
		PROCEDURE Export (folder: ARRAY OF CHAR);
			VAR expName: Files.Name; v: Views.View; mods: Files.FileInfo;
				loc, expLoc: Files.Locator; conv: Converters.Converter; 
		BEGIN
			loc := Files.dir.This(name + "/" + folder);
			expLoc := Files.dir.This(path + "/" + name + "/" + folder);
			mods := Files.dir.FileList(loc);
			WHILE mods # NIL DO
				conv := NIL;
				v := Views.Old(Views.dontAsk, loc, mods.name, conv);
				IF v # NIL THEN
					expName := mods.name;
					IF CutOdc(expName) THEN
						Converters.Export(expLoc, expName + ".cp", expConv, v)
					END
				ELSE
					Log.String("error: can't open module " + name + "/" + folder + "/" + mods.name);
					Log.Ln
				END;
				mods := mods.next
			END
		END Export;
		
	BEGIN
		expConv := Converters.list;
		WHILE (expConv # NIL) & (expConv.exp # "StdTextConv.ExportUtf8") DO
			expConv := expConv.next
		END;
		IF expConv # NIL THEN
			Export("Mod");
			Export("Mod64");
		ELSE
			Log.String("error: no export converter"); Log.Ln;
		END
	END ExportModules;

	PROCEDURE ExportDocumentation (path, name: ARRAY OF CHAR);
		CONST folder = "Docu";
		VAR expName: Files.Name; v: Views.View; mods: Files.FileInfo;
			loc, expLoc: Files.Locator; conv, expConv: Converters.Converter; 
	BEGIN
		expConv := Converters.list;
		WHILE (expConv # NIL) & (expConv.exp # "StdTextConv.ExportUtf8") DO
			expConv := expConv.next
		END;
		IF expConv # NIL THEN
			IF name = "" THEN
				expLoc := Files.dir.This(path + "/" + folder);
				loc := Files.dir.This(folder);
			ELSE
				expLoc := Files.dir.This(path + "/" + name + "/" + folder);
				loc := Files.dir.This(name + "/" + folder);
			END;
			mods := Files.dir.FileList(loc);
			WHILE mods # NIL DO
				conv := NIL;
				v := Views.Old(Views.dontAsk, loc, mods.name, conv);
				IF v # NIL THEN
					expName := mods.name;
					IF CutOdc(expName) THEN
						Converters.Export(expLoc, expName + ".md", expConv, v)
					END
				ELSE
					Log.String("error: can't open module " + name + "/" + folder + "/" + mods.name);
					Log.Ln
				END;
				mods := mods.next
			END
		ELSE
			Log.String("error: no export converter"); Log.Ln
		END
	END ExportDocumentation;

	PROCEDURE To* (path: ARRAY OF CHAR);
		VAR subs: Files.LocInfo; 
	BEGIN
		subs := Files.dir.LocList(Files.dir.This(""));
		WHILE subs # NIL DO
			IF Strings.IsUpper(subs.name[0]) THEN
				Log.String(subs.name); Log.Ln;
				CopyFiles(Files.dir.This(subs.name + "/Code"), path + "/" + subs.name + "/Code");
				CopyFiles(Files.dir.This(subs.name + "/Sym"), path + "/" + subs.name + "/Sym");
				CopyFiles(Files.dir.This(subs.name + "/Rsrc"), path + "/" + subs.name + "/Rsrc");
				ExportModules(path, subs.name);
				ExportDocumentation(path, subs.name)
			END;
			ExportDocumentation(path, "");
			subs := subs.next
		END
	END To;

END AitoolsExportBlackBox.

