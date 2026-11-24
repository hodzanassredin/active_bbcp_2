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
	minor faults) отдельно на создание и на исполнение. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Tasks, AsyncSelect, AsyncChan, System_devCRaw, System_devClock,
		Console, Strings;

	CONST
		pattern = 5A5A5A5AH;
		SP = 4;	(* номер регистра указателя стека amd64 *)
		rounds = 3;	(* операций ожидания на замер глубины *)
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

	VAR
		counter: INTEGER;

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
		VAR i: INTEGER; t: Once; t0, t1, t2, f0, f1, f2: LONGINT;
	BEGIN
		counter := 0;
		f0 := System_devCRaw.c_minflt(); t0 := System_devClock.now();
		System_devCRaw.c_perf(phase, 1);
		FOR i := 1 TO n DO NEW(t); Tasks.SetStackSize(t, size); Tasks.Start(t) END;
		System_devCRaw.c_perf(phase, 0);
		f1 := System_devCRaw.c_minflt(); t1 := System_devClock.now();
		System_devCRaw.c_perf(phase + 1, 1);
		Pump;
		System_devCRaw.c_perf(phase + 1, 0);
		f2 := System_devCRaw.c_minflt(); t2 := System_devClock.now();
		ASSERT(counter = n, 60);
		W("BENCH "); W(tag); W(" stack="); N(size);
		W(" setupNs="); N(t1 - t0); W(" setupFaults="); N(f1 - f0);
		W(" stepNs="); N(t2 - t1); W(" stepFaults="); N(f2 - f1); Console.WriteLn
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

	PROCEDURE LoadSmall*;
	(** То же со стеком 8 КБ (свежий процесс: пул отдаёт стек только того же размера). *)
	BEGIN
		Load1(10000, 8 * 1024, 1, "load8k");
		Load1(10000, 8 * 1024, 3, "load8k-warm")
	END LoadSmall;

END Async_devStacks.
