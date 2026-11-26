MODULE System_devTasksSim;
(** Случайные расписания для ядра задач: проверка монитора-объекта и пропуска
	по простой модели.

	По зерну строится программа из K задач (2..12) и M мониторов (1..4):
	каждая задача исполняет свою последовательность действий. Программа
	каждой задачи планируется заранее по своему генератору, поэтому зерно
	однозначно задаёт набор действий; порядок исполнения задаёт
	планировщик ядра. Действия: Do со счётчиком и повторным входом, Put и
	Get в буфер ограниченного размера (флаги и AwaitCond), ожидание флагов,
	вложенный Do на монитор с большим номером, Yield, Sleep, отмена другой
	задачи, Spawn/JoinAll, повторный Start завершённой задачи, пойманный и
	непойманный трап.

	После каждого шага планировщика и в конце прогона проверяются
	инварианты: не больше одного владельца монитора; TRUE из Await видит
	свой флаг в Ready(); FALSE — только отмена или срок; нет потерянного
	пробуждения; FIFO ждущих одного условия — на монотонном флаге (у
	изменчивого условия более ранний ждущий мог стать готов уже после
	выбора, и проверка порядка дала бы ложный провал); сумма положенного
	равна сумме взятого плюс остаток; счётчики монитора равны модели;
	задачи завершаются, JoinAll не возвращается раньше детей,
	Tasks.Count() в конце равен начальному. В каждом прогоне первые
	задачи встают в очередь на монотонный флаг, следующая его ставит
	трапом на отпускании: так FIFO проверяется в каждом прогоне с K >= 4,
	а кадр ожидания, снятый трапом в окне между постановкой в очередь и
	сном, попадает в AbandonWait. Тогда же монитор передаётся ждущему; при
	четырёх ждущих его сразу отменяют до первого шага (класс «остановлен
	после передачи»), а вложенный Do на том же мониторе продолжается Await с
	проверкой «я один внутри». Трап на отпускании случается, только если
	в очереди флагов есть ждущий: ChooseFlag тогда зовёт Ready(), и второй
	её вызов приходится на отпускание. Покрытие путей (TRUE/FALSE из
	Await и AwaitCond, отмена, Spawn, JoinAll, повторный Start, оба вида
	трапа, вложенный Do, FIFO, трап на отпускании, остановка после
	передачи, Await после вложенного Do) проверяется в конце теста.
	Две пробы не входят в случайные планы и проверяются отдельно: готовый
	ждущий получает монитор раньше входящего из очереди входа, а ожидание
	без срока завершается отменой за ограниченное число шагов, а не
	следующим событием.

	Часы: подменяемых в ядре нет, сроки идут по Kernel.Time(). Сама
	программа от зерна не зависит от времени, но чередование задач и
	моменты истечения сроков — зависят: прогон с тем же зерном повторяет
	набор действий, а не точный порядок.

	Ошибка печатает зерно, шаг и команду повтора:
		System_devTasksSim.Replay 42
	Для режима со сборщиком — Replay g42. Один прогон из теста —
	One(seed, gc). *)

	IMPORT S := SYSTEM, Kernel, Tasks, UnitAssert, Strings, Console,
		DevCommanders, TextModels;

	CONST
		maxTasks = 12;
		maxMon = 4;
		maxActs = 48;
		maxKids = 64;
		cap = 3;
		maxWaiters = 96;
		kidBase = 100;
		topStack = 64 * 1024;
		kidStack = 32 * 1024;

		(* виды действий *)
		aInc = 0; aPut = 1; aGet = 2; aWait = 3; aCond = 4; aFlag = 5;
		aYield = 6; aSleep = 7; aCancel = 8; aSpawn = 9; aJoin = 10;
		aRestart = 11; aTrap = 12; aTrapRel = 13; aNestAwait = 14;
		(* Пробы приоритета: aGateYield ставит затвор и уступает, не отпуская
		   монитор; aTouch входит и метит порядок; aWaitPrio ждёт затвор без
		   срока и метит пробуждение; aWaitForever ждёт без срока условие {3}. *)
		aGateYield = 15; aTouch = 16; aWaitPrio = 17; aWaitForever = 18;

		(* вид очереди в зеркале ждущих *)
		wFlag = 1; wCond = 2;

	TYPE
		Payload = POINTER TO RECORD tag: LONGINT END;

		SimCleaner = POINTER TO RECORD (Kernel.TrapCleaner)
			id: INTEGER
		END;

		(* Задача симуляции. Набор действий и параметры планируются до
		   запуска; rng задачи влияет только на план. *)
		SimTask = POINTER TO RECORD (Tasks.Job)
			id: INTEGER;
			rng: LONGINT;
			nacts: INTEGER;
			acts: ARRAY maxActs OF INTEGER;
			p1, p2, p3: ARRAY maxActs OF INTEGER;
			runs, gen: INTEGER;
			prologueDone: BOOLEAN;	(* проба затвора уже была в этом прогоне *)
			cleaner: SimCleaner
		END;

		Waiter = RECORD
			task: SimTask;
			seq: INTEGER;
			kind: INTEGER;		(* wFlag или wCond *)
			need: SET;
			condIdx: INTEGER;
			active: BOOLEAN
		END;

		(* Условие-объект буфера: 0 — «не пуст», 1 — «не полон». *)
		BufCond = POINTER TO RECORD (Tasks.Cond)
			idx: INTEGER
		END;

		(* Монитор-объект: счётчик, буфер и флаги. inside/insideTask —
		   собственный счётчик «кто внутри» рядом с Tasks.Held/Depth. *)
		Mon = POINTER TO RECORD (Tasks.Object)
			idx: INTEGER;
			transfer: BOOLEAN;
			counter: INTEGER;
			flag2: BOOLEAN;
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER;
			putSeq, lastGot, puts, gets: INTEGER;
			inside, insideGen: INTEGER;
			insideTask: SimTask;
			ws: ARRAY maxWaiters OF Waiter;
			nw: INTEGER;
			(* трап на отпускании: arm — через сколько вызовов Ready;
			   releaseTrapped — трап случился (кадры ожидания сняты) *)
			arm: INTEGER;
			releaseTrapped: BOOLEAN;
			cond: ARRAY 2 OF BufCond
		END;

		IncOp = RECORD (Tasks.Op)
			nested, sameMon: BOOLEAN;
			other: Mon;
			nOuter, nInner: INTEGER;
			innerIdx: INTEGER;
			done: BOOLEAN
		END;

		PutOp = RECORD (Tasks.Op)
			dms: INTEGER;
			ok: BOOLEAN;
			item: INTEGER
		END;

		GetOp = RECORD (Tasks.Op)
			dms: INTEGER;
			ok: BOOLEAN;
			item: INTEGER
		END;

		WaitOp = RECORD (Tasks.Op)
			need: SET;
			dms: INTEGER;
			ok: BOOLEAN
		END;

		CondWaitOp = RECORD (Tasks.Op)
			condIdx, dms: INTEGER;
			ok: BOOLEAN
		END;

		FlagOp = RECORD (Tasks.Op)
			val: BOOLEAN
		END;

		TrapCaughtOp = RECORD (Tasks.Op)
			y: Mon;
			ok: BOOLEAN;
			heldAfter: INTEGER
		END;

		CrashOp = RECORD (Tasks.Op) END;

		(* Трап в Ready() на отпускании монитора внутри Await: кадр ожидания
		   снят, задача продолжает работу вне Do. *)
		TrapRelOp = RECORD (Tasks.Op)
			dms: INTEGER
		END;

		(* Вложенный Do на том же мониторе, затем Await во внешнем теле и
		   проверка «я один внутри» после пробуждения. *)
		NestAwaitOp = RECORD (Tasks.Op)
			dms: INTEGER;
			nOuter, nInner: INTEGER;
			woke: BOOLEAN
		END;

		(* Проба приоритета: вошедшая задача ставит метку порядка. *)
		TouchOp = RECORD (Tasks.Op) mark: INTEGER END;

		(* Проба приоритета: поставить затвор, не отпуская монитор, и уступить. *)
		GateOp = RECORD (Tasks.Op) END;

		CtxPtr = POINTER TO Tasks.Ctx;

	VAR
		(* прогон *)
		seedN: LONGINT;
		stepNo, violations: INTEGER;
		gcMode, failed, reported, bodyDone, stopDone: BOOLEAN;
		baseCount, nTasks, nMon, waitSeq, restartsDone, plannedKid: INTEGER;
		fifoChecks: INTEGER;
		planRng, harnessRng: LONGINT;
		tasks: ARRAY maxTasks OF SimTask;
		taskMade: ARRAY maxTasks OF BOOLEAN;
		kids: ARRAY maxKids OF SimTask;
		kidMade: ARRAY maxKids OF BOOLEAN;
		mons: ARRAY maxMon OF Mon;
		(* простая модель: суммы по мониторам, обычные переменные *)
		modelInc, modelPut, modelGet: ARRAY maxMon OF INTEGER;
		(* трап внутри Exclusive *)
		trapY: Mon;
		trapCtx: S.ADDRESS;
		trapHit: BOOLEAN;
		trapRelDms: INTEGER;
		(* монитор, отданный ждущему, которого остановили до первого шага *)
		handedStopTarget: SimTask;
		(* наблюдение брошенного ожидания: монитор уже передан *)
		abandonSeen, abandonHeld: BOOLEAN;
		abandonDepth: INTEGER;
		(* покрытие путей за прогон теста *)
		covAwaitT, covAwaitF, covCondT, covCondF, covCancel, covSpawn,
		covJoin, covRestart, covCaught, covUncaught, covNested,
		covTrapRel, covHandedStop, covNestAwait: INTEGER;
		(* порядок проб приоритета: 1 — проснулся готовый ждущий,
		   2 — вошёл входящий, 9 — ждущего разбудили без условия *)
		prioLog: ARRAY 8 OF INTEGER;
		prioN: INTEGER;

	PROCEDURE^ PlanChild (c: SimTask; sd: LONGINT; parentId, actIdx, id: INTEGER);
	PROCEDURE^ KidAt (slot: INTEGER): SimTask;

	(* -------------------- сообщения и проверки ---------------------------- *)

	PROCEDURE App (VAR s: ARRAY OF CHAR; IN x: ARRAY OF CHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		j := 0;
		WHILE (i < LEN(s) - 1) & (j < LEN(x)) & (x[j] # 0X) DO s[i] := x[j]; INC(i); INC(j) END;
		s[i] := 0X
	END App;

	PROCEDURE AppNum (VAR s: ARRAY OF CHAR; n: LONGINT);
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(n, d); App(s, d)
	END AppNum;

	PROCEDURE Prio (x: INTEGER);
	BEGIN
		IF prioN < LEN(prioLog) THEN prioLog[prioN] := x; INC(prioN) END
	END Prio;

	PROCEDURE ResetPrio;
	BEGIN
		prioN := 0
	END ResetPrio;

	PROCEDURE ResetCov;
	BEGIN
		covAwaitT := 0; covAwaitF := 0; covCondT := 0; covCondF := 0;
		covCancel := 0; covSpawn := 0; covJoin := 0; covRestart := 0;
		covCaught := 0; covUncaught := 0; covNested := 0;
		covTrapRel := 0; covHandedStop := 0; covNestAwait := 0;
		handedStopTarget := NIL;
		abandonSeen := FALSE; abandonHeld := FALSE; abandonDepth := -1;
		fifoChecks := 0
	END ResetCov;

	PROCEDURE Violation (IN what: ARRAY OF CHAR);
	(** Первый провал прогона: зерно, шаг, инвариант и команда повтора. *)
		VAR s: UnitAssert.Message;
	BEGIN
		INC(violations); failed := TRUE;
		IF ~reported THEN
			reported := TRUE;
			s := "seed "; AppNum(s, seedN); App(s, ", step "); AppNum(s, stepNo);
			App(s, ": "); App(s, what);
			App(s, "; repeat: System_devTasksSim.Replay ");
			IF gcMode THEN App(s, "g") END;
			AppNum(s, seedN);
			UnitAssert.Fail(s)
		END
	END Violation;

	PROCEDURE NilCrash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END NilCrash;

	(* -------------------- генератор -------------------------------------- *)

	PROCEDURE RandK (VAR x: LONGINT; k: INTEGER): INTEGER;
	BEGIN
		x := x * 6364136223846793005 + 1442695040888963407;
		IF k <= 0 THEN RETURN 0 END;
		RETURN SHORT(ASH(x, -33) MOD k)
	END RandK;

	PROCEDURE Mix (a, b: LONGINT): LONGINT;
		VAR x: LONGINT;
	BEGIN
		x := a * 2654435761 + b * 40503 + 12345;
		x := x * 6364136223846793005 + 1442695040888963407;
		RETURN x
	END Mix;

	(* -------------------- кто сейчас и очередь зеркала -------------------- *)

	PROCEDURE CurTask (): SimTask;
		VAR t: Tasks.Task;
	BEGIN
		t := Tasks.Current();
		IF (t # NIL) & (t IS SimTask) THEN RETURN t(SimTask) END;
		RETURN NIL
	END CurTask;

	PROCEDURE CompactWs (o: Mon);
		VAR i, n: INTEGER;
	BEGIN
		n := 0;
		FOR i := 0 TO o.nw - 1 DO
			IF o.ws[i].active THEN o.ws[n] := o.ws[i]; INC(n) END
		END;
		o.nw := n
	END CompactWs;

	PROCEDURE AddWaiter (o: Mon; kind, condIdx: INTEGER; need: SET): INTEGER;
		VAR w: Waiter; sq: INTEGER;
	BEGIN
		IF o.nw >= maxWaiters THEN CompactWs(o) END;
		IF o.nw >= maxWaiters THEN
			Violation("test limit: too many waiters on one monitor"); RETURN waitSeq
		END;
		sq := waitSeq; INC(waitSeq);
		w.task := CurTask(); w.seq := sq; w.kind := kind;
		w.need := need; w.condIdx := condIdx; w.active := TRUE;
		o.ws[o.nw] := w; INC(o.nw);
		RETURN sq
	END AddWaiter;

	PROCEDURE RemoveWaiter (o: Mon; kind, condIdx: INTEGER);
		VAR i: INTEGER; t: SimTask;
	BEGIN
		t := CurTask();
		FOR i := o.nw - 1 TO 0 BY -1 DO
			IF o.ws[i].active & (o.ws[i].task = t) & (o.ws[i].kind = kind)
					& (o.ws[i].condIdx = condIdx) THEN
				o.ws[i].active := FALSE; RETURN
			END
		END
	END RemoveWaiter;

	PROCEDURE WaiterReadyIdx (o: Mon; i: INTEGER): BOOLEAN;
		VAR w: Waiter;
	BEGIN
		IF (i < 0) OR (i >= o.nw) THEN RETURN FALSE END;
		w := o.ws[i];
		IF w.kind = wFlag THEN RETURN (w.need * o.Ready()) # {}
		ELSE RETURN o.cond[w.condIdx].Ready(o)
		END
	END WaiterReadyIdx;

	PROCEDURE ActiveWaiters (o: Mon): INTEGER;
		VAR i, n: INTEGER;
	BEGIN
		n := 0;
		FOR i := 0 TO o.nw - 1 DO IF o.ws[i].active THEN INC(n) END END;
		RETURN n
	END ActiveWaiters;

	PROCEDURE Blocked (t: SimTask): BOOLEAN;
		VAR st: INTEGER;
	BEGIN
		IF t = NIL THEN RETURN FALSE END;
		st := Tasks.State(t);
		RETURN (st = Tasks.Waiting) OR (st = Tasks.Sleeping) OR (st = Tasks.Parked)
	END Blocked;

	PROCEDURE CheckGateFifo (o: Mon; mySeq: INTEGER);
	(** Порядок ждущих одного условия: первым будится вставший раньше.

		Проверяется только монотонный «затвор» — флаг 2, который ставится
		один раз и не снимается. У изменчивого условия (место в буфере,
		предмет) состояние могло измениться между выбором ждущего и его
		возвратом, и тогда более ранний ждущий стал готов уже после выбора:
		это не ошибка порядка. У затвора условие постоянно, поэтому если
		задача вернулась из ожидания, а более ранний ждущий затвора всё ещё
		стоит в очереди, ядро выбрало не первого готового. *)
		VAR i, peers: INTEGER; w: Waiter;
	BEGIN
		peers := 0;
		FOR i := 0 TO o.nw - 1 DO
			w := o.ws[i];
			IF w.active & (w.kind = wFlag) & (w.need = {2}) & (w.task # NIL) THEN
				INC(peers);
				(* Только Sleeping: разбуженный ждущий, который ещё берёт
				   монитор, стоит в очереди входа, а не условия. *)
				IF (w.seq < mySeq) & (Tasks.State(w.task) = Tasks.Sleeping) THEN
					IF o.flag2 THEN
						Violation("FIFO: a later gate waiter was woken before an earlier ready gate waiter")
					END
				END
			END
		END;
		IF peers > 1 THEN INC(fifoChecks) END
	END CheckGateFifo;

	(* -------------------- зеркало внутри монитора ------------------------- *)

	PROCEDURE Bool (b: BOOLEAN): INTEGER;
	BEGIN
		IF b THEN RETURN 1 ELSE RETURN 0 END
	END Bool;

	PROCEDURE RepairMirrors;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nMon - 1 DO
			IF (mons[i] # NIL) & (mons[i].inside > 0) & ~Tasks.Held(mons[i]) THEN
				mons[i].inside := 0; mons[i].insideTask := NIL; mons[i].insideGen := 0
			END
		END
	END RepairMirrors;

	PROCEDURE RepairTask (id: INTEGER);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nMon - 1 DO
			IF (mons[i] # NIL) & (mons[i].insideTask # NIL)
					& (mons[i].insideTask.id = id) & ~Tasks.Held(mons[i]) THEN
				mons[i].inside := 0; mons[i].insideTask := NIL; mons[i].insideGen := 0
			END
		END
	END RepairTask;

	PROCEDURE RepairOne (m: Mon);
	(* Кадр Do этого монитора снят трапом: зеркало недействительно, даже
	   если монитор уже передан другой задаче. *)
	BEGIN
		IF m # NIL THEN
			m.inside := 0; m.insideTask := NIL; m.insideGen := 0
		END
	END RepairOne;

	PROCEDURE (c: SimCleaner) Cleanup;
	BEGIN
		RepairTask(c.id)
	END Cleanup;

	(* -------------------- методы монитора --------------------------------- *)

	PROCEDURE (c: BufCond) Ready (o: Tasks.Object): BOOLEAN;
		VAR m: Mon;
	BEGIN
		m := o(Mon);
		IF c.idx = 0 THEN RETURN m.count > 0 ELSE RETURN m.count < cap END
	END Ready;

	PROCEDURE (o: Mon) Ready (): SET;
		VAR s: SET;
	BEGIN
		(* Задуманный трап на отпускании: arm считает вызовы Ready после
		   взвода. Первый — проверка условия в Await, второй — ChooseFlag
		   на отпускании, когда в очереди флагов есть ждущие. *)
		IF o.arm > 0 THEN
			DEC(o.arm);
			IF o.arm = 0 THEN
				o.arm := -1; o.releaseTrapped := TRUE;
				NilCrash
			END
		END;
		s := {};
		IF o.count < cap THEN INCL(s, 0) END;
		IF o.count > 0 THEN INCL(s, 1) END;
		IF o.flag2 THEN INCL(s, 2) END;
		RETURN s
	END Ready;

	PROCEDURE (o: Mon) HoldsWhileWaiting (): BOOLEAN;
	(* Вложенный Do на другой монитор — законное ожидание с монитором в руках. *)
	BEGIN
		RETURN TRUE
	END HoldsWhileWaiting;

	PROCEDURE (o: Mon) TransferOnRelease (): BOOLEAN;
	BEGIN
		RETURN o.transfer
	END TransferOnRelease;

	PROCEDURE DoWait (o: Mon; VAR ctx: Tasks.Ctx; useCond: BOOLEAN;
		condIdx: INTEGER; need: SET; OUT ok: BOOLEAN);
	(* Ожидание внутри Exclusive: монитор ядру отпускается, зеркало
	   «кто внутри» уменьшается; после возврата монитор снова у задачи. *)
		VAR sq, kind: INTEGER; s0, s1: LONGINT;
	BEGIN
		IF useCond THEN kind := wCond ELSE kind := wFlag END;
		sq := AddWaiter(o, kind, condIdx, need);
		DEC(o.inside);
		s0 := Tasks.Switches();
		IF useCond THEN ok := o.AwaitCond(ctx, o.cond[condIdx]) ELSE ok := o.Await(ctx, need) END;
		s1 := Tasks.Switches();
		(* Монитор снова у нас: внутри не может быть никого, кроме нас.
		   Положительный счётчик — остаток от снятого трапом кадра. *)
		IF o.inside > 0 THEN
			IF (o.insideTask # NIL) & (o.insideGen = o.insideTask.gen)
					& (Tasks.State(o.insideTask) IN {Tasks.Ready, Tasks.Running,
						Tasks.Waiting, Tasks.Sleeping, Tasks.Parked}) THEN
				Violation("Await: another live task was inside the monitor at return")
			END;
			o.inside := 0; o.insideTask := NIL; o.insideGen := 0
		END;
		o.insideTask := CurTask();
		IF o.insideTask # NIL THEN o.insideGen := o.insideTask.gen END;
		INC(o.inside);
		RemoveWaiter(o, kind, condIdx);
		IF ok THEN
			(* Проснувшаяся задача сама перепроверяет условие: TRUE из Await
			   обязан означать, что оно истинно. Ложный TRUE превращаем в
			   FALSE, иначе Put/Get зациклились бы на нём без уступки. *)
			IF useCond THEN
				IF o.cond[condIdx].Ready(o) THEN ok := TRUE ELSE ok := FALSE END
			ELSE
				IF (need * o.Ready()) # {} THEN ok := TRUE ELSE ok := FALSE END
			END;
			IF ~ok THEN
				Violation("Await: returned TRUE but the condition is false")
			END
		END;
		IF ~ok & (CurTask() = handedStopTarget) THEN
			(* Монитор передан этой задаче, но она остановлена раньше, чем
			   успела продолжить: Await вернул FALSE, монитор остался у неё. *)
			handedStopTarget := NIL;
			IF ~Tasks.Held(o) THEN
				Violation("stopped after handoff: the monitor is not held")
			END;
			INC(covHandedStop)
		END;
		IF ok & (s1 # s0) & (kind = wFlag) & (need = {2}) THEN CheckGateFifo(o, sq) END
	END DoWait;

	PROCEDURE AnyFlagWaiter (o: Mon): BOOLEAN;
	(* Спит ли ждущий по флагам: тогда его запись в очереди флагов ядра есть,
	   и ChooseFlag на отпускании вызовет Ready(). Разбуженный, но ещё не
	   продолживший ждущий уже снят из очереди ядра и не годится. *)
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO o.nw - 1 DO
			IF o.ws[i].active & (o.ws[i].kind = wFlag) & (o.ws[i].task # NIL)
					& (Tasks.State(o.ws[i].task) = Tasks.Sleeping) THEN
				RETURN TRUE
			END
		END;
		RETURN FALSE
	END AnyFlagWaiter;

	PROCEDURE GateWaiter (o: Mon; awake: BOOLEAN): SimTask;
	(* Первый в зеркале ждущий затвора {2}; awake — только уже разбуженный. *)
		VAR i: INTEGER; t: SimTask; st: INTEGER;
	BEGIN
		FOR i := 0 TO o.nw - 1 DO
			IF o.ws[i].active & (o.ws[i].kind = wFlag) & (o.ws[i].need = {2})
					& (o.ws[i].task # NIL) THEN
				t := o.ws[i].task; st := Tasks.State(t);
				IF ~awake OR (st IN {Tasks.Ready, Tasks.Running}) THEN RETURN t END
			END
		END;
		RETURN NIL
	END GateWaiter;

	PROCEDURE LostWakeup (o: Mon): BOOLEAN;
	(* Монитор свободен, а ждущий с истинным условием всё ещё спит: разбудить
	   его может только новое отпускание, а если его не будет — пробуждение
	   потеряно. *)
		VAR i: INTEGER;
	BEGIN
		IF Tasks.Held(o) THEN RETURN FALSE END;
		FOR i := 0 TO o.nw - 1 DO
			IF o.ws[i].active & (o.ws[i].task # NIL)
					& (Tasks.State(o.ws[i].task) = Tasks.Sleeping)
					& WaiterReadyIdx(o, i) THEN
				RETURN TRUE
			END
		END;
		RETURN FALSE
	END LostWakeup;

	PROCEDURE TrapRelBody (adr, b, c: S.ADDRESS);
	(* Тело под Kernel.Try: Do, чей Await трапует в Ready() на отпускании. *)
		VAR p: CtxPtr; op: TrapRelOp;
	BEGIN
		p := S.VAL(CtxPtr, adr);
		op.dms := trapRelDms;
		trapY.Do(p^, op)
	END TrapRelBody;

	PROCEDURE SimTrapHandler (adr, b, c: S.ADDRESS);
	(* Трап внутри внутреннего Do: внешний монитор остаётся, внутренний
	   отпускает Heal по отметкам кадров. *)
		VAR p: CtxPtr; y: Mon; op: CrashOp;
	BEGIN
		p := S.VAL(CtxPtr, adr);
		IF p^.Stopped() THEN RETURN END;
		trapHit := TRUE;
		y := trapY;
		IF y # NIL THEN
			Tasks.SetDeadline(p^, 2);
			y.Do(p^, op)
		END;
		NilCrash
	END SimTrapHandler;

	PROCEDURE (o: Mon) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR d: INTEGER; cur: SimTask; inner: IncOp;
			ok, woke: BOOLEAN; it: INTEGER;
	BEGIN
		cur := CurTask();
		(* Зеркало сверяется с ядром: у снятого трапом кадра запись могла
		   остаться. Разные живые задачи внутри одного монитора — ошибка. *)
		d := Tasks.Depth(o);
		IF o.inside # d - 1 THEN
			IF (o.inside > 0) & (o.insideTask # cur) & (o.insideTask # NIL)
					& (o.insideGen = o.insideTask.gen)
					& (Tasks.State(o.insideTask) IN {Tasks.Ready, Tasks.Running,
						Tasks.Waiting, Tasks.Sleeping, Tasks.Parked}) THEN
				Violation("monitor: two different live tasks are inside (more than one owner)")
			END;
			IF d <= 1 THEN
				o.inside := 0; o.insideTask := cur
			ELSE
				o.inside := d - 1
			END
		END;
		IF (o.inside > 0) & (o.insideTask # cur) THEN
			Violation("monitor: inside by one task, entered by another")
		END;
		o.insideTask := cur; o.insideGen := cur.gen; INC(o.inside);

		WITH op: IncOp DO
			op.nOuter := 0; op.nInner := 0; op.innerIdx := -1;
			(* Вложенный Do — до своего увеличения: он может уснуть, а модель
			   обновляется после возврата из внешнего Do. *)
			IF op.nested THEN
				inner.nested := FALSE; inner.sameMon := FALSE; inner.other := NIL;
				inner.nOuter := 0; inner.nInner := 0; inner.innerIdx := -1; inner.done := FALSE;
				IF op.sameMon THEN
					o.Do(ctx, inner);
					op.innerIdx := o.idx
				ELSE
					Tasks.SetDeadline(ctx, 3);
					op.other.Do(ctx, inner);
					op.innerIdx := op.other.idx; INC(covNested)
				END;
				IF inner.done THEN op.nInner := 1 END
			END;
			INC(o.counter); op.nOuter := 1;
			op.done := TRUE
		| op: PutOp DO
			op.ok := FALSE;
			ok := TRUE;
			LOOP
				IF o.count < cap THEN EXIT END;
				Tasks.SetDeadline(ctx, op.dms);
				DoWait(o, ctx, FALSE, 0, {0}, woke);
				IF ~woke THEN ok := FALSE; EXIT END
			END;
			IF ok THEN
				INC(o.putSeq); it := o.putSeq;
				o.ring[o.tail] := it; o.tail := (o.tail + 1) MOD cap; INC(o.count);
				INC(o.puts);
				op.item := it; op.ok := TRUE
			END
		| op: GetOp DO
			op.ok := FALSE;
			ok := TRUE;
			LOOP
				IF o.count > 0 THEN EXIT END;
				Tasks.SetDeadline(ctx, op.dms);
				DoWait(o, ctx, FALSE, 0, {1}, woke);
				IF ~woke THEN ok := FALSE; EXIT END
			END;
			IF ok THEN
				it := o.ring[o.head];
				IF it # o.lastGot + 1 THEN
					Violation("buffer: an item left the buffer out of put order")
				END;
				o.lastGot := it;
				o.head := (o.head + 1) MOD cap; DEC(o.count); INC(o.gets);
				op.item := it; op.ok := TRUE
			END
		| op: WaitOp DO
			op.ok := FALSE;
			IF op.dms > 0 THEN Tasks.SetDeadline(ctx, op.dms) END;
			DoWait(o, ctx, FALSE, 0, op.need, woke);
			op.ok := woke;
			IF woke THEN
				INC(covAwaitT)
			ELSE
				INC(covAwaitF);
				IF ~(ctx.cancelled OR ctx.expired) THEN
					Violation("Await: returned FALSE without cancellation or deadline")
				END
			END
		| op: CondWaitOp DO
			op.ok := FALSE;
			IF op.dms > 0 THEN Tasks.SetDeadline(ctx, op.dms) END;
			DoWait(o, ctx, TRUE, op.condIdx, {}, woke);
			op.ok := woke;
			IF woke THEN
				INC(covCondT)
			ELSE
				INC(covCondF);
				IF ~(ctx.cancelled OR ctx.expired) THEN
					Violation("AwaitCond: returned FALSE without cancellation or deadline")
				END
			END
		| op: FlagOp DO
			o.flag2 := op.val
		| op: GateOp DO
			(* Затвор ставится под монитором; уступка оставляет монитор
			   у задачи, поэтому входящий встаёт в очередь входа. *)
			o.flag2 := TRUE;
			Tasks.Yield
		| op: TouchOp DO
			IF op.mark > 0 THEN Prio(op.mark) END
		| op: TrapCaughtOp DO
			trapY := op.y; trapCtx := S.ADR(ctx); trapHit := FALSE;
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(SimTrapHandler)), trapCtx, 0, 0);
			op.heldAfter := ctx.HeldCount();	(* Heal: отпустить снятые кадры *)
			RepairOne(op.y);
			IF trapHit THEN
				INC(covCaught);
				IF op.heldAfter # 1 THEN
					Violation("caught trap: the pass holds more than the outer monitor")
				END;
				IF ~Tasks.Held(o) THEN
					Violation("caught trap: the outer monitor was released")
				END
			END;
			op.ok := (op.heldAfter = 1) & Tasks.Held(o);
			RepairMirrors
		| op: TrapRelOp DO
			(* Трап в Ready() на отпускании. transfer — чтобы выбор ждущего
			   и передача монитора были одним шагом HandOff. *)
			o.flag2 := TRUE;	(* затвор: ждущие по флагу {2} готовы *)
			o.transfer := TRUE;
			o.arm := 2;
			Tasks.SetDeadline(ctx, op.dms);
			IF o.Await(ctx, {3}) THEN
				Violation("trap-release: Await returned TRUE on an unsatisfiable condition")
			END;
			o.arm := -1;	(* сюда доходят, только если трапа не было *)
			o.releaseTrapped := FALSE
		| op: NestAwaitOp DO
			(* Вложенный Do на том же мониторе, затем ожидание во внешнем
			   теле: после пробуждения внутри снова только эта задача. *)
			inner.nested := FALSE; inner.sameMon := FALSE; inner.other := NIL;
			inner.nOuter := 0; inner.nInner := 0; inner.innerIdx := -1; inner.done := FALSE;
			o.Do(ctx, inner);
			IF inner.done THEN op.nInner := 1 END;
			INC(modelInc[o.idx], op.nInner);
			IF Tasks.Depth(o) # 1 THEN
				Violation("nested-await: depth is not 1 after the nested Do")
			END;
			IF (o.inside # 1) OR (o.insideTask # cur) THEN
				Violation("nested-await: not alone inside after the nested Do")
			END;
			Tasks.SetDeadline(ctx, op.dms);
			DoWait(o, ctx, FALSE, 0, {3}, op.woke);
			IF (o.inside # 1) OR (o.insideTask # cur) THEN
				Violation("nested-await: not alone inside after Await")
			END;
			IF Tasks.Depth(o) # 1 THEN
				Violation("nested-await: depth is not 1 after Await")
			END;
			INC(o.counter); op.nOuter := 1; INC(covNestAwait);
			INC(modelInc[o.idx])
		| op: CrashOp DO
			NilCrash
		ELSE
			HALT(20)
		END;

		DEC(o.inside);
		IF o.inside = 0 THEN o.insideTask := NIL END
	END Exclusive;

	(* -------------------- тело задачи ------------------------------------- *)

	PROCEDURE CheckPayload (t: SimTask; VAR hold: ARRAY OF Payload);
		VAR i: INTEGER; bad: BOOLEAN;
	BEGIN
		IF ~gcMode THEN RETURN END;
		bad := FALSE;
		FOR i := 0 TO LEN(hold) - 1 DO
			IF hold[i] = NIL THEN bad := TRUE
			ELSIF hold[i].tag # t.id * 7 + 1 THEN bad := TRUE
			END
		END;
		IF bad THEN
			Violation("GC: a payload held on the task stack was collected or corrupted")
		END
	END CheckPayload;

	PROCEDURE (t: SimTask) Body- (VAR ctx: Tasks.Ctx);
		VAR i, a, m, r, target, held: INTEGER; hold: ARRAY 2 OF Payload;
			opI: IncOp; opP: PutOp; opG: GetOp; opW: WaitOp; opC: CondWaitOp;
			opF: FlagOp; opT: TrapCaughtOp; opN: NestAwaitOp;
			opGate: GateOp; opTouch: TouchOp; child, gw: SimTask;
			g: Tasks.Group; pushed: BOOLEAN;
	BEGIN
		INC(t.runs); INC(t.gen);
		IF t.cleaner = NIL THEN NEW(t.cleaner) END;
		t.cleaner.id := t.id;
		Kernel.PushTrapCleaner(t.cleaner); pushed := TRUE;
		IF gcMode THEN
			NEW(hold[0]); hold[0].tag := t.id * 7 + 1;
			NEW(hold[1]); hold[1].tag := t.id * 7 + 1
		END;

		FOR i := 0 TO t.nacts - 1 DO
			a := t.acts[i]; m := t.p1[i];
			CASE a OF
				aInc:
					opI.nested := t.p2[i] > 0;
					opI.sameMon := t.p2[i] = 1;
					opI.other := NIL;
					IF opI.nested & ~opI.sameMon THEN opI.other := mons[t.p3[i]] END;
					opI.nOuter := 0; opI.nInner := 0; opI.innerIdx := -1; opI.done := FALSE;
					mons[m].Do(ctx, opI);
					INC(modelInc[m], opI.nOuter);
					IF opI.innerIdx >= 0 THEN INC(modelInc[opI.innerIdx], opI.nInner) END
			| aPut:
				opP.dms := t.p3[i]; opP.ok := FALSE; opP.item := 0;
				mons[m].Do(ctx, opP);
				IF opP.ok THEN INC(modelPut[m]) END
			| aGet:
				opG.dms := t.p3[i]; opG.ok := FALSE; opG.item := 0;
				mons[m].Do(ctx, opG);
				IF opG.ok THEN INC(modelGet[m]) END
			| aWait:
				opW.need := {t.p2[i]}; opW.dms := t.p3[i]; opW.ok := FALSE;
				mons[m].Do(ctx, opW)
			| aCond:
				opC.condIdx := t.p2[i]; opC.dms := t.p3[i]; opC.ok := FALSE;
				mons[m].Do(ctx, opC)
			| aFlag:
				(* затвор: ставится один раз и не снимается — по нему
				   проверяется FIFO ждущих одного условия *)
				opF.val := TRUE;
				mons[m].Do(ctx, opF)
			| aYield:
				Tasks.Yield
			| aSleep:
				Tasks.Sleep(t.p2[i])
			| aCancel:
				target := t.p2[i] + 1;
				IF target > nTasks THEN target := nTasks END;
				IF target # t.id THEN Tasks.Cancel(tasks[target - 1]); INC(covCancel) END
			| aSpawn:
				r := t.p1[i];
				child := KidAt(r);
				(* Слот мог остаться за живым ребёнком прошлого запуска
				   отменённого родителя: второй Start той же задачи трапует. *)
				IF Tasks.State(child) IN {Tasks.New, Tasks.Finished, Tasks.Cancelled} THEN
					PlanChild(child, seedN, t.id, i, kidBase + r);
					Tasks.Spawn(ctx, child); INC(covSpawn);
					IF ~ctx.cancelled & ~ctx.expired & (Tasks.State(child) = Tasks.New) THEN
						Violation("Spawn did not start a child")
					END
				END
			| aJoin:
				g := ctx.GroupOf();
				Tasks.JoinAll(ctx); INC(covJoin);
				IF (g # NIL) & (Tasks.GroupCount(g) # 0) THEN
					Violation("JoinAll returned while children of the group are alive")
				END
			| aRestart:
				target := t.p2[i] + 1;
				IF target > nTasks THEN target := nTasks END;
				IF (target # t.id) & (Tasks.State(tasks[target - 1]) IN {Tasks.Finished, Tasks.Cancelled}) THEN
					Tasks.Start(tasks[target - 1]); INC(restartsDone); INC(covRestart)
				END
			| aTrap:
				IF t.p2[i] = 0 THEN
					opT.y := NIL;
					IF m + 1 < nMon THEN opT.y := mons[m + 1] END;
					opT.ok := FALSE; opT.heldAfter := 0;
					mons[m].Do(ctx, opT)
				ELSE
					(* Непойманный трап: задача гибнет, мониторы отпускает ядро. *)
					INC(covUncaught);
					NilCrash
				END
			| aTrapRel:
				(* Трап в Ready() на отпускании монитора внутри Await. Трап
				   снимает кадр Do и кадр ожидания; Heal отпускает брошенный
				   кадр: AbandonWait убирает задачу из очереди и, если монитор
				   уже у неё, передаёт его ждущему. *)
				m := t.p1[i];
				(* Один раз за прогон: перезапуск выполняет тот же план, но
				   очередь затвора к этому времени уже другая. *)
				IF ~t.prologueDone & AnyFlagWaiter(mons[m]) THEN
					t.prologueDone := TRUE;
					mons[m].releaseTrapped := FALSE;
					trapY := mons[m]; trapCtx := S.ADR(ctx); trapRelDms := t.p3[i];
					Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TrapRelBody)), trapCtx, 0, 0);
					IF ~mons[m].releaseTrapped THEN
						(* Случайное действие могло не застать очередь флагов в
						   ядре: зеркало хранит и уже разбуженного ждущего. *)
						IF t.p2[i] > 0 THEN
							Violation("trap-release: the planned trap did not happen")
						END
					ELSE
						held := ctx.HeldCount();	(* Heal: AbandonWait *)
						RepairOne(mons[m]);	(* кадр Exclusive снят трапом *)
						IF t.p2[i] > 0 THEN
							(* Наблюдение задуманной пробы: глобалы не должны
							   перезаписываться случайными действиями. *)
							abandonSeen := TRUE;
							abandonHeld := Tasks.Held(mons[m]);
							abandonDepth := Tasks.Depth(mons[m]);
							(* Проба: монитор обязан уйти разбуженному ждущему. *)
							gw := GateWaiter(mons[m], TRUE);
							IF ~Tasks.Held(mons[m]) THEN
								Violation("trap-release: the monitor was not handed to a waiter")
							END;
							IF gw = NIL THEN
								Violation("trap-release: no awake gate waiter after the handoff")
							END;
							IF t.p2[i] = 2 THEN
								(* Отменить получившего монитор до его первого шага:
								   Await вернёт FALSE, монитор останется у него. *)
								handedStopTarget := gw; Tasks.Cancel(gw)
							END
						ELSIF LostWakeup(mons[m]) THEN
							Violation("trap-release: lost wakeup after an abandoned wait")
						END;
						INC(covTrapRel)
					END
				END;
				Tasks.Yield
			| aNestAwait:
				(* Вложенный Do на том же мониторе, затем Await во внешнем
				   теле: изменение состояния после пробуждения видно только
				   этой задаче. *)
				opN.dms := t.p3[i]; opN.nOuter := 0; opN.nInner := 0; opN.woke := FALSE;
				mons[m].Do(ctx, opN);	(* модель обновляет сам Exclusive *)
			| aGateYield:
				(* Затвор под монитором и уступка: входящий встаёт в очередь
				   входа, монитор остаётся у задачи. *)
				mons[m].Do(ctx, opGate)
			| aTouch:
				opTouch.mark := 2; mons[m].Do(ctx, opTouch)
			| aWaitPrio:
				(* Ждущий затвора без срока; метка — порядок пробуждения. *)
				opW.need := {2}; opW.dms := 0; opW.ok := FALSE;
				mons[m].Do(ctx, opW);
				IF opW.ok THEN Prio(1) ELSE Prio(9) END
			| aWaitForever:
				(* Условие {3} не ставит никто: разбудить может только отмена. *)
				opW.need := {3}; opW.dms := 0; opW.ok := FALSE;
				mons[m].Do(ctx, opW)
			ELSE
				HALT(20)
			END;
			IF gcMode THEN CheckPayload(t, hold) END
		END;
		IF gcMode THEN CheckPayload(t, hold) END;
		IF pushed THEN Kernel.PopTrapCleaner(t.cleaner) END
	END Body;

	(* -------------------- план -------------------------------------------- *)

	PROCEDURE PlanChild (c: SimTask; sd: LONGINT; parentId, actIdx, id: INTEGER);
		VAR x: LONGINT; n, i, r, kind: INTEGER;
	BEGIN
		c.id := id;
		x := Mix(sd + actIdx * 131, parentId * 977 + id * 17 + 5);
		c.rng := x;
		n := 4 + RandK(x, 6);
		c.nacts := n;
		FOR i := 0 TO n - 1 DO
			r := RandK(x, 100);
			IF r < 30 THEN kind := aInc
			ELSIF r < 50 THEN kind := aPut
			ELSIF r < 70 THEN kind := aGet
			ELSIF r < 80 THEN kind := aWait
			ELSIF r < 86 THEN kind := aCond
			ELSIF r < 91 THEN kind := aFlag
			ELSIF r < 96 THEN kind := aYield
			ELSIF r < 99 THEN kind := aSleep
			ELSE kind := aTrap
			END;
			c.acts[i] := kind;
			c.p1[i] := RandK(x, nMon);
			IF kind = aInc THEN
				c.p2[i] := 0; c.p3[i] := 0
			ELSIF kind = aWait THEN
				c.p2[i] := RandK(x, 3); c.p3[i] := 2 + RandK(x, 3)
			ELSIF kind = aCond THEN
				c.p2[i] := RandK(x, 2); c.p3[i] := 2 + RandK(x, 3)
			ELSIF (kind = aPut) OR (kind = aGet) THEN
				c.p2[i] := 0; c.p3[i] := 2 + RandK(x, 3)
			ELSIF kind = aSleep THEN
				c.p2[i] := RandK(x, 4); c.p3[i] := 0
			ELSIF kind = aTrap THEN
				c.p2[i] := 1; c.p3[i] := 0	(* только непойманный: будит группу *)
			ELSE
				c.p2[i] := RandK(x, 3); c.p3[i] := 0
			END
		END
	END PlanChild;

	PROCEDURE PlanTask (t: SimTask; sd: LONGINT; id: INTEGER);
		VAR x: LONGINT; n, i, r, kind, wcount: INTEGER;
	BEGIN
		t.id := id;
		x := Mix(sd, id * 7919 + 17);
		t.rng := x;
		n := 10 + RandK(x, 18);
		IF n > maxActs THEN n := maxActs END;
		t.nacts := n;
		FOR i := 0 TO n - 1 DO
			r := RandK(x, 100);
			IF r < 18 THEN kind := aInc
			ELSIF r < 30 THEN kind := aPut
			ELSIF r < 42 THEN kind := aGet
			ELSIF r < 52 THEN kind := aWait
			ELSIF r < 60 THEN kind := aCond
			ELSIF r < 65 THEN kind := aFlag
			ELSIF r < 74 THEN kind := aYield
			ELSIF r < 81 THEN kind := aSleep
			ELSIF r < 85 THEN kind := aCancel
			ELSIF r < 89 THEN kind := aSpawn
			ELSIF r < 91 THEN kind := aJoin
			ELSIF r < 93 THEN kind := aRestart
			ELSIF r < 96 THEN kind := aTrap
			ELSE kind := aNestAwait
			END;
			IF (kind = aSpawn) & (plannedKid >= maxKids) THEN kind := aYield END;
			(* Повторный запуск — только задачи с большим номером: цикл
			   перезапусков не может не кончиться. *)
			IF (kind = aRestart) & (id >= nTasks) THEN kind := aYield END;
			t.acts[i] := kind;
			CASE kind OF
				aInc:
					t.p1[i] := RandK(x, nMon);
					t.p2[i] := RandK(x, 3);
					(* вложенный Do на другой монитор — только с большим номером *)
					IF (t.p2[i] = 2) & (t.p1[i] + 1 >= nMon) THEN t.p2[i] := 1 END;
					IF t.p2[i] = 2 THEN
						t.p3[i] := t.p1[i] + 1 + RandK(x, nMon - t.p1[i] - 1)
					ELSE
						t.p3[i] := 0
					END
			| aPut, aGet:
				t.p1[i] := RandK(x, nMon); t.p2[i] := 0;
				t.p3[i] := 2 + RandK(x, 3)
			| aWait:
				t.p1[i] := RandK(x, nMon); t.p2[i] := RandK(x, 3);
				t.p3[i] := 2 + RandK(x, 3)
			| aCond:
				t.p1[i] := RandK(x, nMon); t.p2[i] := RandK(x, 2);
				t.p3[i] := 2 + RandK(x, 3)
			| aFlag:
				t.p1[i] := RandK(x, nMon); t.p2[i] := RandK(x, 2); t.p3[i] := 0
			| aSleep:
				t.p1[i] := 0; t.p2[i] := RandK(x, 4); t.p3[i] := 0
			| aCancel:
				t.p1[i] := 0; t.p2[i] := RandK(x, nTasks); t.p3[i] := 0
			| aSpawn:
				t.p1[i] := plannedKid; INC(plannedKid);
				t.p2[i] := 0; t.p3[i] := 0
			| aRestart:
				t.p1[i] := 0; t.p2[i] := id + RandK(x, nTasks - id); t.p3[i] := 0
			| aTrap:
				t.p1[i] := RandK(x, nMon); t.p2[i] := RandK(x, 2); t.p3[i] := 0
			| aTrapRel, aNestAwait:
				t.p1[i] := RandK(x, nMon); t.p2[i] := 0;
				t.p3[i] := 2 + RandK(x, 3)
			ELSE
				t.p1[i] := 0; t.p2[i] := 0; t.p3[i] := 0
			END
		END;
		(* Проба FIFO: первые задачи встают в очередь на затвор монитора 0,
		   следующая ставит затвор. Так очередь из нескольких ждущих одного
		   условия есть в каждом прогоне с K >= 4; при K >= 5 первого
		   ждущего отменяют, и FIFO проверяется на оставшихся. *)
		wcount := nTasks - 1;
		IF wcount > 4 THEN wcount := 4 END;	(* первый отменяем, для FIFO нужно ещё >= 2 *)
		IF id <= wcount THEN
			(* Затвор: ждём долго, чтобы сцена ниже застала очередь живой. *)
			t.acts[0] := aWait; t.p1[0] := 0; t.p2[0] := 2; t.p3[0] := 50
		ELSIF id = wcount + 1 THEN
			(* Ставит затвор и трапует в Ready() на отпускании: кадр ожидания
			   уже в очереди, а монитор ещё у задачи. При четырёх ждущих
			   первого отменяют до его шага; при трёх его не трогают, иначе
			   FIFO проверять будет не на ком. *)
			t.acts[0] := aTrapRel; t.p1[0] := 0; t.p3[0] := 4;
			IF wcount >= 4 THEN t.p2[0] := 2 ELSE t.p2[0] := 1 END
		END
	END PlanTask;

	PROCEDURE KidAt (slot: INTEGER): SimTask;
	BEGIN
		IF ~kidMade[slot] THEN
			NEW(kids[slot]); Tasks.SetStackSize(kids[slot], kidStack);
			kidMade[slot] := TRUE
		END;
		RETURN kids[slot]
	END KidAt;

	(* -------------------- прогон ------------------------------------------ *)

	PROCEDURE InitMon (m: Mon; idx: INTEGER; transfer: BOOLEAN);
	BEGIN
		m.idx := idx; m.transfer := transfer;
		m.counter := 0; m.flag2 := FALSE;
		m.head := 0; m.tail := 0; m.count := 0;
		m.putSeq := 0; m.lastGot := 0;
		m.puts := 0; m.gets := 0; m.nw := 0;
		m.inside := 0; m.insideTask := NIL; m.insideGen := 0;
		m.arm := -1; m.releaseTrapped := FALSE;
		NEW(m.cond[0]); m.cond[0].idx := 0;
		NEW(m.cond[1]); m.cond[1].idx := 1
	END InitMon;

	PROCEDURE MakeMons;
		VAR m: INTEGER;
	BEGIN
		FOR m := 0 TO nMon - 1 DO
			NEW(mons[m]);
			InitMon(mons[m], m, (m + SHORT(seedN)) MOD 2 = 0)
		END
	END MakeMons;

	PROCEDURE MakeTasks;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nTasks - 1 DO
			IF ~taskMade[i] THEN
				NEW(tasks[i]); Tasks.SetStackSize(tasks[i], topStack); taskMade[i] := TRUE
			END;
			tasks[i].id := i + 1
		END
	END MakeTasks;

	PROCEDURE AllDone (): BOOLEAN;
		VAR i: INTEGER; st: INTEGER;
	BEGIN
		FOR i := 0 TO nTasks - 1 DO
			st := Tasks.State(tasks[i]);
			IF ~(st IN {Tasks.Finished, Tasks.Cancelled}) THEN RETURN FALSE END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] THEN
				st := Tasks.State(kids[i]);
				IF ~(st IN {Tasks.New, Tasks.Finished, Tasks.Cancelled}) THEN RETURN FALSE END
			END
		END;
		RETURN TRUE
	END AllDone;

	PROCEDURE AnyReady (): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nTasks - 1 DO
			IF Tasks.State(tasks[i]) = Tasks.Ready THEN RETURN TRUE END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] & (Tasks.State(kids[i]) = Tasks.Ready) THEN RETURN TRUE END
		END;
		RETURN FALSE
	END AnyReady;

	PROCEDURE CheckModels;
		VAR i: INTEGER; s: UnitAssert.Message;
	BEGIN
		FOR i := 0 TO nMon - 1 DO
			IF (mons[i].count < 0) OR (mons[i].count > cap) THEN
				Violation("buffer model: count is outside 0 .. cap")
			END;
			IF modelPut[i] # modelGet[i] + mons[i].count THEN
				Violation("buffer model: put sum /= get sum + count")
			END;
			IF mons[i].puts # modelPut[i] THEN
				Violation("buffer model: the monitor put counter differs from the model")
			END;
			IF mons[i].gets # modelGet[i] THEN
				Violation("buffer model: the monitor get counter differs from the model")
			END;
			IF mons[i].counter # modelInc[i] THEN
				s := "counter model differs (monitor "; AppNum(s, i);
				App(s, ": counter "); AppNum(s, mons[i].counter);
				App(s, ", model "); AppNum(s, modelInc[i]); App(s, ")");
				Violation(s)
			END;
			IF Tasks.Held(mons[i]) & (Tasks.Depth(mons[i]) < 1) THEN
				Violation("monitor: Held but Depth < 1")
			END;
			IF ~Tasks.Held(mons[i]) & (Tasks.Depth(mons[i]) # 0) THEN
				Violation("monitor: free but Depth /= 0")
			END
		END
	END CheckModels;

	PROCEDURE CheckWorld;
		VAR i, k: INTEGER; any: BOOLEAN; st: INTEGER;
	BEGIN
		IF Tasks.Current() # NIL THEN
			Violation("a task runs between scheduler steps")
		END;
		CheckModels;
		any := AnyReady();
		FOR i := 0 TO nMon - 1 DO
			(* снятый трапом кадр мог оставить зеркало; живой владелец — ошибка *)
			IF ~Tasks.Held(mons[i]) & (mons[i].inside > 0) THEN
				IF mons[i].insideTask # NIL THEN
					st := Tasks.State(mons[i].insideTask);
					IF (mons[i].insideGen = mons[i].insideTask.gen)
							& (st IN {Tasks.Ready, Tasks.Running, Tasks.Waiting,
								Tasks.Sleeping, Tasks.Parked}) THEN
						Violation("monitor: free, but a live task is marked inside")
					END
				END;
				mons[i].inside := 0; mons[i].insideTask := NIL; mons[i].insideGen := 0
			END;
			IF ~Tasks.Held(mons[i]) & ~any THEN
				FOR k := 0 TO mons[i].nw - 1 DO
					IF mons[i].ws[k].active & (mons[i].ws[k].task # NIL)
							& Blocked(mons[i].ws[k].task) & WaiterReadyIdx(mons[i], k) THEN
						Violation("lost wakeup: monitor free, no ready task, a waiting condition is true")
					END
				END
			END
		END;
		FOR k := 0 TO nTasks - 1 DO
			IF Tasks.State(tasks[k]) = Tasks.Running THEN
				Violation("a task is Running between steps")
			END
		END
	END CheckWorld;

	PROCEDURE EndChecks;
		VAR i, k: INTEGER; s: UnitAssert.Message;
	BEGIN
		CheckWorld;
		FOR i := 0 TO nMon - 1 DO
			IF Tasks.Held(mons[i]) THEN Violation("end: a monitor is still held") END;
			IF Tasks.Depth(mons[i]) # 0 THEN Violation("end: monitor depth is not zero") END;
			IF Tasks.Queued(mons[i]) # 0 THEN Violation("end: a monitor queue is not empty") END;
			IF mons[i].inside # 0 THEN
				s := "end: the inside counter is not zero (monitor ";
				AppNum(s, i); App(s, ", inside "); AppNum(s, mons[i].inside); App(s, ")");
				Violation(s)
			END;
			IF ActiveWaiters(mons[i]) # 0 THEN Violation("end: active mirror waiters remain") END
		END;
		FOR k := 0 TO nTasks - 1 DO
			IF ~(Tasks.State(tasks[k]) IN {Tasks.Finished, Tasks.Cancelled}) THEN
				Violation("end: a task did not finish")
			END
		END;
		FOR k := 0 TO maxKids - 1 DO
			IF kidMade[k] & ~(Tasks.State(kids[k]) IN {Tasks.New, Tasks.Finished, Tasks.Cancelled}) THEN
				Violation("end: a child task did not finish")
			END
		END;
		IF Tasks.Count() # baseCount THEN
			Violation("end: Tasks.Count() differs from the value before the run")
		END;
		IF (baseCount = 0) & (Tasks.ListSize() # 0) THEN
			Violation("end: the list of tasks is not empty")
		END
	END EndChecks;

	PROCEDURE AllQuiet (): BOOLEAN;
		VAR i: INTEGER; st: INTEGER;
	BEGIN
		FOR i := 0 TO maxTasks - 1 DO
			IF taskMade[i] THEN
				st := Tasks.State(tasks[i]);
				IF ~(st IN {Tasks.New, Tasks.Finished, Tasks.Cancelled}) THEN RETURN FALSE END
			END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] THEN
				st := Tasks.State(kids[i]);
				IF ~(st IN {Tasks.New, Tasks.Finished, Tasks.Cancelled}) THEN RETURN FALSE END
			END
		END;
		RETURN TRUE
	END AllQuiet;

	PROCEDURE EnsureStopped;
	(* Убрать за прогоном: отменить все свои задачи, в том числе оставшиеся
	   от прошлого зерна с большим номером, и довести их до конца. *)
		VAR i, n: INTEGER; t0: LONGINT;
	BEGIN
		FOR i := 0 TO maxTasks - 1 DO
			IF taskMade[i] & (Tasks.State(tasks[i]) IN {Tasks.New, Tasks.Ready,
					Tasks.Running, Tasks.Sleeping, Tasks.Waiting, Tasks.Parked}) THEN
				Tasks.Cancel(tasks[i])
			END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] & (Tasks.State(kids[i]) IN {Tasks.Ready,
					Tasks.Running, Tasks.Sleeping, Tasks.Waiting, Tasks.Parked}) THEN
				Tasks.Cancel(kids[i])
			END
		END;
		t0 := Kernel.Time(); n := 0;
		WHILE ~AllQuiet() & (Kernel.Time() - t0 < 2000) & (n < 100000) DO
			Tasks.Step; INC(n)
		END
	END EnsureStopped;

	PROCEDURE CanCollect (): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO nTasks - 1 DO
			IF Tasks.State(tasks[i]) IN {Tasks.Ready, Tasks.Running} THEN RETURN FALSE END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] & (Tasks.State(kids[i]) IN {Tasks.Ready, Tasks.Running}) THEN RETURN FALSE END
		END;
		RETURN TRUE
	END CanCollect;

	PROCEDURE Pump (budgetMs: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		LOOP
			CheckWorld;
			IF failed THEN RETURN FALSE END;
			IF AllDone() THEN RETURN TRUE END;
			IF Kernel.Time() - t0 > budgetMs THEN RETURN FALSE END;
			Tasks.Step;
			INC(stepNo);
			IF gcMode & CanCollect() & (RandK(harnessRng, 6) = 0) THEN Kernel.Collect END
		END
	END Pump;

	PROCEDURE ResetRun (sd: LONGINT; gc: BOOLEAN);
		VAR i: INTEGER;
	BEGIN
		seedN := sd; gcMode := gc; stepNo := 0; violations := 0;
		failed := FALSE; reported := FALSE; restartsDone := 0; waitSeq := 0;
		plannedKid := 0; baseCount := 0;
		handedStopTarget := NIL;
		abandonSeen := FALSE; abandonHeld := FALSE; abandonDepth := -1;
		harnessRng := Mix(sd, 999);
		planRng := Mix(sd, 777);
		nMon := 1 + RandK(planRng, maxMon);
		nTasks := 2 + RandK(planRng, maxTasks - 1);
		FOR i := 0 TO maxMon - 1 DO
			modelInc[i] := 0; modelPut[i] := 0; modelGet[i] := 0;
		END;
		MakeMons;
		MakeTasks;
		FOR i := 0 TO maxTasks - 1 DO
			IF taskMade[i] THEN tasks[i].prologueDone := FALSE END
		END;
		FOR i := 0 TO nTasks - 1 DO PlanTask(tasks[i], sd, i + 1) END
	END ResetRun;

	PROCEDURE DumpStuck;
		VAR i, n: INTEGER; s9: ARRAY 24 OF CHAR;
	BEGIN
		Console.WriteStr("TasksSim: stuck, step "); Strings.IntToString(stepNo, s9);
		Console.WriteStr(s9); Console.WriteStr(" count "); Strings.IntToString(Tasks.Count(), s9);
		Console.WriteStr(s9); Console.WriteStr(" list "); Strings.IntToString(Tasks.ListSize(), s9);
		Console.WriteStr(s9); Console.WriteStr(" nextdeadline "); Strings.IntToString(Tasks.NextDeadline(), s9);
		Console.WriteStr(s9); Console.WriteLn;
		n := 0;
		FOR i := 0 TO nTasks - 1 DO
			IF ~(Tasks.State(tasks[i]) IN {Tasks.Finished, Tasks.Cancelled}) THEN
				Console.WriteStr("  task "); Strings.IntToString(i + 1, s9);
				Console.WriteStr(s9); Console.WriteStr(" state "); Strings.IntToString(Tasks.State(tasks[i]), s9);
				Console.WriteStr(s9); Console.WriteStr(" runs "); Strings.IntToString(tasks[i].runs, s9);
				Console.WriteStr(s9); Console.WriteStr(" nacts "); Strings.IntToString(tasks[i].nacts, s9);
				Console.WriteStr(s9); Console.WriteLn; INC(n)
			END
		END;
		FOR i := 0 TO maxKids - 1 DO
			IF kidMade[i] & ~(Tasks.State(kids[i]) IN {Tasks.New, Tasks.Finished, Tasks.Cancelled}) THEN
				Console.WriteStr("  kid "); Strings.IntToString(i, s9);
				Console.WriteStr(s9); Console.WriteStr(" state "); Strings.IntToString(Tasks.State(kids[i]), s9);
				Console.WriteStr(s9); Console.WriteLn; INC(n)
			END
		END;
		FOR i := 0 TO nMon - 1 DO
			Console.WriteStr("  mon "); Strings.IntToString(i, s9);
			Console.WriteStr(s9); Console.WriteStr(" held "); Strings.IntToString(Bool(Tasks.Held(mons[i])), s9);
			Console.WriteStr(s9); Console.WriteStr(" depth "); Strings.IntToString(Tasks.Depth(mons[i]), s9);
			Console.WriteStr(s9); Console.WriteStr(" queued "); Strings.IntToString(Tasks.Queued(mons[i]), s9);
			Console.WriteStr(s9); Console.WriteStr(" inside "); Strings.IntToString(mons[i].inside, s9);
			Console.WriteStr(s9); Console.WriteLn
		END;
		Console.WriteStr("  unfinished "); Strings.IntToString(n, s9);
		Console.WriteStr(s9); Console.WriteStr(" deadlocked "); Strings.IntToString(Bool(Tasks.Deadlocked()), s9);
		Console.WriteStr(s9); Console.WriteLn
	END DumpStuck;

	PROCEDURE RunCore (sd: LONGINT; gc: BOOLEAN);
		VAR i: INTEGER; budget: LONGINT;
	BEGIN
		EnsureStopped;	(* ничего от прошлого зерна или прошлого теста *)
		ResetRun(sd, gc);
		baseCount := Tasks.Count();
		IF baseCount # 0 THEN
			Violation("the run started with live tasks from a previous test")
		END;
		FOR i := 0 TO nTasks - 1 DO Tasks.Start(tasks[i]) END;
		IF gc THEN budget := 20000 ELSE budget := 12000 END;
		IF ~Pump(budget) & ~failed THEN
			DumpStuck;
			Violation("run did not finish within the time budget")
		END;
		IF ~failed THEN EndChecks END;
		EnsureStopped
	END RunCore;

	PROCEDURE RunBody (a, b, c: S.ADDRESS);
	BEGIN
		bodyDone := FALSE;
		RunCore(seedN, gcMode);
		bodyDone := TRUE
	END RunBody;

	PROCEDURE StopBody (a, b, c: S.ADDRESS);
	(* Аварийная уборка под своим Try: её трап не должен подменять
	   сообщение о первом провале. *)
	BEGIN
		stopDone := FALSE;
		EnsureStopped;
		stopDone := TRUE
	END StopBody;

	PROCEDURE One* (sd: LONGINT; gc: BOOLEAN);
	(** Один прогон зерна; gc — со сборщиком между шагами. *)
		VAR s: UnitAssert.Message;
	BEGIN
		seedN := sd; gcMode := gc; stepNo := 0; failed := FALSE; reported := FALSE;
		violations := 0; bodyDone := FALSE;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(RunBody)), 0, 0, 0);
		IF ~bodyDone THEN
			failed := TRUE;
			IF ~reported THEN
				reported := TRUE;
				s := "seed "; AppNum(s, sd); App(s, ", step "); AppNum(s, stepNo);
				App(s, ": a trap in the simulation harness; repeat: System_devTasksSim.Replay ");
				IF gc THEN App(s, "g") END;
				AppNum(s, sd);
				UnitAssert.Fail(s)
			END;
			stopDone := FALSE;
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(StopBody)), 0, 0, 0)
		END;
		IF ~failed THEN UnitAssert.True(TRUE, "") END
	END One;

	PROCEDURE RunSeeds (from, to: INTEGER; gc: BOOLEAN);
		VAR s: INTEGER;
	BEGIN
		FOR s := from TO to DO
			One(s, gc);
			IF failed THEN RETURN END
		END
	END RunSeeds;

	(* -------------------- команды ----------------------------------------- *)

	PROCEDURE GetText (OUT s: ARRAY OF CHAR);
		VAR p: DevCommanders.Par; rd: TextModels.Reader; n: INTEGER;
	BEGIN
		s := "";
		p := DevCommanders.par;
		IF p = NIL THEN RETURN END;
		DevCommanders.par := NIL;
		rd := p.text.NewReader(NIL); rd.SetPos(p.beg); rd.Read; n := 0;
		WHILE (~rd.eot) & (rd.Pos() <= p.end) & (n < LEN(s) - 1) DO
			s[n] := rd.char; INC(n); rd.Read
		END;
		s[n] := 0X
	END GetText;

	PROCEDURE ParseSeed (VAR s: ARRAY OF CHAR; OUT sd: LONGINT; OUT gc: BOOLEAN): BOOLEAN;
		VAR i: INTEGER; neg, any: BOOLEAN;
	BEGIN
		i := 0; gc := FALSE;
		WHILE (s[i] = " ") OR (s[i] = 09X) DO INC(i) END;
		IF (s[i] = "g") OR (s[i] = "G") THEN gc := TRUE; INC(i) END;
		WHILE (s[i] = " ") OR (s[i] = 09X) DO INC(i) END;
		neg := FALSE;
		IF s[i] = "-" THEN neg := TRUE; INC(i) END;
		sd := 0; any := FALSE;
		WHILE (s[i] >= "0") & (s[i] <= "9") DO
			sd := sd * 10 + (ORD(s[i]) - ORD("0")); any := TRUE; INC(i)
		END;
		IF neg THEN sd := -sd END;
		RETURN any
	END ParseSeed;

	PROCEDURE Replay*;
	(** Повторить одно зерно: System_devTasksSim.Replay 42 (g42 — со сборщиком). *)
		VAR s: ARRAY 64 OF CHAR; sd: LONGINT; gc: BOOLEAN; m: UnitAssert.Message;
	BEGIN
		GetText(s);
		IF ParseSeed(s, sd, gc) THEN
			One(sd, gc);
			IF failed THEN
				UnitAssert.LastMessage(m);
				Console.WriteStr("TasksSim: "); Console.WriteStr(m); Console.WriteLn
			ELSE
				Console.WriteStr("TasksSim: seed "); Strings.IntToString(sd, s);
				Console.WriteStr(s); Console.WriteStr(", steps ");
				Strings.IntToString(stepNo, s); Console.WriteStr(s);
				Console.WriteStr(": ok"); Console.WriteLn
			END
		ELSE
			Console.WriteStr("TasksSim: usage: Replay [g]SEED"); Console.WriteLn
		END
	END Replay;

	PROCEDURE CheckCoverage (IN tag: ARRAY OF CHAR);
		VAR s: UnitAssert.Message; complete: BOOLEAN;
	BEGIN
		complete := FALSE;
		s := ""; App(s, tag); App(s, ": paths not exercised:");
		IF covAwaitT = 0 THEN App(s, " Await-TRUE") END;
		IF covAwaitF = 0 THEN App(s, " Await-FALSE") END;
		IF covCondT = 0 THEN App(s, " AwaitCond-TRUE") END;
		IF covCondF = 0 THEN App(s, " AwaitCond-FALSE") END;
		IF covCancel = 0 THEN App(s, " Cancel") END;
		IF covSpawn = 0 THEN App(s, " Spawn") END;
		IF covJoin = 0 THEN App(s, " JoinAll") END;
		IF covRestart = 0 THEN App(s, " Restart") END;
		IF covCaught = 0 THEN App(s, " caught-trap") END;
		IF covUncaught = 0 THEN App(s, " uncaught-trap") END;
		IF covNested = 0 THEN App(s, " nested-Do") END;
		IF covTrapRel = 0 THEN App(s, " trap-on-release") END;
		IF covHandedStop = 0 THEN App(s, " stopped-after-handoff") END;
		IF covNestAwait = 0 THEN App(s, " Await-after-nested-Do") END;
		IF fifoChecks = 0 THEN App(s, " FIFO") END;
		complete := (covAwaitT > 0) & (covAwaitF > 0) & (covCondT > 0) & (covCondF > 0)
			& (covCancel > 0) & (covSpawn > 0) & (covJoin > 0) & (covRestart > 0)
			& (covCaught > 0) & (covUncaught > 0) & (covNested > 0)
			& (covTrapRel > 0) & (covHandedStop > 0) & (covNestAwait > 0)
			& (fifoChecks > 0);
		UnitAssert.True(complete, s)
	END CheckCoverage;

	PROCEDURE TestProcAbandonedHandoffDepth*;
	(** Брошенное ожидание, монитор которого уже передан другой задаче:
		Reconcile второй раз зовёт ReleaseHold для той же записи, и глубина
		нового владельца не должна уменьшиться. Взятый монитор (Held TRUE)
		имеет глубину не меньше 1. *)
		VAR s: UnitAssert.Message;
	BEGIN
		ResetCov;
		One(1, FALSE);
		s := "after an abandoned wait is handed to another task, held is ";
		IF abandonHeld THEN App(s, "TRUE") ELSE App(s, "FALSE") END;
		App(s, ", depth is "); AppNum(s, abandonDepth);
		App(s, " (a held monitor must have depth >= 1)");
		UnitAssert.True(abandonSeen & abandonHeld & (abandonDepth >= 1), s)
	END TestProcAbandonedHandoffDepth;

	PROCEDURE TaskDone (t: Tasks.Task): BOOLEAN;
	(* Задача не идёт и не ждёт: завершена, отменена или ещё не запускалась. *)
	BEGIN
		RETURN (t = NIL) OR (Tasks.State(t) IN {Tasks.New, Tasks.Finished, Tasks.Cancelled})
	END TaskDone;

	PROCEDURE MakeProbe (t: SimTask; id, kind, p2, p3: INTEGER);
	(* Одна проба на монитор mons[0]; план не зависит от зерна. *)
	BEGIN
		t.id := id; t.nacts := 1; t.acts[0] := kind; t.p1[0] := 0;
		t.p2[0] := p2; t.p3[0] := p3; t.prologueDone := TRUE
	END MakeProbe;

	PROCEDURE CaseHandoffPriority (transfer: BOOLEAN; OUT ok: BOOLEAN;
		OUT order: UnitAssert.Message);
	(* Готовый ждущий (затвор {2} без срока) против входящего в очереди
	   входа. Порядок меток: 1 — проснулся ждущий, 2 — вошёл входящий.
	   Задачи успевают пробежать внутри шага планировщика, поэтому
	   проверяется порядок, а не состояние после шага. *)
		VAR m, saved: Mon; w, h, e: SimTask; i, steps: INTEGER; t0: LONGINT;
	BEGIN
		ok := FALSE; order := ""; gcMode := FALSE;
		NEW(m); InitMon(m, 0, transfer);
		saved := mons[0]; mons[0] := m;
		NEW(w); MakeProbe(w, 1, aWaitPrio, 0, 0); Tasks.SetStackSize(w, kidStack);
		NEW(h); MakeProbe(h, 2, aGateYield, 0, 0); Tasks.SetStackSize(h, kidStack);
		NEW(e); MakeProbe(e, 3, aTouch, 0, 0); Tasks.SetStackSize(e, kidStack);
		ResetPrio;
		Tasks.Start(w);
		steps := 0;
		WHILE (Tasks.State(w) # Tasks.Parked) & ~TaskDone(w) & (steps < 100) DO
			Tasks.Step; INC(steps)
		END;
		IF Tasks.State(w) # Tasks.Parked THEN
			App(order, "the gate waiter did not park, state ");
			AppNum(order, Tasks.State(w))
		ELSE
			Tasks.Start(h); Tasks.Start(e);
			steps := 0; t0 := Kernel.Time();
			WHILE (prioN < 2) & ~(TaskDone(w) & TaskDone(h) & TaskDone(e))
					& (steps < 100) & (Kernel.Time() - t0 < 1000) DO
				Tasks.Step; INC(steps)
			END;
			ok := (prioN >= 2) & (prioLog[0] = 1) & (prioLog[1] = 2);
			App(order, "order");
			FOR i := 0 TO prioN - 1 DO
				App(order, " "); AppNum(order, prioLog[i])
			END;
			IF ~(prioN >= 1) OR (prioLog[0] # 1) THEN
				App(order, "; the gate waiter did not wake first")
			END
		END;
		(* Пробы не должны пережить сценарий: доработать или отменить. *)
		steps := 0; t0 := Kernel.Time();
		WHILE ~(TaskDone(w) & TaskDone(h) & TaskDone(e))
				& (steps < 200) & (Kernel.Time() - t0 < 1000) DO
			Tasks.Step; INC(steps)
		END;
		IF ~TaskDone(w) THEN Tasks.Cancel(w) END;
		IF ~TaskDone(h) THEN Tasks.Cancel(h) END;
		IF ~TaskDone(e) THEN Tasks.Cancel(e) END;
		steps := 0;
		WHILE ~(TaskDone(w) & TaskDone(h) & TaskDone(e)) & (steps < 200) DO
			Tasks.Step; INC(steps)
		END;
		mons[0] := saved
	END CaseHandoffPriority;

	PROCEDURE TestProcHandoffPriority*;
	(** Приоритет на отпускании монитора: готовый ждущий получает его
		раньше входящего из очереди входа. Проверяются оба способа —
		передача из рук в руки и пробуждение без передачи. *)
		VAR okT, okF: BOOLEAN; ordT, ordF, s: UnitAssert.Message;
	BEGIN
		CaseHandoffPriority(TRUE, okT, ordT);
		CaseHandoffPriority(FALSE, okF, ordF);
		s := "handoff priority with transfer: "; App(s, ordT);
		UnitAssert.True(okT, s);
		s := "handoff priority without transfer: "; App(s, ordF);
		UnitAssert.True(okF, s)
	END TestProcHandoffPriority;

	PROCEDURE TestProcCancelNoDeadline*;
	(** Ожидание без срока: разбудить задачу может только отмена. После
		Cancel задача обязана сняться с очереди и завершиться за ограниченное
		число шагов планировщика, а не ждать следующего события. *)
		VAR m, saved: Mon; c: SimTask; steps: INTEGER; ok: BOOLEAN;
			s: UnitAssert.Message;
	BEGIN
		ok := FALSE; gcMode := FALSE;
		NEW(m); InitMon(m, 0, FALSE);
		saved := mons[0]; mons[0] := m;
		NEW(c); MakeProbe(c, 1, aWaitForever, 0, 0); Tasks.SetStackSize(c, kidStack);
		Tasks.Start(c); Tasks.Step;
		IF Tasks.State(c) # Tasks.Parked THEN
			s := "cancel without deadline: the waiter did not park, state ";
			AppNum(s, Tasks.State(c))
		ELSE
			Tasks.Cancel(c);
			steps := 0;
			WHILE (Tasks.State(c) # Tasks.Cancelled) & (steps < 8) DO
				Tasks.Step; INC(steps)
			END;
			ok := (Tasks.State(c) = Tasks.Cancelled) & (steps <= 4)
				& (Tasks.Queued(m) = 0);
			s := "cancel without deadline: state "; AppNum(s, Tasks.State(c));
			App(s, ", steps "); AppNum(s, steps);
			App(s, ", queued "); AppNum(s, Tasks.Queued(m))
		END;
		IF Tasks.State(c) # Tasks.Cancelled THEN
			Tasks.Cancel(c);
			IF Tasks.State(c) IN {Tasks.Sleeping, Tasks.Waiting, Tasks.Parked} THEN
				Tasks.Unpark(c)
			END;
			steps := 0;
			WHILE (Tasks.State(c) # Tasks.Cancelled) & (steps < 200) DO
				Tasks.Step; INC(steps)
			END
		END;
		mons[0] := saved;
		UnitAssert.True(ok, s)
	END TestProcCancelNoDeadline;

	PROCEDURE TestSeeds*;
	(** Зёрна 1..80 без сборщика. *)
	BEGIN
		ResetCov;
		RunSeeds(1, 80, FALSE);
		IF ~failed THEN CheckCoverage("TestSeeds") END
	END TestSeeds;

	PROCEDURE TestSeedsGc*;
	(** То же со сборщиком между шагами. *)
	BEGIN
		ResetCov;
		RunSeeds(1, 24, TRUE);
		IF ~failed THEN CheckCoverage("TestSeedsGc") END
	END TestSeedsGc;

	PROCEDURE TestSlowSeeds*;
	(** Длинный прогон: зёрна 1000..1400 без сборщика и 2000..2100 со сборщиком. *)
	BEGIN
		ResetCov;
		RunSeeds(1000, 1400, FALSE);
		IF ~failed THEN RunSeeds(2000, 2100, TRUE) END;
		IF ~failed THEN CheckCoverage("TestSlowSeeds") END
	END TestSlowSeeds;

END System_devTasksSim.
