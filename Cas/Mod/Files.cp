MODULE CasFiles;
(** Хранилище блоков в каталоге файлов (реализация CasStore.Store).

	Блок — файл, имя — 64 знака hex его Id. Ссылка — файл "ref-<имя>" из 32
	байт. Файл пишется под временным именем "tmp-write" и переименовывается
	в постоянное: недописанный блок под своим именем не виден. При чтении
	хеш блока проверяется заново. Хранилищем пользуется один процесс. *)

	IMPORT Files, CasHash, CasStore;

	CONST
		tmpName = "tmp-write";

	TYPE
		Store* = POINTER TO RECORD (CasStore.Store)
			loc: Files.Locator
		END;

	PROCEDURE RefFile (IN name: ARRAY OF CHAR; OUT f: Files.Name);
	BEGIN
		f := "ref-" + name
	END RefFile;

	PROCEDURE (s: Store) Write (IN name: Files.Name; IN data: ARRAY OF BYTE; len: INTEGER;
		OUT res: INTEGER), NEW;
	(* записать файл name целиком: через временное имя и переименование *)
		VAR f: Files.File; w: Files.Writer; r: INTEGER;
	BEGIN
		res := CasStore.failed;
		f := Files.dir.New(s.loc, Files.dontAsk);
		IF f # NIL THEN
			w := f.NewWriter(NIL); w.WriteBytes(data, 0, len);
			f.Register(tmpName, "", Files.dontAsk, r);
			IF r = 0 THEN
				Files.dir.Rename(s.loc, tmpName, name, Files.dontAsk);
				IF s.loc.res = 0 THEN res := CasStore.ok
				ELSE Files.dir.Delete(s.loc, tmpName)
				END
			END
		END
	END Write;

	PROCEDURE (s: Store) Read (IN name: Files.Name; OUT b: CasStore.Block; OUT res: INTEGER), NEW;
	(* прочитать файл name целиком; нет файла — absent *)
		VAR f: Files.File; r: Files.Reader; n: INTEGER;
	BEGIN
		b := NIL;
		f := Files.dir.Old(s.loc, name, Files.shared);
		IF f = NIL THEN res := CasStore.absent
		ELSE
			n := f.Length();
			IF n = 0 THEN res := CasStore.corrupt	(* пустых блоков и ссылок не бывает *)
			ELSE
				NEW(b, n); r := f.NewReader(NIL); r.SetPos(0); r.ReadBytes(b, 0, n);
				IF r.Pos() # n THEN b := NIL; res := CasStore.failed ELSE res := CasStore.ok END
			END;
			f.Close
		END
	END Read;

	PROCEDURE New* (IN path: ARRAY OF CHAR; OUT res: INTEGER): Store;
	(** Хранилище в каталоге path; каталог создаётся при первой записи.
		Пост: res = CasStore.ok, результат # NIL. Временный файл прерванной
		записи удалён. *)
		VAR s: Store;
	BEGIN
		NEW(s); s.loc := Files.dir.This(path);
		Files.dir.Delete(s.loc, tmpName);
		res := CasStore.ok;
		RETURN s
	END New;

	PROCEDURE (s: Store) Has* (IN id: CasHash.Id): BOOLEAN;
		VAR hex: Files.Name; f: Files.File;
	BEGIN
		CasHash.ToHex(id, hex);
		f := Files.dir.Old(s.loc, hex, Files.shared);
		IF f # NIL THEN f.Close END;
		RETURN f # NIL
	END Has;

	PROCEDURE (s: Store) Put* (IN data: ARRAY OF BYTE; len: INTEGER; OUT id: CasHash.Id;
		OUT res: INTEGER);
		VAR hex: Files.Name;
	BEGIN
		ASSERT((len > 0) & (len <= LEN(data)), 20);
		CasHash.Of(data, 0, len, id);
		IF s.Has(id) THEN res := CasStore.ok
		ELSE CasHash.ToHex(id, hex); s.Write(hex, data, len, res)
		END
	END Put;

	PROCEDURE (s: Store) Get* (IN id: CasHash.Id; OUT b: CasStore.Block; OUT res: INTEGER);
		VAR hex: Files.Name; h: CasHash.Id;
	BEGIN
		CasHash.ToHex(id, hex);
		s.Read(hex, b, res);
		IF res = CasStore.ok THEN
			CasHash.Of(b, 0, LEN(b), h);
			IF ~CasHash.Equal(h, id) THEN b := NIL; res := CasStore.corrupt END
		END
	END Get;

	PROCEDURE (s: Store) GetRef* (IN name: ARRAY OF CHAR; OUT id: CasHash.Id; OUT res: INTEGER);
		VAR fn: Files.Name; b: CasStore.Block; i: INTEGER;
	BEGIN
		ASSERT(CasStore.ValidRef(name), 22);
		CasHash.Clear(id);
		RefFile(name, fn); s.Read(fn, b, res);
		IF res = CasStore.absent THEN res := CasStore.ok
		ELSIF res = CasStore.ok THEN
			IF LEN(b) = CasHash.size THEN
				FOR i := 0 TO CasHash.size - 1 DO id[i] := b[i] END
			ELSE res := CasStore.corrupt
			END
		END
	END GetRef;

	PROCEDURE (s: Store) CasRef* (IN name: ARRAY OF CHAR; IN old, new: CasHash.Id): BOOLEAN;
		VAR cur: CasHash.Id; res: INTEGER; fn: Files.Name;
	BEGIN
		ASSERT(CasStore.ValidRef(name), 22);
		s.GetRef(name, cur, res);
		IF (res # CasStore.ok) OR ~CasHash.Equal(cur, old) THEN RETURN FALSE END;
		RefFile(name, fn); s.Write(fn, new, CasHash.size, res);
		RETURN res = CasStore.ok
	END CasRef;

END CasFiles.
