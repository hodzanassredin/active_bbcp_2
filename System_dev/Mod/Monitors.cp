MODULE System_devMonitors;
(** Тесты мониторов этапа 4 (Kernel.Lock/Unlock/Wait/Await).

	Монитор — побочная запись ядра по адресу объекта. Все сценарии,
	кроме проверок отказов, идут в задачах Tasks: ожидание вне задачи
	и без планировщика — трап. Нагрузка — ограниченный буфер на ручных
	Lock/Wait/Unlock; Await проверяется с модульной процедурой условия
	(кадр не используется — компиляторную схему с вложенной процедурой
	проверит этап 6). *)

	IMPORT S := SYSTEM, Kernel, Monitors, Tasks, UnitAssert, Console, Strings;

	CONST
		items = 200;	(* сколько всего произведено в нагрузочном буфере *)
		cap = 4;		(* ёмкость буфера *)

	TYPE
		Box = POINTER TO RECORD n: INTEGER END;
		Cell = POINTER TO RECORD x: INTEGER END;

		Prod = POINTER TO RECORD (Tasks.Task) END;
		Cons = POINTER TO RECORD (Tasks.Task) END;
		Setter = POINTER TO RECORD (Tasks.Task) END;
		Waiter = POINTER TO RECORD (Tasks.Task) END;
		TrapSetter = POINTER TO RECORD (Tasks.Task) END;
		TrapWaiter = POINTER TO RECORD (Tasks.Task) END;
		DTask = POINTER TO RECORD (Tasks.Task) id: INTEGER END;

	VAR
		mon, mon2: Box;		(* объекты-мониторы *)
		buf: ARRAY cap OF INTEGER;
		head, tail, count, produced, consumed: INTEGER;

		shared, condTrap: BOOLEAN;	(* условия Await *)
		outerStill: BOOLEAN;
		gcAdr: LONGINT;
		ma, mb: Box;		(* мониторы детектора тупика *)

	(* --- вспомогательное -------------------------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE NilCrash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END NilCrash;

	(* --- отказы ----------------------------------------------------------- *)

	PROCEDURE UnlockFree;
	BEGIN
		Kernel.Unlock(mon2)
	END UnlockFree;

	PROCEDURE WaitOutside;
	BEGIN
		Kernel.Wait(mon2)
	END WaitOutside;

	PROCEDURE AwaitOutside;
	BEGIN
		Kernel.Await(NIL, 0, mon2)
	END AwaitOutside;

	PROCEDURE TestProcRecursiveLock*;
	BEGIN
		NEW(mon2);
		Kernel.Lock(mon2);
		UnitAssert.True(Monitors.IsLocked(mon2), "first entry");
		Kernel.Lock(mon2);
		Kernel.Unlock(mon2);
		UnitAssert.True(Monitors.IsLocked(mon2), "still locked after one unlock");
		Kernel.Unlock(mon2);
		UnitAssert.True(~Monitors.IsLocked(mon2), "free after two unlocks")
	END TestProcRecursiveLock;

	PROCEDURE TestProcUnlockErrors*;
	BEGIN
		NEW(mon2);
		UnitAssert.Traps(UnlockFree, "unlock of a free monitor must trap");
		Kernel.Lock(mon2); Kernel.Unlock(mon2);
		UnitAssert.Traps(UnlockFree, "second unlock must trap");
		UnitAssert.True(~Monitors.IsLocked(mon2), "monitor is free")
	END TestProcUnlockErrors;

	PROCEDURE TestProcWaitOutsideTask*;
	BEGIN
		NEW(mon2);
		UnitAssert.Traps(WaitOutside, "Wait outside a task must trap");
		UnitAssert.Traps(AwaitOutside, "Await outside a task must trap");
		UnitAssert.True(~Monitors.IsLocked(mon2), "nothing stayed locked")
	END TestProcWaitOutsideTask;

	(* --- снятие при трапе ------------------------------------------------ *)

	PROCEDURE TakeAndTrap (a, b, c: S.ADDRESS);
	BEGIN
		Kernel.Lock(mon);
		NilCrash
	END TakeAndTrap;

	PROCEDURE InnerLocked (a, b, c: S.ADDRESS);
	BEGIN
		Kernel.Lock(mon);
		NilCrash
	END InnerLocked;

	PROCEDURE OuterLocked (a, b, c: S.ADDRESS);
	BEGIN
		Kernel.Lock(mon);
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(InnerLocked)), 0, 0, 0);
		outerStill := Monitors.IsLocked(mon);	(* внешний вход пережил трап *)
		Kernel.Unlock(mon)
	END OuterLocked;

	PROCEDURE TestProcTrapReleases*;
	BEGIN
		NEW(mon);
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TakeAndTrap)), 0, 0, 0);
		UnitAssert.True(~Monitors.IsLocked(mon), "trap released the monitor");

		outerStill := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(OuterLocked)), 0, 0, 0);
		UnitAssert.True(outerStill, "nested Try kept the outer entry");
		UnitAssert.True(~Monitors.IsLocked(mon), "outer Unlock freed the monitor")
	END TestProcTrapReleases;

	(* --- ограниченный буфер на ручных Lock/Wait/Unlock ------------------- *)

	PROCEDURE (p: Prod) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO items DIV 2 DO
			Kernel.Lock(mon);
			WHILE count = cap DO Kernel.Wait(mon) END;
			buf[tail] := i; tail := (tail + 1) MOD cap; INC(count); INC(produced);
			Kernel.Unlock(mon)
		END
	END Run;

	PROCEDURE (c: Cons) Run;
		VAR x: INTEGER;
	BEGIN
		LOOP
			Kernel.Lock(mon);
			WHILE (count = 0) & (produced < items) DO Kernel.Wait(mon) END;
			IF count > 0 THEN
				x := buf[head]; head := (head + 1) MOD cap; DEC(count); INC(consumed);
				Kernel.Unlock(mon)
			ELSE
				Kernel.Unlock(mon); EXIT
			END
		END
	END Run;

	PROCEDURE TestProcManualBuffer*;
		VAR p1, p2: Prod; c1, c2: Cons;
	BEGIN
		NEW(mon); NEW(mon2);
		head := 0; tail := 0; count := 0; produced := 0; consumed := 0;
		NEW(p1); NEW(p2); NEW(c1); NEW(c2);
		Tasks.Start(c1); Tasks.Start(c2); Tasks.Start(p1); Tasks.Start(p2);
		UnitAssert.True(Pump(15000), "producers and consumers finished");
		UnitAssert.EqualInt(items, produced, "all items produced");
		UnitAssert.EqualInt(items, consumed, "all items consumed");
		UnitAssert.True(count <= cap, "buffer stayed within capacity");
		UnitAssert.EqualInt(0, Monitors.WaitersCount(), "no stale monitor waiters");
		UnitAssert.True(~Tasks.Failed(p1) & ~Tasks.Failed(p2), "producers did not trap");
		UnitAssert.True(~Tasks.Failed(c1) & ~Tasks.Failed(c2), "consumers did not trap")
	END TestProcManualBuffer;

	(* --- Await по модульной процедуре условия ---------------------------- *)

	PROCEDURE Cond (frame: S.ADDRESS): BOOLEAN;
	BEGIN
		RETURN shared
	END Cond;

	PROCEDURE TrapCond (frame: S.ADDRESS): BOOLEAN;
	BEGIN
		IF condTrap THEN NilCrash END;
		RETURN shared
	END TrapCond;

	PROCEDURE (w: Waiter) Run;
	BEGIN
		Kernel.Lock(mon);
		WHILE ~shared DO Kernel.Await(Cond, 0, mon) END;
		Kernel.Unlock(mon)
	END Run;

	PROCEDURE (s: Setter) Run;
	BEGIN
		Kernel.Lock(mon);
		shared := TRUE;
		Kernel.Unlock(mon)
	END Run;

	PROCEDURE (w: TrapWaiter) Run;
	BEGIN
		Kernel.Lock(mon);
		WHILE ~shared DO Kernel.Await(TrapCond, 0, mon) END;
		Kernel.Unlock(mon)
	END Run;

	PROCEDURE (s: TrapSetter) Run;
	BEGIN
		Kernel.Lock(mon);
		condTrap := TRUE;	(* условие ждущего трапнет в Unlock *)
		Kernel.Unlock(mon)
	END Run;

	PROCEDURE TestProcAwaitCondition*;
		VAR w: Waiter; s: Setter; n: INTEGER;
	BEGIN
		NEW(mon); shared := FALSE;
		NEW(w); NEW(s); Tasks.Start(w);
		n := 0;
		WHILE (Monitors.WaitersCount() = 0) & (Tasks.Count() > 0) & (n < 100) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Monitors.WaitersCount() > 0, "waiter registered its condition");
		Tasks.Start(s);
		UnitAssert.True(Pump(5000), "waiter and setter finished");
		UnitAssert.True(shared, "condition became true");
		UnitAssert.True(~Tasks.Failed(w) & ~Tasks.Failed(s), "no traps");
		UnitAssert.True(~Monitors.IsLocked(mon), "monitor free at the end");
		UnitAssert.EqualInt(0, Monitors.WaitersCount(), "wait queues empty")
	END TestProcAwaitCondition;

	PROCEDURE TestProcConditionTrapForwarded*;
		VAR w: TrapWaiter; s: TrapSetter; n: INTEGER;
	BEGIN
		NEW(mon); shared := FALSE; condTrap := FALSE;
		NEW(w); NEW(s);
		Tasks.Start(w);
		n := 0;
		WHILE (Monitors.WaitersCount() = 0) & (Tasks.Count() > 0) & (n < 100) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Monitors.WaitersCount() > 0, "waiter with TrapCond is asleep");
		Tasks.Start(s);
		UnitAssert.True(Pump(5000), "tasks finished after the condition trap");
		(* трап в условии случился в Unlock задачи-сеттера и был
		   переадресован ждущей: она failed, сеттер — нет *)
		UnitAssert.True(Tasks.Failed(w), "trap was forwarded to the waiter");
		UnitAssert.True(~Tasks.Failed(s), "setter survived the condition trap");
		UnitAssert.True(~Monitors.IsLocked(mon), "monitor released after a condition trap");
		UnitAssert.EqualInt(0, Monitors.WaitersCount(), "wait queues cleaned")
	END TestProcConditionTrapForwarded;

	(* --- тупик ------------------------------------------------------------ *)

	PROCEDURE (t: DTask) Run;
	BEGIN
		IF t.id = 1 THEN
			Kernel.Lock(ma); Tasks.Yield; Kernel.Lock(mb);
			Kernel.Unlock(mb); Kernel.Unlock(ma)
		ELSE
			Kernel.Lock(mb); Tasks.Yield; Kernel.Lock(ma);
			Kernel.Unlock(ma); Kernel.Unlock(mb)
		END
	END Run;

	PROCEDURE TestProcDeadlock*;
		VAR t1, t2: DTask; i: INTEGER;
	BEGIN
		NEW(ma); NEW(mb);
		NEW(t1); t1.id := 1; NEW(t2); t2.id := 2;
		Tasks.Start(t1); Tasks.Start(t2);
		i := 0;
		WHILE ~Tasks.Deadlocked() & (i < 50) DO Tasks.Step; INC(i) END;
		UnitAssert.True(Tasks.Deadlocked(), "deadlock was detected");
		UnitAssert.True(Monitors.WaitersCount() >= 2, "both tasks wait for a monitor");
		Tasks.Cancel(t1); Tasks.Cancel(t2);
		UnitAssert.True(Pump(3000), "cancelled tasks finished");
		UnitAssert.EqualInt(0, Monitors.WaitersCount(), "wait queues cleaned after cancel");
		UnitAssert.True(~Monitors.IsLocked(ma) & ~Monitors.IsLocked(mb), "monitors released")
	END TestProcDeadlock;

	(* --- сборщик держит объект под монитором ----------------------------- *)

	PROCEDURE MakeHeld;
		VAR c: Cell;
	BEGIN
		NEW(c); c.x := 42;
		Kernel.Lock(c);
		gcAdr := S.VAL(LONGINT, c)
	END MakeHeld;

	PROCEDURE Scrub (n: INTEGER);
		VAR a: ARRAY 512 OF LONGINT; i: INTEGER;
	BEGIN
		FOR i := 0 TO LEN(a) - 1 DO a[i] := n END;
		IF n > 0 THEN Scrub(n - 1) END
	END Scrub;

	PROCEDURE TestProcMonitorKeepsObject*;
		VAR c: Cell;
	BEGIN
		MakeHeld;	(* единственная ссылка — запись монитора *)
		Scrub(32);	(* затереть след указателя на стеке *)
		Kernel.Collect;
		UnitAssert.True(Monitors.IsLocked(S.VAL(ANYPTR, gcAdr)),
			"monitor kept the object alive");
		c := S.VAL(Cell, gcAdr);
		UnitAssert.EqualInt(42, c.x, "object content intact");
		Kernel.Unlock(S.VAL(ANYPTR, gcAdr));
		UnitAssert.True(~Monitors.IsLocked(S.VAL(ANYPTR, gcAdr)), "released")
	END TestProcMonitorKeepsObject;

END System_devMonitors.
