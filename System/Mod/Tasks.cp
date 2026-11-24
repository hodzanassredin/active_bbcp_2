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
	20–25 с текстом в Console (номер — по порядку проверок в процедуре);
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
		poolMax = 1024;		(* сколько свободных стеков пул держит в покое *)
		trimStep = 16;		(* сколько лишних стеков шаг планировщика возвращает ядру *)
		word = Taskarch.word;
		defaultSlice = 5;	(* мс на шаг планировщика *)
		pollPeriod = 5;		(* мс: как часто ключ уступки поднимает флаг Poll *)
		heapArity = 4;		(* детей у узла кучи спящих *)
		heapMin = 64;		(* начальная длина кучи и запас перед уплотнением *)
		closeDrain = 1000;	(* мс: сколько CLOSE доводит отменённые задачи до конца *)

		(** состояния задачи **)
		New* = 0; Ready* = 1; Running* = 2; Sleeping* = 3;
		Waiting* = 4; Parked* = 5; Finished* = 6; Cancelled* = 7;

		entryOff = Taskarch.entryOff;	(* адрес входа от sp нового стека *)
		stackHead = Taskarch.stackHead;	(* запас у верха стека *)

	TYPE
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
			name: ARRAY nameLen OF CHAR
		END;

		Group* = POINTER TO RECORD
			alive: INTEGER;		(* живых участников *)
			failed: BOOLEAN;	(* трап хотя бы в одном *)
			owner: Task		(* задача, которая ждёт опустошения *)
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
			t.state := Parked;
			SwitchTo(t.sp, schedSP, t.stackRec)
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
		t.stackRec := Kernel.RegisterStack(lo, hi);
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
		IF t.state # New THEN Msg("task already started or finished") END;
		ASSERT(t.state = New, 21)
	END CheckTask;

	PROCEDURE StartIn* (t: Task; g: Group);
	(** Пред: t # NIL (трап 20), t ещё не запущена (21), платформа дала
		стек (22). Пост: t готова к исполнению; g # NIL — t в группе g. *)
	BEGIN
		CheckTask(t);
		IF ~InitStack(t) THEN Msg("no stack memory (platform does not support it)"); HALT(22) END;
		ReserveHeap(2 * (alive + 1) + heapMin);	(* PushSleeper не выделяет: heapN < 2 * nSleeping + heapMin *)
		IF g # NIL THEN t.group := g; INC(g.alive) END;
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
		ссылаются, живут до конца процесса. *)
	BEGIN
		IF t.prev = NIL THEN all := t.next ELSE t.prev.next := t.next END;
		IF t.next # NIL THEN t.next.prev := t.prev END;
		t.next := NIL; t.prev := NIL;
		IF t.stackRec # NIL THEN Kernel.UnregisterStack(t.stackRec); t.stackRec := NIL END;
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
		SwitchTo(current.sp, schedSP, current.stackRec)
	END Yield;

	PROCEDURE Sleep* (ms: LONGINT);
	BEGIN
		CheckWait("Sleep");
		IF ms <= 0 THEN Yield; RETURN END;
		current.deadline := Kernel.Time() + ms;
		current.state := Sleeping; INC(current.sleepSeq);
		PushSleeper(current);
		SwitchTo(current.sp, schedSP, current.stackRec);
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
			(* Инвариант: пока t не завершена, current в очереди t.waiters ровно
			   один раз; WakeWaiters снимает всю очередь при Reap(t). *)
			current.wnext := t.waiters; t.waiters := current;
			REPEAT
				current.state := Waiting;
				SwitchTo(current.sp, schedSP, current.stackRec);
				IF current.cancel & ~IsDone(t) THEN RemoveWaiter(t, current) END;
				CheckWait("Join")
			UNTIL IsDone(t)
		END
	END Join;

	PROCEDURE Park*;
	(** Усыпить себя до Unpark. Пробуждение до Park не теряется. *)
	BEGIN
		CheckWait("Park");
		IF current.wake THEN current.wake := FALSE; RETURN END;
		current.state := Parked;
		SwitchTo(current.sp, schedSP, current.stackRec);
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
		current.external := TRUE; INC(nExternal);
		current.state := Parked;
		SwitchTo(current.sp, schedSP, current.stackRec);
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
		current.state := Parked;
		SwitchTo(current.sp, schedSP, current.stackRec);
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
			current := t; t.state := Running;
			SwitchTo(schedSP, t.sp, schedStack);
			current := NIL;
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

	PROCEDURE (s: Sched) current (): S.ADDRESS;
	(** Токен текущей задачи для Monitors; 0 — главный стек. *)
	BEGIN
		IF current # NIL THEN RETURN S.VAL(S.ADDRESS, S.ADR(current^)) ELSE RETURN 0 END
	END current;

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
	readyHead := NIL; readyTail := NIL; heap := NIL; heapN := 0; nSleeping := 0; nExternal := 0; current := NIL;
	scheduled := FALSE; inStep := FALSE
END Tasks.
