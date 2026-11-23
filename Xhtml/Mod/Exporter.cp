MODULE XhtmlExporter;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""
	
	The generated output follows the structure: html = prefix { paragraph | table } postfix.
	prefix and postfix are constant except for the title and the default font.
	A paragraph is a <p> element and a table is a <table> element.
	A table results only from special start and end marker views provided by XhtmlTextTableMarkers
	and uses the current ruler for the alignment of table cells but not for cell spacing.
	Within a table, newlines, paragraphs and rulers introduce a new table row. A tab introduces a table column.
	Link and Target views can be embedded in paragraphs and tables as <a> elements.
	Tables and links cannot be nested otherwise.
	Empty lines, empty paragraphs and multiple spaces  are preserved by the exporter.
	Unknown views are skipped and terminate the current paragraph.
	The exporter tolerates some pairing/nesting errors in the input text but also traps for some.

	Naming convention: BeginXXX means begin or continue XXX, EndXXX means terminate XXX if it is open.
**)

	IMPORT
		Strings, Files, Stores, Fonts, (*WinFonts, adimetrius*) Ports, Views, Windows, Dialog,
		TextModels, TextViews, TextRulers, StdLinks,
		XhtmlWriters, XhtmlStdFileWriters,
		XhtmlEntitySets, XhtmlTextTableMarkers;
		
		(* XhtmlExporter imports WinFonts for the sake of a single reference in PROCEDURE ExportText. It doesn't look good or clear to me, and I don't understand why the universal Fonts.Font services won't suffice, so for now I just comment it out, thus eliminating the dependency and making Xhtml subsystem universal. *)

	CONST
		pubidLiteral = "-//W3C//DTD XHTML 1.0 Strict//EN";
		sysidLiteral = "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd";
		left = 0; center = 1; right = 2;
		untitled = "#Xhtml:New Page";

	TYPE
		Exporter = POINTER TO RECORD
			inLink: BOOLEAN;	(* this variable is used for a state machine used for translating hyperlinks *)
			inTable: BOOLEAN;	(* this variable is used for a state machine used for translating tables *)
			inRow: BOOLEAN;	(* this variable is used for a state machine used for translating tables *)
			inField: BOOLEAN;	(* this variable is used for a state machine used for translating tables *)
			inPara: BOOLEAN;	(* Generating paragraph elems is delayed so that empty elements are never created.
											This variable is used for a state machine that implements the necessary delaying.*)
			afterSpace: BOOLEAN;	(* used for a state machine that avoids successive spaces in an element *)
			inSpan: BOOLEAN;	(* used for maintaining attribute spans *)
			attr: TextModels.Attributes;	(* current attributes; needed for detecting attribute changes *)
			currentRuler: TextRulers.Ruler;	(* most recently read ruler *)
			tabIndex: INTEGER;	(* tab index in current line, -1 means  no tab read in this line yet*)
			defaultSize: INTEGER;	(* the default font size in world coordinates *)
			defaultTypeface: Fonts.Typeface;	(* the default font name *)
			wr: XhtmlWriters.Writer
		END;

	PROCEDURE Invariant (e: Exporter);
	BEGIN
		ASSERT(e # NIL, 100);
		ASSERT(e.inPara OR e.inTable OR (e.attr = NIL), 101);	(* (~e.inPara & ~e.inTable) => (e.attr = NIL) *)
		ASSERT(~e.inField OR e.inRow, 102);	(* e.inField => e.inRow *)
		ASSERT(~e.inRow OR e.inTable, 103);	(* e.inRow => e.inTable *)
		ASSERT(~e.afterSpace OR e.inPara OR e.inTable, 104);	(* e.afterSpace => (e.inPara OR e.inTable) *)
		ASSERT(e.tabIndex >= -1, 106);
		ASSERT(~e.inField OR (e.tabIndex >= 0), 107);	(* e.inField => e.tabIndex >= 0 *)
		ASSERT(~e.inTable OR (e.currentRuler # NIL), 108);	(* e.inTable => (e.currentRuler # NIL) *)
		ASSERT(e.wr # NIL, 109)
	END Invariant;

	PROCEDURE ColorToString (c: INTEGER; OUT str: ARRAY OF CHAR);
		VAR red, green, blue: INTEGER; h: ARRAY 16 OF CHAR;
	BEGIN
		red := c MOD 256; c := c DIV 256;
		green := c MOD 256; c := c DIV 256;
		blue := c;
		c := red;
		c := c * 256 + green;
		c := c * 256 + blue;
		Strings.IntToStringForm(c, Strings.hexadecimal, 6, "0", FALSE, h);
		str := "#" + h
	END ColorToString;

	(*
		text = p | parsed text
		body = text | (table tr td text)
	*)

	PROCEDURE SetAttr (e: Exporter; newAttr: TextModels.Attributes);
	(* nesting: <span> <b> <i> text </i> </b> </span> *)
		CONST bold = (Fonts.normal + Fonts.bold) DIV 2;
		VAR s, tmp: ARRAY 256 OF CHAR;
	BEGIN
		IF (e.attr # newAttr) & ((e.attr = NIL) OR (newAttr = NIL) OR ~e.attr.Equals(newAttr)) THEN
			IF e.attr # NIL THEN (* close all special attributes *)
				IF Fonts.italic IN e.attr.font.style THEN e.wr.EndTag(*i*) END;
				IF e.attr.font.weight >= bold THEN e.wr.EndTag(*b*) END;
			END;
			IF (newAttr = NIL) OR (e.attr = NIL)
			OR (e.attr.font.typeface # newAttr.font.typeface)
			OR (e.attr.font.size # newAttr.font.size)
			OR (e.attr.color # newAttr.color)
			OR (Fonts.underline IN newAttr.font.style / e.attr.font.style)
			OR (Fonts.strikeout IN newAttr.font.style / e.attr.font.style) THEN (* close span *)
				IF e.inSpan THEN e.wr.EndTag(*span*); e.inSpan := FALSE END
			END;
			IF (newAttr # NIL) & (~e.inSpan) THEN (* open span *)
				s := "";
				IF (newAttr.font.typeface # e.defaultTypeface) & (newAttr.font.typeface # Fonts.default) THEN
					s := "font-family: " + newAttr.font.typeface$ + ";"
				END;
				IF newAttr.color # Ports.defaultColor THEN
					IF s[0] # 0X THEN s := s + " " END;
					ColorToString(newAttr.color, tmp);
					s := s + "color: " + tmp + ";"
				END;
				IF newAttr.font.size # e.defaultSize THEN
					IF s[0] # 0X THEN s := s + " " END;
					Strings.IntToString(newAttr.font.size DIV Fonts.point, tmp);
					s := s + "font-size: " + tmp + "pt;"
				END;
				IF Fonts.underline IN newAttr.font.style THEN
					IF s[0] # 0X THEN s := s + " " END;
					IF Fonts.strikeout IN newAttr.font.style THEN s := s + "text-decoration: underline line-through;"
					ELSE s := s + "text-decoration: underline;"
					END
				ELSIF Fonts.strikeout IN newAttr.font.style THEN
					IF s[0] # 0X THEN s := s + " " END;
					s := s + "text-decoration: line-through;"
				END;
				IF s[0] # 0X THEN
					e.wr.StartTag("span", XhtmlWriters.preserve); e.wr.Attr("style", s);
					e.inSpan := TRUE
				END
			END;
			IF newAttr # NIL THEN (* open all special attributes *)
				IF newAttr.font.weight >= bold THEN e.wr.StartTag("b", XhtmlWriters.preserve) END;
				IF Fonts.italic IN newAttr.font.style THEN e.wr.StartTag("i", XhtmlWriters.preserve) END
			END;
			e.attr := newAttr
		END
	END SetAttr;

	PROCEDURE BeginPara (e: Exporter);
		VAR attr: ARRAY 128 OF CHAR; str: ARRAY 64 OF CHAR;
	BEGIN
		IF ~e.inPara & ~e.inLink & ~e.inTable THEN
			e.wr.StartTag("p", XhtmlWriters.preserve);
			attr := "";
			IF e.currentRuler # NIL THEN
				IF TextRulers.rightAdjust IN e.currentRuler.style.attr.opts THEN
					IF TextRulers.leftAdjust IN e.currentRuler.style.attr.opts THEN
						attr := "text-align: justify;"
					ELSE
						attr := "text-align: right;"
					END
				ELSIF ~ (TextRulers.leftAdjust IN e.currentRuler.style.attr.opts) THEN
					attr := "text-align: center;"
				END
			END;
			IF e.currentRuler.style.attr.left # 0 THEN
				Strings.IntToString(e.currentRuler.style.attr.left DIV Fonts.point, str);
				IF attr[0] # 0X THEN attr := attr + " " END;
				attr := attr + "margin-left: " + str$ + "pt;"
			END;
			IF (TextRulers.rightFixed IN e.currentRuler.style.attr.opts) THEN
				Strings.IntToString( (e.currentRuler.style.attr.right - e.currentRuler.style.attr.left) DIV Fonts.point, str);
				IF attr[0] # 0X THEN attr := attr + " " END;
				attr := attr + "width: " + str$ + "pt;"
			END;
			IF e.currentRuler.style.attr.first # 0 THEN
				Strings.IntToString((e.currentRuler.style.attr.first - e.currentRuler.style.attr.left)
					DIV Fonts.point, str);
				IF attr[0] # 0X THEN attr := attr + " " END;
				attr := attr + "text-indent: " + str$ + "pt;"
			END;
			IF e.currentRuler.style.attr.lead > 0 THEN
				Strings.IntToString(e.currentRuler.style.attr.lead DIV Fonts.point + 3, str);
				IF attr[0] # 0X THEN attr := attr + " " END;
				attr := attr + "margin-top: " + str$ + "pt;"
			END;
			IF attr[0] # 0X THEN
				e.wr.Attr("style", attr)
			END;
			e.inPara := TRUE
		END
	END BeginPara;

	PROCEDURE EndPara (e: Exporter);
	BEGIN
		IF e.inPara THEN
			SetAttr(e, NIL);
			e.wr.EndTag(*p*);
			e.afterSpace := FALSE;
			e.inPara := FALSE
		END
	END EndPara;

	PROCEDURE Alignment (r: TextRulers.Ruler; tabIndex: INTEGER): INTEGER;
		VAR a: INTEGER; type: SET;
	BEGIN
		ASSERT(r # NIL, 100); ASSERT(tabIndex >= 0, 101);
		IF tabIndex < r.style.attr.tabs.len THEN
			type := r.style.attr.tabs.tab[tabIndex].type;
			IF TextRulers.centerTab IN type THEN
				a := center
			ELSIF TextRulers.rightTab IN type THEN
				a := right
			ELSE
				a := left
			END
		ELSE a := left
		END;
		RETURN a
	END Alignment;

	PROCEDURE BeginRow (e: Exporter);
	BEGIN
		IF ~e.inRow THEN
			ASSERT(~e.inField, 21); ASSERT(~e.inPara, 22);
			e.wr.StartTag("tr", XhtmlWriters.prettyPrint);
			e.inRow := TRUE
		END
	END BeginRow;

	PROCEDURE BeginField (e: Exporter);
		VAR a: INTEGER;
	BEGIN
		IF ~e.inField THEN
			BeginRow(e);
			ASSERT(~e.inField, 20); ASSERT(~e.inPara, 21);
			a := Alignment(e.currentRuler, e.tabIndex);
			e.wr.StartTag("td", XhtmlWriters.preserve);
			IF a = center THEN
				e.wr.Attr("align", "center")
			ELSIF a = right THEN
				e.wr.Attr("align", "right")
			END;
			e.inField := TRUE
		END
	END BeginField;

	PROCEDURE EndField (e: Exporter);
	BEGIN
		IF e.inField THEN
			ASSERT(~e.inPara, 21);
			SetAttr(e, NIL);
			e.wr.EndTag(*td*);
			e.inField := FALSE
		END
	END EndField;

	PROCEDURE BeginTable (e: Exporter);
	BEGIN
		IF ~e.inTable THEN
			ASSERT(~e.inRow, 21); ASSERT(~e.inField, 22); ASSERT(~e.inPara, 23);
			e.wr.StartTag("table", XhtmlWriters.prettyPrint); e.wr.Attr("border", "1"); e.wr.Attr("width", "100%");
			e.inTable := TRUE
		END
	END BeginTable;

	PROCEDURE EndRow (e: Exporter);
		VAR n, i: INTEGER;
	BEGIN
		ASSERT(e.inTable, 101);
		IF e.inRow THEN
			ASSERT(e.currentRuler # NIL, 102);
			IF e.tabIndex >= 0 THEN
				IF ~e.inField THEN BeginField(e); e.wr.Data("&nbsp;") END;
				EndField(e)
			END;
			n := e.currentRuler.style.attr.tabs.len - e.tabIndex - 1;
			IF n >= 1 THEN	(* fill row with empty fields *)
				i := 0;
				WHILE i # n DO
					INC(e.tabIndex);
					BeginField(e); e.wr.Data("&nbsp;"); EndField(e);
					INC(i)
				END
			END;
			e.wr.EndTag(*tr*); e.inRow := FALSE
		END;
		ASSERT(~e.inRow, 21); ASSERT(~e.inField, 22); ASSERT(~e.inPara, 23)
	END EndRow;

	PROCEDURE EndTable (e: Exporter);
	BEGIN
		IF e.inTable THEN
			EndRow(e);
			e.wr.EndTag(*table*);	
			e.inTable := FALSE
		END
	END EndTable;

	PROCEDURE BegOfTable (v: Views.View): BOOLEAN;
	BEGIN
		IF v # NIL THEN RETURN XhtmlTextTableMarkers.IsOpenMark(v)
		ELSE RETURN FALSE
		END
	END BegOfTable;

	PROCEDURE EndOfTable (v: Views.View): BOOLEAN;
	BEGIN
		IF v # NIL THEN RETURN XhtmlTextTableMarkers.IsCloseMark(v)
		ELSE RETURN FALSE
		END
	END EndOfTable;

	PROCEDURE BeginHref (e: Exporter; v: StdLinks.Link);
		VAR s, cmd: ARRAY 256 OF CHAR; pos: INTEGER;
	BEGIN
		ASSERT(~e.inLink, 20);
		v.GetCmd(s);
		Strings.Find(s, "(", 0, pos);
		IF pos >= 0 THEN Strings.Extract(s, 0, pos, cmd) ELSE cmd := "" END;
		SetAttr(e, NIL);
		e.wr.StartTag("a", XhtmlWriters.preserve);
		IF cmd = "StdLinks.ShowTarget" THEN
			Strings.Extract(s, pos + 2, LEN(s$) - pos - 4, s);	(* extract the name of the target anchor *)
			e.wr.Attr("href", "#a" + s)
		ELSIF cmd = "Dialog.OpenExternal" THEN
			Strings.Extract(s, pos + 2, LEN(s$) - pos - 4, s);	(* extract the name of the url *)
			e.wr.Attr("href", s)
		ELSE
			e.wr.Attr("href", s$)
		END;
		e.inLink := TRUE
	END BeginHref;

	PROCEDURE EndHref (e: Exporter);
	BEGIN
		IF e.inLink THEN 
			SetAttr(e, NIL);
			e.wr.EndTag(*a*);	
			e.inLink := FALSE
		END
	END EndHref;

	PROCEDURE Anchor (e: Exporter; v: StdLinks.Target);
		VAR s: ARRAY 256 OF CHAR;
	BEGIN
		ASSERT(~e.inLink, 20);
		v.GetIdent(s);
		e.wr.StartTag("a", XhtmlWriters.preserve); e.wr.Attr("id", "a" + s$); e.wr.EndTag(*a*)
	END Anchor;

	PROCEDURE View (e: Exporter; rd: TextModels.Reader);
		VAR v: Views.View;
	BEGIN
		v := rd.view;
		ASSERT(v # NIL, 101);
		WITH v: StdLinks.Link DO
			IF v.leftSide & ~e.inLink THEN	(* open link *)
				IF e.inTable THEN BeginField(e) ELSE BeginPara(e) END;
				BeginHref(e, v)
			ELSIF ~v.leftSide THEN	(* close link *)
				EndHref(e)
			(* ELSE ignore inappropriately nested link views *)
			END
		| v: StdLinks.Target DO
			IF v.leftSide THEN
				IF e.inTable THEN BeginField(e) ELSE BeginPara(e) END;
				Anchor(e, v)
			END
		ELSE	(* unknown view: here, an extension mechanism would be needed to handle such views *)
			EndPara(e) 
			(* skip *)
		END
	END View;
	
	PROCEDURE GetTitle(v: Stores.Store; VAR title: Views.Title);
		VAR win: Windows.Window;
	BEGIN
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.Domain() # v.Domain()) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Dialog.MapString(untitled, title)
		ELSE win.GetTitle(title)
		END
	END GetTitle;
	
	PROCEDURE WriteHtmlEncoded(e: Exporter; IN title: Views.Title);
		VAR i: INTEGER; ch: CHAR; str: ARRAY 10 OF CHAR;
	BEGIN
		i := 0; ch := title[0];
		WHILE ch # 0X DO XhtmlEntitySets.MapCharToEntity(ch, str); e.wr.Data(str$); INC(i); ch := title[i] END
	END WriteHtmlEncoded;

	PROCEDURE ExportText* (s: Stores.Store; f: Files.File);
		VAR
			e: Exporter;
			t: TextModels.Model; rd: TextModels.Reader;
			len: INTEGER;	(* number of characters that remain to be translated *)
			ch: CHAR; str, tmp: ARRAY 256 OF CHAR; title: Views.Title;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21); ASSERT(f.Length() = 0, 22);
		ASSERT(s IS TextViews.View, 23);
		NEW(e); e.inLink := FALSE; e.inTable := FALSE; e.inPara := FALSE; e.afterSpace := FALSE; e.inSpan := FALSE;
		e.defaultSize := Fonts.dir.Default().size;
		e.defaultTypeface := Fonts.dir.Default().typeface (*(WinFonts.Font).alias*);
		IF e.defaultTypeface = "*" THEN e.defaultTypeface := "Arial" END;
		e.wr := XhtmlStdFileWriters.New(f);
		t := s(TextViews.View).ThisModel();
		len := t.Length();
		e.wr.DocType("html", pubidLiteral, sysidLiteral );
		e.wr.StartTag("html", XhtmlWriters.prettyPrint);
		e.wr.Attr("xmlns", "http://www.w3.org/1999/xhtml");
		e.wr.StartTag("head", XhtmlWriters.prettyPrint);
		e.wr.StartTag("meta", XhtmlWriters.preserve);
		e.wr.Attr("http-equiv", "content-type");
		e.wr.Attr("content", "text/html; charset=utf-8");
		e.wr.EndTag(*meta*);
		GetTitle(s, title);
		e.wr.StartTag("title", XhtmlWriters.preserve); WriteHtmlEncoded(e, title); e.wr.EndTag(*title*);
		(* body style *)
		str := "font-family: " + e.defaultTypeface + "; ";
		ColorToString(Ports.defaultColor, tmp);
		str := str + "color: " + tmp + "; ";
		Strings.IntToString(e.defaultSize DIV Ports.point, tmp);
		str := str + "font-size: " + tmp + "pt;";
		(* common CSS *)
		e.wr.StartTag("style", XhtmlWriters.prettyPrint);
		e.wr.Attr("type", "text/css");
		e.wr.Data("body { " + str$ + " }");
		e.wr.Ln; e.wr.Data(09X + 09X + 09X + "p { margin-top: 3pt; margin-bottom: 0pt; }");
		e.wr.EndTag(*style*);	
		e.wr.EndTag(*head*);	
		e.wr.StartTag("body", XhtmlWriters.prettyPrint);
		s(TextViews.View).PollDefaults(e.currentRuler, e.attr);
		e.attr := NIL; (* to prevent Invariant TRAP 101 *)
		rd := t.NewReader(NIL);
		e.tabIndex := -1;
		WHILE len # 0 DO
			Invariant(e);
			rd.ReadChar(ch);
			IF (rd.view # NIL) & (rd.view IS TextRulers.Ruler) THEN
				e.currentRuler := rd.view(TextRulers.Ruler)
			END;
			IF BegOfTable(rd.view) THEN
				EndPara(e); EndTable(e); BeginTable(e); e.tabIndex := -1
			ELSIF EndOfTable(rd.view) THEN
				EndTable(e); e.tabIndex := -1
			ELSIF (ch = TextModels.para) OR (ch = TextModels.line) OR (ch = 0X) THEN
				IF e.inTable THEN
					EndRow(e); e.tabIndex := -1
				ELSIF ch = TextModels.line THEN
					BeginPara(e); e.wr.Data("<br/>"+0AX)
				ELSE
					EndPara(e); BeginPara(e)
				END
			ELSIF ch = TextModels.tab THEN
				IF e.inTable THEN
					INC(e.tabIndex);
					BeginRow(e);
					IF e.tabIndex >= 1 THEN	(* space until first tab is not part of a table column *)
						IF ~e.inField THEN (* last table field is empty *) BeginField(e); e.wr.Data("&nbsp;") END;
						EndField(e)
					END
				ELSE
					BeginPara(e);
					e.wr.Data("&nbsp;&nbsp;&nbsp;"); e.afterSpace := TRUE	(* emulate tab with three spaces *)
				END
			ELSIF ch = TextModels.viewcode THEN	(* translate rd.view into HTML *)
				View(e, rd)
			ELSE
				IF e.inTable THEN BeginField(e) ELSE BeginPara(e) END;
				SetAttr(e, rd.attr);
				IF ch = " " THEN
					IF e.afterSpace THEN e.wr.Data("&nbsp;") ELSE e.wr.Data(" "); e.afterSpace := TRUE END
				ELSIF (ch = TextModels.nbspace) OR (ch = TextModels.digitspace) THEN
					e.wr.Data("&nbsp;"); e.afterSpace := FALSE
				ELSIF ch = TextModels.zwspace THEN
					e.wr.Data("&zwnj;"); e.afterSpace := TRUE
				ELSIF (ch = TextModels.hyphen) OR (ch = TextModels.nbhyphen) THEN
					e.wr.Data("-"); e.afterSpace := FALSE
				ELSE
					XhtmlEntitySets.MapCharToEntity(ch, str);	(* this is the normal case of writing a character *)
					e.wr.Data(str$); e.afterSpace := FALSE
				END
			END;
			DEC(len)
		END; (* WHILE *)
		EndHref(e);
		EndTable(e);
		EndPara(e);
		e.wr.EndTag(*body*);
		e.wr.EndTag(*html*)
	END ExportText;

END XhtmlExporter.
