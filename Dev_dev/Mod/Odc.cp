MODULE Dev_devOdc;
(** Документы и исходники для окна BlackBox.

	В репозитории тексты лежат как .cp (исходники) и .md (документы), а
	окно BlackBox открывает документы .odc: Help → Examples ищет
	Obx/Docu/Sys-Map.odc, File → Open показывает .odc. Команда All обходит
	каталог установки и рядом с каждым .cp в <Подсистема>/Mod и каждым
	.md в <Подсистема>/Docu (и его подкаталогах языков) кладёт .odc с тем
	же текстом. Разметка .md не разбирается: текст переносится как есть.

	Зовёт tools/gui-world.sh после копирования текстов в каталог установки. *)

	IMPORT Converters, Stores, TextViews, Files, Strings, Console;

	VAR
		Done-, Failed-: INTEGER;	(* счётчики последней команды *)

	PROCEDURE Find (IN imp, exp: ARRAY OF CHAR): Converters.Converter;
		VAR c: Converters.Converter;
	BEGIN
		c := Converters.list;
		WHILE (c # NIL) & ~((c.imp = imp) & ((exp = "") OR (c.exp = exp))) DO c := c.next END;
		RETURN c
	END Find;

	PROCEDURE HasExt (IN name, ext: ARRAY OF CHAR): BOOLEAN;
		VAR n, e, i: INTEGER;
	BEGIN
		n := LEN(name$); e := LEN(ext$);
		IF n <= e + 1 THEN RETURN FALSE END;
		IF name[n - e - 1] # "." THEN RETURN FALSE END;
		FOR i := 0 TO e - 1 DO
			IF name[n - e + i] # ext[i] THEN RETURN FALSE END
		END;
		RETURN TRUE
	END HasExt;

	PROCEDURE Dir* (IN path, ext: ARRAY OF CHAR);
	(** Каждому файлу path/*.ext — документ path/<имя>.odc с тем же текстом. *)
		VAR loc: Files.Locator; f: Files.FileInfo; name: Files.Name;
			imp, exp, c: Converters.Converter; s: Stores.Store;
	BEGIN
		imp := Find("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8");
		exp := Find("Documents.ImportDocument", "Documents.ExportDocument");
		IF (imp = NIL) OR (exp = NIL) THEN	(* консоль: StdConfig не исполнялся *)
			Converters.Register("Documents.ImportDocument", "Documents.ExportDocument", "", "odc", {});
			Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "utf8",
				{Converters.importAll});
			imp := Find("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8");
			exp := Find("Documents.ImportDocument", "Documents.ExportDocument")
		END;
		IF (imp = NIL) OR (exp = NIL) THEN
			Console.WriteStr("Dev_devOdc: converters not found"); Console.WriteLn; INC(Failed); RETURN
		END;
		loc := Files.dir.This(path);
		IF loc.res # 0 THEN RETURN END;
		f := Files.dir.FileList(loc);
		WHILE f # NIL DO
			IF HasExt(f.name, ext) THEN
				name := f.name$; name[LEN(name$) - LEN(ext$) - 1] := 0X;
				c := imp; s := NIL;
				Converters.Import(loc, f.name, c, s);
				IF (s # NIL) & (s IS TextViews.View) THEN
					Converters.Export(loc, name, exp, s(TextViews.View)); INC(Done)
				ELSE
					Console.WriteStr("Dev_devOdc: not a text: " + path + "/" + f.name); Console.WriteLn;
					INC(Failed)
				END
			END;
			f := f.next
		END
	END Dir;

	PROCEDURE All*;
	(** Все подсистемы каталога установки: Mod/*.cp, Docu/*.md, Docu/<язык>/*.md. *)
		VAR subs, langs: Files.LocInfo; n: ARRAY 16 OF CHAR;
	BEGIN
		Done := 0; Failed := 0;
		subs := Files.dir.LocList(Files.dir.This(""));
		WHILE subs # NIL DO
			IF Strings.IsUpper(subs.name[0]) THEN
				Dir(subs.name + "/Mod", "cp");
				Dir(subs.name + "/Docu", "md");
				langs := Files.dir.LocList(Files.dir.This(subs.name + "/Docu"));
				WHILE langs # NIL DO
					Dir(subs.name + "/Docu/" + langs.name, "md");
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

END Dev_devOdc.
