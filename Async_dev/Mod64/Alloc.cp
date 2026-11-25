MODULE Async_devAlloc;
(** Замеры распределителя памяти ядра и обмена без NEW на сообщение.

	Часть 1. Цена Kernel.NewRec по размерам записи: без указателей
	(R16…R4096) и с указателем (Q16…Q4096), три режима — поток мусора,
	кольцо из 64 живых, 100 тыс. живых. Для каждого: нс на NEW, число
	выделений и сборок (Kernel.watcher, события 1 и 2), цена создания
	задачи с записью 248…288 байт. Ступень 248 → 280 воспроизводится без
	правки Tasks: тип задачи — наследник Tasks.Task с полем-заполнителем.

	Часть 2. Один обмен «производитель → буфер на 64 → потребитель»
	(1e6 сообщений) четырьмя способами: NEW на сообщение (запись 8 и 64
	байта), пул объектов, арена с индексом в сообщении (с проверкой
	поколения и без), канал значений на Tasks.Park/Unpark (копия записи
	16, 64, 256 байт).

	Строки ALLOC … — часть 1, XCHG … — часть 2. Числа зависят от машины
	и нагрузки; сравнивайте попеременные прогоны одного запуска.

	Модуль не входит в tools/tests.txt: полный прогон долгий. Тесты
	TestSizes/TestExchange проверяют раскладку типов и итог обмена. *)

	IMPORT S := SYSTEM, Kernel, Tasks, AsyncSelect, AsyncChan, Console, Strings,
		System_devClock, UnitAssert;

	CONST
		ringCap = 64;			(* живых в режиме «кольцо» *)
		liveN = 100000;			(* живых в режиме «много» *)
		streamN = 300000;		(* операций в режимах «поток» и «кольцо» *)
		liveRuns = 100000;		(* операций в режиме «много живых» *)
		msgN = 1000000;			(* сообщений в обмене *)
		padN = 100000;			(* задач в замере создания *)
		stack = 64 * 1024;
		reps = 5;			(* попеременных прогонов, медиана *)
		arenaN = 128;			(* слотов арены: буфер на 64 и запас *)
		valCap = 64;			(* ёмкость канала значений *)

		(* режимы цены NEW *)
		stream = 0; ring = 1; many = 2;

		(* виды обмена *)
		varN = 8;
		vMsg8 = 0; vMsg64 = 1; vPool = 2; vArena = 3; vArenaNo = 4;
		vVal16 = 5; vVal64 = 6; vVal256 = 7;

	TYPE
		Name = ARRAY 32 OF CHAR;
		PAny = POINTER TO RECORD v: INTEGER END;

		(* записи без указателей: v 8 байт + заполнитель *)
		R16 = RECORD v: LONGINT; pad: ARRAY 8 OF BYTE END;
		P16 = POINTER TO R16;
		R32 = RECORD v: LONGINT; pad: ARRAY 24 OF BYTE END;
		P32 = POINTER TO R32;
		R64 = RECORD v: LONGINT; pad: ARRAY 56 OF BYTE END;
		P64 = POINTER TO R64;
		R40 = RECORD v: LONGINT; pad: ARRAY 32 OF BYTE END;
		P40 = POINTER TO R40;
		R56 = RECORD v: LONGINT; pad: ARRAY 48 OF BYTE END;
		P56 = POINTER TO R56;
		R72 = RECORD v: LONGINT; pad: ARRAY 64 OF BYTE END;
		P72 = POINTER TO R72;
		R88 = RECORD v: LONGINT; pad: ARRAY 80 OF BYTE END;
		P88 = POINTER TO R88;
		R104 = RECORD v: LONGINT; pad: ARRAY 96 OF BYTE END;
		P104 = POINTER TO R104;
		R96 = RECORD v: LONGINT; pad: ARRAY 88 OF BYTE END;
		P96 = POINTER TO R96;
		R128 = RECORD v: LONGINT; pad: ARRAY 120 OF BYTE END;
		P128 = POINTER TO R128;
		R192 = RECORD v: LONGINT; pad: ARRAY 184 OF BYTE END;
		P192 = POINTER TO R192;
		R240 = RECORD v: LONGINT; pad: ARRAY 232 OF BYTE END;
		P240 = POINTER TO R240;
		R248 = RECORD v: LONGINT; pad: ARRAY 240 OF BYTE END;
		P248 = POINTER TO R248;
		R256 = RECORD v: LONGINT; pad: ARRAY 248 OF BYTE END;
		P256 = POINTER TO R256;
		R264 = RECORD v: LONGINT; pad: ARRAY 256 OF BYTE END;
		P264 = POINTER TO R264;
		R272 = RECORD v: LONGINT; pad: ARRAY 264 OF BYTE END;
		P272 = POINTER TO R272;
		R280 = RECORD v: LONGINT; pad: ARRAY 272 OF BYTE END;
		P280 = POINTER TO R280;
		R288 = RECORD v: LONGINT; pad: ARRAY 280 OF BYTE END;
		P288 = POINTER TO R288;
		R304 = RECORD v: LONGINT; pad: ARRAY 296 OF BYTE END;
		P304 = POINTER TO R304;
		R320 = RECORD v: LONGINT; pad: ARRAY 312 OF BYTE END;
		P320 = POINTER TO R320;
		R512 = RECORD v: LONGINT; pad: ARRAY 504 OF BYTE END;
		P512 = POINTER TO R512;
		R1024 = RECORD v: LONGINT; pad: ARRAY 1016 OF BYTE END;
		P1024 = POINTER TO R1024;
		R4096 = RECORD v: LONGINT; pad: ARRAY 4088 OF BYTE END;
		P4096 = POINTER TO R4096;

		(* те же размеры с одним указателем: его обходит Mark *)
		Q16 = RECORD p: PAny; pad: ARRAY 8 OF BYTE END;
		QP16 = POINTER TO Q16;
		Q32 = RECORD p: PAny; pad: ARRAY 24 OF BYTE END;
		QP32 = POINTER TO Q32;
		Q64 = RECORD p: PAny; pad: ARRAY 56 OF BYTE END;
		QP64 = POINTER TO Q64;
		Q96 = RECORD p: PAny; pad: ARRAY 88 OF BYTE END;
		QP96 = POINTER TO Q96;
		Q128 = RECORD p: PAny; pad: ARRAY 120 OF BYTE END;
		QP128 = POINTER TO Q128;
		Q192 = RECORD p: PAny; pad: ARRAY 184 OF BYTE END;
		QP192 = POINTER TO Q192;
		Q240 = RECORD p: PAny; pad: ARRAY 232 OF BYTE END;
		QP240 = POINTER TO Q240;
		Q248 = RECORD p: PAny; pad: ARRAY 240 OF BYTE END;
		QP248 = POINTER TO Q248;
		Q256 = RECORD p: PAny; pad: ARRAY 248 OF BYTE END;
		QP256 = POINTER TO Q256;
		Q264 = RECORD p: PAny; pad: ARRAY 256 OF BYTE END;
		QP264 = POINTER TO Q264;
		Q280 = RECORD p: PAny; pad: ARRAY 272 OF BYTE END;
		QP280 = POINTER TO Q280;
		Q320 = RECORD p: PAny; pad: ARRAY 312 OF BYTE END;
		QP320 = POINTER TO Q320;
		Q512 = RECORD p: PAny; pad: ARRAY 504 OF BYTE END;
		QP512 = POINTER TO Q512;
		Q1024 = RECORD p: PAny; pad: ARRAY 1016 OF BYTE END;
		QP1024 = POINTER TO Q1024;
		Q4096 = RECORD p: PAny; pad: ARRAY 4088 OF BYTE END;
		QP4096 = POINTER TO Q4096;

		(* задачи с записью 248 + заполнитель: воспроизведение ступени *)
		TT248 = RECORD (Tasks.Task) END;
		PT248 = POINTER TO TT248;
		TT256 = RECORD (Tasks.Task) pad: ARRAY 8 OF BYTE END;
		PT256 = POINTER TO TT256;
		TT264 = RECORD (Tasks.Task) pad: ARRAY 16 OF BYTE END;
		PT264 = POINTER TO TT264;
		TT272 = RECORD (Tasks.Task) pad: ARRAY 24 OF BYTE END;
		PT272 = POINTER TO TT272;
		TT280 = RECORD (Tasks.Task) pad: ARRAY 32 OF BYTE END;
		PT280 = POINTER TO TT280;
		TT288 = RECORD (Tasks.Task) pad: ARRAY 40 OF BYTE END;
		PT288 = POINTER TO TT288;

		(* сообщение 8 байт — как M в Async_devBench *)
		M8 = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		(* сообщение 64 байта: next — для пула *)
		M64 = POINTER TO RECORD (AsyncSelect.Msg) next: M64; n: INTEGER;
			pad: ARRAY 52 OF BYTE END;

		(* индекс в канале и запись арены *)
		Env = POINTER TO RECORD (AsyncSelect.Msg) idx, gen: INTEGER END;
		Data = RECORD n: INTEGER; pad: ARRAY 56 OF BYTE END;

		(* запись 248 байт для оценки пула записей задач *)
		PoolR = POINTER TO RECORD next: PoolR; pad: ARRAY 240 OF BYTE END;

		(* запись канала значений: 4, 16 и 64 INTEGER *)
		Val16 = RECORD b: ARRAY 4 OF INTEGER END;
		Val64 = RECORD b: ARRAY 16 OF INTEGER END;
		Val256 = RECORD b: ARRAY 64 OF INTEGER END;

		VB16 = POINTER TO RECORD
			head, count: INTEGER;
			prod, cons: Tasks.Task;
			ring: ARRAY valCap OF Val16
		END;
		VB64 = POINTER TO RECORD
			head, count: INTEGER;
			prod, cons: Tasks.Task;
			ring: ARRAY valCap OF Val64
		END;
		VB256 = POINTER TO RECORD
			head, count: INTEGER;
			prod, cons: Tasks.Task;
			ring: ARRAY valCap OF Val256
		END;

		(* задачи обмена *)
		P0 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		C0 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		P1 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		C1 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		PPool = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		CPool = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		P2 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n, check: INTEGER END;
		C2 = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n, check: INTEGER END;

		VP16 = POINTER TO RECORD (Tasks.Task) b: VB16; n: INTEGER END;
		VC16 = POINTER TO RECORD (Tasks.Task) b: VB16; n: INTEGER END;
		VP64 = POINTER TO RECORD (Tasks.Task) b: VB64; n: INTEGER END;
		VC64 = POINTER TO RECORD (Tasks.Task) b: VB64; n: INTEGER END;
		VP256 = POINTER TO RECORD (Tasks.Task) b: VB256; n: INTEGER END;
		VC256 = POINTER TO RECORD (Tasks.Task) b: VB256; n: INTEGER END;

	VAR
		keep: ARRAY liveN OF PAny;	(* корень режима «много живых» *)
		keepT: ARRAY liveN OF Tasks.Task;	(* корень записей задач *)
		keepD: ARRAY liveN OF PAny;	(* корень записей-описателей *)
		sum: LONGINT;			(* контрольная сумма обмена *)
		badGen: LONGINT;		(* обращения к чужому поколению арены *)
		collects: INTEGER;		(* сборки: события 1 и 2 Kernel.watcher *)
		countOn: BOOLEAN;
		oldDebug: BOOLEAN;
		oldWatch: PROCEDURE (e: INTEGER);

		pool: M64;			(* свободные сообщения пула *)
		poolR: PoolR;		(* свободные записи задач для оценки пула *)
		arena: ARRAY arenaN OF Data;
		envs: ARRAY arenaN OF Env;
		slots: ARRAY arenaN OF INTEGER;
		slotN: INTEGER;
		gen: ARRAY arenaN OF INTEGER;

		t: LONGINT;			(* текущее время прогона *)
		a0: LONGINT;
		times: ARRAY varN, reps OF LONGINT;
		allocs: ARRAY varN, reps OF LONGINT;
		cols: ARRAY varN OF LONGINT;

	(* --- вывод ------------------------------------------------------------ *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	(* --- часы и сборки ---------------------------------------------------- *)

	PROCEDURE Now (): LONGINT;
	BEGIN
		RETURN System_devClock.now()
	END Now;

	PROCEDURE Watch (e: INTEGER);
	BEGIN
		IF countOn & ((e = 1) OR (e = 2)) THEN INC(collects) END
	END Watch;

	PROCEDURE SetCounting (on: BOOLEAN);
	(* Считать сборки. debug включает watcher в Kernel; на время счёта
	   цена NEW искажена отладочными проверками — время берём отдельно. *)
	BEGIN
		IF on THEN
			oldDebug := Kernel.debug; oldWatch := Kernel.watcher;
			Kernel.watcher := Watch; Kernel.debug := TRUE; countOn := TRUE
		ELSE
			countOn := FALSE;
			Kernel.watcher := oldWatch;
			Kernel.debug := oldDebug
		END
	END SetCounting;

	PROCEDURE ClearKeep;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO liveN - 1 DO keep[i] := NIL END
	END ClearKeep;

	PROCEDURE Med (VAR a: ARRAY OF LONGINT; n: INTEGER): LONGINT;
		VAR i, j: INTEGER; x: LONGINT; b: ARRAY 16 OF LONGINT;
	BEGIN
		FOR i := 0 TO n - 1 DO b[i] := a[i] END;
		FOR i := 1 TO n - 1 DO
			x := b[i]; j := i - 1;
			WHILE (j >= 0) & (b[j] > x) DO b[j + 1] := b[j]; DEC(j) END;
			b[j + 1] := x
		END;
		RETURN b[n DIV 2]
	END Med;

	(* --- часть 1: цена NEW по размерам ------------------------------------ *)

	PROCEDURE AllocStream (typ: S.ADDRESS; n: INTEGER): LONGINT;
	(* Выделил и забыл: живых почти нет. *)
		VAR i: INTEGER; a: S.ADDRESS; t0: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 1 TO n DO a := Kernel.NewRec(typ) END;
		a := 0;
		RETURN Now() - t0
	END AllocStream;

	PROCEDURE AllocRing (typ: S.ADDRESS; n: INTEGER): LONGINT;
	(* Кольцо из ringCap живых: старые становятся мусором. *)
		VAR i: INTEGER; ring: ARRAY ringCap OF S.ADDRESS; t0: LONGINT;
	BEGIN
		FOR i := 0 TO ringCap - 1 DO ring[i] := 0 END;
		t0 := Now();
		FOR i := 1 TO n DO ring[i MOD ringCap] := Kernel.NewRec(typ) END;
		RETURN Now() - t0
	END AllocRing;

	PROCEDURE AllocMany (typ: S.ADDRESS; n: INTEGER): LONGINT;
	(* Все выделенные живы: ссылки в глобальном массиве keep. *)
		VAR i: INTEGER; t0: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 0 TO n - 1 DO keep[i] := S.VAL(PAny, Kernel.NewRec(typ)) END;
		RETURN Now() - t0
	END AllocMany;

	PROCEDURE DoAlloc (typ: S.ADDRESS; n, mode: INTEGER): LONGINT;
	BEGIN
		CASE mode OF
		| stream: RETURN AllocStream(typ, n)
		| ring: RETURN AllocRing(typ, n)
		ELSE RETURN AllocMany(typ, n)
		END
	END DoAlloc;

	PROCEDURE Measure (IN name: ARRAY OF CHAR; typ: S.ADDRESS; size, mode: INTEGER);
		VAR n: INTEGER; ns, ns2, churn, c0, cl, gcNs, g0: LONGINT;
	BEGIN
		IF mode = many THEN n := liveRuns ELSE n := streamN END;
		ClearKeep; Kernel.Collect;
		SetCounting(FALSE);
		a0 := Kernel.AllocCount();
		ns := DoAlloc(typ, n, mode);
		churn := Kernel.AllocCount() - a0;
		gcNs := 0;
		IF mode = many THEN g0 := Now(); Kernel.Collect; gcNs := Now() - g0 END;
		ClearKeep; Kernel.Collect;
		(* счётный прогон: то же число выделений и сборок; время его не берём *)
		SetCounting(TRUE);
		c0 := collects;
		ns2 := DoAlloc(typ, n, mode);
		cl := collects - c0;
		SetCounting(FALSE);
		ClearKeep; Kernel.Collect;
		W("ALLOC new name="); W(name); W(" size="); N(size);
		W(" tsize="); N((size + 23) DIV 16 * 16);
		W(" class="); N(MIN(7, ((size + 23) DIV 16 * 16 - 8) DIV 16));
		W(" mode="); N(mode);
		W(" ns="); N(ns DIV n);
		W(" allocs="); N(churn);
		W(" collects="); N(cl);
		W(" collectNs="); N(gcNs); Console.WriteLn
	END Measure;

	PROCEDURE SizeTable*;
	(* Печатает SIZE всех типов: проверка, что запись ровно нужной длины. *)
		VAR i, size: INTEGER;
	BEGIN
		W("SIZE R16="); N(SIZE(R16)); W(" R32="); N(SIZE(R32));
		W(" R64="); N(SIZE(R64)); W(" R96="); N(SIZE(R96));
		W(" R128="); N(SIZE(R128)); W(" R192="); N(SIZE(R192));
		W(" R240="); N(SIZE(R240)); W(" R248="); N(SIZE(R248));
		W(" R256="); N(SIZE(R256)); W(" R264="); N(SIZE(R264));
		W(" R280="); N(SIZE(R280)); W(" R320="); N(SIZE(R320));
		W(" R512="); N(SIZE(R512)); W(" R1024="); N(SIZE(R1024));
		W(" R4096="); N(SIZE(R4096)); Console.WriteLn;
		W("SIZE Q16="); N(SIZE(Q16)); W(" Q280="); N(SIZE(Q280));
		W(" Q4096="); N(SIZE(Q4096)); Console.WriteLn;
		W("SIZE TT248="); N(SIZE(TT248)); W(" TT256="); N(SIZE(TT256));
		W(" TT264="); N(SIZE(TT264)); W(" TT272="); N(SIZE(TT272));
		W(" TT280="); N(SIZE(TT280)); W(" TT288="); N(SIZE(TT288));
		Console.WriteLn;
		i := SIZE(M8); W("SIZE M8="); N(i);
		i := SIZE(Data); W(" Data="); N(i);
		W(" Val16="); N(SIZE(Val16)); W(" Val64="); N(SIZE(Val64));
		W(" Val256="); N(SIZE(Val256)); Console.WriteLn
	END SizeTable;

	PROCEDURE RawOne (IN name: ARRAY OF CHAR; typ: S.ADDRESS; size: INTEGER);
		VAR i: INTEGER; a: S.ADDRESS; t0: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 1 TO streamN DO a := Kernel.NewRec(typ) END;
		a := 0;
		W("ALLOC raw name="); W(name); W(" size="); N(size);
		W(" tsize="); N((size + 23) DIV 16 * 16);
		W(" ns="); N((Now() - t0) DIV streamN); Console.WriteLn
	END RawOne;

	PROCEDURE RawStep*;
	(** Цена NewRec без сборки вокруг ступени tsize = 288. *)
	BEGIN
		Kernel.Collect; Kernel.blockCollect := TRUE;
		RawOne("R248", S.TYP(P248), SIZE(R248));
		RawOne("R264", S.TYP(P264), SIZE(R264));
		RawOne("R272", S.TYP(P272), SIZE(R272));
		RawOne("R280", S.TYP(P280), SIZE(R280));
		RawOne("R288", S.TYP(P288), SIZE(R288));
		RawOne("R304", S.TYP(P304), SIZE(R304));
		RawOne("R320", S.TYP(P320), SIZE(R320));
		Kernel.blockCollect := FALSE; Kernel.Collect
	END RawStep;

	PROCEDURE AllSizes*;
	(** Полная таблица: 15 размеров, две семьи записей, три режима. *)
	BEGIN
		Measure("R16", S.TYP(P16), SIZE(R16), stream);
		Measure("R16", S.TYP(P16), SIZE(R16), ring);
		Measure("R16", S.TYP(P16), SIZE(R16), many);
		Measure("Q16", S.TYP(QP16), SIZE(Q16), stream);
		Measure("Q16", S.TYP(QP16), SIZE(Q16), ring);
		Measure("Q16", S.TYP(QP16), SIZE(Q16), many);
		Measure("R32", S.TYP(P32), SIZE(R32), stream);
		Measure("R32", S.TYP(P32), SIZE(R32), ring);
		Measure("R32", S.TYP(P32), SIZE(R32), many);
		Measure("Q32", S.TYP(QP32), SIZE(Q32), stream);
		Measure("Q32", S.TYP(QP32), SIZE(Q32), ring);
		Measure("Q32", S.TYP(QP32), SIZE(Q32), many);
		Measure("R64", S.TYP(P64), SIZE(R64), stream);
		Measure("R64", S.TYP(P64), SIZE(R64), ring);
		Measure("R64", S.TYP(P64), SIZE(R64), many);
		Measure("Q64", S.TYP(QP64), SIZE(Q64), stream);
		Measure("Q64", S.TYP(QP64), SIZE(Q64), ring);
		Measure("Q64", S.TYP(QP64), SIZE(Q64), many);
		Measure("R96", S.TYP(P96), SIZE(R96), stream);
		Measure("R96", S.TYP(P96), SIZE(R96), ring);
		Measure("R96", S.TYP(P96), SIZE(R96), many);
		Measure("Q96", S.TYP(QP96), SIZE(Q96), stream);
		Measure("Q96", S.TYP(QP96), SIZE(Q96), ring);
		Measure("Q96", S.TYP(QP96), SIZE(Q96), many);
		Measure("R128", S.TYP(P128), SIZE(R128), stream);
		Measure("R128", S.TYP(P128), SIZE(R128), ring);
		Measure("R128", S.TYP(P128), SIZE(R128), many);
		Measure("Q128", S.TYP(QP128), SIZE(Q128), stream);
		Measure("Q128", S.TYP(QP128), SIZE(Q128), ring);
		Measure("Q128", S.TYP(QP128), SIZE(Q128), many);
		Measure("R192", S.TYP(P192), SIZE(R192), stream);
		Measure("R192", S.TYP(P192), SIZE(R192), ring);
		Measure("R192", S.TYP(P192), SIZE(R192), many);
		Measure("Q192", S.TYP(QP192), SIZE(Q192), stream);
		Measure("Q192", S.TYP(QP192), SIZE(Q192), ring);
		Measure("Q192", S.TYP(QP192), SIZE(Q192), many);
		Measure("R240", S.TYP(P240), SIZE(R240), stream);
		Measure("R240", S.TYP(P240), SIZE(R240), ring);
		Measure("R240", S.TYP(P240), SIZE(R240), many);
		Measure("Q240", S.TYP(QP240), SIZE(Q240), stream);
		Measure("Q240", S.TYP(QP240), SIZE(Q240), ring);
		Measure("Q240", S.TYP(QP240), SIZE(Q240), many);
		Measure("R248", S.TYP(P248), SIZE(R248), stream);
		Measure("R248", S.TYP(P248), SIZE(R248), ring);
		Measure("R248", S.TYP(P248), SIZE(R248), many);
		Measure("Q248", S.TYP(QP248), SIZE(Q248), stream);
		Measure("Q248", S.TYP(QP248), SIZE(Q248), ring);
		Measure("Q248", S.TYP(QP248), SIZE(Q248), many);
		Measure("R256", S.TYP(P256), SIZE(R256), stream);
		Measure("R256", S.TYP(P256), SIZE(R256), ring);
		Measure("R256", S.TYP(P256), SIZE(R256), many);
		Measure("Q256", S.TYP(QP256), SIZE(Q256), stream);
		Measure("Q256", S.TYP(QP256), SIZE(Q256), ring);
		Measure("Q256", S.TYP(QP256), SIZE(Q256), many);
		Measure("R264", S.TYP(P264), SIZE(R264), stream);
		Measure("R264", S.TYP(P264), SIZE(R264), ring);
		Measure("R264", S.TYP(P264), SIZE(R264), many);
		Measure("Q264", S.TYP(QP264), SIZE(Q264), stream);
		Measure("Q264", S.TYP(QP264), SIZE(Q264), ring);
		Measure("Q264", S.TYP(QP264), SIZE(Q264), many);
		Measure("R280", S.TYP(P280), SIZE(R280), stream);
		Measure("R280", S.TYP(P280), SIZE(R280), ring);
		Measure("R280", S.TYP(P280), SIZE(R280), many);
		Measure("Q280", S.TYP(QP280), SIZE(Q280), stream);
		Measure("Q280", S.TYP(QP280), SIZE(Q280), ring);
		Measure("Q280", S.TYP(QP280), SIZE(Q280), many);
		Measure("R320", S.TYP(P320), SIZE(R320), stream);
		Measure("R320", S.TYP(P320), SIZE(R320), ring);
		Measure("R320", S.TYP(P320), SIZE(R320), many);
		Measure("Q320", S.TYP(QP320), SIZE(Q320), stream);
		Measure("Q320", S.TYP(QP320), SIZE(Q320), ring);
		Measure("Q320", S.TYP(QP320), SIZE(Q320), many);
		Measure("R512", S.TYP(P512), SIZE(R512), stream);
		Measure("R512", S.TYP(P512), SIZE(R512), ring);
		Measure("R512", S.TYP(P512), SIZE(R512), many);
		Measure("Q512", S.TYP(QP512), SIZE(Q512), stream);
		Measure("Q512", S.TYP(QP512), SIZE(Q512), ring);
		Measure("Q512", S.TYP(QP512), SIZE(Q512), many);
		Measure("R1024", S.TYP(P1024), SIZE(R1024), stream);
		Measure("R1024", S.TYP(P1024), SIZE(R1024), ring);
		Measure("R1024", S.TYP(P1024), SIZE(R1024), many);
		Measure("Q1024", S.TYP(QP1024), SIZE(Q1024), stream);
		Measure("Q1024", S.TYP(QP1024), SIZE(Q1024), ring);
		Measure("Q1024", S.TYP(QP1024), SIZE(Q1024), many);
		Measure("R4096", S.TYP(P4096), SIZE(R4096), stream);
		Measure("R4096", S.TYP(P4096), SIZE(R4096), ring);
		Measure("R4096", S.TYP(P4096), SIZE(R4096), many);
		Measure("Q4096", S.TYP(QP4096), SIZE(Q4096), stream);
		Measure("Q4096", S.TYP(QP4096), SIZE(Q4096), ring);
		Measure("Q4096", S.TYP(QP4096), SIZE(Q4096), many)
	END AllSizes;

	(* --- часть 1: создание задачи, запись 248…288 байт --------------------- *)

	PROCEDURE (t: PT248) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE (t: PT256) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE (t: PT264) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE (t: PT272) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE (t: PT280) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE (t: PT288) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE Spawn248 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT248; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn248;

	PROCEDURE Spawn256 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT256; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn256;

	PROCEDURE Spawn264 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT264; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn264;

	PROCEDURE Spawn272 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT272; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn272;

	PROCEDURE Spawn280 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT280; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn280;

	PROCEDURE Spawn288 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT288; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN Now() - t0
	END Spawn288;

	PROCEDURE SpawnRun (pad: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR ns: LONGINT;
	BEGIN
		Kernel.Collect;
		CASE pad OF
		| 248: ns := Spawn248(padN, nogc)
		| 256: ns := Spawn256(padN, nogc)
		| 264: ns := Spawn264(padN, nogc)
		| 272: ns := Spawn272(padN, nogc)
		| 280: ns := Spawn280(padN, nogc)
		ELSE ns := Spawn288(padN, nogc)
		END;
		Kernel.Collect;
		RETURN ns DIV padN
	END SpawnRun;

	PROCEDURE SpawnPads*;
	(** Цена создания задачи при размере записи 248…288, сборка вкл/выкл. *)
		VAR pad, rep, k, mode: INTEGER;
			v: ARRAY 6, reps OF LONGINT; nogc: BOOLEAN;
	BEGIN
		(* прогреть пул стеков *)
		sum := 0;
		t := Spawn248(2000, TRUE); Kernel.Collect;
		t := Spawn248(2000, TRUE); Kernel.Collect;
		FOR mode := 0 TO 1 DO
			nogc := mode = 1;
			FOR rep := 0 TO reps - 1 DO
				FOR k := 0 TO 5 DO
					v[k, rep] := SpawnRun(248 + 8 * k, nogc)
				END
			END;
			FOR k := 0 TO 5 DO
				W("ALLOC spawn pad="); N(248 + 8 * k);
				W(" nogc="); N(mode);
				W(" nsPerTask="); N(Med(v[k], reps));
				Console.WriteLn
			END
		END
	END SpawnPads;

	PROCEDURE NewTask264 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT264; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 0 TO n - 1 DO NEW(x); keepT[i] := x END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN (Now() - t0) DIV n
	END NewTask264;

	PROCEDURE NewTask280 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT280; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 0 TO n - 1 DO NEW(x); keepT[i] := x END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN (Now() - t0) DIV n
	END NewTask280;

	PROCEDURE ClearTasks;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO liveN - 1 DO keepT[i] := NIL END
	END ClearTasks;

	PROCEDURE ClearPair;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO liveN - 1 DO keepT[i] := NIL; keepD[i] := NIL END
	END ClearPair;

	PROCEDURE PairOne (typA, typB: S.ADDRESS; n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; a, b: S.ADDRESS; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 0 TO n - 1 DO
			a := Kernel.NewRec(typA); keepT[i] := S.VAL(Tasks.Task, a);
			b := Kernel.NewRec(typB); keepD[i] := S.VAL(PAny, b)
		END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN (Now() - t0) DIV n
	END PairOne;

	PROCEDURE PairRow (IN name: ARRAY OF CHAR; typA: S.ADDRESS; typB: S.ADDRESS; nogc: BOOLEAN);
		VAR ns: LONGINT;
	BEGIN
		ClearPair; Kernel.Collect;
		ns := PairOne(typA, typB, liveN, nogc); ClearPair; Kernel.Collect;
		W("ALLOC pair "); W(name); W(" nogc=");
		IF nogc THEN W("1") ELSE W("0") END;
		W(" ns="); N(ns); Console.WriteLn
	END PairRow;

	PROCEDURE Pair264One*;
	(** Один прогон пар 264+72 байта для tools/prof.sh. *)
	BEGIN
		ClearPair; Kernel.Collect;
		W("ALLOC pair264 ns="); N(PairOne(S.TYP(P264), S.TYP(P72), liveN, TRUE)); Console.WriteLn;
		ClearPair; Kernel.Collect
	END Pair264One;

	PROCEDURE Pair280One*;
	(** Один прогон пар 280+72 байта для tools/prof.sh. *)
	BEGIN
		ClearPair; Kernel.Collect;
		W("ALLOC pair280 ns="); N(PairOne(S.TYP(P280), S.TYP(P72), liveN, TRUE)); Console.WriteLn;
		ClearPair; Kernel.Collect
	END Pair280One;

	PROCEDURE PairMatrix*;
	(** Пары «запись задачи + описатель»: где ступень. *)
		VAR k: INTEGER;
	BEGIN
		PairRow("A248 B72", S.TYP(P248), S.TYP(P72), TRUE);
		PairRow("A264 B72", S.TYP(P264), S.TYP(P72), TRUE);
		PairRow("A272 B72", S.TYP(P272), S.TYP(P72), TRUE);
		PairRow("A280 B72", S.TYP(P280), S.TYP(P72), TRUE);
		PairRow("A288 B72", S.TYP(P288), S.TYP(P72), TRUE);
		PairRow("A304 B72", S.TYP(P304), S.TYP(P72), TRUE);
		PairRow("A280 B40", S.TYP(P280), S.TYP(P40), TRUE);
		PairRow("A280 B56", S.TYP(P280), S.TYP(P56), TRUE);
		PairRow("A280 B72", S.TYP(P280), S.TYP(P72), TRUE);
		PairRow("A280 B88", S.TYP(P280), S.TYP(P88), TRUE);
		PairRow("A280 B104", S.TYP(P280), S.TYP(P104), TRUE)
	END PairMatrix;

	PROCEDURE Pair264 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT264; d: S.ADDRESS; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 0 TO n - 1 DO
			NEW(x); keepT[i] := x;
			d := Kernel.NewRec(S.TYP(P72)); keepD[i] := S.VAL(PAny, d)
		END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN (Now() - t0) DIV n
	END Pair264;

	PROCEDURE Pair280 (n: INTEGER; nogc: BOOLEAN): LONGINT;
		VAR i: INTEGER; x: PT280; d: S.ADDRESS; t0: LONGINT;
	BEGIN
		IF nogc THEN Kernel.blockCollect := TRUE END;
		t0 := Now();
		FOR i := 0 TO n - 1 DO
			NEW(x); keepT[i] := x;
			d := Kernel.NewRec(S.TYP(P72)); keepD[i] := S.VAL(PAny, d)
		END;
		IF nogc THEN Kernel.blockCollect := FALSE END;
		RETURN (Now() - t0) DIV n
	END Pair280;

	PROCEDURE AllocPair*;
	(** Две записи на задачу: 264/280 и описатель 72 байта. *)
		VAR i: INTEGER; a, b: LONGINT;
	BEGIN
		FOR i := 1 TO 3 DO
			ClearPair; Kernel.Collect;
			a := Pair264(liveN, FALSE); ClearPair; Kernel.Collect;
			b := Pair280(liveN, FALSE); ClearPair; Kernel.Collect;
			W("ALLOC pair gc=1 pad264="); N(a); W(" pad280="); N(b); Console.WriteLn
		END;
		FOR i := 1 TO 2 DO
			ClearPair; Kernel.Collect;
			a := Pair264(liveN, TRUE); ClearPair; Kernel.Collect;
			b := Pair280(liveN, TRUE); ClearPair; Kernel.Collect;
			W("ALLOC pair gc=0 pad264="); N(a); W(" pad280="); N(b); Console.WriteLn
		END
	END AllocPair;

	PROCEDURE Start264 (n: INTEGER): LONGINT;
		VAR i: INTEGER; x: PT264; t0: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 0 TO n - 1 DO
			x := S.VAL(PT264, keepT[i]); Tasks.SetStackSize(x, stack); Tasks.Start(x)
		END;
		RETURN (Now() - t0) DIV n
	END Start264;

	PROCEDURE Start280 (n: INTEGER): LONGINT;
		VAR i: INTEGER; x: PT280; t0: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 0 TO n - 1 DO
			x := S.VAL(PT280, keepT[i]); Tasks.SetStackSize(x, stack); Tasks.Start(x)
		END;
		RETURN (Now() - t0) DIV n
	END Start280;

	PROCEDURE Fill264 (n: INTEGER);
		VAR i: INTEGER; x: PT264;
	BEGIN FOR i := 0 TO n - 1 DO NEW(x); keepT[i] := x END END Fill264;

	PROCEDURE Fill280 (n: INTEGER);
		VAR i: INTEGER; x: PT280;
	BEGIN FOR i := 0 TO n - 1 DO NEW(x); keepT[i] := x END END Fill280;

	PROCEDURE StartOnly*;
	(** Только Tasks.Start для заранее выделенных записей 264 и 280 байт. *)
		VAR i: INTEGER; a, b: LONGINT;
	BEGIN
		FOR i := 1 TO 3 DO
			ClearTasks; Kernel.Collect; Fill264(liveN);
			a := Start264(liveN); WHILE Tasks.Count() > 0 DO Tasks.Step END;
			ClearTasks; Kernel.Collect; Fill280(liveN);
			b := Start280(liveN); WHILE Tasks.Count() > 0 DO Tasks.Step END;
			Kernel.Collect;
			W("ALLOC start pad264="); N(a); W(" pad280="); N(b); Console.WriteLn
		END
	END StartOnly;

	PROCEDURE NewTasks*;
	(** Только NEW записи задачи 264 и 280 байт, без Tasks.Start. *)
		VAR i: INTEGER; a, b: LONGINT;
	BEGIN
		FOR i := 1 TO 4 DO
			ClearTasks; Kernel.Collect;
			a := NewTask264(liveN, FALSE); ClearTasks; Kernel.Collect;
			b := NewTask280(liveN, FALSE); ClearTasks; Kernel.Collect;
			W("ALLOC newtask gc=1 pad264="); N(a); W(" pad280="); N(b); Console.WriteLn
		END;
		FOR i := 1 TO 2 DO
			ClearTasks; Kernel.Collect;
			a := NewTask264(liveN, TRUE); ClearTasks; Kernel.Collect;
			b := NewTask280(liveN, TRUE); ClearTasks; Kernel.Collect;
			W("ALLOC newtask gc=0 pad264="); N(a); W(" pad280="); N(b); Console.WriteLn
		END
	END NewTasks;

	PROCEDURE Phase264 (n: INTEGER; OUT setup, exec: LONGINT);
		VAR i: INTEGER; x: PT264; t0, t1: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		t1 := Now();
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		setup := (t1 - t0) DIV n; exec := (Now() - t1) DIV n
	END Phase264;

	PROCEDURE Phase280 (n: INTEGER; OUT setup, exec: LONGINT);
		VAR i: INTEGER; x: PT280; t0, t1: LONGINT;
	BEGIN
		t0 := Now();
		FOR i := 1 TO n DO NEW(x); Tasks.SetStackSize(x, stack); Tasks.Start(x) END;
		t1 := Now();
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		setup := (t1 - t0) DIV n; exec := (Now() - t1) DIV n
	END Phase280;

	PROCEDURE SpawnPhases*;
	(** Разложить ступень: создание (NEW+Start) и исполнение (Step) отдельно. *)
		VAR i: INTEGER; s2, e2, s8, e8: LONGINT;
	BEGIN
		sum := 0; t := Spawn248(padN, TRUE); Kernel.Collect;
		FOR i := 1 TO 4 DO
			Kernel.Collect; Phase264(padN, s2, e2); Kernel.Collect;
			Phase280(padN, s8, e8);
			W("ALLOC phase pad=264 setup="); N(s2); W(" exec="); N(e2);
			W(" pad=280 setup="); N(s8); W(" exec="); N(e8);
			Console.WriteLn
		END
	END SpawnPhases;

	PROCEDURE SpawnScale*;
	(** Ступень против размера кучи: 25, 50, 100 и 200 тыс. задач. *)
		VAR k, n, i: INTEGER; a, b: LONGINT;
	BEGIN
		sum := 0; t := Spawn248(2000, TRUE); Kernel.Collect;
		n := 25000;
		FOR i := 1 TO 4 DO
			Kernel.Collect; a := Spawn264(n, FALSE) DIV n;
			Kernel.Collect; b := Spawn280(n, FALSE) DIV n;
			Kernel.Collect;
			W("ALLOC scale tasks="); N(n); W(" pad264="); N(a);
			W(" pad280="); N(b); Console.WriteLn;
			n := 2 * n
		END
	END SpawnScale;

	PROCEDURE SpawnAlt*;
	(** 264 и 280 попеременно: проверка, не зависит ли ступень от порядка. *)
		VAR i, k: INTEGER; ns: LONGINT;
	BEGIN
		sum := 0; t := Spawn248(2000, TRUE); Kernel.Collect;
		t := Spawn248(padN, FALSE); Kernel.Collect;	(* прогреть пул стеков *)
		FOR i := 1 TO 6 DO
			FOR k := 0 TO 1 DO
				CASE k OF
				| 0: ns := Spawn264(padN, FALSE)
				ELSE ns := Spawn280(padN, FALSE)
				END;
				W("ALLOC alt pad="); N(264 + 16 * k);
				W(" run="); N(i); W(" nsPerTask="); N(ns DIV padN);
				Console.WriteLn;
				Kernel.Collect
			END
		END
	END SpawnAlt;

	PROCEDURE Spawn248One*;
	(** Один прогон 248 байт для tools/prof.sh. *)
	BEGIN
		sum := 0; t := Spawn248(padN, TRUE); Kernel.Collect;	(* пул стеков *)
		W("ALLOC spawn248 nsPerTask="); N(SpawnRun(248, FALSE)); Console.WriteLn
	END Spawn248One;

	PROCEDURE Spawn280One*;
	(** Один прогон 280 байт для tools/prof.sh. *)
	BEGIN
		sum := 0; t := Spawn248(padN, TRUE); Kernel.Collect;	(* пул стеков *)
		W("ALLOC spawn280 nsPerTask="); N(SpawnRun(280, FALSE)); Console.WriteLn
	END Spawn280One;

	(* --- часть 2: сообщения ----------------------------------------------- *)

	PROCEDURE Take (): M64;
		VAR m: M64;
	BEGIN
		m := pool;
		IF m # NIL THEN pool := m.next; m.next := NIL ELSE NEW(m) END;
		RETURN m
	END Take;

	PROCEDURE Give (m: M64);
	BEGIN
		m.next := pool; pool := m
	END Give;

	PROCEDURE ResetArena;
		VAR i: INTEGER;
	BEGIN
		slotN := arenaN;
		FOR i := 0 TO arenaN - 1 DO
			slots[i] := i; gen[i] := 0;
			NEW(envs[i]); envs[i].idx := i; envs[i].gen := 0
		END;
		badGen := 0
	END ResetArena;

	(* производитель и потребитель: NEW на сообщение *)

	PROCEDURE (t: P0) Run;
		VAR i: INTEGER; m: M8;
	BEGIN
		FOR i := 1 TO t.n DO NEW(m); m.n := i; t.c.Send(m) END
	END Run;

	PROCEDURE (t: C0) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); INC(sum, x(M8).n MOD 2) END
	END Run;

	PROCEDURE (t: P1) Run;
		VAR i: INTEGER; m: M64;
	BEGIN
		FOR i := 1 TO t.n DO NEW(m); m.n := i; t.c.Send(m) END
	END Run;

	PROCEDURE (t: C1) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); INC(sum, x(M64).n MOD 2) END
	END Run;

	(* пул: отправитель берёт, получатель возвращает *)

	PROCEDURE (t: PPool) Run;
		VAR i: INTEGER; m: M64;
	BEGIN
		FOR i := 1 TO t.n DO m := Take(); m.n := i; t.c.Send(m) END
	END Run;

	PROCEDURE (t: CPool) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO
			t.c.Recv(x); INC(sum, x(M64).n MOD 2); Give(x(M64))
		END
	END Run;

	(* арена: по каналу идёт индекс, запись лежит в массиве *)

	PROCEDURE (t: P2) Run;
		VAR i, slot: INTEGER; e: Env;
	BEGIN
		FOR i := 1 TO t.n DO
			DEC(slotN); slot := slots[slotN];
			INC(gen[slot]); arena[slot].n := i;
			e := envs[slot]; e.idx := slot; e.gen := gen[slot];
			t.c.Send(e)
		END
	END Run;

	PROCEDURE (t: C2) Run;
		VAR i, slot: INTEGER; x: AsyncSelect.Msg; e: Env;
	BEGIN
		FOR i := 1 TO t.n DO
			t.c.Recv(x); e := x(Env); slot := e.idx;
			IF (t.check # 0) & (e.gen # gen[slot]) THEN INC(badGen) END;
			INC(sum, arena[slot].n MOD 2);
			slots[slotN] := slot; INC(slotN)
		END
	END Run;

	(* канал значений: копия записи в кольцо и из кольца *)

	PROCEDURE Put16 (b: VB16; IN r: Val16);
	BEGIN
		WHILE b.count = valCap DO Tasks.Park END;
		b.ring[(b.head + b.count) MOD valCap] := r; INC(b.count);
		IF b.cons # NIL THEN Tasks.Unpark(b.cons) END
	END Put16;

	PROCEDURE Get16 (b: VB16; OUT r: Val16);
	BEGIN
		WHILE b.count = 0 DO Tasks.Park END;
		r := b.ring[b.head]; b.head := (b.head + 1) MOD valCap; DEC(b.count);
		IF b.prod # NIL THEN Tasks.Unpark(b.prod) END
	END Get16;

	PROCEDURE Put64 (b: VB64; IN r: Val64);
	BEGIN
		WHILE b.count = valCap DO Tasks.Park END;
		b.ring[(b.head + b.count) MOD valCap] := r; INC(b.count);
		IF b.cons # NIL THEN Tasks.Unpark(b.cons) END
	END Put64;

	PROCEDURE Get64 (b: VB64; OUT r: Val64);
	BEGIN
		WHILE b.count = 0 DO Tasks.Park END;
		r := b.ring[b.head]; b.head := (b.head + 1) MOD valCap; DEC(b.count);
		IF b.prod # NIL THEN Tasks.Unpark(b.prod) END
	END Get64;

	PROCEDURE Put256 (b: VB256; IN r: Val256);
	BEGIN
		WHILE b.count = valCap DO Tasks.Park END;
		b.ring[(b.head + b.count) MOD valCap] := r; INC(b.count);
		IF b.cons # NIL THEN Tasks.Unpark(b.cons) END
	END Put256;

	PROCEDURE Get256 (b: VB256; OUT r: Val256);
	BEGIN
		WHILE b.count = 0 DO Tasks.Park END;
		r := b.ring[b.head]; b.head := (b.head + 1) MOD valCap; DEC(b.count);
		IF b.prod # NIL THEN Tasks.Unpark(b.prod) END
	END Get256;

	PROCEDURE (t: VP16) Run;
		VAR i: INTEGER; r: Val16;
	BEGIN
		t.b.prod := Tasks.Current();
		FOR i := 1 TO t.n DO
			r.b[0] := i MOD 256; r.b[3] := 1; Put16(t.b, r)
		END
	END Run;

	PROCEDURE (t: VC16) Run;
		VAR i: INTEGER; r: Val16;
	BEGIN
		t.b.cons := Tasks.Current();
		FOR i := 1 TO t.n DO Get16(t.b, r); INC(sum, r.b[0] MOD 2); INC(sum, r.b[3]) END
	END Run;

	PROCEDURE (t: VP64) Run;
		VAR i: INTEGER; r: Val64;
	BEGIN
		t.b.prod := Tasks.Current();
		FOR i := 1 TO t.n DO
			r.b[0] := i MOD 256; r.b[15] := 1; Put64(t.b, r)
		END
	END Run;

	PROCEDURE (t: VC64) Run;
		VAR i: INTEGER; r: Val64;
	BEGIN
		t.b.cons := Tasks.Current();
		FOR i := 1 TO t.n DO Get64(t.b, r); INC(sum, r.b[0] MOD 2); INC(sum, r.b[15]) END
	END Run;

	PROCEDURE (t: VP256) Run;
		VAR i: INTEGER; r: Val256;
	BEGIN
		t.b.prod := Tasks.Current();
		FOR i := 1 TO t.n DO
			r.b[0] := i MOD 256; r.b[63] := 1; Put256(t.b, r)
		END
	END Run;

	PROCEDURE (t: VC256) Run;
		VAR i: INTEGER; r: Val256;
	BEGIN
		t.b.cons := Tasks.Current();
		FOR i := 1 TO t.n DO Get256(t.b, r); INC(sum, r.b[0] MOD 2); INC(sum, r.b[63]) END
	END Run;

	(* --- прогоны обмена --------------------------------------------------- *)

	PROCEDURE Go (x: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(x, stack); Tasks.Start(x)
	END Go;

	PROCEDURE Wait;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Wait;

	PROCEDURE Var0 (n: INTEGER): LONGINT;
		VAR p: P0; c: C0; b: AsyncChan.Buffer; t0: LONGINT;
	BEGIN
		sum := 0; b := AsyncChan.NewBuffer(valCap);
		NEW(p); NEW(c); p.c := b; c.c := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var0;

	PROCEDURE Var1 (n: INTEGER): LONGINT;
		VAR p: P1; c: C1; b: AsyncChan.Buffer; t0: LONGINT;
	BEGIN
		sum := 0; b := AsyncChan.NewBuffer(valCap);
		NEW(p); NEW(c); p.c := b; c.c := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var1;

	PROCEDURE Var2 (n: INTEGER): LONGINT;
		VAR p: PPool; c: CPool; b: AsyncChan.Buffer; t0: LONGINT;
	BEGIN
		sum := 0; pool := NIL; b := AsyncChan.NewBuffer(valCap);
		NEW(p); NEW(c); p.c := b; c.c := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var2;

	PROCEDURE VarArena (n, check: INTEGER): LONGINT;
		VAR p: P2; c: C2; b: AsyncChan.Buffer; t0: LONGINT;
	BEGIN
		sum := 0; b := AsyncChan.NewBuffer(valCap); ResetArena;
		NEW(p); NEW(c); p.c := b; c.c := b; p.n := n; c.n := n; p.check := check; c.check := check;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END VarArena;

	PROCEDURE Var16 (n: INTEGER): LONGINT;
		VAR p: VP16; c: VC16; b: VB16; t0: LONGINT;
	BEGIN
		sum := 0; NEW(b);
		NEW(p); NEW(c); p.b := b; c.b := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var16;

	PROCEDURE Var64 (n: INTEGER): LONGINT;
		VAR p: VP64; c: VC64; b: VB64; t0: LONGINT;
	BEGIN
		sum := 0; NEW(b);
		NEW(p); NEW(c); p.b := b; c.b := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var64;

	PROCEDURE Var256 (n: INTEGER): LONGINT;
		VAR p: VP256; c: VC256; b: VB256; t0: LONGINT;
	BEGIN
		sum := 0; NEW(b);
		NEW(p); NEW(c); p.b := b; c.b := b; p.n := n; c.n := n;
		t0 := Now(); Go(p); Go(c); Wait();
		RETURN Now() - t0
	END Var256;

	PROCEDURE RunVar (kind, n: INTEGER): LONGINT;
	BEGIN
		CASE kind OF
		| vMsg8: RETURN Var0(n)
		| vMsg64: RETURN Var1(n)
		| vPool: RETURN Var2(n)
		| vArena: RETURN VarArena(n, 1)
		| vArenaNo: RETURN VarArena(n, 0)
		| vVal16: RETURN Var16(n)
		| vVal64: RETURN Var64(n)
		ELSE RETURN Var256(n)
		END
	END RunVar;

	PROCEDURE VarName (kind: INTEGER; VAR s: Name);
	BEGIN
		CASE kind OF
		| vMsg8: s := "msg8"
		| vMsg64: s := "msg64"
		| vPool: s := "pool64"
		| vArena: s := "arena64"
		| vArenaNo: s := "arena64-nochk"
		| vVal16: s := "val16"
		| vVal64: s := "val64"
		ELSE s := "val256"
		END
	END VarName;

	PROCEDURE One (kind: INTEGER; n: INTEGER; OUT ns, alloc: LONGINT);
		VAR t0: LONGINT;
	BEGIN
		Kernel.Collect;
		a0 := Kernel.AllocCount();
		t0 := RunVar(kind, n);
		alloc := Kernel.AllocCount() - a0;
		ns := t0
	END One;

	PROCEDURE Clusters*;
	(** Напечатать первые кластеры кучи: проверка раскладки заголовка. *)
		VAR c: LONGINT; i, k: INTEGER; w: ARRAY 8 OF LONGINT;
	BEGIN
		c := Kernel.Root();
		FOR k := 0 TO 7 DO S.GET(c + 4 * k, w[k]) END;
		W("CLUSTER "); N(c); Console.WriteLn;
		FOR k := 0 TO 7 DO W("  +"); N(4 * k); W("="); N(w[k]); Console.WriteLn END
	END Clusters;

	PROCEDURE HeapWalk (IN tag: ARRAY OF CHAR);
	(* Пройти кластеры кучи и посчитать свободные блоки по размеру. *)
		VAR c, blk, end, tg, last: LONGINT; sz, stored, stride: INTEGER;
			ncl, nfree, nlive, n88, n16, i: LONGINT;
	BEGIN
		c := Kernel.Root(); ncl := 0; nfree := 0; nlive := 0; n88 := 0;
		WHILE (c # 0) & (ncl < 100000) DO
			INC(ncl);
			S.GET(c, sz);
			blk := c + 24; end := c + (sz - 24) DIV 16 * 16;
			i := 0;
			WHILE (blk < end) & (i < 20000000) DO
				INC(i);
				S.GET(blk, tg);
				IF tg = blk + 8 THEN	(* свободный блок: tag = ADR(size) *)
					S.GET(blk + 8, stored);
					INC(nfree);
					IF stored = 88 THEN INC(n88) END;
					stride := (stored + 23) DIV 16 * 16;
					IF stride < 32 THEN stride := 32 END
				ELSIF tg > 100000H THEN
					S.GET(tg DIV 4 * 4, sz);
					IF ODD(tg DIV 2) THEN
						S.GET(blk + 8, last); sz := sz + SHORT(last - (blk + 8))
					END;
					stride := (sz + 23) DIV 16 * 16;
					IF stride < 32 THEN stride := 32 END;
					INC(nlive)
				ELSE	(* непонятный тег: прекратить обход *)
					blk := end; stride := 0
				END;
				INC(blk, stride)
			END;
			S.GET(c + 4, c)	(* next кластера: поле next @4 *)
		END;
		W("HEAP "); W(tag); W(" clusters="); N(ncl);
		W(" live="); N(nlive); W(" free="); N(nfree);
		W(" free88="); N(n88); Console.WriteLn
	END HeapWalk;

	PROCEDURE SpawnLeftovers*;
	(** Реальная задача: что остаётся в куче после 100 тыс. задач 264 и 280. *)
	BEGIN
		sum := 0; t := Spawn248(2000, TRUE); Kernel.Collect;
		t := Spawn264(padN, FALSE); HeapWalk("spawn264");
		Kernel.Collect;
		t := Spawn280(padN, FALSE); HeapWalk("spawn280");
		Kernel.Collect
	END SpawnLeftovers;

	PROCEDURE HeapLeftovers*;
	(** Что остаётся в списках свободных блоков после пар 264/280 + 72. *)
	BEGIN
		ClearPair; Kernel.Collect;
		t := PairOne(S.TYP(P264), S.TYP(P72), liveN, TRUE); HeapWalk("pair264");
		ClearPair; Kernel.Collect;
		t := PairOne(S.TYP(P280), S.TYP(P72), liveN, TRUE); HeapWalk("pair280");
		ClearPair; Kernel.Collect
	END HeapLeftovers;

	PROCEDURE TaskPoolCost*;
	(** Взять и вернуть запись 248 байт из пула: цена без NEW. *)
		VAR i: INTEGER; x: PoolR; t0: LONGINT; s2: ARRAY 32 OF CHAR;
	BEGIN
		poolR := NIL;
		FOR i := 1 TO ringCap DO NEW(x); x.next := poolR; poolR := x END;
		t0 := Now();
		FOR i := 1 TO msgN DO
			x := poolR; poolR := x.next;	(* взять *)
			x.next := poolR; poolR := x		(* вернуть *)
		END;
		W("POOL task248 totalNs="); N(Now() - t0); W(" perPair="); N((Now() - t0) DIV msgN); Console.WriteLn
	END TaskPoolCost;

	PROCEDURE Exchange*;
	(** Попеременные прогоны восьми видов обмена, медиана из reps. *)
		VAR kind, rep: INTEGER; ns, alloc: LONGINT;
			name: Name;
	BEGIN
		(* прогреть стеки и код *)
		FOR kind := 0 TO varN - 1 DO One(kind, 20000, ns, alloc) END;
		FOR rep := 0 TO reps - 1 DO
			FOR kind := 0 TO varN - 1 DO
				One(kind, msgN, ns, alloc);
				times[kind, rep] := ns;
				allocs[kind, rep] := alloc
			END
		END;
		SetCounting(TRUE);
		FOR kind := 0 TO varN - 1 DO
			collects := 0;
			ns := RunVar(kind, msgN);
			cols[kind] := collects
		END;
		SetCounting(FALSE);
		Kernel.Collect;
		FOR kind := 0 TO varN - 1 DO
			VarName(kind, name);
			W("XCHG name="); W(name);
			W(" msgs="); N(msgN);
			W(" ns="); N(Med(times[kind], reps) DIV msgN);
			W(" allocsPer1000="); N(Med(allocs[kind], reps) * 1000 DIV msgN);
			W(" collects="); N(cols[kind]);
			Console.WriteLn
		END
	END Exchange;

	PROCEDURE Msg8One*;
	(** Один прогон NEW-сообщения 8 байт: сравнение с docs/measurements.md. *)
		VAR ns: LONGINT;
	BEGIN
		One(vMsg8, msgN, ns, a0);
		W("XCHG name=msg8 msgs="); N(msgN); W(" ns="); N(ns DIV msgN); Console.WriteLn
	END Msg8One;

	PROCEDURE Pool64One*;
	(** Один прогон пула сообщений 64 байта. *)
		VAR ns: LONGINT;
	BEGIN
		One(vPool, msgN, ns, a0);
		W("XCHG name=pool64 msgs="); N(msgN); W(" ns="); N(ns DIV msgN); Console.WriteLn
	END Pool64One;

	PROCEDURE Arena64One*;
	(** Один прогон арены с проверкой поколения. *)
		VAR ns: LONGINT;
	BEGIN
		One(vArena, msgN, ns, a0);
		W("XCHG name=arena64 msgs="); N(msgN); W(" ns="); N(ns DIV msgN);
		W(" badGen="); N(badGen); Console.WriteLn
	END Arena64One;

	PROCEDURE Val64One*;
	(** Один прогон канала значений 64 байта. *)
		VAR ns: LONGINT;
	BEGIN
		One(vVal64, msgN, ns, a0);
		W("XCHG name=val64 msgs="); N(msgN); W(" ns="); N(ns DIV msgN); Console.WriteLn
	END Val64One;

	(* --- тесты ------------------------------------------------------------ *)

	PROCEDURE TestSizes*;
	(** Размеры типов — ровно заявленные; цель — запись 248 и 280 байт. *)
	BEGIN
		UnitAssert.EqualInt(248, SIZE(TT248), "task record 248");
		UnitAssert.EqualInt(280, SIZE(TT280), "task record 280");
		UnitAssert.EqualInt(16, SIZE(R16), "R16");
		UnitAssert.EqualInt(280, SIZE(R280), "R280");
		UnitAssert.EqualInt(64, SIZE(Data), "arena data 64")
	END TestSizes;

	PROCEDURE TestExchange*;
	(** Итог обмена: все сообщения дошли, поколения не нарушены. *)
		VAR i: INTEGER; ns: LONGINT; a0: LONGINT;
	BEGIN
		FOR i := 0 TO varN - 1 DO
			One(i, 2000, ns, a0);
			UnitAssert.True(sum > 0, "exchange delivered")
		END;
		UnitAssert.EqualInt(0, badGen, "arena generation")
	END TestExchange;

BEGIN
	collects := 0; countOn := FALSE; oldDebug := FALSE; oldWatch := NIL;
	slotN := 0; pool := NIL; badGen := 0
END Async_devAlloc.
