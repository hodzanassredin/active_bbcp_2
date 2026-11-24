MODULE Dev_testsYield;
(** Тесты ключа точек уступки (этап 6.2).

	Модуль компилируется с ключом «^»: на обратном переходе циклов
	вставлен Kernel.Poll. Проверяется, что счётный цикл в задаче уступает
	другой задаче, но не уступает внутри монитора и вне задачи. *)

	IMPORT ACTIVE, Kernel, Tasks, UnitAssert;

	TYPE
		Box = POINTER TO RECORD v: INTEGER END;

		Chewer = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Short = POINTER TO RECORD (Tasks.Task) END;
		Holder = POINTER TO RECORD (Tasks.Task) b: Box; n: INTEGER END;
		Contender = POINTER TO RECORD (Tasks.Task) b: Box END;

	VAR
		chews, total: INTEGER;
		shortDone, sawEarly: BOOLEAN;
		inside, contenderSaw: BOOLEAN;

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

END Dev_testsYield.
