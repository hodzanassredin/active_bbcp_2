MODULE System_devStackDirs;
(** Общие тесты поставщиков стеков (Tasks.StackDir): каждый тест проходит
	по всем трём поставщикам — (а) стек со сторожем, (б) плиты со сторожем
	на стек, (в) плотные плиты с запасом и канарейкой. Модуль собран с
	ключом % (tools/tests.txt): его задачи проверяют границу стека в прологе. *)

	IMPORT S := SYSTEM, Kernel, Tasks, System_devStackPlain, UnitAssert;

	CONST
		nDirs = 3;
		reserve = 2048;	(* запас плотного поставщика в тестах *)
		small = 16 * 1024;

	TYPE
		Calm = POINTER TO RECORD (Tasks.Task) n: INTEGER; lo, hi: S.ADDRESS END;
		Smash = POINTER TO RECORD (Tasks.Task) END;
		Deep = POINTER TO RECORD (Tasks.Task) END;
		Halter = POINTER TO RECORD (Tasks.Task) lo: S.ADDRESS END;
		Holder = POINTER TO RECORD (Tasks.Task) data: POINTER TO ARRAY OF INTEGER; ok: BOOLEAN END;
		Diver = POINTER TO RECORD (Tasks.Task) lo, low: S.ADDRESS; budget: INTEGER; done: BOOLEAN END;
		Below = POINTER TO RECORD (Tasks.Task) END;
		Once = POINTER TO RECORD (Tasks.Task) END;
		Parker = POINTER TO RECORD (Tasks.Task) lo: S.ADDRESS END;
		SmashEnd = POINTER TO RECORD (Tasks.Task) END;
		PlainSub = POINTER TO RECORD (System_devStackPlain.Plain) END;	(* Run унаследован: без ключа *)
		PlainOwn = POINTER TO RECORD (System_devStackPlain.Plain) END;	(* свой Run: с ключом *)

	VAR
		onceCount: INTEGER;
		dirs: ARRAY nDirs OF Tasks.StackDir;
		names: ARRAY nDirs OF ARRAY 16 OF CHAR;

	PROCEDURE (t: Calm) Run;
		VAR i: INTEGER; st: Kernel.Stack;
	BEGIN
		st := Kernel.StackOf(S.ADR(i)); t.lo := st.lo; t.hi := st.hi;
		FOR i := 1 TO 10 DO INC(t.n); Tasks.Yield END
	END Run;

	PROCEDURE (t: Smash) Run;
	(* Портит канарейку у дна своего стека, как это сделал бы код без проверки,
	   ушедший ниже запаса, и уступает. *)
		VAR x: INTEGER;
	BEGIN
		S.PUT(Kernel.StackOf(S.ADR(x)).lo, 0);
		Tasks.Yield;
		x := 1
	END Run;

	PROCEDURE Rec (n: INTEGER): INTEGER;
		VAR a: ARRAY 64 OF INTEGER;
	BEGIN
		a[n MOD 64] := n;
		IF n = 0 THEN RETURN 0 END;
		RETURN Rec(n - 1) + a[n MOD 64]
	END Rec;

	PROCEDURE (t: Deep) Run;
		VAR x: INTEGER;
	BEGIN
		x := Rec(100000)
	END Run;

	PROCEDURE (t: Halter) Run;
		VAR x: INTEGER;
	BEGIN
		t.lo := Kernel.StackOf(S.ADR(x)).lo; Tasks.Yield;
		HALT(42)
	END Run;

	PROCEDURE (t: Holder) Run;
		VAR i, k: INTEGER;
	BEGIN
		t.ok := TRUE;
		FOR k := 1 TO 20 DO
			NEW(t.data, 1000); FOR i := 0 TO 999 DO t.data[i] := i * k END;
			Tasks.Yield;
			FOR i := 0 TO 999 DO IF t.data[i] # i * k THEN t.ok := FALSE END END
		END
	END Run;

	PROCEDURE Down (d: Diver);
	(* Проверяемый код уходит вглубь до последнего кадра, который пролог ещё
	   пускает, и оттуда зовёт код без проверки на d.budget байт. *)
		VAR a: ARRAY 8 OF INTEGER; n, f: INTEGER;
	BEGIN
		a[0] := 0;
		IF S.ADR(a) - Kernel.stkLo > 1024 + 512 THEN Down(d)
		ELSE
			System_devStackPlain.lowest := S.ADR(a); n := System_devStackPlain.Burn(1);
			f := SHORT(S.ADR(a) - System_devStackPlain.lowest);	(* ~ два кадра Burn *)
			n := (SHORT(S.ADR(a) - Kernel.stkLo) + d.budget) DIV (f DIV 2 + 1);
			System_devStackPlain.lowest := S.ADR(a);
			n := System_devStackPlain.Burn(n);
			d.low := System_devStackPlain.lowest
		END
	END Down;

	PROCEDURE (t: Diver) Run;
		VAR x: INTEGER;
	BEGIN
		t.lo := Kernel.StackOf(S.ADR(x)).lo;
		Down(t); Tasks.Yield; t.done := TRUE
	END Run;

	PROCEDURE (t: Below) Run;
		VAR x: INTEGER;
	BEGIN
		S.PUT(Kernel.StackOf(S.ADR(x)).lo - 8, 0)	(* ниже первого стека плиты *)
	END Run;

	PROCEDURE (t: Parker) Run;
		VAR x: INTEGER;
	BEGIN
		t.lo := Kernel.StackOf(S.ADR(x)).lo; Tasks.Park
	END Run;

	PROCEDURE (t: Once) Run;
	BEGIN
		Tasks.Yield; INC(onceCount)
	END Run;

	PROCEDURE (t: SmashEnd) Run;
		VAR x: INTEGER;
	BEGIN
		S.PUT(Kernel.StackOf(S.ADR(x)).lo, 0)	(* и выход без ожидания: ловит уборка *)
	END Run;

	PROCEDURE (t: PlainOwn) Run;
	BEGIN
		t.n := 2
	END Run;

	PROCEDURE Pump;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 5000) DO Tasks.Step END
	END Pump;

	PROCEDURE Use (k: INTEGER);
	BEGIN
		CASE k OF
		| 0: dirs[k] := Tasks.NewGuardDir(); names[k] := "guard"
		| 1: dirs[k] := Tasks.NewSlabDir(); names[k] := "slab"
		| 2: dirs[k] := Tasks.NewDenseDir(reserve); names[k] := "dense"
		END;
		Tasks.SetStackDir(dirs[k])
	END Use;

	PROCEDURE TestProcEachDirRuns*;
	(* Задачи исполняются; стек выдан выбранным поставщиком; следующая задача
	   того же размера берёт стек из пула (тот же адрес). *)
		VAR k: INTEGER; old: Tasks.StackDir; a, b: Calm; lo: S.ADDRESS;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			NEW(a); Tasks.SetStackSize(a, small); Tasks.Start(a);
			UnitAssert.True(Tasks.StackDirOfTask(a) = dirs[k], names[k]);
			Pump; lo := a.lo;
			UnitAssert.EqualInt(10, a.n, names[k]);
			NEW(b); Tasks.SetStackSize(b, small); Tasks.Start(b); Pump;
			UnitAssert.EqualInt(10, b.n, names[k]);
			UnitAssert.True(b.lo = lo, "stack from the pool");
			UnitAssert.True(b.hi - b.lo >= small, "stack not smaller than asked")
		END;
		Tasks.SetStackDir(old)
	END TestProcEachDirRuns;

	PROCEDURE TestProcSwitchDirWithLiveTasks*;
	(* Смена поставщика при живых задачах: каждая возвращает стек своему. *)
		CONST n = 5;
		VAR k, i: INTEGER; old: Tasks.StackDir; c: ARRAY nDirs * n OF Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			FOR i := 0 TO n - 1 DO NEW(c[k * n + i]); Tasks.SetStackSize(c[k * n + i], small); Tasks.Start(c[k * n + i]) END;
			Tasks.Step
		END;
		Tasks.SetStackDir(old);
		Pump;
		FOR i := 0 TO nDirs * n - 1 DO
			UnitAssert.EqualInt(10, c[i].n, "task finished");
			UnitAssert.True(~Tasks.Failed(c[i]), "task not failed")
		END;
		Tasks.Step;
		UnitAssert.EqualInt(0, Tasks.Count(), "no tasks")
	END TestProcSwitchDirWithLiveTasks;

	PROCEDURE TestProcStackBackToOwnDir*;
	(* Задача, запущенная у плотного поставщика, после смены поставщика
	   возвращает стек ему, а не текущему: задача текущего поставщика этот
	   стек не получает. (Плотный поставщик без живых стеков затем
	   опустошается, поэтому «тот же адрес у плотного» не проверяется.) *)
		VAR old: Tasks.StackDir; a: Parker; c: Calm;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2);
		NEW(a); Tasks.SetStackSize(a, small); Tasks.Start(a);
		Tasks.Step;	(* a запаркована на плотном стеке *)
		Use(1);
		Tasks.Unpark(a); Pump;	(* a завершается при другом текущем поставщике *)
		NEW(c); Tasks.SetStackSize(c, small); Tasks.Start(c); Pump;
		UnitAssert.True(c.lo # a.lo, "the current provider did not get the dense stack");
		Tasks.SetStackDir(old)
	END TestProcStackBackToOwnDir;

	PROCEDURE TestProcOverflowTraps137*;
	(* Переполнение в проверяемом коде: трап 137, соседи живы — у каждого поставщика. *)
		VAR k, i: INTEGER; old: Tasks.StackDir; d: Deep; c: ARRAY 4 OF Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			FOR i := 0 TO 1 DO NEW(c[i]); Tasks.SetStackSize(c[i], small); Tasks.Start(c[i]) END;
			NEW(d); Tasks.SetStackSize(d, small); Tasks.Start(d);
			FOR i := 2 TO 3 DO NEW(c[i]); Tasks.SetStackSize(c[i], small); Tasks.Start(c[i]) END;
			Pump;
			UnitAssert.True(Tasks.Failed(d), names[k]);
			UnitAssert.EqualInt(137, Kernel.err, names[k]);
			FOR i := 0 TO 3 DO
				UnitAssert.EqualInt(10, c[i].n, names[k]); UnitAssert.True(~Tasks.Failed(c[i]), names[k])
			END
		END;
		Tasks.SetStackDir(old)
	END TestProcOverflowTraps137;

	PROCEDURE TestProcPoolStackAfterTrap*;
	(* Трап в задаче; её стек из пула получает следующая, и та работает. *)
		VAR k: INTEGER; old: Tasks.StackDir; h: Halter; c: Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			NEW(h); Tasks.SetStackSize(h, small); Tasks.Start(h); Pump;
			UnitAssert.True(Tasks.Failed(h), names[k]);
			UnitAssert.EqualInt(42, Kernel.err, names[k]);
			NEW(c); Tasks.SetStackSize(c, small); Tasks.Start(c); Pump;
			UnitAssert.True(c.lo = h.lo, names[k]);
			UnitAssert.EqualInt(10, c.n, names[k]); UnitAssert.True(~Tasks.Failed(c), names[k])
		END;
		Tasks.SetStackDir(old)
	END TestProcPoolStackAfterTrap;

	PROCEDURE TestProcCollectWithLiveTasks*;
	(* Сборка мусора между шагами: данные, на которые ссылаются только кадры
	   и поля спящих задач, живы. *)
		CONST n = 20;
		VAR k, i, j: INTEGER; old: Tasks.StackDir; h: ARRAY n OF Holder;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			FOR i := 0 TO n - 1 DO NEW(h[i]); Tasks.SetStackSize(h[i], small); Tasks.Start(h[i]) END;
			FOR j := 1 TO 30 DO Tasks.Step; Kernel.Collect END;
			Pump;
			FOR i := 0 TO n - 1 DO UnitAssert.True(h[i].ok & ~Tasks.Failed(h[i]), names[k]) END
		END;
		Tasks.SetStackDir(old)
	END TestProcCollectWithLiveTasks;

	PROCEDURE TestProcUncheckedWithinReserve*;
	(* Проверяемый код на последнем пропускаемом кадре зовёт код без проверки,
	   который берёт почти весь запас ниже Kernel.stkLo (у стека со сторожем —
	   1 КБ запаса пролога). Канарейка цела, сосед жив, ниже lo не писали. *)
		VAR k: INTEGER; old: Tasks.StackDir; d: Diver; c: Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO nDirs - 1 DO
			Use(k);
			NEW(c); Tasks.SetStackSize(c, small); Tasks.Start(c);
			NEW(d); Tasks.SetStackSize(d, small);
			IF k = 2 THEN d.budget := reserve - 256 ELSE d.budget := -256 END;
			Tasks.Start(d);
			Pump;
			UnitAssert.True(d.done & ~Tasks.Failed(d), names[k]);
			UnitAssert.True(d.low >= d.lo, "unchecked code stayed above lo");
			UnitAssert.True(d.low < d.lo + reserve DIV 2 + 1024, "unchecked code went deep");
			UnitAssert.EqualInt(10, c.n, names[k]); UnitAssert.True(~Tasks.Failed(c), names[k])
		END;
		Tasks.SetStackDir(old)
	END TestProcUncheckedWithinReserve;

	PROCEDURE TestProcCanaryCaughtAtReap*;
	(* Задача портит канарейку и выходит без ожидания: уборка помечает её
	   ошибочной и сообщает. *)
		VAR old: Tasks.StackDir; s: SmashEnd;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2);
		NEW(s); Tasks.SetStackSize(s, small); Tasks.Start(s); Pump;
		Tasks.SetStackDir(old);
		UnitAssert.True(Tasks.Failed(s), "corrupted stack: task failed")
	END TestProcCanaryCaughtAtReap;

	PROCEDURE TestProcDenseSlabGuard*;
	(* Под плотной плитой — сторожевая страница: запись ниже первого стека
	   свежей плиты — трап, а не тихая порча чужой памяти. *)
		VAR old: Tasks.StackDir; b: Below;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2);
		NEW(b); Tasks.SetStackSize(b, small); Tasks.Start(b); Pump;
		Tasks.SetStackDir(old);
		UnitAssert.True(Tasks.Failed(b), "write below the slab trapped")
	END TestProcDenseSlabGuard;

	PROCEDURE TestProcHundredThousandDense*;
	(* 100 тыс. задач со стеком 4 КБ у плотного поставщика создаются разом
	   и завершаются. *)
		CONST n = 100000;
		VAR old: Tasks.StackDir; i: INTEGER; t: Once; t0: LONGINT;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2); onceCount := 0;
		FOR i := 1 TO n DO NEW(t); Tasks.SetStackSize(t, 4096); Tasks.Start(t) END;
		UnitAssert.EqualInt(n, Tasks.Count(), "all started");
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 60000) DO Tasks.Step END;
		Tasks.SetStackDir(old);
		UnitAssert.EqualInt(n, onceCount, "all finished")
	END TestProcHundredThousandDense;

	PROCEDURE TestProcCanaryCaught*;
	(* Порча канарейки у плотного стека ловится при переключении с задачи:
	   трап 126 в ней, сосед доживает. *)
		VAR old: Tasks.StackDir; s: Smash; c: Calm; n: INTEGER;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2);
		NEW(c); Tasks.SetStackSize(c, small); Tasks.Start(c);
		NEW(s); Tasks.SetStackSize(s, small); Tasks.Start(s);
		n := Kernel.trapCount;
		Pump;
		Tasks.SetStackDir(old);
		UnitAssert.True(Tasks.Failed(s), "smashing task failed");
		UnitAssert.EqualInt(126, Kernel.err, "trap 126 stack corrupted");
		UnitAssert.EqualInt(n + 1, Kernel.trapCount, "one trap report");
		UnitAssert.EqualInt(10, c.n, "neighbour finished");
		UnitAssert.True(~Tasks.Failed(c), "neighbour not failed")
	END TestProcCanaryCaught;

	PROCEDURE TestModuleCheckedFlag*;
	(* Признак «собран с ключом %» — бит опций модуля из объектного файла. *)
	BEGIN
		UnitAssert.True(Tasks.ModuleChecked(Kernel.ThisMod("System_devStackDirs")), "this module has %");
		UnitAssert.True(~Tasks.ModuleChecked(Kernel.ThisMod("System_devStackPlain")), "plain module has no %");
		UnitAssert.True(~Tasks.ModuleChecked(Kernel.ThisMod("Tasks")), "Tasks has no %")
	END TestModuleCheckedFlag;

	PROCEDURE TestProcDenseOnlyForCheckedRun*;
	(* Плотный поставщик даёт стек только задаче, чей Run собран с ключом %;
	   остальные получают стек со сторожем (не от плотного поставщика). *)
		VAR old: Tasks.StackDir; c: Calm; p: System_devStackPlain.Plain; ps: PlainSub; po: PlainOwn;
	BEGIN
		old := Tasks.StackDirOf();
		Use(2);
		NEW(c); Tasks.Start(c);
		NEW(p); Tasks.Start(p);
		NEW(ps); Tasks.Start(ps);
		NEW(po); Tasks.Start(po);
		UnitAssert.True(Tasks.StackDirOfTask(c) = dirs[2], "checked Run: dense");
		UnitAssert.True(Tasks.StackDirOfTask(po) = dirs[2], "own checked Run over plain base: dense");
		UnitAssert.True((Tasks.StackDirOfTask(p) # NIL) & (Tasks.StackDirOfTask(p) # dirs[2]), "plain Run: guarded");
		UnitAssert.True((Tasks.StackDirOfTask(ps) # NIL) & (Tasks.StackDirOfTask(ps) # dirs[2]), "inherited plain Run: guarded");
		Pump;
		Tasks.SetStackDir(old);
		UnitAssert.EqualInt(2, p.n, "plain ran"); UnitAssert.EqualInt(2, ps.n, "sub ran"); UnitAssert.EqualInt(2, po.n, "own ran")
	END TestProcDenseOnlyForCheckedRun;

END System_devStackDirs.
