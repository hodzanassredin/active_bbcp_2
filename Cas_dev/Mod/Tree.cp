MODULE Cas_devTree;
(** Тесты CasTree: поток туда и обратно (пустой, малый, многоуровневый),
	один корень на одно содержимое, повторная запись не добавляет блоков,
	пропавший блок — результат absent. Файлы — UnitTmp/cas-tree. *)

	IMPORT SYSTEM, Files, CasHash, CasStore, CasTree, UnitAssert, UnitRunner;

	TYPE
		Data = POINTER TO ARRAY OF BYTE;

	VAR
		loc: Files.Locator;

	PROCEDURE Xs (x: INTEGER): INTEGER;
	BEGIN
		x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 13)));
		x := SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, -17)));
		RETURN SYSTEM.VAL(INTEGER, SYSTEM.VAL(SET, x) / SYSTEM.VAL(SET, SYSTEM.LSH(x, 5)))
	END Xs;

	PROCEDURE Gen (n, seed: INTEGER): Data;
		VAR d: Data; i, x, v: INTEGER;
	BEGIN
		NEW(d, n + 1); x := seed;	(* +1: массив длины 0 не создать *)
		FOR i := 0 TO n - 1 DO
			x := Xs(x); v := x MOD 256;
			IF v >= 128 THEN DEC(v, 256) END;
			d[i] := SHORT(SHORT(v))
		END;
		RETURN d
	END Gen;

	PROCEDURE WriteFile (IN name: Files.Name; d: Data; n: INTEGER);
		VAR f: Files.File; w: Files.Writer; res: INTEGER;
	BEGIN
		f := Files.dir.New(loc, Files.dontAsk);
		w := f.NewWriter(NIL);
		IF n > 0 THEN w.WriteBytes(d, 0, n) END;
		f.Register(name, "", Files.dontAsk, res);
		ASSERT(res = 0, 60)
	END WriteFile;

	PROCEDURE Put (s: CasStore.Store; d: Data; n: INTEGER; OUT root: CasHash.Id; OUT res: INTEGER);
		VAR f: Files.File;
	BEGIN
		WriteFile("in.bin", d, n);
		f := Files.dir.Old(loc, "in.bin", Files.shared);
		CasTree.PutStream(s, f.NewReader(NIL), root, res);
		f.Close
	END Put;

	PROCEDURE Same (s: CasStore.Store; IN root: CasHash.Id; d: Data; n: INTEGER): BOOLEAN;
	(* GetStream в файл out.bin и сравнение с d[0 .. n-1] *)
		VAR f: Files.File; r: Files.Reader; res, i: INTEGER; got: Data;
	BEGIN
		f := Files.dir.New(loc, Files.dontAsk);
		CasTree.GetStream(s, root, f.NewWriter(NIL), res);
		UnitAssert.EqualInt(CasStore.ok, res, "GetStream");
		f.Register("out.bin", "", Files.dontAsk, res);
		f := Files.dir.Old(loc, "out.bin", Files.shared);
		UnitAssert.EqualInt(n, f.Length(), "длина");
		IF f.Length() # n THEN f.Close; RETURN FALSE END;
		NEW(got, n + 1); r := f.NewReader(NIL);
		IF n > 0 THEN r.ReadBytes(got, 0, n) END;
		f.Close;
		i := 0; WHILE (i < n) & (got[i] = d[i]) DO INC(i) END;
		RETURN i = n
	END Same;

	PROCEDURE SetupModule*;
		VAR tmp, path: Files.Name;
	BEGIN
		UnitRunner.TempDir(tmp);
		path := tmp$ + "/cas-tree";
		loc := Files.dir.This(path)
	END SetupModule;

	PROCEDURE TeardownModule*;
	BEGIN
		Files.dir.Delete(loc, "in.bin"); Files.dir.Delete(loc, "out.bin")
	END TeardownModule;

	PROCEDURE RoundTrip (n, seed: INTEGER; tag: ARRAY OF CHAR);
		VAR s: CasStore.Mem; d: Data; root: CasHash.Id; res: INTEGER;
	BEGIN
		s := CasStore.NewMem(); d := Gen(n, seed);
		Put(s, d, n, root, res);
		UnitAssert.EqualInt(CasStore.ok, res, tag);
		UnitAssert.True(Same(s, root, d, n), tag)
	END RoundTrip;

	PROCEDURE TestEmpty*;
	BEGIN
		RoundTrip(0, 1, "пустой поток")
	END TestEmpty;

	PROCEDURE TestSmall*;
	BEGIN
		RoundTrip(1000, 2, "короче куска")
	END TestSmall;

	PROCEDURE TestMultiLevel*;
	(* 1,5 МБ — около 150 кусков: больше, чем детей у одного узла *)
	BEGIN
		RoundTrip(1500000, 3, "два уровня узлов")
	END TestMultiLevel;

	PROCEDURE TestUniform*;
	(* одинаковые байты: границ по маске нет, куски режутся по max *)
		VAR s: CasStore.Mem; d: Data; root: CasHash.Id; res, i: INTEGER;
	BEGIN
		s := CasStore.NewMem(); NEW(d, 300000);
		FOR i := 0 TO LEN(d) - 1 DO d[i] := 0 END;
		Put(s, d, 300000, root, res);
		UnitAssert.EqualInt(CasStore.ok, res, "запись");
		UnitAssert.True(Same(s, root, d, 300000), "чтение");
		UnitAssert.EqualInt(3, s.blocks, "кусок 64 КБ, хвост и корень")
	END TestUniform;

	PROCEDURE TestRootIdentifiesContent*;
		VAR s: CasStore.Mem; d: Data; a, b, c: CasHash.Id; res, blocks: INTEGER;
	BEGIN
		s := CasStore.NewMem(); d := Gen(200000, 4);
		Put(s, d, 200000, a, res);
		blocks := s.blocks;
		Put(s, d, 200000, b, res);
		UnitAssert.True(CasHash.Equal(a, b), "то же содержимое — тот же корень");
		UnitAssert.EqualInt(blocks, s.blocks, "повтор не добавил блоков");
		d[100000] := SHORT(SHORT(d[100000] + 1));
		Put(s, d, 200000, c, res);
		UnitAssert.True(~CasHash.Equal(a, c), "другой байт — другой корень");
		UnitAssert.True(s.blocks - blocks <= 3, "изменились лишь кусок и корень")
	END TestRootIdentifiesContent;

	PROCEDURE TestMissingBlock*;
	(* корень из одного хранилища в другом, пустом: блока нет *)
		VAR s: CasStore.Mem; d: Data; root: CasHash.Id; res: INTEGER; f: Files.File;
	BEGIN
		s := CasStore.NewMem(); d := Gen(5000, 5);
		Put(s, d, 5000, root, res);
		f := Files.dir.New(loc, Files.dontAsk);
		CasTree.GetStream(CasStore.NewMem(), root, f.NewWriter(NIL), res);
		UnitAssert.EqualInt(CasStore.absent, res, "нет корня");
		f.Close
	END TestMissingBlock;

	PROCEDURE TestNotATree*;
	(* Id обычного блока вместо корня дерева — corrupt, а не трап *)
		VAR s: CasStore.Mem; d: Data; id: CasHash.Id; res: INTEGER; f: Files.File;
	BEGIN
		s := CasStore.NewMem(); d := Gen(100, 6);
		s.Put(d, 100, id, res);
		f := Files.dir.New(loc, Files.dontAsk);
		CasTree.GetStream(s, id, f.NewWriter(NIL), res);
		UnitAssert.EqualInt(CasStore.corrupt, res, "не узел");
		f.Close
	END TestNotATree;

END Cas_devTree.
