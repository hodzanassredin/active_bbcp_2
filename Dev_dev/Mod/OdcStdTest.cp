MODULE Dev_devOdcStdTest;
(** Документ, записанный консольным Documents, читает StdDocuments.

	Окно Help → Examples открывает .odc через StdDocuments.ImportDocument.
	Тест пишет образец консольным Documents.ExportDocument, ставит каталог
	StdDocuments и читает файл ею же: текст и ссылка сохраняются. *)

	IMPORT Files, Stores, Documents, StdDocuments, TextModels, TextViews, StdLinks,
		UnitAssert, Dev_devOdc;

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

	PROCEDURE TestStdReader*;
		VAR md: ARRAY 512 OF CHAR; m, m2: TextModels.Model; tv: TextViews.View;
			loc: Files.Locator; f: Files.File; res: INTEGER; name: Files.Name;
			s: Stores.Store;
	BEGIN
		md := "See [page](Other.md).";
		Dev_devOdc.ConvertString(md, "Obx/Docu", m);
		tv := TextViews.dir.New(m);
		loc := Files.dir.This("");
		name := "DevOdcStdTest.odc";
		Files.dir.Delete(loc, name);
		loc := Files.dir.This("");
		f := Files.dir.New(loc, FALSE);
		UnitAssert.NotNil(f, "временный файл создан");
		Documents.ExportDocument(tv, f);
		f.Register("DevOdcStdTest", "odc", Files.dontAsk, res);
		UnitAssert.EqualInt(0, res, "документ сохранён");
		StdDocuments.Install;
		f := Files.dir.Old(loc, name, Files.shared);
		UnitAssert.NotNil(f, "файл открыт заново");
		s := NIL; StdDocuments.ImportDocument(f, s);
		UnitAssert.NotNil(s, "StdDocuments прочитала документ");
		UnitAssert.True(s IS TextViews.View, "открылся текстовый вид");
		IF s IS TextViews.View THEN
			m2 := s(TextViews.View).ThisModel();
			UnitAssert.True(HasLink(m2, "StdCmds.OpenDoc('Obx/Docu/Other.odc')"),
				"ссылка видна через StdDocuments")
		END;
		Files.dir.Delete(loc, name)
	END TestStdReader;

END Dev_devOdcStdTest.
