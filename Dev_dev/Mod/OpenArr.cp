MODULE Dev_devOpenArr;
(** Тесты многомерных открытых массивов.

	Параметр ARRAY OF ARRAY OF T (IN, VAR, по значению) получает адрес и
	по длине на каждое измерение; проверяются LEN по всем измерениям, чтение
	и запись элементов, параметр после открытого массива, проброс
	открытого параметра дальше (целиком и срезом a[i]) и динамические
	массивы в куче (NEW(p, n, m)). ARRAY OF BYTE от многомерного массива не
	проверяется: SYSTEM.BYTE в DevCPT выключен, из исходника такой параметр
	не объявить (ветка bytetyp в VarParDynArr недостижима). *)

	IMPORT S := SYSTEM, UnitAssert;

	TYPE
		Rec = RECORD x, y: INTEGER END;
		Names = ARRAY 3 OF ARRAY 8 OF CHAR;
		Ints = ARRAY 4 OF ARRAY 5 OF INTEGER;
		Cube = ARRAY 2 OF ARRAY 3 OF ARRAY 4 OF INTEGER;
		Recs = ARRAY 3 OF ARRAY 2 OF Rec;
		Holder = RECORD pad: INTEGER; n: Names; c: Cube END;
		PInts = POINTER TO ARRAY OF ARRAY OF INTEGER;
		PCube = POINTER TO ARRAY OF ARRAY OF ARRAY OF INTEGER;
		PChars = POINTER TO ARRAY OF ARRAY OF CHAR;

	VAR
		gNames: Names; gInts: Ints; gCube: Cube; gRecs: Recs; gHold: Holder;

	(* ---------- заполнение и проверка ---------- *)

	PROCEDURE FillNames (VAR a: Names);
	BEGIN a[0] := "zero"; a[1] := "one"; a[2] := "two"
	END FillNames;

	PROCEDURE FillInts (VAR a: Ints);
		VAR i, j: INTEGER;
	BEGIN FOR i := 0 TO 3 DO FOR j := 0 TO 4 DO a[i, j] := i * 10 + j END END
	END FillInts;

	PROCEDURE FillCube (VAR a: Cube);
		VAR i, j, k: INTEGER;
	BEGIN
		FOR i := 0 TO 1 DO FOR j := 0 TO 2 DO FOR k := 0 TO 3 DO
			a[i, j, k] := i * 100 + j * 10 + k
		END END END
	END FillCube;

	PROCEDURE FillRecs (VAR a: Recs);
		VAR i, j: INTEGER;
	BEGIN FOR i := 0 TO 2 DO FOR j := 0 TO 1 DO a[i, j].x := i; a[i, j].y := j + 7 END END
	END FillRecs;

	(* ---------- вызываемые с открытыми параметрами ---------- *)

	PROCEDURE NamesIn (IN a: ARRAY OF ARRAY OF CHAR; tail: INTEGER);
	BEGIN
		UnitAssert.EqualInt(3, LEN(a), "IN names LEN(a)");
		UnitAssert.EqualInt(8, LEN(a, 1), "IN names LEN(a, 1)");
		UnitAssert.EqualInt(77, tail, "IN names: parameter after open array");
		UnitAssert.EqualStr("zero", a[0], "IN names a[0]");
		UnitAssert.EqualStr("one", a[1], "IN names a[1]");
		UnitAssert.EqualStr("two", a[2], "IN names a[2]");
		UnitAssert.True(a[2, 1] = "w", "IN names a[2, 1]")
	END NamesIn;

	PROCEDURE Row (IN r: ARRAY OF CHAR; IN exp: ARRAY OF CHAR; len: INTEGER);
	BEGIN
		UnitAssert.EqualInt(len, LEN(r), "slice LEN");
		UnitAssert.EqualStr(exp, r, "slice content")
	END Row;

	PROCEDURE NamesFwd (IN b: ARRAY OF ARRAY OF CHAR);
	BEGIN NamesIn(b, 77); Row(b[1], "one", 8); Row(b[2], "two", 8)
	END NamesFwd;

	PROCEDURE NamesVar (VAR a: ARRAY OF ARRAY OF CHAR; tail: INTEGER);
	BEGIN
		UnitAssert.EqualInt(3, LEN(a), "VAR names LEN(a)");
		UnitAssert.EqualInt(8, LEN(a, 1), "VAR names LEN(a, 1)");
		UnitAssert.EqualInt(55, tail, "VAR names: parameter after open array");
		a[1] := "uno"; a[2, 0] := "T"
	END NamesVar;

	PROCEDURE NamesVal (a: ARRAY OF ARRAY OF CHAR; tail: INTEGER);
	BEGIN
		UnitAssert.EqualInt(3, LEN(a), "value names LEN(a)");
		UnitAssert.EqualInt(8, LEN(a, 1), "value names LEN(a, 1)");
		UnitAssert.EqualInt(33, tail, "value names: parameter after open array");
		UnitAssert.EqualStr("one", a[1], "value names a[1]");
		a[1] := "copy";	(* копия: вызывающий не меняется *)
		UnitAssert.EqualStr("copy", a[1], "value names write")
	END NamesVal;

	PROCEDURE IntsSum (IN a: ARRAY OF ARRAY OF INTEGER): INTEGER;
		VAR i, j, s: INTEGER;
	BEGIN
		s := 0;
		FOR i := 0 TO LEN(a) - 1 DO FOR j := 0 TO LEN(a, 1) - 1 DO s := s + a[i, j] END END;
		RETURN s
	END IntsSum;

	PROCEDURE IntsVar (VAR a: ARRAY OF ARRAY OF INTEGER; n, m: INTEGER);
		VAR i, j: INTEGER;
	BEGIN
		UnitAssert.EqualInt(n, LEN(a), "VAR ints LEN(a)");
		UnitAssert.EqualInt(m, LEN(a, 1), "VAR ints LEN(a, 1)");
		FOR i := 0 TO LEN(a) - 1 DO FOR j := 0 TO LEN(a, 1) - 1 DO
			UnitAssert.EqualInt(i * 10 + j, a[i, j], "VAR ints read");
			a[i, j] := -a[i, j]
		END END
	END IntsVar;

	PROCEDURE IntsVal (a: ARRAY OF ARRAY OF INTEGER; n, m: INTEGER);
		VAR i, j: INTEGER;
	BEGIN
		UnitAssert.EqualInt(n, LEN(a), "value ints LEN(a)");
		UnitAssert.EqualInt(m, LEN(a, 1), "value ints LEN(a, 1)");
		FOR i := 0 TO LEN(a) - 1 DO FOR j := 0 TO LEN(a, 1) - 1 DO
			UnitAssert.EqualInt(i * 10 + j, a[i, j], "value ints read");
			a[i, j] := 0
		END END
	END IntsVal;

	PROCEDURE CubeCheck (IN a: ARRAY OF ARRAY OF ARRAY OF INTEGER; n, m, k: INTEGER);
		VAR i, j, l: INTEGER;
	BEGIN
		UnitAssert.EqualInt(n, LEN(a), "cube LEN(a)");
		UnitAssert.EqualInt(m, LEN(a, 1), "cube LEN(a, 1)");
		UnitAssert.EqualInt(k, LEN(a, 2), "cube LEN(a, 2)");
		FOR i := 0 TO LEN(a) - 1 DO FOR j := 0 TO LEN(a, 1) - 1 DO FOR l := 0 TO LEN(a, 2) - 1 DO
			UnitAssert.EqualInt(i * 100 + j * 10 + l, a[i, j, l], "cube read")
		END END END
	END CubeCheck;

	PROCEDURE CubeSlice (IN a: ARRAY OF ARRAY OF INTEGER; base, m, k: INTEGER);
		VAR j, l: INTEGER;
	BEGIN
		UnitAssert.EqualInt(m, LEN(a), "cube slice LEN(a)");
		UnitAssert.EqualInt(k, LEN(a, 1), "cube slice LEN(a, 1)");
		FOR j := 0 TO m - 1 DO FOR l := 0 TO k - 1 DO
			UnitAssert.EqualInt(base + j * 10 + l, a[j, l], "cube slice read")
		END END
	END CubeSlice;

	PROCEDURE CubeFwd (IN a: ARRAY OF ARRAY OF ARRAY OF INTEGER; n, m, k: INTEGER);
	BEGIN CubeCheck(a, n, m, k); CubeSlice(a[1], 100, m, k)
	END CubeFwd;

	PROCEDURE CubeVar (VAR a: ARRAY OF ARRAY OF ARRAY OF INTEGER; tail: INTEGER);
	BEGIN
		UnitAssert.EqualInt(2, LEN(a), "VAR cube LEN(a)");
		UnitAssert.EqualInt(3, LEN(a, 1), "VAR cube LEN(a, 1)");
		UnitAssert.EqualInt(4, LEN(a, 2), "VAR cube LEN(a, 2)");
		UnitAssert.EqualInt(99, tail, "VAR cube: parameter after open array");
		a[1, 2, 3] := 4242
	END CubeVar;

	PROCEDURE CubeVal (a: ARRAY OF ARRAY OF ARRAY OF INTEGER; tail: INTEGER);
	BEGIN
		UnitAssert.EqualInt(11, tail, "value cube: parameter after open array");
		CubeCheck(a, 2, 3, 4);
		a[0, 0, 0] := -1
	END CubeVal;

	PROCEDURE RecsCheck (VAR a: ARRAY OF ARRAY OF Rec; tail: INTEGER);
		VAR i, j: INTEGER;
	BEGIN
		UnitAssert.EqualInt(3, LEN(a), "recs LEN(a)");
		UnitAssert.EqualInt(2, LEN(a, 1), "recs LEN(a, 1)");
		UnitAssert.EqualInt(66, tail, "recs: parameter after open array");
		FOR i := 0 TO 2 DO FOR j := 0 TO 1 DO
			UnitAssert.EqualInt(i, a[i, j].x, "recs x");
			UnitAssert.EqualInt(j + 7, a[i, j].y, "recs y")
		END END;
		a[2, 1].x := 500
	END RecsCheck;

	PROCEDURE RecsVal (a: ARRAY OF ARRAY OF Rec);
	BEGIN
		UnitAssert.EqualInt(3, LEN(a), "value recs LEN(a)");
		UnitAssert.EqualInt(2, LEN(a, 1), "value recs LEN(a, 1)");
		UnitAssert.EqualInt(8, a[1, 1].y, "value recs read")
	END RecsVal;

	(* ---------- тесты ---------- *)

	PROCEDURE TestCharGlobal*;
		(** Глобальный ARRAY 3 OF ARRAY 8 OF CHAR через IN, VAR и по значению. *)
	BEGIN
		FillNames(gNames);
		NamesIn(gNames, 77);
		NamesVal(gNames, 33);
		UnitAssert.EqualStr("one", gNames[1], "value param did not change caller");
		NamesVar(gNames, 55);
		UnitAssert.EqualStr("uno", gNames[1], "VAR names write a[1]");
		UnitAssert.EqualStr("Two", gNames[2], "VAR names write a[2, 0]")
	END TestCharGlobal;

	PROCEDURE TestCharLocalAndForward*;
		(** Локальный массив и проброс открытого параметра дальше, целиком и срезом. *)
		VAR loc: Names;
	BEGIN
		FillNames(loc);
		NamesFwd(loc);
		NamesVar(loc, 55);
		UnitAssert.EqualStr("uno", loc[1], "local VAR write")
	END TestCharLocalAndForward;

	PROCEDURE TestCharField*;
		(** Поле записи как фактический параметр. *)
	BEGIN
		FillNames(gHold.n);
		NamesFwd(gHold.n);
		NamesVal(gHold.n, 33)
	END TestCharField;

	PROCEDURE TestCharHeap*;
		(** Динамический массив из кучи POINTER TO ARRAY OF ARRAY OF CHAR. *)
		VAR p: PChars;
	BEGIN
		NEW(p, 3, 8);
		UnitAssert.EqualInt(3, LEN(p^), "heap chars LEN");
		UnitAssert.EqualInt(8, LEN(p^, 1), "heap chars LEN 1");
		p[0] := "zero"; p[1] := "one"; p[2] := "two";
		NamesIn(p^, 77);
		NamesFwd(p^);
		NamesVal(p^, 33);
		NamesVar(p^, 55);
		UnitAssert.EqualStr("uno", p[1], "heap chars VAR write")
	END TestCharHeap;

	PROCEDURE TestInts*;
		(** ARRAY OF ARRAY OF INTEGER: глобальный, локальный, куча; IN, VAR, по значению. *)
		VAR loc: Ints; p: PInts; i, j, s: INTEGER;
	BEGIN
		FillInts(gInts); FillInts(loc);
		s := 0; FOR i := 0 TO 3 DO FOR j := 0 TO 4 DO s := s + i * 10 + j END END;
		UnitAssert.EqualInt(s, IntsSum(gInts), "global sum");
		UnitAssert.EqualInt(s, IntsSum(loc), "local sum");
		IntsVal(loc, 4, 5);
		UnitAssert.EqualInt(34, loc[3, 4], "value ints did not change caller");
		IntsVar(loc, 4, 5);
		UnitAssert.EqualInt(-34, loc[3, 4], "VAR ints write");
		NEW(p, 4, 5);
		FOR i := 0 TO 3 DO FOR j := 0 TO 4 DO p[i, j] := i * 10 + j END END;
		UnitAssert.EqualInt(s, IntsSum(p^), "heap sum");
		IntsVal(p^, 4, 5);
		IntsVar(p^, 4, 5);
		UnitAssert.EqualInt(-21, p[2, 1], "heap VAR write")
	END TestInts;

	PROCEDURE TestCube*;
		(** Три измерения: глобальный, поле записи, локальный, куча, проброс и срез. *)
		VAR loc: Cube; p: PCube; i, j, k: INTEGER;
	BEGIN
		FillCube(gCube); FillCube(loc); FillCube(gHold.c);
		CubeCheck(gCube, 2, 3, 4);
		CubeFwd(loc, 2, 3, 4);
		CubeFwd(gHold.c, 2, 3, 4);
		CubeVal(loc, 11);
		UnitAssert.EqualInt(0, loc[0, 0, 0], "value cube did not change caller");
		CubeVar(gCube, 99);
		UnitAssert.EqualInt(4242, gCube[1, 2, 3], "VAR cube write");
		NEW(p, 2, 3, 4);
		UnitAssert.EqualInt(2, LEN(p^), "heap cube LEN");
		UnitAssert.EqualInt(3, LEN(p^, 1), "heap cube LEN 1");
		UnitAssert.EqualInt(4, LEN(p^, 2), "heap cube LEN 2");
		FOR i := 0 TO 1 DO FOR j := 0 TO 2 DO FOR k := 0 TO 3 DO
			p[i, j, k] := i * 100 + j * 10 + k
		END END END;
		CubeFwd(p^, 2, 3, 4);
		CubeVal(p^, 11);
		CubeVar(p^, 99);
		UnitAssert.EqualInt(4242, p[1, 2, 3], "heap VAR cube write")
	END TestCube;

	PROCEDURE TestRecords*;
		(** Элемент-запись. *)
		VAR loc: Recs;
	BEGIN
		FillRecs(gRecs); FillRecs(loc);
		RecsCheck(gRecs, 66);
		UnitAssert.EqualInt(500, gRecs[2, 1].x, "recs VAR write");
		RecsVal(loc);
		RecsCheck(loc, 66)
	END TestRecords;

	PROCEDURE TestHeapShapes*;
		(** Динамические массивы в куче: LEN и индексы без параметров. *)
		VAR p: PInts; q: PCube; i, j, k, s: INTEGER;
	BEGIN
		NEW(p, 7, 3);
		UnitAssert.EqualInt(7, LEN(p^), "heap 2D LEN");
		UnitAssert.EqualInt(3, LEN(p^, 1), "heap 2D LEN 1");
		FOR i := 0 TO 6 DO FOR j := 0 TO 2 DO p[i, j] := i * 3 + j END END;
		UnitAssert.EqualInt(20, p[6, 2], "heap 2D last");
		UnitAssert.EqualInt(S.ADR(p[0, 0]) + 4 * 20, S.ADR(p[6, 2]), "heap 2D dense layout");
		NEW(q, 3, 1, 5);
		UnitAssert.EqualInt(3, LEN(q^), "heap 3D LEN");
		UnitAssert.EqualInt(1, LEN(q^, 1), "heap 3D LEN 1");
		UnitAssert.EqualInt(5, LEN(q^, 2), "heap 3D LEN 2");
		s := 0;
		FOR i := 0 TO 2 DO FOR j := 0 TO 0 DO FOR k := 0 TO 4 DO q[i, j, k] := s; INC(s) END END END;
		UnitAssert.EqualInt(14, q[2, 0, 4], "heap 3D last");
		UnitAssert.EqualInt(S.ADR(q[0, 0, 0]) + 4 * 14, S.ADR(q[2, 0, 4]), "heap 3D dense layout")
	END TestHeapShapes;

END Dev_devOpenArr.
