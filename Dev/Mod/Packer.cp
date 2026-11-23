MODULE DevPacker;
(**
	project	= "BlackBox 2.0, diffs vs 1.7.2 are marked by green"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Oberon microsystems, I.A. Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	(* WinPackedFiles and LinPackedFiles depend on the way files are packed here *)

	IMPORT
		Kernel, Services, Strings, Files, Dialog, Stores, Views, Librarian,
		TextModels, TextViews, TextMappers, StdLog, DevCommanders;

	CONST
		packTag = 12681268H; (* same as in HostPackedFiles *)
		chunksize = 65536;
		version = 1; (* same as in HostPackedFiles *)

	TYPE
		FileList = POINTER TO RECORD
			path, name: Files.Name;
			aliasPath, aliasName: Files.Name;
			adr, len: INTEGER;
			year, month, day, hour, minute, second: INTEGER;
			next: FileList
		END;

	PROCEDURE Diff (VAR a, b: ARRAY OF CHAR; caseSens: BOOLEAN): INTEGER;
		VAR i: INTEGER; cha, chb: CHAR;
	BEGIN
		i := 0;
		REPEAT
			cha := a[i]; chb := b[i]; INC(i);
			IF cha # chb THEN
				IF ~caseSens THEN
					cha := Strings.Upper(cha);
					chb := Strings.Upper(chb);
				END;
				IF cha = "\" THEN cha := "/" END;
				IF chb = "\" THEN chb := "/" END;
				IF cha # chb THEN RETURN ORD(cha) - ORD(chb) END
			END
		UNTIL cha = 0X;
		RETURN 0
	END Diff;

	PROCEDURE SplitName (IN name: Files.Name; OUT path, fname: Files.Name);
		VAR i, l, sp: INTEGER;
	BEGIN
		path := ""; fname := "";
		l := LEN(name$);
		i := 0; sp := -1;
		WHILE i < l DO
			path[i] := name[i];
			IF (name[i] = "\") OR (name[i] = "/") THEN sp := i END;
			INC(i)
		END;
		IF sp < 0 THEN
			fname := name; path := ""
		ELSE
			path[sp] := 0X;
			i := 0; INC(sp);
			WHILE name[sp] # 0X DO fname[i] := name[sp]; INC(sp); INC(i) END;
			fname[i] := 0X
		END
	END SplitName;

	PROCEDURE PackExe(files: FileList; exe: Files.File);
		VAR tableadr, tot, err, ok, i, dl: INTEGER; l: FileList;
			wr: Files.Writer; sw: Stores.Writer;
			bytes: ARRAY chunksize OF BYTE;
			loc: Files.Locator; f: Files.File; r: Files.Reader; shared: BOOLEAN;
			num: ARRAY 7 OF CHAR; 
			
			PROCEDURE GetModDate (loc: Files.Locator; name: Files.Name;
				VAR year, month, day, hour, minute, second: INTEGER);
				VAR res: INTEGER; list: Files.FileInfo;
			BEGIN
				list := Files.dir.FileList(loc);
				WHILE (list # NIL) & ~ Files.dir.SameFile(loc, name, loc, list.name) DO
					list := list.next
				END;
				IF list # NIL THEN
					year := list.modified.year; month := list.modified.month;
					day := list.modified.day; hour := list.modified.hour;
					minute := list.modified.minute; second := list.modified.second
				END
			END GetModDate;
			
	BEGIN
		tot := 0; err := 0; ok := 0;
		l := files; wr := exe.NewWriter(NIL);
		Dialog.ShowStatus("Packing...");
		WHILE l # NIL DO
			StdLog.String("Packing file: " + l.path + "/" + l.name); StdLog.Ln;
			INC(tot);
			loc := Files.dir.This(l.path); shared := FALSE;
			f := Files.dir.Old(loc, l.name, shared);
			IF f = NIL THEN shared := TRUE; f := Files.dir.Old(loc, l.name, shared) END;
			IF f # NIL THEN
				l.len := f.Length();
				l.adr := exe.Length();
				GetModDate(loc, l.name, l.year, l.month, l.day, l.hour, l.minute, l.second);
				r := f.NewReader(NIL); r.SetPos(0);
				wr.SetPos(l.adr);
				i := 0; dl := MIN(chunksize, l.len);
				WHILE dl > 0 DO
					r.ReadBytes(bytes, 0, dl);
					wr.WriteBytes(bytes, 0, dl); exe.Flush;
					INC(i); dl := MIN(chunksize, l.len - i * chunksize)
				END;
				IF ~shared THEN f.Close END;
				INC(ok);
				Kernel.Collect
			ELSE
				StdLog.String("Could not read file: " + l.path + '/' + l.name); StdLog.Ln; INC(err)
			END;
			l := l.next
		END;
		l := files; tableadr := exe.Length();
		sw.ConnectTo(exe); sw.SetPos(tableadr);
		sw.WriteInt(tot);
		WHILE l # NIL DO
			sw.WriteString(l.aliasPath); sw.WriteString(l.aliasName);
			sw.WriteInt(l.adr); sw.WriteInt(l.len);
			sw.WriteInt(l.year); sw.WriteInt(l.month); sw.WriteInt(l.day);
			sw.WriteInt(l.hour); sw.WriteInt(l.minute); sw.WriteInt(l.second);
			l := l.next
		END;
		sw.WriteInt(packTag);
		sw.WriteInt(version);
		sw.WriteInt(tableadr);
		IF err > 0 THEN
			Strings.IntToString(err, num);
			Dialog.ShowMsg("Pack failed for: " + num + " files.");
			Dialog.ShowStatus("failed")
		ELSE
			StdLog.String("Packed files: "); StdLog.Int(ok); StdLog.Ln;
			Dialog.ShowStatus("ok")
		END
	END PackExe;

	PROCEDURE RecAdd(path: Files.Name; VAR files: FileList; VAR tot: INTEGER);
		VAR loc: Files.Locator; fi: Files.FileInfo; li: Files.LocInfo; l: FileList;
	BEGIN
		loc := Files.dir.This(path); fi := Files.dir.FileList(loc);
		WHILE fi # NIL DO
			NEW(l); l.path := path$; l.name := fi.name;
			l.next := files; files := l; INC(tot);
			fi := fi.next
		END;
		li := Files.dir.LocList(loc);
		WHILE li # NIL DO
			IF path = "" THEN
				RecAdd(li.name, files, tot)
			ELSE
				RecAdd(path + '/' + li.name, files, tot)
			END;
			li := li.next
		END
	END RecAdd;

	PROCEDURE ListFromSub* (sdir: ARRAY OF CHAR);
		CONST colPerRow = 3;
		VAR tot, col: INTEGER; files: FileList; t: TextModels.Model;
			f: TextMappers.Formatter; tv: TextViews.View; name: Files.Name;
	BEGIN
		StdLog.String("Examining subdirectory: " + sdir); StdLog.Ln;
		files := NIL; tot := 0;
		RecAdd(sdir$, files, tot);
		IF files # NIL THEN
			t := TextModels.dir.New(); f.ConnectTo(t); f.SetPos(0);
			f.WriteView(DevCommanders.dir.New());
			IF Dialog.platform = Dialog.windows THEN
				f.WriteString(" DevPacker.PackThis exefilename.exe :="); f.WriteLn;
			ELSE
				f.WriteString(" DevPacker.PackThis exefilename :="); f.WriteLn;
			END;
			col := 0;
			WHILE files # NIL DO
				IF files.path # "" THEN
					name := '"' + files.path + '/'
				ELSE
					name := '"'
				END;
				name := name + files.name + '"';
				IF files.next # NIL THEN name := name + ' ' END;
				f.WriteString(name);
				INC(col);
				IF col >= colPerRow THEN (* To avoid long lines *)
					f.WriteLn; col := 0
				END;
				files := files.next
			END;
			f.WriteView(DevCommanders.dir.NewEnd()); f.WriteLn;
			tv := TextViews.dir.New(t);
			Views.OpenView(tv)
		END;
		StdLog.String("Found "); StdLog.Int(tot); StdLog.String(" files."); StdLog.Ln
	END ListFromSub;

	PROCEDURE ListLoadedModules*;
		VAR
			t: TextModels.Model; f: TextMappers.Formatter; tv: TextViews.View;
			path, name: Files.Name; m: Kernel.Module; modName: Kernel.Name;
	BEGIN
		t := TextModels.dir.New(); f.ConnectTo(t); f.SetPos(0);
		f.WriteView(DevCommanders.dir.New());
		IF Dialog.platform = Dialog.windows THEN
			f.WriteString(" DevPacker.PackThis exefilename.exe :="); f.WriteLn;
		ELSE
			f.WriteString(" DevPacker.PackThis exefilename :="); f.WriteLn;
		END;
		m := Kernel.modList;
		WHILE m # NIL DO
			Kernel.GetModName(m, modName);
			Librarian.lib.SplitName(modName, path, name);
			path := path + '/Code/';
			f.WriteString(path + name + '.ocf ');
			m := m.next
		END;
		f.WriteView(DevCommanders.dir.NewEnd()); f.WriteLn;
		tv := TextViews.dir.New(t);
		Views.OpenView(tv)
	END ListLoadedModules;

	PROCEDURE FilesOk(files: FileList; exeloc: Files.Locator; IN exefile: Files.Name): BOOLEAN;
		VAR l: FileList; loc: Files.Locator; fi: Files.FileInfo; ret: BOOLEAN;
			err, tot: INTEGER;
	BEGIN
		StdLog.String("Validating files..."); StdLog.Ln;
		IF files = NIL THEN StdLog.String("No files to pack."); StdLog.Ln; RETURN FALSE END;
		l := files; ret := TRUE; err := 0; tot := 0;
		WHILE l # NIL DO
			loc := Files.dir.This(l.path);
			IF loc.res = 0 THEN
				fi := Files.dir.FileList(loc);
				WHILE (fi # NIL) & (Diff(l.name, fi.name, FALSE) # 0) DO
					fi := fi.next
				END
			END;
			IF (loc.res # 0) OR (fi = NIL) THEN
				IF l.path # "" THEN StdLog.String(l.path + "/") END;
				StdLog.String(l.name);
				StdLog.String(" ...not found"); ret := FALSE; INC(err);
				StdLog.Ln
			ELSIF Files.dir.SameFile(exeloc, exefile, loc, fi.name) THEN
				StdLog.String("Cannot pack a file into itself. (" + l.name + " cannot be both the target exe and included in the list)");
				StdLog.Ln; ret := FALSE; INC(err)
			END;
			INC(tot); l := l.next
		END;
		IF err > 0 THEN
			StdLog.Int(err); StdLog.String(" files of");
			StdLog.Int(tot); StdLog.String(" not correct.");
			Dialog.ShowStatus("failed")
		ELSE
			StdLog.String("All"); StdLog.Int(tot); StdLog.String(" files found.")
		END;
		StdLog.Ln;
		RETURN ret
	END FilesOk;

	(* Parse the file list *)

	PROCEDURE GetCh (rd: TextModels.Reader);
	BEGIN
		REPEAT rd.Read UNTIL rd.char # TextModels.viewcode
	END GetCh;

	PROCEDURE RemoveWhiteSpaces (rd: TextModels.Reader; end: INTEGER);
	BEGIN
		WHILE ~rd.eot & (rd.Pos() <= end) & (rd.char <= 20X) DO GetCh(rd) END
	END RemoveWhiteSpaces;

	PROCEDURE FileName (rd: TextModels.Reader; end: INTEGER;
		OUT name: ARRAY OF CHAR; OUT ok: BOOLEAN
	);
		VAR i: INTEGER; dquote, squote: BOOLEAN;

		PROCEDURE ValidChar(ch: CHAR): BOOLEAN;
		BEGIN
			IF dquote THEN RETURN ch # '"'
			ELSIF squote THEN RETURN ch # "'"
			ELSE RETURN ch > 20X
			END
		END ValidChar;

	BEGIN
		ok := TRUE;
		RemoveWhiteSpaces(rd, end);
		i := 0; dquote := FALSE; squote := FALSE;
		IF rd.char = '"' THEN dquote := TRUE
		ELSIF rd.char = "'" THEN squote := TRUE
		END;
		IF dquote OR squote THEN GetCh(rd) END;
		WHILE (rd.Pos() <= end) & ValidChar(rd.char) DO
			name[i] := rd.char;
			INC(i);
			GetCh(rd)
		END;
		name[i] := 0X;
		IF dquote THEN ok := rd.char = '"'; rd.Read END;
		IF squote THEN ok := rd.char = "'"; rd.Read END
	END FileName;

	PROCEDURE GetNextFileName (rd: TextModels.Reader; end: INTEGER; VAR file: FileList; OUT ok: BOOLEAN);
		VAR name: Files.Name;
	BEGIN
		FileName(rd, end, name, ok);
		SplitName(name, file.path, file.name);
		RemoveWhiteSpaces(rd, end);
		IF ok & (rd.char = "=") THEN
			GetCh(rd);
			IF rd.char = ">" THEN
				GetCh(rd);
				FileName(rd, end, name, ok);
				IF name # "" THEN
					SplitName(name, file.aliasPath, file.aliasName)
				ELSE
					ok := FALSE
				END
			ELSE
				ok := FALSE
			END
		ELSE
			file.aliasPath := file.path; file.aliasName := file.name
		END
	END GetNextFileName;

	PROCEDURE ParseExe (rd: TextModels.Reader; end: INTEGER;
		OUT exepath, exefile: Files.Name; OUT ok: BOOLEAN
	);
		VAR name: Files.Name;
	BEGIN
		ok := FALSE;
		GetCh(rd);
		FileName(rd, end, name, ok);
		IF ok THEN
			SplitName(name, exepath, exefile);
			RemoveWhiteSpaces(rd, end);
			IF ok & (rd.char = ":") THEN
				GetCh(rd);
				IF rd.char = "=" THEN
					GetCh(rd);
					ok := TRUE
				END
			END
		END
	END ParseExe;

	PROCEDURE AlreadyPacked (f: Files.File): BOOLEAN;
		VAR rd: Stores.Reader; int: INTEGER;
	BEGIN
		rd.ConnectTo(f);
		rd.SetPos(f.Length() - 12);
		rd.ReadInt(int);
		rd.ConnectTo(NIL);
		RETURN int = packTag
	END AlreadyPacked;

	PROCEDURE PackThis*;
		VAR rd: TextModels.Reader; end, diff: INTEGER; exepath, exefile: Files.Name; ok: BOOLEAN;
			files, l, tf, last: FileList; f: Files.File; loc: Files.Locator;
	BEGIN
		StdLog.String("Sorting file list..."); StdLog.Ln;
		rd := DevCommanders.par.text.NewReader(NIL);
		rd.SetPos(DevCommanders.par.beg);
		end := DevCommanders.par.end;
		ParseExe(rd, end, exepath, exefile, ok);
		IF ~ok THEN
			StdLog.String("exe file not correctly specified"); StdLog.Ln
		ELSE
			files := NIL; NEW(l);
			GetNextFileName(rd, end, l, ok);
			WHILE ok & (l.name # "") DO
				IF files = NIL THEN
					files := l
				ELSE
					tf := files; last := NIL;
					diff := Diff(l.aliasPath, tf.aliasPath, FALSE);
					WHILE (tf # NIL) & (diff < 0) DO
						last := tf; tf := tf.next;
						IF tf # NIL THEN diff := Diff(l.aliasPath, tf.aliasPath, FALSE) END
					END;
					IF (tf = NIL) OR (diff > 0) THEN
						IF last = NIL THEN
							l.next := files; files := l
						ELSE
							l.next := last.next; last.next := l
						END
					ELSE
						diff := Diff(l.aliasName, tf.aliasName, FALSE);
						WHILE (tf # NIL) & (diff < 0) & (Diff(l.aliasPath, tf.aliasPath, FALSE) = 0) DO
							last := tf; tf := tf.next;
							IF tf # NIL THEN diff := Diff(l.aliasName, tf.aliasName, FALSE) END
						END;
						IF (diff = 0) & (Diff(l.aliasPath, tf.aliasPath, FALSE) = 0) THEN
							StdLog.String("File " + l.path + "/" + l.name + " appears more than once in the list."); StdLog.Ln
						ELSE
							IF last = NIL THEN
								l.next := files; files := l
							ELSE
								l.next := last.next; last.next := l
							END
						END
					END
				END;
				NEW(l);
				GetNextFileName(rd, end, l, ok)
			END;
			loc := Files.dir.This(exepath$)
		END;
		IF ok & FilesOk(files, loc, exefile) THEN
			f := Files.dir.Old(loc, exefile$, Files.exclusive);
			IF f # NIL THEN
				IF ~AlreadyPacked(f) THEN
					StdLog.String("Packing files to exe..."); StdLog.Ln;
					PackExe(files, f);
					f.Flush; f.Close;
					StdLog.String("Done."); StdLog.Ln
				ELSE
					f.Flush; f.Close;
					IF exepath # "" THEN exepath := exepath + "/" END;
					StdLog.String(
						"Executable (" + exepath + exefile + ") already contains packed files. Link a new executable.");
					StdLog.Ln;
					Dialog.ShowMsg("failed")
				END
			ELSE
				IF exepath # "" THEN exepath := exepath + "/" END;
				StdLog.String("Could not open exe file: " + exepath + exefile); StdLog.Ln;
				Dialog.ShowStatus("failed")
			END
		ELSE
			StdLog.String("Packing canceled."); StdLog.Ln;
			Dialog.ShowStatus("failed")
		END
	END PackThis;

END DevPacker.