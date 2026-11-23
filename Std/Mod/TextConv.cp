MODULE StdTextConv;
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
		SYSTEM, 
		Files, Stores, Views, Properties,
		TextModels, TextViews, TextMappers;
	
	CONST
		CR = 0DX; LF = 0AX; TAB = 09X;

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
	
	PROCEDURE (r: MemReader) ReadBytes (VAR x: ARRAY OF BYTE; beg_, len_: INTEGER);
	BEGIN
		HALT(126)
	END ReadBytes;

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



	PROCEDURE ImportText* (f: Files.File; OUT s: Stores.Store);
		VAR r: Stores.Reader; t: TextModels.Model; wr: TextModels.Writer; ch, nch: SHORTCHAR;
	BEGIN
		ASSERT(f # NIL, 20);
		r.ConnectTo(f); r.SetPos(0);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		r.ReadSChar(ch);
		WHILE ~r.rider.eof DO
			r.ReadSChar(nch);
			IF (ch = CR) & (nch = LF) THEN r.ReadSChar(nch)
			ELSIF ch = LF THEN ch := CR
			END;
			WriteWndChar(wr, ch); ch := nch
		END;
		s := TextViews.dir.New(t)
	END ImportText;

	PROCEDURE ImportTabText* (f: Files.File; OUT s: Stores.Store);
		VAR r: Stores.Reader; t: TextModels.Model; wr: TextModels.Writer; ch, nch: SHORTCHAR;
	BEGIN
		ASSERT(f # NIL, 20);
		r.ConnectTo(f); r.SetPos(0);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		r.ReadSChar(ch);
		WHILE ~r.rider.eof DO
			r.ReadSChar(nch);
			IF (ch = CR) & (nch = LF) THEN r.ReadSChar(nch)
			ELSIF ch = LF THEN ch := CR
			ELSIF (ch = " ") & (nch = " ") THEN ch := TAB; r.ReadSChar(nch)
			END;
			WriteWndChar(wr, ch); ch := nch
		END;
		s := TextViews.dir.New(t)
	END ImportTabText;


	
	PROCEDURE ImportUnicode* (f: Files.File; OUT s: Stores.Store);
		VAR r: Stores.Reader; t: TextModels.Model; v: TextViews.View; w: TextModels.Writer;
			ch0, ch1: SHORTCHAR; len: INTEGER; uc: CHAR; rev: BOOLEAN;
	BEGIN
		ASSERT(f # NIL, 20);
		r.ConnectTo(f); r.SetPos(0);
		len := f.Length(); rev := FALSE;
		t := TextModels.dir.New(); w := t.NewWriter(NIL); w.SetPos(0);
		WHILE len > 0 DO
			r.ReadSChar(ch0); r.ReadSChar(ch1);
			IF rev THEN uc := CHR(ORD(ch1) + 256 * ORD(ch0))
			ELSE uc := CHR(ORD(ch0) + 256 * ORD(ch1))
			END;
			DEC(len, 2);
			IF uc = 0FFFEX THEN rev := ~rev
			ELSIF uc < 100X THEN WriteWndChar(w, uc)
			ELSIF uc # 0FEFFX THEN w.WriteChar(uc)
			END
		END;
		v := TextViews.dir.New(t);
		s := v
	END ImportUnicode;
	
	PROCEDURE ImportDosText* (f: Files.File; OUT s: Stores.Store);
		VAR r: Stores.Reader; t: TextModels.Model; wr: TextModels.Writer; ch, nch: SHORTCHAR;
		
		PROCEDURE ConvertChar (wr: TextModels.Writer; ch: CHAR);
		(* PC Code Page Mappings M4 (Latin) to Unicode Encoding *)
		(* Reference: The Unicode Standard, Version 1.0, Vol 1, Addison Wesley, p. 536 *)
		BEGIN		
			CASE ch OF
			| CR, TAB, " "..7EX: wr.WriteChar(ch)
			| LF:
			| 080X: wr.WriteChar(0C7X)
			| 081X: wr.WriteChar(0FCX)
			| 082X: wr.WriteChar(0E9X)
			| 083X: wr.WriteChar(0E2X)
			| 084X: wr.WriteChar(0E4X)
			| 085X: wr.WriteChar(0E0X)
			| 086X: wr.WriteChar(0E5X)
			| 087X: wr.WriteChar(0E7X)
			| 088X: wr.WriteChar(0EAX)
			| 089X: wr.WriteChar(0EBX)
			| 08AX: wr.WriteChar(0E8X)
			| 08BX: wr.WriteChar(0EFX)
			| 08CX: wr.WriteChar(0EEX)
			| 08DX: wr.WriteChar(0ECX)
			| 08EX: wr.WriteChar(0C4X)
			| 08FX: wr.WriteChar(0C5X)
			| 090X: wr.WriteChar(0C9X)
			| 091X: wr.WriteChar(0E6X)
			| 092X: wr.WriteChar(0C6X)
			| 093X: wr.WriteChar(0F4X)
			| 094X: wr.WriteChar(0F6X)
			| 095X: wr.WriteChar(0F2X)
			| 096X: wr.WriteChar(0FBX)
			| 097X: wr.WriteChar(0F9X)
			| 098X: wr.WriteChar(0FFX)
			| 099X: wr.WriteChar(0D6X)
			| 09AX: wr.WriteChar(0DCX)
			| 09BX: wr.WriteChar(0F8X)
			| 09CX: wr.WriteChar(0A3X)
			| 09DX: wr.WriteChar(0D8X)
			| 09EX: wr.WriteChar(0D7X)
			| 09FX: wr.WriteChar(0192X)
			| 0A0X: wr.WriteChar(0E1X)
			| 0A1X: wr.WriteChar(0EDX)
			| 0A2X: wr.WriteChar(0F3X)
			| 0A3X: wr.WriteChar(0FAX)
			| 0A4X: wr.WriteChar(0F1X)
			| 0A5X: wr.WriteChar(0D1X)
			| 0A6X: wr.WriteChar(0AAX)
			| 0A7X: wr.WriteChar(0BAX)
			| 0A8X: wr.WriteChar(0BFX)
			| 0A9X: wr.WriteChar(0AEX)
			| 0AAX: wr.WriteChar(0ACX)
			| 0ABX: wr.WriteChar(0BDX)
			| 0ACX: wr.WriteChar(0BCX)
			| 0ADX: wr.WriteChar(0A1X)
			| 0AEX: wr.WriteChar(0ABX)
			| 0AFX: wr.WriteChar(0BBX)
			| 0B5X: wr.WriteChar(0C1X)
			| 0B6X: wr.WriteChar(0C2X)
			| 0B7X: wr.WriteChar(0C0X)
			| 0B8X: wr.WriteChar(0A9X)
			| 0BDX: wr.WriteChar(0A2X)
			| 0BEX: wr.WriteChar(0A5X)
			| 0C6X: wr.WriteChar(0E3X)
			| 0C7X: wr.WriteChar(0C3X)
			| 0CFX: wr.WriteChar(0A4X)
			| 0D0X: wr.WriteChar(0F0X)
			| 0D1X: wr.WriteChar(0D0X)
			| 0D2X: wr.WriteChar(0CAX)
			| 0D3X: wr.WriteChar(0CBX)
			| 0D4X: wr.WriteChar(0C8X)
			| 0D5X: wr.WriteChar(0131X)
			| 0D6X: wr.WriteChar(0CDX)
			| 0D7X: wr.WriteChar(0CEX)
			| 0D8X: wr.WriteChar(0CFX)
			| 0DDX: wr.WriteChar(0A6X)
			| 0DEX: wr.WriteChar(0CCX)
			| 0E0X: wr.WriteChar(0D3X)
			| 0E1X: wr.WriteChar(0DFX)
			| 0E2X: wr.WriteChar(0D4X)
			| 0E3X: wr.WriteChar(0D2X)
			| 0E4X: wr.WriteChar(0F5X)
			| 0E5X: wr.WriteChar(0D5X)
			| 0E6X: wr.WriteChar(0B5X)
			| 0E7X: wr.WriteChar(0FEX)
			| 0E8X: wr.WriteChar(0DEX)
			| 0E9X: wr.WriteChar(0DAX)
			| 0EAX: wr.WriteChar(0DBX)
			| 0EBX: wr.WriteChar(0D9X)
			| 0ECX: wr.WriteChar(0FDX)
			| 0EDX: wr.WriteChar(0DDX)
			| 0EEX: wr.WriteChar(0AFX)
			| 0EFX: wr.WriteChar(0B4X)
			| 0F0X: wr.WriteChar(0ADX)
			| 0F1X: wr.WriteChar(0B1X)
			| 0F2X: wr.WriteChar(02017X)
			| 0F3X: wr.WriteChar(0BEX)
			| 0F4X: wr.WriteChar(0B6X)
			| 0F5X: wr.WriteChar(0A7X)
			| 0F6X: wr.WriteChar(0F7X)
			| 0F7X: wr.WriteChar(0B8X)
			| 0F8X: wr.WriteChar(0B0X)
			| 0F9X: wr.WriteChar(0A8X)
			| 0FAX: wr.WriteChar(0B7X)
			| 0FBX: wr.WriteChar(0B9X)
			| 0FCX: wr.WriteChar(0B3X)
			| 0FDX: wr.WriteChar(0B2X)
			| 0X..8X, 0BX, 0CX, 0EX..1FX, 7FX,
			  0B0X..0B4X, 0B9X..0BCX, 0BFX..0C5X, 0C8X..0CEX, 0D9X..0DCX, 0DFX, 0FEX, 0FFX:
				wr.WriteChar(CHR(0EF00H + ORD(ch)))
			END
		END ConvertChar;
		
	BEGIN
		ASSERT(f # NIL, 20);
		r.ConnectTo(f); r.SetPos(0);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL);
		r.ReadSChar(ch);
		WHILE ~r.rider.eof DO
			r.ReadSChar(nch);
			IF (ch = CR) & (nch = LF) THEN r.ReadSChar(nch)
			ELSIF ch = LF THEN ch := CR
			END;
			ConvertChar(wr, ch); ch := nch
		END;
		s := TextViews.dir.New(t)
	END ImportDosText;

	PROCEDURE TextView(s: Stores.Store): Stores.Store;
	BEGIN
		IF s IS Views.View THEN RETURN Properties.ThisType(s(Views.View), "TextViews.View")
		ELSE RETURN NIL
		END
	END TextView;

	PROCEDURE ExportText* (s: Stores.Store; f: Files.File);
		VAR w: Stores.Writer; t: TextModels.Model; r: TextModels.Reader; ch: CHAR;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21);
		s := TextView(s);
		IF s # NIL THEN
			w.ConnectTo(f); w.SetPos(0);
			t := s(TextViews.View).ThisModel();
			IF t # NIL THEN
				r := t.NewReader(NIL);
				r.ReadChar(ch);
				WHILE ~r.eot DO
					IF (ch # TextModels.viewcode) & (ch # TextModels.para) THEN
						ch := ThisWndChar(ch);
						w.WriteSChar(SHORT(ch));
						IF ch = CR THEN w.WriteSChar(LF) END
					END;
					r.ReadChar(ch)
				END
			END
		END
	END ExportText;
	
	PROCEDURE ExportTabText* (s: Stores.Store; f: Files.File);
		VAR w: Stores.Writer; t: TextModels.Model; r: TextModels.Reader; ch: CHAR;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21);
		s := TextView(s);
		IF s # NIL THEN
			w.ConnectTo(f); w.SetPos(0);
			t := s(TextViews.View).ThisModel();
			IF t # NIL THEN
				r := t.NewReader(NIL);
				r.ReadChar(ch);
				WHILE ~r.eot DO
					IF (ch # TextModels.viewcode) & (ch # TextModels.para) THEN
						ch := ThisWndChar(ch);
						IF ch = CR THEN w.WriteSChar(CR); w.WriteSChar(LF)
						ELSIF ch = TAB THEN w.WriteSChar(" "); w.WriteSChar(" ")
						ELSE w.WriteSChar(SHORT(ch))
						END
					END;
					r.ReadChar(ch)
				END
			END
		END
	END ExportTabText;
	
	
	PROCEDURE ExportUnicode* (s: Stores.Store; f: Files.File);
		VAR w: Stores.Writer; t: TextModels.Model; r: TextModels.Reader; ch: CHAR;
	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21);
		s := TextView(s);
		IF s # NIL THEN
			w.ConnectTo(f); w.SetPos(0);
			w.WriteChar(0FEFFX);	(* little endian *)
			t := s(TextViews.View).ThisModel();
			IF t # NIL THEN
				r := t.NewReader(NIL);
				r.ReadChar(ch);
				WHILE ~r.eot DO
					IF ch = CR THEN
						w.WriteChar(CR); w.WriteChar(LF)
					ELSIF (ch >= " ") OR (ch = TAB) THEN
						IF (ch >= 0EF00X) & (ch <= 0EFFFX) THEN ch := CHR(ORD(ch) - 0EF00H) END;
						w.WriteChar(ch)
					END;
					r.ReadChar(ch)
				END
			END
		END
	END ExportUnicode;
	
	PROCEDURE ImportHex* (f: Files.File; OUT s: Stores.Store);
		VAR r: Stores.Reader; t: TextModels.Model; w: TextMappers.Formatter; ch: SHORTCHAR; a: INTEGER;
			i: INTEGER; str: ARRAY 17 OF CHAR;
	BEGIN
		ASSERT(f # NIL, 20);
		r.ConnectTo(f); r.SetPos(0);
		t := TextModels.dir.New();
		w.ConnectTo(t); w.SetPos(0);
		r.ReadSChar(ch); a := 0;
		WHILE ~r.rider.eof DO
			IF a MOD 16 = 0 THEN
				w.WriteChar("[");
				w.WriteIntForm(a, TextMappers.hexadecimal, 8, "0", FALSE);
				w.WriteSString("]")
			END;
			w.WriteIntForm(ORD(ch), TextMappers.hexadecimal, 2, "0", FALSE);
			IF ch > 20X THEN str[a MOD 16] := ch ELSE str[a MOD 16] := "" END;
			INC(a);
			IF a MOD 16 = 0 THEN
				str[16] := 0X; w.WriteString(""); w.WriteString(str);
				w.WriteLn
			ELSIF a MOD 4 = 0 THEN
				w.WriteString("")
			ELSE
				w.WriteChar("")
			END;
			r.ReadSChar(ch)
		END;
		IF a MOD 16 # 0 THEN
			str[a MOD 16] := 0X;
			i := (16 - a MOD 16) * 3 + (3 - a MOD 16 DIV 4) + 3;
			WHILE i # 0 DO w.WriteChar(""); DEC(i) END;
			w.WriteString(str)
		END;
		s := TextViews.dir.New(t)
	END ImportHex;

	(*
		imports a UTF-8 encoded file:
		1. if the optional byte order mark (BOM) is found at the beginning of the file, it is skipped
		2. three forms of line separators are supported: CR, LF, and CR LF
		3. characters larger than 16 bit are reported as '?'
		4. in case of finding an illegal  encoding it falls back to importing a windows text
	*)
	PROCEDURE ImportUtf8* (f: Files.File; OUT s: Stores.Store);
		VAR r: Files.Reader; t: TextModels.Model; wr: TextModels.Writer; ch, nch: CHAR; formatError: BOOLEAN;

		PROCEDURE ReadUtf8Char (VAR rd: Files.Reader; VAR ch: CHAR);
			VAR c1, c2, c3: BYTE;

			PROCEDURE FormatError;
			BEGIN rd.SetPos(rd.Base().Length()); rd.eof := TRUE; ch := 0X; formatError := TRUE
			END FormatError;

		BEGIN
			rd.ReadByte(c1);
			IF c1 >= 0 THEN (* 0xxx xxxx *)
				ch := CHR(c1)
			ELSIF BITS(c1) * {7, 6, 5} = {7, 6} THEN (* 110x xxxx, 10xx xxxx *)
				rd.ReadByte(c2);
				IF BITS(c2) * {7, 6} = {7} THEN
					ch := CHR(64 * (c1 MOD 32) + (c2 MOD 64))
				ELSE FormatError
				END
			ELSIF BITS(c1) * {7, 6, 5, 4} = {7, 6, 5} THEN (* 1110 xxxx, 10xx xxxx, 10xx xxxx *)
				rd.ReadByte(c2); rd.ReadByte(c3);
				IF (BITS(c2) * {7, 6} = {7}) &  (BITS(c3) * {7, 6} = {7}) THEN
					ch := CHR(4096 * (c1 MOD 16) + 64 * (c2 MOD 64) + (c3 MOD 64))
				ELSE FormatError
				END
			ELSIF BITS(c1) * {7, 6, 5, 4, 3} = {7, 6, 5, 4} THEN (* 1111 0xxx, 10xx xxxx, 10xx xxxx, 10xx xxxx *)
				rd.ReadByte(c2); rd.ReadByte(c3); rd.ReadByte(c3); ch := '?'
			ELSE FormatError
			END
		END ReadUtf8Char;

	BEGIN
		ASSERT(f # NIL, 20);
		r := f.NewReader(NIL);
		t := TextModels.dir.New(); wr := t.NewWriter(NIL); formatError := FALSE;
		ReadUtf8Char(r, ch);
		IF ch = 0FEFFX THEN ReadUtf8Char(r, ch) END; (* skip BOM *)
		WHILE ~r.eof DO
			ReadUtf8Char(r, nch);
			IF (ch = CR) & (nch = LF) THEN ReadUtf8Char(r, nch)
			ELSIF ch = LF THEN ch := CR
			END;
			wr.WriteChar(ch);
			ch := nch
		END;
		IF ~formatError THEN s := TextViews.dir.New(t)
		ELSE ImportText(f, s) (* fallback to Windows text importer *)
		END
	END ImportUtf8;

	(* exports a UTF-8 encoded file without inserting a byte order mark (BOM) and with LF as line separator *)
	PROCEDURE ExportUtf8* (s: Stores.Store; f: Files.File);
		VAR w: Stores.Writer; t: TextModels.Model; r: TextModels.Reader; ch: CHAR;

		PROCEDURE WriteUtf8Char (VAR wr: Stores.Writer; ch: CHAR);
		BEGIN
			IF ch <= 7FX THEN
				wr.WriteByte(SHORT(SHORT(ORD(ch))))
			ELSIF ch <= 7FFX THEN
				wr.WriteByte(SHORT(SHORT( -64 + ORD(ch) DIV 64)));
				wr.WriteByte(SHORT(SHORT( -128 + ORD(ch) MOD 64)))
			ELSE
				wr.WriteByte(SHORT(SHORT( -32 + ORD(ch) DIV 4096)));
				wr.WriteByte(SHORT(SHORT( -128 + ORD(ch) DIV 64 MOD 64)));
				wr.WriteByte(SHORT(SHORT( -128 + ORD(ch) MOD 64)))
			END
		END WriteUtf8Char;

	BEGIN
		ASSERT(s # NIL, 20); ASSERT(f # NIL, 21);
		s := TextView(s);
		IF s # NIL THEN
			w.ConnectTo(f); w.SetPos(0);
			t := s(TextViews.View).ThisModel();
			IF t # NIL THEN
				r := t.NewReader(NIL);
				r.ReadChar(ch);
				WHILE ~r.eot DO
					IF (ch # TextModels.viewcode) & (ch # TextModels.para) THEN
						IF ch = CR THEN WriteUtf8Char(w, LF) ELSE WriteUtf8Char(w, ch) END
					END;
					r.ReadChar(ch)
				END
			END
		END
	END ExportUtf8;

END StdTextConv.
 