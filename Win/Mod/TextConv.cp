MODULE WinTextConv;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems, Alexander Iljin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		SYSTEM, WinApi, WinOle, COM,
		Files, Fonts, Ports, Stores, Views, Properties,
		HostFonts := WinFonts, HostClipboard := WinClipboard, StdLinks, TextModels,
		TextRulers, TextViews, TextMappers;
	
	CONST
		CR = 0DX; LF = 0AX; FF = 0EX; TAB = 09X;
		halfpoint = Ports.point DIV 2;
		twips = Ports.point DIV 20;
		linkcmd = "Dialog.OpenExternal";

	TYPE
		MemReader = POINTER TO RECORD (Files.Reader)
			adr, pos: INTEGER
		END;
	
	VAR 
		debug*: BOOLEAN;
		

	(* MemReader *)
	
	PROCEDURE (r: MemReader) Base (): Files.File;
	BEGIN
		RETURN NIL
	END Base;
	
	PROCEDURE (r: MemReader) Pos (): INTEGER;
	BEGIN
		RETURN r.pos
	END Pos;
	
	PROCEDURE (r: MemReader) SetPos (pos: INTEGER);
	BEGIN
		r.pos := pos
	END SetPos;
	
	PROCEDURE (r: MemReader) ReadByte (OUT x: BYTE);
	BEGIN
		SYSTEM.GET(r.adr + r.pos, x); INC(r.pos)
	END ReadByte;
	
	PROCEDURE (r: MemReader) ReadBytes (VAR x: ARRAY OF BYTE; beg, len: INTEGER);
	BEGIN
		HALT(126)
	END ReadBytes;
	
	
	PROCEDURE GenGlobalMedium (hg: WinApi.HGLOBAL; unk: COM.IUnknown; VAR sm: WinOle.STGMEDIUM);
	BEGIN
		sm.tymed := WinOle.TYMED_HGLOBAL;
		sm.u.hGlobal := hg;
		sm.pUnkForRelease := unk
	END GenGlobalMedium;
	
	PROCEDURE MediumGlobal (VAR sm: WinOle.STGMEDIUM): WinApi.HGLOBAL;
	BEGIN
		ASSERT(sm.tymed = WinOle.TYMED_HGLOBAL, 20);
		RETURN sm.u.hGlobal
	END MediumGlobal;
	
	
	PROCEDURE WriteWndChar (wr: TextModels.Writer; ch: CHAR);
	BEGIN
		CASE ch OF
		| CR, TAB, " "..7EX, 0A0X..0FFX: wr.WriteChar(ch)
		| LF:
		| 80X: wr.WriteChar(20ACX)	(* euro *)
		| 82X: wr.WriteChar(201AX)
		| 83X: wr.WriteChar(0192X)
		| 84X: wr.WriteChar(201EX)
		| 85X: wr.WriteChar(2026X)
		| 86X: wr.WriteChar(2020X)
		| 87X: wr.WriteChar(2021X)
		| 88X: wr.WriteChar(02C6X)
		| 89X: wr.WriteChar(2030X)
		| 8AX: wr.WriteChar(0160X)
		| 8BX: wr.WriteChar(2039X)
		| 8CX: wr.WriteChar(0152X)
		| 8EX: wr.WriteChar(017DX)
		| 91X: wr.WriteChar(2018X)
		| 92X: wr.WriteChar(2019X)
		| 93X: wr.WriteChar(201CX)
		| 94X: wr.WriteChar(201DX)
		| 95X: wr.WriteChar(2022X)
		| 96X: wr.WriteChar(2013X)
		| 97X: wr.WriteChar(2014X)
		| 98X: wr.WriteChar(02DCX)
		| 99X: wr.WriteChar(2122X)
		| 9AX: wr.WriteChar(0161X)
		| 9BX: wr.WriteChar(203AX)
		| 9CX: wr.WriteChar(0153X)
		| 9EX: wr.WriteChar(017EX)
		| 9FX: wr.WriteChar(0178X)
		| 0X..8X, 0BX, 0CX, 0EX..1FX, 7FX, 81X, 8DX, 8FX..90X, 9DX:
			wr.WriteChar(CHR(0EF00H + ORD(ch)))
		END
	END WriteWndChar;
	
	PROCEDURE ThisWndChar (ch: CHAR): CHAR;
	BEGIN
		IF ch >= 100X THEN
			IF (ch >= 0EF00X) & (ch <= 0EFFFX) THEN ch := CHR(ORD(ch) - 0EF00H)
			ELSIF ch =  20ACX THEN ch := 80X	(* euro *)
			ELSIF ch =  201AX THEN ch := 82X
			ELSIF ch =  0192X THEN ch := 83X
			ELSIF ch =  201EX THEN ch := 84X
			ELSIF ch =  2026X THEN ch := 85X
			ELSIF ch =  2020X THEN ch := 86X
			ELSIF ch =  2021X THEN ch := 87X
			ELSIF ch =  02C6X THEN ch := 88X
			ELSIF ch =  2030X THEN ch := 89X
			ELSIF ch =  0160X THEN ch := 8AX
			ELSIF ch =  2039X THEN ch := 8BX
			ELSIF ch =  0152X THEN ch := 8CX
			ELSIF ch =  017DX THEN ch := 8EX
			ELSIF ch =  2018X THEN ch := 91X
			ELSIF ch =  2019X THEN ch := 92X
			ELSIF ch =  201CX THEN ch := 93X
			ELSIF ch =  201DX THEN ch := 94X
			ELSIF ch =  2022X THEN ch := 95X
			ELSIF ch =  2013X THEN ch := 96X
			ELSIF ch =  2014X THEN ch := 97X
			ELSIF ch =  02DCX THEN ch := 98X
			ELSIF ch =  2122X THEN ch := 99X
			ELSIF ch =  0161X THEN ch := 9AX
			ELSIF ch =  203AX THEN ch := 9BX
			ELSIF ch =  0153X THEN ch := 9CX
			ELSIF ch =  017EX THEN ch := 9EX
			ELSIF ch =  0178X THEN ch := 9FX
			ELSE  ch := "?"
			END
		ELSIF ch = 08FX THEN ch := " "	(* digit space *)
		END;
		RETURN ch
	END ThisWndChar;
	
	PROCEDURE ParseRichText (rd: Files.Reader; wr: TextModels.Writer; VAR defRuler: TextRulers.Ruler);
		TYPE 
			FontInfo = POINTER TO RECORD id, codePage: INTEGER; f: Fonts.Typeface; next: FontInfo END;
			ColorInfo = POINTER TO RECORD id: INTEGER; c: Ports.Color; next: ColorInfo END;
			Context = POINTER TO RECORD next: Context;
				dest, uniCnt: INTEGER; font: FontInfo; attr: TextModels.Attributes; pattr: TextRulers.Attributes
			END;
		CONST
			text = 0; fonttab = 1; colortab = 2; skip = 3; fieldinstr = 4;
			noFontId = MIN(INTEGER); (* font not defined in \fonttbl *)
		VAR
			ch: SHORTCHAR; tabStyle: SET;
			fact, val, defFont, dest, idx, fnum, cnum, paraPos, i: INTEGER;
			fonts, font: FontInfo; colors: ColorInfo;
			hasNum, skipDest: BOOLEAN;
			comm: ARRAY 32 OF CHAR;
			c, con: Context; p0: Properties.Property; p: TextRulers.Prop;
			ruler: TextRulers.Ruler;
			pattr: TextRulers.Attributes;
			skipCnt, uniCnt : INTEGER;
			hyperlinkCtx: Context; idx2: INTEGER; fieldStr: ARRAY 1024 OF CHAR; v: Views.View;
			tableCtx: Context; trgaph, ansiCodePage: INTEGER;
			superOrSubCtx: Context;
			(* buffer for code-page to Unicode conversion; by buffering we get multi-byte decoding for free *)
			cpSrc: POINTER TO ARRAY OF SHORTCHAR;
			cpDst: POINTER TO ARRAY OF CHAR;
			cpLen, cpPos: INTEGER;

		PROCEDURE Color(i: INTEGER): ColorInfo;
			VAR c: ColorInfo;
		BEGIN
			ASSERT(colors # NIL, 20);
			c := colors;
			WHILE (c # NIL) & (c.id # i) DO c := c.next END;
			ASSERT(c # NIL, 100);
			RETURN c
		END Color;
		
		PROCEDURE SetColor(i: INTEGER; c: Ports.Color);
			VAR ci: ColorInfo;
		BEGIN
			NEW(ci); ci.id := i; ci.c := c; ci.next := colors; colors := ci
		END SetColor;
		
		PROCEDURE Font(i: INTEGER): FontInfo;
			VAR f: FontInfo;
		BEGIN
			ASSERT(fonts # NIL, 20);
			f := fonts;
			WHILE (f # NIL) & (f.id # i) DO f := f.next END;
			IF (f = NIL) & (i # noFontId) THEN f := Font(noFontId) END;
			ASSERT(f # NIL, 100);
			RETURN f
		END Font;
		
		PROCEDURE Next (VAR ch: SHORTCHAR);
			VAR b: BYTE;
		BEGIN
			rd.ReadByte(b); ch := SHORT(CHR(b))
		END Next;
		
		PROCEDURE WriteDest (ch: CHAR);
		BEGIN
			IF skipCnt > 0 THEN
				DEC(skipCnt)
			ELSIF dest = text THEN
				IF ch < 100X THEN WriteWndChar(wr, ch) ELSE wr.WriteChar(ch) END
			ELSIF dest = fonttab THEN
				ASSERT(font # NIL, 20);
				font.f[idx] := ch; INC(idx); font.f[idx] := 0X
			ELSIF dest = fieldinstr THEN
				fieldStr[idx2] := ch; INC(idx2); fieldStr[idx2] := 0X
			END
		END WriteDest;
		
		PROCEDURE WriteCP(ch: SHORTCHAR);
			VAR s: POINTER TO ARRAY OF SHORTCHAR;
		BEGIN
			IF cpPos = cpLen THEN NEW(s, cpLen * 2);
				SYSTEM.MOVE(SYSTEM.ADR(cpSrc[0]), SYSTEM.ADR(s[0]), cpLen);
				cpSrc := s; cpLen := LEN(s); NEW(cpDst, cpLen)
			END;
			cpSrc[cpPos] := ch; INC(cpPos)
		END WriteCP;
		
		PROCEDURE FlushCP();
			VAR i, res, cp: INTEGER;
		BEGIN
			IF cpPos > 0 THEN
				IF (font # NIL) & (dest = text) THEN cp := font.codePage ELSE cp := ansiCodePage END;
				IF cp = 1252 THEN
					FOR i := 0 TO cpPos - 1 DO WriteDest(cpSrc[i]) END
				ELSE
					res := WinApi.MultiByteToWideChar(cp, {}, cpSrc^, cpPos, cpDst^, cpLen);
					IF res > 0 THEN FOR i := 0 TO res - 1 DO WriteDest(cpDst[i]) END
					ELSE WriteDest("?"); WriteDest("?")
					END
				END;
				cpPos := 0
			END
		END FlushCP;
		
		PROCEDURE Write (ch: CHAR);
		BEGIN
			FlushCP; WriteDest(ch)
		END Write;
		
		PROCEDURE Paragraph;
			VAR v: Views.View;
		BEGIN
			IF ~pattr.Equals(ruler.style.attr) THEN	(* new ruler needed *)
				wr.SetPos(paraPos);
				v := Views.CopyOf(ruler, Views.deep); ruler := v(TextRulers.Ruler);
				ruler.style.SetAttr(pattr);
				wr.WriteView(ruler, Views.undefined, Views.undefined);
				wr.SetPos(wr.Base().Length())
			ELSIF (pattr.first # pattr.left)
					OR (pattr.lead > 0)
					OR (TextRulers.pageBreak IN pattr.opts) THEN	(* paragraph marker needed *)
				wr.SetPos(paraPos);
				wr.WriteChar(FF);
				wr.SetPos(wr.Base().Length())
			END;
			wr.WriteChar(CR);
			paraPos := wr.Pos()
		END Paragraph;
		
		PROCEDURE Trim(VAR str: ARRAY OF CHAR);
			VAR i, j: INTEGER; ch: CHAR;
		BEGIN
			i := 0; ch := str[0];
			WHILE (ch # 0X) & (ch <= " ") DO INC(i); ch := str[i] END;
			IF i > 0 THEN j := 0;
				WHILE ch # 0X DO str[j] := ch; INC(i); INC(j); ch := str[i] END
			ELSE j := LEN(str$)
			END;
			WHILE (j > 0) & (str[j - 1] <= " ") DO DEC(j) END;
			str[j] := 0X
		END Trim;
		
	BEGIN
		NEW(fonts); fonts.id := noFontId; fonts.codePage := 1252; fonts.f := Fonts.dir.Default().typeface;
		defFont := noFontId; fnum := noFontId; skipCnt := 0; uniCnt := 1;
		cnum := 1; NEW(colors); SetColor(0, Ports.defaultColor);
		dest := text; con := NIL; paraPos := 0; skipDest := FALSE;
		defRuler := TextRulers.dir.New(NIL); ruler := defRuler; pattr := defRuler.style.attr; tabStyle := {};
		hyperlinkCtx := NIL; tableCtx := NIL; superOrSubCtx := NIL; ansiCodePage := 1252;
		cpLen := 256; NEW(cpSrc, cpLen); NEW(cpDst, cpLen); cpPos := 0; 			
		IF WinApi.GetKeyState(WinApi.VK_MENU) < 0 THEN	(*  Alt key, no conversion  *)
			Next(ch);
			WHILE ch # 0X DO
				IF (ch = '\') OR (ch = '{') OR (ch = '}') THEN wr.WriteChar(CR) END;
				wr.WriteChar(ch); Next(ch)
				(* note: MemReader may deliver undefined characters at the end *)
			END;
			RETURN
		END;
		Next(ch);
		WHILE ch # 0X DO
			IF ch = "{" THEN
				skipCnt := 0; FlushCP;
				NEW(c); c.dest := dest; c.attr := wr.attr; c.pattr := pattr; c.uniCnt := uniCnt; c.font := font;
				c.next := con; con := c;
				Next(ch)
			ELSIF ch = "}" THEN
				skipCnt := 0; FlushCP;
				IF con # NIL THEN
					IF con = hyperlinkCtx THEN wr.SetPos(wr.Base().Length()); hyperlinkCtx := NIL
					ELSIF con = superOrSubCtx THEN superOrSubCtx:= NIL
					END;
					dest := con.dest; uniCnt := con.uniCnt; font := con.font; wr.SetAttr(con.attr); pattr := con.pattr;
					con := con.next
				END;
				IF con # NIL THEN Next(ch) ELSE ch := 0X END
			ELSIF ch = "\" THEN
				Next(ch); i := 0; val := 0;
				IF ch # "'" THEN FlushCP(); END;
				IF (ch >= "a") & (ch <= "z") OR (ch >= "A") & (ch <= "Z") THEN
					WHILE (ch >= "a") & (ch <= "z") OR (ch >= "A") & (ch <= "Z") DO comm[i] := ch; INC(i); Next(ch) END;
					comm[i] := 0X; fact := 1; hasNum := FALSE;
					IF ch = "-" THEN fact := -1; Next(ch) END;
					WHILE (ch >= "0") & (ch <= "9") DO
						val := 10 * val + ORD(ch) - ORD("0"); Next(ch); hasNum := TRUE
					END;
					val := val * fact;
					IF ch = " " THEN Next(ch) END;
					(* special characters *)
					IF skipCnt > 0 THEN DEC(skipCnt)	(* command skipped as single character *)
					ELSIF comm = "tab" THEN Write(TAB)
					ELSIF comm = "line" THEN Write(CR)
					ELSIF comm = "par" THEN Paragraph
					ELSIF comm = "sect" THEN Paragraph
					ELSIF comm =  "ldblquote" THEN Write(201CX) (* unicode: left double quote *)
					ELSIF comm = "rdblquote" THEN Write(201DX) (* unicode: right double quote *)
					ELSIF comm = "lquote" THEN Write(2018X) (* unicode: left single quote *)
					ELSIF comm = "rquote" THEN Write(2019X) (* unicode: right single quote *)
					ELSIF comm = "enspace" THEN Write(2002X) (* unicode: en space *)
					ELSIF comm = "emspace" THEN Write(2003X) (* unicode: em space *)
					ELSIF comm = "endash" THEN Write(2013X) (* unicode: en dash *)
					ELSIF comm = "emdash" THEN Write(2014X) (* unicode: em dash *)
					ELSIF comm = "page" THEN
						Paragraph; NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {TextRulers.pageBreak}; p.opts.mask := p.opts.val;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					(* character attributes *)
					ELSIF comm = "plain" THEN
						font := Font(defFont);
						wr.SetAttr(TextModels.NewWeight(wr.attr, Fonts.normal));
						wr.SetAttr(TextModels.NewStyle(wr.attr, {}));
						wr.SetAttr(TextModels.NewTypeface(wr.attr, font.f));
						wr.SetAttr(TextModels.NewSize(wr.attr, 24 * halfpoint));
						wr.SetAttr(TextModels.NewColor(wr.attr, Ports.defaultColor));
						wr.SetAttr(TextModels.NewOffset(wr.attr, 0))
					ELSIF comm = "b" THEN
						IF hasNum & (val = 0) THEN wr.SetAttr(TextModels.NewWeight(wr.attr, Fonts.normal))
						ELSE wr.SetAttr(TextModels.NewWeight(wr.attr, Fonts.bold))
						END
					ELSIF comm = "i" THEN
						IF hasNum & (val = 0) THEN
							wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style - {Fonts.italic}))
						ELSE wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style + {Fonts.italic}))
						END
					ELSIF comm = "ul" THEN
						IF hasNum & (val = 0) THEN
							wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style - {Fonts.underline}))
						ELSE wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style + {Fonts.underline}))
						END
					ELSIF comm = "ulnone" THEN	(* stops all underlining *)
						wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style - {Fonts.underline}))
					ELSIF comm = "strike" THEN
						IF hasNum & (val = 0) THEN
							wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style - {Fonts.strikeout}))
						ELSE wr.SetAttr(TextModels.NewStyle(wr.attr, wr.attr.font.style + {Fonts.strikeout}))
						END
					ELSIF comm = "super" THEN
						superOrSubCtx := con;
						wr.SetAttr(TextModels.NewOffset(wr.attr, (wr.attr.font.size * 4 + 5) DIV 10));
						wr.SetAttr(TextModels.NewSize(wr.attr, (wr.attr.font.size * 8 + 5) DIV 10))
					ELSIF comm = "sub" THEN
						superOrSubCtx := con;
						wr.SetAttr(TextModels.NewOffset(wr.attr, -(wr.attr.font.size * 4 + 5) DIV 10));
						wr.SetAttr(TextModels.NewSize(wr.attr, (wr.attr.font.size * 8 + 5) DIV 10))
					(* fonts *)
					ELSIF comm = "f" THEN
						IF ~hasNum THEN val := defFont END;
						IF dest = fonttab THEN 
							fnum := val; idx := 0; NEW(font); font.id := val; font.codePage := ansiCodePage;
							font.next := fonts; fonts := font
						ELSE 
							font := Font(val); wr.SetAttr(TextModels.NewTypeface(wr.attr, Font(val).f))
						END
					ELSIF comm = 'fcharset' THEN
						IF hasNum & (dest = fonttab) THEN
							CASE val OF
							| 0: font.codePage := 1252	(* Latin-1 subset of Unicode *)
							| 1: font.codePage := WinApi.CP_ACP	(* Windows default code page *)
							| 2: font.codePage := 42	(* Symbol *)
							| 77: font.codePage := 1252	(*  Mac -> Western European  *)
							| 78: font.codePage := 10001	(* Mac Shift Jis  *)
							| 79: font.codePage := 10003	(*  Mac Hangul  *)
							| 80: font.codePage := 10008	(*  Mac GB2312  *)
							| 81: font.codePage := 10002	(*  Mac Big5  *)
							(* | 82:	Mac Johab (old)  *)
							| 83: font.codePage := 10005	(*  Mac Hebrew  *)
							| 84: font.codePage := 10004	(*  Mac Arabic  *)
							| 85: font.codePage := 10006	(*  Mac Greek  *)
							| 86: font.codePage := 10081	(*  Mac Turkish  *)
							| 87: font.codePage := 10021	(*  Mac Thai  *)
							| 88: font.codePage := 10029	(*  Mac East Europe  *)
							| 89: font.codePage := 10007	(*  Mac Russian  *)
							| 128: font.codePage := 932	(*  Shift Jis -> Japanese  *)
							| 129: font.codePage := 949	(*  Hangul -> Korean  *)
							| 130: font.codePage := 1361	(*  Johab -> Johab  *)
							| 134: font.codePage := 936	(*  GB2312 -> Simplified Chinese  *)
							| 136: font.codePage := 950	(*  Big5 -> Traditional Chinese   *)
							| 161: font.codePage := 1253	(*  Greek -> Greek  *)
							| 162: font.codePage := 1254	(*  Turkish -> Turkish  *)
							| 163: font.codePage := 1258	(*  Vietnamese -> Vietnamese  *)
							| 177: font.codePage := 1255	(*  Hebrew -> Hebrew  *)
							| 178: font.codePage := 1256	(*  Arabic - Arabic  *)
							| 179: font.codePage := 1256	(*  Arabic Traditional - Arabic  *)
							| 180: font.codePage := 1256	(*  Arabic user - Arabic  *)
							| 181: font.codePage := 1255	(*  Hebrew user -> Hebrew  *)
							| 186: font.codePage := 1257	(*  Baltic -> Baltic  *)
							| 204: font.codePage := 1251	(*  Russian -> Windows 3.1 (Cyrillic)  *)
							| 222: font.codePage := 874	(*  Thai -> Thai  *)
							| 238: font.codePage := 1250	(*  Eastern European -> Windows 3.1 (Eastern European)  *)
							| 254: font.codePage := 437	(*  PC 437 ->  437  United States IBM  *)
							END
						END
					ELSIF comm = 'cpg' THEN
						IF hasNum & (dest = fonttab) THEN font.codePage := val END
					ELSIF comm = "fs" THEN
						IF ~hasNum THEN val := 24 END;
						IF superOrSubCtx # NIL THEN (* scale down *)
							wr.SetAttr(TextModels.NewSize(wr.attr, (val * halfpoint * 8 + 5) DIV 10))
						ELSE
							wr.SetAttr(TextModels.NewSize(wr.attr, val * halfpoint))
						END
					ELSIF comm = "cf" THEN
						wr.SetAttr(TextModels.NewColor(wr.attr, Color(val).c))
					ELSIF comm = "dn" THEN
						IF ~hasNum THEN val := 6 END;
						wr.SetAttr(TextModels.NewOffset(wr.attr, -(val * halfpoint)))
					ELSIF comm = "up" THEN
						IF ~hasNum THEN val := 6 END;
						wr.SetAttr(TextModels.NewOffset(wr.attr, val * halfpoint))
					(* hyperlinks *)
					ELSIF comm = "fldinst" THEN	(*  field instruction  *)
						dest := fieldinstr; idx2 := 0; fieldStr[idx2] := 0X;
					ELSIF comm = "fldrslt" THEN	(*  field result  *)
						dest := text;
						Trim(fieldStr);
						fieldStr[9] := 0X;
						IF fieldStr = "HYPERLINK" THEN
							(* link target is assumed to be enclosed in quotes *)
							FOR idx2 := 11 TO LEN(fieldStr) - 1 DO fieldStr[idx2-11] := fieldStr[idx2] END; 
							fieldStr[LEN(fieldStr$)-1] := 0X;
							fieldStr := linkcmd + "('" + fieldStr + "')";
							v := StdLinks.dir.NewLink(fieldStr); wr.WriteView(v, Views.undefined, Views.undefined);
							v := StdLinks.dir.NewLink(''); wr.WriteView(v, Views.undefined, Views.undefined);
							wr.SetPos(wr.Base().Length()-1);
							hyperlinkCtx := con
						END;
					(* paragraph attributes *)
					ELSIF comm = "pard" THEN
						IF tableCtx = NIL THEN
							pattr := defRuler.style.attr; tabStyle := {}
						END
					ELSIF comm = "fi" THEN
						NEW(p);
						p.valid := {TextRulers.first}; p.first := pattr.left + val * twips;
						IF p.first < 0 THEN	(* change left indent to make the value legal *)
							p.valid := {TextRulers.left, TextRulers.first};
							p.left := pattr.left - p.first; p.first := 0
						END;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "li" THEN
						NEW(p);
						p.valid := {TextRulers.left, TextRulers.first};
						p.left := val * twips; p.first := p.left + pattr.first - pattr.left;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "ql" THEN
						NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {TextRulers.leftAdjust};
						p.opts.mask := {TextRulers.leftAdjust, TextRulers.rightAdjust};
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "qr" THEN
						NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {TextRulers.rightAdjust};
						p.opts.mask := {TextRulers.leftAdjust, TextRulers.rightAdjust};
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "qc" THEN
						NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {};
						p.opts.mask := {TextRulers.leftAdjust, TextRulers.rightAdjust};
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "qj" THEN
						NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {TextRulers.leftAdjust, TextRulers.rightAdjust};
						p.opts.mask := {TextRulers.leftAdjust, TextRulers.rightAdjust};
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "sb" THEN
						NEW(p);
						p.valid := {TextRulers.lead}; p.lead := val * twips;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "sl" THEN
						NEW(p);
						p.valid := {TextRulers.grid}; p.grid := val * twips;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "tqc" THEN
						tabStyle := {TextRulers.centerTab}
					ELSIF comm = "tqr" THEN
						tabStyle := {TextRulers.rightTab}
					ELSIF comm = "tqdec" THEN
						tabStyle := {TextRulers.centerTab, TextRulers.rightTab}
					ELSIF comm = "tb" THEN
						p0 := pattr.Prop(); p := p0(TextRulers.Prop);
						p.valid := {TextRulers.tabs};
						p.tabs.tab[p.tabs.len].stop := val * twips; 
						p.tabs.tab[p.tabs.len].type := {TextRulers.barTab}; tabStyle := {}; 
						INC(p.tabs.len);
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "tx" THEN
						p0 := pattr.Prop(); p := p0(TextRulers.Prop);
						p.valid := {TextRulers.tabs};
						p.tabs.tab[p.tabs.len].stop := val * twips; 
						p.tabs.tab[p.tabs.len].type := tabStyle; tabStyle := {}; 
						INC(p.tabs.len);
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "pagebb" THEN
						NEW(p);
						p.valid := {TextRulers.opts}; p.opts.val := {TextRulers.pageBreak}; p.opts.mask := p.opts.val;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					(* table attributes *)
					ELSIF comm = "trowd" THEN	(* start of table row *)
						pattr := defRuler.style.attr; tabStyle := {}; tableCtx := con; trgaph := 0;
						NEW(p);
						p.valid := {TextRulers.first, TextRulers.left, TextRulers.opts, TextRulers.tabs};
						p.opts.mask := {TextRulers.leftAdjust, TextRulers.rightAdjust, TextRulers.pageBreak};
						p.first := 0; p.left := 0; p.opts.val := {TextRulers.leftAdjust}; p.tabs.len := 0;
						pattr := TextRulers.ModifiedAttr(pattr, p);
					ELSIF comm = "trgaph" THEN	(* half the space between the cells of a table row in twips. *)
						trgaph := val;
					ELSIF comm = "trleft" THEN	(* left offset of the table row *)
						NEW(p);
						p.valid := {TextRulers.left, TextRulers.first};
						p.left := (val + trgaph) * twips; p.first := p.left;
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "cellx" THEN	(* right boundary of a table cell in twips, including its half of 
							the space between cells; used to add a Tab to the paragraph ruler *)
						p0 := pattr.Prop(); p := p0(TextRulers.Prop);
						p.valid := {TextRulers.tabs};
						p.tabs.tab[p.tabs.len].stop := (val + trgaph) * twips; 
						p.tabs.tab[p.tabs.len].type := {}; 
						INC(p.tabs.len);
						pattr := TextRulers.ModifiedAttr(pattr, p)
					ELSIF comm = "intbl" THEN	(* paragraph is part of a table *)
					ELSIF comm = "cell" THEN	(* end of cell *)
						wr.WriteChar(TAB)
					ELSIF comm = "row" THEN	(* end of row *)
						Paragraph();
						pattr := defRuler.style.attr; tabStyle := {}; tableCtx := NIL;
					(* header *)
					ELSIF comm = "deff" THEN
						IF hasNum THEN defFont := val END
					ELSIF comm = "fonttbl" THEN
						IF dest # skip THEN dest := fonttab END
					ELSIF comm = "colortbl" THEN
						IF dest # skip THEN dest := colortab; cnum := 0; SetColor(0, 0) END
					ELSIF comm = "red" THEN
						IF dest = colortab THEN SetColor(cnum, Color(cnum).c + val MOD 256) END
					ELSIF comm = "green" THEN
						IF dest = colortab THEN SetColor(cnum, Color(cnum).c + val MOD 256 * 256) END
					ELSIF comm = "blue" THEN
						IF dest = colortab THEN SetColor(cnum, Color(cnum).c + val MOD 256 *  65536) END
					ELSIF comm = "rtf" THEN
					ELSIF comm = "ansi" THEN
					ELSIF comm = "lang" THEN
					ELSIF comm = "langfe" THEN
					ELSIF comm = "loch" THEN
					ELSIF comm = "ltrch" THEN
					ELSIF comm = "rtlch" THEN
					ELSIF comm = "ansicpg" THEN ansiCodePage := val
					ELSIF comm = "hich" THEN
					ELSIF comm = "dbch" THEN
					(* misc *)
					ELSIF comm = "bin" THEN rd.SetPos(rd.Pos() + val - 1); Next(ch)
					(* unicode *)
					ELSIF comm = "u" THEN Write(CHR(val)); skipCnt := uniCnt
					ELSIF comm = "uc" THEN IF hasNum THEN uniCnt := val END
					ELSIF comm = "upr" THEN dest := skip	(* skip ANSI part *)
					ELSIF comm = "ud" THEN dest := text	(* use Unicode part *)
					ELSIF (comm = "fbimajor") OR (comm = "fbiminor") OR (comm = "fdbmajor")
						OR (comm = "fdbminor") OR (comm = "fhimajor") OR (comm = "fhiminor")
						OR (comm = "flomajor") OR (comm = "flominor") THEN dest := fonttab
					(* unhandled destinations *)
					ELSIF comm = "author" THEN dest := skip
					ELSIF comm = "buptim" THEN dest := skip
					ELSIF comm = "comment" THEN dest := skip
					ELSIF comm = "creatim" THEN dest := skip
					ELSIF comm = "doccomm" THEN dest := skip
					ELSIF comm = "footer" THEN dest := skip
					ELSIF comm = "footerl" THEN dest := skip
					ELSIF comm = "footerr" THEN dest := skip
					ELSIF comm = "footerf" THEN dest := skip
					ELSIF comm = "footnote" THEN dest := skip
					ELSIF comm = "ftnsep" THEN dest := skip
					ELSIF comm = "ftnsepc" THEN dest := skip
					ELSIF comm = "ftncn" THEN dest := skip
					ELSIF comm = "header" THEN dest := skip
					ELSIF comm = "headerl" THEN dest := skip
					ELSIF comm = "headerr" THEN dest := skip
					ELSIF comm = "headerf" THEN dest := skip
					ELSIF comm = "info" THEN dest := skip
					ELSIF comm = "keywords" THEN dest := skip
					ELSIF comm = "object" THEN dest := skip
					ELSIF comm = "operator" THEN dest := skip
					ELSIF comm = "pict" THEN dest := skip
					ELSIF comm = "printim" THEN dest := skip
					ELSIF comm = "private1" THEN dest := skip
					ELSIF comm = "revtim" THEN dest := skip
					ELSIF comm = "rxe" THEN dest := skip
					ELSIF comm = "stylesheet" THEN dest := skip
					ELSIF comm = "subject" THEN dest := skip
					ELSIF comm = "tc" THEN dest := skip
					ELSIF comm = "title" THEN dest := skip
					ELSIF comm = "txe" THEN dest := skip
					ELSIF comm = "xe" THEN dest := skip
					ELSE (* unknown *)
						IF skipDest & (con # NIL) & (con.next # NIL) THEN dest := skip END
					END;
					skipDest := FALSE
				ELSIF ch = "'" THEN
					Next(ch);
					IF ch <= "9" THEN val := ORD(ch) - ORD("0") ELSE val := ORD(CAP(ch)) - ORD("A") + 10 END;
					Next(ch);
					IF ch <= "9" THEN val := 16 * val + ORD(ch) - ORD("0")
					ELSE val := 16 * val + ORD(CAP(ch)) - ORD("A") + 10
					END;
					WriteCP(SHORT(CHR(val))); Next(ch)
				ELSE
					IF ch = "~" THEN Write(0A0X)	(* nonbreaking space *)
					ELSIF ch = "-" THEN Write(0ADX)	(* soft hyphen *)
					ELSIF ch = "_" THEN Write(2011X)	(* nonbreaking hyphen *)
					ELSIF ch = "*" THEN skipDest := TRUE
					ELSIF (ch = LF) OR (ch = CR) THEN Paragraph
					ELSIF (ch = "{") OR (ch = "}") OR (ch = "\") THEN Write(ch)
					END;
					Next(ch)
				END
			ELSIF ch = ";" THEN FlushCP;
				IF dest = fonttab THEN font := Font(fnum); ASSERT(font.id # noFontId, 100); font.f[idx] := 0X; INC(idx)
				ELSIF dest = colortab THEN INC(cnum); SetColor(cnum, 0)
				ELSIF dest = text THEN Write(";")
				END;
				Next(ch)
			ELSIF (ch >= " ") OR (ch = TAB) THEN
				Write(ch); Next(ch)
			ELSE 
				Next(ch)
			END
		END
	END ParseRichText;
	
	PROCEDURE ConvertToRichText (in: TextViews.View; beg, end: INTEGER; VAR out: TextModels.Model);
		VAR r: TextModels.Reader; w: TextMappers.Formatter; ch: CHAR; f: Fonts.Font;
			attr, attr0: TextModels.Attributes; col: Ports.Color; tf, atf: Fonts.Typeface; p, size, asize, offs: INTEGER;
			style, astyle: SET; weight, aweight: INTEGER; rattr, rattr0: TextRulers.Attributes; ruler: TextRulers.Ruler;
			text: TextModels.Model; firstLine, firstLine0: BOOLEAN; fonts: ARRAY 256 OF Fonts.Typeface;
			colors: ARRAY 256 OF Ports.Color; fnum, cnum, i: INTEGER;
	BEGIN
		out := TextModels.dir.New(); w.ConnectTo(out);
		f := Fonts.dir.Default(); tf := f.typeface;
		fnum := 1; fonts[0] := tf;
		cnum := 1; colors[0] := Ports.defaultColor;
		col := Ports.defaultColor; size := 12 * Ports.point;
		offs := 0; style := {}; weight := Fonts.normal;
		attr0 := NIL; rattr0 := NIL; firstLine := TRUE; firstLine0 := FALSE;
		text := in.ThisModel(); r := text.NewReader(NIL);
		ruler := TextViews.ThisRuler(in, beg); rattr := ruler.style.attr;
		r.SetPos(beg); r.ReadChar(ch);
		WHILE ~r.eot & (r.Pos() <= end) DO
			attr := r.attr;
			IF (r.view # NIL) & (r.view IS TextRulers.Ruler) THEN
				ruler := r.view(TextRulers.Ruler); rattr := ruler.style.attr;
				firstLine := TRUE
			ELSIF ch = FF THEN firstLine := TRUE
			END;
			IF (rattr # rattr0) OR (firstLine # firstLine0) THEN
				IF (rattr # rattr0) OR (rattr.first # rattr.left) OR (rattr.lead # 0) OR (TextRulers.pageBreak IN rattr.opts)
				THEN
					w.WriteSString("\pard");
					IF rattr.left # 0 THEN
						w.WriteSString("\li"); w.WriteInt(rattr.left DIV twips)
					END;
					IF firstLine & (rattr.first # rattr.left) THEN
						w.WriteSString("\fi"); w.WriteInt((rattr.first - rattr.left) DIV twips)
					END;
					IF firstLine & (rattr.lead # 0) THEN
						w.WriteSString("\sb"); w.WriteInt(rattr.lead DIV twips)
					END;
					IF rattr.grid > Ports.point THEN
						w.WriteSString("\sl"); w.WriteInt(rattr.grid DIV twips); w.WriteSString("\slmult1")
					END;
					IF {TextRulers.leftAdjust, TextRulers.rightAdjust} - rattr.opts = {} THEN w.WriteSString("\qj")
					ELSIF TextRulers.rightAdjust IN rattr.opts THEN w.WriteSString("\qr")
					ELSIF ~(TextRulers.leftAdjust IN rattr.opts) THEN w.WriteSString("\qc")
					END;
					IF firstLine & (TextRulers.pageBreak IN rattr.opts) THEN
						w.WriteSString("\pagebb")
					END;
					i := 0;
					WHILE i < rattr.tabs.len DO
						IF TextRulers.centerTab IN rattr.tabs.tab[i].type THEN
							IF TextRulers.rightTab IN rattr.tabs.tab[i].type THEN w.WriteSString("\tqdec") 
							ELSE w.WriteSString("\tqc")
							END
						ELSIF TextRulers.rightTab IN rattr.tabs.tab[i].type THEN w.WriteSString("\tqr")
						END; 
						IF TextRulers.barTab IN rattr.tabs.tab[i].type THEN w.WriteSString("\tb") END; 
						w.WriteSString("\tx"); w.WriteInt(rattr.tabs.tab[i].stop DIV twips);
						INC(i)
					END;
					w.WriteChar(" ")
				END;
				rattr0 := rattr; firstLine0 := firstLine
			END;
			IF attr # attr0 THEN
				p := w.Pos();
				IF attr.color # col THEN
					i := 0; WHILE (i < cnum) & (colors[i] # attr.color) DO INC(i) END;
					IF i = cnum THEN colors[i] := attr.color; INC(cnum) END;
					w.WriteSString("\cf"); w.WriteInt(i);
					col := attr.color
				END;
				atf := attr.font.typeface$; asize := attr.font.size; astyle := attr.font.style; aweight := attr.font.weight;
				IF atf # tf THEN
					i := 0; WHILE (i < fnum) & (fonts[i] # atf) DO INC(i) END;
					IF i = fnum THEN fonts[i] := atf; INC(fnum) END;
					w.WriteSString("\f"); w.WriteInt(i);
					tf := atf
				END;
				IF asize # size THEN
					w.WriteSString("\fs"); w.WriteInt(asize DIV halfpoint);
					size := asize
				END;
				IF astyle # style THEN
					IF (Fonts.italic IN astyle) & ~(Fonts.italic IN style) THEN w.WriteSString("\i")
					ELSIF ~(Fonts.italic IN astyle) & (Fonts.italic IN style) THEN w.WriteSString("\i0")
					END;
					IF (Fonts.underline IN astyle) & ~(Fonts.underline IN style) THEN w.WriteSString("\ul")
					ELSIF ~(Fonts.underline IN astyle) & (Fonts.underline IN style) THEN w.WriteSString("\ul0")
					END;
					IF (Fonts.strikeout IN astyle) & ~(Fonts.strikeout IN style) THEN w.WriteSString("\strike")
					ELSIF ~(Fonts.strikeout IN astyle) & (Fonts.strikeout IN style) THEN w.WriteSString("\strike0")
					END;
					style := astyle
				END;
				IF aweight # weight THEN
					IF (aweight > Fonts.normal) & (weight = Fonts.normal) THEN w.WriteSString("\b")
					ELSIF (aweight = Fonts.normal) & (weight > Fonts.normal) THEN w.WriteSString("\b0")
					END;
					weight := aweight
				END;
				IF attr.offset # offs THEN
					IF attr.offset > 0 THEN w.WriteSString("\up"); w.WriteInt(attr.offset DIV halfpoint)
					ELSIF attr.offset < 0 THEN w.WriteSString("\dn"); w.WriteInt(-(attr.offset DIV halfpoint))
					ELSIF offs > 0 THEN w.WriteSString("\up0")
					ELSE w.WriteSString("\dn0")
					END;
					offs := attr.offset
				END;
				IF w.Pos() # p THEN w.WriteChar(" ") END;
				attr0 := attr
			END;
			IF ch >= 100X THEN
				IF ch = 2002X THEN w.WriteSString("\enspace ")
				ELSIF ch = 2003X THEN w.WriteSString("\emspace ")
				ELSIF ch = 2013X THEN w.WriteSString("\endash ")
				ELSIF ch = 2014X THEN w.WriteSString("\emdash ")
				ELSIF ch = 2010X THEN w.WriteChar("-")
				ELSIF ch = 2011X THEN w.WriteSString("\_")
				ELSIF ch = 201CX THEN (* unicode: left double quote *) w.WriteSString("\ldblquote ")
				ELSIF ch = 201DX THEN (* unicode: right double quote *) w.WriteSString("\rdblquote ")
				ELSIF ch = 2018X THEN (* unicode: left single quote *) w.WriteSString("\lquote ")
				ELSIF ch = 2019X THEN (* unicode: right single quote *) w.WriteSString("\rquote ")					
				ELSE
					w.WriteSString("\u"); w.WriteInt(ORD(ch)); 
					ch := ThisWndChar(ch);
					IF ch >= 80X THEN
						w.WriteSString("\'"); 
						w.WriteIntForm(ORD(ch), TextMappers.hexadecimal, 2, "0", FALSE)
					ELSE
						w.WriteChar(ch)
					END
				END
			ELSE
				CASE ch OF 
				| TAB: w.WriteSString("\tab ")
				| CR: w.WriteSString("\par "); w.WriteLn; firstLine := FALSE
				| " ".."[", "]".."z", "|", "~": w.WriteChar(ch)
				| "\": w.WriteSString("\\")
				| "{": w.WriteSString("\{")
				| "}": w.WriteSString("\}")
				| 8FX: (* digit space *) w.WriteChar(" ")
				| 90X: (* hyphen *) w.WriteChar("-")
				| 91X: (* non-breaking hyphen *) w.WriteSString("\_")
				| 0A0X: (* non-breaking space *) w.WriteSString("\~")
				| 0ADX: (* soft hyphen *) w.WriteSString("\-")
				| 0A1X..0ACX, 0AEX..0FFX:
					w.WriteSString("\'"); w.WriteIntForm(ORD(ch), TextMappers.hexadecimal, 2, "0", FALSE)
				ELSE
				END
			END;
			r.ReadChar(ch)
		END;
		w.WriteChar("}");
		(* header *)
		w.SetPos(0);
		w.WriteSString("{\rtf1\ansi\ansicpg1252\deff0");
		w.WriteSString("{\fonttbl"); i := 0;
		WHILE i < fnum DO
			IF fonts[i] = Fonts.default THEN fonts[i] := HostFonts.defFont.alias$ END;
			w.WriteSString("{\f"); w.WriteInt(i); w.WriteSString("\fnil "); w.WriteString(fonts[i]); w.WriteSString(";}");
			INC(i)
		END;
		w.WriteChar("}"); w.WriteLn;
		w.WriteSString("{\colortbl;"); i := 1;
		WHILE i < cnum DO
			w.WriteSString("\red"); w.WriteInt(colors[i] MOD 256);
			w.WriteSString("\green"); w.WriteInt(colors[i] DIV 256 MOD 256);
			w.WriteSString("\blue"); w.WriteInt(colors[i] DIV 65536 MOD 256);
			w.WriteChar(";"); INC(i)
		END;
		w.WriteChar("}"); w.WriteLn;
		w.WriteSString("\deftab216 ");
		w.WriteSString("\plain")
	END ConvertToRichText;
	
	
	PROCEDURE ImportDText* (VAR med: WinOle.STGMEDIUM; OUT v: Views.View;
											OUT w, h: INTEGER; OUT isSingle: BOOLEAN);
		VAR t: TextModels.Model; res, adr: INTEGER; wr: TextModels.Writer; ch: SHORTCHAR;
			hnd: WinApi.HANDLE; attr: TextModels.Attributes; p: Properties.StdProp; pref: Properties.BoundsPref;
	BEGIN
		hnd := MediumGlobal(med);
		ASSERT(hnd # 0, 20);
		adr := WinApi.GlobalLock(hnd);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		IF HostClipboard.cloneAttributes THEN
			Properties.CollectStdProp(p);
			NEW(attr); attr.InitFromProp(p);
			wr.SetAttr(attr)
		END;
		SYSTEM.GET(adr, ch);
		WHILE ch # 0X DO
			WriteWndChar(wr, ch);
			INC(adr); SYSTEM.GET(adr, ch)
		END;
		res := WinApi.GlobalUnlock(hnd);
		v := TextViews.dir.New(t);
		pref.w := Views.undefined; pref.h := Views.undefined;
		Views.HandlePropMsg(v, pref);
		w := pref.w; h := pref.h; isSingle := FALSE
	END ImportDText;
		
	PROCEDURE ImportDRichText* (VAR med: WinOle.STGMEDIUM; OUT v: Views.View;
												OUT w, h: INTEGER; OUT isSingle: BOOLEAN);
		VAR t: TextModels.Model; res, adr: INTEGER; wr: TextModels.Writer; rd: MemReader;
			hnd: WinApi.HANDLE; ruler: TextRulers.Ruler; pref: Properties.BoundsPref;
	BEGIN
		IF debug THEN
			ImportDText(med, v, w, h, isSingle);
			RETURN
		END;		
		hnd := MediumGlobal(med);
		ASSERT(hnd # 0, 20);
		adr := WinApi.GlobalLock(hnd);
		NEW(rd); rd.adr := adr; rd.pos := 0;
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		ParseRichText(rd, wr, ruler);
		res := WinApi.GlobalUnlock(hnd);
		v := TextViews.dir.New(t);
		v(TextViews.View).SetDefaults(ruler, TextModels.dir.attr);
		pref.w := Views.undefined; pref.h := Views.undefined;
		Views.HandlePropMsg(v, pref);
		w := pref.w; h := pref.h; isSingle := FALSE
	END ImportDRichText;
	
	PROCEDURE ImportDUnicode* (VAR med: WinOle.STGMEDIUM; OUT v: Views.View;
												OUT w, h: INTEGER; OUT isSingle: BOOLEAN);
		VAR t: TextModels.Model; res, adr: INTEGER; wr: TextModels.Writer; uc, prev: CHAR;
			hnd: WinApi.HANDLE; attr: TextModels.Attributes; p: Properties.StdProp; pref: Properties.BoundsPref;
	BEGIN
		hnd := MediumGlobal(med);
		ASSERT(hnd # 0, 20);
		adr := WinApi.GlobalLock(hnd);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		IF HostClipboard.cloneAttributes THEN
			Properties.CollectStdProp(p);
			NEW(attr); attr.InitFromProp(p);
			wr.SetAttr(attr)
		END;
		prev := 0X;
		SYSTEM.GET(adr, uc);
		WHILE uc # 0X DO
			ASSERT(uc # 0FFFEX, 100);
			IF (uc = LF) & (prev # CR) THEN
				WriteWndChar(wr, CR)
			ELSIF uc < 100X THEN
				WriteWndChar(wr, uc)
			ELSIF uc # 0FEFFX THEN
				wr.WriteChar(uc)
			END;
			prev := uc;
			INC(adr, 2); SYSTEM.GET(adr, uc);
		END;
		res := WinApi.GlobalUnlock(hnd);
		v := TextViews.dir.New(t);
		pref.w := Views.undefined; pref.h := Views.undefined;
		Views.HandlePropMsg(v, pref);
		w := pref.w; h := pref.h; isSingle := FALSE
	END ImportDUnicode;

	PROCEDURE ExportDText* (
		v: Views.View; w, h, x, y: INTEGER; isSingle: BOOLEAN; VAR med: WinOle.STGMEDIUM
	);
		VAR t: TextModels.Model; r: TextModels.Reader; ch: CHAR;
			res, len, adr: INTEGER; hnd: WinApi.HANDLE;
	BEGIN
		ASSERT(v # NIL, 20);
		WITH v: TextViews.View DO
			t := v.ThisModel();
			hnd := WinApi.GlobalAlloc({1, 13}, 2 * t.Length() + 1);	(* movable, sharable *)
			IF hnd # 0 THEN
				adr  := WinApi.GlobalLock(hnd); len := 0;
				r := t.NewReader(NIL); r.ReadChar(ch);
				WHILE ~r.eot DO
					IF (ch # TextModels.viewcode) & (ch # TextModels.para) THEN
						ch := ThisWndChar(ch);
						SYSTEM.PUT(adr, SHORT(ch)); INC(adr); INC(len);
						IF ch = CR THEN SYSTEM.PUT(adr, LF); INC(adr); INC(len) END
					END;
					r.ReadChar(ch)
				END;
				SYSTEM.PUT(adr, 0X); INC(len);
				res := WinApi.GlobalUnlock(hnd);
				hnd := WinApi.GlobalReAlloc(hnd, len, {});
				GenGlobalMedium(hnd, NIL, med)
			END
		ELSE
		END
	END ExportDText;
	
	PROCEDURE ExportDRichText* (
		v: Views.View; w, h, x, y: INTEGER; isSingle: BOOLEAN; VAR med: WinOle.STGMEDIUM
	);
		VAR t: TextModels.Model; r: TextModels.Reader; ch: CHAR; res, adr: INTEGER; hnd: WinApi.HANDLE;
	BEGIN
		ASSERT(v # NIL, 20);
		WITH v: TextViews.View DO
			ConvertToRichText(v, 0, MAX(INTEGER), t);
			hnd := WinApi.GlobalAlloc({1, 13}, t.Length() + 1);	(* movable, sharable *)
			IF hnd # 0 THEN
				adr := WinApi.GlobalLock(hnd);
				r := t.NewReader(NIL); r.ReadChar(ch);
				WHILE ~r.eot DO
					SYSTEM.PUT(adr, SHORT(ch)); INC(adr);
					r.ReadChar(ch)
				END;
				SYSTEM.PUT(adr, 0X);
				res := WinApi.GlobalUnlock(hnd);
				GenGlobalMedium(hnd, NIL, med)
			END
		ELSE
		END
	END ExportDRichText;
	
	PROCEDURE ExportDUnicode* (
		v: Views.View; w, h, x, y: INTEGER; isSingle: BOOLEAN; VAR med: WinOle.STGMEDIUM
	);
		VAR t: TextModels.Model; r: TextModels.Reader; ch: CHAR; res, len, adr: INTEGER; hnd: WinApi.HANDLE;
	BEGIN
		ASSERT(v # NIL, 20);
		WITH v: TextViews.View DO
			t := v.ThisModel();
			hnd := WinApi.GlobalAlloc({1, 13}, 4 * t.Length() + 2);	(* movable, sharable *)
			IF hnd # 0 THEN
				adr  := WinApi.GlobalLock(hnd); len := 0;
				r := t.NewReader(NIL); r.ReadChar(ch);
				WHILE ~r.eot DO
					IF ch = CR THEN
						SYSTEM.PUT(adr, LONG(CR)); INC(adr, 2); INC(len, 2);
						SYSTEM.PUT(adr, LONG(LF)); INC(adr, 2); INC(len, 2)
					ELSIF (ch >= " ") OR (ch = TAB) THEN
						IF (ch >= 0EF00X) & (ch <= 0EFFFX) THEN ch := CHR(ORD(ch) - 0EF00H) END;
						SYSTEM.PUT(adr, ch); INC(adr, 2); INC(len, 2)
					END;
					r.ReadChar(ch)
				END;
				SYSTEM.PUT(adr, LONG(0X)); INC(len, 2);
				res := WinApi.GlobalUnlock(hnd);
				hnd := WinApi.GlobalReAlloc(hnd, len, {});
				GenGlobalMedium(hnd, NIL, med)
			END
		ELSE
		END
	END ExportDUnicode;


	PROCEDURE ImportRichText* (f: Files.File; OUT s: Stores.Store);
		VAR t: TextModels.Model; wr: TextModels.Writer; rd: Files.Reader; ruler: TextRulers.Ruler;
	BEGIN
		rd := f.NewReader(NIL); rd.SetPos(0);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		ParseRichText(rd, wr, ruler);
		s := TextViews.dir.New(t);
		s(TextViews.View).SetDefaults(ruler, TextModels.dir.attr)
	END ImportRichText;
	

	PROCEDURE TextView(s: Stores.Store): Stores.Store;
	BEGIN
		IF s IS Views.View THEN RETURN Properties.ThisType(s(Views.View), "TextViews.View")
		ELSE RETURN NIL
		END
	END TextView;

	PROCEDURE ExportRichText* (s: Stores.Store; f: Files.File);
		VAR t: TextModels.Model; r: TextModels.Reader; ch: CHAR; w: Stores.Writer;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21);
		WITH s: TextViews.View DO
			ConvertToRichText(s, 0, MAX(INTEGER), t);
			w.ConnectTo(f); w.SetPos(0);
			r := t.NewReader(NIL); r.ReadChar(ch);
			WHILE ~r.eot DO
				w.WriteSChar(SHORT(ch)); r.ReadChar(ch)
			END
(*
			w.WriteSChar(0X)
*)
		ELSE
		END
	END ExportRichText;
	


END WinTextConv.
 