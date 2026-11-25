MODULE System_devStackDirs;
(** Тесты поставщиков стеков Tasks.StackDir: стек со сторожем (NewGuardDir)
	и плиты со сторожем под каждым стеком (NewSlabDir). Плотного поставщика
	без сторожей в основной ветке нет.

	Переполнение проверяется так: задача-сосед заливает низ своего стека
	шаблоном и паркуется, задача с большим стеком (её стек выдан раньше и
	лежит выше) уходит в рекурсию до сторожевой страницы и получает трап.
	После трапа шаблон соседа сверяется слово в слово, а сам сосед
	разбуживается и должен дожить до конца. *)

	IMPORT S := SYSTEM, Kernel, Tasks, UnitAssert;

	CONST
		small = 16 * 1024;
		pat = 12345678H;	(* не адрес кучи: шаблон в низу стека соседа *)
		patBytes = 4096;
		word = 8;

	TYPE
		Calm = POINTER TO RECORD (Tasks.Task) n: INTEGER; lo, hi: S.ADDRESS END;
		Filler = POINTER TO RECORD (Tasks.Task) lo: S.ADDRESS; ok, done: BOOLEAN END;
		Deep = POINTER TO RECORD (Tasks.Task) END;
		Halter = POINTER TO RECORD (Tasks.Task) lo: S.ADDRESS END;
		Holder = POINTER TO RECORD (Tasks.Task) data: POINTER TO ARRAY OF INTEGER; ok: BOOLEAN END;

	VAR
		dirs: ARRAY 2 OF Tasks.StackDir;
		names: ARRAY 2 OF ARRAY 16 OF CHAR;

	PROCEDURE (t: Calm) Run;
		VAR i: INTEGER; st: Kernel.Stack;
	BEGIN
		st := Kernel.StackOf(S.ADR(i)); t.lo := st.lo; t.hi := st.hi;
		FOR i := 1 TO 10 DO Tasks.Yield END;
		t.n := 10
	END Run;

	PROCEDURE Fill (lo: S.ADDRESS);
		VAR a: S.ADDRESS; v: INTEGER;
	BEGIN
		v := pat; a := lo;
		WHILE a < lo + patBytes DO S.PUT(a, v); INC(a, word) END
	END Fill;

	PROCEDURE Intact (lo: S.ADDRESS): BOOLEAN;
		VAR a: S.ADDRESS; v: INTEGER; ok: BOOLEAN;
	BEGIN
		ok := TRUE; a := lo;
		WHILE a < lo + patBytes DO
			S.GET(a, v); IF v # pat THEN ok := FALSE END; INC(a, word)
		END;
		RETURN ok
	END Intact;

	PROCEDURE (t: Filler) Run;
		VAR x: INTEGER;
	BEGIN
		t.lo := Kernel.StackOf(S.ADR(x)).lo;
		Fill(t.lo);
		Tasks.ParkExternal;
		t.ok := Intact(t.lo);
		t.done := TRUE
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
		Tasks.Yield;	(* сосед успевает залить шаблон и запарковаться *)
		x := Rec(100000)
	END Run;

	PROCEDURE (t: Halter) Run;
		VAR x: INTEGER;
	BEGIN
		t.lo := Kernel.StackOf(S.ADR(x)).lo;
		Tasks.Yield;
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
		END;
		Tasks.SetStackDir(dirs[k])
	END Use;

	PROCEDURE TestProcEachDirRuns*;
	(* Задачи исполняются; стек выдан выбранным поставщиком; следующая задача
	   того же размера берёт стек из пула (тот же адрес). *)
		VAR k: INTEGER; old: Tasks.StackDir; a, b: Calm; lo: S.ADDRESS;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO 1 DO
			Use(k);
			NEW(a); Tasks.SetStackSize(a, small); Tasks.Start(a);
			UnitAssert.True(Tasks.StackDirOfTask(a) = dirs[k], names[k]);
			Pump; lo := a.lo;
			UnitAssert.EqualInt(10, a.n, names[k]);
			UnitAssert.True(~Tasks.Failed(a), names[k]);
			NEW(b); Tasks.SetStackSize(b, small); Tasks.Start(b); Pump;
			UnitAssert.EqualInt(10, b.n, names[k]);
			UnitAssert.True(b.lo = lo, "stack from the pool");
			UnitAssert.True(b.hi - b.lo >= small, "stack not smaller than asked")
		END;
		Tasks.SetStackDir(old)
	END TestProcEachDirRuns;

	PROCEDURE TestProcSwitchDirWithLiveTasks*;
	(* Смена поставщика при живых задачах: задача возвращает стек своему
	   поставщику, а не текущему. *)
		CONST n = 4;
		VAR k, i: INTEGER; old: Tasks.StackDir; a: ARRAY 2 * n OF Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO 1 DO
			Use(k);
			FOR i := 0 TO n - 1 DO
				NEW(a[k * n + i]); Tasks.SetStackSize(a[k * n + i], small);
				Tasks.Start(a[k * n + i]);
				UnitAssert.True(Tasks.StackDirOfTask(a[k * n + i]) = dirs[k], names[k])
			END;
			Tasks.Step
		END;
		Tasks.SetStackDir(old);
		Pump;
		FOR i := 0 TO 2 * n - 1 DO
			UnitAssert.EqualInt(10, a[i].n, "task finished");
			UnitAssert.True(~Tasks.Failed(a[i]), "task not failed")
		END
	END TestProcSwitchDirWithLiveTasks;

	PROCEDURE TestProcOverflowNeighbourIntact*;
	(* Переполнение в плите: трап в своей задаче, шаблон соседа цел, сосед
	   доживает. Стек Deep выдан раньше и лежит выше стека Filler; между ними
	   сторожевая страница Deep. *)
		VAR k: INTEGER; old: Tasks.StackDir; d: Deep; f: Filler;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO 1 DO
			Use(k);
			NEW(d); Tasks.SetStackSize(d, small); Tasks.Start(d);
			NEW(f); Tasks.SetStackSize(f, small); Tasks.Start(f);
			Tasks.Step;	(* Deep уступает, Filler заливает и паркуется, Deep трапует *)
			UnitAssert.True(Tasks.Failed(d), names[k]);
			UnitAssert.True(~Tasks.Failed(f), names[k]);
			UnitAssert.True(Intact(f.lo), "neighbour stack pattern intact");
			UnitAssert.True(Tasks.StackDirOfTask(f) = dirs[k], "stack belongs to the provider");
			Tasks.Unpark(f); Pump;
			UnitAssert.True(f.done & f.ok, "neighbour finished and saw its pattern")
		END;
		Tasks.SetStackDir(old)
	END TestProcOverflowNeighbourIntact;

	PROCEDURE TestProcPoolStackAfterTrap*;
	(* Трап в задаче; её стек из пула получает следующая, и та работает. *)
		VAR k: INTEGER; old: Tasks.StackDir; h: Halter; c: Calm;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO 1 DO
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
		CONST n = 12;
		VAR k, i, j: INTEGER; old: Tasks.StackDir; h: ARRAY n OF Holder;
	BEGIN
		old := Tasks.StackDirOf();
		FOR k := 0 TO 1 DO
			Use(k);
			FOR i := 0 TO n - 1 DO NEW(h[i]); Tasks.SetStackSize(h[i], small); Tasks.Start(h[i]) END;
			FOR j := 1 TO 20 DO Tasks.Step; Kernel.Collect END;
			Pump;
			FOR i := 0 TO n - 1 DO UnitAssert.True(h[i].ok & ~Tasks.Failed(h[i]), names[k]) END
		END;
		Tasks.SetStackDir(old)
	END TestProcCollectWithLiveTasks;

	PROCEDURE TestProcPoolReuse*;
	(* Пул поставщика пополняется: после исполнения задач PoolCount > 0,
	   а взятая из пула задача не просит память у ядра. *)
		VAR old: Tasks.StackDir; a, b: Calm;
	BEGIN
		old := Tasks.StackDirOf();
		Use(1);
		NEW(a); Tasks.SetStackSize(a, small); Tasks.Start(a); Pump;
		UnitAssert.True(Tasks.PoolCount() > 0, "pool filled");
		NEW(b); Tasks.SetStackSize(b, small); Tasks.Start(b);
		UnitAssert.True(Tasks.PoolCount() = 0, "the task took the pooled stack");
		Pump;
		UnitAssert.True(b.lo = a.lo, "pool gave the same stack");
		Tasks.SetStackDir(old)
	END TestProcPoolReuse;

END System_devStackDirs.
