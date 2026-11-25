MODULE System_devSegStacks;
(** Проба сегментированного стека. Модуль собран с ключом % (tools/tests.txt):
	прологи его процедур проверяют границу и при нехватке места зовут
	Kernel.GrowStack. Задачи начинают со стека segBase и растут кусками
	segGrowth; переход проверяется счётчиком Kernel.GrowCount. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Tasks, UnitAssert, System_devCRaw, System_devClock,
		Console, Strings;

	CONST
		reserve = 2 * 1024;
		base = 4 * 1024;
		growth = 8 * 1024;
		deepN = 2000;
		targetFrame = 600;	(* кадр цели: > 1400 - 1024, чтобы вызов всегда рос *)
		near = 1400;		(* до какого остатка спускаемся перед вызовом цели *)

	TYPE
		Pair = RECORD a, b, c, d: INTEGER END;
		Cell = POINTER TO RECORD x: INTEGER END;

		Deep = POINTER TO RECORD (Tasks.Task) n, res: INTEGER END;
		Params = POINTER TO RECORD (Tasks.Task)
			v, varV, arr, rec, nested: INTEGER; real: REAL; loOk: BOOLEAN
		END;
		Yielder = POINTER TO RECORD (Tasks.Task) lo: S.ADDRESS; ok: BOOLEAN END;
		Gcer = POINTER TO RECORD (Tasks.Task) ok: BOOLEAN END;
		Tryer = POINTER TO RECORD (Tasks.Task) ok: BOOLEAN; err: INTEGER END;
		Canceller = POINTER TO RECORD (Tasks.Task) state: INTEGER END;
		Smasher = POINTER TO RECORD (Tasks.Task) END;
		Waiter = POINTER TO RECORD (Tasks.Task) cell: Cell; done: BOOLEAN END;
		Setter = POINTER TO RECORD (Tasks.Task) cell: Cell END;
		Hot = POINTER TO RECORD (Tasks.Task) n: INTEGER; ns, grows: LONGINT END;
		Once = POINTER TO RECORD (Tasks.Task) END;

	VAR
		oldDir: Tasks.StackDir;
		oldDefault: INTEGER;
		onceCount: INTEGER;

	PROCEDURE Begin;
	BEGIN
		oldDir := Tasks.StackDirOf(); oldDefault := Tasks.DefaultStackSize();
		Tasks.SetSegmentParams(reserve, base, growth);
		Kernel.ResetGrowCount()
	END Begin;

	PROCEDURE End;
	BEGIN
		Tasks.StopSegmentedStacks;
		Tasks.SetStackDir(oldDir);
		Tasks.SetDefaultStackSize(oldDefault)
	END End;

	PROCEDURE Pump;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 5000) DO Tasks.Step END
	END Pump;

	(* --- глубокая рекурсия -------------------------------------------------- *)

	PROCEDURE Rec (n: INTEGER): INTEGER;
		VAR a: ARRAY 16 OF INTEGER; i: INTEGER;
	BEGIN
		a[0] := n; i := 1;
		WHILE i < 16 DO a[i] := a[i - 1] + 1; INC(i) END;
		IF n = 0 THEN RETURN 0 END;
		RETURN Rec(n - 1) + a[0]
	END Rec;

	PROCEDURE (t: Deep) Run;
	BEGIN
		t.res := Rec(t.n)
	END Run;

	PROCEDURE TestProcDeepRecursion*;
	(* Рекурсия на 2000 кадров проходит через десятки кусков; сумма верна,
		задача не трапнула. *)
		VAR t: Deep;
	BEGIN
		Begin;
		NEW(t); t.n := deepN; Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "deep task failed");
		UnitAssert.EqualInt(deepN * (deepN + 1) DIV 2, t.res, "deep sum");
		UnitAssert.True(Kernel.GrowCount() > 10, "many segment crossings");
		End
	END TestProcDeepRecursion;

	(* --- параметры всех видов через границу --------------------------------- *)

	PROCEDURE TakeValue (x: INTEGER): INTEGER;
		VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
		RETURN x + 1
	END TakeValue;

	PROCEDURE TakeVar (VAR x: INTEGER);
		VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
		x := x + 7
	END TakeVar;

	PROCEDURE TakeArr (IN a: ARRAY OF INTEGER): INTEGER;
		VAR b: ARRAY 120 OF INTEGER; i, s: INTEGER;
	BEGIN
		s := 0; i := 0; WHILE i < LEN(a) DO INC(s, a[i]); INC(i) END;
		i := 0; WHILE i < 120 DO b[i] := i; INC(i) END;
		RETURN s
	END TakeArr;

	PROCEDURE TakePair (p: Pair): INTEGER;
		VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
		RETURN p.a + p.b + p.c + p.d
	END TakePair;

	PROCEDURE TakeReal (x: REAL): REAL;
		VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
		RETURN x * 2.0
	END TakeReal;

	PROCEDURE Outer (OUT res: INTEGER);
		VAR k: INTEGER;
		PROCEDURE Inner (x: INTEGER): INTEGER;
			VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
		BEGIN
			i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
			RETURN k + x + a[0]
		END Inner;
	BEGIN
		k := 5; res := Inner(3)
	END Outer;

	PROCEDURE DiveParams (t: Params; guard: INTEGER);
		VAR a: ARRAY 8 OF INTEGER; arr: ARRAY 8 OF INTEGER; p: Pair; i: INTEGER; lo0: S.ADDRESS;
	BEGIN
		a[0] := guard; i := 1;
		WHILE i < 8 DO a[i] := i; INC(i) END;
		IF (guard > 0) & (SHORT(S.ADR(a) - Kernel.stkLo) > near) THEN DiveParams(t, guard - 1)
		ELSE
			lo0 := Kernel.stkLo;
			t.v := TakeValue(5);
			IF Kernel.stkLo # lo0 THEN t.loOk := FALSE END;
			t.varV := 10; TakeVar(t.varV);
			i := 0; WHILE i < 8 DO arr[i] := i; INC(i) END;
			t.arr := TakeArr(arr);
			p.a := 1; p.b := 2; p.c := 3; p.d := 4;
			t.rec := TakePair(p);
			t.real := TakeReal(2.5);
			Outer(t.nested);
			IF Kernel.stkLo # lo0 THEN t.loOk := FALSE END
		END
	END DiveParams;

	PROCEDURE (t: Params) Run;
	BEGIN
		t.loOk := TRUE; DiveParams(t, 1000)
	END Run;

	PROCEDURE TestProcParamsAcrossBoundary*;
	(* Каждый вид параметра передаётся вызову, чей пролог переходит на новый
		кусок: значение, VAR, открытый массив, запись, REAL-результат и
		вложенная процедура со статической связью. *)
		VAR t: Params; before: LONGINT;
	BEGIN
		Begin;
		NEW(t); Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "params task failed");
		UnitAssert.EqualInt(6, t.v, "value parameter");
		UnitAssert.EqualInt(17, t.varV, "VAR parameter");
		UnitAssert.EqualInt(28, t.arr, "open array");
		UnitAssert.EqualInt(10, t.rec, "record value");
		UnitAssert.EqualReal(5.0, t.real, 0.0001, "REAL result");
		UnitAssert.EqualInt(8, t.nested, "static link");
		UnitAssert.True(t.loOk, "stkLo restored after every return");
		UnitAssert.True(Kernel.GrowCount() >= 6, "each call crossed a boundary");
		End
	END TestProcParamsAcrossBoundary;

	(* --- уступка на нижнем куске ------------------------------------------- *)

	PROCEDURE DiveYield (t: Yielder; depth: INTEGER): INTEGER;
		VAR a: ARRAY 8 OF INTEGER; marker: INTEGER;
	BEGIN
		a[0] := depth; marker := 12345;
		IF depth > 0 THEN
			RETURN DiveYield(t, depth - 1)
		ELSE
			t.lo := Kernel.stkLo;
			Tasks.Yield;
			IF (Kernel.stkLo # t.lo) OR (marker # 12345) OR (a[0] # depth) THEN t.ok := FALSE END;
			RETURN marker
		END
	END DiveYield;

	PROCEDURE (t: Yielder) Run;
		VAR x: INTEGER;
	BEGIN
		x := DiveYield(t, deepN)
	END Run;

	PROCEDURE TestProcYieldDeep*;
	(* Задача уступает, стоя на нижнем куске: после возврата Kernel.stkLo,
		локальные переменные и кадр прежние. *)
		VAR t: Yielder;
	BEGIN
		Begin;
		t := NIL; NEW(t); t.ok := TRUE; Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "yield task failed");
		UnitAssert.True(t.ok, "stkLo and locals survived the switch");
		UnitAssert.True(Kernel.GrowCount() > 0, "yield happened on a grown segment");
		End
	END TestProcYieldDeep;

	(* --- сборка мусора со ссылками только на нижних кусках ----------------- *)

	PROCEDURE GcDive (d: INTEGER; VAR ok: BOOLEAN);
		VAR mine: POINTER TO ARRAY OF INTEGER; junk: POINTER TO ARRAY OF INTEGER;
			i, k: INTEGER; a: ARRAY 8 OF INTEGER;
	BEGIN
		NEW(mine, 200); i := 0;
		WHILE i < 200 DO mine[i] := d + i; INC(i) END;
		a[0] := d;
		IF d > 0 THEN GcDive(d - 1, ok)
		ELSIF SHORT(S.ADR(a) - Kernel.stkLo) > near THEN GcDive(d, ok)
		ELSE
			Kernel.Collect();
			(* Набить освобождённые блоки мусором: собранный массив нижнего
			   куска будет испорчен, и проверка при возврате это увидит. *)
			k := 0;
			WHILE k < 200 DO
				NEW(junk, 200); i := 0;
				WHILE i < 200 DO junk[i] := -1; INC(i) END;
				INC(k)
			END
		END;
		i := 0;
		WHILE i < 200 DO
			IF mine[i] # d + i THEN ok := FALSE END;
			INC(i)
		END
	END GcDive;

	PROCEDURE (t: Gcer) Run;
	BEGIN
		GcDive(2000, t.ok)
	END Run;

	PROCEDURE TestProcCollectLowerSegments*;
	(* Живые указатели лежат только в кадрах нижних кусков: после
		Kernel.Collect и затирания освобождённых блоков массивы целы. *)
		VAR t: Gcer;
	BEGIN
		Begin;
		NEW(t); t.ok := TRUE; Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "gc task failed");
		UnitAssert.True(t.ok, "objects on lower segments survived collection");
		UnitAssert.True(Kernel.GrowCount() > 10, "recursion crossed segments");
		End
	END TestProcCollectLowerSegments;

	(* --- Kernel.Try через границу куска ------------------------------------ *)

	PROCEDURE TrapOnNewSegment (a, b, c: S.ADDRESS);
		VAR big: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO big[i] := i; INC(i) END;
		HALT(42)
	END TrapOnNewSegment;

	PROCEDURE DiveTry (t: Tryer; guard: INTEGER);
		VAR a: ARRAY 8 OF INTEGER;
	BEGIN
		a[0] := guard;
		IF (guard > 0) & (SHORT(S.ADR(a) - Kernel.stkLo) > near) THEN DiveTry(t, guard - 1)
		ELSE
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TrapOnNewSegment)), 0, 0, 0);
			t.err := Kernel.err; t.ok := TRUE
		END
	END DiveTry;

	PROCEDURE (t: Tryer) Run;
	BEGIN
		t.ok := FALSE; DiveTry(t, 1000);
		UnitAssert.True(t.ok, "Try returned to its segment")
	END Run;

	PROCEDURE TestProcTryTrapOnLowerSegment*;
	(* Точка Kernel.Try на одном куске, трап в процедуре, чей пролог ушёл на
		следующий: управление возвращается в Try, Kernel.stkLo восстановлен,
		задача продолжает работу. *)
		VAR t: Tryer; c: Deep;
	BEGIN
		Begin;
		NEW(t); Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "try task failed");
		UnitAssert.EqualInt(42, t.err, "trap number");
		UnitAssert.True(Kernel.GrowCount() > 0, "trap happened on a grown segment");
		(* Следующая задача на тех же кусках работает. *)
		NEW(c); c.n := 500; Tasks.Start(c); Pump;
		UnitAssert.True(~Tasks.Failed(c), "next task failed");
		UnitAssert.EqualInt(500 * 501 DIV 2, c.res, "next task result");
		End
	END TestProcTryTrapOnLowerSegment;

	(* --- AWAIT на нижнем куске --------------------------------------------- *)

	PROCEDURE [exclusive] (c: Cell) Wait*, NEW;
	(** Ждёт, пока x станет больше нуля. Условие AWAIT — скрытая вложенная
		процедура со статической связью на кадр Wait: Monitors читает его из
		чужой задачи, поэтому кадр не должен переезжать. *)
	BEGIN
		ACTIVE.AWAIT(c.x > 0)
	END Wait;

	PROCEDURE [exclusive] (c: Cell) Set*, NEW;
	BEGIN
		c.x := 1
	END Set;

	PROCEDURE DiveAwait (t: Waiter; depth: INTEGER);
		VAR a: ARRAY 8 OF INTEGER;
	BEGIN
		a[0] := depth;
		IF depth > 0 THEN DiveAwait(t, depth - 1)
		ELSE
			t.cell.Wait;	(* кадр Wait лежит на нижнем куске *)
			t.done := TRUE
		END
	END DiveAwait;

	PROCEDURE (t: Waiter) Run;
	BEGIN
		DiveAwait(t, deepN)
	END Run;

	PROCEDURE (t: Setter) Run;
	BEGIN
		t.cell.Set
	END Run;

	PROCEDURE TestProcAwaitDeep*;
	(* Задача ждёт в [exclusive] на нижнем куске; другая задача ставит
		условие, и Monitors вычисляет его по кадру, лежащему в куске. *)
		VAR w: Waiter; s: Setter; c: Cell;
	BEGIN
		Begin;
		NEW(c); NEW(w); w.cell := c; NEW(s); s.cell := c;
		Tasks.Start(w); Tasks.Start(s); Pump;
		UnitAssert.True(~Tasks.Failed(w), "waiter failed");
		UnitAssert.True(~Tasks.Failed(s), "setter failed");
		UnitAssert.True(w.done, "AWAIT condition read from the lower segment");
		UnitAssert.True(Kernel.GrowCount() > 0, "waited on a grown segment");
		End
	END TestProcAwaitDeep;

	(* --- отмена на нижнем куске -------------------------------------------- *)

	PROCEDURE DiveCancel (t: Canceller; depth: INTEGER);
		VAR a: ARRAY 8 OF INTEGER;
	BEGIN
		a[0] := depth;
		IF depth > 0 THEN DiveCancel(t, depth - 1)
		ELSE
			t.state := 1;
			Tasks.Park;
			t.state := 9	(* отменённая задача сюда не доходит *)
		END
	END DiveCancel;

	PROCEDURE (t: Canceller) Run;
	BEGIN
		t.state := 0; DiveCancel(t, deepN)
	END Run;

	PROCEDURE TestProcCancelDeep*;	(* Отмена задачи, запаркованной на нижнем куске: трап 127 в точке
		ожидания, задача Cancelled, куски возвращены и не портят следующие. *)
		VAR t: Canceller; c: Deep;
	BEGIN
		Begin;
		NEW(t); Tasks.Start(t); Tasks.Step;	(* дошла до Park на нижнем куске *)
		UnitAssert.EqualInt(1, t.state, "parked deep");
		UnitAssert.True(Kernel.GrowCount() > 0, "parked on a grown segment");
		Tasks.Cancel(t); Pump;
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(t), "cancelled");
		UnitAssert.EqualInt(0, Tasks.Count(), "no tasks left");
		UnitAssert.True(Kernel.SegmentPoolCount() > 0, "segments returned to the pool");
		(* Процесс жив и следующие задачи работают. *)
		NEW(c); c.n := 300; Tasks.Start(c); Pump;
		UnitAssert.True(~Tasks.Failed(c), "next task failed");
		UnitAssert.EqualInt(300 * 301 DIV 2, c.res, "next task result");
		End
	END TestProcCancelDeep;

	(* --- запасной кусок ----------------------------------------------------- *)

	PROCEDURE TestProcSpareAndRelease*;
	(* Без запасного куска возвращённые куски ждут следующего роста и
		освобождаются там; задача всё равно доходит до конца. *)
		VAR t: Deep;
	BEGIN
		Begin;
		Tasks.SegmentKeep(FALSE);
		NEW(t); t.n := 500; Tasks.Start(t); Pump;
		UnitAssert.True(~Tasks.Failed(t), "no-spare task failed");
		UnitAssert.EqualInt(500 * 501 DIV 2, t.res, "no-spare result");
		UnitAssert.True(Kernel.SegmentPendingCount() > 0, "segments pending return to OS");
		Tasks.SegmentKeep(TRUE);
		End
	END TestProcSpareAndRelease;

	PROCEDURE (t: Smasher) Run;
	(* Портит канарейку у дна стартового стека, как код без проверки,
	   ушедший ниже запаса. *)
		VAR x: INTEGER;
	BEGIN
		S.PUT(Kernel.StackOf(S.ADR(x)).lo, 0);
		Tasks.Yield;
		x := 1
	END Run;

	PROCEDURE TestProcCanarySegmented*;
	(* Стартовый стек поставщика SegDir — плотный, с канарейкой у дна:
		порча ловится в точке ожидания, задача трапует 126, соседи живы. *)
		VAR t: Smasher; c: Deep;
	BEGIN
		Begin;
		NEW(t); Tasks.Start(t); NEW(c); c.n := 100; Tasks.Start(c); Pump;
		UnitAssert.True(Tasks.Failed(t), "corrupted task failed");
		UnitAssert.EqualInt(126, Kernel.err, "canary trap");
		UnitAssert.True(~Tasks.Failed(c), "neighbour survived");
		UnitAssert.EqualInt(100 * 101 DIV 2, c.res, "neighbour result");
		End
	END TestProcCanarySegmented;

	(* --- замеры ------------------------------------------------------------ *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE PumpLong;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 120000) DO Tasks.Step END
	END PumpLong;

	PROCEDURE Empty (x: INTEGER): INTEGER;
	BEGIN
		RETURN x + 1
	END Empty;

	PROCEDURE Leaf (x: INTEGER): INTEGER;
		VAR a: ARRAY 120 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < 120 DO a[i] := i; INC(i) END;
		RETURN x + a[0] + 1
	END Leaf;

	PROCEDURE HotDive (t: Hot; guard: INTEGER);
		VAR a: ARRAY 8 OF INTEGER; i, x: INTEGER; t0: LONGINT;
	BEGIN
		a[0] := guard;
		IF (guard > 0) & (SHORT(S.ADR(a) - Kernel.stkLo) > near) THEN HotDive(t, guard - 1)
		ELSE
			Kernel.ResetGrowCount(); x := 0;
			t0 := System_devClock.now(); i := 0;
			WHILE i < t.n DO x := Leaf(x); INC(i) END;
			t.ns := System_devClock.now() - t0;
			t.grows := Kernel.GrowCount()
		END
	END HotDive;

	PROCEDURE (t: Hot) Run;
	BEGIN
		HotDive(t, 1000)
	END Run;

	PROCEDURE BenchCall*;
	(** Цена обычного проверенного вызова против вызова, каждый раз
		переходящего на новый кусок: с запасным куском и без. *)
		VAR t: Hot; i, x: INTEGER; t0, dt: LONGINT;
	BEGIN
		Begin;
		x := 0; t0 := System_devClock.now();
		i := 0; WHILE i < 10000000 DO x := Empty(x); INC(i) END;
		dt := System_devClock.now() - t0;
		W("BENCH call ops=10000000 nsPerOp="); N(dt DIV 10000000); Console.WriteLn;
		Tasks.SegmentKeep(TRUE);
		NEW(t); t.n := 1000000; Tasks.Start(t); PumpLong;
		W("BENCH boundary-spare ops="); N(t.n); W(" nsPerOp="); N(t.ns DIV t.n);
		W(" grows="); N(t.grows); Console.WriteLn;
		Tasks.SegmentKeep(FALSE);
		NEW(t); t.n := 1000000; Tasks.Start(t); PumpLong;
		W("BENCH boundary-pending ops="); N(t.n); W(" nsPerOp="); N(t.ns DIV t.n);
		W(" grows="); N(t.grows); Console.WriteLn;
		Tasks.SegmentKeep(TRUE);
		End
	END BenchCall;

	PROCEDURE (t: Once) Run;
	BEGIN
		INC(onceCount)
	END Run;

	PROCEDURE MassN (n: INTEGER);
	(* n задач со стартовым стеком base у поставщика SegDir: создание,
	   память, отображения, исполнение. *)
		VAR i: INTEGER; t: Once; t0, t1, t2: LONGINT; r0, r1, v0, v1: LONGINT;
	BEGIN
		Begin;
		onceCount := 0;
		r0 := System_devCRaw.c_rss(); v0 := System_devCRaw.c_vmas();
		t0 := System_devClock.now();
		i := 0;
		WHILE i < n DO
			NEW(t); Tasks.SetStackSize(t, base); Tasks.Start(t); INC(i)
		END;
		t1 := System_devClock.now(); r1 := System_devCRaw.c_rss(); v1 := System_devCRaw.c_vmas();
		W("BENCH segtasks n="); N(n); W(" stack="); N(base);
		W(" createMs="); N((t1 - t0) DIV 1000000); W(" nsPerTask="); N((t1 - t0) DIV n);
		W(" rssMB="); N((r1 - r0) DIV (1024 * 1024)); W(" vmas="); N(v1); Console.WriteLn;
		PumpLong; t2 := System_devClock.now();
		W("BENCH segtasks-run n="); N(n); W(" runMs="); N((t2 - t1) DIV 1000000);
		W(" done="); N(onceCount); Console.WriteLn;
		End
	END MassN;

	PROCEDURE Mass10k*;
	BEGIN MassN(10000)
	END Mass10k;

	PROCEDURE Mass100k*;
	BEGIN MassN(100000)
	END Mass100k;

	PROCEDURE Mass1m*;
	BEGIN MassN(1000000)
	END Mass1m;

	PROCEDURE TryReserve (r, b, g: INTEGER);
		VAR t: Deep; oldD: Tasks.StackDir; oldDef: INTEGER;
	BEGIN
		oldD := Tasks.StackDirOf(); oldDef := Tasks.DefaultStackSize();
		Tasks.SetSegmentParams(r, b, g); Kernel.ResetGrowCount();
		NEW(t); t.n := deepN; Tasks.Start(t); PumpLong;
		W("RESERVE reserve="); N(r); W(" base="); N(b);
		W(" failed="); IF Tasks.Failed(t) THEN W("yes") ELSE W("no") END;
		W(" res="); N(t.res); W(" grows="); N(Kernel.GrowCount()); Console.WriteLn;
		Tasks.StopSegmentedStacks; Tasks.SetStackDir(oldD); Tasks.SetDefaultStackSize(oldDef)
	END TryReserve;

	PROCEDURE Reserve1k*;
	BEGIN TryReserve(1024, 3072, 4096)
	END Reserve1k;

	PROCEDURE Reserve1536*;
	BEGIN TryReserve(1536, 3584, 4096)
	END Reserve1536;

	PROCEDURE Reserve2k*;
	BEGIN TryReserve(2048, 4096, 4096)
	END Reserve2k;

	PROCEDURE Reserve768*;
	BEGIN TryReserve(768, 2816, 4096)
	END Reserve768;

	PROCEDURE Reserve512*;
	BEGIN TryReserve(512, 2560, 4096)
	END Reserve512;

	PROCEDURE Reserve256*;
	BEGIN TryReserve(256, 2304, 4096)
	END Reserve256;

END System_devSegStacks.
