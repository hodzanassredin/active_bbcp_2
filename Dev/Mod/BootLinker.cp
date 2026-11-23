MODULE DevBootLinker;

(* Implementation Docu 

This is the linker tool for BlackBox on Linux. The code files on Windows and Linux are the same, but the linkers work differently. The linux linker is actually more of a packer. It simply packs object files together and prepends a boot loader written in C. When the application is started the boot loader reads the object files and gives the Kernel the control.

The linux linker does two important things. First, it packs the object files together to one executable thus, only one file is needed. Second, it guarantees that the order in which the object files are packed is the "import order". That is, all imports of any given object file appear before the object file itself in the boot image. (Which implies that the very first object file cannot have any imports at all.)

The file format for the boot image is:

	Executable	= BootLoader BootImg
	BootLoader	= ELFExecutable
	Bootimg 	= Header {ObjFile} 
	Header	= fileTag version nofMods kernelname mainname newRecFP newArrFP
	fileTag	= 3AX 4BX 5CX 6DX

ELFExecutable is a C program compiled in the ELF format and the ObjFile entries are exact copies of the object files passed to the linker.

The ELF executable is copied from the image saved in Dev/Rsrc/exe{OSName}.img.
Where {OSName} is the name of OS, for example "Linux".
*)
	
	IMPORT Kernel, (* LinLibc, *) Librarian, DevLnkBase, 
			Files, Dialog, Log, TextMappers, DevCommanders;
	
	CONST 
		NewRecFP = 00000048H;
		NewArrFP = 000004A8H;
		
		OFdir = "Code";	SYSdir = "System";
		OFtag = 6F4F4346H;
		
		fileTag = 3A4B5C6DH; version = 0; ext = "";
		bufsize = 10*1024;
		stdBootLoader = "Dev/Rsrc/exe.img";

	TYPE 
		Name = ARRAY 40 OF SHORTCHAR;
		Module = POINTER TO RECORD
			next: Module;
			name: Name;
			fileName: Files.Name;
			hs, ms, ds, cs, vs, ni, ma, ca, va: INTEGER;
			dll, intf: BOOLEAN;
			imp: POINTER TO ARRAY OF Module;
			data: POINTER TO ARRAY OF BYTE;
		END;

	VAR bootLoader: Files.Name;
	
	PROCEDURE Write4 (wr: Files.Writer; x: INTEGER);
	BEGIN
		wr.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		wr.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		wr.WriteByte(SHORT(SHORT(x MOD 256))); x := x DIV 256;
		wr.WriteByte(SHORT(SHORT(x MOD 256)))
	END Write4;
	
	PROCEDURE WriteName (wr: Files.Writer; s: ARRAY OF SHORTCHAR);
		VAR i: SHORTINT;
	BEGIN 
		i := 0;
		WHILE s[i] # 0X DO wr.WriteByte(SHORT(ORD(s[i]))); INC(i) END;
		wr.WriteByte(0)
	END WriteName;

	PROCEDURE MakeExecutable (IN fileName: Files.Name); 
	BEGIN
		DevLnkBase.Chmod(fileName, {0, 2, 3, 4, 5, 6, 7, 8}) (* rwxrwxr-x*)
	END MakeExecutable;

	PROCEDURE ThisObjFile (modname: ARRAY OF CHAR): Files.File;
		VAR f: Files.File; loc: Files.Locator; fname: Files.Name;
	BEGIN
		Librarian.lib.GetSpec(modname, "Code", loc, fname);
		f := Files.dir.Old(loc, fname, Files.shared);
		RETURN f
	END ThisObjFile;
	
	PROCEDURE WriteExe(IN fileName: Files.Name; modlist, kernel, main: Module; 
					nofMods: INTEGER; VAR error: BOOLEAN);
		VAR 
			m: Module; 
			exe, img, f: Files.File;
			wr: Files.Writer; 
			r: Files.Reader;
			head, tail: Files.Name; 
			b: ARRAY bufsize OF BYTE; 
			res, len: INTEGER; 
	BEGIN
		img := Files.dir.Old(Files.dir.This(""), bootLoader, Files.shared);
		IF img = NIL THEN
			Log.String("image file ("+ bootLoader +") not found");Log.Ln;
			error := TRUE;
			RETURN;
		END;
			
		exe := Files.dir.New(Files.dir.This(""), Files.dontAsk);
		IF exe # NIL THEN
			wr := exe.NewWriter(NIL); wr.SetPos(0);
			(* copy the exe image *)
			r := img.NewReader(r);
			r.SetPos(0);
			len := MIN(r.Base().Length(), bufsize);
			WHILE len > 0 DO
				r.ReadBytes(b, 0, len); 
				wr.WriteBytes(b, 0, len);
				len := MIN(r.Base().Length() - r.Pos(), bufsize)
			END;
			img.Close;
			
			(* write header *)
			Write4(wr, fileTag); 
			Write4(wr, version); 
			Write4(wr, nofMods);
			WriteName(wr, kernel.name); 
			WriteName(wr, main.name);
			Write4(wr, NewRecFP); 
			Write4(wr, NewArrFP);
				
			(* write object files *)
			m := modlist;
			WHILE (m # NIL) & ~error DO
				Log.String(m.fileName);Log.Ln;
				f := ThisObjFile(m.fileName);
				r := f.NewReader(r);
				r.SetPos(0);
				len := MIN(r.Base().Length(), bufsize);
				WHILE len > 0 DO
					r.ReadBytes(b, 0, len); 
					wr.WriteBytes(b, 0, len);
					len := MIN(r.Base().Length() - r.Pos(), bufsize)
				END;
				m := m.next
			END;
			IF ~error THEN 
				exe.Register(fileName, ext, Files.dontAsk, res);
				IF res = 0 THEN MakeExecutable(fileName) END
			END
		END;
		IF (exe = NIL) OR (res # 0) THEN
			Log.Int(res); Log.String(" - couldn't create a new file"); Log.Ln;
			error := TRUE
		END;
		Kernel.Cleanup
	END WriteExe;
		
	PROCEDURE ValidateModList(modlist: Module; VAR error: BOOLEAN);
		VAR mod: Module; f: Files.File; R: Files.Reader;
				x, i: INTEGER; 
		PROCEDURE Read4 (VAR x: INTEGER);
			VAR b: BYTE;
		BEGIN
			R.ReadByte(b); x := b MOD 256;
			R.ReadByte(b); x := x + 100H * (b MOD 256);
			R.ReadByte(b); x := x + 10000H * (b MOD 256);
			R.ReadByte(b); x := x + 1000000H * b
		END Read4;
		
		PROCEDURE RNum (VAR i: INTEGER);
			VAR b: BYTE; s, y: INTEGER;
		BEGIN
			s := 0; y := 0; R.ReadByte(b);
			WHILE b < 0 DO INC(y, ASH(b + 128, s)); INC(s, 7); R.ReadByte(b) END;
			i := ASH((b + 64) MOD 128 - 64, s) + y
		END RNum;
		PROCEDURE ReadName (VAR name: ARRAY OF SHORTCHAR);
			VAR i: INTEGER; b: BYTE;
		BEGIN i := 0;
			REPEAT
				R.ReadByte(b); name[i] := SHORT(CHR(b)); INC(i)
			UNTIL b = 0
		END ReadName;

	BEGIN
		mod := modlist;
		WHILE (mod # NIL) & ~error DO
			f := ThisObjFile(mod.fileName);
			IF f = NIL THEN
				Log.String("code file for: " + mod.name + "  not found");Log.Ln;
				error := TRUE
			ELSE
				R := f.NewReader(R);
				R.SetPos(0); Read4(x);
				IF x = OFtag THEN
					Read4(x); 	(* = 10 i386 *)
					Read4(mod.hs); Read4(mod.ms); Read4(mod.ds); Read4(mod.cs);
					Read4(mod.vs); RNum(mod.ni); 
					ReadName(mod.name); (* mod.name= mod.fileName? *)
				ELSE
					Log.String(mod.name$); Log.String(": wrong file type"); 
					Log.Ln;  
					error := TRUE
				END;
			END;
			(* TODO: validate the order of the imports. *)
			mod := mod.next
		END
	END ValidateModList;
		
	PROCEDURE BuildModList(S: TextMappers.Scanner; end: INTEGER; 
										OUT modlist, kernel, main: Module; 
										OUT nofMods: INTEGER; VAR error: BOOLEAN);
		VAR mod, last: Module;
	BEGIN 
		last := NIL; modlist := NIL; kernel := NIL; 
		main := NIL; 
		nofMods := 0;
		WHILE (S.Pos() <= end) & (S.type # TextMappers.eot) & ~error DO
			IF S.type = TextMappers.string THEN
				NEW(mod); 
				mod.fileName := S.string$; 
				Log.String(mod.fileName);Log.Ln;
				INC(nofMods);
				IF modlist = NIL THEN modlist := mod ELSE last.next := mod END;
				last := mod;
			ELSIF (S.type = TextMappers.char) & (S.char = "+") THEN
				kernel := mod;
			ELSIF (S.type = TextMappers.char) & (S.char = "$") THEN
				main := mod;
			ELSE
				Log.String("unknown symbols in list"); Log.Ln;
				error := TRUE;
			END;
			S.Scan;
		END
	END BuildModList;

	PROCEDURE Link*;
		VAR
			S: TextMappers.Scanner;
			name: Files.Name;
			end: INTEGER;
			error: BOOLEAN;
			nofMods: INTEGER;
			modList, kernel, main: Module;
	BEGIN
		error := FALSE;
		Dialog.ShowStatus("linking");
		IF DevCommanders.par = NIL THEN
			Log.String("unable to parse link command"); Log.Ln;
			RETURN
		END;
		S.ConnectTo(DevCommanders.par.text);
		S.SetPos(DevCommanders.par.beg);
		end := DevCommanders.par.end;
		DevCommanders.par := NIL;
		S.Scan;
		IF S.type = TextMappers.string THEN
			name := S.string$;
			S.Scan;
			IF (S.type = TextMappers.char) & (S.char = ".") THEN
				S.Scan;
				IF S.type = TextMappers.string THEN
					IF LEN(S.string$) > 0 THEN
						name := name + "." + S.string
					END;
					S.Scan
				END
			ELSE
				Files.dir.GetFileName(name, ext, name)
			END;
			IF (S.type = TextMappers.char) & (S.char = ":") THEN
				S.Scan;
				IF (S.type = TextMappers.char) & (S.char = "=") THEN
					S.Scan;
				ELSE
					error := TRUE;
					Log.String(" := missing"); Log.Ln
				END
			ELSE
				error := TRUE;
				Log.String(" := missing"); Log.Ln
			END;
		ELSE
			Log.String("no modules to link"); Log.Ln
		END;
		IF ~error THEN 
			BuildModList(S, end, modList, kernel, main, nofMods, error);
			IF modList = NIL THEN
				Log.String("no modules to link"); Log.Ln; error := TRUE
			ELSIF kernel = NIL THEN
				Log.String("no kernel given"); Log.Ln; error := TRUE
			ELSIF main = NIL THEN
				Log.String("no main module given"); Log.Ln; error := TRUE
			END;
		END;
		S.ConnectTo(NIL);

		IF ~error THEN ValidateModList(modList, error) END;
		IF ~error THEN WriteExe(name, modList, kernel, main, nofMods, error) END;

		IF error THEN
			Dialog.ShowStatus("failed")
		ELSE
			Dialog.ShowMsg("wrote: " + name); Dialog.ShowStatus("ok")
		END;
		modList := NIL; kernel := NIL; main := NIL;
	END Link;

	PROCEDURE SetLoaderName*(name: ARRAY OF CHAR);
	BEGIN
		bootLoader := name$;
	END SetLoaderName;

BEGIN 
	bootLoader:= stdBootLoader;
END DevBootLinker.
