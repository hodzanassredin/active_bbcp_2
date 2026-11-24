MODULE Demo_testsExamples;
(** Тесты подсистемы примеров Demo.

	Каждый пример — отдельный модуль с командой Run и детерминированным
	итогом; тест зовёт команду и проверяет счётчики. Примеры работают
	на задачах, поэтому все тесты помечены TestProc… (отдельный процесс
	через tools/unit.sh). Ants проверяется без окна через Simulate. *)

	IMPORT UnitAssert, Tasks,
		DemoBuffer, DemoPhilo, DemoReaderWriter, DemoBarber, DemoModuleMonitor,
		DemoPingPong, DemoPipeline, DemoSieve, DemoSelectTimeout,
		DemoWorkerPool, DemoBoring, DemoGroup, DemoAnts;

	PROCEDURE TestProcBoundedBuffer*;
	BEGIN
		DemoBuffer.Run;
		UnitAssert.EqualInt(120, DemoBuffer.Produced, "bounded: produced");
		UnitAssert.EqualInt(120, DemoBuffer.Consumed, "bounded: consumed");
		UnitAssert.EqualInt(2460, DemoBuffer.Sum, "bounded: sum");
		UnitAssert.True(~DemoBuffer.Failed, "bounded: no traps")
	END TestProcBoundedBuffer;

	PROCEDURE TestProcPhilosophers*;
	BEGIN
		DemoPhilo.Run;
		UnitAssert.EqualInt(15, DemoPhilo.Eaten, "philosophers: meals");
		UnitAssert.True(~DemoPhilo.Failed, "philosophers: no traps")
	END TestProcPhilosophers;

	PROCEDURE TestProcReadersWriters*;
	BEGIN
		DemoReaderWriter.Run;
		UnitAssert.EqualInt(15, DemoReaderWriter.Reads, "reader-writer: reads");
		UnitAssert.EqualInt(6, DemoReaderWriter.Writes, "reader-writer: writes");
		UnitAssert.EqualInt(0, DemoReaderWriter.Bad, "reader-writer: no overlap");
		UnitAssert.True(~DemoReaderWriter.Failed, "reader-writer: no traps")
	END TestProcReadersWriters;

	PROCEDURE TestProcBarber*;
	BEGIN
		DemoBarber.Run;
		UnitAssert.EqualInt(3, DemoBarber.Served, "barber: served");
		UnitAssert.EqualInt(3, DemoBarber.TurnedAway, "barber: turned away");
		UnitAssert.True(~DemoBarber.Failed, "barber: no traps")
	END TestProcBarber;

	PROCEDURE TestProcModuleMonitor*;
	BEGIN
		DemoModuleMonitor.Run;
		UnitAssert.EqualInt(120, DemoModuleMonitor.Produced, "module monitor: produced");
		UnitAssert.EqualInt(120, DemoModuleMonitor.Consumed, "module monitor: consumed");
		UnitAssert.EqualInt(7260, DemoModuleMonitor.Sum, "module monitor: sum");
		UnitAssert.True(~DemoModuleMonitor.Failed, "module monitor: no traps")
	END TestProcModuleMonitor;

	PROCEDURE TestProcPingPong*;
	BEGIN
		DemoPingPong.Run;
		UnitAssert.EqualInt(10, DemoPingPong.Hits, "ping-pong: hits");
		UnitAssert.EqualInt(45, DemoPingPong.Sum, "ping-pong: sum");
		UnitAssert.True(~DemoPingPong.Failed, "ping-pong: no traps")
	END TestProcPingPong;

	PROCEDURE TestProcPipeline*;
	BEGIN
		DemoPipeline.Run;
		UnitAssert.EqualInt(10, DemoPipeline.Count, "pipeline: count");
		UnitAssert.EqualInt(385, DemoPipeline.Sum, "pipeline: sum of squares");
		UnitAssert.True(~DemoPipeline.Failed, "pipeline: no traps")
	END TestProcPipeline;

	PROCEDURE TestProcSieve*;
	BEGIN
		DemoSieve.Run;
		UnitAssert.EqualInt(25, DemoSieve.Primes, "sieve: number of primes");
		UnitAssert.EqualInt(1060, DemoSieve.Sum, "sieve: sum of primes");
		UnitAssert.EqualInt(97, DemoSieve.MaxPrime, "sieve: largest prime");
		UnitAssert.True(~DemoSieve.Failed, "sieve: no traps")
	END TestProcSieve;

	PROCEDURE TestProcSelectTimeout*;
	BEGIN
		DemoSelectTimeout.Run;
		UnitAssert.EqualInt(1, DemoSelectTimeout.TimeoutIdx, "select: timeout wins");
		UnitAssert.True(DemoSelectTimeout.TimeoutElapsed >= 20, "select: timeout waited");
		UnitAssert.EqualInt(0, DemoSelectTimeout.GotIdx, "select: receive wins");
		UnitAssert.EqualInt(42, DemoSelectTimeout.GotValue, "select: value");
		UnitAssert.True(~DemoSelectTimeout.Failed, "select: no traps")
	END TestProcSelectTimeout;

	PROCEDURE TestProcWorkerPool*;
	BEGIN
		DemoWorkerPool.Run;
		UnitAssert.EqualInt(3, DemoWorkerPool.Workers, "worker pool: workers");
		UnitAssert.EqualInt(9, DemoWorkerPool.Count, "worker pool: results");
		UnitAssert.EqualInt(285, DemoWorkerPool.Sum, "worker pool: sum");
		UnitAssert.True(~DemoWorkerPool.Failed, "worker pool: no traps")
	END TestProcWorkerPool;

	PROCEDURE TestProcBoring*;
	BEGIN
		DemoBoring.Run;
		UnitAssert.EqualInt(16, DemoBoring.Messages, "boring: messages");
		UnitAssert.EqualInt(8, DemoBoring.FromA, "boring: from A");
		UnitAssert.EqualInt(8, DemoBoring.FromB, "boring: from B");
		UnitAssert.True(~DemoBoring.Failed, "boring: no traps")
	END TestProcBoring;

	PROCEDURE TestProcGroupCancel*;
	BEGIN
		DemoGroup.Run;
		UnitAssert.EqualInt(1, DemoGroup.Errors, "group: one error");
		UnitAssert.EqualInt(1, DemoGroup.FailedWorker, "group: one trapped member");
		UnitAssert.EqualInt(3, DemoGroup.Cancelled, "group: three members cancelled");
		UnitAssert.EqualInt(0, DemoGroup.Completed, "group: none completed");
		UnitAssert.EqualInt(0, DemoGroup.GroupAlive, "group: empty");
		UnitAssert.True(DemoGroup.GroupIsFailed, "group: marked failed");
		UnitAssert.True(~DemoGroup.Failed, "group: run itself did not fail")
	END TestProcGroupCancel;

	PROCEDURE TestProcAnts*;
		VAR food: LONGINT;
	BEGIN
		food := DemoAnts.Simulate(1000, 300);
		UnitAssert.True(food > 0, "ants: food brought home");
		UnitAssert.EqualInt(1000, DemoAnts.AntCount(), "ants: count preserved");
		UnitAssert.EqualInt(0, DemoAnts.Left, "ants: all finished");
		UnitAssert.True(~DemoAnts.Failed, "ants: no traps");
		UnitAssert.True(~DemoAnts.Deadlocked, "ants: no deadlock")
	END TestProcAnts;

	PROCEDURE TestProcAntsWalls*;
	(** Стена непроходима: за прогон ни один муравей не оказался в стене,
		хотя стены реально мешали движению. *)
		VAR food: LONGINT;
	BEGIN
		food := DemoAnts.Simulate(200, 400);
		UnitAssert.True(DemoAnts.WallCount() >= 3, "ants: at least three walls");
		UnitAssert.EqualInt(0, DemoAnts.AntsInWalls(), "ants: no ant inside a wall");
		UnitAssert.EqualInt(0, DemoAnts.WallViolations(), "ants: wall never entered");
		UnitAssert.True(DemoAnts.WallHits() > 0, "ants: walls blocked moves")
	END TestProcAntsWalls;

	PROCEDURE TestProcAntsReachable*;
	(** Каждая куча еды достижима из гнезда по свободным клеткам. *)
	BEGIN
		DemoAnts.Reset;
		UnitAssert.True(DemoAnts.FoodReachable(), "ants: every food patch reachable")
	END TestProcAntsReachable;

	PROCEDURE TestProcAntsPauseStep*;
	(** Pause + Step: ровно один шаг каждому муравью. *)
		VAR food, m0, m1: LONGINT;
	BEGIN
		food := DemoAnts.Simulate(10, 3);
		DemoAnts.Start;
		DemoAnts.Pause;
		m0 := DemoAnts.Moves();
		DemoAnts.Step;
		m1 := DemoAnts.Moves();
		UnitAssert.EqualInt(SHORT(m0) + 10, SHORT(m1), "ants: Step is one round");
		DemoAnts.Stop
	END TestProcAntsPauseStep;

	PROCEDURE TestProcAntsSpeed*;
		VAR i: INTEGER;
	BEGIN
		DemoAnts.SetSpeed(10);
		FOR i := 1 TO 20 DO DemoAnts.Faster END;
		UnitAssert.EqualInt(200, DemoAnts.Speed(), "ants: speed upper bound");
		FOR i := 1 TO 20 DO DemoAnts.Slower END;
		UnitAssert.EqualInt(1, DemoAnts.Speed(), "ants: speed lower bound");
		DemoAnts.SetSpeed(10);
		UnitAssert.EqualInt(10, DemoAnts.Speed(), "ants: speed restored")
	END TestProcAntsSpeed;

	PROCEDURE TestProcAntsFoodWalls*;
	(** Еда доставляется и при стенах: тропа огибает стену. *)
		VAR food: LONGINT;
	BEGIN
		food := DemoAnts.Simulate(200, 1200);
		UnitAssert.True(food > 0, "ants: food delivered around walls");
		UnitAssert.EqualInt(0, DemoAnts.AntsInWalls(), "ants: no ant inside a wall")
	END TestProcAntsFoodWalls;

	PROCEDURE TestProcAntsMany*;
	(** 1000 муравьёв завершаются и снимаются: задач в системе не остаётся. *)
		VAR food: LONGINT;
	BEGIN
		food := DemoAnts.Simulate(1000, 40);
		UnitAssert.EqualInt(1000, DemoAnts.AntCount(), "ants many: count preserved");
		UnitAssert.EqualInt(0, DemoAnts.Left, "ants many: all finished");
		UnitAssert.EqualInt(0, Tasks.Count(), "ants many: no leftover tasks");
		UnitAssert.True(~DemoAnts.Failed, "ants many: no traps");
		UnitAssert.True(~DemoAnts.Deadlocked, "ants many: no deadlock")
	END TestProcAntsMany;

END Demo_testsExamples.
