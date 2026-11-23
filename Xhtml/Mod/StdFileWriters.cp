MODULE XhtmlStdFileWriters;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT Files, Strings, Utf, XhtmlWriters, XhtmlEntitySets;

	CONST tab = 09X; line = 0AX;

	TYPE
		Writer = POINTER TO RECORD (XhtmlWriters.Writer)
			level: INTEGER;	(* indentation level *)
			current: Tag;	(* top of tag stack *)
			rider: Files.Writer;
			afterData: BOOLEAN	(* allows delayed newline after data contents *)
		END;

		Tag = POINTER TO RECORD
			up: Tag;
			inContent: BOOLEAN;	(* subtag was entered, or text data written *)
			preserve: BOOLEAN;	(* write complete element on one line, no prettyprinting white space *)
			name: ARRAY 256 OF CHAR
		END;

	(* generic output procedures *)

	PROCEDURE String (wr: Writer; IN str: ARRAY OF CHAR);
		VAR ch: CHAR; i, res: INTEGER; s1: ARRAY 2 OF CHAR; utf8Str: ARRAY 4 OF SHORTCHAR;
			j: INTEGER; u: SHORTCHAR;
	BEGIN
		i := 0; ch := str[0];
		WHILE ch # 0X DO
			IF ch <= 7FX THEN wr.rider.WriteByte(SHORT(SHORT(ORD(ch))))
			ELSE s1[0] := ch; s1[1] := 0X; Utf.StringToUtf8(s1, utf8Str, res); ASSERT(res = 0);
				j := 0; u := utf8Str[0];
				WHILE u # 0X DO wr.rider.WriteByte(SHORT(ORD(u))); INC(j); u := utf8Str[j] END
			END;
			INC(i); ch := str[i]
		END
	END String;

	PROCEDURE HtmlEncodedString (wr: Writer; IN str: ARRAY OF CHAR);
		VAR ch: CHAR; i: INTEGER; entityStr: ARRAY 10 OF CHAR;
	BEGIN
		i := 0; ch := str[0];
		WHILE ch # 0X DO
			XhtmlEntitySets.MapCharToEntity(ch, entityStr);
			String(wr, entityStr);
			INC(i); ch := str[i]
		END
	END HtmlEncodedString;

	PROCEDURE Tabs (wr: Writer);
		VAR i: INTEGER;
	BEGIN
		IF ~wr.current.preserve THEN
			i := 0; WHILE i # wr.level DO
				wr.rider.WriteByte(ORD(tab));
			INC(i) END
		END
	END Tabs;

	PROCEDURE StartElemContent (wr: Writer);
	BEGIN	(* complete start tag *)
		String(wr, ">"); wr.Ln;
		wr.current.inContent := TRUE
	END StartElemContent;


	(* methods *)

	PROCEDURE (wr: Writer) Error (): XhtmlWriters.Error;
	BEGIN
		RETURN NIL
	END Error;

	PROCEDURE (wr: Writer) Ln;
	BEGIN
		IF ~wr.current.preserve THEN
			wr.rider.WriteByte(ORD(line))
		END
	END Ln;

	PROCEDURE (wr: Writer) Instruction (IN piTarget, instruction: ARRAY OF CHAR);
	BEGIN
		Tabs(wr); String(wr, "<?" + piTarget + " " + instruction + "?>")
	END Instruction;

	PROCEDURE (wr: Writer) Comment (IN comment: ARRAY OF CHAR);
	BEGIN
		Tabs(wr); String(wr, "<!--" + comment + "-->")
	END Comment;

	PROCEDURE (wr: Writer) DocType (IN rootName, pubidLiteral, sysidLiteral: ARRAY OF CHAR);
	BEGIN
		ASSERT(wr.level = 0, 100);
		ASSERT(rootName # "", 101);
		ASSERT((pubidLiteral = "") OR (sysidLiteral # ""), 102);
		IF pubidLiteral # "" THEN
			String(wr, '<!DOCTYPE ' + rootName + ' PUBLIC "' + pubidLiteral + '" "' + sysidLiteral + '">')
		ELSIF sysidLiteral # "" THEN
			String(wr, '<!DOCTYPE ' + rootName + ' SYSTEM "' + sysidLiteral + '">')
		ELSE
			String(wr, '<!DOCTYPE ' + rootName)
		END;
		wr.Ln
	END DocType;

	PROCEDURE (wr: Writer) StartTag (IN elem: ARRAY OF CHAR; preserve: BOOLEAN);
		VAR t: Tag;
	BEGIN
		IF ~wr.current.inContent & (wr.current.up # NIL) THEN StartElemContent(wr) END;
		Tabs(wr); String(wr, "<" + elem); wr.afterData := FALSE;
		INC(wr.level);
		NEW(t); t.name := elem$; t.up := wr.current; t.preserve := preserve OR wr.current.preserve; wr.current := t
	END StartTag;

	PROCEDURE (wr: Writer) Attr (IN name, value: ARRAY OF CHAR);
	BEGIN
		ASSERT(wr.level >= 1, 100);
		String(wr, ' ' + name + '="');
		HtmlEncodedString(wr, value);
		String(wr, '"')
	END Attr;

	PROCEDURE (wr: Writer) Data (IN data: ARRAY OF CHAR);
	BEGIN
		ASSERT(wr.level >= 1, 100);
		IF data # "" THEN
			IF ~wr.current.inContent THEN
				StartElemContent(wr);
				Tabs(wr)
			END;
			String(wr, data); wr.afterData := TRUE
		END
	END Data;

	PROCEDURE (wr: Writer) EndTag;
	BEGIN
		ASSERT(wr.level >= 1, 100);
		IF (wr.current.name = "p") & ~wr.current.inContent THEN
			wr.Data("&nbsp;") (* fixes empty paragraph issue *)
		END;
		DEC(wr.level);
		IF wr.current.inContent THEN
			IF wr.afterData THEN wr.Ln END;
			Tabs(wr); String(wr, "</" + wr.current.name + ">")
		ELSE
			(* note that StartElemContents is NOT called! *)
			String(wr, "/>")
		END;
		wr.current := wr.current.up;
		wr.Ln; wr.afterData := FALSE;
		IF wr.level = 0 THEN wr.rider.Base().Flush END
	END EndTag;


	(* factory functions *)

	PROCEDURE MakeFileSpec (IN path: ARRAY OF CHAR; OUT loc: Files.Locator; OUT file: Files.Name);
		VAR i, j: INTEGER; ch: CHAR; s: Files.Name;
	BEGIN
		loc := Files.dir.This(""); IF loc.res # 0 THEN RETURN END;
		j := 0; i := 0; ch := path[0];
		WHILE ch # 0X DO
			IF ch = "/" THEN
				s[j] := 0X;	(* s contains next path element *)
				loc := loc.This(s); IF loc.res # 0 THEN RETURN END;
				j := 0
			ELSE
				s[j] := ch; INC(j)
			END;
			INC(i); ch := path[i]
		END;
		s[j] := 0X;	(* s contains file name *)
		file := s$
	END MakeFileSpec;

	PROCEDURE New* (f: Files.File): XhtmlWriters.Writer;
		VAR wr: Writer;
	BEGIN
		ASSERT(f # NIL, 20);
		NEW(wr);
		NEW(wr.current); wr.current.name := ""; wr.current.up := NIL;
		wr.rider := f.NewWriter(NIL);
		ASSERT(wr # NIL, 100); ASSERT(wr.rider # NIL, 101); ASSERT(wr.level >= 0, 102);
		String(wr, '<?xml version="1.0" encoding="utf-8"?>'); wr.Ln;
		RETURN wr
	END New;

	PROCEDURE NewForPath* (IN path: ARRAY OF CHAR): XhtmlWriters.Writer;
		VAR loc: Files.Locator; f: Files.File; name: Files.Name; wr: XhtmlWriters.Writer;
	BEGIN
		ASSERT(path # "", 20);
		MakeFileSpec(path, loc, name);
		f := Files.dir.New(loc, Files.dontAsk);
		IF f # NIL THEN wr := New(f) ELSE wr := NIL END;
		RETURN wr
	END NewForPath;

END XhtmlStdFileWriters.
