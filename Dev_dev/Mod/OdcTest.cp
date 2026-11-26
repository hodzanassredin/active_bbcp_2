MODULE Dev_devOdcTest;
(** Проверки конвертера .md → .odc без окна.

	Образец разметки даёт ожидаемые атрибуты (заголовок крупнее и жирный,
	жирный и курсив, моноширинные код и таблица), ссылки [текст](Путь.md)
	и !Модуль.Процедура становятся StdLinks.Link с командой OpenDoc или
	командой модуля. Отдельный тест пишет .odc и открывает его через
	Views.OldView: ссылки переживают сохранение и чтение. *)

	IMPORT Strings, Files, Documents, Views, Fonts, TextModels, TextViews, StdLinks,
		UnitAssert, Dev_devOdc;

	CONST
		LF = 0AX;

	VAR
		md: ARRAY 8192 OF CHAR;

	(* Консольный ConsFonts игнорирует запрошенные начертание, размер и
	   шрифт и всегда возвращает один и тот же "*". Для проверки атрибутов
	   подменяем каталог Fonts на честный: он сохраняет запрошенное. *)
	TYPE
		TFont = POINTER TO RECORD (Fonts.Font) END;
		TDir = POINTER TO RECORD (Fonts.Directory) END;

	PROCEDURE (f: TFont) GetBounds (OUT asc, dsc, w: INTEGER);
	BEGIN asc := f.size; dsc := f.size DIV 3; w := f.size DIV 2
	END GetBounds;

	PROCEDURE (f: TFont) StringWidth (IN s: ARRAY OF CHAR): INTEGER;
	BEGIN RETURN LEN(s$) * (f.size DIV 2)
	END StringWidth;

	PROCEDURE (f: TFont) SStringWidth (IN s: ARRAY OF SHORTCHAR): INTEGER;
	BEGIN RETURN 0
	END SStringWidth;

	PROCEDURE (f: TFont) IsAlien (): BOOLEAN;
	BEGIN RETURN FALSE
	END IsAlien;

	PROCEDURE (d: TDir) This (typeface: Fonts.Typeface; size: INTEGER;
		style: SET; weight: INTEGER): Fonts.Font;
		VAR f: TFont;
	BEGIN
		NEW(f); f.Init(typeface, size, style, weight); RETURN f
	END This;

	PROCEDURE (d: TDir) Default (): Fonts.Font;
	BEGIN RETURN d.This("Courier", 10 * Fonts.point, {}, Fonts.normal)
	END Default;

	PROCEDURE (d: TDir) TypefaceList (): Fonts.TypefaceInfo;
	BEGIN RETURN NIL
	END TypefaceList;

	PROCEDURE SetupModule*;
		VAR d: TDir;
	BEGIN
		NEW(d); Fonts.SetDir(d)
	END SetupModule;

	PROCEDURE SLen (IN s: ARRAY OF CHAR): INTEGER;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < LEN(s)) & (s[i] # 0X) DO INC(i) END;
		RETURN i
	END SLen;

	PROCEDURE L (IN s: ARRAY OF CHAR);
		VAR n, i: INTEGER;
	BEGIN
		n := SLen(md); i := 0;
		WHILE s[i] # 0X DO md[n] := s[i]; INC(n); INC(i) END;
		md[n] := LF; md[n + 1] := 0X
	END L;

	PROCEDURE BuildSample;
	BEGIN
		md := "";
		L("# Heading One");
		L("");
		L("Body **bold** and *italic* and `x := 1`.");
		L("");
		L("## Heading Two");
		L("");
		L("- first item");
		L("- second item");
		L("");
		L("| Name | Value |");
		L("|------|-------|");
		L("| `a`  | 1     |");
		L("| b    | 22    |");
		L("");
		L("See [Other page](Other.md), [command](!Demo.Run), and !Demo.Stop.");
		L("External [guide](https://example.org/x).");
		L("");
		L("```pascal");
		L("MODULE Demo;");
		L("END Demo.");
		L("```")
	END BuildSample;

	PROCEDURE FindAttr (m: TextModels.Model; IN word: ARRAY OF CHAR;
		OUT found: BOOLEAN; OUT attr: TextModels.Attributes);
		VAR rd: TextModels.Reader; ch: CHAR; n, wl: INTEGER;
			first: TextModels.Attributes;
	BEGIN
		found := FALSE; attr := NIL; first := NIL;
		wl := SLen(word); n := 0;
		rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF ch = TextModels.viewcode THEN
				n := 0
			ELSIF (n < wl) & (ch = word[n]) THEN
				IF n = 0 THEN first := rd.attr END;
				INC(n);
				IF n = wl THEN found := TRUE; attr := first; RETURN END
			ELSE
				n := 0;
				IF (wl > 0) & (ch = word[0]) THEN n := 1; first := rd.attr END
			END
		END
	END FindAttr;

	PROCEDURE HasLink (m: TextModels.Model; IN cmd: ARRAY OF CHAR): BOOLEAN;
		VAR rd: TextModels.Reader; ch: CHAR; l: StdLinks.Link;
			s: ARRAY 512 OF CHAR;
	BEGIN
		rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF (ch = TextModels.viewcode) & (rd.view # NIL) & (rd.view IS StdLinks.Link) THEN
				l := rd.view(StdLinks.Link);
				IF l.leftSide THEN
					l.GetCmd(s);
					IF s = cmd THEN RETURN TRUE END
				END
			END
		END;
		RETURN FALSE
	END HasLink;

	PROCEDURE CountLinks (m: TextModels.Model): INTEGER;
		VAR rd: TextModels.Reader; ch: CHAR; l: StdLinks.Link; n: INTEGER;
	BEGIN
		n := 0; rd := m.NewReader(NIL);
		WHILE rd.Pos() < m.Length() DO
			rd.ReadChar(ch);
			IF (ch = TextModels.viewcode) & (rd.view # NIL) & (rd.view IS StdLinks.Link) THEN
				l := rd.view(StdLinks.Link);
				IF l.leftSide THEN INC(n) END
			END
		END;
		RETURN n
	END CountLinks;

	PROCEDURE Convert (OUT m: TextModels.Model);
	BEGIN
		BuildSample;
		Dev_devOdc.ConvertString(md, "Obx/Docu", m)
	END Convert;

	PROCEDURE TestAttributes*;
		VAR m: TextModels.Model; found: BOOLEAN;
			a, body: TextModels.Attributes;
	BEGIN
		Convert(m);
		FindAttr(m, "Heading One", found, a);
		UnitAssert.True(found, "заголовок найден");
		UnitAssert.EqualInt(Fonts.bold, a.font.weight, "заголовок жирный");
		FindAttr(m, "Body", found, body);
		UnitAssert.True(found, "обычный текст найден");
		UnitAssert.True(a.font.size > body.font.size, "заголовок крупнее текста");

		FindAttr(m, "bold", found, a);
		UnitAssert.True(found, "жирное слово найдено");
		UnitAssert.EqualInt(Fonts.bold, a.font.weight, "**жирный** — вес bold");

		FindAttr(m, "italic", found, a);
		UnitAssert.True(found, "курсив найден");
		UnitAssert.True(Fonts.italic IN a.font.style, "*курсив* — начертание italic");

		FindAttr(m, "x := 1", found, a);
		UnitAssert.True(found, "код в строке найден");
		UnitAssert.EqualStr("Courier", a.font.typeface, "`код` — моноширинный шрифт");

		FindAttr(m, "MODULE Demo;", found, a);
		UnitAssert.True(found, "строка блока кода найдена");
		UnitAssert.EqualStr("Courier", a.font.typeface, "блок ``` — моноширинный шрифт")
	END TestAttributes;

	PROCEDURE TestListsAndTables*;
		VAR m: TextModels.Model; found: BOOLEAN; a: TextModels.Attributes;
	BEGIN
		Convert(m);
		FindAttr(m, "• first item", found, a);
		UnitAssert.True(found, "маркер списка и текст");
		FindAttr(m, "• second item", found, a);
		UnitAssert.True(found, "второй элемент списка");

		FindAttr(m, "| Name | Value |", found, a);
		UnitAssert.True(found, "шапка таблицы выровнена");
		UnitAssert.EqualStr("Courier", a.font.typeface, "таблица — моноширинный шрифт");
		FindAttr(m, "| a    | 1     |", found, a);
		UnitAssert.True(found, "ячейки таблицы выровнены по ширине столбцов");
		FindAttr(m, "| b    | 22    |", found, a);
		UnitAssert.True(found, "вторая строка таблицы выровнена");
		FindAttr(m, "|------|-------|", found, a);
		UnitAssert.True(found, "линия под шапкой таблицы")
	END TestListsAndTables;

	PROCEDURE TestLinks*;
		VAR m: TextModels.Model;
	BEGIN
		Convert(m);
		UnitAssert.EqualInt(4, CountLinks(m), "четыре ссылки");
		UnitAssert.True(HasLink(m, "StdCmds.OpenDoc('Obx/Docu/Other.odc')"),
			"ссылка на .md ведёт на .odc рядом с документом");
		UnitAssert.True(HasLink(m, "Demo.Run"), "ссылка [текст](!Модуль.Процедура) — команда");
		UnitAssert.True(HasLink(m, "Demo.Stop"), "запись !Модуль.Процедура — команда");
		UnitAssert.True(HasLink(m, "Dialog.OpenExternal('https://example.org/x')"),
			"внешняя ссылка открывается через Dialog.OpenExternal")
	END TestLinks;

	PROCEDURE TestOdcRoundTrip*;
		VAR m, m2: TextModels.Model; tv: TextViews.View; v: Views.View; a, body: TextModels.Attributes;
			loc: Files.Locator; f: Files.File; res: INTEGER; name: Files.Name; found: BOOLEAN;
	BEGIN
		Convert(m);
		tv := TextViews.dir.New(m);
		UnitAssert.NotNil(tv, "текстовый вид создан");
		loc := Files.dir.This("");
		name := "DevOdcTest.odc";
		Files.dir.Delete(loc, name);
		loc := Files.dir.This("");
		f := Files.dir.New(loc, FALSE);
		UnitAssert.NotNil(f, "временный файл создан");
		Documents.ExportDocument(tv, f);
		f.Register("DevOdcTest", "odc", Files.dontAsk, res);
		UnitAssert.EqualInt(0, res, "документ сохранён");
		v := Views.OldView(loc, name);
		UnitAssert.NotNil(v, "документ открыт через Views.OldView");
		UnitAssert.True(v IS TextViews.View, "открылся текстовый вид");
		IF v IS TextViews.View THEN
			m2 := v(TextViews.View).ThisModel();
			UnitAssert.True(HasLink(m2, "StdCmds.OpenDoc('Obx/Docu/Other.odc')"),
				"ссылка на документ пережила запись и чтение");
			UnitAssert.True(HasLink(m2, "Demo.Run"), "ссылка на команду пережила запись и чтение");
			FindAttr(m2, "Heading One", found, a);
			UnitAssert.True(found, "заголовок пережил запись и чтение");
			UnitAssert.EqualInt(Fonts.bold, a.font.weight, "жирность заголовка пережила запись");
			FindAttr(m2, "Body", found, body);
			UnitAssert.True(found, "текст пережил запись");
			UnitAssert.True(a.font.size > body.font.size, "размер заголовка пережил запись");
			FindAttr(m2, "x := 1", found, a);
			UnitAssert.True(found, "код пережил запись");
			UnitAssert.EqualStr("Courier", a.font.typeface, "моноширинный шрифт пережил запись")
		END;
		Files.dir.Delete(loc, name)
	END TestOdcRoundTrip;

END Dev_devOdcTest.
