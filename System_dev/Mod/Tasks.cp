MODULE System_devTasks;
(** Тесты модуля Tasks.

	Планировщик — действие Services.Action, но здесь мы крутим
	Tasks.Step вручную: отдельного главного цикла в консольной команде
	нет. Это те же шаги, что делает действие.
	Все тесты помечены TestProc… — задачи трогают ядро, сборщик и стеки;
	tools/unit.sh гонит модуль отдельным процессом. Нагрузка на 10 тыс.
	задач помечена TestSlow… и включается в манифесте marks=proc,slow. *)

	IMPORT S := SYSTEM, ACTIVE, Kernel, Monitors, Services, Tasks, UnitAssert, System_devClock, Console, Strings;

	CONST
		benchStack = 64 * 1024;
		smallStack = 16 * 1024;

	VAR
		counter, ticks, eats, produced, consumed, buf, groupEats: INTEGER;
		finalized: INTEGER;	(* сработали FINALIZE задач в тесте Reap *)
		bufFull: BOOLEAN;
		cons, prod: Tasks.Task;
		waitFailed: BOOLEAN;

	TYPE
		Counter = POINTER TO RECORD (Tasks.Task) END;
		Lazy = POINTER TO RECORD (Tasks.Task) END;
		Bad = POINTER TO RECORD (Tasks.Task) END;
		DeepHalt = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) END;
		Parent = POINTER TO RECORD (Tasks.Task) END;
		ParentTrap = POINTER TO RECORD (Tasks.Task) END;
		Once = POINTER TO RECORD (Tasks.Task) END;
		Reaper = POINTER TO RECORD (Tasks.Task) END;
		Probe = POINTER TO RECORD (Tasks.Task) END;
		GroupChild = POINTER TO RECORD (Tasks.Task) END;
		GroupTrapChild = POINTER TO RECORD (Tasks.Task) END;
		ProbeBox = POINTER TO RECORD n: INTEGER END;
		LockHolder = POINTER TO RECORD (Tasks.Task) mon: ProbeBox END;
		LockProbe = POINTER TO RECORD (Tasks.Task) mon: ProbeBox; got: BOOLEAN END;
		Prod = POINTER TO RECORD (Tasks.Task) END;
		Cons = POINTER TO RECORD (Tasks.Task) END;
		Phil = POINTER TO RECORD (Tasks.Task) id: INTEGER END;
		LongSleeper = POINTER TO RECORD (Tasks.Task) ms: INTEGER END;
		Resleeper = POINTER TO RECORD (Tasks.Task) wakes: INTEGER END;
		Churner = POINTER TO RECORD (Tasks.Task) wakes: INTEGER END;
		YieldLocker = POINTER TO RECORD (Tasks.Task) mon: ProbeBox; got, parkedBack: BOOLEAN END;
		Joiner = POINTER TO RECORD (Tasks.Task) target: Tasks.Task; done: BOOLEAN END;
		SelfJoiner = POINTER TO RECORD (Tasks.Task) target: Tasks.Task END;
		Unparker = POINTER TO RECORD (Tasks.Task) target: Tasks.Task; sawReady: BOOLEAN END;
		FlagSleeper = POINTER TO RECORD (Tasks.Task) flag: BOOLEAN END;
		TrapParent = POINTER TO RECORD (Tasks.Task) child: LongSleeper END;
		TwoGroups = POINTER TO RECORD (Tasks.Task) long: LongSleeper; same: BOOLEAN END;
		Parker = POINTER TO RECORD (Tasks.Task) done: BOOLEAN END;
		ExtParker = POINTER TO RECORD (Tasks.Task) done: BOOLEAN END;
		AtomicLeaker = POINTER TO RECORD (Tasks.Task) END;
		Spinner = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		StopSleeper = POINTER TO RECORD (Tasks.Task) END;
		CountYielder = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Room = POINTER TO RECORD inside, overlaps, visits: INTEGER END;
		Visitor = POINTER TO RECORD (Tasks.Task) room: Room END;

		Fork = RECORD free: BOOLEAN; waiter: Tasks.Task END;

		ProbeAction = POINTER TO RECORD (Services.Action) END;

	VAR forks: ARRAY 5 OF Fork;
		stopSpin: BOOLEAN;

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
		RETURN System_devClock.now()
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

	PROCEDURE (t: Reaper) Run;
	BEGIN
		Tasks.Sleep(0)
	END Run;

	PROCEDURE (t: Reaper) FINALIZE;
	BEGIN
		INC(finalized)
	END FINALIZE;

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
		UnitAssert.EqualInt(127, Kernel.err, "cancellation is trap 127, not a precondition trap");
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

	(* --- трап у дна маленького стека ------------------------------------ *)

	PROCEDURE Dive (top: LONGINT; room: INTEGER);
	(* Спуск рекурсией, пока до дна стека не останется меньше room байт; там HALT. *)
		VAR pad: ARRAY 64 OF BYTE; here: LONGINT;
	BEGIN
		pad[0] := 0; here := S.ADR(pad);
		IF top - here < smallStack - room THEN Dive(top, room) ELSE HALT(42) END
	END Dive;

	PROCEDURE (t: DeepHalt) Run;
		VAR x: INTEGER;
	BEGIN
		Dive(S.ADR(x), 200)
	END Run;

	PROCEDURE TestProcHaltNearStackBottom*;
	(* Трап у дна стека задачи: обработчик не должен переполнить стек задачи. *)
		VAR d: DeepHalt; g: Counter;
	BEGIN
		counter := 0;
		NEW(d); Tasks.SetStackSize(d, smallStack); NEW(g); Tasks.Start(d); Tasks.Start(g);
		UnitAssert.True(Pump(3000), "tasks finished after a trap at the stack bottom");
		UnitAssert.True(Tasks.Failed(d), "trapping task marked failed");
		UnitAssert.EqualInt(10, counter, "other task completed its work")
	END TestProcHaltNearStackBottom;

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
		LoadBench(1000, benchStack, "tasks1000");
		LoadBench(1000, benchStack, "tasks1000-warm")	(* стеки уже в пуле *)
	END TestProcThousand;

	PROCEDURE TestSlowLoadTenThousand*;
	BEGIN
		LoadBench(10000, benchStack, "tasks10k");
		LoadBench(10000, benchStack, "tasks10k-warm")
	END TestSlowLoadTenThousand;

	PROCEDURE SpawnShort (n: INTEGER);
	(* Создаём в отдельном кадре: после возврата его мёртвые слоты не
	   сканируются консервативным ScanStack и не удерживают задачи. *)
		VAR i: INTEGER; t: Reaper;
	BEGIN
		FOR i := 1 TO n DO
			NEW(t); Tasks.SetStackSize(t, benchStack); Tasks.Start(t)
		END;
		t := NIL
	END SpawnShort;

	PROCEDURE TestSlowReap*;
	(** Завершённые задачи должны уходить из списка `all` в Reap: иначе
		каждая когда-либо запущенная задача и всё, на что она ссылается,
		живёт до конца процесса. После 10 тыс. коротких задач список пуст,
		а объекты задач собраны (об этом говорит их FINALIZE). *)
	BEGIN
		finalized := 0;
		SpawnShort(10000);
		UnitAssert.True(Pump(120000), "short tasks finished");
		UnitAssert.EqualInt(0, Tasks.ListSize(), "finished tasks left the all list");
		Kernel.Collect;
		(* Консервативный ScanStack может удержать один объект, адрес
		   которого остался в сохранённом регистре кадра; при старом
		   поведении (список all) не собрался бы ни один. *)
		UnitAssert.True(finalized >= 9999, "task objects were collected")
	END TestSlowReap;

	PROCEDURE TestProcList*;
	(** Отладочный список задач печатается без трапа. *)
		VAR t: Counter;
	BEGIN
		NEW(t); Tasks.SetName(t, "listed"); Tasks.Start(t);
		Tasks.List;	(* задача ещё в очереди: строка со state=ready *)
		UnitAssert.True(Pump(2000), "listed task finished")
	END TestProcList;

	(* --- флаг wake не должен протекать в ожидание монитора ------------- *)

	PROCEDURE (h: LockHolder) Run;
	BEGIN
		Kernel.Lock(h.mon);
		Tasks.Sleep(50);
		Kernel.Unlock(h.mon)
	END Run;

	PROCEDURE (p: LockProbe) Run;
	BEGIN
		Tasks.Park;		(* сюда придёт Unpark *)
		Kernel.Lock(p.mon);	(* монитор занят держателем *)
		p.got := TRUE;
		Kernel.Unlock(p.mon)
	END Run;

	PROCEDURE TestProcParkWakeFlag*;
	(** Unpark будит Park; флаг wake должен быть съеден этим пробуждением.
		Если он остаётся, следующий Park (внутри Kernel.Lock) вернётся
		сразу, и Lock трапнет «woken without the monitor». *)
		VAR mon: ProbeBox; h: LockHolder; p: LockProbe; ok: BOOLEAN;
	BEGIN
		NEW(mon); NEW(h); h.mon := mon;
		Tasks.Start(h);
		WHILE ~Monitors.IsLocked(mon) & (Tasks.Count() > 0) DO Tasks.Step END;
		UnitAssert.True(Monitors.IsLocked(mon), "holder took the monitor");
		NEW(p); p.mon := mon; Tasks.Start(p);
		Tasks.Step;	(* probe ушёл в Park *)
		UnitAssert.True(Tasks.State(p) = Tasks.Parked, "probe parked");
		Tasks.Unpark(p);
		ok := Pump(5000);
		UnitAssert.True(ok, "tasks finished");
		UnitAssert.True(~Tasks.Failed(p), "probe survived the monitor wait");
		UnitAssert.True(p.got, "probe acquired the monitor");
		UnitAssert.True(~Monitors.IsLocked(mon), "monitor released")
	END TestProcParkWakeFlag;

	(* --- очередь спящих --------------------------------------------------- *)

	PROCEDURE (t: LongSleeper) Run;
	BEGIN
		Tasks.Sleep(t.ms)
	END Run;

	PROCEDURE (t: Resleeper) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 3 DO Tasks.Sleep(1000); INC(t.wakes) END;	(* будят раньше срока *)
		Tasks.Sleep(2); INC(t.wakes)
	END Run;

	PROCEDURE TestProcNextDeadlineCheap*;
	(** Ближайший срок не требует обхода спящих: 2000 вопросов при 10 тыс.
		спящих задач укладываются в 20 мс (обход всех — около 200 мс и больше). *)
		CONST n = 10000; q = 2000;
		VAR i: INTEGER; t: POINTER TO ARRAY OF LongSleeper; d, t0, t1: LONGINT;
	BEGIN
		NEW(t, n);
		FOR i := 0 TO n - 1 DO
			NEW(t[i]); t[i].ms := 60000 + i; Tasks.SetStackSize(t[i], benchStack); Tasks.Start(t[i])
		END;
		Tasks.Step;	(* все уснули *)
		d := 0;
		t0 := Kernel.Time();
		FOR i := 1 TO q DO d := Tasks.NextDeadline() END;
		t1 := Kernel.Time();
		UnitAssert.True(d > 0, "deadline found");
		UnitAssert.True(t1 - t0 < 20, "NextDeadline does not walk all sleepers");
		FOR i := 0 TO n - 1 DO Tasks.Cancel(t[i]) END;
		UnitAssert.True(Pump(5000), "cancelled sleepers finished")
	END TestProcNextDeadlineCheap;

	PROCEDURE TestProcResleepAfterUnpark*;
	(** Задачу будят раньше срока и она засыпает снова: устаревший срок не
		будит её второй раз, последний сон заканчивается по своему сроку. *)
		VAR t: Resleeper; i: INTEGER;
	BEGIN
		NEW(t); Tasks.Start(t);
		FOR i := 1 TO 3 DO
			Tasks.Step; UnitAssert.True(Tasks.State(t) = Tasks.Sleeping, "sleeps");
			Tasks.Unpark(t)
		END;
		UnitAssert.True(Pump(2000), "finished");
		UnitAssert.EqualInt(4, t.wakes, "one wake per sleep");
		UnitAssert.EqualInt(-1, SHORT(Tasks.NextDeadline()), "no stale deadlines")
	END TestProcResleepAfterUnpark;

	(* --- найденные ревью дефекты ------------------------------------------ *)

	PROCEDURE (t: Churner) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 200 DO Tasks.Sleep(200000); INC(t.wakes) END
	END Run;

	PROCEDURE TestProcCompactWhileSleeping*;
	(** Уплотнение кучи внутри Sleep: запись засыпающей задачи ещё не в куче,
		но nSleeping её уже учитывает — инвариант уплотнения не должен ломаться. *)
		VAR a: LongSleeper; b: Churner; i: INTEGER;
	BEGIN
		NEW(a); a.ms := 100000; Tasks.Start(a);
		NEW(b); Tasks.Start(b);
		Tasks.Step;
		i := 0;
		WHILE (i < 200) & (Tasks.State(b) = Tasks.Sleeping) DO
			Tasks.Unpark(b); Tasks.Step; INC(i)
		END;
		UnitAssert.True(~Tasks.Failed(b), "sleeper survived heap compaction");
		UnitAssert.EqualInt(200, b.wakes, "every early wake returned from Sleep");
		Tasks.Cancel(a);
		UnitAssert.True(Pump(2000), "tasks finished")
	END TestProcCompactWhileSleeping;

	PROCEDURE (t: YieldLocker) Run;
	BEGIN
		Tasks.Yield;	(* пока готова, главный стек зовёт Unpark *)
		Kernel.Lock(t.mon);	(* монитор держит LockHolder *)
		t.got := TRUE;
		Kernel.Unlock(t.mon);
		Tasks.Park;	(* пробуждение, пришедшее до ожидания монитора, не теряется *)
		t.parkedBack := TRUE
	END Run;

	PROCEDURE (t: Unparker) Run;
	BEGIN
		t.sawReady := Tasks.State(t.target) = Tasks.Ready;
		Tasks.Unpark(t.target)
	END Run;

	PROCEDURE (t: Parker) Run;
	BEGIN
		Tasks.Park; t.done := TRUE
	END Run;

	PROCEDURE (t: ExtParker) Run;
	BEGIN
		Tasks.ParkExternal; t.done := TRUE
	END Run;

	PROCEDURE StepUntilParked (t: Tasks.Task);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (Tasks.State(t) # Tasks.Parked) & (i < 50) DO Tasks.Step; INC(i) END;
		Tasks.Step
	END StepUntilParked;

	PROCEDURE TestProcParkExternalIsNotDeadlock*;
	(** Задача ждёт события извне: планировщик тупик не объявляет, Unpark
		её будит. Такая же задача в простом Park — тупик. *)
		VAR e: ExtParker; p: Parker;
	BEGIN
		NEW(e); e.done := FALSE; Tasks.Start(e);
		StepUntilParked(e);
		UnitAssert.True(Tasks.State(e) = Tasks.Parked, "task waits");
		UnitAssert.True(~Tasks.Deadlocked(), "waiting for an external event is not a deadlock");
		NEW(p); p.done := FALSE; Tasks.Start(p);
		StepUntilParked(p);
		UnitAssert.True(~Tasks.Deadlocked(), "still not a deadlock while one task waits outside");
		Tasks.Unpark(e);
		Tasks.Step;
		UnitAssert.True(e.done, "external waiter finished");
		Tasks.Step;
		UnitAssert.True(Tasks.Deadlocked(), "plain Park with nobody to wake is a deadlock");
		Tasks.Unpark(p);
		UnitAssert.True(Pump(1000), "tasks finished")
	END TestProcParkExternalIsNotDeadlock;

	PROCEDURE (t: AtomicLeaker) Run;
	BEGIN
		Tasks.BeginAtomic; HALT(50)
	END Run;

	PROCEDURE TestProcAtomicSectionEndsWithTask*;
	(** Задача трапнула внутри неделимого участка: участок кончается вместе
		с задачей, остальные задачи ждут как обычно. *)
		VAR a: AtomicLeaker; s: LongSleeper;
	BEGIN
		NEW(a); Tasks.Start(a);
		NEW(s); s.ms := 1; Tasks.Start(s);
		UnitAssert.True(Pump(2000), "tasks finished");
		UnitAssert.True(Tasks.Failed(a), "the task trapped");
		UnitAssert.True(~Tasks.Failed(s), "another task waits after the section was abandoned")
	END TestProcAtomicSectionEndsWithTask;

	PROCEDURE TestProcUnparkOutsideTaskReschedules*;
	(** Планировщик назначил свой шаг на срок спящей задачи. Unpark с
		главного стека делает другую задачу готовой: она исполняется в
		ближайшем шаге главного цикла, а не в срок спящей. *)
		VAR s: LongSleeper; p: Parker; t0: LONGINT;
	BEGIN
		NEW(s); s.ms := 3000; Tasks.Start(s);
		NEW(p); p.done := FALSE; Tasks.Start(p);
		t0 := Kernel.Time();
		WHILE ((Tasks.State(p) # Tasks.Parked) OR (Tasks.State(s) # Tasks.Sleeping))
			& (Kernel.Time() - t0 <= 1000) DO Services.actionHook.Step END;
		UnitAssert.True(Tasks.State(p) = Tasks.Parked, "parker parked");
		Services.actionHook.Step;	(* шаг планировщика назначен на срок спящей *)
		Tasks.Unpark(p);
		t0 := Kernel.Time();
		WHILE ~p.done & (Kernel.Time() - t0 <= 1000) DO Services.actionHook.Step END;
		UnitAssert.True(p.done, "woken task ran before the sleeper's deadline");
		Tasks.Cancel(s);
		UnitAssert.True(Pump(2000), "tasks finished")
	END TestProcUnparkOutsideTaskReschedules;

	PROCEDURE TestProcUnparkReadyThenLock*;
	(** Unpark готовой задачи не делает ожидание монитора холостым, а сам
		остаётся пользователю: следующий Park возвращается сразу.
		Очередь: b уступает, u будит готовую b, b входит в занятый монитор. *)
		VAR mon: ProbeBox; h: LockHolder; b: YieldLocker; u: Unparker;
	BEGIN
		NEW(mon); NEW(h); h.mon := mon; Tasks.Start(h);
		WHILE ~Monitors.IsLocked(mon) & (Tasks.Count() > 0) DO Tasks.Step END;
		NEW(b); b.mon := mon; Tasks.Start(b);
		NEW(u); u.target := b; Tasks.Start(u);
		UnitAssert.True(Pump(3000), "tasks finished");
		UnitAssert.True(u.sawReady, "locker was ready when woken");
		UnitAssert.True(~Tasks.Failed(b), "monitor wait survived the early Unpark");
		UnitAssert.True(b.got, "locker acquired the monitor");
		UnitAssert.True(b.parkedBack, "user wake was kept for Park")
	END TestProcUnparkReadyThenLock;

	PROCEDURE TestProcUnparkInEnterQueue*;
	(** Unpark задачи из очереди входа монитора — ложное пробуждение:
		задача ждёт дальше и получает монитор от владельца. *)
		VAR mon: ProbeBox; h: LockHolder; b: YieldLocker;
	BEGIN
		NEW(mon); NEW(h); h.mon := mon; Tasks.Start(h);
		WHILE ~Monitors.IsLocked(mon) & (Tasks.Count() > 0) DO Tasks.Step END;
		NEW(b); b.mon := mon; Tasks.Start(b);
		Tasks.Step; Tasks.Step;	(* Yield, затем Lock: b в очереди входа *)
		UnitAssert.True(Tasks.State(b) = Tasks.Parked, "locker waits for the monitor");
		Tasks.Unpark(b);
		UnitAssert.True(Pump(3000), "tasks finished");
		UnitAssert.True(~Tasks.Failed(b), "spurious wake is not an error");
		UnitAssert.True(b.got, "locker acquired the monitor");
		UnitAssert.True(b.parkedBack, "user wake was kept for Park")
	END TestProcUnparkInEnterQueue;

	PROCEDURE (t: Joiner) Run;
	BEGIN
		Tasks.Join(t.target); t.done := TRUE
	END Run;

	PROCEDURE TestProcJoinForeignUnpark*;
	(** Посторонний Unpark одного ждущего Join не теряет остальных ждущих. *)
		VAR tt: LongSleeper; x, w: Joiner;
	BEGIN
		NEW(tt); tt.ms := 30; Tasks.Start(tt);
		NEW(x); x.target := tt; Tasks.Start(x);
		NEW(w); w.target := tt; Tasks.Start(w);
		Tasks.Step;
		UnitAssert.True(Tasks.State(w) = Tasks.Waiting, "joiner waits");
		Tasks.Unpark(w); Tasks.Step;
		UnitAssert.True(Pump(2000), "all joiners woke");
		UnitAssert.True(x.done & w.done, "both joiners returned");
		Tasks.Cancel(x); Tasks.Cancel(w); IF Pump(2000) THEN END	(* уборка после провала *)
	END TestProcJoinForeignUnpark;

	PROCEDURE (t: SelfJoiner) Run;
	BEGIN
		Tasks.Join(t.target)
	END Run;

	PROCEDURE TestProcJoinPreconditions*;
	(** Join себя — трап 23, Join незапущенной задачи — трап 24. *)
		VAR s: SelfJoiner; n: SelfJoiner; fresh: Lazy;
	BEGIN
		NEW(s); s.target := s; Tasks.Start(s);
		IF ~Pump(1000) THEN Tasks.Cancel(s); IF Pump(1000) THEN END; UnitAssert.Fail("self join waits forever") END;
		UnitAssert.True(Tasks.Failed(s), "self join traps");
		UnitAssert.EqualInt(23, Kernel.err, "self join is trap 23");
		NEW(fresh); NEW(n); n.target := fresh; Tasks.Start(n);
		IF ~Pump(1000) THEN Tasks.Cancel(n); IF Pump(1000) THEN END; UnitAssert.Fail("join of a new task waits forever") END;
		UnitAssert.True(Tasks.Failed(n), "join of a new task traps");
		UnitAssert.EqualInt(24, Kernel.err, "join of a new task is trap 24")
	END TestProcJoinPreconditions;

	PROCEDURE (t: FlagSleeper) Run;
	BEGIN
		Tasks.Sleep(10000); t.flag := TRUE
	END Run;

	PROCEDURE TestProcCancelSleeping*;
	(** Отмена спящей задачи прерывает её в Sleep трапом 127. *)
		VAR t: FlagSleeper;
	BEGIN
		NEW(t); Tasks.Start(t); Tasks.Step;
		Tasks.Cancel(t);
		UnitAssert.True(Pump(2000), "cancelled sleeper finished");
		UnitAssert.True(~t.flag, "Sleep did not return normally");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(t), "state cancelled")
	END TestProcCancelSleeping;

	PROCEDURE (t: TrapParent) Run;
		VAR g: Tasks.Group;
	BEGIN
		g := Tasks.NewGroup();
		NEW(t.child); t.child.ms := 100000; Tasks.StartIn(t.child, g);
		HALT(1)
	END Run;

	PROCEDURE TestProcTrapCancelsOwnGroup*;
	(** Трап родителя отменяет его собственную группу: дети не живут сиротами. *)
		VAR p: TrapParent;
	BEGIN
		NEW(p); Tasks.Start(p);
		UnitAssert.True(Pump(2000), "parent and child finished");
		UnitAssert.EqualInt(0, Tasks.Count(), "no orphans");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(p.child), "child cancelled");
		Tasks.Cancel(p.child); IF Pump(2000) THEN END	(* уборка после провала *)
	END TestProcTrapCancelsOwnGroup;

	PROCEDURE (t: TwoGroups) Run;
		VAR g1, g2: Tasks.Group; c: GroupChild;
	BEGIN
		g1 := Tasks.NewGroup();
		NEW(t.long); t.long.ms := 50; Tasks.StartIn(t.long, g1);
		g2 := Tasks.NewGroup();
		NEW(c); Tasks.StartIn(c, g2);
		t.same := g1 = g2
	END Run;

	PROCEDURE TestProcNewGroupTwice*;
	(** У задачи одна собственная группа: второй NewGroup возвращает её же,
		и родитель ждёт всех детей. *)
		VAR p: TwoGroups; t0: LONGINT;
	BEGIN
		NEW(p); Tasks.Start(p);
		t0 := Kernel.Time();
		WHILE ~((Tasks.State(p) = Tasks.Finished) OR (Tasks.State(p) = Tasks.Cancelled))
				& (Kernel.Time() - t0 < 2000) DO Tasks.Step END;
		UnitAssert.True(p.same, "second NewGroup returns the own group");
		UnitAssert.EqualInt(Tasks.Finished, Tasks.State(p.long), "parent waited for the first child");
		UnitAssert.True(Pump(2000), "tasks finished")
	END TestProcNewGroupTwice;

	(* --- прямое переключение задача → задача ------------------------------ *)

	PROCEDURE (t: Spinner) Run;
	BEGIN
		WHILE ~stopSpin DO INC(t.n); Tasks.Yield END
	END Run;

	PROCEDURE (t: StopSleeper) Run;
	BEGIN
		Tasks.Sleep(2); stopSpin := TRUE
	END Run;

	PROCEDURE (t: CountYielder) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO INC(t.n); Tasks.Yield END
	END Run;

	PROCEDURE TestProcDirectStepBounded*;
	(** Две задачи уступают друг другу без конца: Step всё равно отдаёт
		управление главному циклу по кванту, а спящая задача просыпается
		и останавливает их. Одна задача, уступающая сама себе, тоже не
		держит Step. *)
		VAR a, b: Spinner; z: StopSleeper; t0, dt: LONGINT;
	BEGIN
		stopSpin := FALSE;
		NEW(a); Tasks.Start(a);
		t0 := Kernel.Time(); Tasks.Step; dt := Kernel.Time() - t0;
		UnitAssert.True(dt < 200, "one yielding task: Step returns");
		UnitAssert.True(a.n > 64, "one yielding task ran past the direct limit");
		NEW(b); Tasks.Start(b);
		t0 := Kernel.Time(); Tasks.Step; dt := Kernel.Time() - t0;
		UnitAssert.True(dt < 200, "two yielding tasks: Step returns");
		NEW(z); Tasks.Start(z);
		UnitAssert.True(Pump(2000), "sleeper woke among yielding tasks and stopped them");
		UnitAssert.True(b.n > 0, "second task ran")
	END TestProcDirectStepBounded;

	PROCEDURE TestProcDirectFinishAndTrap*;
	(** Задача, получившая процессор прямо от соседки, завершается или
		трапает: планировщик убирает именно её, соседки доходят до конца. *)
		VAR a, c: CountYielder; bad: Bad; once: Once;
	BEGIN
		counter := 0;
		NEW(a); Tasks.Start(a);
		NEW(bad); Tasks.Start(bad);
		NEW(once); Tasks.Start(once);
		NEW(c); Tasks.Start(c);
		UnitAssert.True(Pump(2000), "all finished");
		UnitAssert.EqualInt(5, a.n, "first yielder");
		UnitAssert.EqualInt(5, c.n, "last yielder");
		UnitAssert.EqualInt(1, counter, "once ran");
		UnitAssert.True(Tasks.Failed(bad), "trapped task failed");
		UnitAssert.True(~Tasks.Failed(a) & ~Tasks.Failed(c), "neighbours not failed")
	END TestProcDirectFinishAndTrap;

	PROCEDURE [exclusive] (r: Room) Visit, NEW;
		VAR i: INTEGER;
	BEGIN
		INC(r.inside);
		IF r.inside > 1 THEN INC(r.overlaps) END;
		FOR i := 1 TO 3 DO Tasks.Yield END;
		DEC(r.inside); INC(r.visits)
	END Visit;

	PROCEDURE (t: Visitor) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 20 DO t.room.Visit; Tasks.Yield END
	END Run;

	PROCEDURE TestProcDirectMonitorToken*;
	(** Задача уступает внутри исключающей процедуры прямо соседке; соседка
		видит свой токен, а не токен владельца, и ждёт выхода. *)
		VAR r: Room; v: ARRAY 3 OF Visitor; i: INTEGER;
	BEGIN
		NEW(r);
		FOR i := 0 TO 2 DO NEW(v[i]); v[i].room := r; Tasks.Start(v[i]) END;
		UnitAssert.True(Pump(2000), "all finished");
		UnitAssert.EqualInt(0, r.overlaps, "never two inside");
		UnitAssert.EqualInt(60, r.visits, "all visits done");
		UnitAssert.EqualInt(0, Monitors.LockDepth(), "main stack holds no monitor")
	END TestProcDirectMonitorToken;

END System_devTasks.
