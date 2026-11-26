MODULE Dev_devOdc;
(** Документы и исходники для окна BlackBox.

	В репозитории тексты лежат как .cp (исходники) и .md (документы), а
	окно BlackBox открывает документы .odc: Help → Examples ищет
	Obx/Docu/Sys-Map.odc, File → Open показывает .odc. Команда All обходит
	каталог установки и рядом с каждым .cp в <Подсистема>/Mod и каждым
	.md в <Подсистема>/Docu (и его подкаталогах языков) кладёт .odc.

	Исходник переносится как есть; разметка .md разбирается: заголовки
	получают больший размер и жирность, **жирный** и *курсив* — начертание,
	`код` и блоки ``` — моноширинный шрифт, списки — маркеры, таблицы —
	выровненный моноширинный текст. Ссылка [текст](Путь.md) становится
	StdLinks-ссылкой на соответствующий .odc (команда StdCmds.OpenDoc),
	ссылка [текст](!Модуль.Процедура) и запись !Модуль.Процедура —
	StdLinks-ссылкой на команду.

	Зовёт tools/gui-world.sh после копирования текстов в каталог установки.
	Проверки: Dev_devOdcTest и команда Links. *)

	IMPORT Strings, Files, Converters, Stores, Console,
		Views, Ports, Fonts, TextModels, TextViews, TextMappers, StdLinks;

	CONST
		maxLine = 4096; maxPath = 1024; maxCell = 512; maxCols = 16;
		backtick = 60X; star = 2AX; under = 5FX; bar = 7CX;
		lbrack = 5BX; rbrack = 5DX; lparen = 28X; rparen = 29X;
		bang = 21X; backslash = 5CX; dot = 2EX; hash = 23X;

	TYPE
		Line = POINTER TO ARRAY OF CHAR;
		Lines = POINTER TO ARRAY OF Line;

		Cell = ARRAY maxCell OF CHAR;

		RowRec = RECORD
			n: INTEGER;
			sep: BOOLEAN;
			cells: ARRAY maxCols OF Cell
		END;

		Rows = POINTER TO ARRAY OF RowRec;

		Converter = RECORD
			f: TextMappers.Formatter;
			docDir: ARRAY 256 OF CHAR;
			base, code, h1, h2, h3, h4: TextModels.Attributes
		END;

	VAR
		Done-, Failed-: INTEGER;	(* счётчики последней команды *)

	(* ---------- строки ---------- *)

	PROCEDURE SLen (IN s: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < LEN(s)) & (s[i] # 0X) DO INC(i) END;
		RETURN i
	END SLen;

	PROCEDURE CopyRange (IN s: ARRAY OF CHAR; beg, end: INTEGER; OUT dst: ARRAY OF CHAR);
		VAR i, j, max: INTEGER;
	BEGIN
		i := beg; j := 0; max := LEN(dst) - 1;
		WHILE (i < end) & (j < max) & (s[i] # 0X) DO dst[j] := s[i]; INC(i); INC(j) END;
		dst[j] := 0X
	END CopyRange;

	PROCEDURE Copy (IN src: ARRAY OF CHAR; OUT dst: ARRAY OF CHAR);
	BEGIN CopyRange(src, 0, SLen(src), dst)
	END Copy;

	PROCEDURE AddLine (VAR lines: Lines; VAR n, cap: INTEGER; IN s: ARRAY OF CHAR);
		VAR t: Lines; i, len: INTEGER;
	BEGIN
		IF n = cap THEN
			cap := cap * 2; NEW(t, cap);
			FOR i := 0 TO n - 1 DO t[i] := lines[i] END;
			lines := t
		END;
		len := SLen(s);
		NEW(lines[n], len + 1);
		FOR i := 0 TO len - 1 DO lines[n][i] := s[i] END;
		lines[n][len] := 0X;
		INC(n)
	END AddLine;

	PROCEDURE SplitString* (IN s: ARRAY OF CHAR; OUT lines: Lines; OUT n: INTEGER);
		VAR cap, i, len: INTEGER; buf: ARRAY maxLine OF CHAR; ch: CHAR;
	BEGIN
		n := 0; cap := 64; NEW(lines, cap);
		i := 0; len := 0;
		WHILE s[i] # 0X DO
			ch := s[i];
			IF (ch = 0DX) OR (ch = 0AX) THEN
				IF (ch = 0DX) & (s[i + 1] = 0AX) THEN INC(i) END;
				buf[len] := 0X; AddLine(lines, n, cap, buf); len := 0
			ELSE
				IF len < LEN(buf) - 1 THEN buf[len] := ch; INC(len) END
			END;
			INC(i)
		END;
		IF len > 0 THEN buf[len] := 0X; AddLine(lines, n, cap, buf) END
	END SplitString;

	(* ---------- разметка строк ---------- *)

	PROCEDURE TrimLeft (IN s: ARRAY OF CHAR; OUT t: ARRAY OF CHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := 0;
		WHILE (s[i] = " ") OR (s[i] = 09X) DO INC(i) END;
		j := 0;
		WHILE s[i] # 0X DO t[j] := s[i]; INC(i); INC(j) END;
		t[j] := 0X
	END TrimLeft;

	PROCEDURE IsBlank (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (s[i] = " ") OR (s[i] = 09X) DO INC(i) END;
		RETURN s[i] = 0X
	END IsBlank;

	PROCEDURE IsFence (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR t: ARRAY maxLine OF CHAR; n: INTEGER;
	BEGIN
		TrimLeft(s, t);
		n := SLen(t);
		RETURN (n >= 3) & (t[0] = backtick) & (t[1] = backtick) & (t[2] = backtick)
	END IsFence;

	PROCEDURE IsTableRow (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR t: ARRAY maxLine OF CHAR; i: INTEGER; found: BOOLEAN;
	BEGIN
		TrimLeft(s, t);
		IF t[0] # bar THEN RETURN FALSE END;
		i := 1; found := FALSE;
		WHILE (t[i] # 0X) & ~found DO
			IF t[i] = bar THEN found := TRUE END;
			INC(i)
		END;
		RETURN found
	END IsTableRow;

	PROCEDURE IsComment (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR t: ARRAY maxLine OF CHAR; n: INTEGER;
	BEGIN
		TrimLeft(s, t);
		n := SLen(t);
		RETURN (n >= 4) & (t[0] = "<") & (t[1] = "!") & (t[2] = "-") & (t[3] = "-")
	END IsComment;

	PROCEDURE IsHr (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR t: ARRAY maxLine OF CHAR; i: INTEGER; ch: CHAR; n: INTEGER;
	BEGIN
		TrimLeft(s, t);
		ch := t[0];
		IF (ch # "-") & (ch # star) & (ch # under) THEN RETURN FALSE END;
		i := 0; n := 0;
		WHILE t[i] # 0X DO
			IF (t[i] # ch) & (t[i] # " ") & (t[i] # 09X) THEN RETURN FALSE END;
			IF t[i] = ch THEN INC(n) END;
			INC(i)
		END;
		RETURN n >= 3
	END IsHr;

	PROCEDURE HeadingLevel (IN s: ARRAY OF CHAR): INTEGER;
		VAR t: ARRAY maxLine OF CHAR; i: INTEGER;
	BEGIN
		TrimLeft(s, t);
		i := 0;
		WHILE (i < 6) & (t[i] = hash) DO INC(i) END;
		IF (i = 0) OR ((t[i] # " ") & (t[i] # 09X)) THEN RETURN 0 END;
		RETURN i
	END HeadingLevel;

	PROCEDURE HeadingText (IN s: ARRAY OF CHAR; OUT t: ARRAY OF CHAR);
		VAR u: ARRAY maxLine OF CHAR; i, j: INTEGER;
	BEGIN
		TrimLeft(s, u);
		i := 0;
		WHILE (u[i] = hash) OR (u[i] = " ") OR (u[i] = 09X) DO INC(i) END;
		j := 0;
		WHILE u[i] # 0X DO t[j] := u[i]; INC(i); INC(j) END;
		WHILE (j > 0) & ((t[j - 1] = " ") OR (t[j - 1] = hash)) DO DEC(j) END;
		t[j] := 0X
	END HeadingText;

	(* ---------- поиск ---------- *)

	PROCEDURE FindChar (IN s: ARRAY OF CHAR; beg, end: INTEGER; ch: CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := beg;
		WHILE (i < end) & (s[i] # 0X) DO
			IF s[i] = ch THEN RETURN i END;
			INC(i)
		END;
		RETURN -1
	END FindChar;

	PROCEDURE FindStr (IN s: ARRAY OF CHAR; beg, end: INTEGER; IN pat: ARRAY OF CHAR): INTEGER;
		VAR i, j: INTEGER; ok: BOOLEAN;
	BEGIN
		i := beg;
		WHILE i < end DO
			j := 0; ok := TRUE;
			LOOP
				IF pat[j] = 0X THEN EXIT END;
				IF (i + j >= end) OR (s[i + j] # pat[j]) THEN ok := FALSE; EXIT END;
				INC(j)
			END;
			IF ok THEN RETURN i END;
			INC(i)
		END;
		RETURN -1
	END FindStr;

	PROCEDURE StartsWith (IN s, pat: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE pat[i] # 0X DO
			IF s[i] # pat[i] THEN RETURN FALSE END;
			INC(i)
		END;
		RETURN TRUE
	END StartsWith;

	PROCEDURE HasExt (IN name, ext: ARRAY OF CHAR): BOOLEAN;
		VAR n, e, i: INTEGER;
	BEGIN
		n := SLen(name); e := SLen(ext);
		IF n <= e + 1 THEN RETURN FALSE END;
		IF name[n - e - 1] # "." THEN RETURN FALSE END;
		FOR i := 0 TO e - 1 DO
			IF name[n - e + i] # ext[i] THEN RETURN FALSE END
		END;
		RETURN TRUE
	END HasExt;

	(* ---------- пути ссылок ---------- *)

	PROCEDURE AppendSeg (VAR out: ARRAY OF CHAR; IN seg: ARRAY OF CHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := SLen(out);
		IF (i > 0) & (i < LEN(out) - 1) THEN out[i] := "/"; INC(i) END;
		j := 0;
		WHILE (seg[j] # 0X) & (i < LEN(out) - 1) DO out[i] := seg[j]; INC(i); INC(j) END;
		out[i] := 0X
	END AppendSeg;

	PROCEDURE RemoveLastSeg (VAR out: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := SLen(out);
		WHILE (i > 0) & (out[i - 1] # "/") DO DEC(i) END;
		IF i > 0 THEN out[i - 1] := 0X ELSE out[0] := 0X END
	END RemoveLastSeg;

	PROCEDURE ResolvePath (IN base, rel: ARRAY OF CHAR; OUT out: ARRAY OF CHAR);
		VAR tmp: ARRAY maxPath OF CHAR; seg: ARRAY 256 OF CHAR;
			i, j: INTEGER;
	BEGIN
		IF (rel[0] = "/") OR (base = "") THEN tmp := rel$ ELSE tmp := base + "/" + rel$ END;
		out := "";
		i := 0;
		WHILE tmp[i] # 0X DO
			IF tmp[i] = "/" THEN
				INC(i)
			ELSE
				j := i;
				WHILE (tmp[j] # 0X) & (tmp[j] # "/") DO INC(j) END;
				CopyRange(tmp, i, j, seg);
				IF seg = ".." THEN
					RemoveLastSeg(out)
				ELSIF seg # "." THEN
					AppendSeg(out, seg)
				END;
				i := j
			END
		END
	END ResolvePath;

	PROCEDURE ChangeExt (VAR path: ARRAY OF CHAR; IN ext: ARRAY OF CHAR);
		VAR i, lastDot, lastSlash, len: INTEGER;
	BEGIN
		len := SLen(path); i := 0; lastDot := -1; lastSlash := -1;
		WHILE i < len DO
			IF path[i] = "/" THEN lastSlash := i
			ELSIF path[i] = "." THEN lastDot := i
			END;
			INC(i)
		END;
		IF lastDot > lastSlash THEN
			Strings.Replace(path, lastDot + 1, len - lastDot - 1, ext)
		ELSE
			IF len < LEN(path) - 1 THEN path[len] := "."; INC(len); path[len] := 0X END;
			Strings.Replace(path, len, 0, ext)
		END
	END ChangeExt;

	PROCEDURE ReplaceMod64 (VAR path: ARRAY OF CHAR);
		VAR pos: INTEGER;
	BEGIN
		Strings.Find(path, "/Mod64/", 0, pos);
		IF pos >= 0 THEN Strings.Replace(path, pos + 1, 5, "Mod")
		ELSIF StartsWith(path, "Mod64/") THEN Strings.Replace(path, 0, 5, "Mod")
		END
	END ReplaceMod64;

	PROCEDURE LinkCmd (IN url, docDir: ARRAY OF CHAR; OUT cmd: ARRAY OF CHAR): BOOLEAN;
		VAR u, path: ARRAY maxPath OF CHAR; i: INTEGER;
	BEGIN
		u := url$;
		IF u = "" THEN RETURN FALSE END;
		IF u[0] = bang THEN
			cmd := u$; Strings.Replace(cmd, 0, 1, ""); RETURN TRUE
		END;
		IF StartsWith(u, "http://") OR StartsWith(u, "https://")
			OR StartsWith(u, "mailto:") OR StartsWith(u, "ftp://") THEN
			cmd := "Dialog.OpenExternal('" + u + "')";
			RETURN TRUE
		END;
		i := FindChar(u, 0, SLen(u), hash);
		IF i >= 0 THEN u[i] := 0X END;
		IF u = "" THEN RETURN FALSE END;
		IF HasExt(u, "md") OR HasExt(u, "cp") THEN
			ResolvePath(docDir, u, path);
			IF HasExt(u, "cp") THEN ReplaceMod64(path) END;
			ChangeExt(path, "odc");
			cmd := "StdCmds.OpenDoc('" + path + "')";
			RETURN TRUE
		END;
		RETURN FALSE
	END LinkCmd;

	(* ---------- подсчёт видимой длины ---------- *)

	PROCEDURE ^ ScanCmd (IN s: ARRAY OF CHAR; start, end: INTEGER;
		OUT ts, te: INTEGER; OUT cmd: ARRAY OF CHAR): BOOLEAN;

	PROCEDURE VLen (IN s: ARRAY OF CHAR; beg, end: INTEGER): INTEGER;
		VAR i, j, n: INTEGER; ch: CHAR; ts, te: INTEGER; cmd: ARRAY maxCell OF CHAR;

		PROCEDURE ItalicOpen (i: INTEGER): BOOLEAN;
		BEGIN
			IF (i + 1 >= end) OR (s[i + 1] = " ") OR (s[i + 1] = 09X) THEN RETURN FALSE END;
			IF (s[i] = under) & (i > beg) & Strings.IsIdent(s[i - 1]) THEN RETURN FALSE END;
			RETURN TRUE
		END ItalicOpen;

		PROCEDURE ItalicClose (from: INTEGER): INTEGER;
			VAR j: INTEGER;
		BEGIN
			j := from;
			WHILE j < end DO
				IF (s[j] = s[from - 1]) & (s[j - 1] # " ")
					& ~((s[j] = under) & (j + 1 < end) & Strings.IsIdent(s[j + 1])) THEN RETURN j END;
				INC(j)
			END;
			RETURN -1
		END ItalicClose;

	BEGIN
		i := beg; n := 0;
		WHILE i < end DO
			ch := s[i];
			IF ch = backslash THEN
				INC(n);
				IF i + 1 < end THEN i := i + 2 ELSE INC(i) END
			ELSIF ch = backtick THEN
				j := FindChar(s, i + 1, end, backtick);
				IF j >= 0 THEN INC(n, j - i - 1); i := j + 1 ELSE INC(n); INC(i) END
			ELSIF (ch = star) & (i + 1 < end) & (s[i + 1] = star) THEN
				j := FindStr(s, i + 2, end, "**");
				IF (j >= 0) & (i + 2 < j) THEN INC(n, VLen(s, i + 2, j)); i := j + 2
				ELSE INC(n); INC(i)
				END
			ELSIF (ch = star) OR (ch = under) THEN
				IF ItalicOpen(i) THEN
					j := ItalicClose(i + 1);
					IF j >= 0 THEN INC(n, VLen(s, i + 1, j)); i := j + 1
					ELSE INC(n); INC(i)
					END
				ELSE INC(n); INC(i)
				END
			ELSIF ch = lbrack THEN
				j := FindChar(s, i + 1, end, rbrack);
				IF (j >= 0) & (j + 1 < end) & (s[j + 1] = lparen) THEN
					INC(n, VLen(s, i + 1, j));
					j := FindChar(s, j + 2, end, rparen);
					IF j >= 0 THEN i := j + 1 ELSE INC(i) END
				ELSE INC(n); INC(i)
				END
			ELSIF ch = bang THEN
				IF ScanCmd(s, i, end, ts, te, cmd) THEN
					INC(n, te - ts); i := te
				ELSE INC(n); INC(i)
				END
			ELSE
				INC(n); INC(i)
			END
		END;
		RETURN n
	END VLen;

	PROCEDURE VisibleLen (IN s: ARRAY OF CHAR): INTEGER;
	BEGIN RETURN VLen(s, 0, SLen(s)) END VisibleLen;

	(* ---------- запись inline-разметки ---------- *)

	PROCEDURE ^ WriteInline (VAR c: Converter; IN s: ARRAY OF CHAR; beg, end: INTEGER;
		attr: TextModels.Attributes);

	PROCEDURE ScanCmd (IN s: ARRAY OF CHAR; start, end: INTEGER;
		OUT ts, te: INTEGER; OUT cmd: ARRAY OF CHAR): BOOLEAN;
		VAR i, j, k: INTEGER;
	BEGIN
		i := start + 1;	(* за '!' *)
		IF (i < end) & Strings.IsUpper(s[i]) THEN
			j := i;
			WHILE (j < end) & Strings.IsIdent(s[j]) DO INC(j) END;
			IF (j < end) & (s[j] = dot) THEN
				k := j + 1;
				IF (k < end) & Strings.IsUpper(s[k]) THEN
					INC(k);
					WHILE (k < end) & Strings.IsIdent(s[k]) DO INC(k) END;
					CopyRange(s, i, k, cmd); ts := i; te := k;
					RETURN TRUE
				END
			END
		END;
		RETURN FALSE
	END ScanCmd;

	PROCEDURE LinkAttr (a: TextModels.Attributes): TextModels.Attributes;
	BEGIN
		RETURN TextModels.NewColor(
			TextModels.NewStyle(a, a.font.style + {Fonts.underline}), Ports.blue)
	END LinkAttr;

	PROCEDURE WriteChars (VAR c: Converter; IN s: ARRAY OF CHAR; beg, end: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := beg TO end - 1 DO c.f.rider.WriteChar(s[i]) END
	END WriteChars;

	PROCEDURE WriteCommandLink (VAR c: Converter; IN cmd: ARRAY OF CHAR;
		IN s: ARRAY OF CHAR; beg, end: INTEGER; attr: TextModels.Attributes);
		VAR l: StdLinks.Link;
	BEGIN
		l := StdLinks.dir.NewLink(cmd); c.f.WriteView(l);
		WriteInline(c, s, beg, end, LinkAttr(attr));
		l := StdLinks.dir.NewLink(""); c.f.WriteView(l)
	END WriteCommandLink;

	PROCEDURE WriteLink (VAR c: Converter; IN s: ARRAY OF CHAR;
		textBeg, textEnd, urlBeg, urlEnd: INTEGER; attr: TextModels.Attributes);
		VAR url, cmd: ARRAY maxPath OF CHAR;
	BEGIN
		CopyRange(s, urlBeg, urlEnd, url);
		IF LinkCmd(url, c.docDir, cmd) THEN
			WriteCommandLink(c, cmd, s, textBeg, textEnd, attr)
		ELSE
			WriteInline(c, s, textBeg, textEnd, attr)
		END
	END WriteLink;

	PROCEDURE ItalicOpenAt (IN s: ARRAY OF CHAR; i, beg, end: INTEGER): BOOLEAN;
	BEGIN
		IF (i + 1 >= end) OR (s[i + 1] = " ") OR (s[i + 1] = 09X) THEN RETURN FALSE END;
		IF (s[i] = under) & (i > beg) & Strings.IsIdent(s[i - 1]) THEN RETURN FALSE END;
		RETURN TRUE
	END ItalicOpenAt;

	PROCEDURE ItalicCloseAt (IN s: ARRAY OF CHAR; from, end: INTEGER; ch: CHAR): INTEGER;
		VAR j: INTEGER;
	BEGIN
		j := from;
		WHILE j < end DO
			IF (s[j] = ch) & (s[j - 1] # " ")
				& ~((ch = under) & (j + 1 < end) & Strings.IsIdent(s[j + 1])) THEN RETURN j END;
			INC(j)
		END;
		RETURN -1
	END ItalicCloseAt;

	PROCEDURE WriteInline (VAR c: Converter; IN s: ARRAY OF CHAR; beg, end: INTEGER;
		attr: TextModels.Attributes);
		VAR i, j, k, ts, te: INTEGER; ch: CHAR; na: TextModels.Attributes;
			cmd: ARRAY maxPath OF CHAR;
	BEGIN
		c.f.rider.SetAttr(attr);
		i := beg;
		WHILE i < end DO
			ch := s[i];
			IF ch = backslash THEN
				IF i + 1 < end THEN c.f.rider.WriteChar(s[i + 1]); i := i + 2
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSIF ch = backtick THEN
				j := FindChar(s, i + 1, end, backtick);
				IF j >= 0 THEN
					c.f.rider.SetAttr(c.code);
					WriteChars(c, s, i + 1, j);
					c.f.rider.SetAttr(attr);
					i := j + 1
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSIF (ch = star) & (i + 1 < end) & (s[i + 1] = star) THEN
				j := FindStr(s, i + 2, end, "**");
				IF (j >= 0) & (i + 2 < j) THEN
					na := TextModels.NewWeight(attr, Fonts.bold);
					WriteInline(c, s, i + 2, j, na); i := j + 2
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSIF (ch = star) OR (ch = under) THEN
				IF ItalicOpenAt(s, i, beg, end) THEN
					j := ItalicCloseAt(s, i + 1, end, ch);
					IF j >= 0 THEN
						na := TextModels.NewStyle(attr, attr.font.style + {Fonts.italic});
						WriteInline(c, s, i + 1, j, na); i := j + 1
					ELSE c.f.rider.WriteChar(ch); INC(i)
					END
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSIF ch = lbrack THEN
				j := FindChar(s, i + 1, end, rbrack);
				IF (j >= 0) & (j + 1 < end) & (s[j + 1] = lparen) THEN
					k := FindChar(s, j + 2, end, rparen);
					IF k >= 0 THEN
						WriteLink(c, s, i + 1, j, j + 2, k, attr); i := k + 1
					ELSE c.f.rider.WriteChar(ch); INC(i)
					END
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSIF ch = bang THEN
				IF (i + 1 < end) & (s[i + 1] = lbrack) THEN
					(* изображение: показываем только подпись *)
					j := FindChar(s, i + 2, end, rbrack);
					IF (j >= 0) & (j + 1 < end) & (s[j + 1] = lparen) THEN
						k := FindChar(s, j + 2, end, rparen);
						IF k >= 0 THEN WriteInline(c, s, i + 2, j, attr); i := k + 1
						ELSE c.f.rider.WriteChar(ch); INC(i)
						END
					ELSE c.f.rider.WriteChar(ch); INC(i)
					END
				ELSIF ScanCmd(s, i, end, ts, te, cmd) THEN
					WriteCommandLink(c, cmd, s, ts, te, attr); i := te
				ELSE c.f.rider.WriteChar(ch); INC(i)
				END
			ELSE
				c.f.rider.WriteChar(ch); INC(i)
			END
		END;
		c.f.rider.SetAttr(attr)
	END WriteInline;

	(* ---------- таблицы ---------- *)

	PROCEDURE ParseRow (IN s: ARRAY OF CHAR; OUT row: RowRec);
		VAR i, j, beg, end, len: INTEGER; ch: CHAR;
	BEGIN
		row.n := 0; row.sep := FALSE;
		len := SLen(s);
		i := 0;
		WHILE (i < len) & ((s[i] = " ") OR (s[i] = 09X)) DO INC(i) END;
		IF (i < len) & (s[i] = bar) THEN INC(i) END;
		IF i >= len THEN RETURN END;
		REPEAT
			beg := i;
			WHILE (i < len) & (s[i] # bar) DO INC(i) END;
			end := i;
			WHILE (beg < end) & ((s[beg] = " ") OR (s[beg] = 09X) OR (s[beg] = 0A0X)) DO INC(beg) END;
			WHILE (end > beg) & ((s[end - 1] = " ") OR (s[end - 1] = 09X) OR (s[end - 1] = 0A0X)) DO DEC(end) END;
			IF row.n < maxCols THEN
				CopyRange(s, beg, end, row.cells[row.n]);
				INC(row.n)
			END;
			IF (i < len) & (s[i] = bar) THEN INC(i) END
		UNTIL i >= len;
		(* строка-разделитель: все ячейки из '-', ':' и пробелов *)
		IF row.n > 0 THEN
			row.sep := TRUE;
			FOR j := 0 TO row.n - 1 DO
				i := 0;
				IF row.cells[j][0] = 0X THEN row.sep := FALSE END;
				WHILE row.cells[j][i] # 0X DO
					ch := row.cells[j][i];
					IF (ch # "-") & (ch # ":") & (ch # " ") & (ch # 09X) THEN row.sep := FALSE END;
					INC(i)
				END
			END
		END
	END ParseRow;

	PROCEDURE RenderTable (VAR c: Converter; lines: Lines; n: INTEGER; VAR i: INTEGER);
		VAR rows: Rows; start, count, k, col, nCols, w: INTEGER;
			widths: ARRAY maxCols OF INTEGER; len: INTEGER;
	BEGIN
		start := i; count := 0; k := i;
		WHILE (k < n) & IsTableRow(lines[k]) DO INC(count); INC(k) END;
		IF count = 0 THEN RETURN END;
		NEW(rows, count);
		nCols := 0;
		FOR k := 0 TO count - 1 DO
			ParseRow(lines[i + k]^, rows[k]);
			IF rows[k].n > nCols THEN nCols := rows[k].n END
		END;
		FOR col := 0 TO nCols - 1 DO widths[col] := 3 END;
		FOR k := 0 TO count - 1 DO
			IF ~rows[k].sep THEN
				FOR col := 0 TO rows[k].n - 1 DO
					w := VisibleLen(rows[k].cells[col]);
					IF w > widths[col] THEN widths[col] := w END
				END
			END
		END;
		FOR k := 0 TO count - 1 DO
			c.f.rider.SetAttr(c.code);
			IF rows[k].sep THEN
				FOR col := 0 TO nCols - 1 DO
					c.f.rider.WriteChar(bar);
					w := widths[col] + 2;
					FOR len := 1 TO w DO c.f.rider.WriteChar("-") END
				END;
				c.f.rider.WriteChar(bar)
			ELSE
				FOR col := 0 TO nCols - 1 DO
					c.f.rider.WriteChar(bar); c.f.rider.WriteChar(" ");
					IF col < rows[k].n THEN
						len := SLen(rows[k].cells[col]);
						WriteInline(c, rows[k].cells[col], 0, len, c.code);
						w := VisibleLen(rows[k].cells[col]);
						FOR len := w TO widths[col] - 1 DO c.f.rider.WriteChar(" ") END
					ELSE
						FOR len := 1 TO widths[col] DO c.f.rider.WriteChar(" ") END
					END;
					c.f.rider.WriteChar(" ")
				END;
				c.f.rider.WriteChar(bar)
			END;
			c.f.WriteLn
		END;
		i := start + count
	END RenderTable;

	(* ---------- установка атрибутов ---------- *)

	PROCEDURE HeadingAttr (VAR c: Converter; level: INTEGER): TextModels.Attributes;
	BEGIN
		CASE level OF
		| 1: RETURN c.h1
		| 2: RETURN c.h2
		| 3: RETURN c.h3
		ELSE RETURN c.h4
		END
	END HeadingAttr;

	PROCEDURE InitConverter (VAR c: Converter; IN docDir: ARRAY OF CHAR);
		VAR codeFont: Fonts.Font; size: INTEGER;
	BEGIN
		c.docDir := docDir$;
		c.base := c.f.rider.attr;
		size := c.base.font.size;
		codeFont := Fonts.dir.This("Courier", size, {}, Fonts.normal);
		c.code := TextModels.NewFont(c.base, codeFont);
		c.h1 := TextModels.NewSize(TextModels.NewWeight(c.base, Fonts.bold), size + 8 * Fonts.point);
		c.h2 := TextModels.NewSize(TextModels.NewWeight(c.base, Fonts.bold), size + 4 * Fonts.point);
		c.h3 := TextModels.NewSize(TextModels.NewWeight(c.base, Fonts.bold), size + 2 * Fonts.point);
		c.h4 := TextModels.NewSize(TextModels.NewWeight(c.base, Fonts.bold), size + Fonts.point)
	END InitConverter;

	(* ---------- строка за строкой ---------- *)

	PROCEDURE ProcessLine (VAR c: Converter; IN line: ARRAY OF CHAR);
		VAR level, i, j, len: INTEGER; t: ARRAY maxLine OF CHAR;
			attr: TextModels.Attributes; indent: INTEGER; ordered: BOOLEAN; num: ARRAY 16 OF CHAR;
	BEGIN
		IF IsBlank(line) THEN c.f.WriteLn; RETURN END;
		level := HeadingLevel(line);
		IF level > 0 THEN
			HeadingText(line, t);
			attr := HeadingAttr(c, level);
			c.f.rider.SetAttr(attr);
			WriteInline(c, t, 0, SLen(t), attr);
			c.f.WriteLn;
			RETURN
		END;
		IF IsHr(line) THEN
			c.f.rider.SetAttr(c.base);
			FOR i := 1 TO 40 DO c.f.rider.WriteChar("-") END;
			c.f.WriteLn;
			RETURN
		END;
		TrimLeft(line, t);
		(* список *)
		IF (t[0] = "-") OR (t[0] = star) OR (t[0] = "+") THEN
			IF (t[1] = " ") OR (t[1] = 09X) THEN
				i := 0;
				WHILE (line[i] = " ") OR (line[i] = 09X) DO c.f.rider.WriteChar(line[i]); INC(i) END;
				c.f.rider.SetAttr(c.base);
				c.f.rider.WriteChar(2022X); c.f.rider.WriteChar(" ");
				WriteInline(c, t, 2, SLen(t), c.base);
				c.f.WriteLn;
				RETURN
			END
		END;
		ordered := FALSE; j := 0;
		WHILE Strings.IsNumeric(t[j]) DO
			IF j < LEN(num) - 1 THEN num[j] := t[j] END;
			INC(j)
		END;
		IF (j > 0) & (j < LEN(num) - 1) & (t[j] = ".")
			& ((t[j + 1] = " ") OR (t[j + 1] = 09X)) THEN ordered := TRUE END;
		IF ordered THEN
			i := 0;
			WHILE (line[i] = " ") OR (line[i] = 09X) DO c.f.rider.WriteChar(line[i]); INC(i) END;
			c.f.rider.SetAttr(c.base);
			num[j] := 0X;
			WriteChars(c, num, 0, j);
			c.f.rider.WriteChar("."); c.f.rider.WriteChar(" ");
			WriteInline(c, t, j + 1, SLen(t), c.base);
			c.f.WriteLn;
			RETURN
		END;
		(* цитата *)
		IF t[0] = ">" THEN
			i := 1;
			WHILE (t[i] = " ") OR (t[i] = 09X) DO INC(i) END;
			c.f.rider.SetAttr(c.base);
			c.f.rider.WriteChar(" "); c.f.rider.WriteChar(" ");
			WriteInline(c, t, i, SLen(t), c.base);
			c.f.WriteLn;
			RETURN
		END;
		WriteInline(c, line, 0, SLen(line), c.base);
		c.f.WriteLn
	END ProcessLine;

	PROCEDURE ConvertLines (lines: Lines; n: INTEGER; IN docDir: ARRAY OF CHAR;
		OUT out: TextModels.Model);
		VAR c: Converter; i, j: INTEGER;
	BEGIN
		out := TextModels.dir.New();
		c.f.ConnectTo(out);
		InitConverter(c, docDir);
		i := 0;
		WHILE i < n DO
			IF IsComment(lines[i]^) THEN
				j := i;
				WHILE (j < n) & (FindStr(lines[j]^, 0, SLen(lines[j]^), "-->") < 0) DO INC(j) END;
				i := j + 1
			ELSIF IsFence(lines[i]^) THEN
				INC(i);
				WHILE (i < n) & ~IsFence(lines[i]^) DO
					c.f.rider.SetAttr(c.code);
					WriteChars(c, lines[i]^, 0, SLen(lines[i]^));
					c.f.WriteLn;
					INC(i)
				END;
				IF i < n THEN INC(i) END
			ELSIF IsTableRow(lines[i]^) THEN
				RenderTable(c, lines, n, i)
			ELSE
				ProcessLine(c, lines[i]^); INC(i)
			END
		END
	END ConvertLines;

	PROCEDURE ConvertModel* (src: TextModels.Model; IN docDir: ARRAY OF CHAR;
		OUT out: TextModels.Model);
		VAR lines: Lines; n, cap, len: INTEGER; buf: ARRAY maxLine OF CHAR;
			rd: TextModels.Reader; ch: CHAR;
	BEGIN
		n := 0; cap := 64; NEW(lines, cap); len := 0;
		rd := src.NewReader(NIL);
		WHILE rd.Pos() < src.Length() DO
			rd.ReadChar(ch);
			IF (ch = 0DX) OR (ch = 0AX) OR (ch = 0EX) THEN
				buf[len] := 0X; AddLine(lines, n, cap, buf); len := 0
			ELSIF (ch = 09X) OR (ch >= " ") THEN
				IF len < LEN(buf) - 1 THEN buf[len] := ch; INC(len) END
			END
		END;
		IF len > 0 THEN buf[len] := 0X; AddLine(lines, n, cap, buf) END;
		ConvertLines(lines, n, docDir, out)
	END ConvertModel;

	PROCEDURE ConvertString* (IN md: ARRAY OF CHAR; IN docDir: ARRAY OF CHAR;
		OUT out: TextModels.Model);
		VAR lines: Lines; n: INTEGER;
	BEGIN
		SplitString(md, lines, n);
		ConvertLines(lines, n, docDir, out)
	END ConvertString;

	(* ---------- поиск ссылок в готовом тексте ---------- *)

	PROCEDURE CountLinks (m: TextModels.Model): INTEGER;
		VAR rd: TextModels.Reader; ch: CHAR; n: INTEGER; v: StdLinks.Link;
	BEGIN
		n := 0; rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF (ch = TextModels.viewcode) & (rd.view # NIL) THEN
				IF rd.view IS StdLinks.Link THEN
					v := rd.view(StdLinks.Link);
					IF v.leftSide THEN INC(n) END
				END
			END
		END;
		RETURN n
	END CountLinks;

	PROCEDURE EnsureConverters*;
		PROCEDURE Find (IN imp, exp: ARRAY OF CHAR): Converters.Converter;
			VAR c: Converters.Converter;
		BEGIN
			c := Converters.list;
			WHILE (c # NIL) & ~((c.imp = imp) & ((exp = "") OR (c.exp = exp))) DO c := c.next END;
			RETURN c
		END Find;
	BEGIN
		IF Find("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8") = NIL THEN
			Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "utf8",
				{Converters.importAll})
		END;
		IF Find("Documents.ImportDocument", "Documents.ExportDocument") = NIL THEN
			Converters.Register("Documents.ImportDocument", "Documents.ExportDocument", "", "odc", {})
		END
	END EnsureConverters;

	PROCEDURE FindConverter (IN imp, exp: ARRAY OF CHAR): Converters.Converter;
		VAR c: Converters.Converter;
	BEGIN
		c := Converters.list;
		WHILE (c # NIL) & ~((c.imp = imp) & ((exp = "") OR (c.exp = exp))) DO c := c.next END;
		RETURN c
	END FindConverter;

	PROCEDURE Documents* (IN path, ext: ARRAY OF CHAR);
	(** Каждому файлу path/*.ext — документ path/<имя>.odc с тем же текстом. *)
		VAR loc: Files.Locator; f: Files.FileInfo; name: Files.Name;
			imp, exp: Converters.Converter; s: Stores.Store; out: TextModels.Model;
			v: TextViews.View;
	BEGIN
		EnsureConverters;
		imp := FindConverter("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8");
		exp := FindConverter("Documents.ImportDocument", "Documents.ExportDocument");
		IF (imp = NIL) OR (exp = NIL) THEN
			Console.WriteStr("Dev_devOdc: converters not found"); Console.WriteLn; INC(Failed); RETURN
		END;
		loc := Files.dir.This(path);
		IF loc.res # 0 THEN RETURN END;
		f := Files.dir.FileList(loc);
		WHILE f # NIL DO
			IF HasExt(f.name, ext) THEN
				name := f.name$; name[LEN(name$) - LEN(ext$) - 1] := 0X;
				s := NIL;
				Converters.Import(loc, f.name, imp, s);
				IF (s # NIL) & (s IS TextViews.View) THEN
					IF ext = "md" THEN
						ConvertModel(s(TextViews.View).ThisModel(), path, out);
						v := TextViews.dir.New(out);
						Converters.Export(loc, name, exp, v); INC(Done)
					ELSE
						Converters.Export(loc, name, exp, s(TextViews.View)); INC(Done)
					END
				ELSE
					Console.WriteStr("Dev_devOdc: not a text: " + path + "/" + f.name); Console.WriteLn;
					INC(Failed)
				END
			END;
			f := f.next
		END
	END Documents;

	PROCEDURE All*;
	(** Все подсистемы каталога установки: Mod/*.cp, Docu/*.md, Docu/<язык>/*.md. *)
		VAR subs, langs: Files.LocInfo; n: ARRAY 16 OF CHAR;
	BEGIN
		Done := 0; Failed := 0;
		subs := Files.dir.LocList(Files.dir.This(""));
		WHILE subs # NIL DO
			IF Strings.IsUpper(subs.name[0]) THEN
				Documents(subs.name + "/Mod", "cp");
				Documents(subs.name + "/Docu", "md");
				langs := Files.dir.LocList(Files.dir.This(subs.name + "/Docu"));
				WHILE langs # NIL DO
					Documents(subs.name + "/Docu/" + langs.name, "md");
					langs := langs.next
				END
			END;
			subs := subs.next
		END;
		Console.WriteStr("Dev_devOdc: documents ");
		Strings.IntToString(Done, n); Console.WriteStr(n);
		Console.WriteStr(", failed ");
		Strings.IntToString(Failed, n); Console.WriteStr(n); Console.WriteLn
	END All;

	PROCEDURE Check* (IN path, name: ARRAY OF CHAR);
	(** Открыть документ через Views.OldView: сколько StdLinks-ссылок и пустых. *)
		VAR loc: Files.Locator; v: Views.View; m: TextModels.Model; nbuf, ebuf: ARRAY 16 OF CHAR;
			rd: TextModels.Reader; ch: CHAR; cmd: ARRAY maxPath OF CHAR; n, empty: INTEGER;
			l: StdLinks.Link;
	BEGIN
		EnsureConverters;
		loc := Files.dir.This(path);
		IF loc.res # 0 THEN
			Console.WriteStr("Dev_devOdc: no path " + path); Console.WriteLn; INC(Failed); RETURN
		END;
		v := Views.OldView(loc, name$);
		IF v = NIL THEN
			Console.WriteStr("Dev_devOdc: no view " + path + "/" + name); Console.WriteLn; INC(Failed); RETURN
		END;
		IF ~(v IS TextViews.View) THEN
			Console.WriteStr("Dev_devOdc: not a text view "); Console.WriteLn; INC(Failed); RETURN
		END;
		m := v(TextViews.View).ThisModel();
		n := 0; empty := 0; rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF (ch = TextModels.viewcode) & (rd.view # NIL) THEN
				IF rd.view IS StdLinks.Link THEN
					l := rd.view(StdLinks.Link);
					IF l.leftSide THEN
						l.GetCmd(cmd); INC(n);
						IF cmd = "" THEN INC(empty) END
					END
				END
			END
		END;
		Strings.IntToString(n, nbuf); Strings.IntToString(empty, ebuf);
		Console.WriteStr("Dev_devOdc: check links "); Console.WriteStr(nbuf);
		Console.WriteStr(" empty "); Console.WriteStr(ebuf); Console.WriteLn
	END Check;

	PROCEDURE Links* (IN path, name: ARRAY OF CHAR);
	(** Открыть документ через Views.OldView и напечатать цели ссылок. *)
		VAR loc: Files.Locator; v: Views.View; m: TextModels.Model; nbuf: ARRAY 16 OF CHAR;
			rd: TextModels.Reader; ch: CHAR; cmd: ARRAY maxPath OF CHAR; n: INTEGER;
			l: StdLinks.Link;
	BEGIN
		EnsureConverters;
		loc := Files.dir.This(path);
		IF loc.res # 0 THEN
			Console.WriteStr("Dev_devOdc: no path " + path); Console.WriteLn; INC(Failed); RETURN
		END;
		v := Views.OldView(loc, name$);
		IF v = NIL THEN
			Console.WriteStr("Dev_devOdc: no view " + path + "/" + name); Console.WriteLn; INC(Failed); RETURN
		END;
		IF ~(v IS TextViews.View) THEN
			Console.WriteStr("Dev_devOdc: not a text view "); Console.WriteLn; INC(Failed); RETURN
		END;
		m := v(TextViews.View).ThisModel();
		n := 0; rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF (ch = TextModels.viewcode) & (rd.view # NIL) THEN
				IF rd.view IS StdLinks.Link THEN
					l := rd.view(StdLinks.Link);
					IF l.leftSide THEN
						l.GetCmd(cmd); INC(n);
						Console.WriteStr("link "); Console.WriteStr(cmd); Console.WriteLn
					END
				END
			END
		END;
		Console.WriteStr("Dev_devOdc: links "); Strings.IntToString(n, nbuf);
		Console.WriteStr(nbuf); Console.WriteLn
	END Links;

END Dev_devOdc.
