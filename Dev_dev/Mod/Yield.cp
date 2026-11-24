MODULE Dev_devYield;
(** Тесты ключа точек уступки.

	Модуль компилируется с ключом «^»: на обратном переходе циклов
	вставлен Kernel.Poll. Проверяется, что счётный цикл в задаче уступает
	другой задаче, но не уступает внутри монитора и вне задачи. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Tasks, UnitAssert;

	TYPE
		Box = POINTER TO RECORD v: INTEGER END;

		Chewer = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Short = POINTER TO RECORD (Tasks.Task) END;
		Holder = POINTER TO RECORD (Tasks.Task) b: Box; n: INTEGER END;
		Contender = POINTER TO RECORD (Tasks.Task) b: Box END;
		CondWaiter = POINTER TO RECORD (Tasks.Task) b: Box END;
		CondSetter = POINTER TO RECORD (Tasks.Task) b: Box END;
		CondEnterer = POINTER TO RECORD (Tasks.Task) b: Box END;

	VAR
		chews, total: INTEGER;
		shortDone, sawEarly: BOOLEAN;
		inside, contenderSaw: BOOLEAN;
		condFlag, inCond, enteredInCond: BOOLEAN;

	PROCEDURE (c: Chewer) Run;
		VAR i: INTEGER;
	BEGIN
		(* RequestPoll внутри цикла делает уступку детерминированной:
		   иначе одна и та же задача может доработать срез до конца. *)
		FOR i := 1 TO c.n DO INC(chews); Kernel.RequestPoll END
	END Run;

	PROCEDURE (s: Short) Run;
	BEGIN sawEarly := chews < total; shortDone := TRUE
	END Run;

	PROCEDURE [exclusive] (b: Box) Spin (n: INTEGER), NEW;
		VAR i: INTEGER;
	BEGIN
		inside := TRUE;
		FOR i := 1 TO n DO INC(chews) END;
		inside := FALSE
	END Spin;

	PROCEDURE [exclusive] (b: Box) Set (x: INTEGER), NEW;
	BEGIN contenderSaw := inside; b.v := x
	END Set;

	PROCEDURE (h: Holder) Run;
	BEGIN h.b.Spin(h.n)
	END Run;

	PROCEDURE (c: Contender) Run;
	BEGIN c.b.Set(1)
	END Run;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE TestProcYieldProgress*;
		(** Счётный цикл с ключом не замораживает другие задачи. *)
		VAR c: Chewer; s: Short; n: INTEGER;
	BEGIN
		chews := 0; shortDone := FALSE; sawEarly := FALSE; total := 2000000000;
		NEW(c); c.n := total; NEW(s);
		Tasks.Start(c); Tasks.Start(s);
		n := 0;
		WHILE ~shortDone & (n < 5000) DO Kernel.RequestPoll; Tasks.Step; INC(n) END;
		UnitAssert.True(shortDone, "short task ran while the keyed loop was going");
		UnitAssert.True(sawEarly, "short task saw the long loop unfinished");
		Tasks.Cancel(c);	(* отмена срабатывает в точке уступки *)
		n := 0;
		WHILE (Tasks.Count() > 0) & (n < 5000) DO Kernel.RequestPoll; Tasks.Step; INC(n) END;
		UnitAssert.True(Tasks.Count() = 0, "cancelled task finished");
		UnitAssert.True(Tasks.State(c) = Tasks.Cancelled, "cancel trapped at the yield point")
	END TestProcYieldProgress;

	PROCEDURE TestProcNoYieldInMonitor*;
		(** Poll не уступает, пока задача держит монитор. *)
		VAR b: Box; h: Holder; c: Contender; n: INTEGER;
	BEGIN
		NEW(b); inside := FALSE; contenderSaw := TRUE;
		NEW(h); h.b := b; h.n := 10000; NEW(c); c.b := b;
		Tasks.Start(h); Tasks.Start(c);
		n := 0;
		WHILE (Tasks.Count() > 0) & (n < 5000) DO Kernel.RequestPoll; Tasks.Step; INC(n) END;
		UnitAssert.True(~contenderSaw, "Poll did not yield inside the monitor");
		UnitAssert.True(Pump(5000), "both tasks finished")
	END TestProcNoYieldInMonitor;

	PROCEDURE CountOnMain (n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO n DO INC(chews) END
	END CountOnMain;

	PROCEDURE TestProcNoYieldOutsideTask*;
		(** Poll вне задачи ничего не делает. *)
	BEGIN
		chews := 0;
		Kernel.RequestPoll;
		CountOnMain(1000);
		UnitAssert.EqualInt(1000, chews, "keyed loop outside a task completed")
	END TestProcNoYieldOutsideTask;

	(* --- условие с точкой уступки при передаче монитора ------------------- *)

	PROCEDURE SlowCond (frame: S.ADDRESS): BOOLEAN;
	(* Условие прямого Kernel.Await: цикл с точкой уступки (ключ ^). *)
		VAR t0: LONGINT;
	BEGIN
		inCond := TRUE;
		t0 := Kernel.Time();
		WHILE Kernel.Time() - t0 < 10 DO Kernel.RequestPoll END;
		inCond := FALSE;
		RETURN condFlag
	END SlowCond;

	PROCEDURE (t: CondWaiter) Run;
	BEGIN
		Kernel.Lock(t.b); Kernel.Await(SlowCond, 0, t.b); Kernel.Unlock(t.b)
	END Run;

	PROCEDURE (t: CondSetter) Run;
	BEGIN
		Kernel.Lock(t.b); condFlag := TRUE; Kernel.Unlock(t.b)	(* Unlock вычисляет SlowCond *)
	END Run;

	PROCEDURE (t: CondEnterer) Run;
	BEGIN
		Kernel.Lock(t.b); enteredInCond := inCond; Kernel.Unlock(t.b)
	END Run;

	PROCEDURE TestProcNoEntryDuringHandOff*;
		(** Пока уходящий перебирает условия ждущих, монитор занят: точка
			уступки в условии не пускает в монитор другую задачу. *)
		VAR b: Box; w: CondWaiter; s: CondSetter; e: CondEnterer; n: INTEGER;
	BEGIN
		NEW(b); condFlag := FALSE; inCond := FALSE; enteredInCond := FALSE;
		NEW(w); w.b := b; Tasks.Start(w);
		n := 0;
		WHILE (Tasks.State(w) # Tasks.Parked) & (n < 100) DO Tasks.Step; INC(n) END;
		UnitAssert.True(Tasks.State(w) = Tasks.Parked, "waiter waits");
		NEW(s); s.b := b; NEW(e); e.b := b;
		Tasks.Start(s); Tasks.Start(e);
		UnitAssert.True(Pump(5000), "tasks finished");
		UnitAssert.True(~Tasks.Failed(w) & ~Tasks.Failed(s) & ~Tasks.Failed(e), "no traps");
		UnitAssert.True(~enteredInCond, "nobody entered while conditions were evaluated")
	END TestProcNoEntryDuringHandOff;

END Dev_devYield.
