MODULE Async_devStacks;
(** Замеры стеков задач для docs/stacks-study.md (Linux amd64).

	Depth — сколько байт стека задача трогает: от верха стека до самого
	глубокого изменённого слова. Задача заливает свободную часть своего
	стека шаблоном, делает операцию ожидания и ищет снизу первое
	изменённое слово. Строка DEPTH <операция> boot=… op=…: boot — байт от
	верха до кадра Run (TaskBoot, Try, TaskBody), op — от верха до самого
	глубокого слова, тронутого операцией.

	Load — 10 тыс. задач по одному Sleep(0) с заданным размером стека,
	строка BENCH с обращениями к отсутствующим страницам (getrusage,
	minor faults) отдельно на создание и на исполнение.

	Mass… — n задач у плотного поставщика стеков (Tasks.NewDenseDir) разом:
	время создания, память процесса (RSS), число отображений, исполнение.
	Модуль собирается с ключом % (go64 Async_devStacks%), иначе плотный
	поставщик отдаёт задачам этого модуля стеки со сторожем.

	Ceiling… — потолок числа задач у поставщика: задачи создаются разом, не
	исполняясь (у незапущенной задачи запись стека в реестре ядра имеет sp = 0,
	поэтому сборка при создании не сканирует стеки; иначе создание становится
	квадратичным), пока очередной Tasks.Start не откажет. Отказ — трап 22 от
	HALT(22) в StartIn (AllocStack вернул FALSE); он ловится Kernel.Try вокруг
	создания, процесс жив. После отказа каждая созданная задача исполняется один
	раз (касается своего стека и засыпает), затем все разбужены и завершены.
	Строка CEIL печатает, сколько задач успело создаться, признак отказа, код
	трапа, число строк /proc/self/maps и VmRSS/VmSize из /proc/self/status до и
	после первого исполнения.

	Maps… — сколько строк /proc/self/maps добавляет одна задача у каждого
	поставщика (1000, 10 000 и 100 000 задач). Отсюда расчётный потолок при
	чужом vm.max_map_count: предел участков делится на участки на задачу. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Tasks, AsyncSelect, AsyncChan, System_devCRaw, System_devClock,
		Console, Strings;

	CONST
		pattern = 5A5A5A5AH;
		SP = 4;	(* номер регистра указателя стека amd64 *)
		rounds = 3;	(* операций ожидания на замер глубины *)
		sleepForever = 1000000000;	(* срок сна задачи замера, мс (~11,6 суток) *)
		yield = 0; chan = 1; select = 2; await = 3; buffer = 4;

	TYPE
		M = POINTER TO RECORD (AsyncSelect.Msg) END;

		Box = POINTER TO RECORD n: INTEGER END;

		Probe = POINTER TO RECORD (Tasks.Task)
			kind: INTEGER;
			a, b: AsyncChan.Chan;
			buf: AsyncChan.Buffer;
			box: Box;
			boot, op: LONGINT
		END;

		Partner = POINTER TO RECORD (Tasks.Task)
			kind: INTEGER;
			a, b: AsyncChan.Chan;
			buf: AsyncChan.Buffer;
			box: Box
		END;

		Once = POINTER TO RECORD (Tasks.Task) END;

		Waiter = POINTER TO RECORD (Tasks.Task)
			link: Waiter;	(* список созданных задач для разблокировки *)
			mode: INTEGER;	(* 0 — Park до Unpark, 1 — Sleep до Unpark *)
			sum: INTEGER	(* след касания своего стека *)
		END;

	VAR
		counter: INTEGER;
		ceilHead: Waiter;	(* уже созданные задачи замера потолка *)
		ceilLimit, ceilSize, ceilMade, ceilMode: INTEGER;
		ceilDone: BOOLEAN;
		ceilRan: INTEGER;	(* сколько задач уже исполнилось хотя бы раз *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE [exclusive] (b: Box) Wait, NEW;
	BEGIN
		ACTIVE.AWAIT(b.n > 0); DEC(b.n)
	END Wait;

	PROCEDURE [exclusive] (b: Box) Put, NEW;
	BEGIN
		INC(b.n)
	END Put;

	PROCEDURE (p: Probe) Run;
		VAR s: Kernel.Stack; a, top, sp: S.ADDRESS; i, w: INTEGER; x: AsyncSelect.Msg;
			ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		s := Kernel.StackOf(S.ADR(a));
		S.GETREG(SP, sp);
		top := sp - 256;	(* ниже — только кадры вызовов из Run *)
		p.boot := s.hi - sp;
		a := s.lo;
		WHILE a < top DO S.PUT(a, pattern); INC(a, 4) END;
		FOR i := 1 TO rounds DO
			CASE p.kind OF
			| yield: Tasks.Sleep(0)
			| chan: p.a.Send(NIL); p.b.Recv(x)
			| select:
				AsyncChan.RecvAlt(p.a, ops[0]); AsyncChan.RecvAlt(p.b, ops[1]);
				w := AsyncChan.Select(ops)
			| await: p.box.Wait
			| buffer: p.buf.Recv(x)
			END
		END;
		a := s.lo;
		S.GET(a, w);
		WHILE (a < top) & (w = pattern) DO INC(a, 4); S.GET(a, w) END;
		p.op := s.hi - a
	END Run;

	PROCEDURE (p: Partner) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg; m: M;
	BEGIN
		NEW(m);
		FOR i := 1 TO rounds DO
			CASE p.kind OF
			| yield: Tasks.Sleep(0)
			| chan: p.a.Recv(x); p.b.Send(m)
			| select: p.a.Send(m)
			| await: Tasks.Sleep(0); p.box.Put
			| buffer: p.buf.Send(m)
			END
		END
	END Run;

	PROCEDURE Pump;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Pump;

	PROCEDURE Depth1 (kind: INTEGER; IN name: ARRAY OF CHAR);
		VAR p: Probe; q: Partner;
	BEGIN
		NEW(p); NEW(q); p.kind := kind; q.kind := kind;
		p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan(); q.a := p.a; q.b := p.b;
		p.buf := AsyncChan.NewBuffer(4); q.buf := p.buf;
		NEW(p.box); q.box := p.box;
		Tasks.Start(p); Tasks.Start(q);
		Pump;
		W("DEPTH "); W(name); W(" boot="); N(p.boot); W(" op="); N(p.op); Console.WriteLn
	END Depth1;

	PROCEDURE Depth*;
	BEGIN
		Depth1(yield, "yield");
		Depth1(chan, "chan");
		Depth1(select, "select2");
		Depth1(await, "await");
		Depth1(buffer, "buffer")
	END Depth;

	PROCEDURE (t: Once) Run;
	BEGIN
		Tasks.Sleep(0); INC(counter)
	END Run;

	PROCEDURE Load1 (n, size, phase: INTEGER; IN tag: ARRAY OF CHAR);
	(* phase, phase + 1 — номера фаз создания и исполнения для окна счётчиков
	   perf (System_devCRaw.c_perf, переменная PERF_PHASE). *)
		VAR i: INTEGER; t: Once; t0, t1, t2, t3, f0, f1, f2, vmas: LONGINT;
	BEGIN
		counter := 0;
		f0 := System_devCRaw.c_minflt(); t0 := System_devClock.now();
		System_devCRaw.c_perf(phase, 1);
		FOR i := 1 TO n DO NEW(t); Tasks.SetStackSize(t, size); Tasks.Start(t) END;
		System_devCRaw.c_perf(phase, 0);
		t1 := System_devClock.now();
		vmas := System_devCRaw.c_vmas();	(* чтение maps — вне окон времени *)
		f1 := System_devCRaw.c_minflt(); t3 := System_devClock.now();
		System_devCRaw.c_perf(phase + 1, 1);
		Pump;
		System_devCRaw.c_perf(phase + 1, 0);
		f2 := System_devCRaw.c_minflt(); t2 := System_devClock.now();
		ASSERT(counter = n, 60);
		W("BENCH "); W(tag); W(" stack="); N(size);
		W(" setupNs="); N(t1 - t0); W(" setupFaults="); N(f1 - f0);
		W(" stepNs="); N(t2 - t3); W(" stepFaults="); N(f2 - f1); W(" vmas="); N(vmas); Console.WriteLn
	END Load1;

	PROCEDURE Load*;
	(** Первый прогон — свежие стеки 64 КБ, второй — те же из пула. *)
	BEGIN
		Load1(10000, 64 * 1024, 1, "load");
		Load1(10000, 64 * 1024, 3, "load-warm")
	END Load;

	PROCEDURE Scale*;
	(** Исполнение на задачу при 100, 1000 и 10 тыс. задач, стеки из пула
		(второй прогон каждого размера): рост цены с числом задач — промахи
		кеша и TLB по стекам, а не отказы страниц. *)
	BEGIN
		Load1(100, 64 * 1024, 10, "scale100"); Load1(100, 64 * 1024, 10, "scale100-warm");
		Load1(1000, 64 * 1024, 10, "scale1000"); Load1(1000, 64 * 1024, 10, "scale1000-warm");
		Load1(10000, 64 * 1024, 10, "scale10k"); Load1(10000, 64 * 1024, 10, "scale10k-warm")
	END Scale;

	PROCEDURE Empty (x: INTEGER): INTEGER;
	BEGIN
		RETURN x + 1
	END Empty;

	PROCEDURE Fib (n: INTEGER): INTEGER;
	BEGIN
		IF n < 2 THEN RETURN n END;
		RETURN Fib(n - 1) + Fib(n - 2)
	END Fib;

	PROCEDURE Calls*;
	(** Цена вызова: пустая процедура 1e8 раз и рекурсия Fib(30); с ключом
		компилятора "%" к каждому вызову прибавляется проверка границы стека. *)
		VAR i, x: INTEGER; t0: LONGINT;
	BEGIN
		x := 0; t0 := System_devClock.now();
		FOR i := 1 TO 100000000 DO x := Empty(x) END;
		W("BENCH call ops=100000000 nsPerOp10="); N((System_devClock.now() - t0) DIV 10000000); Console.WriteLn;
		t0 := System_devClock.now(); x := Fib(30);
		W("BENCH fib30 ms="); N((System_devClock.now() - t0) DIV 1000000); W(" x="); N(x); Console.WriteLn
	END Calls;

	PROCEDURE Mass1 (n, size, reserve: INTEGER; IN tag: ARRAY OF CHAR);
		VAR i: INTEGER; t: Once; old: Tasks.StackDir; t0, t1, t2, t3, r0, r1, r2, vmas: LONGINT;
	BEGIN
		old := Tasks.StackDirOf(); Tasks.SetStackDir(Tasks.NewDenseDir(reserve));
		counter := 0; r0 := System_devCRaw.c_rss(); t0 := System_devClock.now();
		FOR i := 1 TO n DO
			NEW(t); Tasks.SetStackSize(t, size); Tasks.Start(t);
			IF i MOD 250000 = 0 THEN W("MASS created="); N(i); Console.WriteLn END
		END;
		t1 := System_devClock.now();
		vmas := System_devCRaw.c_vmas(); r1 := System_devCRaw.c_rss();
		t3 := System_devClock.now();
		Pump;
		t2 := System_devClock.now(); r2 := System_devCRaw.c_rss();
		ASSERT(counter = n, 60);
		W("BENCH "); W(tag); W(" n="); N(n); W(" stack="); N(size); W(" reserve="); N(reserve);
		W(" createMs="); N((t1 - t0) DIV 1000000); W(" createNsPerTask="); N((t1 - t0) DIV n);
		W(" rssMB="); N((r1 - r0) DIV (1024 * 1024)); W(" vmas="); N(vmas);
		W(" runMs="); N((t2 - t3) DIV 1000000); W(" rssAfterMB="); N((r2 - r0) DIV (1024 * 1024)); Console.WriteLn;
		Tasks.SetStackDir(old)
	END Mass1;

	PROCEDURE Mass100k4*;
	BEGIN Mass1(100000, 4096, 2048, "mass100k-4k")
	END Mass100k4;

	PROCEDURE Mass100k8*;
	BEGIN Mass1(100000, 8192, 2048, "mass100k-8k")
	END Mass100k8;

	PROCEDURE Mass1m4*;
	BEGIN Mass1(1000000, 4096, 2048, "mass1m-4k")
	END Mass1m4;

	PROCEDURE Mass1m8*;
	BEGIN Mass1(1000000, 8192, 2048, "mass1m-8k")
	END Mass1m8;

	PROCEDURE LoadSmall*;
	(** То же со стеком 8 КБ (свежий процесс: пул отдаёт стек только того же размера). *)
	BEGIN
		Load1(10000, 8 * 1024, 1, "load8k");
		Load1(10000, 8 * 1024, 3, "load8k-warm")
	END LoadSmall;

	(* --- потолок числа задач --------------------------------------------- *)

	PROCEDURE (w: Waiter) Run;
	(* Один раз касается своего стека, отмечается в ceilRan и встаёт в ожидание.
	   mode 1 — Sleep: у спящих есть срок, поэтому планировщик не считает
	   остановку тупиком. mode 0 — Park: так задачи в MAPS не исполняются до
	   Unpark, который делает Park пустым (задача сразу завершается). *)
		VAR buf: ARRAY 128 OF CHAR; i, x: INTEGER;
	BEGIN
		i := 0;
		WHILE i < LEN(buf) DO buf[i] := CHR(65 + i MOD 26); INC(i) END;
		x := ORD(buf[0]) + ORD(buf[LEN(buf) - 1]);
		w.sum := x; INC(ceilRan);
		IF w.mode = 0 THEN Tasks.Park ELSE Tasks.Sleep(sleepForever) END
	END Run;

	PROCEDURE CreateAll (a, b, c: INTEGER);
	(* Создаёт задачи до ceilLimit, не исполняя их: у ещё не запущенной задачи
	   запись стека в реестре ядра имеет sp = 0, поэтому сборка при создании не
	   сканирует стеки (иначе скан всех стеков на каждой сборке делает создание
	   квадратичным по числу задач). Отказ платформы (HALT(22) в StartIn)
	   прерывает цикл и ловится Kernel.Try снаружи; ceilDone тогда FALSE. *)
		VAR i: INTEGER; t: Waiter;
	BEGIN
		FOR i := ceilMade + 1 TO ceilLimit DO
			NEW(t); t.mode := ceilMode; Tasks.SetStackSize(t, ceilSize); t.link := ceilHead;
			Tasks.Start(t);
			ceilHead := t; ceilMade := i
		END;
		ceilDone := TRUE
	END CreateAll;

	PROCEDURE RunCreated;
	(* Исполнить каждую созданную задачу один раз: она коснётся своего стека и
	   уснёт. Создание уже кончилось, поэтому сборки здесь не запускаются. *)
	BEGIN
		WHILE ceilRan < ceilMade DO Tasks.Step END
	END RunCreated;

	PROCEDURE Ceiling1 (kind, limit, size, reserve: INTEGER; IN tag: ARRAY OF CHAR);
		VAR old, d: Tasks.StackDir; t: Waiter; maps0, maps1, maps2, rss0, rss1, rss2, vmsz1, vmsz2: LONGINT;
			err: INTEGER; refused: BOOLEAN; createdMs, runMs: LONGINT;
	BEGIN
		old := Tasks.StackDirOf();
		CASE kind OF
		| 0: d := Tasks.NewGuardDir()
		| 1: d := Tasks.NewSlabDir()
		| 2: d := Tasks.NewDenseDir(reserve)
		END;
		maps0 := System_devCRaw.c_vmas(); rss0 := System_devCRaw.c_vmrss();
		Tasks.SetStackDir(d);
		ceilHead := NIL; ceilMade := 0; ceilRan := 0; ceilLimit := limit; ceilSize := size;
		ceilDone := FALSE; ceilMode := 1;
		createdMs := System_devClock.now();
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(CreateAll)), 0, 0, 0);
		refused := ~ceilDone; err := Kernel.err;
		maps1 := System_devCRaw.c_vmas();
		rss1 := System_devCRaw.c_vmrss(); vmsz1 := System_devCRaw.c_vmsize();
		createdMs := (System_devClock.now() - createdMs) DIV 1000000;
		(* однократное исполнение всех созданных задач: касание стеков *)
		runMs := System_devClock.now(); RunCreated; runMs := (System_devClock.now() - runMs) DIV 1000000;
		maps2 := System_devCRaw.c_vmas(); rss2 := System_devCRaw.c_vmrss(); vmsz2 := System_devCRaw.c_vmsize();
		Tasks.SetStackDir(old);
		t := ceilHead;
		WHILE t # NIL DO Tasks.Unpark(t); t := t.link END;
		Pump;
		W("CEIL "); W(tag); W(" size="); N(size); W(" limit="); N(limit); W(" made="); N(ceilMade);
		W(" refused="); IF refused THEN W("1"); W(" err="); N(err) ELSE W("0") END;
		W(" maps0="); N(maps0); W(" mapsBeforeRun="); N(maps1); W(" mapsAfterRun="); N(maps2);
		W(" rssBeforeMB="); N((rss1 - rss0) DIV (1024 * 1024));
		W(" rssAfterMB="); N((rss2 - rss0) DIV (1024 * 1024));
		W(" vmsizeBeforeMB="); N(vmsz1 DIV (1024 * 1024));
		W(" vmsizeAfterMB="); N(vmsz2 DIV (1024 * 1024));
		W(" createMs="); N(createdMs); W(" runMs="); N(runMs); Console.WriteLn
	END Ceiling1;

	PROCEDURE CeilingSmoke*;
	(** Быстрая проверка механизма замера: 1000 задач со стеком 4 КБ, отказа нет. *)
	BEGIN Ceiling1(0, 1000, 4096, 0, "smoke")
	END CeilingSmoke;

	PROCEDURE CeilingRefuse*;
	(** Проверка пути отказа: стек 70 МБ больше Kernel.maxStackScan, поэтому
		AllocStack отказывает на первой задаче; отказ ловится, процесс жив. *)
	BEGIN Ceiling1(0, 1, 70 * 1024 * 1024, 0, "refuse")
	END CeilingRefuse;

	PROCEDURE CeilingGuard4k*;
	(** (а) стек со сторожем на задачу, стек 4 КБ; предел поиска 2 млн задач. *)
	BEGIN Ceiling1(0, 2000000, 4096, 0, "guard4k")
	END CeilingGuard4k;

	PROCEDURE CeilingSlab4k*;
	(** (б) плиты со сторожем на стек, стек 4 КБ; предел поиска 2 млн задач. *)
	BEGIN Ceiling1(1, 2000000, 4096, 0, "slab4k")
	END CeilingSlab4k;

	PROCEDURE CeilingDense4k*;
	(** (в) плотные плиты, стек 4 КБ; предел поиска 1 млн задач. *)
	BEGIN Ceiling1(2, 1000000, 4096, 2048, "dense4k")
	END CeilingDense4k;

	PROCEDURE CeilingDense4m*;
	(** (в) плотные плиты, стек 4 КБ; предел 4 млн — ищем предел по памяти. *)
	BEGIN Ceiling1(2, 4000000, 4096, 2048, "dense4m")
	END CeilingDense4m;

	PROCEDURE CeilingGuardDef*;
	(** (а) стек по умолчанию (256 КБ); предел поиска 2 млн задач. *)
	BEGIN Ceiling1(0, 2000000, 256 * 1024, 0, "guardDef")
	END CeilingGuardDef;

	PROCEDURE CeilingSlabDef*;
	(** (б) стек по умолчанию (256 КБ); предел поиска 2 млн задач. *)
	BEGIN Ceiling1(1, 2000000, 256 * 1024, 0, "slabDef")
	END CeilingSlabDef;

	PROCEDURE CeilingDenseDef*;
	(** (в) стек по умолчанию (256 КБ); предел поиска 1 млн задач. *)
	BEGIN Ceiling1(2, 1000000, 256 * 1024, 2048, "denseDef")
	END CeilingDenseDef;

	PROCEDURE Maps1 (kind, n, size, reserve: INTEGER; IN tag: ARRAY OF CHAR);
	(* Создаёт n задач без шага планировщика (стеки уже выделены), печатает
	   число добавленных строк /proc/self/maps, затем разблокирует и завершает. *)
		VAR old, d: Tasks.StackDir; t: Waiter; maps0, maps1: LONGINT; refused: BOOLEAN; err: INTEGER;
	BEGIN
		old := Tasks.StackDirOf();
		CASE kind OF
		| 0: d := Tasks.NewGuardDir()
		| 1: d := Tasks.NewSlabDir()
		| 2: d := Tasks.NewDenseDir(reserve)
		END;
		maps0 := System_devCRaw.c_vmas();
		Tasks.SetStackDir(d);
		ceilHead := NIL; ceilMade := 0; ceilRan := 0; ceilLimit := n; ceilSize := size;
		ceilDone := FALSE; ceilMode := 0;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(CreateAll)), 0, 0, 0);
		refused := ~ceilDone; err := Kernel.err;
		maps1 := System_devCRaw.c_vmas();
		Tasks.SetStackDir(old);
		t := ceilHead;
		WHILE t # NIL DO Tasks.Unpark(t); t := t.link END;
		Pump;
		W("MAPS "); W(tag); W(" n="); N(ceilMade); W(" size="); N(size);
		W(" maps0="); N(maps0); W(" maps="); N(maps1); W(" added="); N(maps1 - maps0);
		W(" milliPerTask="); N((maps1 - maps0) * 1000 DIV ceilMade);
		W(" refused="); IF refused THEN W("1"); W(" err="); N(err) ELSE W("0") END; Console.WriteLn
	END Maps1;

	PROCEDURE MapsPerTask*;
	(** Добавка строк /proc/self/maps на задачу у трёх поставщиков: 1000,
		10 тыс. и 100 тыс. задач, стек 4 КБ. *)
	BEGIN
		Maps1(0, 1000, 4096, 0, "guard1k"); Maps1(0, 10000, 4096, 0, "guard10k");
		Maps1(0, 100000, 4096, 0, "guard100k");
		Maps1(1, 1000, 4096, 0, "slab1k"); Maps1(1, 10000, 4096, 0, "slab10k");
		Maps1(1, 100000, 4096, 0, "slab100k");
		Maps1(2, 1000, 4096, 2048, "dense1k"); Maps1(2, 10000, 4096, 2048, "dense10k");
		Maps1(2, 100000, 4096, 2048, "dense100k")
	END MapsPerTask;

END Async_devStacks.
