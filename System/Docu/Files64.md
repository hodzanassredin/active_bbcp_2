Files64

DEFINITION Files64;

	IMPORT Files;

	CONST
		exclusive = FALSE; shared = TRUE;
		dontAsk = FALSE; ask = TRUE;
		readOnly = 0; hidden = 1; system = 2; archive = 3; stationery = 4;

	TYPE
		Name = Files.Name;
		Type = Files.Type;

		FileInfo = POINTER TO RECORD 
			next: FileInfo;
			name: Files.Name;
			length: LONGINT;
			type: Files.Type;
			modified: RECORD 
				year, month, day, hour, minute, second: INTEGER
			END;
			attr: SET
		END;

		LocInfo = Files.LocInfo;

		Locator = Files.Locator;

		File = POINTER TO ABSTRACT RECORD 
			type-: Files.Type;
			(f: File) Length (): LONGINT, NEW, ABSTRACT;
			(f: File) NewReader (old: Reader): Reader, NEW, ABSTRACT;
			(f: File) NewWriter (old: Writer): Writer, NEW, ABSTRACT;
			(f: File) Flush, NEW, ABSTRACT;
			(f: File) Register (name: Files.Name; type: Files.Type; ask: BOOLEAN; OUT res: INTEGER),
					NEW, ABSTRACT;
			(f: File) Close, NEW, ABSTRACT;
			(f: File) InitType (type: Files.Type), NEW;
		END;

		Reader = POINTER TO ABSTRACT RECORD 
			eof: BOOLEAN;
			(r: Reader) Base (): File, NEW, ABSTRACT;
			(r: Reader) Pos (): LONGINT, NEW, ABSTRACT;
			(r: Reader) SetPos (pos: LONGINT), NEW, ABSTRACT
			(r: Reader) ReadByte (OUT x: BYTE), NEW, ABSTRACT;
			(r: Reader) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER), NEW, ABSTRACT;
		END;

		Writer = POINTER TO ABSTRACT RECORD 
			(w: Writer) Base (): File, NEW, ABSTRACT;
			(w: Writer) Pos (): LONGINT, NEW, ABSTRACT;
			(w: Writer) SetPos (pos: LONGINT), NEW, ABSTRACT;
			(w: Writer) WriteByte (x: BYTE), NEW, ABSTRACT;
			(w: Writer) WriteBytes (IN x: ARRAY OF BYTE; beg, len: INTEGER), NEW, ABSTRACT
		END;
		
		Directory = POINTER TO ABSTRACT RECORD 
			(d: Directory) This (IN path: ARRAY OF CHAR): Files.Locator, NEW, ABSTRACT
			(d: Directory) Temp (): File, NEW, ABSTRACT;
			(d: Directory) New (loc: Files.Locator; ask: BOOLEAN): File, NEW, ABSTRACT;
			(d: Directory) Old (loc: Files.Locator; name: Files.Name; shared: BOOLEAN): File,
					NEW, ABSTRACT;
			(d: Directory) Delete (loc: Files.Locator; name: Files.Name), NEW, ABSTRACT;
			(d: Directory) Rename (loc: Files.Locator; old, new: Files.Name; ask: BOOLEAN),
					NEW, ABSTRACT;
			(d: Directory) SameFile (loc0: Files.Locator; name0: Files.Name;
					loc1: Files.Locator; name1: Files.Name): BOOLEAN, NEW, ABSTRACT;
			(d: Directory) FileList (loc: Files.Locator): FileInfo, NEW, ABSTRACT;
			(d: Directory) LocList (loc: Files.Locator): Files.LocInfo, NEW, ABSTRACT;
			(d: Directory) GetFileName (name: Files.Name; type: Files.Type; OUT filename: Files.Name),
					NEW, ABSTRACT;
		END;

	VAR
		dir-, stdDir: Directory;
		docType-, objType-, symType-: Files.Type;

	PROCEDURE SetDir (d: Directory);

END Files64.

Module Files64 is identical to Files except that it uses the 64-bit LONGINT type instead of the 32-bit INTEGER type for the file length and positions within a file.

Note: This module may be removed in a later BlackBox release when 64-bit file length support is added to the module Files.