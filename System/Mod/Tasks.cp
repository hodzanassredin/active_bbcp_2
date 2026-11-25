MODULE Tasks;
(** Задачи со своим стеком в одной нити ОС.

	Задача — расширяемая запись с методом Run. Start кладёт её в очередь
	готовых; планировщик — действие Services.Action: исполняет готовые
	задачи не дольше общего кванта (defaultSlice мс) и возвращает
	управление главному циклу. Спящие ждут срока через Kernel.Time.

	Память под стек выделяет ядро: Kernel.AllocStack (mmap + сторожевая
	страница в LinKernel, ленивые страницы). Переключатель — [code]
	Taskarch.Switch: сохраняет регистры и указатель стека, восстанавливает
	регистры другого стека. Модуль один на i386 и amd64; машинная часть
	(слово, раскладка кадра, Switch) — в Taskarch, по файлу на платформу
	(System/Mod и System/Mod64). Внешней .so нет.

	Контекст исполнения (Kernel.Try, trapStack, tryCount) уже лежит в
	записи стека ядра: SwitchSave/SwitchRestore вокруг сырого
	переключения. Трап в задаче ловится своим Kernel.Try в TaskBoot,
	задача завершается, остальные живут.

	Отмена — флаг; каждая точка ожидания проверяет его до и после
	ожидания и прерывает задачу трапом 127. Нарушение предусловия — трап
	20–26 с текстом в Console (номер — по порядку проверок в процедуре);
	100 — нарушен инвариант планировщика.

	Неделимый участок (BeginAtomic/EndAtomic) запрещает точки
	переключения: на нём держится транзакция AsyncStm.

	Мониторы ([exclusive], ACTIVE.AWAIT) реализует модуль Monitors; ему
	этот модуль отдаёт крючок Kernel.Scheduler: токен задачи, ожидание
	монитора и пробуждение передачей монитора. Ожидание монитора не
	трогает пользовательский флаг пробуждения Park/Unpark.

	Ограничения:
	- одна нить ОС;
	- вытеснения нет: отмена и Yield работают только в точках ожидания;
	- API с Run без параметров, поэтому проверка ABB «VAR-параметры не
	  ссылаются в чужой стек» сводится к проверке, что сам объект задачи
	  не лежит на стеке. *)

	IMPORT S := SYSTEM, Taskarch, Kernel, Monitors, Services, Models, Console, Strings;

	CONST
		nameLen = 64;
		defaultStack = 256 * 1024;
		directMax = 64;	(* переключений задача → задача подряд, без возврата к Step *)
		poolMax = 1024;		(* сколько свободных стеков пул держит в покое *)
		trimStep = 16;		(* сколько лишних стеков шаг планировщика возвращает ядру *)
		word = Taskarch.word;
		defaultSlice = 5;	(* мс на шаг планировщика *)
		pollPeriod = 5;		(* мс: как часто ключ уступки поднимает флаг Poll *)
		heapArity = 4;		(* детей у узла кучи спящих *)
		heapMin = 64;		(* начальная длина кучи и запас перед уплотнением *)
		closeDrain = 1000;	(* мс: сколько CLOSE доводит отменённые задачи до конца *)
		entryQ = 1;	(* вид очереди в Task.qkind: вход в монитор *)
		flagQ = 2;	(* ожидание по флагам Ready *)
		condQ = 3;	(* ожидание по условию-объекту Cond *)

		(** состояния задачи **)
		New* = 0; Ready* = 1; Running* = 2; Sleeping* = 3;
		Waiting* = 4; Parked* = 5; Finished* = 6; Cancelled* = 7;

		entryOff = Taskarch.entryOff;	(* адрес входа от sp нового стека *)
		stackHead = Taskarch.stackHead;	(* запас у верха стека *)

	TYPE
		(* Условие-объект для Object.WaitFor: Ready читает состояние
		   объекта-монитора o. Наследник заводит свои поля. *)
		Cond* = POINTER TO ABSTRACT RECORD END;

		Task* = POINTER TO ABSTRACT RECORD
			next: Task;		(* список всех задач *)
			prev: Task;		(* предыдущая в списке всех задач *)
			rnext: Task;		(* очередь готовых *)
			sleepSeq: INTEGER;	(* номер текущего сна: отличает свою запись кучи от устаревших *)
			wnext: Task;		(* очередь ждущих эту задачу *)
			waiters: Task;
			sp: S.ADDRESS;		(* сохранённый rsp *)
			lo, hi: S.ADDRESS;	(* границы стека [lo, hi) *)
			stackRec: Kernel.Stack;
			state: INTEGER;
			deadline: LONGINT;	(* до какого Kernel.Time спать *)
			stackSize: INTEGER;
			cancel, wake, failed, bodyOk: BOOLEAN;
			external: BOOLEAN;	(* ждёт в ParkExternal; учтена в nExternal *)
			group: Group;
			own: Group;
			(* Очередь монитора-объекта: задача стоит не больше чем в одной.
			   qnext — сосед, qkind — вид очереди, qneed и qcond — что ждёт. *)
			qnext: Task;
			qkind: INTEGER;
			qneed: SET;
			qcond: Cond;
			holds: Hold;		(* вершина цепочки взятых мониторов-объектов *)
			pctx: S.ADDRESS;	(* Ctx задачи Job; 0 — задача не Job *)
			name: ARRAY nameLen OF CHAR
		END;

		Group* = POINTER TO RECORD
			alive: INTEGER;		(* живых участников *)
			failed: BOOLEAN;	(* трап хотя бы в одном *)
			owner: Task		(* задача, которая ждёт опустошения *)
		END;

		(* Запрос к объекту-монитору: запись-наследник на стеке
		   вызывающего, передаётся в Do и Exclusive по VAR. *)
		Op* = ABSTRACT RECORD END;

		(* Объект-монитор. Служебные поля закрыты: наследник добавляет
		   данные и пишет Exclusive. Очереди входа, флагов и условий —
		   списки задач; пустая очередь не стоит вызова Ready. *)
		Object* = POINTER TO ABSTRACT RECORD
			owner: S.ADDRESS;	(* токен владельца; 0 — свободен *)
			depth: INTEGER;		(* взятия владельца: повторный вход *)
			enter, enterTail: Task;
			flagq, flagTail: Task;
			condq, condTail: Task
		END;

		(* Запись одного вызова Do. Лежит в куче и берётся из списка
		   свободных Ctx: после прогрева Do и Wait не выделяют памяти.
		   mark — адрес локальной переменной кадра Do; по нему Reconcile
		   отличает кадры, уже снятые трапом, от живых. *)
		Hold = POINTER TO HoldRec;
		HoldRec = RECORD
			obj: Object;		(* монитор этого вызова *)
			next: Hold;		(* предыдущий (внешний) вызов *)
			mark: S.ADDRESS;	(* кадр Do этого вызова *)
			wmark: S.ADDRESS;	(* кадр ожидания, пока waiting *)
			acq, waiting: BOOLEAN
		END;

		Cleaner = POINTER TO RECORD (Kernel.TrapCleaner)
			ctxAdr: S.ADDRESS
		END;

		(* Пропуск задачи Job: создаётся на стеке Run, наружу уходит
		   только VAR-параметром Body. *)
		Ctx* = LIMITED RECORD
			task-: Task;
			group-: Group;
			cancelled-, expired-: BOOLEAN;
			free: Hold;		(* свободные записи вызовов *)
			cleaner: Cleaner;
			deadline: LONGINT;	(* абсолютный срок Kernel.Time; 0 — нет *)
			trapped, cleanerPushed: BOOLEAN
		END;

		CtxPtr = POINTER TO Ctx;

		(* Задача с пропуском: Run реализован здесь, тело пишет наследник.
		   cleaner и holdFree переживают запуск: повторный Start той же
		   задачи не выделяет память. *)
		Job* = POINTER TO ABSTRACT RECORD (Task)
			cleaner: Cleaner;
			holdFree: Hold
		END;

		(* Запись кучи спящих. Действительна, пока t спит этим сном:
		   t.state = Sleeping и t.sleepSeq = seq; иначе устарела (задачу
		   разбудили раньше срока) и выбрасывается при подъёме к вершине. *)
		SleepEntry = RECORD
			t: Task;
			deadline: LONGINT;
			seq: INTEGER
		END;

		SchedAction = POINTER TO RECORD (Services.Action) END;
		Sched = POINTER TO RECORD (Kernel.Scheduler) END;

	VAR
		all, readyHead, readyTail: Task;
		(* Куча спящих: четверичная, по возрастанию deadline, в heap[0 .. heapN).
		   Инвариант: heap[(i - 1) DIV heapArity].deadline <= heap[i].deadline;
		   число действительных записей кучи = nSleeping, у каждой из этих задач
		   ровно одна действительная запись; heapN - nSleeping — устаревших.
		   Инвариант верен вне PushSleeper: задача, которая засыпает, входит в
		   nSleeping вместе со своей записью (PushSleeper); MakeReady и
		   WakeSleepers уменьшают nSleeping вместе с тем, что запись устаревает
		   или снимается. *)
		heap: POINTER TO ARRAY OF SleepEntry;
		heapN, nSleeping: INTEGER;
		current: Task;
		schedSP: S.ADDRESS;
		sched: SchedAction;	(* действие планировщика Services *)
		schedHook: Sched;	(* крючок Kernel.Scheduler *)
		scheduled, inStep: BOOLEAN;
		schedAt: LONGINT;	(* срок назначенного шага; смысл имеет при scheduled *)
		alive: INTEGER;		(* незавершённых задач *)
		slice: LONGINT;
		lastPoll: LONGINT;	(* когда Kernel.RequestPoll поднимался последний раз *)
		lastWake: LONGINT;	(* время последнего просмотра спящих в Step *)
		direct: INTEGER;	(* прямых переключений с последнего возврата в Step *)
		switches: LONGINT;	(* переключений стека, диагностика *)
		deadlocked: BOOLEAN;	(* все ждут, сроков нет; журнал уже напечатан *)
		nExternal: INTEGER;	(* инвариант: число задач с external = TRUE *)
		atomic: INTEGER;	(* глубина BeginAtomic: > 0 — точки переключения запрещены *)
		(* Пул свободных стеков: односвязный список через верхние слова самого
		   стека. Инвариант: poolHead = 0 равносильно poolCount = 0; иначе poolHead —
		   верх hi свободного стека, в [hi - word] — верх следующего, в [hi - 2*word] —
		   его lo, в [hi - 3*word] — размер, с которым стек запрошен у ядра.
		   Стек в пуле не зарегистрирован в реестре ядра и не сканируется. *)
		poolHead: S.ADDRESS;
		poolCount: INTEGER;
		closeT: Task; closeT0: LONGINT;	(* для CLOSE *)

	PROCEDURE (t: Task) Run* (), NEW, ABSTRACT;

	PROCEDURE^ EnsureScheduled;
	PROCEDURE^ CancelGroup (g: Group);
	PROCEDURE^ Reconcile (VAR ctx: Ctx);
	PROCEDURE^ CheckSleepHolds (except: Object);
	PROCEDURE^ FinishTaskPass (t: Task);

	(* --- текст в Console -------------------------------------------------- *)

	PROCEDURE Msg (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr("Tasks: "); Console.WriteStr(s); Console.WriteLn
	END Msg;

	PROCEDURE StateName (st: INTEGER; OUT s: ARRAY OF CHAR);
	BEGIN
		CASE st OF
			New: s := "new"
		| Ready: s := "ready"
		| Running: s := "running"
		| Sleeping: s := "sleeping"
		| Waiting: s := "waiting"
		| Parked: s := "parked"
		| Finished: s := "finished"
		| Cancelled: s := "cancelled"
		ELSE s := "?"
		END
	END StateName;

	PROCEDURE SwitchTo (VAR from, to: S.ADDRESS; self: Kernel.Stack);
	(** Сырое переключение с сохранением контекста ядра своего стека. *)
	BEGIN
		INC(switches);
		Kernel.SwitchSave(self);
		Taskarch.Switch(from, to);
		Kernel.SwitchRestore(self)
	END SwitchTo;

	PROCEDURE SetCurrent (t: Task);
	(* Пост: current = t; Kernel.monTok — адрес t^ (Kernel.mainToken при
	   t = NIL); взятия мониторов быстрым путём прежнего токена отданы
	   таблице Monitors до смены токена. *)
	BEGIN
		IF Kernel.monTop # Kernel.monBase THEN Kernel.FlushMonitors END;
		current := t;
		IF t # NIL THEN Kernel.monTok := S.ADR(t^) ELSE Kernel.monTok := Kernel.mainToken END
	END SetCurrent;

	(* --- очереди ---------------------------------------------------------- *)

	PROCEDURE Enqueue (t: Task);
	BEGIN
		t.rnext := NIL;
		IF readyTail = NIL THEN readyHead := t ELSE readyTail.rnext := t END;
		readyTail := t
	END Enqueue;

	PROCEDURE Dequeue (): Task;
		VAR t: Task;
	BEGIN
		t := readyHead;
		IF t # NIL THEN
			readyHead := t.rnext;
			IF readyHead = NIL THEN readyTail := NIL END;
			t.rnext := NIL
		END;
		RETURN t
	END Dequeue;

	PROCEDURE MakeReady (t: Task);
	BEGIN
		IF (t.state IN {Sleeping, Waiting, Parked}) THEN
			IF t.state = Sleeping THEN DEC(nSleeping) END;	(* запись кучи устарела *)
			IF t.external THEN t.external := FALSE; DEC(nExternal) END;
			t.state := Ready; Enqueue(t); deadlocked := FALSE; EnsureScheduled
		END
	END MakeReady;

	PROCEDURE IsDone (t: Task): BOOLEAN;
	BEGIN
		RETURN (t.state = Finished) OR (t.state = Cancelled)
	END IsDone;

	(* --- куча спящих ------------------------------------------------------ *)

	PROCEDURE Valid (IN e: SleepEntry): BOOLEAN;
	BEGIN
		RETURN (e.t.state = Sleeping) & (e.t.sleepSeq = e.seq)
	END Valid;

	PROCEDURE SiftUp (i: INTEGER);
	(* Пред: инвариант кучи нарушен только в i относительно предков. Пост: инвариант. *)
		VAR e: SleepEntry; p: INTEGER;
	BEGIN
		e := heap[i];
		WHILE (i > 0) & (e.deadline < heap[(i - 1) DIV heapArity].deadline) DO
			p := (i - 1) DIV heapArity; heap[i] := heap[p]; i := p
		END;
		heap[i] := e
	END SiftUp;

	PROCEDURE SiftDown (i: INTEGER);
	(* Пред: инвариант кучи нарушен только в i относительно потомков. Пост: инвариант. *)
		VAR e: SleepEntry; c, m, j: INTEGER;
	BEGIN
		e := heap[i];
		LOOP
			c := heapArity * i + 1;
			IF c >= heapN THEN EXIT END;
			m := c; j := c + 1;
			WHILE (j < heapN) & (j < c + heapArity) DO
				IF heap[j].deadline < heap[m].deadline THEN m := j END;
				INC(j)
			END;
			IF heap[m].deadline < e.deadline THEN heap[i] := heap[m]; i := m ELSE EXIT END
		END;
		heap[i] := e
	END SiftDown;

	PROCEDURE PopTop;
	(* Пред: heapN > 0. Пост: вершина снята, инвариант кучи сохранён. *)
	BEGIN
		DEC(heapN);
		heap[0] := heap[heapN]; heap[heapN].t := NIL;
		IF heapN > 0 THEN SiftDown(0) END
	END PopTop;

	PROCEDURE Compact;
	(* Пост: в куче только действительные записи, heapN = nSleeping. *)
		VAR i, n: INTEGER;
	BEGIN
		n := 0;
		FOR i := 0 TO heapN - 1 DO
			IF Valid(heap[i]) THEN heap[n] := heap[i]; INC(n) END
		END;
		FOR i := n TO heapN - 1 DO heap[i].t := NIL END;
		heapN := n;
		ASSERT(heapN = nSleeping, 100);
		i := (heapN - 2) DIV heapArity;
		WHILE i >= 0 DO SiftDown(i); DEC(i) END
	END Compact;

	PROCEDURE ReserveHeap (n: INTEGER);
	(* Пост: в куче место хотя бы под n записей. Место запасается при запуске
	   задачи (StartIn): выделение памяти в Sleep, когда спят тысячи задач,
	   запустило бы сборку со сканом всех их стеков. *)
		VAR new: POINTER TO ARRAY OF SleepEntry; i, len: INTEGER;
	BEGIN
		IF (heap = NIL) OR (LEN(heap^) < n) THEN
			len := heapMin;
			WHILE len < n DO len := 2 * len END;
			NEW(new, len);
			FOR i := 0 TO heapN - 1 DO new[i] := heap[i] END;
			heap := new
		END
	END ReserveHeap;

	PROCEDURE PushSleeper (t: Task);
	(* Пред: t.state = Sleeping, t.deadline и t.sleepSeq — этого сна, записи
	   этого сна в куче нет, nSleeping t не учитывает.
	   Пост: в куче действительная запись t, nSleeping учитывает t. *)
	BEGIN
		ASSERT(t.state = Sleeping, 20);
		IF heapN >= 2 * nSleeping + heapMin THEN Compact END;
		ReserveHeap(heapN + 1);
		heap[heapN].t := t; heap[heapN].deadline := t.deadline; heap[heapN].seq := t.sleepSeq;
		INC(heapN); SiftUp(heapN - 1);
		INC(nSleeping)
	END PushSleeper;

	PROCEDURE WakeSleepers (now: LONGINT);
	(* Пост: спящие со сроком <= now — в очереди готовых, устаревшие записи
	   с вершины сняты. *)
		VAR t: Task;
	BEGIN
		WHILE heapN > 0 DO
			IF ~Valid(heap[0]) THEN PopTop
			ELSIF heap[0].deadline <= now THEN
				t := heap[0].t; PopTop; DEC(nSleeping);
				t.state := Ready; Enqueue(t)
			ELSE RETURN
			END
		END
	END WakeSleepers;

	PROCEDURE RemoveWaiter (t, w: Task);
	(* Пост: w нет в очереди ждущих t. *)
		VAR p: Task;
	BEGIN
		IF t.waiters = w THEN t.waiters := w.wnext
		ELSE
			p := t.waiters;
			WHILE (p # NIL) & (p.wnext # w) DO p := p.wnext END;
			IF p # NIL THEN p.wnext := w.wnext END
		END;
		w.wnext := NIL
	END RemoveWaiter;

	PROCEDURE WakeWaiters (t: Task);
		VAR w: Task;
	BEGIN
		WHILE t.waiters # NIL DO
			w := t.waiters; t.waiters := w.wnext; w.wnext := NIL;
			IF w.state = Waiting THEN w.state := Ready; Enqueue(w) END
		END
	END WakeWaiters;

	(* --- уход с процессора ------------------------------------------------ *)

	PROCEDURE Leave;
	(* Пред: current # NIL — задача, которая уже записала своё новое состояние
	   (Ready и в очереди готовых или одно из ожиданий).
	   Пост: current снова исполняется — та же задача, что вызвала Leave.
	   Следующая готовая получает процессор прямо со стека уходящей задачи,
	   без захода на стек планировщика; после directMax таких переходов
	   подряд управление идёт в Step, который смотрит часы, будит спящих,
	   поднимает флаг Poll и отдаёт главный цикл по кванту. Задачу, которая
	   ещё не исполнялась (t.sp = t.hi - stackHead), запускает Step: прямой
	   первый запуск с холодного стека на холодный дороже на 15 % (замер
	   10 тыс. задач, docs/stacks-study.md). Переключение встроено, а не
	   вызвано через SwitchTo: после смены стека каждый возврат вверх по
	   цепочке кадров задачи предсказатель возвратов угадывает неверно, и
	   каждый лишний кадр над переключением — лишний промах.
	   Инвариант: на стеке планировщика ждёт ровно один Step; задача, которая
	   из него ушла, может вернуться в него с любой другой задачей в current. *)
		VAR me, t: Task;
	BEGIN
		me := current;
		IF (readyHead # NIL) & (direct < directMax) & (readyHead.sp # readyHead.hi - stackHead) THEN
			INC(direct);
			t := Dequeue(); SetCurrent(t); t.state := Running;
			IF t # me THEN
				INC(switches); Kernel.SwitchSave(me.stackRec);
				Taskarch.Switch(me.sp, t.sp);
				Kernel.SwitchRestore(me.stackRec)
			END
		ELSE
			INC(switches); Kernel.SwitchSave(me.stackRec);
			Taskarch.Switch(me.sp, schedSP);
			Kernel.SwitchRestore(me.stackRec)
		END
	END Leave;

	(* --- проверки точек ожидания ------------------------------------------ *)

	PROCEDURE InFramework (): BOOLEAN;
	(** Идёт обработчик действия (кроме нашего планировщика) или
		рассылка по домену. Внутренняя проверка планировщика. *)
	BEGIN
		RETURN Services.executingAction & (S.VAL(S.ADDRESS, Services.executing) # S.VAL(S.ADDRESS, sched))
			OR Models.InDomainCast()
	END InFramework;

	PROCEDURE CheckWait (IN what: ARRAY OF CHAR);
	BEGIN
		IF current = NIL THEN Msg(what); Msg("wait outside a task") END;
		ASSERT(current # NIL, 20);
		IF current.cancel THEN Msg("cancelled"); HALT(127) END;
		IF InFramework() THEN Msg(what); Msg("wait inside an action handler or domain broadcast") END;
		ASSERT(~InFramework(), 21);
		IF atomic > 0 THEN Msg(what); Msg("wait inside an atomic section") END;
		ASSERT(atomic = 0, 25)
	END CheckWait;

	(* --- завершение и запуск ---------------------------------------------- *)

	PROCEDURE Finish;
	(** Вызывается из TaskBoot на стеке задачи: перейти на главный стек. *)
	BEGIN
		SwitchTo(current.sp, schedSP, current.stackRec);
		HALT(100)	(* недостижимо: на стек завершённой задачи не переключаются *)
	END Finish;

	PROCEDURE TaskBody (a, b, c: S.ADDRESS);
	(** bodyOk — поле задачи, а не глобал: иначе завершившаяся соседняя
		задача выставила бы «успех» трапнувшей. *)
	BEGIN
		current.bodyOk := FALSE;
		current.Run;
		current.bodyOk := TRUE
	END TaskBody;

	PROCEDURE WaitOwnGroup (t: Task);
	(** Родитель не завершается, пока живы дочерние задачи группы. *)
	BEGIN
		WHILE (t.own # NIL) & (t.own.alive > 0) DO
			IF t.cancel THEN
				CancelGroup(t.own);
				t.state := Cancelled;
				Finish; RETURN
			END;
			CheckSleepHolds(NIL);
			t.state := Parked;
			Leave
		END
	END WaitOwnGroup;

	PROCEDURE TaskBoot;
	(** Точка входа нового стека (адрес возврата из Switch). *)
		VAR t: Task;
	BEGIN
		t := current;
		Kernel.SwitchRestore(t.stackRec);
		IF t.cancel THEN
			t.state := Cancelled
		ELSE
			t.bodyOk := FALSE;
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TaskBody)), 0, 0, 0);
			(* Трап снял кадры Body: отпустить мониторы, которых уже нет
			   на стеке. Обычный конец Job делает это в Run. *)
			IF t.pctx # 0 THEN FinishTaskPass(t) END;
			(* Отменённая или трапнувшая задача отменяет свою группу: дети не
			   переживают родителя (структурная конкуренция). *)
			IF t.cancel THEN
				IF t.own # NIL THEN CancelGroup(t.own) END;
				t.state := Cancelled
			ELSIF ~t.bodyOk THEN
				IF t.own # NIL THEN CancelGroup(t.own) END;
				t.failed := TRUE;
				t.state := Finished;
				IF t.group # NIL THEN
					t.group.failed := TRUE; CancelGroup(t.group)
				END
			ELSE
				t.state := Finished;
				WaitOwnGroup(t);
				t.state := Finished	(* WaitOwnGroup ставил Parked *)
			END
		END;
		Services.SetTrapGuardCleanup;	(* трап внутри Do не оставляет флаг *)
		Finish
	END TaskBoot;

	PROCEDURE TakeStack (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	(* Пред: size > 0. Пост: TRUE — [lo, hi) пригоден под стек, запрошенный
	   с размером size, ниже lo лежит сторожевая страница; FALSE — памяти нет.
	   Стек берётся из пула, если на его вершине лежит стек того же размера;
	   иначе у ядра. *)
		VAR sz: S.ADDRESS;
	BEGIN
		IF poolHead # 0 THEN
			S.GET(poolHead - 3 * word, sz);
			IF sz = size THEN
				hi := poolHead; S.GET(hi - 2 * word, lo); S.GET(hi - word, poolHead);
				DEC(poolCount);
				RETURN TRUE
			END
		END;
		RETURN Kernel.AllocStack(size, lo, hi)
	END TakeStack;

	PROCEDURE GiveStack (size: INTEGER; lo, hi: S.ADDRESS);
	(* Пред: [lo, hi) получен от TakeStack(size), на нём никто не исполняется,
	   запись реестра ядра снята. Пост: стек в пуле. *)
		VAR sz: S.ADDRESS;
	BEGIN
		IF lo = 0 THEN RETURN END;
		sz := size;
		S.PUT(hi - word, poolHead); S.PUT(hi - 2 * word, lo); S.PUT(hi - 3 * word, sz);
		poolHead := hi; INC(poolCount)
	END GiveStack;

	PROCEDURE TrimPool;
	(* Пул принимает любой освобождённый стек: возврат памяти ядру стоит
	   системного вызова, и при массовом завершении задач он дороже самих
	   задач. Лишнее сверх poolMax возвращается понемногу, не больше trimStep
	   стеков за шаг планировщика. *)
		VAR lo, hi: S.ADDRESS; n: INTEGER;
	BEGIN
		n := 0;
		WHILE (poolCount > poolMax) & (n < trimStep) DO
			hi := poolHead; S.GET(hi - 2 * word, lo); S.GET(hi - word, poolHead);
			DEC(poolCount); INC(n);
			Kernel.FreeStack(lo, hi)
		END
	END TrimPool;

	PROCEDURE DrainPool;
		VAR lo, hi: S.ADDRESS;
	BEGIN
		WHILE poolHead # 0 DO
			hi := poolHead; S.GET(hi - 2 * word, lo); S.GET(hi - word, poolHead);
			Kernel.FreeStack(lo, hi)
		END;
		poolCount := 0
	END DrainPool;

	PROCEDURE StackSizeOf (t: Task): INTEGER;
	BEGIN
		IF t.stackSize = 0 THEN RETURN defaultStack ELSE RETURN t.stackSize END
	END StackSizeOf;

	PROCEDURE InitStack (t: Task): BOOLEAN;
		VAR lo, hi, sp, a, zero: S.ADDRESS; size: INTEGER;
	BEGIN
		size := StackSizeOf(t);
		IF ~TakeStack(size, lo, hi) THEN RETURN FALSE END;
		t.lo := lo; t.hi := hi;
		IF t.stackRec = NIL THEN
			t.stackRec := Kernel.RegisterStack(lo, hi)
		ELSE
			(* запись прошлого запуска: привязать к стеку заново *)
			Kernel.RelinkStack(t.stackRec, lo, hi)
		END;
		IF t.stackRec = NIL THEN GiveStack(size, lo, hi); t.lo := 0; RETURN FALSE END;
		(* 13 нулевых слотов регистров (обнуляются ниже: стек из пула не пуст), адрес входа,
		   8 байт запаса: sp % 16 = 8, после 13 pop и ret rsp % 16 = 8 *)
		sp := hi - stackHead;
		zero := 0; a := sp;
		WHILE a < hi DO S.PUT(a, zero); INC(a, word) END;
		S.PUT(sp + entryOff, S.ADR(TaskBoot));
		t.sp := sp;
		RETURN TRUE
	END InitStack;

	PROCEDURE CheckTask (t: Task);
	BEGIN
		IF t = NIL THEN Msg("NIL task") END;
		ASSERT(t # NIL, 20);
		IF ~(t.state IN {New, Finished, Cancelled}) THEN
			Msg("task is running or waiting")
		END;
		ASSERT(t.state IN {New, Finished, Cancelled}, 21)
	END CheckTask;

	PROCEDURE StartIn* (t: Task; g: Group);
	(** Пред: t # NIL (трап 20), t не идёт и не ждёт (21), платформа дала
		стек (22). Пост: t готова к исполнению; g # NIL — t в группе g.
		Завершённую или отменённую задачу можно запустить снова: поля
		прошлого запуска сбрасываются, стек и его запись берутся заново. *)
		VAR restart: BOOLEAN;
	BEGIN
		CheckTask(t);
		restart := t.state IN {Finished, Cancelled};
		IF restart THEN
			ASSERT(t.holds = NIL, 100);
			ASSERT(t.pctx = 0, 100);
			t.cancel := FALSE; t.wake := FALSE; t.failed := FALSE;
			t.bodyOk := FALSE; t.external := FALSE; t.deadline := 0;
			t.waiters := NIL; t.wnext := NIL;
			t.qnext := NIL; t.qkind := 0; t.qcond := NIL; t.qneed := {};
			(* Своя группа прошлого запуска: дети уже отменены или кончились;
			   владельца снимаем, чтобы их завершение не будило новый запуск. *)
			IF t.own # NIL THEN t.own.owner := NIL; t.own := NIL END
		END;
		IF ~InitStack(t) THEN Msg("no stack memory (platform does not support it)"); HALT(22) END;
		ReserveHeap(2 * (alive + 1) + heapMin);	(* PushSleeper не выделяет: heapN < 2 * nSleeping + heapMin *)
		t.group := g;
		IF g # NIL THEN INC(g.alive) END;
		t.state := Ready; Enqueue(t); deadlocked := FALSE;
		t.prev := NIL; t.next := all;
		IF all # NIL THEN all.prev := t END;
		all := t;
		INC(alive);
		EnsureScheduled
	END StartIn;

	PROCEDURE Start* (t: Task);
	BEGIN
		StartIn(t, NIL)
	END Start;

	PROCEDURE Reap (t: Task);
	(** Освободить стек завершённой задачи (уже на главном стеке) и убрать
		её из списка всех задач: иначе завершённые задачи и всё, на что они
		ссылаются, живут до конца процесса. Запись реестра стека остаётся
		в задаче: повторный запуск привяжет её к стеку заново без NewRec. *)
	BEGIN
		IF t.prev = NIL THEN all := t.next ELSE t.prev.next := t.next END;
		IF t.next # NIL THEN t.next.prev := t.prev END;
		t.next := NIL; t.prev := NIL;
		IF t.stackRec # NIL THEN Kernel.UnregisterStack(t.stackRec) END;
		GiveStack(StackSizeOf(t), t.lo, t.hi);
		t.lo := 0; t.hi := 0;
		WakeWaiters(t);
		DEC(alive);
		IF t.group # NIL THEN
			DEC(t.group.alive);
			IF t.group.alive <= 0 THEN
				IF t.group.owner # NIL THEN MakeReady(t.group.owner) END
			END
		END
	END Reap;

	(* --- точки ожидания --------------------------------------------------- *)

	PROCEDURE Yield*;
	BEGIN
		CheckWait("Yield");
		current.state := Ready; Enqueue(current);
		Leave
	END Yield;

	PROCEDURE Sleep* (ms: LONGINT);
	(** Пред: точка ожидания; держа монитор-объект, спать можно только с его
		разрешения HoldsWhileWaiting (трап 26). Sleep(0) — уступка, не сон:
		как Yield, проверки мониторов нет. *)
	BEGIN
		CheckWait("Sleep");
		IF ms <= 0 THEN current.state := Ready; Enqueue(current); Leave; RETURN END;
		CheckSleepHolds(NIL);
		current.deadline := Kernel.Time() + ms;
		current.state := Sleeping; INC(current.sleepSeq);
		PushSleeper(current);
		Leave;
		current.wake := FALSE;	(* Unpark разбудил сон, а не будущий Park *)
		CheckWait("Sleep")
	END Sleep;

	PROCEDURE Join* (t: Task);
	(** Пред: точка ожидания (трапы 20, 21, отмена — 127); t # NIL (22);
		t не текущая задача (23); t запущена (24).
		Пост: t завершена или отменена. Посторонний Unpark не прерывает
		ожидание и остаётся следующему Park. *)
	BEGIN
		CheckWait("Join");
		IF t = NIL THEN Msg("Join: NIL task") END;
		ASSERT(t # NIL, 22);
		IF t = current THEN Msg("Join: the current task") END;
		ASSERT(t # current, 23);
		IF t.state = New THEN Msg("Join: task not started") END;
		ASSERT(t.state # New, 24);
		IF ~IsDone(t) THEN
			CheckSleepHolds(NIL);
			(* Инвариант: пока t не завершена, current в очереди t.waiters ровно
			   один раз; WakeWaiters снимает всю очередь при Reap(t). *)
			current.wnext := t.waiters; t.waiters := current;
			REPEAT
				current.state := Waiting;
				Leave;
				IF current.cancel & ~IsDone(t) THEN RemoveWaiter(t, current) END;
				CheckWait("Join")
			UNTIL IsDone(t)
		END
	END Join;

	PROCEDURE Park*;
	(** Усыпить себя до Unpark. Пробуждение до Park не теряется.
		Держа монитор-объект, спать можно только с его разрешения (трап 26). *)
	BEGIN
		CheckWait("Park");
		IF current.wake THEN current.wake := FALSE; RETURN END;
		CheckSleepHolds(NIL);
		current.state := Parked;
		Leave;
		current.wake := FALSE;	(* пробуждение потреблено этим Park *)
		CheckWait("Park")
	END Park;

	PROCEDURE ParkExternal*;
	(** Park для ожидания события извне: таймера, ввода, рабочей нити,
		вызова с главного стека. Пока такая задача ждёт, планировщик не
		считает общее ожидание тупиком. В остальном — как Park. *)
	BEGIN
		CheckWait("Park");
		IF current.wake THEN current.wake := FALSE; RETURN END;
		CheckSleepHolds(NIL);
		current.external := TRUE; INC(nExternal);
		current.state := Parked;
		Leave;
		current.wake := FALSE;
		CheckWait("Park")
	END ParkExternal;

	PROCEDURE ParkMonitor;
	(** Ожидание монитора (крючок Kernel.Scheduler.park): до пробуждения
		передачей монитора, отмены или постороннего Unpark. Флаг wake не
		читается и не сбрасывается: пользовательское пробуждение остаётся
		следующему Park. Monitors ждёт в цикле «пока не владелец». *)
	BEGIN
		CheckWait("monitor wait");
		CheckSleepHolds(NIL);
		current.state := Parked;
		Leave;
		CheckWait("monitor wait")
	END ParkMonitor;

	PROCEDURE Unpark* (t: Task);
	(** Пред: t # NIL (трап 20). Пост: t разбужена, если ждёт; иначе
		её следующий Park вернётся сразу. *)
	BEGIN
		IF t = NIL THEN Msg("Unpark: NIL task") END;
		ASSERT(t # NIL, 20);
		t.wake := TRUE;
		IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
	END Unpark;

	PROCEDURE Cancel* (t: Task);
	(** Пред: t # NIL (трап 20). Пост: t отмечена отменённой; спящая или
		ждущая t разбужена и в точке ожидания прерывается трапом 127. *)
	BEGIN
		IF t = NIL THEN Msg("Cancel: NIL task") END;
		ASSERT(t # NIL, 20);
		IF ~IsDone(t) THEN
			t.cancel := TRUE;
			IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
		END
	END Cancel;

	(* --- группа ----------------------------------------------------------- *)

	PROCEDURE NewGroup* (): Group;
	(** Пост: в задаче — собственная группа текущей задачи (одна на задачу:
		повторный вызов возвращает ту же); вне задачи — новая группа без
		владельца. *)
		VAR g: Group;
	BEGIN
		IF (current # NIL) & (current.own # NIL) THEN RETURN current.own END;
		NEW(g); g.alive := 0; g.failed := FALSE; g.owner := current;
		IF current # NIL THEN current.own := g END;
		RETURN g
	END NewGroup;

	PROCEDURE GroupFailed* (g: Group): BOOLEAN;
	BEGIN
		RETURN (g # NIL) & g.failed
	END GroupFailed;

	PROCEDURE GroupCount* (g: Group): INTEGER;
	BEGIN
		IF g = NIL THEN RETURN 0 ELSE RETURN g.alive END
	END GroupCount;

	PROCEDURE OwnGroup* (t: Task): Group;
	BEGIN
		IF t = NIL THEN RETURN NIL ELSE RETURN t.own END
	END OwnGroup;

	PROCEDURE CancelGroup (g: Group);
		VAR t: Task;
	BEGIN
		t := all;
		WHILE t # NIL DO
			IF (t.group = g) & ~IsDone(t) THEN Cancel(t) END;
			t := t.next
		END
	END CancelGroup;

	(* --- монитор-объект и пропуск ----------------------------------------- *)

	PROCEDURE (o: Object) Exclusive- (VAR ctx: Ctx; VAR op: Op), NEW, ABSTRACT;
	PROCEDURE (c: Cond) Ready* (o: Object): BOOLEAN, NEW, ABSTRACT;

	PROCEDURE (o: Object) Ready* (): SET, NEW, EXTENSIBLE;
	(** Флаги состояния объекта. По умолчанию пусто. *)
	BEGIN
		RETURN {}
	END Ready;

	PROCEDURE (o: Object) HoldsWhileWaiting* (): BOOLEAN, NEW, EXTENSIBLE;
	(** TRUE — держать этот монитор, пока задача ждёт в другом. *)
	BEGIN
		RETURN FALSE
	END HoldsWhileWaiting;

	PROCEDURE (o: Object) TransferOnRelease* (): BOOLEAN, NEW, EXTENSIBLE;
	(** Способ пробуждения на выходе из монитора. TRUE — передать монитор
		выбранному ждущему из рук в руки; FALSE — оставить монитор свободным,
		разбудить первого готового ждущего и дать ему самому снова взять
		монитор и перепроверить условие. По умолчанию FALSE: на буфере
		это 92 нс против 195 нс на предмет и 31 переключение на 1000
		против 2030. *)
	BEGIN
		RETURN FALSE
	END TransferOnRelease;

	PROCEDURE Append (VAR head, tail: Task; t: Task);
	BEGIN
		t.qnext := NIL;
		IF tail = NIL THEN head := t ELSE tail.qnext := t END;
		tail := t
	END Append;

	PROCEDURE Prepend (VAR head, tail: Task; t: Task);
	(* Вернуть задачу в голову очереди: она уже ждала и не должна терять
	   место после ложного пробуждения. *)
	BEGIN
		t.qnext := head;
		IF head = NIL THEN tail := t END;
		head := t
	END Prepend;

	PROCEDURE Unlink (VAR head, tail: Task; t: Task);
		VAR p: Task;
	BEGIN
		IF head = t THEN
			head := t.qnext;
			IF head = NIL THEN tail := NIL END
		ELSE
			p := head;
			WHILE (p # NIL) & (p.qnext # t) DO p := p.qnext END;
			ASSERT(p # NIL, 100);
			p.qnext := t.qnext;
			IF tail = t THEN tail := p END
		END;
		t.qnext := NIL
	END Unlink;

	PROCEDURE RemoveQueued (o: Object; t: Task);
	BEGIN
		CASE t.qkind OF
			entryQ: Unlink(o.enter, o.enterTail, t)
		| flagQ: Unlink(o.flagq, o.flagTail, t)
		| condQ: Unlink(o.condq, o.condTail, t)
		END;
		t.qkind := 0; t.qcond := NIL
	END RemoveQueued;

	PROCEDURE ChooseFlag (o: Object): Task;
		VAR t: Task; rdy: SET;
	BEGIN
		IF o.flagq = NIL THEN RETURN NIL END;
		rdy := o.Ready();	(* один раз на выход, только если ждущие есть *)
		t := o.flagq;
		WHILE (t # NIL) & ((t.qneed * rdy) = {}) DO t := t.qnext END;
		RETURN t
	END ChooseFlag;

	PROCEDURE ChooseCond (o: Object): Task;
		VAR t: Task;
	BEGIN
		IF o.condq = NIL THEN RETURN NIL END;
		t := o.condq;
		WHILE (t # NIL) & ((t.qcond = NIL) OR ~t.qcond.Ready(o)) DO t := t.qnext END;
		RETURN t
	END ChooseCond;

	PROCEDURE HandOff (o: Object);
	(* Передать свободный монитор: ждущему по флагам с истинным
	   условием, иначе ждущему по условию-объекту, иначе входящему. *)
		VAR t: Task;
	BEGIN
		t := ChooseFlag(o);
		IF t = NIL THEN t := ChooseCond(o) END;
		IF t = NIL THEN t := o.enter END;
		IF t # NIL THEN
			RemoveQueued(o, t);
			o.owner := S.ADR(t^); o.depth := 1;
			MakeReady(t)
		ELSE
			o.owner := 0; o.depth := 0
		END
	END HandOff;

	PROCEDURE WakeChosen (o: Object);
	(* Способ без передачи: снять с очереди первого готового и разбудить
	   его, оставив монитор свободным. Задача сама возьмёт его в Wait. *)
		VAR t: Task;
	BEGIN
		t := ChooseFlag(o);
		IF t = NIL THEN t := ChooseCond(o) END;
		IF t = NIL THEN t := o.enter END;
		IF t # NIL THEN RemoveQueued(o, t); MakeReady(t) END
	END WakeChosen;

	PROCEDURE ReleaseMonitor (o: Object);
	(* Отпустить монитор на нуле глубины способом самого объекта. *)
	BEGIN
		o.depth := 0;
		IF o.TransferOnRelease() THEN
			HandOff(o)
		ELSE
			o.owner := 0;
			WakeChosen(o)
		END
	END ReleaseMonitor;

	PROCEDURE ReleaseOne (o: Object);
	(* Отпустить одно взятие; на нуле передать монитор. *)
	BEGIN
		IF o.owner = 0 THEN RETURN END;
		DEC(o.depth);
		IF o.depth <= 0 THEN ReleaseMonitor(o) END
	END ReleaseOne;

	PROCEDURE TakeHold (VAR ctx: Ctx): Hold;
		VAR h: Hold;
	BEGIN
		IF ctx.free # NIL THEN
			h := ctx.free; ctx.free := h.next
		ELSE
			NEW(h)
		END;
		h.next := NIL;
		RETURN h
	END TakeHold;

	PROCEDURE GiveHold (VAR ctx: Ctx; h: Hold);
	BEGIN
		h.obj := NIL;
		h.acq := FALSE; h.waiting := FALSE;
		h.next := ctx.free; ctx.free := h
	END GiveHold;

	PROCEDURE Token (): S.ADDRESS;
	(* Токен текущей задачи — адрес её записи. Поле Kernel.monTok хранит то
	   же значение, но им пользуются Monitors и старая проба AsyncObject;
	   монитору-объекту достаточно самой задачи. *)
	BEGIN
		IF current = NIL THEN RETURN 0 ELSE RETURN S.ADR(current^) END
	END Token;

	PROCEDURE AbandonWait (h: Hold);
	(* Кадр ожидания снят трапом: убрать задачу из очереди и отпустить
	   монитор, если он уже передан ей. *)
		VAR o: Object;
	BEGIN
		o := h.obj;
		h.waiting := FALSE; h.acq := FALSE;
		IF (o # NIL) & (current # NIL) THEN
			IF current.qkind # 0 THEN RemoveQueued(o, current) END;
			IF o.owner = Token() THEN
				o.owner := 0; o.depth := 0;
				ReleaseMonitor(o)
			END
		END
	END AbandonWait;

	PROCEDURE ReleaseHold (h: Hold);
	BEGIN
		IF h.waiting THEN AbandonWait(h); RETURN END;
		IF h.obj # NIL THEN
			IF h.acq THEN
				ASSERT(h.obj.owner = Token(), 100);
				ReleaseOne(h.obj)
			ELSIF h.obj.depth > 0 THEN
				DEC(h.obj.depth)
			END
		END;
		h.acq := FALSE
	END ReleaseHold;

	PROCEDURE Chain (VAR ctx: Ctx): Hold;
	(** Вершина цепочки мониторов задачи; NIL — задачи уже нет. *)
	BEGIN
		IF ctx.task = NIL THEN RETURN NIL ELSE RETURN ctx.task.holds END
	END Chain;

	PROCEDURE Reconcile (VAR ctx: Ctx);
	(* Отпустить записи вызовов, чьи кадры уже сняты трапом: mark ниже
	   текущего указателя стека. Записи лежат в куче, поэтому переживают
	   снятый кадр; порядок цепочки — от глубокого к внешнему.
	   Ожидание снимается, только если снят его собственный кадр (wmark):
	   живой Wait, только что вставший в очередь перед сном, не трогается. *)
		VAR h, n: Hold; sp: S.ADDRESS; marker: INTEGER;
	BEGIN
		IF (current = NIL) OR (ctx.task = NIL) THEN RETURN END;
		sp := S.ADR(marker);
		h := ctx.task.holds;
		WHILE h # NIL DO
			n := h.next;
			IF h.waiting & (h.wmark < sp) THEN ReleaseHold(h) END;
			h := n
		END;
		WHILE (ctx.task.holds # NIL) & (ctx.task.holds.mark < sp) DO
			h := ctx.task.holds; ctx.task.holds := h.next;
			ReleaseHold(h);
			GiveHold(ctx, h)
		END
	END Reconcile;

	PROCEDURE Heal (VAR ctx: Ctx);
	(* Вернуть уборщик на стек, если трап его снял, и убрать брошенные
	   кадры, если уборщик отметил трап. Без трапа — две проверки:
	   сверка на каждом входе больше не нужна. *)
	BEGIN
		IF ~ctx.cleanerPushed & (ctx.cleaner # NIL) THEN
			Kernel.PushTrapCleaner(ctx.cleaner);
			ctx.cleanerPushed := TRUE
		END;
		IF ctx.trapped THEN
			ctx.trapped := FALSE;
			Reconcile(ctx)
		END
	END Heal;

	PROCEDURE EnterOp (VAR ctx: Ctx);
	(* Общая точка входа операции. *)
	BEGIN
		IF current # NIL THEN Heal(ctx) END
	END EnterOp;

	(* --- запросы Ctx ------------------------------------------------------ *)

	PROCEDURE StoppedNow (VAR c: Ctx): BOOLEAN;
	(* Stopped без входа в операцию: вызывающий уже сделал Heal. *)
	BEGIN
		IF (c.task # NIL) & c.task.cancel THEN c.cancelled := TRUE END;
		(* expired не сбрасывается: повторно часы не читаем *)
		IF ~c.expired & (c.deadline > 0) & (Kernel.Time() >= c.deadline) THEN c.expired := TRUE END;
		RETURN c.cancelled OR c.expired
	END StoppedNow;

	PROCEDURE (VAR c: Ctx) Stopped* (): BOOLEAN, NEW;
	(** TRUE — задача отменена или срок истёк. *)
	BEGIN
		EnterOp(c);
		RETURN StoppedNow(c)
	END Stopped;

	PROCEDURE (VAR c: Ctx) TaskOf* (): Task, NEW;
	BEGIN
		RETURN c.task
	END TaskOf;

	PROCEDURE (VAR c: Ctx) GroupOf* (): Group, NEW;
	BEGIN
		RETURN c.group
	END GroupOf;

	PROCEDURE (VAR c: Ctx) HeldCount* (): INTEGER, NEW;
	(** Число разных мониторов, взятых задачей через Do. *)
		VAR h: Hold; n: INTEGER;
	BEGIN
		EnterOp(c);
		n := 0; h := Chain(c);
		WHILE h # NIL DO
			IF h.acq THEN INC(n) END;
			h := h.next
		END;
		RETURN n
	END HeldCount;

	PROCEDURE ParkAwait (VAR ctx: Ctx; except: Object);
	(* Усыпить до передачи монитора, отмены или срока. Отмена не трапует:
	   решение принимает Wait, вернув FALSE. except — монитор, в чьём Wait
	   задача спит на глубине 1. *)
	BEGIN
		ASSERT(current # NIL, 22);
		IF current.wake THEN current.wake := FALSE; RETURN END;
		IF (ctx.deadline > 0) & (Kernel.Time() >= ctx.deadline) THEN
			ctx.expired := TRUE; RETURN
		END;
		CheckSleepHolds(except);
		IF ctx.deadline > 0 THEN
			current.deadline := ctx.deadline;
			current.state := Sleeping; INC(current.sleepSeq); PushSleeper(current)
		ELSE
			current.state := Parked
		END;
		Leave;
		current.wake := FALSE;
		IF current.cancel THEN ctx.cancelled := TRUE END;
		IF (ctx.deadline > 0) & (Kernel.Time() >= ctx.deadline) THEN ctx.expired := TRUE END
	END ParkAwait;

	PROCEDURE ParkRaw (except: Object);
	(* Усыпить без проверки остановки: дождаться опустошения группы или
	   монитора. *)
	BEGIN
		ASSERT(current # NIL, 22);
		IF current.wake THEN current.wake := FALSE; RETURN END;
		CheckSleepHolds(except);
		current.state := Parked;
		Leave;
		current.wake := FALSE
	END ParkRaw;

	PROCEDURE AcquireEntry (VAR ctx: Ctx; o: Object; h: Hold): BOOLEAN;
	(* Войти в занятый монитор. FALSE — задача остановлена до входа. *)
		VAR tok: S.ADDRESS; marker: INTEGER;
	BEGIN
		tok := Token();
		WHILE o.owner # tok DO
			IF StoppedNow(ctx) THEN RETURN FALSE END;
			IF o.owner = 0 THEN	(* проснулись без передачи, монитор свободен *)
				o.owner := tok; o.depth := 1;
				RETURN TRUE
			END;
			h.waiting := TRUE; h.wmark := S.ADR(marker);
			current.qkind := entryQ; current.qnext := NIL;
			Append(o.enter, o.enterTail, current);
			ParkAwait(ctx, NIL);
			IF current.qkind # 0 THEN RemoveQueued(o, current) END;
			h.waiting := FALSE;
			IF o.owner = tok THEN RETURN TRUE END;	(* передан вместе с пробуждением *)
			IF StoppedNow(ctx) THEN RETURN FALSE END
		END;
		o.owner := tok; o.depth := 1;
		RETURN TRUE
	END AcquireEntry;

	PROCEDURE AcquireForced (o: Object);
	(* Войти, даже если задача остановлена: Wait обязан вернуть FALSE,
	   держа монитор, чтобы Exclusive мог выйти. *)
		VAR tok: S.ADDRESS;
	BEGIN
		tok := Token();
		WHILE o.owner # tok DO
			IF o.owner = 0 THEN
				o.owner := tok; o.depth := 1; RETURN
			END;
			current.qkind := entryQ; current.qnext := NIL;
			Append(o.enter, o.enterTail, current);
			ParkRaw(o);	(* o — монитор, в чьём Wait задача уже стоит *)
			IF current.qkind # 0 THEN RemoveQueued(o, current) END
		END
	END AcquireForced;

	PROCEDURE HealCurrent;
	(* Перед сном: если уборщик отметил трап, отпустить мониторы снятых
	   кадров, иначе задача уснула бы с чужим монитором в руках. *)
		VAR p: CtxPtr;
	BEGIN
		IF (current # NIL) & (current.pctx # 0) THEN
			p := S.VAL(CtxPtr, current.pctx);
			Heal(p^)
		END
	END HealCurrent;

	PROCEDURE CheckSleepHolds (except: Object);
	(* Перед сном: каждый взятый монитор-объект, кроме except (того, в
	   чьём Wait задача засыпает на глубине 1), должен разрешать ожидание.
	   Быстрый путь: задача не Job или ничего не держит — одно сравнение. *)
		VAR h: Hold;
	BEGIN
		IF current = NIL THEN RETURN END;
		HealCurrent;
		IF current.holds = NIL THEN RETURN END;
		h := current.holds;
		WHILE h # NIL DO
			IF h.acq & (h.obj # NIL) & (h.obj # except) THEN
				IF ~h.obj.HoldsWhileWaiting() THEN
					Msg("Sleep: a held monitor does not allow waiting"); HALT(26)
				END
			END;
			h := h.next
		END
	END CheckSleepHolds;

	PROCEDURE ParkCtx* (VAR ctx: Ctx; external: BOOLEAN): BOOLEAN;
	(** Пред: вызов из этой задачи (22), ctx этой задачи (25). Пост: TRUE —
		задача проснулась по Unpark; FALSE — пропуск остановлен (отмена или
		срок; причина в ctx), и тогда вызов возвращается сразу, не засыпая.
		external — ожидание может кончиться событием извне задач (таймер,
		ввод): планировщик не считает его тупиком. Держа монитор-объект,
		спать можно только с его разрешения HoldsWhileWaiting (26). *)
	BEGIN
		IF current = NIL THEN Msg("ParkCtx outside a task"); HALT(22) END;
		IF ctx.task # current THEN Msg("ParkCtx: pass of another task"); HALT(25) END;
		Heal(ctx);
		IF current.wake THEN
			current.wake := FALSE;
			IF StoppedNow(ctx) THEN RETURN FALSE ELSE RETURN TRUE END
		END;
		IF StoppedNow(ctx) THEN RETURN FALSE END;
		CheckSleepHolds(NIL);
		IF external THEN current.external := TRUE; INC(nExternal) END;
		IF ctx.deadline > 0 THEN
			current.deadline := ctx.deadline;
			current.state := Sleeping; INC(current.sleepSeq); PushSleeper(current)
		ELSE
			current.state := Parked
		END;
		Leave;
		current.wake := FALSE;
		IF current.external THEN	(* срок разбудил спящего мимо MakeReady *)
			current.external := FALSE; DEC(nExternal)
		END;
		IF current.cancel THEN ctx.cancelled := TRUE END;
		IF ~ctx.expired & (ctx.deadline > 0) & (Kernel.Time() >= ctx.deadline) THEN
			ctx.expired := TRUE
		END;
		RETURN ~(ctx.cancelled OR ctx.expired)
	END ParkCtx;

	PROCEDURE (o: Object) Do* (VAR ctx: Ctx; VAR op: Op), NEW;
	(** Пред: o # NIL (20), вызов из задачи (22), ctx этой задачи (25).
		Пост: Exclusive выполнен под монитором; при трапе внутри
		Exclusive мониторы задачи отпускаются по отметкам кадров. *)
		VAR h: Hold; tok: S.ADDRESS; marker: INTEGER; acq, entered: BOOLEAN;
	BEGIN
		IF o = NIL THEN Msg("Do: NIL object"); HALT(20) END;
		IF current = NIL THEN Msg("Do outside a task"); HALT(22) END;
		IF ctx.task # current THEN Msg("Do: pass of another task"); HALT(25) END;
		Heal(ctx);
		IF StoppedNow(ctx) THEN RETURN END;
		tok := Token();
		h := TakeHold(ctx);
		h.obj := o; h.next := ctx.task.holds; h.mark := S.ADR(marker);
		h.waiting := FALSE; h.acq := FALSE;
		ctx.task.holds := h;
		acq := FALSE; entered := FALSE;
		IF o.owner = tok THEN
			INC(o.depth); entered := TRUE
		ELSIF o.owner = 0 THEN
			o.owner := tok; o.depth := 1; acq := TRUE; entered := TRUE
		ELSE
			entered := AcquireEntry(ctx, o, h);
			acq := entered
		END;
		h.acq := acq;
		IF entered THEN
			o.Exclusive(ctx, op);
			Heal(ctx);	(* трап внутри Exclusive мог снять внутренние кадры *)
			IF acq THEN ReleaseOne(o) ELSIF o.depth > 0 THEN DEC(o.depth) END
		END;
		IF ctx.task.holds = h THEN
			ctx.task.holds := h.next
		ELSE
			(* Более глубокие записи сняты трапом; текущая — вершина. *)
			ASSERT(ctx.task.holds = h, 100)
		END;
		GiveHold(ctx, h)
	END Do;

	PROCEDURE WaitCommon (o: Object; VAR ctx: Ctx; cond: Cond; need: SET): BOOLEAN;
	(* Общее тело Wait (cond = NIL, ждём флаги need) и WaitFor (cond # NIL).
	   Предусловия и сообщения проверяют обёртки. *)
		VAR h: Hold; tok: S.ADDRESS; first, isCond: BOOLEAN; marker: INTEGER;
	BEGIN
		Heal(ctx);
		tok := Token();
		IF o.owner # tok THEN Msg("Wait outside Exclusive"); HALT(21) END;
		IF o.depth # 1 THEN Msg("Wait on a nested entry"); HALT(23) END;
		h := ctx.task.holds;
		IF (h = NIL) OR (h.obj # o) OR ~h.acq THEN
			Msg("Wait: no Do record"); HALT(100)
		END;
		isCond := cond # NIL;
		IF StoppedNow(ctx) THEN RETURN FALSE END;
		first := TRUE;
		LOOP
			IF isCond THEN
				IF cond.Ready(o) THEN RETURN TRUE END
			ELSIF (need * o.Ready()) # {} THEN RETURN TRUE
			END;
			IF StoppedNow(ctx) THEN RETURN FALSE END;
			h.waiting := TRUE; h.wmark := S.ADR(marker);
			current.qnext := NIL;
			IF isCond THEN
				current.qkind := condQ; current.qcond := cond; current.qneed := {};
				IF first THEN Append(o.condq, o.condTail, current)
				ELSE Prepend(o.condq, o.condTail, current)
				END
			ELSE
				current.qkind := flagQ; current.qneed := need; current.qcond := NIL;
				IF first THEN Append(o.flagq, o.flagTail, current)
				ELSE Prepend(o.flagq, o.flagTail, current)
				END
			END;
			first := FALSE;
			ReleaseMonitor(o);
			ParkAwait(ctx, o);
			IF o.owner = tok THEN
				(* монитор передан этой задаче *)
			ELSE
				IF current.qkind # 0 THEN RemoveQueued(o, current) END;
				AcquireForced(o)
			END;
			IF current.qkind # 0 THEN RemoveQueued(o, current) END;
			h.waiting := FALSE;
			o.depth := 1;
			IF StoppedNow(ctx) THEN RETURN FALSE END
		END
	END WaitCommon;

	PROCEDURE (o: Object) Wait* (VAR ctx: Ctx; need: SET): BOOLEAN, NEW;
	(** Пред: монитор у вызывающей задачи (21), глубина 1 (23), сон с
		удерживаемыми мониторами разрешён (26), вызов из задачи (22), ctx этой
		задачи (25). Пост: TRUE — условие истинно, монитор у задачи;
		FALSE — задача остановлена, монитор тоже у задачи. *)
	BEGIN
		IF o = NIL THEN Msg("Wait: NIL object"); HALT(20) END;
		IF current = NIL THEN Msg("Wait outside a task"); HALT(22) END;
		IF ctx.task # current THEN Msg("Wait: pass of another task"); HALT(25) END;
		RETURN WaitCommon(o, ctx, NIL, need)
	END Wait;

	PROCEDURE (o: Object) WaitFor* (VAR ctx: Ctx; c: Cond): BOOLEAN, NEW;
	(** То же, что Wait, но условие — объект c.Ready(o). *)
	BEGIN
		IF o = NIL THEN Msg("WaitFor: NIL object"); HALT(20) END;
		IF c = NIL THEN Msg("WaitFor: NIL condition"); HALT(25) END;
		IF current = NIL THEN Msg("WaitFor outside a task"); HALT(22) END;
		IF ctx.task # current THEN Msg("WaitFor: pass of another task"); HALT(25) END;
		RETURN WaitCommon(o, ctx, c, {})
	END WaitFor;

	(* --- дети пропуска ---------------------------------------------------- *)

	PROCEDURE Spawn* (VAR ctx: Ctx; t: Task);
	(** Пред: t # NIL (20), ctx этой задачи (25). Пост: t запущена в
		группе пропуска; после остановки задача не запускается. *)
		VAR g: Group;
	BEGIN
		IF ctx.task # current THEN Msg("Spawn: pass of another task"); HALT(25) END;
		IF t = NIL THEN Msg("Spawn: NIL task"); HALT(20) END;
		EnterOp(ctx);
		IF StoppedNow(ctx) THEN Msg("Spawn: pass stopped"); RETURN END;
		IF ctx.group = NIL THEN
			NEW(g); g.alive := 0; g.failed := FALSE; g.owner := NIL;
			ctx.group := g
		END;
		StartIn(t, ctx.group)
	END Spawn;

	PROCEDURE JoinAll* (VAR ctx: Ctx);
	(** Дождаться завершения всех дочерних задач пропуска; при остановке
		сначала отменить их. *)
		VAR g: Group;
	BEGIN
		IF ctx.task # current THEN Msg("JoinAll: pass of another task"); HALT(25) END;
		EnterOp(ctx);
		g := ctx.group;
		IF g = NIL THEN RETURN END;
		ASSERT((g.owner = NIL) OR (g.owner = ctx.task), 100);
		g.owner := ctx.task;
		WHILE g.alive > 0 DO
			IF StoppedNow(ctx) THEN
				CancelGroup(g);
				ParkRaw(NIL)
			ELSE
				ParkAwait(ctx, NIL)
			END
		END;
		g.owner := NIL
	END JoinAll;

	PROCEDURE SetDeadline* (VAR ctx: Ctx; ms: LONGINT);
	(** Пред: ctx этой задачи (25). Пост: срок — ms мс от сейчас;
		0 или меньше — срок уже наступил. *)
	BEGIN
		IF ctx.task # current THEN Msg("SetDeadline: pass of another task"); HALT(25) END;
		IF ms > 0 THEN ctx.deadline := Kernel.Time() + ms ELSE ctx.deadline := Kernel.Time() END
	END SetDeadline;

	(* --- задача Job ------------------------------------------------------- *)

	PROCEDURE (t: Job) Body- (VAR ctx: Ctx), NEW, ABSTRACT;

	PROCEDURE FinishCtx (VAR ctx: Ctx);
	(* Отпустить всё, что осталось после Body, и погасить пропуск. *)
		VAR h: Hold;
	BEGIN
		ctx.cleaner := NIL;
		ctx.cleanerPushed := FALSE;
		Reconcile(ctx);
		WHILE ctx.task.holds # NIL DO
			h := ctx.task.holds; ctx.task.holds := h.next;
			ReleaseHold(h);
			GiveHold(ctx, h)
		END;
		IF ctx.group # NIL THEN
			IF ctx.group.alive > 0 THEN CancelGroup(ctx.group) END;
			ctx.group.owner := NIL
		END;
		(* Свободные записи вызовов переживают запуск: повторный Run без
		   выделений. ctx.task = NIL — пропуск уже погашен. *)
		IF ctx.task # NIL THEN ctx.task(Job).holdFree := ctx.free END;
		ctx.free := NIL;
		ctx.task := NIL
	END FinishCtx;

	PROCEDURE FinishTaskPass (t: Task);
		VAR ctx: POINTER TO Ctx;
	BEGIN
		IF t.pctx = 0 THEN RETURN END;
		ctx := S.VAL(CtxPtr, t.pctx);
		t.pctx := 0;
		FinishCtx(ctx^)
	END FinishTaskPass;

	PROCEDURE (c: Cleaner) Cleanup;
		VAR ctx: POINTER TO Ctx;
	BEGIN
		IF c.ctxAdr # 0 THEN
			ctx := S.VAL(CtxPtr, c.ctxAdr);
			ctx^.trapped := TRUE;
			ctx^.cleanerPushed := FALSE
		END
	END Cleanup;

	PROCEDURE (t: Job) Run* ();
	(** Реализация Run задачи с пропуском: Ctx на стеке, одно тело. *)
		VAR ctx: Ctx; cleaner: Cleaner;
	BEGIN
		ctx.task := t; ctx.group := NIL;
		ctx.cancelled := FALSE; ctx.expired := FALSE;
		ctx.deadline := 0;
		ctx.trapped := FALSE; ctx.cleanerPushed := FALSE;
		cleaner := t.cleaner;
		IF cleaner = NIL THEN NEW(cleaner); t.cleaner := cleaner END;
		cleaner.ctxAdr := S.ADR(ctx); ctx.cleaner := cleaner;
		ctx.free := t.holdFree; t.holdFree := NIL;
		t.pctx := S.ADR(ctx);
		Kernel.PushTrapCleaner(cleaner); ctx.cleanerPushed := TRUE;
		t.Body(ctx);
		JoinAll(ctx);
		IF ctx.cleanerPushed THEN
			Kernel.PopTrapCleaner(cleaner); ctx.cleanerPushed := FALSE
		END;
		FinishCtx(ctx);
		t.pctx := 0
	END Run;

	(* --- диагностика ------------------------------------------------------ *)

	PROCEDURE Held* (o: Object): BOOLEAN;
	(** TRUE — монитор кем-то взят. *)
	BEGIN
		RETURN (o # NIL) & (o.owner # 0)
	END Held;

	PROCEDURE Depth* (o: Object): INTEGER;
	(** Глубина повторного входа владельца; 0 — свободен. *)
	BEGIN
		IF o = NIL THEN RETURN 0 END;
		RETURN o.depth
	END Depth;

	PROCEDURE Queued* (o: Object): INTEGER;
	(** Записей в очередях входа, флагов и условий. *)
		VAR t: Task; n: INTEGER;
	BEGIN
		n := 0;
		IF o # NIL THEN
			t := o.enter; WHILE t # NIL DO INC(n); t := t.qnext END;
			t := o.flagq; WHILE t # NIL DO INC(n); t := t.qnext END;
			t := o.condq; WHILE t # NIL DO INC(n); t := t.qnext END
		END;
		RETURN n
	END Queued;

	(* --- планировщик ------------------------------------------------------ *)

	PROCEDURE HasReady (): BOOLEAN;
	BEGIN
		RETURN readyHead # NIL
	END HasReady;

	PROCEDURE NextDeadline* (): LONGINT;
	(** Ближайший срок спящей задачи в мс Kernel.Time; -1 — нет. *)
	BEGIN
		WHILE (heapN > 0) & ~Valid(heap[0]) DO PopTop END;
		IF heapN = 0 THEN RETURN -1 ELSE RETURN heap[0].deadline END
	END NextDeadline;

	PROCEDURE EnsureScheduled;
	(* Назначить шаг планировщика в главном цикле Services: немедленно,
	   если есть готовые задачи, иначе на ближайший срок спящих.
	   Инвариант: scheduled => шаг назначен на schedAt. Уже назначенный шаг
	   переносится, если нужен более ранний срок: Unpark или Cancel с
	   главного стека делает задачу готовой, когда шаг ждёт срока спящей.
	   Пост: есть готовые => шаг назначен не позже Services.now. *)
		VAR d: LONGINT;
	BEGIN
		IF HasReady() THEN d := Services.now ELSE d := NextDeadline() END;
		IF (d >= 0) & (~scheduled OR (d < schedAt)) THEN
			scheduled := TRUE; schedAt := d; Services.DoLater(sched, d)
		END
	END EnsureScheduled;

	PROCEDURE CheckDeadlock;
	(** Все живые задачи ждут, сроков нет и ни одна не ждёт события извне
		(ParkExternal): печатаем журнал ядра «кто чего ждёт» один раз до
		следующего пробуждения. *)
	BEGIN
		IF (alive > 0) & (readyHead = NIL) & (nExternal = 0) & (NextDeadline() < 0) THEN
			IF ~deadlocked THEN
				deadlocked := TRUE;
				Msg("deadlock: no ready tasks and no wake-up deadlines");
				Monitors.DumpMonitors
			END
		ELSE
			deadlocked := FALSE
		END
	END CheckDeadlock;

	PROCEDURE Step*;
	(** Исполнить готовые задачи, не дольше slice мс. Вызывается со дна
		главного цикла (действие планировщика) или тестом. *)
		VAR t: Task; t0, now: LONGINT; schedStack: Kernel.Stack;
	BEGIN
		IF current # NIL THEN Msg("Step called from a task") END;
		ASSERT(current = NIL, 20);
		IF inStep THEN Msg("Step is not reentrant") END;
		ASSERT(~inStep, 21);
		IF atomic > 0 THEN Msg("Step inside an atomic section") END;
		ASSERT(atomic = 0, 22);
		Services.SetTrapGuardCleanup;	(* страховка от трапа в прошлом действии *)
		inStep := TRUE;
		(* Запись своего стека ищем один раз на шаг; задачи свои записи знают.
		   Переключение обходится без поиска в реестре стеков. *)
		schedStack := Kernel.StackOf(S.ADR(t0));
		t0 := Kernel.Time(); now := t0;
		LOOP
			(* Инвариант: now — время, прочитанное после последнего переключения. *)
			IF now - lastPoll >= pollPeriod THEN	(* ключ точек уступки *)
				Kernel.RequestPoll; lastPoll := now
			END;
			(* Спящих просматриваем не чаще раза на изменившееся время. *)
			IF now # lastWake THEN lastWake := now; WakeSleepers(now) END;
			t := Dequeue();
			IF t = NIL THEN CheckDeadlock; EXIT END;
			SetCurrent(t); t.state := Running;
			SwitchTo(schedSP, t.sp, schedStack);
			t := current; SetCurrent(NIL); direct := 0;	(* вернулась, возможно, другая задача *)
			atomic := 0;	(* вернуться из участка можно, только кончив задачу трапом: участок брошен *)
			IF IsDone(t) THEN Reap(t) END;
			now := Kernel.Time();
			IF now - t0 >= slice THEN EXIT END
		END;
		TrimPool;
		inStep := FALSE;
		IF alive > 0 THEN EnsureScheduled END
	END Step;

	PROCEDURE (a: SchedAction) Do;
	BEGIN
		scheduled := FALSE;
		Step
	END Do;

	(* --- запросы ---------------------------------------------------------- *)

	PROCEDURE Current* (): Task;
	BEGIN
		RETURN current
	END Current;

	PROCEDURE InTask* (): BOOLEAN;
	BEGIN
		RETURN current # NIL
	END InTask;

	PROCEDURE State* (t: Task): INTEGER;
	BEGIN
		IF t = NIL THEN RETURN New END;
		RETURN t.state
	END State;

	PROCEDURE GetName* (t: Task; OUT s: ARRAY OF CHAR);
	BEGIN
		IF t = NIL THEN s := "" ELSE s := t.name$ END
	END GetName;

	PROCEDURE SetName* (t: Task; IN s: ARRAY OF CHAR);
	BEGIN
		IF t # NIL THEN t.name := s$ END
	END SetName;

	PROCEDURE Failed* (t: Task): BOOLEAN;
	BEGIN
		RETURN (t # NIL) & t.failed
	END Failed;

	PROCEDURE SetStackSize* (t: Task; size: INTEGER);
	(** Пред: t # NIL (трап 20), t ещё не запущена (21), size >= 0 (22).
		Пост: стек t — size байт; 0 — размер по умолчанию. *)
	BEGIN
		ASSERT(t # NIL, 20); ASSERT(t.state = New, 21); ASSERT(size >= 0, 22);
		t.stackSize := size
	END SetStackSize;

	PROCEDURE Count* (): INTEGER;
	BEGIN
		RETURN alive
	END Count;

	PROCEDURE BeginAtomic*;
	(** Начать неделимый участок; участки вкладываются. Участок, внутри
		которого задача кончилась трапом, кончается вместе с ней.
		Пост: до парного EndAtomic ожидание (Yield, Sleep, Join, Park, вход в
		занятый монитор, AWAIT) — трап 25, Step — трап 22, точка уступки не
		переключает. *)
	BEGIN
		INC(atomic)
	END BeginAtomic;

	PROCEDURE EndAtomic*;
	(** Пред: идёт неделимый участок (трап 20). Пост: участок закрыт. *)
	BEGIN
		IF atomic <= 0 THEN Msg("EndAtomic without BeginAtomic") END;
		ASSERT(atomic > 0, 20);
		DEC(atomic)
	END EndAtomic;

	PROCEDURE Deadlocked* (): BOOLEAN;
	(** Планировщик упёрся в «все ждут, сроков нет» и напечатал журнал. *)
	BEGIN
		RETURN deadlocked
	END Deadlocked;

	PROCEDURE Switches* (): LONGINT;
	BEGIN
		RETURN switches
	END Switches;

	PROCEDURE ResetStats*;
	BEGIN
		switches := 0
	END ResetStats;

	PROCEDURE ListSize* (): INTEGER;
	(** Длина списка всех задач (диагностика: после Reap завершённых в нём
		быть не должно). *)
		VAR t: Task; n: INTEGER;
	BEGIN
		n := 0; t := all;
		WHILE t # NIL DO INC(n); t := t.next END;
		RETURN n
	END ListSize;

	PROCEDURE List*;
	(** Список задач для отладки: имя, состояние, стек, срок. *)
		VAR t: Task; s: ARRAY 16 OF CHAR; st: ARRAY 16 OF CHAR;
	BEGIN
		t := all;
		WHILE t # NIL DO
			StateName(t.state, st);
			Console.WriteStr("Tasks: "); IF t.name # "" THEN Console.WriteStr(t.name) ELSE Console.WriteStr("?") END;
			Console.WriteStr(" "); Console.WriteStr(st);
			Strings.IntToString(t.lo, s); Console.WriteStr(" stack="); Console.WriteStr(s);
			Strings.IntToString(t.hi - t.lo, s);
			Console.WriteStr("+"); Console.WriteStr(s);
			IF t.state = Sleeping THEN
				Strings.IntToString(t.deadline, s); Console.WriteStr(" deadline="); Console.WriteStr(s)
			END;
			Console.WriteLn;
			t := t.next
		END
	END List;

(* --- крючок планировщика для ядра ------------------------------------- *)

	PROCEDURE (s: Sched) park;
	BEGIN
		ParkMonitor
	END park;

	PROCEDURE (s: Sched) unpark (a: S.ADDRESS);
	(** Пробуждение ядром (передача монитора): НЕ трогать пользовательский
		флаг wake — иначе следующий Tasks.Park вернулся бы сразу, не дождавшись
		своего события. *)
		VAR t: Task;
	BEGIN
		IF a # 0 THEN
			t := S.VAL(Task, a);
			IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
		END
	END unpark;

	PROCEDURE (s: Sched) name (a: S.ADDRESS; OUT name: ARRAY OF CHAR);
	BEGIN
		IF a = 0 THEN name := "?"
		ELSE GetName(S.VAL(Task, a), name)
		END
	END name;

	PROCEDURE (s: Sched) poll (): BOOLEAN;
	(** Уступка из Kernel.Poll: только в задаче, вне каркаса и вне
		монитора. Отмена проверяется в Yield (CheckWait): Poll — тоже
		точка отмены. *)
	BEGIN
		IF (current # NIL) & (atomic = 0) & ~InFramework() & (Monitors.LockDepth() = 0) THEN
			Yield; RETURN TRUE
		ELSE
			RETURN FALSE
		END
	END poll;

BEGIN
	NEW(sched);
	NEW(schedHook);
	slice := defaultSlice;
	scheduled := FALSE; inStep := FALSE; deadlocked := FALSE;
	lastPoll := Kernel.Time();
	lastWake := -1;	(* первый Step обязан просмотреть спящих *)
	Kernel.SetScheduler(schedHook)
CLOSE
	(* Выгрузка/завершение мира: в ядре не должно остаться указателей на
	   процедуры этого модуля, а стеки живых задач надо вернуть платформе.
	   Сначала живые задачи отменяются и доводятся до конца: трап 127 в
	   точке ожидания снимает их мониторы (Kernel.Try в TaskBoot, endTry) и
	   записи в очередях Monitors (purge), пока их кадры ещё на живых стеках.
	   Задача, которая не дошла до конца за closeDrain мс (поймала 127 и
	   ждёт дальше), остаётся, её стек освобождается как есть. *)
	IF (current = NIL) & ~inStep THEN
		closeT := all; WHILE closeT # NIL DO Cancel(closeT); closeT := closeT.next END;
		closeT0 := Kernel.Time();
		WHILE (alive > 0) & (Kernel.Time() - closeT0 < closeDrain) DO Step END
	END;
	Services.RemoveAction(sched);
	Kernel.SetScheduler(NIL);
	WHILE all # NIL DO
		IF all.stackRec # NIL THEN Kernel.UnregisterStack(all.stackRec); all.stackRec := NIL END;
		Kernel.FreeStack(all.lo, all.hi);
		all.lo := 0; all.hi := 0;
		all := all.next
	END;
	DrainPool;
	readyHead := NIL; readyTail := NIL; heap := NIL; heapN := 0; nSleeping := 0; nExternal := 0; SetCurrent(NIL);
	scheduled := FALSE; inStep := FALSE
END Tasks.
