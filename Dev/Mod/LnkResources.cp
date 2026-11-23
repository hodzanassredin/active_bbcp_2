MODULE DevLnkResources;

	IMPORT LB := DevLnkBase, Files, TextMappers;

	TYPE
		Bytes = POINTER TO ARRAY OF BYTE;
		Key = POINTER TO RECORD id: INTEGER; name: ARRAY 256 OF CHAR END;
		Node = POINTER TO RECORD
			next, child: Node; key: Key; bytes: Bytes; offset, nameOffset, dataOffset: INTEGER
		END;
		Entry = POINTER TO RECORD next: Entry; typ, name: Key; language: INTEGER; bytes: Bytes END;

	VAR root: Node; image: Bytes; length: INTEGER;

	PROCEDURE Clear*;
	BEGIN NEW(root); image := NIL; length := 0 END Clear;

	PROCEDURE Number (id: INTEGER): Key;
		VAR k: Key;
	BEGIN NEW(k); k.id := id; RETURN k END Number;

	PROCEDURE Named (IN name: ARRAY OF CHAR): Key;
		VAR k: Key;
	BEGIN NEW(k); k.id := -1; k.name := name$; RETURN k END Named;

	PROCEDURE Equal (a, b: Key): BOOLEAN;
	BEGIN RETURN (a.id = b.id) & ((a.id >= 0) OR (a.name = b.name)) END Equal;

	PROCEDURE Before (a, b: Key): BOOLEAN;
	BEGIN
		IF a.id < 0 THEN RETURN (b.id >= 0) OR (a.name < b.name)
		ELSE RETURN (b.id >= 0) & (a.id < b.id) END
	END Before;

	PROCEDURE Child (parent: Node; key: Key): Node;
		VAR p, prev, n: Node;
	BEGIN
		p := parent.child; prev := NIL;
		WHILE (p # NIL) & Before(p.key, key) DO prev := p; p := p.next END;
		IF (p # NIL) & Equal(p.key, key) THEN RETURN p END;
		NEW(n); n.key := key; n.next := p;
		IF prev = NIL THEN parent.child := n ELSE prev.next := n END;
		RETURN n
	END Child;

	PROCEDURE Add (typ, name: Key; language: INTEGER; bytes: Bytes);
		VAR p: Node;
	BEGIN
		p := Child(root, typ); p := Child(p, name); p := Child(p, Number(language));
		IF p.bytes # NIL THEN LB.Error("duplicate resource type/name/language") ELSE p.bytes := bytes END
	END Add;

	PROCEDURE Range (b: Bytes; at, count: INTEGER): BOOLEAN;
	BEGIN
		IF (at < 0) OR (count < 0) OR (LONG(at)+count > LEN(b)) THEN
			LB.Error("truncated or invalid resource file"); RETURN FALSE
		END;
		RETURN TRUE
	END Range;

	PROCEDURE U16 (b: Bytes; at: INTEGER): INTEGER;
	BEGIN IF Range(b, at, 2) THEN RETURN b[at] MOD 256 + 256*(b[at+1] MOD 256) ELSE RETURN 0 END END U16;

	PROCEDURE D32 (b: Bytes; at: INTEGER): INTEGER;
	BEGIN IF Range(b, at, 4) THEN RETURN LB.Get4(b^, at) ELSE RETURN 0 END END D32;

	PROCEDURE Slice (b: Bytes; at, count: INTEGER): Bytes;
		VAR x: Bytes; i: INTEGER;
	BEGIN
		IF ~Range(b, at, count) THEN RETURN NIL END;
		NEW(x, count); FOR i := 0 TO count-1 DO x[i] := b[at+i] END; RETURN x
	END Slice;

	PROCEDURE Put16 (b: Bytes; at, value: INTEGER);
	BEGIN b[at] := SHORT(SHORT(value)); b[at+1] := SHORT(SHORT(value DIV 256)) END Put16;

	PROCEDURE ReadKey (b: Bytes; VAR at: INTEGER; limit: INTEGER): Key;
		VAR k: Key; n, i: INTEGER;
	BEGIN
		NEW(k); k.id := -1; i := 0;
		IF at+2 > limit THEN LB.Error("invalid resource identifier"); RETURN k END;
		n := U16(b, at); INC(at, 2);
		IF n = 65535 THEN
			IF at+2 > limit THEN LB.Error("invalid resource ordinal"); RETURN k END;
			k.id := U16(b, at); INC(at, 2)
		ELSE
			WHILE (n # 0) & ~LB.error DO
				IF (i >= LEN(k.name)-1) OR (at+2 > limit) THEN LB.Error("invalid resource name"); RETURN k END;
				k.name[i] := CHR(n); INC(i); n := U16(b, at); INC(at, 2)
			END;
			k.name[i] := 0X
		END;
		RETURN k
	END ReadKey;

	PROCEDURE ReadRes (b: Bytes; id, language: INTEGER);
		VAR at, size, header, p, finish, count: INTEGER; e, first, last: Entry;
	BEGIN
		at := 0; count := 0; first := NIL; last := NIL;
		WHILE (at < LEN(b)) & ~LB.error DO
			IF ~Range(b, at, 8) THEN RETURN END;
			size := D32(b, at); header := D32(b, at+4);
			IF (header < 24) OR ~Range(b, at, header) THEN LB.Error("invalid .res header"); RETURN END;
			finish := at+header; NEW(e); p := at+8;
			e.typ := ReadKey(b, p, finish); e.name := ReadKey(b, p, finish);
			p := LB.Aligned(p, 4);
			IF (LONG(p)+16 > finish) OR ~Range(b, finish, size) THEN LB.Error("invalid .res record"); RETURN END;
			e.language := U16(b, p+6);
			IF language >= 0 THEN e.language := language END;
			IF (size > 0) OR (e.typ.id # 0) OR (e.name.id # 0) THEN
				e.bytes := Slice(b, finish, size); INC(count);
				IF last = NIL THEN first := e ELSE last.next := e END; last := e
			END;
			at := LB.Aligned(finish+size, 4)
		END;
		IF count = 0 THEN LB.Error("empty .res file"); RETURN END;
		(* Legacy DevLinker replaces the ID of a single-resource .res file. *)
		IF count = 1 THEN first.name := Number(id) END;
		e := first;
		WHILE (e # NIL) & ~LB.error DO Add(e.typ, e.name, e.language, e.bytes); e := e.next END
	END ReadRes;

	PROCEDURE ImageId (typ: INTEGER): INTEGER;
		VAR p: Node; id: INTEGER; used: BOOLEAN;
	BEGIN
		id := 1;
		REPEAT
			used := FALSE; p := root.child;
			WHILE p # NIL DO
				IF p.key.id = typ THEN
					p := p.child;
					WHILE p # NIL DO IF p.key.id = id THEN used := TRUE END; p := p.next END
				ELSE p := p.next END
			END;
			IF used THEN INC(id) END
		UNTIL ~used OR (id > 65535);
		IF id > 65535 THEN LB.Error("too many icon/cursor images") END;
		RETURN id
	END ImageId;

	PROCEDURE ReadIcon (b: Bytes; id, language: INTEGER; cursor: BOOLEAN);
		VAR count, i, j, p, q, size, offset, imageId, typ, groupType, width, height: INTEGER;
			group, bytes, data: Bytes;
	BEGIN
		count := U16(b, 4);
		IF (count = 0) OR ~Range(b, 6, 16*count) THEN LB.Error("invalid icon/cursor directory"); RETURN END;
		IF cursor THEN typ := 1; groupType := 12 ELSE typ := 3; groupType := 14 END;
		NEW(group, 6+14*count); Put16(group, 2, U16(b, 2)); Put16(group, 4, count);
		FOR i := 0 TO count-1 DO
			p := 6+16*i; q := 6+14*i; size := D32(b, p+8); offset := D32(b, p+12);
			IF (size <= 0) OR (offset < 6+16*count) THEN LB.Error("invalid icon/cursor image"); RETURN END;
			bytes := Slice(b, offset, size); IF LB.error THEN RETURN END;
			imageId := ImageId(typ);
			IF cursor THEN
				width := b[p] MOD 256; IF width = 0 THEN width := 256 END;
				height := b[p+1] MOD 256; IF height = 0 THEN height := 256 END;
				Put16(group, q, width); Put16(group, q+2, height*2); Put16(group, q+4, 1);
				IF (size >= 16) & (D32(bytes, 0) >= 40) THEN Put16(group, q+6, U16(bytes, 14))
				ELSE Put16(group, q+6, 1) END;
				NEW(data, size+4); FOR j := 0 TO 3 DO data[j] := b[p+4+j] END;
				FOR j := 0 TO size-1 DO data[j+4] := bytes[j] END; bytes := data; INC(size, 4)
			ELSE FOR j := 0 TO 7 DO group[q+j] := b[p+j] END
			END;
			LB.Put4(group^, q+8, size); Put16(group, q+12, imageId);
			Add(Number(typ), Number(imageId), language, bytes)
		END;
		Add(Number(groupType), Number(id), language, group)
	END ReadIcon;

	PROCEDURE ReadFile (path: Files.Name; id, language: INTEGER);
		VAR file: Files.File; loc: Files.Locator; reader: Files.Reader; b: Bytes;
			i, split, j, size, lang: INTEGER; name, dir, ext: Files.Name;
	BEGIN
		file := Files.dir.Old(Files.dir.This("Rsrc"), path, TRUE);
		IF file = NIL THEN file := Files.dir.Old(Files.dir.This("Win/Rsrc"), path, TRUE) END;
		IF file = NIL THEN
			i := 0; split := -1;
			WHILE path[i] # 0X DO IF (path[i] = "/") OR (path[i] = "\") THEN split := i END; INC(i) END;
			i := 0; WHILE i <= split DO dir[i] := path[i]; INC(i) END; dir[i] := 0X;
			j := 0; WHILE path[i] # 0X DO name[j] := path[i]; INC(i); INC(j) END; name[j] := 0X;
			loc := Files.dir.This(dir); file := Files.dir.Old(loc, name, TRUE)
		END;
		IF file = NIL THEN LB.Error("resource file not found: "+path); RETURN END;
		size := file.Length(); NEW(b, size); reader := file.NewReader(NIL); reader.ReadBytes(b^, 0, size);
		IF reader.eof THEN LB.Error("truncated resource file") END; file.Close;
		IF LB.error THEN RETURN END;
		i := LEN(path$)-1; WHILE (i >= 0) & (path[i] # ".") DO DEC(i) END;
		j := 0; INC(i);
		WHILE path[i] # 0X DO ext[j] := CAP(path[i]); INC(i); INC(j) END; ext[j] := 0X;
		lang := language; IF lang < 0 THEN lang := 1033 END;
		IF ext = "RES" THEN ReadRes(b, id, language)
		ELSIF ext = "TLB" THEN Add(Named("TYPELIB"), Number(id), lang, b)
		ELSIF (ext = "MANIFEST") OR (ext = "XML") THEN Add(Number(24), Number(id), lang, b)
		ELSIF ext = "BMP" THEN
			IF (size < 26) OR (U16(b, 0) # 4D42H) OR (D32(b, 2) # size) THEN LB.Error("invalid bitmap resource")
			ELSE Add(Number(2), Number(id), lang, Slice(b, 14, size-14)) END
		ELSIF (ext = "ICO") OR (ext = "CUR") THEN
			IF (size < 6) OR (U16(b, 0) # 0) OR
				((ext = "ICO") & (U16(b, 2) # 1)) OR ((ext = "CUR") & (U16(b, 2) # 2)) THEN LB.Error("invalid icon/cursor resource")
			ELSE ReadIcon(b, id, lang, ext = "CUR") END
		ELSE LB.Error("unsupported resource file: "+path)
		END
	END ReadFile;

	PROCEDURE Scan* (VAR s: TextMappers.Scanner; end: INTEGER);
		VAR id, language: INTEGER; path: Files.Name;
	BEGIN
		WHILE (s.start < end) & (s.type = TextMappers.int) & ~LB.error DO
			IF ~LB.outPe THEN LB.Error("Windows resources require PE output"); RETURN END;
			id := s.int;
			IF (id < 0) OR (id > 65535) THEN LB.Error("resource ID outside 0..65535"); RETURN END;
			s.Scan; language := -1;
			IF (s.type = TextMappers.char) & (s.char = "[") THEN
				s.Scan;
				IF (s.type # TextMappers.int) OR (s.int < 0) OR (s.int > 65535) THEN LB.Error("resource language expected"); RETURN END;
				language := s.int; s.Scan;
				IF (s.type # TextMappers.char) OR (s.char # "]") THEN LB.Error("missing resource language ]"); RETURN END;
				s.Scan
			END;
			IF s.type # TextMappers.string THEN LB.Error("resource filename expected"); RETURN END;
			path := s.string$; s.Scan;
			IF (s.type = TextMappers.char) & (s.char = ".") THEN
				s.Scan; IF s.type # TextMappers.string THEN LB.Error("resource extension expected"); RETURN END;
				path := path$+"."+s.string$; s.Scan
			END;
			ReadFile(path, id, language)
		END
	END Scan;

	PROCEDURE Directories (node: Node; VAR position: INTEGER);
		VAR p: Node; count: INTEGER;
	BEGIN
		count := 0; p := node.child; WHILE p # NIL DO INC(count); p := p.next END;
		node.offset := position; INC(position, 16+8*count);
		p := node.child; WHILE p # NIL DO IF p.child # NIL THEN Directories(p, position) END; p := p.next END
	END Directories;

	PROCEDURE Names (node: Node; VAR position: INTEGER);
		VAR p: Node;
	BEGIN
		p := node.child;
		WHILE p # NIL DO
			IF p.key.id < 0 THEN p.nameOffset := position; INC(position, 2+2*LEN(p.key.name$)) END;
			IF p.child # NIL THEN Names(p, position) END; p := p.next
		END
	END Names;

	PROCEDURE Leaves (node: Node; VAR position: INTEGER; payload: BOOLEAN);
		VAR p: Node;
	BEGIN
		p := node.child;
		WHILE p # NIL DO
			IF p.child # NIL THEN Leaves(p, position, payload)
			ELSIF payload THEN p.dataOffset := position; INC(position, LB.Aligned(LEN(p.bytes), 4))
			ELSE p.offset := position; INC(position, 16) END;
			p := p.next
		END
	END Leaves;

	PROCEDURE Size* (): INTEGER;
	BEGIN
		IF (root = NIL) OR (root.child = NIL) THEN RETURN 0 END;
		length := 0; Directories(root, length); Names(root, length); length := LB.Aligned(length, 4);
		Leaves(root, length, FALSE); Leaves(root, length, TRUE); RETURN length
	END Size;

	PROCEDURE Emit (node: Node; rva: INTEGER);
		VAR p: Node; at, named, numbered, i: INTEGER;
	BEGIN
		at := node.offset+16; p := node.child; named := 0; numbered := 0;
		WHILE p # NIL DO
			IF p.key.id < 0 THEN
				INC(named); LB.Put4(image^, at, MIN(INTEGER)+p.nameOffset);
				Put16(image, p.nameOffset, LEN(p.key.name$));
				FOR i := 0 TO LEN(p.key.name$)-1 DO Put16(image, p.nameOffset+2+2*i, ORD(p.key.name[i])) END
			ELSE INC(numbered); LB.Put4(image^, at, p.key.id) END;
			IF p.child # NIL THEN LB.Put4(image^, at+4, MIN(INTEGER)+p.offset); Emit(p, rva)
			ELSE
				LB.Put4(image^, at+4, p.offset); LB.Put4(image^, p.offset, rva+p.dataOffset);
				LB.Put4(image^, p.offset+4, LEN(p.bytes));
				FOR i := 0 TO LEN(p.bytes)-1 DO image[p.dataOffset+i] := p.bytes[i] END
			END;
			INC(at, 8); p := p.next
		END;
		Put16(image, node.offset+12, named); Put16(image, node.offset+14, numbered)
	END Emit;

	PROCEDURE Write* (out: Files.Writer; rva: INTEGER);
	BEGIN NEW(image, length); Emit(root, rva); out.WriteBytes(image^, 0, length); image := NIL END Write;

END DevLnkResources.