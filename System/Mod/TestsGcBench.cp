MODULE SystemTestsGcBench;
(** Замер сборщика (этап 1 плана: «Замер до этапа 2»).

	Строит связный список мелких объектов (запись из 1–2 полей) и меряет:
	  - время NEW на 1 и 10 млн;
	  - время полного Kernel.Collect при 1, 10 и 50 млн ЖИВЫХ объектов;
	  - время Collect при 1 и 10 млн МЁРТВЫХ объектов;
	  - размер кучи (Kernel.Allocated);
	  - периодическую сборку из главного цикла (System/Mod/Loop: gcCycle = 100)
	    при большой живой куче в покое и на пустой куче.

	Во время накопления объектов ставится Kernel.blockCollect := TRUE, иначе
	NewBlock при исчерпании кластера запускает полную сборку и построение
	50 млн объектов превращается в O(n^2). Время NEW поэтому измеряется без
	автоматических сборок — это и есть «стоимость NEW» при достаточной куче.

	В манифест tools/tests.txt НЕ входит: слишком долго. Запуск —
	tools/bench-gc.sh (он же собирает RSS через /usr/bin/time -v).

	Вывод: строки "BENCH ..." (стабильные метки) и маркер SYSTEMTESTS-PASS/FAIL. *)

	IMPORT Console, Strings, Kernel, Loop, SystemTestsSupport, SystemTestsClock;

	CONST
		m1 = 1000000; m10 = 10000000; m50 = 50000000;
		loopIters = 500;

	TYPE
		Node = POINTER TO RECORD
			next: Node;
			val: INTEGER
		END;

	VAR
		head: Node;		(* корень: global, поэтому MarkGlobals его видит *)
		live: LONGINT;		(* сколько объектов сейчас в списке *)
		collects, fasts, iters: INTEGER;

	PROCEDURE Now (): LONGINT;
	BEGIN
		RETURN SystemTestsClock.now()
	END Now;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(s)
	END N;

	PROCEDURE Rec (IN tag: ARRAY OF CHAR; n, ns, alloc: LONGINT);
	BEGIN
		W("BENCH "); W(tag);
		W(" n="); N(n);
		W(" ns="); N(ns);
		W(" us="); N(ns DIV 1000);
		W(" alloc="); N(alloc);
		Console.WriteLn
	END Rec;

	(* --- построение ------------------------------------------------------- *)

	PROCEDURE Build (n: LONGINT);
		VAR i: LONGINT; p: Node;
	BEGIN
		i := 0;
		WHILE i < n DO
			NEW(p);
			IF p = NIL THEN
				W("BENCH alloc-failed live="); N(live); Console.WriteLn;
				RETURN
			END;
			p.val := SHORT(i MOD 1000000);
			p.next := head;
			head := p;
			INC(i)
		END;
		INC(live, n)
	END Build;

	PROCEDURE Drop;
	BEGIN
		head := NIL; live := 0
	END Drop;

	PROCEDURE SetBlock (on: BOOLEAN);
	BEGIN
		Kernel.blockCollect := on
	END SetBlock;

	(* --- сборка ----------------------------------------------------------- *)

	PROCEDURE TimeCollect (IN tag: ARRAY OF CHAR);
		VAR t0, t1, ns: LONGINT;
	BEGIN
		SetBlock(FALSE);
		t0 := Now(); Kernel.Collect; t1 := Now();
		ns := t1 - t0;
		Rec(tag, live, ns, Kernel.Allocated())
	END TimeCollect;

	(* --- главный цикл ----------------------------------------------------- *)

	PROCEDURE Noop;
	BEGIN
	END Noop;

	PROCEDURE Guard (): BOOLEAN;
	BEGIN
		INC(iters);
		RETURN iters >= loopIters
	END Guard;

	PROCEDURE Watch (event: INTEGER);
	BEGIN
		IF event = 1 THEN INC(collects)
		ELSIF event = 2 THEN INC(fasts)
		END
	END Watch;

	PROCEDURE LoopBench (IN tag: ARRAY OF CHAR);
		VAR q, t0, t1, ns, a0, a1, gcChecks, gcSkips: LONGINT;
	BEGIN
		collects := 0; fasts := 0; iters := 0;
		q := Loop.quantum; Loop.quantum := 0;
		Kernel.debug := TRUE; Kernel.watcher := Watch;
		a0 := Kernel.AllocCount();
		t0 := Now();
		Loop.Start(Noop, NIL, Guard);
		t1 := Now();
		a1 := Kernel.AllocCount();
		Loop.Stats(gcChecks, gcSkips);
		Kernel.watcher := NIL; Kernel.debug := FALSE; Loop.quantum := q;
		ns := t1 - t0;
		W("BENCH "); W(tag);
		W(" n="); N(live);
		W(" iters="); N(iters);
		W(" collects="); N(collects);
		W(" fast="); N(fasts);
		W(" allocs="); N(a1 - a0);
		W(" checks="); N(gcChecks); W(" skips="); N(gcSkips);
		W(" ns="); N(ns);
		W(" perIterUs=");
		IF iters > 0 THEN N(ns DIV 1000 DIV iters) ELSE N(0) END;
		Console.WriteLn
	END LoopBench;

	(* --- сценарий --------------------------------------------------------- *)

	PROCEDURE Do*;
		VAR t0, t1, ns: LONGINT;
	BEGIN
		SystemTestsSupport.Begin("GcBench");
		W("BENCH heap-start alloc="); N(Kernel.Allocated()); Console.WriteLn;

		head := NIL; live := 0;

		(* 1 млн живых *)
		SetBlock(TRUE); t0 := Now(); Build(m1); t1 := Now();
		Rec("new1m", m1, t1 - t0, Kernel.Allocated());
		TimeCollect("live1m");

		(* +9 млн => NEW на 10 млн *)
		SetBlock(TRUE); t0 := Now(); Build(m10 - m1); t1 := Now();
		Rec("new9m", m10 - m1, t1 - t0, Kernel.Allocated());
		TimeCollect("live10m");

		(* периодическая сборка главного цикла при 10 млн живых *)
		LoopBench("loop10m");

		(* +40 млн => 50 млн живых *)
		SetBlock(TRUE); t0 := Now(); Build(m50 - m10); t1 := Now();
		Rec("new40m", m50 - m10, t1 - t0, Kernel.Allocated());
		TimeCollect("live50m");

		(* освободить всё *)
		Drop; TimeCollect("drop50m");

		(* покой на пустой куче (базовая стоимость цикла) *)
		LoopBench("loopEmpty");

		(* 1 млн мёртвых: построить, снять корень, собрать *)
		SetBlock(TRUE); Build(m1); Drop;
		TimeCollect("dead1m");

		(* 10 млн мёртвых *)
		SetBlock(TRUE); Build(m10); Drop;
		TimeCollect("dead10m");

		W("BENCH heap-end alloc="); N(Kernel.Allocated()); Console.WriteLn;

		SystemTestsSupport.End
	END Do;

	(** То же без 50 млн: для быстрого замера на слабой машине. *)
	PROCEDURE DoSmall*;
		VAR t0, t1: LONGINT;
	BEGIN
		SystemTestsSupport.Begin("GcBenchSmall");
		W("BENCH heap-start alloc="); N(Kernel.Allocated()); Console.WriteLn;

		head := NIL; live := 0;
		SetBlock(TRUE); t0 := Now(); Build(m1); t1 := Now();
		Rec("new1m", m1, t1 - t0, Kernel.Allocated());
		TimeCollect("live1m");
		SetBlock(TRUE); t0 := Now(); Build(m10 - m1); t1 := Now();
		Rec("new9m", m10 - m1, t1 - t0, Kernel.Allocated());
		TimeCollect("live10m");
		LoopBench("loop10m");
		Drop; TimeCollect("drop10m");
		LoopBench("loopEmpty");
		SetBlock(TRUE); Build(m1); Drop; TimeCollect("dead1m");
		SetBlock(TRUE); Build(m10); Drop; TimeCollect("dead10m");
		W("BENCH heap-end alloc="); N(Kernel.Allocated()); Console.WriteLn;
		SystemTestsSupport.End
	END DoSmall;

END SystemTestsGcBench.
