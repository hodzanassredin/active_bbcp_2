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
		PlainSub = POINTER TO RECORD (System_devStackPlain.Plain) END;	(* Run унаследован: без ключа *)
		PlainOwn = POINTER TO RECORD (System_devStackPlain.Plain) END;	(* свой Run: с ключом *)

	VAR
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
		Tasks.Step;	(* уборка: прежние поставщики опустошены *)
		UnitAssert.EqualInt(0, Tasks.Count(), "no tasks")
	END TestProcSwitchDirWithLiveTasks;

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
