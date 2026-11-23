MODULE WinFiles64;
(* Windows AMD64 Files64 backend: four 2 KiB buffers per file, lazy temporary
	backing files, shared read handles and transactional registration. *)

	IMPORT S := SYSTEM, Files := Files64, BaseFiles := Files, HostFiles := WinFiles, Kernel, A := WinApi;

	CONST pathLen* = 1024;
		new = 0; temporary = 1; shared = 2; exclusive = 3; closed = 4;
		nofbufs = 4; bufsize = 2 * 1024;

	TYPE
		FullName* = ARRAY 1024 OF CHAR;
		Path = FullName;
		Locator* = HostFiles.Locator;
		Buffer = POINTER TO RECORD
			dirty: BOOLEAN; org: LONGINT; len: INTEGER; data: ARRAY bufsize OF BYTE
		END;
		File = POINTER TO RECORD (Files.File)
			length, diskLength: LONGINT; state, swapper: INTEGER; bufs: ARRAY nofbufs OF Buffer;
			loc: Locator; handle: S.ADDRESS; name: Path
		END;
		Writer = POINTER TO RECORD (Files.Writer) file: File; pos: LONGINT END;
		Reader = POINTER TO RECORD (Files.Reader) file: File; pos: LONGINT END;
		Directory = POINTER TO RECORD (Files.Directory) END;
		Identifier = RECORD (Kernel.Identifier) name: Path END;
		Counter = RECORD (Kernel.Identifier) count: INTEGER END;
	VAR directory: Directory; appName*: ARRAY 256 OF CHAR; serial: INTEGER;
		errString: PROCEDURE (IN text: ARRAY OF CHAR); errLn: PROCEDURE;
		MessageBox*: PROCEDURE (title, text: ARRAY OF CHAR; buttons: SET): INTEGER;

	PROCEDURE Join (IN base, name: ARRAY OF CHAR; OUT path: Path): BOOLEAN;
		VAR i, j: INTEGER;
	BEGIN
		i := 0;
		IF (name[0] # "/") & (name[0] # "\") & ~((LEN(name$) > 1) & (name[1] = ":")) THEN
			WHILE base[i] # 0X DO
				IF i >= LEN(path)-2 THEN RETURN FALSE END; path[i] := base[i]; INC(i)
			END;
			IF (i > 0) & (path[i-1] # "/") & (path[i-1] # "\") THEN path[i] := "\"; INC(i) END
		END;
		j := 0;
		WHILE name[j] # 0X DO
			IF i >= LEN(path)-1 THEN RETURN FALSE END; path[i] := name[j]; INC(i); INC(j)
		END;
		path[i] := 0X; RETURN TRUE
	END Join;


	PROCEDURE Error (error: INTEGER): INTEGER;
	BEGIN
		CASE error OF
		| 0: RETURN 0
		| 87, 123, 206: RETURN 1
		| 2, 3: RETURN 2
		| 80, 183: RETURN 3
		| 19: RETURN 4
		| 29, 30, 31, 1117: RETURN 5
		| 5, 32, 33, 65: RETURN 6
		| 8, 14: RETURN 80
		| 4, 39, 112, 1450: RETURN 81
		ELSE RETURN -error
		END
	END Error;

	PROCEDURE PathError (error: INTEGER; IN path: Path): INTEGER;
		VAR attributes: INTEGER;
	BEGIN
		IF error = 5 THEN
			attributes := A.GetFileAttributesW(S.ADR(path));
			IF (attributes # -1) & (0 IN BITS(attributes)) & ~(4 IN BITS(attributes)) THEN RETURN 4 END
		END;
		RETURN Error(error)
	END PathError;

	PROCEDURE Protection (IN path: Path): INTEGER;
		VAR attributes: INTEGER;
	BEGIN
		attributes := A.GetFileAttributesW(S.ADR(path));
		IF (attributes # -1) & (0 IN BITS(attributes)) & ~(4 IN BITS(attributes)) THEN RETURN 4 END;
		RETURN 0
	END Protection;

	PROCEDURE Retry (res: INTEGER; IN path: Path; ask: BOOLEAN; OUT cancelled: BOOLEAN): BOOLEAN;
		VAR result: INTEGER; text: ARRAY 1200 OF CHAR;
			box: PROCEDURE (title, text: ARRAY OF CHAR; buttons: SET): INTEGER;
	BEGIN
		box := MessageBox; IF box = NIL THEN box := HostFiles.MessageBox END;
		cancelled := FALSE;
		IF ask & (box # NIL) & ((res = 4) OR (res = 6) OR (res = 81)) THEN
			text := "Cannot write file: " + path + ". Retry?";
			result := box("BlackBox file error", text, {0, 2, 4, 5});
			IF result = 4 THEN RETURN TRUE END;
			cancelled := TRUE
		END;
		RETURN FALSE
	END Retry;

	PROCEDURE Hex (value: INTEGER; OUT text: ARRAY OF CHAR);
		VAR i: INTEGER; digits: ARRAY 17 OF CHAR;
	BEGIN
		digits := "0123456789ABCDEF";
		FOR i := 7 TO 0 BY -1 DO text[i] := digits[value MOD 16]; value := value DIV 16 END;
		text[8] := 0X
	END Hex;

	PROCEDURE CreateStage (IN folder: Path; OUT path: Path; OUT handle: S.ADDRESS; flags: INTEGER): INTEGER;
		VAR pid, number: ARRAY 9 OF CHAR; name: ARRAY 40 OF CHAR; tries, error: INTEGER;
	BEGIN
		handle := 0; Hex(A.GetCurrentProcessId(), pid); tries := 0;
		REPEAT
			IF serial = MAX(INTEGER) THEN serial := MIN(INTEGER) ELSE INC(serial) END;
			Hex(serial, number); name := ".bbtmp-" + pid + "-" + number + ".tmp";
			IF ~Join(folder, name, path) THEN RETURN 1 END;
			handle := A.CreateFileW(path, {30, 31}, {}, NIL, 1, S.VAL(SET, flags), 0);
			IF handle # -1 THEN RETURN 0 END;
			error := A.GetLastError(); INC(tries)
		UNTIL ((error # 80) & (error # 183)) OR (tries = 1024);
		handle := 0; RETURN Error(error)
	END CreateStage;

	PROCEDURE Backing (f: File): INTEGER;
		VAR folder, path: Path; n: INTEGER;
	BEGIN
		IF f.handle # 0 THEN RETURN 0 END;
		(* Match the i386 lazy spill scheme; Windows removes the file on Close
			or process termination, including when the carrier is abandoned. *)
		n := A.GetTempPathW(LEN(folder), S.ADR(folder));
		IF n = 0 THEN RETURN Error(A.GetLastError()) END;
		IF n >= LEN(folder) THEN RETURN 1 END;
		RETURN CreateStage(folder, path, f.handle, 4000100H)
	END Backing;

	PROCEDURE FlushBuffer (f: File; b: Buffer): INTEGER;
		VAR res, pos, written, result: INTEGER;
	BEGIN
		IF (b = NIL) OR ~b.dirty THEN RETURN 0 END;
		res := Backing(f); IF res # 0 THEN RETURN res END;
		IF A.SetFilePointerEx(f.handle, b.org, 0, 0) = 0 THEN RETURN Error(A.GetLastError()) END;
		pos := 0;
		WHILE pos < b.len DO
			result := A.WriteFile(f.handle, S.ADR(b.data[pos]), b.len-pos, written, NIL);
			IF result = 0 THEN RETURN Error(A.GetLastError()) END;
			IF (written <= 0) OR (written > b.len-pos) THEN RETURN 5 END;
			INC(pos, written)
		END;
		f.diskLength := MAX(f.diskLength, b.org+b.len); b.dirty := FALSE; RETURN 0
	END FlushBuffer;

	PROCEDURE FlushBuffers (f: File): INTEGER;
		VAR i, res: INTEGER;
	BEGIN
		FOR i := 0 TO nofbufs-1 DO
			res := FlushBuffer(f, f.bufs[i]); IF res # 0 THEN RETURN res END
		END;
		IF (f.handle # 0) & (f.state # shared) THEN
			IF A.FlushFileBuffers(f.handle) = 0 THEN RETURN Error(A.GetLastError()) END
		END;
		RETURN 0
	END FlushBuffers;

	PROCEDURE GetBuffer (f: File; pos: LONGINT; OUT b: Buffer): INTEGER;
		VAR org: LONGINT; i, count, got, result, res: INTEGER;
	BEGIN
		org := pos - pos MOD bufsize; i := 0;
		WHILE (i < nofbufs) & (f.bufs[i] # NIL) DO
			IF f.bufs[i].org = org THEN b := f.bufs[i]; RETURN 0 END;
			INC(i)
		END;
		IF i < nofbufs THEN NEW(b); b.org := -1; f.bufs[i] := b
		ELSE
			f.swapper := (f.swapper+1) MOD nofbufs; b := f.bufs[f.swapper];
			res := FlushBuffer(f, b); IF res # 0 THEN RETURN res END;
			b.org := -1
		END;
		b.len := SHORT(MIN(bufsize, f.length-org)); count := SHORT(MIN(bufsize, MAX(0, f.diskLength-org)));
		IF count > 0 THEN
			IF A.SetFilePointerEx(f.handle, org, 0, 0) = 0 THEN RETURN Error(A.GetLastError()) END;
			i := 0;
			WHILE i < count DO
				result := A.ReadFile(f.handle, S.ADR(b.data[i]), count-i, got, NIL);
				IF result = 0 THEN RETURN Error(A.GetLastError()) END;
				IF (got <= 0) OR (got > count-i) THEN RETURN 5 END;
				INC(i, got)
			END
		END;
		b.org := org; b.dirty := FALSE; RETURN 0
	END GetBuffer;

	PROCEDURE CheckIO (f: File; res: INTEGER);
	BEGIN
		IF res # 0 THEN
			IF f.loc # NIL THEN f.loc.res := res END;
			IF errString # NIL THEN errString("WinFiles64 I/O error") END; IF errLn # NIL THEN errLn END
		END;
		ASSERT(res = 0, 100)
	END CheckIO;

	PROCEDURE CopyAll (f: File; handle: S.ADDRESS): INTEGER;
		VAR data: ARRAY 32768 OF BYTE; pos: LONGINT; count, got, written, offset, result: INTEGER;
	BEGIN
		IF f.length = 0 THEN RETURN 0 END;
		IF A.SetFilePointerEx(f.handle, 0, 0, 0) = 0 THEN RETURN Error(A.GetLastError()) END;
		pos := 0;
		WHILE pos < f.length DO
			count := SHORT(MIN(LEN(data), f.length-pos));
			result := A.ReadFile(f.handle, S.ADR(data), count, got, NIL);
			IF result = 0 THEN RETURN Error(A.GetLastError()) END;
			IF (got <= 0) OR (got > count) THEN RETURN 5 END;
			offset := 0;
			WHILE offset < got DO
				result := A.WriteFile(handle, S.ADR(data[offset]), got-offset, written, NIL);
				IF result = 0 THEN RETURN Error(A.GetLastError()) END;
				IF (written <= 0) OR (written > got-offset) THEN RETURN 5 END;
				INC(offset, written)
			END;
			INC(pos, got)
		END;
		RETURN 0
	END CopyAll;

	PROCEDURE (VAR id: Identifier) Identified (): BOOLEAN;
		VAR f: File;
	BEGIN
		f := id.obj(File);
		RETURN (f.state = shared) & (f.name # "") &
			(A.CompareStringOrdinal(S.ADR(f.name), -1, S.ADR(id.name), -1, 1) = 2)
	END Identified;

	PROCEDURE DetachReaders (IN path: Path): INTEGER;
		VAR id: Identifier; obj: ANYPTR; f, copy: File; n, res, result: INTEGER;
	BEGIN
		id.typ := S.TYP(File);
		n := A.GetFullPathNameW(S.ADR(path), LEN(id.name), S.ADR(id.name), 0);
		IF n = 0 THEN RETURN Error(A.GetLastError()) END;
		IF n >= LEN(id.name) THEN RETURN 1 END;
		LOOP
			obj := Kernel.ThisFinObj(id); IF obj = NIL THEN RETURN 0 END;
			f := obj(File); NEW(copy); copy.state := temporary;
			res := Backing(copy);
			IF res = 0 THEN res := CopyAll(f, copy.handle) END;
			IF res # 0 THEN
				IF copy.handle # 0 THEN result := A.CloseHandle(copy.handle); copy.handle := 0 END;
				copy.state := closed; RETURN res
			END;
			(* Keep existing readers on the old contents, including uncached blocks.
				Do not release the original handle until the private copy is complete. *)
			result := A.CloseHandle(f.handle);
			f.handle := copy.handle; copy.handle := 0; copy.state := closed; f.name := "";
			IF result = 0 THEN RETURN Error(A.GetLastError()) END
		END
	END DetachReaders;

	PROCEDURE (f: File) Length (): LONGINT;
	BEGIN ASSERT(f.state # closed, 20); RETURN f.length END Length;

	PROCEDURE (f: File) NewReader (old: Files.Reader): Files.Reader;
		VAR r: Reader;
	BEGIN
		IF (old # NIL) & (old IS Reader) THEN r := old(Reader) ELSE NEW(r) END;
		ASSERT(f.state # closed, 20);
		IF r.file # f THEN r.file := f; r.pos := 0 END; r.eof := FALSE; RETURN r
	END NewReader;

	PROCEDURE (f: File) NewWriter (old: Files.Writer): Files.Writer;
		VAR w: Writer;
	BEGIN
		ASSERT(f.state # closed, 20); IF f.state = shared THEN RETURN NIL END;
		IF (old # NIL) & (old IS Writer) THEN w := old(Writer) ELSE NEW(w) END;
		IF w.file # f THEN w.file := f; w.pos := f.length END; RETURN w
	END NewWriter;

	PROCEDURE (w: Writer) Base (): Files.File;
	BEGIN RETURN w.file END Base;

	PROCEDURE (w: Writer) Pos (): LONGINT;
	BEGIN RETURN w.pos END Pos;

	PROCEDURE (w: Writer) SetPos (pos: LONGINT);
	BEGIN ASSERT(w.file.state # closed, 20); ASSERT((pos >= 0) & (pos <= w.file.length), 21); w.pos := pos END SetPos;

	PROCEDURE (w: Writer) WriteBytes (IN x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR b: Buffer; offset, count, res: INTEGER;
	BEGIN
		ASSERT(w.file.state # closed, 20);
		ASSERT((beg >= 0) & (len >= 0) & (beg <= LEN(x)-len), 21);
		ASSERT(w.pos <= MAX(LONGINT)-len, 21);
		WHILE len > 0 DO
			res := GetBuffer(w.file, w.pos, b); CheckIO(w.file, res);
			offset := SHORT(w.pos-b.org); count := MIN(len, bufsize-offset);
			S.MOVE(S.ADR(x[beg]), S.ADR(b.data[offset]), count);
			INC(w.pos, count); INC(beg, count); DEC(len, count);
			b.len := MAX(b.len, offset+count); b.dirty := TRUE;
			w.file.length := MAX(w.file.length, w.pos)
		END
	END WriteBytes;

	PROCEDURE (w: Writer) WriteByte (x: BYTE);
		VAR data: ARRAY 1 OF BYTE;
	BEGIN data[0] := x; w.WriteBytes(data, 0, 1) END WriteByte;

	PROCEDURE (f: File) Flush;
		VAR res: INTEGER;
	BEGIN
		ASSERT(f.state # closed, 20); res := FlushBuffers(f);
		IF f.loc # NIL THEN f.loc.res := res END; CheckIO(f, res)
	END Flush;

	PROCEDURE (f: File) Register (name: Files.Name; type: Files.Type; ask: BOOLEAN; OUT res: INTEGER);
		VAR filename: Files.Name; target, destination, folder, stage: Path; handle: S.ADDRESS;
			result, error, i, slash: INTEGER; cancelled, again: BOOLEAN;
	BEGIN
		ASSERT(f.state = new, 20); ASSERT(name # "", 21);
		directory.GetFileName(name, type, filename);
		IF ~Join(f.loc.path, filename, target) THEN res := 1; f.loc.res := res; RETURN END;
		result := A.GetFullPathNameW(S.ADR(target), LEN(destination), S.ADR(destination), 0);
		IF (result = 0) OR (result >= LEN(destination)) THEN
			IF result = 0 THEN res := Error(A.GetLastError()) ELSE res := 1 END;
			f.loc.res := res; RETURN
		END;
		folder := destination$; i := 0; slash := -1;
		WHILE folder[i] # 0X DO IF (folder[i] = "/") OR (folder[i] = "\") THEN slash := i END; INC(i) END;
		ASSERT(slash >= 0, 100); folder[slash+1] := 0X;
		res := FlushBuffers(f); IF res # 0 THEN f.loc.res := res; RETURN END;
		res := CreateStage(folder, stage, handle, 80H);
		IF res # 0 THEN f.loc.res := res; RETURN END;
		res := CopyAll(f, handle);
		IF (res = 0) & (A.FlushFileBuffers(handle) = 0) THEN res := Error(A.GetLastError()) END;
		result := A.CloseHandle(handle); IF (result = 0) & (res = 0) THEN res := Error(A.GetLastError()) END;
		IF res = 0 THEN
			REPEAT
				res := Protection(destination); result := 0;
				IF res = 0 THEN res := DetachReaders(destination) END;
				IF res = 0 THEN
					result := A.MoveFileExW(S.ADR(stage), S.ADR(destination), 9);
					IF result = 0 THEN error := A.GetLastError(); res := PathError(error, destination) END
				END;
				IF result # 0 THEN res := 0; again := FALSE
				ELSE
					again := Retry(res, destination, ask, cancelled); IF cancelled THEN res := 8 END
				END
			UNTIL ~again
		END;
		IF res = 0 THEN
			IF f.handle # 0 THEN result := A.CloseHandle(f.handle); f.handle := 0 END;
			f.state := closed; FOR i := 0 TO nofbufs-1 DO f.bufs[i] := NIL END
		ELSE result := A.DeleteFileW(S.ADR(stage))
		END;
		f.loc.res := res
	END Register;

	PROCEDURE (f: File) Close;
		VAR res, result, i: INTEGER;
	BEGIN
		IF f.state # closed THEN
			res := 0;
			IF f.state = exclusive THEN res := FlushBuffers(f) END;
			IF f.handle # 0 THEN
				result := A.CloseHandle(f.handle);
				IF (result = 0) & (res = 0) THEN res := Error(A.GetLastError()) END;
				f.handle := 0
			END;
			f.state := closed; FOR i := 0 TO nofbufs-1 DO f.bufs[i] := NIL END;
			IF (f.loc # NIL) & (res # 0) THEN f.loc.res := res END;
			ASSERT(res = 0, 100)
		END
	END Close;

	PROCEDURE (f: File) FINALIZE;
	BEGIN f.Close END FINALIZE;

	PROCEDURE (r: Reader) Base (): Files.File;
	BEGIN RETURN r.file END Base;

	PROCEDURE (r: Reader) Pos (): LONGINT;
	BEGIN RETURN r.pos END Pos;

	PROCEDURE (r: Reader) SetPos (pos: LONGINT);
	BEGIN ASSERT(r.file.state # closed, 20); ASSERT((pos >= 0) & (pos <= r.file.length), 21); r.pos := pos; r.eof := FALSE END SetPos;

	PROCEDURE (r: Reader) ReadByte (OUT x: BYTE);
		VAR b: Buffer; res: INTEGER;
	BEGIN
		ASSERT(r.file.state # closed, 20);
		IF r.pos < r.file.length THEN
			res := GetBuffer(r.file, r.pos, b); CheckIO(r.file, res);
			x := b.data[SHORT(r.pos-b.org)]; INC(r.pos); r.eof := FALSE
		ELSE x := 0; r.eof := TRUE END
	END ReadByte;

	PROCEDURE (r: Reader) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER);
		VAR b: Buffer; n, count, offset, res: INTEGER;
	BEGIN
		ASSERT(r.file.state # closed, 20);
		ASSERT((beg >= 0) & (len >= 0) & (beg <= LEN(x)-len), 21);
		n := SHORT(MIN(len, r.file.length-r.pos)); r.eof := n # len;
		WHILE n > 0 DO
			res := GetBuffer(r.file, r.pos, b); CheckIO(r.file, res);
			offset := SHORT(r.pos-b.org); count := MIN(n, b.len-offset);
			ASSERT(count > 0, 100); S.MOVE(S.ADR(b.data[offset]), S.ADR(x[beg]), count);
			INC(r.pos, count); INC(beg, count); DEC(n, count)
		END
	END ReadBytes;

	PROCEDURE (d: Directory) This (IN path: ARRAY OF CHAR): Files.Locator;
	BEGIN RETURN BaseFiles.dir.This(path) END This;

	PROCEDURE FileType (IN name: ARRAY OF CHAR; OUT type: Files.Type);
		VAR i, dot, n: INTEGER;
	BEGIN
		dot := -1; i := 0;
		WHILE name[i] # 0X DO IF name[i] = "." THEN dot := i END; INC(i) END;
		type := "";
		IF (dot >= 0) & (i-dot < LEN(type)) THEN
			n := 0; INC(dot);
			WHILE name[dot] # 0X DO type[n] := name[dot];
				IF (type[n] >= "A") & (type[n] <= "Z") THEN type[n] := CHR(ORD(type[n])+32) END;
				INC(n); INC(dot) END;
			type[n] := 0X
		END
	END FileType;

	PROCEDURE (d: Directory) Old (loc: Files.Locator; name: Files.Name; shrd: BOOLEAN): Files.File;
		VAR path: Path; type: Files.Type; file: File; handle: S.ADDRESS; size: LONGINT;
			result, access, share, res: INTEGER;
	BEGIN
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN NIL END;
		IF (loc(Locator).path = "") OR (name = "") OR ~Join(loc(Locator).path, name, path) THEN loc.res := 1; RETURN NIL END;
		IF ~shrd & (Protection(path) # 0) THEN loc.res := 4; RETURN NIL END;
		IF shrd THEN access := 80000000H; share := 1 ELSE access := 0C0000000H; share := 0 END;
		handle := A.CreateFileW(path, S.VAL(SET, access), S.VAL(SET, share), NIL, 3, {7}, 0);
		IF handle = -1 THEN loc.res := PathError(A.GetLastError(), path); RETURN NIL END;
		result := A.GetFileSizeEx(handle, size);
		IF result = 0 THEN res := Error(A.GetLastError())
		ELSIF size < 0 THEN res := 5
		ELSE res := 0
		END;
		IF res # 0 THEN result := A.CloseHandle(handle); loc.res := res; RETURN NIL END;
		NEW(file);
		result := A.GetFullPathNameW(S.ADR(path), LEN(file.name), S.ADR(file.name), 0);
		IF (result = 0) OR (result >= LEN(file.name)) THEN
			result := A.CloseHandle(handle); file.state := closed; loc.res := 1; RETURN NIL
		END;
		file.handle := handle; file.loc := loc(Locator); file.swapper := -1;
		file.length := size; file.diskLength := file.length;
		IF shrd THEN file.state := shared ELSE file.state := exclusive END;
		FileType(name, type); file.InitType(type); loc.res := 0; RETURN file
	END Old;

	PROCEDURE MakeDirectory (IN path: ARRAY OF CHAR): INTEGER;
		VAR parent: Path; i, n, res, attributes: INTEGER;
	BEGIN
		IF A.CreateDirectoryW(S.ADR(path), 0) # 0 THEN RETURN 0 END;
		res := A.GetLastError();
		IF res = 183 THEN
			attributes := A.GetFileAttributesW(S.ADR(path));
			IF (attributes # -1) & (4 IN BITS(attributes)) THEN RETURN 0 END
		ELSIF res = 3 THEN
			parent := path$; n := LEN(parent$);
			WHILE (n > 0) & ((parent[n-1] = "/") OR (parent[n-1] = "\")) DO DEC(n) END;
			i := n; WHILE (i > 0) & (parent[i-1] # "/") & (parent[i-1] # "\") DO DEC(i) END;
			IF (i > 0) & (i < n) THEN
				parent[i] := 0X; res := MakeDirectory(parent);
				IF res = 0 THEN
					IF A.CreateDirectoryW(S.ADR(path), 0) # 0 THEN RETURN 0 END;
					RETURN Error(A.GetLastError())
				END;
				RETURN res
			END
		END;
		RETURN Error(res)
	END MakeDirectory;

	PROCEDURE (d: Directory) New (loc: Files.Locator; ask: BOOLEAN): Files.File;
		VAR f: File; attributes, res, result: INTEGER; text: ARRAY 1200 OF CHAR;
			box: PROCEDURE (title, text: ARRAY OF CHAR; buttons: SET): INTEGER;
	BEGIN
		box := MessageBox; IF box = NIL THEN box := HostFiles.MessageBox END;
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN NIL END;
		IF (loc(Locator).path = "") THEN loc.res := 1; RETURN NIL END;
		attributes := A.GetFileAttributesW(S.ADR(loc(Locator).path));
		IF attributes = -1 THEN
			res := Error(A.GetLastError());
			IF (res = 2) & ((loc.res = 76) OR ~ask OR (box = NIL)) THEN
				res := MakeDirectory(loc(Locator).path)
			ELSIF (res = 2) & ask & (box # NIL) THEN
				text := "Create directory: " + loc(Locator).path + "?";
				result := box("BlackBox", text, {2, 5});
				IF result = 6 THEN res := MakeDirectory(loc(Locator).path) ELSE res := 8 END
			END
		ELSIF ~(4 IN BITS(attributes)) THEN res := 1
		ELSE res := 0
		END;
		loc.res := res; IF res # 0 THEN RETURN NIL END;
		NEW(f); f.swapper := -1; f.loc := loc(Locator); f.state := new; RETURN f
	END New;

	PROCEDURE (d: Directory) Temp (): Files.File;
		VAR f: File;
	BEGIN NEW(f); f.swapper := -1; f.state := temporary; f.InitType(""); RETURN f END Temp;

	PROCEDURE (d: Directory) Delete (loc: Files.Locator; name: Files.Name);
		VAR path: Path; attributes, result, res: INTEGER;
	BEGIN
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN END;
		IF (loc(Locator).path = "") OR (name = "") OR ~Join(loc(Locator).path, name, path) THEN loc.res := 1; RETURN END;
		IF Protection(path) # 0 THEN loc.res := 4; RETURN END;
		res := DetachReaders(path); IF res # 0 THEN loc.res := res; RETURN END;
		attributes := A.GetFileAttributesW(S.ADR(path));
		IF (attributes # -1) & (4 IN BITS(attributes)) THEN result := A.RemoveDirectoryW(S.ADR(path))
		ELSE result := A.DeleteFileW(S.ADR(path))
		END;
		IF result # 0 THEN loc.res := 0
		ELSE loc.res := PathError(A.GetLastError(), path)
		END
	END Delete;

	PROCEDURE (d: Directory) Rename (loc: Files.Locator; old, new: Files.Name; ask: BOOLEAN);
		VAR source, target: Path; res: INTEGER; again, cancelled: BOOLEAN;
	BEGIN
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN END;
		IF (loc(Locator).path = "") OR (old = "") OR (new = "") OR ~Join(loc(Locator).path, old, source)
			OR ~Join(loc(Locator).path, new, target) THEN loc.res := 1; RETURN END;
		REPEAT
			res := Protection(target);
			IF res = 0 THEN res := DetachReaders(source) END;
			IF res = 0 THEN res := DetachReaders(target) END;
			IF res = 0 THEN
				IF A.MoveFileExW(S.ADR(source), S.ADR(target), 9) # 0 THEN res := 0
				ELSE res := PathError(A.GetLastError(), target)
				END
			END;
			IF res = 0 THEN again := FALSE
			ELSE
				again := Retry(res, target, ask, cancelled);
				IF cancelled THEN res := 8 END
			END
		UNTIL ~again;
		loc.res := res
	END Rename;

	PROCEDURE (d: Directory) SameFile (loc0: Files.Locator; name0: Files.Name; loc1: Files.Locator; name1: Files.Name): BOOLEAN;
		VAR path0, path1, full0, full1: Path; n0, n1: INTEGER;
	BEGIN
		IF (loc0 = NIL) OR (loc1 = NIL) OR ~(loc0 IS Locator) OR ~(loc1 IS Locator) THEN RETURN FALSE END;
		IF (loc0(Locator).path = "") OR (loc1(Locator).path = "") OR ~Join(loc0(Locator).path, name0, path0)
			OR ~Join(loc1(Locator).path, name1, path1) THEN RETURN FALSE END;
		n0 := A.GetFullPathNameW(S.ADR(path0), LEN(full0), S.ADR(full0), 0);
		n1 := A.GetFullPathNameW(S.ADR(path1), LEN(full1), S.ADR(full1), 0);
		RETURN (n0 > 0) & (n0 < LEN(full0)) & (n1 > 0) & (n1 < LEN(full1)) &
			(A.CompareStringOrdinal(S.ADR(full0), -1, S.ADR(full1), -1, 1) = 2)
	END SameFile;

	PROCEDURE (d: Directory) FileList (loc: Files.Locator): Files.FileInfo;
		VAR path: Path; data: A.WIN32_FIND_DATAW; time: A.SYSTEMTIME;
			handle: S.ADDRESS; result: INTEGER; head, last, item: Files.FileInfo;
	BEGIN
		head := NIL; last := NIL;
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN NIL END;
		IF (loc(Locator).path = "") THEN loc.res := 1; RETURN NIL END;
		IF ~Join(loc(Locator).path, "*", path) THEN loc.res := 1; RETURN NIL END;
		handle := A.FindFirstFileW(S.ADR(path), data);
		IF handle = -1 THEN loc.res := 2; RETURN NIL END;
		REPEAT
			IF ~(4 IN data.dwFileAttributes) & (data.nFileSizeHigh >= 0) THEN
				NEW(item); item.name := data.cFileName$; item.length := LONG(data.nFileSizeHigh)*65536*65536 + LONG(data.nFileSizeLow);
				IF data.nFileSizeLow < 0 THEN INC(item.length, 100000000L) END;
				FileType(data.cFileName, item.type); item.attr := data.dwFileAttributes * {0..2};
				IF 5 IN data.dwFileAttributes THEN INCL(item.attr, Files.archive) END;
				result := A.FileTimeToSystemTime(data.ftLastWriteTime, time);
				IF result # 0 THEN
					item.modified.year := time.wYear; item.modified.month := time.wMonth; item.modified.day := time.wDay;
					item.modified.hour := time.wHour; item.modified.minute := time.wMinute; item.modified.second := time.wSecond
				END;
				IF last = NIL THEN head := item ELSE last.next := item END; last := item
			END;
			result := A.FindNextFileW(handle, data)
		UNTIL result = 0;
		result := A.GetLastError(); IF result = 18 THEN loc.res := 0 ELSE loc.res := 5 END;
		result := A.FindClose(handle); RETURN head
	END FileList;

	PROCEDURE (d: Directory) LocList (loc: Files.Locator): Files.LocInfo;
		VAR path: Path; data: A.WIN32_FIND_DATAW; handle: S.ADDRESS;
			result: INTEGER; head, last, item: Files.LocInfo;
	BEGIN
		head := NIL; last := NIL;
		IF (loc = NIL) OR ~(loc IS Locator) THEN RETURN NIL END;
		IF (loc(Locator).path = "") THEN loc.res := 1; RETURN NIL END;
		IF ~Join(loc(Locator).path, "*", path) THEN loc.res := 1; RETURN NIL END;
		handle := A.FindFirstFileW(S.ADR(path), data);
		IF handle = -1 THEN loc.res := 2; RETURN NIL END;
		REPEAT
			IF (4 IN data.dwFileAttributes) & (data.cFileName # ".") & (data.cFileName # "..") THEN
				NEW(item); item.name := data.cFileName$; item.attr := data.dwFileAttributes * {0..2};
				IF 5 IN data.dwFileAttributes THEN INCL(item.attr, Files.archive) END;
				IF last = NIL THEN head := item ELSE last.next := item END; last := item
			END;
			result := A.FindNextFileW(handle, data)
		UNTIL result = 0;
		result := A.GetLastError(); IF result = 18 THEN loc.res := 0 ELSE loc.res := 5 END;
		result := A.FindClose(handle); RETURN head
	END LocList;

	PROCEDURE (d: Directory) GetFileName (name: Files.Name; type: Files.Type; OUT filename: Files.Name);
		VAR i, j, dot: INTEGER;
	BEGIN
		filename := name$; i := 0; dot := -1;
		WHILE filename[i] # 0X DO
			IF (filename[i] = "/") OR (filename[i] = "\") THEN dot := -1
			ELSIF filename[i] = "." THEN dot := i
			END;
			INC(i)
		END;
		IF type # "" THEN
			IF dot < 0 THEN
				ASSERT(i < LEN(filename)-1, 20); filename[i] := "."; dot := i; INC(i)
			END;
			IF dot = i-1 THEN
				ASSERT(i+LEN(type$) < LEN(filename), 20); j := 0;
				WHILE type[j] # 0X DO filename[i] := type[j]; INC(i); INC(j) END; filename[i] := 0X
			END
		END
	END GetFileName;

	PROCEDURE (VAR c: Counter) Identified (): BOOLEAN;
	BEGIN IF c.obj(File).state # closed THEN INC(c.count) END; RETURN FALSE END Identified;

	PROCEDURE NofFiles* (): INTEGER;
		VAR c: Counter; ignored: ANYPTR;
	BEGIN c.typ := S.TYP(File); c.count := 0; ignored := Kernel.ThisFinObj(c); RETURN c.count END NofFiles;

	PROCEDURE SetErr* (string: PROCEDURE (IN text: ARRAY OF CHAR); ln: PROCEDURE);
	BEGIN errString := string; errLn := ln END SetErr;

	PROCEDURE GetModDate* (f: Files.File; VAR year, month, day, hour, minute, second: INTEGER);
		VAR stamp: A.FILETIME; time: A.SYSTEMTIME; result: INTEGER;
	BEGIN
		ASSERT(f IS File, 20); ASSERT(f(File).state # closed, 21);
		result := Backing(f(File)); CheckIO(f(File), result);
		result := A.GetFileTime(f(File).handle, NIL, NIL, stamp); IF result = 0 THEN CheckIO(f(File), Error(A.GetLastError())) END;
		result := A.FileTimeToSystemTime(stamp, time); ASSERT(result # 0, 100);
		year := time.wYear; month := time.wMonth; day := time.wDay;
		hour := time.wHour; minute := time.wMinute; second := time.wSecond
	END GetModDate;

	PROCEDURE Init;
	BEGIN
		NEW(directory);
		IF directory = NIL THEN Kernel.FatalError(1, "WinFiles64: cannot allocate directory object"); RETURN END;
		Files.SetDir(directory)
	END Init;
BEGIN Init
END WinFiles64.
