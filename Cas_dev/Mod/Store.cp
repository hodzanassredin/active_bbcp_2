MODULE Cas_devStore;
(** Тесты CasStore (память) и CasFiles (каталог): идемпотентная запись,
	чтение, отсутствие, ссылки «сравнить и заменить», проверка хеша при
	чтении из файла. Каталог — UnitTmp/cas-store, тесты убирают за собой. *)

	IMPORT Files, CasHash, CasStore, CasFiles, UnitAssert, UnitRunner;

	VAR
		path: Files.Name;

	PROCEDURE Bytes (IN s: ARRAY OF CHAR; OUT b: ARRAY OF BYTE; OUT n: INTEGER);
	BEGIN
		n := 0;
		WHILE s[n] # 0X DO b[n] := SHORT(SHORT(ORD(s[n]))); INC(n) END
	END Bytes;

	PROCEDURE Clean;
	(* удалить все файлы каталога хранилища *)
		VAR loc: Files.Locator; fi: Files.FileInfo;
	BEGIN
		loc := Files.dir.This(path);
		fi := Files.dir.FileList(loc);
		WHILE fi # NIL DO Files.dir.Delete(loc, fi.name); fi := fi.next END
	END Clean;

	PROCEDURE FileCount (): INTEGER;
		VAR loc: Files.Locator; fi: Files.FileInfo; n: INTEGER;
	BEGIN
		loc := Files.dir.This(path); fi := Files.dir.FileList(loc); n := 0;
		WHILE fi # NIL DO INC(n); fi := fi.next END;
		RETURN n
	END FileCount;

	PROCEDURE SetupModule*;
		VAR tmp: Files.Name;
	BEGIN
		UnitRunner.TempDir(tmp);
		path := tmp$ + "/cas-store"
	END SetupModule;

	PROCEDURE Setup*;
	BEGIN
		Clean
	END Setup;

	PROCEDURE TeardownModule*;
	BEGIN
		Clean
	END TeardownModule;

	PROCEDURE PutGet (s: CasStore.Store; tag: ARRAY OF CHAR);
		VAR b: ARRAY 64 OF BYTE; n, res, i: INTEGER; x, y, want: CasHash.Id; got: CasStore.Block;
	BEGIN
		Bytes("hello, blocks", b, n);
		s.Put(b, n, x, res);
		UnitAssert.EqualInt(CasStore.ok, res, tag);
		CasHash.Of(b, 0, n, want);
		UnitAssert.True(CasHash.Equal(want, x), "Id = SHA-256");
		UnitAssert.True(s.Has(x), "Has после Put");
		s.Put(b, n, y, res);
		UnitAssert.True(CasHash.Equal(x, y), "повтор даёт тот же Id");
		s.Get(x, got, res);
		UnitAssert.EqualInt(CasStore.ok, res, "Get");
		UnitAssert.True(got # NIL, "данные");
		IF got # NIL THEN
			UnitAssert.EqualInt(n, LEN(got), "длина");
			i := 0; WHILE (i < n) & (got[i] = b[i]) DO INC(i) END;
			UnitAssert.EqualInt(n, i, "содержимое")
		END
	END PutGet;

	PROCEDURE Absent (s: CasStore.Store);
		VAR b: ARRAY 8 OF BYTE; n, res: INTEGER; x: CasHash.Id; got: CasStore.Block;
	BEGIN
		Bytes("nothing", b, n);
		CasHash.Of(b, 0, n, x);
		UnitAssert.True(~s.Has(x), "нет блока");
		s.Get(x, got, res);
		UnitAssert.EqualInt(CasStore.absent, res, "Get отсутствующего");
		UnitAssert.True(got = NIL, "без данных")
	END Absent;

	PROCEDURE Refs (s: CasStore.Store);
		VAR b: ARRAY 8 OF BYTE; n, res: INTEGER; a, c, nil, cur: CasHash.Id;
	BEGIN
		CasHash.Clear(nil);
		Bytes("a", b, n); CasHash.Of(b, 0, n, a);
		Bytes("c", b, n); CasHash.Of(b, 0, n, c);
		s.GetRef("main", cur, res);
		UnitAssert.EqualInt(CasStore.ok, res, "нет ссылки — не ошибка");
		UnitAssert.True(CasHash.IsNil(cur), "нет ссылки — пустой Id");
		UnitAssert.True(s.CasRef("main", nil, a), "пусто -> a");
		UnitAssert.True(~s.CasRef("main", nil, c), "ожидали пусто, а там a");
		s.GetRef("main", cur, res);
		UnitAssert.True(CasHash.Equal(a, cur), "отказ не меняет ссылку");
		UnitAssert.True(s.CasRef("main", a, c), "a -> c");
		s.GetRef("main", cur, res);
		UnitAssert.True(CasHash.Equal(c, cur), "ссылка = c");
		s.GetRef("other", cur, res);
		UnitAssert.True(CasHash.IsNil(cur), "ссылки независимы")
	END Refs;

	PROCEDURE TestMemPutGet*;
	BEGIN
		PutGet(CasStore.NewMem(), "память")
	END TestMemPutGet;

	PROCEDURE TestMemDedup*;
		VAR m: CasStore.Mem; b: ARRAY 8 OF BYTE; n, res: INTEGER; x: CasHash.Id;
	BEGIN
		m := CasStore.NewMem();
		Bytes("same", b, n);
		m.Put(b, n, x, res); m.Put(b, n, x, res);
		UnitAssert.EqualInt(1, m.blocks, "один блок");
		UnitAssert.EqualInt(n, m.bytes, "байты одного блока")
	END TestMemDedup;

	PROCEDURE TestMemAbsent*;
	BEGIN
		Absent(CasStore.NewMem())
	END TestMemAbsent;

	PROCEDURE TestMemRefs*;
	BEGIN
		Refs(CasStore.NewMem())
	END TestMemRefs;

	PROCEDURE PutEmpty;
		VAR m: CasStore.Store; b: ARRAY 1 OF BYTE; x: CasHash.Id; res: INTEGER;
	BEGIN
		m := CasStore.NewMem();
		m.Put(b, 0, x, res)
	END PutEmpty;

	PROCEDURE TestEmptyBlockTraps*;
	(* блок длины 0 запрещён: открытый массив длины 0 не создать *)
	BEGIN
		UnitAssert.Traps(PutEmpty, "Put пустого блока")
	END TestEmptyBlockTraps;

	PROCEDURE TestFilesPutGet*;
		VAR s: CasStore.Store; res: INTEGER;
	BEGIN
		s := CasFiles.New(path, res);
		UnitAssert.EqualInt(CasStore.ok, res, "открыть");
		PutGet(s, "файлы");
		UnitAssert.EqualInt(1, FileCount(), "один файл на блок, временных нет")
	END TestFilesPutGet;

	PROCEDURE TestFilesAbsent*;
		VAR res: INTEGER;
	BEGIN
		Absent(CasFiles.New(path, res))
	END TestFilesAbsent;

	PROCEDURE TestFilesRefs*;
		VAR res: INTEGER;
	BEGIN
		Refs(CasFiles.New(path, res))
	END TestFilesRefs;

	PROCEDURE TestFilesReopen*;
	(* второе хранилище над тем же каталогом видит блоки и ссылки первого *)
		VAR s, t: CasStore.Store; b: ARRAY 8 OF BYTE; n, res: INTEGER; x, nil, cur: CasHash.Id;
	BEGIN
		s := CasFiles.New(path, res);
		Bytes("keep", b, n); s.Put(b, n, x, res);
		CasHash.Clear(nil);
		UnitAssert.True(s.CasRef("head", nil, x), "публикация");
		t := CasFiles.New(path, res);
		UnitAssert.True(t.Has(x), "блок виден");
		t.GetRef("head", cur, res);
		UnitAssert.True(CasHash.Equal(x, cur), "ссылка видна")
	END TestFilesReopen;

	PROCEDURE TestFilesCorrupt*;
	(* испорченный файл блока — результат corrupt, а не данные *)
		VAR s: CasStore.Store; b: ARRAY 16 OF BYTE; n, res: INTEGER; x: CasHash.Id;
			got: CasStore.Block; hex: ARRAY 65 OF CHAR; loc: Files.Locator; f: Files.File;
			w: Files.Writer;
	BEGIN
		s := CasFiles.New(path, res);
		Bytes("precious data", b, n); s.Put(b, n, x, res);
		CasHash.ToHex(x, hex);
		loc := Files.dir.This(path);
		f := Files.dir.Old(loc, hex$, Files.exclusive);
		UnitAssert.True(f # NIL, "файл блока по имени hex");
		IF f # NIL THEN
			w := f.NewWriter(NIL); w.SetPos(0); w.WriteByte(ORD("P"));
			f.Flush; f.Close
		END;
		s.Get(x, got, res);
		UnitAssert.EqualInt(CasStore.corrupt, res, "хеш не сошёлся");
		UnitAssert.True(got = NIL, "без данных")
	END TestFilesCorrupt;

END Cas_devStore.
