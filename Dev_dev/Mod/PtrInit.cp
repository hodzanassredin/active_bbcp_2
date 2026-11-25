MODULE Dev_devPtrInit;
(** Тесты обнуления локальных указателей при входе в процедуру.

	Язык требует: локальные переменные указательных и процедурных типов
	равны NIL перед телом процедуры; то же для OUT-параметров указательных
	типов, в том числе внутри записей и массивов. Обнуление локальных
	переменных нужно и сборщику мусора: он консервативно сканирует кадр.

	Перед каждой проверяемой процедурой стек пачкается ненулевым образцом:
	Dirty с кадром в 512 LONGINT перекрывает кадр вызываемой процедуры.
	Проверяются 1, 2, 3, 4, 5 и 8 указателей, указатель среди неуказательных
	переменных, запись с указателем в начале, середине и конце, массив
	записей, массив указателей, процедурная переменная, запись [align2] с
	невыровненным указателем, большой кадр (ветка size >= stackAllocLimit),
	больше MaxPtrs указателей (ветка InitPtrs2 с GenStrStore), вложенная
	процедура со статической связью, [exclusive]-процедура и OUT-параметры
	(указатель, запись с указателями, [nil]). Прямое чтение p = NIL
	дублируется чтением через VAR-параметр и IN-параметр-запись.

	Модуль компилируется с ключом точек уступки "^": пролог тот же, но
	проверяются и процедуры с точками уступки. *)

	IMPORT ACTIVE, S := SYSTEM, Kernel, UnitAssert;

	CONST
		dirtyLim = 512;	(* LONGINT: больше самого большого проверяемого кадра *)
		dirtyBase = 1234567890123456;

	TYPE
		Int = RECORD v: INTEGER END;
		PInt = POINTER TO Int;
		Bytes = POINTER TO ARRAY OF BYTE;
		PRec = POINTER TO RECORD x: LONGINT END;
		Proc = PROCEDURE;
		OnePtr = RECORD p: PInt; tail: INTEGER END;	(* указатель в начале *)
		MidPtr = RECORD a: INTEGER; p: PInt; b: INTEGER END;	(* в середине *)
		EndPtr = RECORD a, b: INTEGER; p: PInt END;	(* в конце *)
		Packed = RECORD [align2] tag: INTEGER; p: PInt END;	(* указатель на 4 (mod 8) *)
		Ptrs12 = RECORD a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11: PInt END;

	VAR
		g: PInt;	(* живой объект для проверки сборщика *)
		gKeep: PRec;

	(* ---------- пачкание стека ---------- *)

	PROCEDURE Dirty;
		VAR a: ARRAY dirtyLim OF LONGINT; i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(a) - 1 DO a[i] := dirtyBase + i END
	END Dirty;

	(* ---------- чтение через параметры ---------- *)

	PROCEDURE IsNilVar (VAR p: PInt): BOOLEAN;
	BEGIN RETURN p = NIL
	END IsNilVar;

	PROCEDURE IsNilIn (IN r: OnePtr): BOOLEAN;	(* IN разрешён только для записей *)
	BEGIN RETURN r.p = NIL
	END IsNilIn;

	(* ---------- число указателей ---------- *)

	PROCEDURE OneDirect (): BOOLEAN;
		VAR p: PInt;
	BEGIN RETURN p = NIL
	END OneDirect;

	PROCEDURE OneVar (): BOOLEAN;
		VAR p: PInt;
	BEGIN RETURN IsNilVar(p)
	END OneVar;

	PROCEDURE OneIn (): BOOLEAN;
		VAR r: OnePtr;
	BEGIN RETURN IsNilIn(r)
	END OneIn;

	PROCEDURE Two (): BOOLEAN;
		VAR a, b: PInt;
	BEGIN RETURN (a = NIL) & (b = NIL)
	END Two;

	PROCEDURE Three (): BOOLEAN;
		VAR a, b, c: PInt;
	BEGIN RETURN (a = NIL) & (b = NIL) & (c = NIL)
	END Three;

	PROCEDURE Four (): BOOLEAN;
		VAR a, b, c, d: PInt;
	BEGIN RETURN (a = NIL) & (b = NIL) & (c = NIL) & (d = NIL)
	END Four;

	PROCEDURE Five (): BOOLEAN;
		VAR a, b, c, d, e: PInt;
	BEGIN RETURN (a = NIL) & (b = NIL) & (c = NIL) & (d = NIL) & (e = NIL)
	END Five;

	PROCEDURE Eight (): BOOLEAN;
		VAR a, b, c, d, e, f, g, h: PInt;
	BEGIN
		RETURN (a = NIL) & (b = NIL) & (c = NIL) & (d = NIL)
			& (e = NIL) & (f = NIL) & (g = NIL) & (h = NIL)
	END Eight;

	(* ---------- расположение ---------- *)

	PROCEDURE Between (): BOOLEAN;
		VAR i: LONGINT; p: PInt; j: LONGINT;
	BEGIN i := 1; j := 2; RETURN p = NIL
	END Between;

	PROCEDURE RecBegin (): BOOLEAN;
		VAR r: OnePtr;
	BEGIN RETURN r.p = NIL
	END RecBegin;

	PROCEDURE RecMid (): BOOLEAN;
		VAR r: MidPtr;
	BEGIN RETURN r.p = NIL
	END RecMid;

	PROCEDURE RecEnd (): BOOLEAN;
		VAR r: EndPtr;
	BEGIN RETURN r.p = NIL
	END RecEnd;

	PROCEDURE Recs (): BOOLEAN;
		VAR a: ARRAY 3 OF OnePtr;
	BEGIN RETURN (a[0].p = NIL) & (a[1].p = NIL) & (a[2].p = NIL)
	END Recs;

	PROCEDURE Ptrs (): BOOLEAN;
		VAR a: ARRAY 5 OF PInt;
	BEGIN
		RETURN (a[0] = NIL) & (a[1] = NIL) & (a[2] = NIL) & (a[3] = NIL) & (a[4] = NIL)
	END Ptrs;

	PROCEDURE ProcVar (): BOOLEAN;
		VAR p: Proc;
	BEGIN RETURN p = NIL
	END ProcVar;

	PROCEDURE PackedRec (): BOOLEAN;
		VAR i: INTEGER; r: Packed;
	BEGIN i := 7; RETURN r.p = NIL
	END PackedRec;

	(* ---------- большие кадры и много указателей ---------- *)

	PROCEDURE BigFrame (): BOOLEAN;
		VAR big: ARRAY 300 OF LONGINT; p: PInt;
	BEGIN big[0] := 1; big[LEN(big) - 1] := 2; RETURN p = NIL
	END BigFrame;

	PROCEDURE ManyPtrs (): BOOLEAN;
		VAR a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11: PInt;
	BEGIN
		RETURN (a0 = NIL) & (a1 = NIL) & (a2 = NIL) & (a3 = NIL)
			& (a4 = NIL) & (a5 = NIL) & (a6 = NIL) & (a7 = NIL)
			& (a8 = NIL) & (a9 = NIL) & (a10 = NIL) & (a11 = NIL)
	END ManyPtrs;

	PROCEDURE RecMany (): BOOLEAN;
		VAR r: Ptrs12;
	BEGIN
		RETURN (r.a0 = NIL) & (r.a1 = NIL) & (r.a2 = NIL) & (r.a3 = NIL)
			& (r.a4 = NIL) & (r.a5 = NIL) & (r.a6 = NIL) & (r.a7 = NIL)
			& (r.a8 = NIL) & (r.a9 = NIL) & (r.a10 = NIL) & (r.a11 = NIL)
	END RecMany;

	(* ---------- вложенная процедура и [exclusive] ---------- *)

	PROCEDURE Outer (): BOOLEAN;
		VAR p: PInt;

		PROCEDURE Inner (): BOOLEAN;
			VAR q: PInt;
		BEGIN RETURN (p = NIL) & (q = NIL)	(* p — через статическую связь *)
		END Inner;

	BEGIN RETURN (p = NIL) & Inner()
	END Outer;

	PROCEDURE [exclusive] Excl (): BOOLEAN;
		VAR p: PInt;
	BEGIN RETURN p = NIL
	END Excl;

	(* ---------- OUT-параметры ----------
	   Тела пусты: обнулять должен пролог вызываемого (InitPtrs2), иначе
	   тест не отличит инициализацию от присваивания в теле. *)

	PROCEDURE OutPtr (OUT p: PInt);
	BEGIN
	END OutPtr;

	PROCEDURE OutRec (OUT r: OnePtr);
	BEGIN
	END OutRec;

	PROCEDURE OutMany (OUT r: Ptrs12);
	BEGIN
	END OutMany;

	PROCEDURE OutNil (OUT [nil] p: PInt);
	BEGIN
	END OutNil;

	(* ---------- сборщик мусора ---------- *)

	PROCEDURE GcLive (): BOOLEAN;
		VAR i: LONGINT; p: PInt;
	BEGIN
		i := 1;
		NEW(gKeep); gKeep.x := 42;
		Kernel.Collect;
		RETURN (p = NIL) & (gKeep.x = 42)
	END GcLive;

	(* ---------- тесты ---------- *)

	PROCEDURE SetupModule*;
	BEGIN
		NEW(g); g.v := 5
	END SetupModule;

	PROCEDURE TestOne*;
	BEGIN
		Dirty; UnitAssert.True(OneDirect(), "один указатель: прямое чтение");
		Dirty; UnitAssert.True(OneVar(), "один указатель: VAR-параметр");
		Dirty; UnitAssert.True(OneIn(), "один указатель: IN-параметр-запись")
	END TestOne;

	PROCEDURE TestTwoToEight*;
	BEGIN
		Dirty; UnitAssert.True(Two(), "два указателя");
		Dirty; UnitAssert.True(Three(), "три указателя");
		Dirty; UnitAssert.True(Four(), "четыре указателя");
		Dirty; UnitAssert.True(Five(), "пять указателей");
		Dirty; UnitAssert.True(Eight(), "восемь указателей")
	END TestTwoToEight;

	PROCEDURE TestLayout*;
	BEGIN
		Dirty; UnitAssert.True(Between(), "указатель между локальными");
		Dirty; UnitAssert.True(RecBegin(), "запись: указатель в начале");
		Dirty; UnitAssert.True(RecMid(), "запись: указатель в середине");
		Dirty; UnitAssert.True(RecEnd(), "запись: указатель в конце");
		Dirty; UnitAssert.True(Recs(), "массив записей с указателями");
		Dirty; UnitAssert.True(Ptrs(), "массив указателей");
		Dirty; UnitAssert.True(ProcVar(), "процедурная переменная");
		Dirty; UnitAssert.True(PackedRec(), "запись [align2]: указатель на 4 (mod 8)")
	END TestLayout;

	PROCEDURE TestBigAndMany*;
	BEGIN
		Dirty; UnitAssert.True(BigFrame(), "большой кадр >= stackAllocLimit");
		Dirty; UnitAssert.True(ManyPtrs(), "больше MaxPtrs указателей");
		Dirty; UnitAssert.True(RecMany(), "запись с 12 указателями")
	END TestBigAndMany;

	PROCEDURE TestNestedExclusive*;
	BEGIN
		Dirty; UnitAssert.True(Outer(), "вложенная процедура и статическая связь");
		Dirty; UnitAssert.True(Excl(), "[exclusive]-процедура")
	END TestNestedExclusive;

	PROCEDURE TestOut*;
		VAR p: PInt; r: OnePtr; m: Ptrs12;
	BEGIN
		p := g; OutPtr(p);
		UnitAssert.True(p = NIL, "OUT-указатель");
		r.p := g; r.tail := 7; OutRec(r);
		UnitAssert.True(r.p = NIL, "OUT-запись с указателем");
		m.a0 := g; m.a11 := g; OutMany(m);
		UnitAssert.True((m.a0 = NIL) & (m.a11 = NIL), "OUT-запись с 12 указателями");
		p := g; OutNil(p);
		UnitAssert.True(p = NIL, "OUT [nil]: настоящий адрес");
		OutNil(NIL);	(* NIL-адрес: обнулять нечего, разыменования нет *)
		UnitAssert.True(TRUE, "OUT [nil]: NIL-адрес не трапает")
	END TestOut;

	PROCEDURE TestProcGc*;
	BEGIN
		Dirty; UnitAssert.True(GcLive(), "сборка при живом грязном кадре")
	END TestProcGc;

END Dev_devPtrInit.
