MODULE System_devGcMany;
(** Сборка мусора при многих стеках: что именно терялось и что обязано жить.

	1. Объект, ссылка на который есть только на стеке спящей задачи (задач
	   тысяча, проверяются все, включая средние в реестре), переживает
	   Kernel.Collect. После сборки задачи будятся и сами проверяют поле
	   объекта: сборщик не должен ни собрать, ни испортить его.
	2. Объект, на который не ссылается никто, собирается — срабатывает
	   финализатор.
	3. Блок с заголовком «интерфейсной записи» (tag.base[0] = NIL и
	   actual > 0) переживает сборку: полный проход по куче обязан его
	   отметить. Настоящую COM-запись на Linux не создать (компилятор с
	   IMPORT COM падает), поэтому заголовок блока подделывается: адрес
	   блока пишется в фиктивный Type с base[0] = NIL, а actual > 0 — это
	   второе поле записи. Контрольная сборка с настоящим тегом обязана
	   этот объект собрать — значит, живым его держал именно проход
	   интерфейсных записей.
	4. Переполнение буфера кандидатов: > LEN(candidates) ссылок на одном
	   стеке и суммарно на многих стеках. Особый объект лежит в самом
	   начале первого буфера; при переполнении он обязан быть отмечен, а
	   не потерян вместе со сбросом буфера.

	Тесты помечены TestProc…: трогают ядро, сборщик и стеки. *)

	IMPORT SYSTEM, Kernel, Tasks, UnitAssert;

	CONST
		nSleepers = 1000;
		sleeperStack = 16 * 1024;
		bulkSlots = 70000;	(* > LEN(candidates) в ядре *)
		bulkStack = 2 * 1024 * 1024;
		manyTasks = 100;
		manySlots = 1000;
		manyStack = 64 * 1024;

	TYPE
		Payload = POINTER TO RECORD tag: LONGINT END;
		Holder = POINTER TO RECORD (Tasks.Task) id: INTEGER END;
		BulkOne = POINTER TO RECORD (Tasks.Task) END;
		BulkMany = POINTER TO RECORD (Tasks.Task) mode: INTEGER END;
		Hidden = POINTER TO RECORD tag: LONGINT END;
		Anchor = POINTER TO RECORD tag: LONGINT END;
		Garbage = POINTER TO RECORD tag: LONGINT END;
		VictimRec = RECORD a, b: SYSTEM.ADDRESS END;	(* b на месте Block.actual *)
		Victim = POINTER TO VictimRec;
		FakeType = RECORD [untagged]	(* раскладка Kernel.Type *)
			size, tpad0: INTEGER;
			mod: SYSTEM.ADDRESS;
			id, tpad1: INTEGER;
			base: ARRAY 16 OF SYSTEM.ADDRESS;
			fields: SYSTEM.ADDRESS;
			ptroffs: ARRAY 4 OF INTEGER
		END;

	VAR
		parked, bad, payloadFin, hiddenFin, garbageFin: INTEGER;
		anchor: Anchor;
		anchorAdr: SYSTEM.ADDRESS;
		fake: FakeType;
		victimFin: BOOLEAN;
		victimBlk, realTag: LONGINT;
		oneTask: BulkOne;
		manies: ARRAY manyTasks OF BulkMany;
		victimTask: BulkMany;

	PROCEDURE^ PutHidden (VAR slot: SYSTEM.ADDRESS);

	PROCEDURE (p: Payload) FINALIZE;
	BEGIN INC(payloadFin)
	END FINALIZE;

	PROCEDURE (h: Hidden) FINALIZE;
	BEGIN INC(hiddenFin)
	END FINALIZE;

	PROCEDURE (g: Garbage) FINALIZE;
	BEGIN INC(garbageFin)
	END FINALIZE;

	PROCEDURE (v: Victim) FINALIZE;
	BEGIN victimFin := TRUE
	END FINALIZE;

	PROCEDURE (t: Holder) Run;
		VAR p: Payload;
	BEGIN
		NEW(p); p.tag := t.id;
		INC(parked);
		Tasks.Park;
		(* после Park ссылка снова нужна: компилятор держит её на стеке *)
		IF (p = NIL) OR (p.tag # t.id) THEN INC(bad) END
	END Run;

	PROCEDURE (t: BulkOne) Run;
		VAR arr: ARRAY bulkSlots OF SYSTEM.ADDRESS; i: INTEGER;
	BEGIN
		FOR i := 0 TO bulkSlots - 1 DO arr[i] := anchorAdr END;
		PutHidden(arr[0]);
		INC(parked);
		Tasks.Park;
		IF arr[0] = 0 THEN INC(bad) END	(* массив обязан остаться живым *)
	END Run;

	PROCEDURE (t: BulkMany) Run;
		VAR arr: ARRAY manySlots OF SYSTEM.ADDRESS; i: INTEGER;
	BEGIN
		FOR i := 0 TO manySlots - 1 DO arr[i] := anchorAdr END;
		IF t.mode = 1 THEN PutHidden(arr[0]) END;
		INC(parked);
		Tasks.Park;
		IF arr[0] = 0 THEN INC(bad) END
	END Run;

	(* --- вспомогательное -------------------------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE WaitParked (n: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (parked < n) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN parked >= n
	END WaitParked;

	PROCEDURE PrepareAnchor;
	BEGIN
		IF anchor = NIL THEN
			NEW(anchor); anchor.tag := 1; anchorAdr := SYSTEM.ADR(anchor^)
		END
	END PrepareAnchor;

	PROCEDURE PutHidden (VAR slot: SYSTEM.ADDRESS);
	(* Объект создаётся в мёртвом кадре: единственная ссылка — slot. *)
		VAR h: Hidden;
	BEGIN
		NEW(h); h.tag := 33;
		slot := SYSTEM.ADR(h^)
	END PutHidden;

	PROCEDURE PutWord (adr, value: LONGINT);
	BEGIN
		SYSTEM.PUT(SYSTEM.VAL(SYSTEM.ADDRESS, adr), value)
	END PutWord;

	PROCEDURE GetWord (adr: LONGINT): LONGINT;
		VAR v: LONGINT;
	BEGIN
		SYSTEM.GET(SYSTEM.VAL(SYSTEM.ADDRESS, adr), v);
		RETURN v
	END GetWord;

	PROCEDURE MakeGarbage;
		VAR i: INTEGER; g: Garbage;
	BEGIN
		FOR i := 1 TO 64 DO NEW(g); g.tag := i END
	END MakeGarbage;

	PROCEDURE InitFake;
		VAR i: INTEGER;
	BEGIN
		fake.size := SIZE(VictimRec);
		fake.tpad0 := 0; fake.mod := 0; fake.id := 0; fake.tpad1 := 0;
		FOR i := 0 TO 15 DO fake.base[i] := 0 END;
		fake.fields := 0;
		FOR i := 0 TO 3 DO fake.ptroffs[i] := 0 END;
		fake.ptroffs[0] := -4	(* конец списка полей *)
	END InitFake;

	PROCEDURE MakeVictim;
		VAR v: Victim;
	BEGIN
		NEW(v); v.a := 0; v.b := 1;
		victimBlk := SYSTEM.VAL(LONGINT, SYSTEM.ADR(v^)) - SIZE(SYSTEM.ADDRESS);
		realTag := GetWord(victimBlk)
	END MakeVictim;

	(* --- тесты ------------------------------------------------------------ *)

	PROCEDURE TestProcSleepMid*;
	(** Ссылка только на стеке спящей задачи; задачи в реестре — от свежей
		(голова) до первой, середина проверяется наравне со всеми. *)
		VAR i: INTEGER; tasks: ARRAY nSleepers OF Holder;
	BEGIN
		Kernel.Collect;
		payloadFin := 0; bad := 0; parked := 0;
		FOR i := 0 TO nSleepers - 1 DO
			NEW(tasks[i]); tasks[i].id := i;
			Tasks.SetStackSize(tasks[i], sleeperStack);
			Tasks.Start(tasks[i])
		END;
		UnitAssert.True(WaitParked(nSleepers, 60000), "all 1000 tasks parked");
		Kernel.Collect; Kernel.Collect;
		UnitAssert.EqualInt(0, payloadFin,
			"object referenced only by a sleeping stack was finalized");
		FOR i := 0 TO nSleepers - 1 DO Tasks.Unpark(tasks[i]) END;
		UnitAssert.True(Pump(60000), "sleeping tasks finished after collection");
		UnitAssert.EqualInt(0, bad, "object on a sleeping stack was corrupted");
		Kernel.Collect
	END TestProcSleepMid;

	PROCEDURE TestProcGarbage*;
	(** Мусор, на который никто не ссылается, обязан собраться. *)
	BEGIN
		Kernel.Collect;
		garbageFin := 0;
		MakeGarbage;
		Kernel.Collect; Kernel.Collect;
		UnitAssert.True(garbageFin > 0, "unreferenced object was not collected")
	END TestProcGarbage;

	PROCEDURE TestProcIface*;
	(** Блок с заголовком интерфейсной записи переживает сборку; контроль
		с настоящим тегом — собирается. *)
	BEGIN
		Kernel.Collect;
		InitFake;
		UnitAssert.True(SYSTEM.VAL(LONGINT, SYSTEM.ADR(fake)) MOD 4 = 0,
			"fake type is not 4-aligned");
		MakeVictim;
		victimFin := FALSE;
		PutWord(victimBlk, SYSTEM.VAL(LONGINT, SYSTEM.ADR(fake)));
		Kernel.Collect;
		UnitAssert.True(~victimFin, "interface record (actual > 0) was collected");
		PutWord(victimBlk, realTag);
		victimFin := FALSE;
		Kernel.Collect; Kernel.Collect;
		UnitAssert.True(victimFin, "control: unreferenced record was not collected")
	END TestProcIface;

	PROCEDURE TestProcBufferOne*;
	(** Переполнение буфера кандидатов на одном стеке. *)
	BEGIN
		Kernel.Collect;
		PrepareAnchor;
		hiddenFin := 0; bad := 0; parked := 0;
		NEW(oneTask);
		Tasks.SetStackSize(oneTask, bulkStack);
		Tasks.Start(oneTask);
		UnitAssert.True(WaitParked(1, 60000), "bulk task parked");
		Kernel.Collect; Kernel.Collect;
		UnitAssert.EqualInt(0, hiddenFin,
			"object in the first candidate buffer was collected");
		Tasks.Cancel(oneTask);
		UnitAssert.True(Pump(20000), "bulk task finished");
		Kernel.Collect
	END TestProcBufferOne;

	PROCEDURE TestProcBufferMany*;
	(** Переполнение буфера кандидатов суммарно на многих стеках: особая
		задача создана последней, поэтому сканируется первой, а её объект
		лежит в самом начале первого буфера. *)
		VAR i: INTEGER;
	BEGIN
		Kernel.Collect;
		PrepareAnchor;
		hiddenFin := 0; bad := 0; parked := 0;
		FOR i := 0 TO manyTasks - 1 DO
			NEW(manies[i]); manies[i].mode := 0;
			Tasks.SetStackSize(manies[i], manyStack);
			Tasks.Start(manies[i])
		END;
		NEW(victimTask); victimTask.mode := 1;
		Tasks.SetStackSize(victimTask, manyStack);
		Tasks.Start(victimTask);
		UnitAssert.True(WaitParked(manyTasks + 1, 60000), "bulk stacks parked");
		Kernel.Collect; Kernel.Collect;
		UnitAssert.EqualInt(0, hiddenFin,
			"object in the first buffer across many stacks was collected");
		FOR i := 0 TO manyTasks - 1 DO Tasks.Cancel(manies[i]) END;
		Tasks.Cancel(victimTask);
		UnitAssert.True(Pump(30000), "bulk stacks finished");
		Kernel.Collect
	END TestProcBufferMany;

END System_devGcMany.
