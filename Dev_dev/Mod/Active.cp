MODULE Dev_devActive;
(** Тесты языкового расширения: [exclusive] и ACTIVE.AWAIT.

	Проверяется то, что не покрывает System_devMonitors (ручные Lock/Await):
	-AWAIT в методе и в процедуре модуля (монитор модуля), условие
	 читает поле получателя и локальную переменную через кадр ждущего;
	- условие перевычисляется уходящим: ложное значение не будит;
	- RETURN из середины и результаты всех видов в [exclusive]-функциях;
	- трап внутри [exclusive] снимает монитор, в том числе во вложенном
	 Kernel.Try. *)

	IMPORT ACTIVE, S := SYSTEM, Kernel, Monitors, Tasks, UnitAssert;

	CONST
		items = 50;

	TYPE
		Box = POINTER TO RECORD v: INTEGER END;
		Cell = POINTER TO RECORD x: INTEGER END;

		Waiter = POINTER TO RECORD (Tasks.Task) b: Box; x: INTEGER; done: BOOLEAN END;
		Setter = POINTER TO RECORD (Tasks.Task) b: Box; x: INTEGER END;
		GSetter = POINTER TO RECORD (Tasks.Task) x: INTEGER END;
		GWaiter = POINTER TO RECORD (Tasks.Task) x: INTEGER; done: BOOLEAN END;
		LocalWaiter = POINTER TO RECORD (Tasks.Task) x: INTEGER; done: BOOLEAN END;

		Chewer = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

		Mixed = POINTER TO RECORD r: REAL; l, k: LONGINT END;
		MixedWaiter = POINTER TO RECORD (Tasks.Task) m: Mixed; done: BOOLEAN END;
		MixedSetter = POINTER TO RECORD (Tasks.Task) m: Mixed END;

	VAR
		g: INTEGER;
		tb, gb: Box;
		outerHeld: BOOLEAN;
		chews: INTEGER;

	(* --- вспомогательное -------------------------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Waiters (): INTEGER;
	BEGIN
		RETURN Monitors.WaitersCount()
	END Waiters;

	(* --- [exclusive] методы и процедуры модуля --------------------------- *)

	PROCEDURE [exclusive] (b: Box) Set (x: INTEGER), NEW;
	BEGIN b.v := x
	END Set;

	PROCEDURE [exclusive] (b: Box) Wait (x: INTEGER), NEW;
	BEGIN ACTIVE.AWAIT(b.v = x)
	END Wait;

	PROCEDURE [exclusive] SetG* (x: INTEGER);
	BEGIN g := x
	END SetG;

	PROCEDURE [exclusive] WaitG (x: INTEGER);
	BEGIN ACTIVE.AWAIT(g = x)
	END WaitG;

	PROCEDURE [exclusive] WaitLocal (x: INTEGER);
		VAR y: INTEGER;
	BEGIN y := x; ACTIVE.AWAIT(y = g)
	END WaitLocal;

	(* --- результаты и RETURN из середины --------------------------------- *)

	PROCEDURE [exclusive] (b: Box) Mid (x: INTEGER): INTEGER, NEW;
	BEGIN
		IF x > 0 THEN RETURN 11 END;
		RETURN 22
	END Mid;

	PROCEDURE [exclusive] (b: Box) Real* (): REAL, NEW;
	BEGIN RETURN 2.5
	END Real;

	PROCEDURE [exclusive] (b: Box) Ptr (c: Cell): Cell, NEW;
	BEGIN RETURN S.VAL(Cell, c)
	END Ptr;

	PROCEDURE [exclusive] (b: Box) Big (): LONGINT, NEW;
	BEGIN RETURN 1234567890123
	END Big;

	(* --- трап внутри [exclusive] ----------------------------------------- *)

	PROCEDURE NilCrash;
		VAR p: Box;
	BEGIN p := NIL; p.v := 1
	END NilCrash;

	PROCEDURE [exclusive] (b: Box) Trap (x: INTEGER), NEW;
	BEGIN NilCrash
	END Trap;

	PROCEDURE TrapCall;
	BEGIN tb.Trap(1)
	END TrapCall;

	PROCEDURE [exclusive] (b: Box) InnerTrap (x: INTEGER), NEW;
	BEGIN NilCrash
	END InnerTrap;

	PROCEDURE InnerCall;
	BEGIN gb.InnerTrap(1)
	END InnerCall;

	PROCEDURE [exclusive] (b: Box) Outer (x: INTEGER), NEW;
	BEGIN
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(InnerCall)), 0, 0, 0);
		outerHeld := Monitors.IsLocked(b)
	END Outer;

	PROCEDURE OuterCall;
	BEGIN tb.Outer(1)
	END OuterCall;

	(* --- задачи ----------------------------------------------------------- *)

	PROCEDURE (w: Waiter) Run;
	BEGIN w.b.Wait(w.x); w.done := TRUE
	END Run;

	PROCEDURE (s: Setter) Run;
	BEGIN s.b.Set(s.x)
	END Run;

	PROCEDURE (w: GWaiter) Run;
	BEGIN WaitG(w.x); w.done := TRUE
	END Run;

	PROCEDURE (s: GSetter) Run;
	BEGIN SetG(s.x)
	END Run;

	PROCEDURE (w: LocalWaiter) Run;
	BEGIN WaitLocal(w.x); w.done := TRUE
	END Run;

	PROCEDURE (c: Chewer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO c.n DO
			INC(chews)
		END
	END Run;

	(* --- тесты ------------------------------------------------------------ *)

	PROCEDURE TestProcAwaitMethod*;
		(** Ложное значение не будит; истинное — будит и передаёт монитор. *)
		VAR b: Box; w: Waiter; s: Setter; n: INTEGER;
	BEGIN
		NEW(b); b.v := 0; NEW(w); w.b := b; w.x := 5;
		Tasks.Start(w); n := 0;
		WHILE (Waiters() = 0) & (Tasks.Count() > 0) & (n < 50) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Waiters() > 0, "waiter registered its condition");
		NEW(s); s.b := b; s.x := 4; Tasks.Start(s);
		n := 0; WHILE (Waiters() > 0) & (n < 200) DO Tasks.Step; INC(n) END;
		UnitAssert.True(~w.done, "false condition did not wake the waiter");
		UnitAssert.True(Waiters() > 0, "waiter is still asleep");
		NEW(s); s.b := b; s.x := 5; Tasks.Start(s);
		n := 0; WHILE ~w.done & (n < 2000) DO Tasks.Step; INC(n) END;
		UnitAssert.True(w.done, "true condition woke the waiter");
		UnitAssert.True(Pump(3000), "all tasks finished");
		UnitAssert.EqualInt(0, Waiters(), "wait queues empty");
		UnitAssert.True(~Monitors.IsLocked(b), "monitor released")
	END TestProcAwaitMethod;

	PROCEDURE TestProcAwaitModuleMonitor*;
		(** Монитор модуля: скрытая глобальная, AWAIT по глобальной переменной. *)
		VAR w: GWaiter; s: GSetter; n: INTEGER;
	BEGIN
		g := 0; NEW(w); w.x := 7; Tasks.Start(w); n := 0;
		WHILE (Waiters() = 0) & (Tasks.Count() > 0) & (n < 50) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Waiters() > 0, "module-monitor waiter is asleep");
		NEW(s); s.x := 6; Tasks.Start(s);
		n := 0; WHILE (Waiters() > 0) & (n < 200) DO Tasks.Step; INC(n) END;
		UnitAssert.True(~w.done, "false value did not wake");
		NEW(s); s.x := 7; Tasks.Start(s);
		n := 0; WHILE ~w.done & (n < 2000) DO Tasks.Step; INC(n) END;
		UnitAssert.True(w.done, "module monitor woke the waiter");
		UnitAssert.True(Pump(3000), "all tasks finished");
		UnitAssert.EqualInt(0, Waiters(), "wait queues empty");
		UnitAssert.EqualInt(7, g, "global was set")
	END TestProcAwaitModuleMonitor;

	PROCEDURE TestProcAwaitLocalCondition*;
		(** Условие читает локальную переменную ждущего через кадр. *)
		VAR w: LocalWaiter; s: GSetter; n: INTEGER;
	BEGIN
		g := 0; NEW(w); w.x := 9; Tasks.Start(w); n := 0;
		WHILE (Waiters() = 0) & (Tasks.Count() > 0) & (n < 50) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Waiters() > 0, "local-condition waiter is asleep");
		NEW(s); s.x := 9; Tasks.Start(s);
		UnitAssert.True(Pump(3000), "setter finished");
		UnitAssert.True(w.done, "local y = g became true");
		UnitAssert.EqualInt(0, Waiters(), "wait queues empty")
	END TestProcAwaitLocalCondition;

	PROCEDURE TestProcExclusiveResults*;
		(** Функции [exclusive] всех видов результата и RETURN из середины. *)
		VAR b: Box; c: Cell;
	BEGIN
		NEW(b); NEW(c); c.x := 3;
		UnitAssert.EqualInt(11, b.Mid(1), "RETURN from the middle");
		UnitAssert.EqualInt(22, b.Mid(0), "fall-through RETURN");
		UnitAssert.EqualReal(2.5, b.Real(), 0.0001, "REAL result survives Unlock");
		UnitAssert.True(b.Ptr(c) = c, "pointer result survives Unlock");
		UnitAssert.EqualInt(1234567890123, b.Big(), "LONGINT result survives Unlock");
		UnitAssert.True(~Monitors.IsLocked(b), "monitor free after functions")
	END TestProcExclusiveResults;

	PROCEDURE TestProcExclusiveTrap*;
		(** Трап внутри [exclusive] снимает монитор. *)
		VAR b: Box;
	BEGIN
		NEW(b); tb := b;
		UnitAssert.Traps(TrapCall, "trap inside [exclusive]");
		UnitAssert.True(~Monitors.IsLocked(b), "trap released the monitor")
	END TestProcExclusiveTrap;

	PROCEDURE TestProcExclusiveNestedTry*;
		(** Вложенный Kernel.Try снимает только внутреннее взятие. *)
		VAR b: Box;
	BEGIN
		NEW(b); tb := b; gb := b; outerHeld := FALSE;
		OuterCall;	(* внутренний Try ловит трап *)
		UnitAssert.True(outerHeld, "outer entry survived the inner Try");
		UnitAssert.True(~Monitors.IsLocked(b), "all entries released")
	END TestProcExclusiveNestedTry;

	(* --- условие с вещественным, LONGINT и константным подвыражением ----- *)

	PROCEDURE [exclusive] (m: Mixed) WaitMixed, NEW;
	BEGIN
		ACTIVE.AWAIT((m.r > 0.5) & (ABS(m.l) > 2 * 3) & (m.l + 1 > m.k))
	END WaitMixed;

	PROCEDURE [exclusive] (m: Mixed) SetMixed (r: REAL; l, k: LONGINT), NEW;
	BEGIN
		m.r := r; m.l := l; m.k := k
	END SetMixed;

	PROCEDURE (t: MixedWaiter) Run;
	BEGIN
		t.m.WaitMixed; t.done := TRUE
	END Run;

	PROCEDURE (t: MixedSetter) Run;
	BEGIN
		t.m.SetMixed(1.0, 3, 0);	(* ABS(l) > 6 ложно *)
		Tasks.Yield;
		t.m.SetMixed(1.0, -10000000000, -20000000000);	(* истинно: 1e10 > 6, l + 1 > k *)
	END Run;

	PROCEDURE TestProcAwaitMixedCondition*;
	(** Условие AWAIT стоит в двух местах дерева (проверка до ожидания и
		процедура условия); оба места вычисляют одно и то же. *)
		VAR m: Mixed; w: MixedWaiter; s: MixedSetter;
	BEGIN
		NEW(m); m.r := 0; m.l := 0; m.k := 0;
		NEW(w); w.m := m; Tasks.Start(w);
		NEW(s); s.m := m; Tasks.Start(s);
		UnitAssert.True(Pump(3000), "tasks finished");
		UnitAssert.True(w.done & ~Tasks.Failed(w), "waiter passed the condition");
		UnitAssert.EqualInt(0, Waiters(), "no waiters left")
	END TestProcAwaitMixedCondition;

	PROCEDURE TestProcAwaitMixedTrueAtOnce*;
	(** Истинное сразу условие не ждёт (проверка до вызова Kernel.Await). *)
		VAR m: Mixed; w: MixedWaiter;
	BEGIN
		NEW(m); m.r := 2.0; m.l := 100; m.k := 0;
		NEW(w); w.m := m; Tasks.Start(w);
		UnitAssert.True(Pump(1000), "task finished");
		UnitAssert.True(w.done, "condition true at once")
	END TestProcAwaitMixedTrueAtOnce;

END Dev_devActive.
