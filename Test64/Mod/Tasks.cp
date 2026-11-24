MODULE Test64Tasks;
(** Тесты модуля Tasks (этап 3).

	Планировщик — действие Services.Action, но здесь мы крутим
	Tasks.Step вручную: отдельного главного цикла в консольной команде
	нет. Это те же шаги, что делает действие.
	Все тесты помечены TestProc… — задачи трогают ядро, сборщик и стеки;
	tools/unit.sh гонит модуль отдельным процессом. Нагрузка на 10 тыс.
	задач помечена TestSlow… и включается в манифесте marks=proc,slow. *)

	IMPORT Kernel, Services, Tasks, UnitAssert, Test64Clock, Console, Strings;

	CONST
		benchStack = 64 * 1024;

	VAR
		counter, ticks, eats, produced, consumed, buf, groupEats: INTEGER;
		bufFull: BOOLEAN;
		cons, prod: Tasks.Task;
		waitFailed: BOOLEAN;

	TYPE
		Counter = POINTER TO RECORD (Tasks.Task) END;
		Lazy = POINTER TO RECORD (Tasks.Task) END;
		Bad = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) END;
		Parent = POINTER TO RECORD (Tasks.Task) END;
		ParentTrap = POINTER TO RECORD (Tasks.Task) END;
		Once = POINTER TO RECORD (Tasks.Task) END;
		Probe = POINTER TO RECORD (Tasks.Task) END;
		GroupChild = POINTER TO RECORD (Tasks.Task) END;
		GroupTrapChild = POINTER TO RECORD (Tasks.Task) END;
		Prod = POINTER TO RECORD (Tasks.Task) END;
		Cons = POINTER TO RECORD (Tasks.Task) END;
		Phil = POINTER TO RECORD (Tasks.Task) id: INTEGER END;

		Fork = RECORD free: BOOLEAN; waiter: Tasks.Task END;

		ProbeAction = POINTER TO RECORD (Services.Action) END;

	VAR forks: ARRAY 5 OF Fork;

	(* --- вспомогательное -------------------------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE Now (): LONGINT;
	BEGIN
		RETURN Test64Clock.now()
	END Now;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE NilCrash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END NilCrash;

	(* --- методы задач ----------------------------------------------------- *)

	PROCEDURE (t: Counter) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO INC(counter); Tasks.Yield END
	END Run;

	PROCEDURE (t: Lazy) Run;
	BEGIN
		LOOP INC(ticks); Tasks.Sleep(1) END
	END Run;

	PROCEDURE (t: Bad) Run;
	BEGIN
		NilCrash
	END Run;

	PROCEDURE (t: Waker) Run;
		VAR me: Tasks.Task;
	BEGIN
		me := Tasks.Current();
		Tasks.Unpark(me);	(* до Park *)
		Tasks.Park;		(* не должен уснуть *)
		INC(counter)
	END Run;

	PROCEDURE (t: Once) Run;
	BEGIN
		Tasks.Sleep(0); INC(counter)
	END Run;

	PROCEDURE (t: Probe) Run;
		VAR a: ProbeAction;
	BEGIN
		NEW(a); Services.DoLater(a, Services.now);
		Services.actionHook.Step
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR g: Tasks.Group; i: INTEGER; ch: GroupChild;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 1 TO 3 DO NEW(ch); Tasks.StartIn(ch, g) END;
		(* Run возвращается сразу; TaskBoot ждёт опустошения группы *)
	END Run;

	PROCEDURE (c: GroupChild) Run;
	BEGIN
		Tasks.Yield; INC(groupEats)
	END Run;

	PROCEDURE (p: ParentTrap) Run;
		VAR g: Tasks.Group; i: INTEGER; ch: GroupTrapChild;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 1 TO 2 DO NEW(ch); Tasks.StartIn(ch, g) END
	END Run;

	PROCEDURE (c: GroupTrapChild) Run;
	BEGIN
		NilCrash
	END Run;

	(* --- производитель и потребитель на Park/Unpark ----------------------- *)

	PROCEDURE (p: Prod) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 50 DO
			WHILE bufFull DO Tasks.Park END;
			buf := i; bufFull := TRUE; INC(produced);
			Tasks.Unpark(cons)
		END
	END Run;

	PROCEDURE (c: Cons) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 50 DO
			WHILE ~bufFull DO Tasks.Park END;
			x := buf; bufFull := FALSE; INC(consumed);
			Tasks.Unpark(prod)
		END
	END Run;

	PROCEDURE TestProcYield*;
		VAR t: Counter;
	BEGIN
		counter := 0;
		NEW(t); Tasks.SetName(t, "counter"); Tasks.Start(t);
		UnitAssert.True(Pump(2000), "all tasks finished");
		UnitAssert.EqualInt(10, counter, "Yield resumed the task ten times");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(t), "state finished");
		UnitAssert.True(~Tasks.Failed(t), "no trap")
	END TestProcYield;

	PROCEDURE PumpActions (ms: LONGINT): BOOLEAN;
	(** Крутит шаги Services (как главный цикл), не вызывая Tasks.Step прямо:
		задачи двигает зарегистрированное действие планировщика. *)
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Services.actionHook.Step END;
		RETURN Tasks.Count() = 0
	END PumpActions;

	PROCEDURE TestProcSchedulerAction*;
		VAR t: Counter;
	BEGIN
		counter := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(PumpActions(3000), "scheduler action finished the task");
		UnitAssert.EqualInt(10, counter, "task ran under Services.Action")
	END TestProcSchedulerAction;

	PROCEDURE TestProcProducerConsumer*;
		VAR p: Prod; c: Cons;
	BEGIN
		produced := 0; consumed := 0; bufFull := FALSE;
		NEW(c); NEW(p); cons := c; prod := p;
		Tasks.Start(cons); Tasks.Start(prod);
		UnitAssert.True(Pump(5000), "producer and consumer finished");
		UnitAssert.EqualInt(50, produced, "produced all items");
		UnitAssert.EqualInt(50, consumed, "consumed all items");
		UnitAssert.True(~Tasks.Failed(prod) & ~Tasks.Failed(cons), "no traps")
	END TestProcProducerConsumer;

	(* --- философы: ручные Park/Unpark, вилки по возрастанию индекса ------- *)

	PROCEDURE Take (VAR f: Fork);
	BEGIN
		IF f.free THEN f.free := FALSE; RETURN END;
		f.waiter := Tasks.Current();
		Tasks.Park	(* разбудит Release, вилка переходит нам *)
	END Take;

	PROCEDURE Release (VAR f: Fork);
		VAR w: Tasks.Task;
	BEGIN
		IF f.waiter # NIL THEN
			w := f.waiter; f.waiter := NIL; Tasks.Unpark(w)
			(* владение передано: free остаётся FALSE *)
		ELSE f.free := TRUE
		END
	END Release;

	PROCEDURE (p: Phil) Run;
		VAR e, a, b: INTEGER;
	BEGIN
		FOR e := 1 TO 4 DO
			a := p.id; b := (p.id + 1) MOD 5;
			IF a > b THEN a := b; b := p.id END;	(* порядок вилок *)
			Take(forks[a]); Take(forks[b]);
			INC(eats);
			Tasks.Yield();
			Release(forks[b]); Release(forks[a])
		END
	END Run;

	PROCEDURE TestProcPhilosophers*;
		VAR i: INTEGER; p: Phil;
	BEGIN
		eats := 0;
		FOR i := 0 TO 4 DO forks[i].free := TRUE; forks[i].waiter := NIL END;
		FOR i := 0 TO 4 DO NEW(p); p.id := i; Tasks.Start(p) END;
		UnitAssert.True(Pump(5000), "philosophers finished");
		UnitAssert.EqualInt(20, eats, "every philosopher ate four times")
	END TestProcPhilosophers;

	(* --- отмена ----------------------------------------------------------- *)

	PROCEDURE TestProcCancel*;
		VAR t: Lazy; q: Counter;
	BEGIN
		ticks := 0;
		NEW(t); NEW(q); Tasks.Start(t); Tasks.Start(q);
		Tasks.Step; Tasks.Step;	(* дать обеим задачам поработать *)
		Tasks.Cancel(t);
		UnitAssert.True(Pump(3000), "cancelled and quick tasks finished");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(t), "cancelled task state");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(q), "other task finished");
		UnitAssert.True(ticks >= 1, "looper ran before cancellation");
		UnitAssert.True(~Tasks.Failed(q), "other task was not affected")
	END TestProcCancel;

	(* --- трап в задаче не трогает остальные ------------------------------ *)

	PROCEDURE TestProcTrapIsolation*;
		VAR b: Bad; g: Counter;
	BEGIN
		counter := 0;
		NEW(b); NEW(g); Tasks.Start(b); Tasks.Start(g);
		UnitAssert.True(Pump(3000), "tasks finished after a trap");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(b), "trapping task finished");
		UnitAssert.True(Tasks.Failed(b), "trapping task marked failed");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(g), "other task finished");
		UnitAssert.EqualInt(10, counter, "other task completed its work");
		UnitAssert.True(~Tasks.Failed(g), "other task not failed")
	END TestProcTrapIsolation;

	(* --- ожидание вне задачи и внутри обработчика ------------------------ *)

	PROCEDURE WaitOutside;
	BEGIN
		Tasks.Sleep(1)
	END WaitOutside;

	PROCEDURE (a: ProbeAction) Do;
	BEGIN
		waitFailed := FALSE;
		Tasks.Yield;
		waitFailed := TRUE
	END Do;

	PROCEDURE TestProcWaitGuards*;
		VAR t: Probe;
	BEGIN
		UnitAssert.Traps(WaitOutside, "wait outside a task must trap");
		NEW(t); Tasks.Start(t);
		UnitAssert.True(Pump(2000), "probe task finished");
		UnitAssert.True(~waitFailed, "Yield inside an action handler trapped");
		UnitAssert.True(Tasks.Failed(t), "probe task failed as expected")
	END TestProcWaitGuards;

	PROCEDURE TestProcNoLostWake*;
		VAR t: Waker;
	BEGIN
		counter := 0;
		NEW(t); Tasks.Start(t);
		UnitAssert.True(Pump(2000), "task finished");
		UnitAssert.EqualInt(1, counter, "Unpark before Park is not lost")
	END TestProcNoLostWake;

	(* --- группа задач ----------------------------------------------------- *)

	PROCEDURE TestProcGroup*;
		VAR p: Parent;
	BEGIN
		groupEats := 0;
		NEW(p); Tasks.Start(p);
		UnitAssert.True(Pump(3000), "parent and children finished");
		UnitAssert.EqualInt(3, groupEats, "all children ran");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(p), "parent waited for children")
	END TestProcGroup;

	PROCEDURE TestProcGroupTrap*;
		VAR p: ParentTrap;
	BEGIN
		NEW(p); Tasks.Start(p);
		UnitAssert.True(Pump(3000), "group tasks finished");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(p), "parent finished")
	END TestProcGroupTrap;

	(* --- нагрузка --------------------------------------------------------- *)

	PROCEDURE LoadBench (n, stackSize: INTEGER; IN tag: ARRAY OF CHAR);
		VAR i: INTEGER; t: Once; tStart0, tStart1, t0, t1, setupNs, stepNs, sw0, sw1, bytes: LONGINT;
	BEGIN
		counter := 0; Tasks.ResetStats();
		tStart0 := Now();
		FOR i := 1 TO n DO
			NEW(t); Tasks.SetStackSize(t, stackSize); Tasks.Start(t)
		END;
		tStart1 := Now(); sw0 := Tasks.Switches(); t0 := Now();
		IF ~Pump(60000) THEN UnitAssert.Fail("load: tasks did not finish") END;
		t1 := Now(); sw1 := Tasks.Switches();
		setupNs := tStart1 - tStart0; stepNs := t1 - t0; bytes := n * stackSize;
		W("BENCH "); W(tag);
		W(" tasks="); N(n);
		W(" counter="); N(counter);
		W(" switches="); N(sw1 - sw0);
		W(" setupNs="); N(setupNs);
		W(" stepNs="); N(stepNs);
		IF sw1 > sw0 THEN W(" nsPerSwitch="); N(stepNs DIV (sw1 - sw0)) END;
		W(" stackVirtual="); N(bytes);
		Console.WriteLn;
		UnitAssert.EqualInt(n, counter, "every task ran once")
	END LoadBench;

	PROCEDURE TestProcThousand*;
	BEGIN
		LoadBench(1000, benchStack, "tasks1000")
	END TestProcThousand;

	PROCEDURE TestSlowLoadTenThousand*;
	BEGIN
		LoadBench(10000, benchStack, "tasks10k")
	END TestSlowLoadTenThousand;

	PROCEDURE TestProcList*;
	(** Отладочный список задач печатается без трапа. *)
		VAR t: Counter;
	BEGIN
		NEW(t); Tasks.SetName(t, "listed"); Tasks.Start(t);
		Tasks.List;	(* задача ещё в очереди: строка со state=ready *)
		UnitAssert.True(Pump(2000), "listed task finished")
	END TestProcList;

END Test64Tasks.
