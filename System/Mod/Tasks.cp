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
	20–24 с текстом в Console (номер — по порядку проверок в процедуре);
	100 — нарушен инвариант планировщика.

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
		slabN = 64;		(* стеков в плите (Kernel.AllocStacks) *)
		denseN = 64;		(* стеков в плотной плите (DenseDir) *)
		denseMax = 32 * 1024 * 1024;	(* наибольшая плотная плита, байт *)
		denseMinChecked = 2048;	(* наименьший стек выше запаса плотной плиты *)
		canaryVal = 5AFEC0DEH;	(* слово у дна плотного стека *)
		stackChkOpt = 12;	(* бит Kernel.Module.opts: модуль собран с ключом % (Compiler64.stackChk;
			DevCPE пишет в объектный файл биты опций 0..15) *)
		denseReserve = 8 * 1024;	(* запас UseDenseStacks: глубина кода без проверки по замеру
			(docs/stacks-study.md, 6.5) — не больше 7,8 КБ на всех тестах *)
		segReserve = 2 * 1024;	(* запас куска и стартового стека пробы: рост и код без проверки *)
		segBase = 4 * 1024;		(* стартовый кусок (стек задачи) *)
		segGrowth = 8 * 1024;	(* кусок роста *)
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
			state: INTEGER;	(* рядом с sleepSeq: два INTEGER в одном слове *)
			wnext: Task;		(* очередь ждущих эту задачу *)
			waiters: Task;
			sp: S.ADDRESS;		(* сохранённый rsp *)
			lo, hi: S.ADDRESS;	(* границы стека [lo, hi) *)
			stackRec: Kernel.Stack;
			dir: StackDir;		(* поставщик, выдавший стек: ему стек и возвращается *)
			deadline: LONGINT;	(* до какого Kernel.Time спать *)
			stackSize: INTEGER;
			cancel, wake, failed, bodyOk: BOOLEAN;
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

		(** Поставщик стеков задач — каталог в духе Files.Directory. Tasks держит
			у каждого поставщика пул свободных стеков и берёт стек из пула без
			вызова поставщика; поставщика зовёт только промах пула, уборка пула
			(TrimPool) и опустошение. Поле reserve > 0 значит, что под стеком
			нет сторожевой страницы: Kernel.stkLo = lo + reserve, у дна (lo)
			лежит канарейка, а код задачи обязан быть собран с проверкой в
			прологе (ключ %, checked). *)
		StackDir* = POINTER TO ABSTRACT RECORD
			pool: S.ADDRESS;	(* свободные стеки: список через верхние слова, см. poolHead ниже *)
			poolCount: INTEGER;
			live: INTEGER;		(* стеков у задач *)
			reserve: INTEGER;	(* 0 — сторож под стеком; иначе запас ниже Kernel.stkLo *)
			checked: BOOLEAN;	(* код задачи должен быть собран с ключом % *)
			segmented: BOOLEAN;	(* стеки растут кусками (проба): пролог зовёт GrowStack *)
			keep: BOOLEAN;		(* стеки не возвращаются ядру по одному (FreeStack не зовётся) *)
			listed: BOOLEAN;	(* в списке dirs *)
			nextDir: StackDir
		END;

		GuardDir = POINTER TO RECORD (StackDir) END;
		SlabDir = POINTER TO RECORD (StackDir)
			(* Плита: стеки [slabNext + g, slabNext + slabStep) ... до slabEnd ещё не выданы;
			   у каждого снизу сторожевая страница g. Инвариант: slabNext = slabEnd — плиты нет. *)
			slabNext, slabEnd: S.ADDRESS;
			slabStep, slabGuard, slabSize: INTEGER
		END;
		DenseSlab = POINTER TO RECORD lo, hi: S.ADDRESS; next: DenseSlab END;
		DenseDir = POINTER TO EXTENSIBLE RECORD (StackDir)
			(* Плотная плита: стеки вплотную, одна сторожевая страница под плитой.
			   Невыданные — [next, end) снизу вверх, шаг step. *)
			next, end: S.ADDRESS;
			step: INTEGER;
			slabs: DenseSlab
		END;
		SegDir = POINTER TO RECORD (DenseDir) END;	(* плотные стартовые стеки, растут кусками *)

		ProbeA = POINTER TO RECORD (Task) END;	(* два Run: по разнице таблиц методов — номер Run *)
		ProbeB = POINTER TO RECORD (Task) END;

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
		alive: INTEGER;		(* незавершённых задач *)
		slice: LONGINT;
		lastPoll: LONGINT;	(* когда Kernel.RequestPoll поднимался последний раз *)
		lastWake: LONGINT;	(* время последнего просмотра спящих в Step *)
		direct: INTEGER;	(* прямых переключений с последнего возврата в Step *)
		switches: LONGINT;	(* переключений стека, диагностика *)
		deadlocked: BOOLEAN;	(* все ждут, сроков нет; журнал уже напечатан *)
		(* Пул свободных стеков поставщика d: односвязный список через верхние
		   слова самого стека. Инвариант: d.pool = 0 равносильно d.poolCount = 0;
		   иначе d.pool — верх hi свободного стека, в [hi - word] — верх следующего,
		   в [hi - 2*word] — его lo, в [hi - 3*word] — размер, с которым стек
		   запрошен. Стек в пуле не зарегистрирован в реестре ядра и не сканируется. *)
		dir: StackDir;		(* поставщик для новых задач *)
		guardDir: StackDir;	(* поставщик со сторожем: по умолчанию и для кода без ключа % *)
		dirs: StackDir;		(* поставщики, у которых есть стеки (живые или в пуле) *)
		canaryCell: S.ADDRESS;	(* значение канарейки (canaryVal) в слове: сравнение без расширения константы *)
		runSlot: INTEGER;	(* номер Run в таблице методов; -1 — не найден *)
		lastType: Kernel.Type; lastChecked: BOOLEAN;	(* итог RunChecked для последнего типа *)
		(* Проход Step: его стек и начало, итог RunLoop. oneTry — один Kernel.Try
		   на проход (TryTasks); FALSE — платформа его не даёт, Try на задачу. *)
		stepStack: Kernel.Stack;
		stepT0: LONGINT;
		loopDone, oneTry: BOOLEAN;
		stackDefault: INTEGER;	(* размер стека по умолчанию, SetDefaultStackSize *)
		closeT: Task; closeT0: LONGINT;	(* для CLOSE *)

	PROCEDURE (t: Task) Run* (), NEW, ABSTRACT;

	PROCEDURE (p: ProbeA) Run;
	BEGIN
	END Run;

	PROCEDURE (p: ProbeB) Run;
	BEGIN
		HALT(100)
	END Run;

	PROCEDURE FindRunSlot;
	(* Пост: runSlot — номер метода Run: единственный слот, в котором таблицы
	   методов ProbeA и ProbeB расходятся (методы ANYREC у них общие). *)
		VAR a: ProbeA; b: ProbeB; ta, tb, x, y: S.ADDRESS; n: INTEGER;
	BEGIN
		NEW(a); NEW(b);
		ta := S.VAL(S.ADDRESS, Kernel.TypeOf(a^)); tb := S.VAL(S.ADDRESS, Kernel.TypeOf(b^));
		runSlot := -1; n := 0;
		WHILE (runSlot < 0) & (n < 16) DO
			S.GET(ta - word * (n + 1), x); S.GET(tb - word * (n + 1), y);
			IF x # y THEN runSlot := n END;
			INC(n)
		END
	END FindRunSlot;

	PROCEDURE ModuleChecked* (m: Kernel.Module): BOOLEAN;
	(** Модуль m собран с ключом % (проверка границы стека в прологе). Пред: m # NIL. *)
	BEGIN
		RETURN stackChkOpt IN m.opts
	END ModuleChecked;

	PROCEDURE RunChecked* (t: Task): BOOLEAN;
	(** Метод Run задачи t (её динамического типа) собран с ключом %. Признак
		модуля, а не всего кода: Run может звать модули без ключа и C, их
		покрывает запас поставщика. *)
		VAR typ: Kernel.Type; adr: S.ADDRESS; m: Kernel.Module; offs: INTEGER;
	BEGIN
		ASSERT(t # NIL, 20);
		typ := Kernel.TypeOf(t^);
		IF typ # lastType THEN
			lastChecked := FALSE;
			IF runSlot >= 0 THEN
				S.GET(S.VAL(S.ADDRESS, typ) - word * (runSlot + 1), adr);
				Kernel.SearchProcVar(adr, m, offs);
				lastChecked := (m # NIL) & ModuleChecked(m)
			END;
			lastType := typ
		END;
		RETURN lastChecked
	END RunChecked;

	PROCEDURE (d: StackDir) NewStack- (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN, NEW, ABSTRACT;
	(** Пред: size > 0. Пост: TRUE — [lo, hi) новый стек не меньше size байт; FALSE — памяти нет.
		Зовётся только при пустом пуле: стеки из пула Tasks выдаёт сам. *)

	PROCEDURE (d: StackDir) FreeStack- (lo, hi: S.ADDRESS), NEW, ABSTRACT;
	(** Вернуть ядру стек, выданный NewStack. Не зовётся у поставщика с keep. *)

	PROCEDURE (d: StackDir) Drain-, NEW, EMPTY;
	(** Пул пуст и стеков у задач нет: вернуть ядру всё, что поставщик держит. *)

	PROCEDURE (d: StackDir) Overflow- (t: Task), NEW, EMPTY;
	(** Точка расширения: канарейка у дна стека t испорчена — код без проверки
		в прологе ушёл ниже запаса, соседний стек плиты мог быть испорчен.
		Зовётся до трапа 126 в задаче (в точке переключения, до ухода с процессора) или до пометки
		её ошибочной (при уборке). Роста стека нет. *)

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

	PROCEDURE SetCurrent (t: Task);
	(* Пост: current = t; Kernel.monTok — адрес t^ (Kernel.mainToken при
	   t = NIL); Kernel.stkLo — низ стека t плюс запас его поставщика (0 при t = NIL); взятия мониторов быстрым путём прежнего токена отданы
	   таблице Monitors до смены токена. *)
	BEGIN
		IF Kernel.monTop # Kernel.monBase THEN Kernel.FlushMonitors END;
		current := t;
		IF t # NIL THEN Kernel.monTok := S.ADR(t^)
		ELSE Kernel.monTok := Kernel.mainToken; Kernel.stkLo := 0
		END
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

	PROCEDURE StackBroken (t: Task; inTask: BOOLEAN);
	(* Канарейка t испорчена. В задаче — трап 126; при уборке — задача ошибочна. *)
	BEGIN
		Msg("task stack corrupted: code without the stack check went below the reserve; the neighbour stack may be damaged");
		S.PUT(t.lo, canaryCell);	(* одно сообщение на порчу *)
		t.dir.Overflow(t);
		IF inTask THEN HALT(126)
		ELSE
			t.failed := TRUE;
			IF t.group # NIL THEN t.group.failed := TRUE END
		END
	END StackBroken;

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
	(* Каждая точка ожидания зовёт её до смены состояния задачи, поэтому трап
	   отсюда (127, 126) не оставляет задачу в очередях. 126 — канарейка у дна
	   стека испорчена (StackBroken). *)
		VAR c: S.ADDRESS;
	BEGIN
		IF current = NIL THEN Msg(what); Msg("wait outside a task") END;
		ASSERT(current # NIL, 20);
		IF current.dir.reserve # 0 THEN
			S.GET(current.lo, c); IF c # canaryCell THEN StackBroken(current, TRUE) END
		END;
		IF current.cancel THEN Msg("cancelled"); HALT(127) END;
		IF InFramework() THEN Msg(what); Msg("wait inside an action handler or domain broadcast") END;
		ASSERT(~InFramework(), 21)
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
			Leave
		END
	END WaitOwnGroup;

	PROCEDURE EndBody (t: Task);
	(* Пред: тело t вернулось (t.bodyOk) или прервано трапом (~t.bodyOk).
	   Пост: t Finished или Cancelled. Отменённая или трапнувшая задача
	   отменяет свою группу: дети не переживают родителя (структурная
	   конкуренция). Ветка нормального выхода ждёт группу и потому зовётся
	   только на стеке t; ветки трапа и отмены — и со стека планировщика. *)
	BEGIN
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
	END EndBody;

	PROCEDURE TaskBoot;
	(** Точка входа нового стека (адрес возврата из Switch). При oneTry тело
		идёт без своего Kernel.Try: трап возвращает управление в Kernel.TryTasks
		прохода планировщика (Step, AfterLoop), и задачу завершает он. *)
		VAR t: Task;
	BEGIN
		t := current;
		Kernel.SwitchRestore(t.stackRec);
		IF t.cancel THEN
			t.state := Cancelled
		ELSE
			t.bodyOk := FALSE;
			IF oneTry THEN
				TaskBody(0, 0, 0);
				Kernel.DropMonitors	(* как выход из Kernel.Try: несбалансированные входы тела сняты *)
			ELSE
				Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TaskBody)), 0, 0, 0)
			END;
			EndBody(t)
		END;
		Services.SetTrapGuardCleanup;	(* трап внутри Do не оставляет флаг *)
		Finish
	END TaskBoot;

	(* --- поставщики стеков ------------------------------------------------- *)

	PROCEDURE (d: GuardDir) NewStack- (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	BEGIN
		RETURN Kernel.AllocStack(size, lo, hi)
	END NewStack;

	PROCEDURE (d: GuardDir) FreeStack- (lo, hi: S.ADDRESS);
	BEGIN
		Kernel.FreeStack(lo, hi)
	END FreeStack;

	PROCEDURE (d: SlabDir) Drain-;
	(* Пост: невыданные стеки плиты возвращены ядру, плиты нет. *)
	BEGIN
		WHILE d.slabNext # d.slabEnd DO
			Kernel.FreeStack(d.slabEnd - d.slabStep + d.slabGuard, d.slabEnd); d.slabEnd := d.slabEnd - d.slabStep
		END;
		d.slabNext := 0; d.slabEnd := 0
	END Drain;

	PROCEDURE (d: SlabDir) NewStack- (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	(* Из плиты того же размера; иначе новой плитой (slabN стеков, сторожевые
	   страницы ставятся при нарезке); платформа без плит — по одному. *)
	BEGIN
		IF (d.slabNext # d.slabEnd) & (d.slabSize # size) THEN d.Drain END;
		IF d.slabNext = d.slabEnd THEN
			IF ~Kernel.AllocStacks(size, slabN, d.slabNext, d.slabStep, d.slabGuard) THEN
				d.slabNext := 0; d.slabEnd := 0;
				RETURN Kernel.AllocStack(size, lo, hi)
			END;
			d.slabEnd := d.slabNext + d.slabStep * slabN; d.slabSize := size
		END;
		lo := d.slabEnd - d.slabStep + d.slabGuard;
		hi := d.slabEnd; d.slabEnd := d.slabEnd - d.slabStep;
		RETURN TRUE
	END NewStack;

	PROCEDURE (d: SlabDir) FreeStack- (lo, hi: S.ADDRESS);
	BEGIN
		Kernel.FreeStack(lo, hi)
	END FreeStack;

	PROCEDURE DenseSize (d: DenseDir; size: INTEGER): INTEGER;
	BEGIN
		IF size < d.reserve + denseMinChecked THEN size := d.reserve + denseMinChecked END;
		RETURN (size + 15) DIV 16 * 16
	END DenseSize;

	PROCEDURE (d: DenseDir) NewStack- (size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	(* Плиту нарезают снизу вверх; стеки другого размера — из новой плиты, остаток
	   прежней не выдаётся (пул держит стеки по размеру). *)
		VAR n: INTEGER; sl: DenseSlab; a, b: S.ADDRESS;
	BEGIN
		size := DenseSize(d, size);
		IF (d.next = d.end) OR (d.step # size) THEN
			n := denseN; IF size * n > denseMax THEN n := denseMax DIV size END;
			IF n < 1 THEN n := 1 END;
			IF ~Kernel.AllocStack(size * n, a, b) THEN RETURN FALSE END;
			NEW(sl); sl.lo := a; sl.hi := b; sl.next := d.slabs; d.slabs := sl;
			d.next := a; d.end := a + size * n; d.step := size
		END;
		lo := d.next; hi := lo + size; d.next := hi;
		RETURN TRUE
	END NewStack;

	PROCEDURE (d: DenseDir) FreeStack- (lo, hi: S.ADDRESS);
	BEGIN
		HALT(100)	(* keep: Tasks не возвращает плотные стеки по одному *)
	END FreeStack;

	PROCEDURE (d: DenseDir) Drain-;
	BEGIN
		WHILE d.slabs # NIL DO Kernel.FreeStack(d.slabs.lo, d.slabs.hi); d.slabs := d.slabs.next END;
		d.next := 0; d.end := 0; d.step := 0
	END Drain;

	PROCEDURE NewGuardDir* (): StackDir;
	(** Поставщик (а): отдельный стек со сторожевой страницей на задачу. *)
		VAR d: GuardDir;
	BEGIN
		NEW(d); RETURN d
	END NewGuardDir;

	PROCEDURE NewSlabDir* (): StackDir;
	(** Поставщик (б): плиты по slabN стеков со сторожевой страницей под каждым;
		платформа без плит — по одному. *)
		VAR d: SlabDir;
	BEGIN
		NEW(d); RETURN d
	END NewSlabDir;

	PROCEDURE NewDenseDir* (reserve: INTEGER): StackDir;
	(** Поставщик (в): плиты по denseN стеков вплотную, одна сторожевая страница
		под плитой. Пред: reserve >= word (трап 20). Kernel.stkLo задачи = lo +
		reserve: запас под код без проверки в прологе (модули без ключа %, C);
		у дна — канарейка. Стек меньше reserve + denseMinChecked увеличивается
		до этого размера. *)
		VAR d: DenseDir;
	BEGIN
		ASSERT(reserve >= word, 20);
		NEW(d); d.reserve := reserve; d.checked := TRUE; d.keep := TRUE; RETURN d
	END NewDenseDir;

	PROCEDURE NewSegDir* (reserve: INTEGER): StackDir;
	(** Поставщик пробы: стартовые стеки — плотные плиты (как NewDenseDir),
		при нехватке места задача растёт кусками Kernel.GrowStack. Пред:
		reserve >= word (трап 20). *)
		VAR d: SegDir;
	BEGIN
		ASSERT(reserve >= word, 20);
		NEW(d); d.reserve := reserve; d.checked := TRUE; d.keep := TRUE; d.segmented := TRUE;
		RETURN d
	END NewSegDir;

	PROCEDURE InitStackDir* (d: StackDir; reserve: INTEGER; checked, keep: BOOLEAN);
	(** Для поставщика вне модуля, до первой выдачи. Пред: d # NIL (20), стеков
		ещё не выдавал (21), reserve = 0 или reserve >= word (22). *)
	BEGIN
		ASSERT(d # NIL, 20); ASSERT(~d.listed & (d.live = 0) & (d.pool = 0), 21);
		ASSERT((reserve = 0) OR (reserve >= word), 22);
		d.reserve := reserve; d.checked := checked; d.keep := keep
	END InitStackDir;

	PROCEDURE SetStackDir* (d: StackDir);
	(** Пред: d # NIL (трап 20). Пост: новые задачи берут стеки у d. Живые
		задачи вернут свои стеки тому поставщику, который их выдал. *)
	BEGIN
		ASSERT(d # NIL, 20);
		dir := d
	END SetStackDir;

	PROCEDURE StackDirOf* (): StackDir;
	BEGIN
		RETURN dir
	END StackDirOf;

	PROCEDURE StackDirOfTask* (t: Task): StackDir;
	(** Поставщик стека t; NIL — у t нет стека. *)
	BEGIN
		ASSERT(t # NIL, 20);
		IF t.lo = 0 THEN RETURN NIL ELSE RETURN t.dir END
	END StackDirOfTask;

	PROCEDURE UseGuardStacks*;
	(** Команда: новые задачи — со стеком со сторожем (поставщик (а)). *)
	BEGIN
		SetStackDir(NewGuardDir())
	END UseGuardStacks;

	PROCEDURE UseSlabStacks*;
	(** Команда: поставщик (б), плиты со сторожем на стек (по умолчанию). *)
	BEGIN
		SetStackDir(NewSlabDir())
	END UseSlabStacks;

	PROCEDURE UseDenseStacks*;
	(** Команда: поставщик (в), плотные плиты с запасом denseReserve. *)
	BEGIN
		SetStackDir(NewDenseDir(denseReserve))
	END UseDenseStacks;

	PROCEDURE SetSegmentParams* (reserve, base, growth: INTEGER);
	(** Проба сегментированного стека: новые проверенные задачи начинают со
		стека base байт (поставщик NewSegDir с запасом reserve) и растут
		кусками growth. Пред: reserve >= word (20), base > 0 (21), growth > 0 (22). *)
	BEGIN
		ASSERT(reserve >= word, 20); ASSERT(base > 0, 21); ASSERT(growth > 0, 22);
		Kernel.SetSegmentedStacks(TRUE, growth, reserve);
		Kernel.SetSegmentKeep(TRUE);	(* по умолчанию кусок остаётся запасным *)
		SetStackDir(NewSegDir(reserve));
		stackDefault := base
	END SetSegmentParams;

	PROCEDURE UseSegmentedStacks*;
	(** Команда: проба сегментированного стека с параметрами по умолчанию
		(стартовый кусок segBase, рост segGrowth, запас segReserve). *)
	BEGIN
		SetSegmentParams(segReserve, segBase, segGrowth)
	END UseSegmentedStacks;

	PROCEDURE SegmentKeep* (keep: BOOLEAN);
	(** Запасной кусок: TRUE — возвращённый кусок остаётся в пуле задач,
		FALSE — освобождается ядру при следующем росте (замер горячей границы). *)
	BEGIN
		Kernel.SetSegmentKeep(keep)
	END SegmentKeep;

	PROCEDURE StopSegmentedStacks*;
	(** Выключить рост кусками; новые задачи снова берут стеки у поставщика
		со сторожем. *)
	BEGIN
		Kernel.SetSegmentedStacks(FALSE, 0, 0);
		SetStackDir(guardDir)
	END StopSegmentedStacks;

	PROCEDURE TakeStack (d: StackDir; size: INTEGER; OUT lo, hi: S.ADDRESS): BOOLEAN;
	(* Пред: size > 0. Пост: TRUE — [lo, hi) пригоден под стек, запрошенный у d
	   с размером size; FALSE — памяти нет. Стек берётся из пула d, если на его
	   вершине стек того же размера; иначе у d (виртуальный вызов — только здесь). *)
		VAR sz: S.ADDRESS;
	BEGIN
		IF d.pool # 0 THEN
			S.GET(d.pool - 3 * word, sz);
			IF sz = size THEN
				hi := d.pool; S.GET(hi - 2 * word, lo); S.GET(hi - word, d.pool);
				DEC(d.poolCount);
				RETURN TRUE
			END
		END;
		RETURN d.NewStack(size, lo, hi)
	END TakeStack;

	PROCEDURE GiveStack (d: StackDir; size: INTEGER; lo, hi: S.ADDRESS);
	(* Пред: [lo, hi) получен от TakeStack(d, size), на нём никто не исполняется,
	   запись реестра ядра снята. Пост: стек в пуле d. *)
		VAR sz: S.ADDRESS;
	BEGIN
		IF lo = 0 THEN RETURN END;
		sz := size;
		S.PUT(hi - word, d.pool); S.PUT(hi - 2 * word, lo); S.PUT(hi - 3 * word, sz);
		d.pool := hi; INC(d.poolCount)
	END GiveStack;

	PROCEDURE DrainDir (d: StackDir);
	(* Пред: d.live = 0. Пост: пул d пуст, память d возвращена ядру. *)
		VAR lo, hi: S.ADDRESS;
	BEGIN
		WHILE d.pool # 0 DO
			hi := d.pool; S.GET(hi - 2 * word, lo); S.GET(hi - word, d.pool);
			IF ~d.keep THEN d.FreeStack(lo, hi) END
		END;
		d.poolCount := 0;
		d.Drain
	END DrainDir;

	PROCEDURE TrimPool;
	(* Пул принимает любой освобождённый стек: возврат памяти ядру стоит
	   системного вызова, и при массовом завершении задач он дороже самих
	   задач. Лишнее сверх poolMax возвращается понемногу, не больше trimStep
	   стеков за шаг планировщика. Поставщик, который уже не текущий и чьих
	   стеков у задач нет, опустошается целиком и выходит из списка. *)
		VAR lo, hi: S.ADDRESS; n: INTEGER; d, prev: StackDir;
	BEGIN
		prev := NIL; d := dirs;
		WHILE d # NIL DO
			IF (d # dir) & (d # guardDir) & (d.live = 0) THEN
				DrainDir(d); d.listed := FALSE;
				IF prev = NIL THEN dirs := d.nextDir ELSE prev.nextDir := d.nextDir END
			ELSE
				n := 0;
				WHILE ~d.keep & (d.poolCount > poolMax) & (n < trimStep) DO
					hi := d.pool; S.GET(hi - 2 * word, lo); S.GET(hi - word, d.pool);
					DEC(d.poolCount); INC(n);
					d.FreeStack(lo, hi)
				END;
				prev := d
			END;
			d := d.nextDir
		END
	END TrimPool;

	PROCEDURE DrainPool;
	(* Опустошить поставщиков без стеков у задач. *)
		VAR d: StackDir;
	BEGIN
		d := dirs;
		WHILE d # NIL DO
			IF d.live = 0 THEN DrainDir(d) END;
			d := d.nextDir
		END
	END DrainPool;

	PROCEDURE PoolCount* (): INTEGER;
	(** Свободных стеков в пулах всех поставщиков (диагностика). *)
		VAR d: StackDir; n: INTEGER;
	BEGIN
		n := 0; d := dirs; WHILE d # NIL DO INC(n, d.poolCount); d := d.nextDir END;
		RETURN n
	END PoolCount;

	PROCEDURE StackSizeOf (t: Task): INTEGER;
	BEGIN
		IF t.stackSize = 0 THEN RETURN stackDefault ELSE RETURN t.stackSize END
	END StackSizeOf;

	PROCEDURE InitStack (t: Task): BOOLEAN;
		VAR lo, hi, sp, a, zero: S.ADDRESS; size: INTEGER; d: StackDir;
	BEGIN
		size := StackSizeOf(t);
		d := dir;
		IF d.checked & ~RunChecked(t) THEN d := guardDir END;	(* код задачи без проверки — стек со сторожем *)
		IF ~TakeStack(d, size, lo, hi) THEN RETURN FALSE END;
		t.stackRec := Kernel.RegisterStack(lo, hi);
		IF t.stackRec = NIL THEN GiveStack(d, size, lo, hi); RETURN FALSE END;
		IF ~d.listed THEN d.listed := TRUE; d.nextDir := dirs; dirs := d END;
		INC(d.live); t.dir := d;
		t.lo := lo; t.hi := hi;
		t.stackRec.stkLo := lo + d.reserve;	(* граница для проверки в прологе; переключатель переносит *)
		t.stackRec.seg := d.segmented;		(* растёт кусками только проверенный стартовый стек *)
		IF d.reserve # 0 THEN S.PUT(lo, canaryCell) END;	(* канарейка у дна плотного стека *)
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
		VAR c: S.ADDRESS;
	BEGIN
		IF t.prev = NIL THEN all := t.next ELSE t.prev.next := t.next END;
		IF t.next # NIL THEN t.next.prev := t.prev END;
		t.next := NIL; t.prev := NIL;
		IF t.stackRec # NIL THEN
			IF t.stackRec.seg THEN Kernel.ReleaseSegments(t.stackRec) END;
			Kernel.UnregisterStack(t.stackRec); t.stackRec := NIL
		END;
		IF t.lo # 0 THEN
			IF t.dir.reserve # 0 THEN
				S.GET(t.lo, c); IF c # canaryCell THEN StackBroken(t, FALSE) END
			END;
			GiveStack(t.dir, StackSizeOf(t), t.lo, t.hi); DEC(t.dir.live)
		END;
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
	BEGIN
		CheckWait("Sleep");
		IF ms <= 0 THEN current.state := Ready; Enqueue(current); Leave; RETURN END;
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
	(** Усыпить себя до Unpark. Пробуждение до Park не теряется. *)
	BEGIN
		CheckWait("Park");
		IF current.wake THEN current.wake := FALSE; RETURN END;
		current.state := Parked;
		Leave;
		current.wake := FALSE;	(* пробуждение потреблено этим Park *)
		CheckWait("Park")
	END Park;

	PROCEDURE ParkMonitor;
	(** Ожидание монитора (крючок Kernel.Scheduler.park): до пробуждения
		передачей монитора, отмены или постороннего Unpark. Флаг wake не
		читается и не сбрасывается: пользовательское пробуждение остаётся
		следующему Park. Monitors ждёт в цикле «пока не владелец». *)
	BEGIN
		CheckWait("monitor wait");
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
	(** Разбудить главный цикл: действие планировщика выполнится в ближайшем
		шаге Services. Если только спят — на ближайший срок. *)
		VAR d: LONGINT;
	BEGIN
		IF ~scheduled THEN
			IF HasReady() THEN
				scheduled := TRUE; Services.DoLater(sched, Services.now)
			ELSE
				d := NextDeadline();
				IF d >= 0 THEN scheduled := TRUE; Services.DoLater(sched, d) END
			END
		END
	END EnsureScheduled;

	PROCEDURE CheckDeadlock;
	(** Все живые задачи ждут, будить их нечем и сроков нет: печатаем
		журнал ядра «кто чего ждёт» один раз до следующего пробуждения. *)
	BEGIN
		IF (alive > 0) & (readyHead = NIL) & (NextDeadline() < 0) THEN
			IF ~deadlocked THEN
				deadlocked := TRUE;
				Msg("deadlock: no ready tasks and no wake-up deadlines");
				Monitors.DumpMonitors
			END
		ELSE
			deadlocked := FALSE
		END
	END CheckDeadlock;

	PROCEDURE RunLoop (a, b, c: S.ADDRESS);
	(* Тело прохода Step; при oneTry — под Kernel.TryTasks. Пост: loopDone —
	   проход закончен без трапа задачи. *)
		VAR t: Task; now: LONGINT;
	BEGIN
		now := Kernel.Time();
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
			(* Первый запуск всегда отсюда (Leave его не делает): стек получает
			   контекст Try прохода. *)
			IF oneTry & (t.sp = t.hi - stackHead) THEN Kernel.InheritTry(t.stackRec) END;
			SwitchTo(schedSP, t.sp, stepStack);
			t := current; SetCurrent(NIL); direct := 0;	(* вернулась, возможно, другая задача *)
			IF IsDone(t) THEN Reap(t) END;
			now := Kernel.Time();
			IF now - stepT0 >= slice THEN EXIT END
		END;
		loopDone := TRUE
	END RunLoop;

	PROCEDURE AfterLoop (a, b, c: S.ADDRESS);
	(* Зовётся Kernel.TryTasks на стеке планировщика. ~loopDone — трап задачи
	   current, не пойманный её Try: её мониторы сняты, задача завершена как
	   трапнувшая (или отменённая), стек убран. Трап самого планировщика
	   (current = NIL) заканчивает проход. *)
		VAR t: Task;
	BEGIN
		IF ~loopDone THEN
			t := current;
			IF t = NIL THEN loopDone := TRUE
			ELSE
				Kernel.DropMonitors;
				t.bodyOk := FALSE;
				IF t.state # Cancelled THEN EndBody(t) END;
				Services.SetTrapGuardCleanup;
				SetCurrent(NIL); direct := 0;
				Reap(t)
			END
		END
	END AfterLoop;

	PROCEDURE Step*;
	(** Исполнить готовые задачи, не дольше slice мс. Вызывается со дна
		главного цикла (действие планировщика) или тестом. Один Kernel.TryTasks
		на проход: трап задачи возвращает сюда, задача завершается, проход
		продолжается. Платформа без TryShared — Try на задачу (TaskBoot). *)
		VAR here: INTEGER;	(* адрес на стеке планировщика *)
	BEGIN
		IF current # NIL THEN Msg("Step called from a task") END;
		ASSERT(current = NIL, 20);
		IF inStep THEN Msg("Step is not reentrant") END;
		ASSERT(~inStep, 21);
		Services.SetTrapGuardCleanup;	(* страховка от трапа в прошлом действии *)
		inStep := TRUE;
		(* Запись своего стека ищем один раз на шаг; задачи свои записи знают.
		   Переключение обходится без поиска в реестре стеков. *)
		stepStack := Kernel.StackOf(S.ADR(here));	(* не адрес глобала: запись стека ищется по адресу на нём *)
		stepT0 := Kernel.Time();
		REPEAT
			loopDone := FALSE;
			IF oneTry & ~Kernel.TryTasks(S.VAL(Kernel.TryHandler, S.ADR(RunLoop)), S.VAL(Kernel.TryHandler, S.ADR(AfterLoop)), 0, 0, 0) THEN oneTry := FALSE END;
			IF ~oneTry & ~loopDone THEN RunLoop(0, 0, 0) END
		UNTIL loopDone;
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

	PROCEDURE SetDefaultStackSize* (size: INTEGER);
	(** Пред: size > 0 (трап 20). Пост: задачи без SetStackSize получают стек size байт.
		Задаётся программно или командой при запуске (Tasks.SetDefaultStackSize(16384)).
		Без ключа компилятора % в коде задачи переполнение малого стека ловит только
		сторожевая страница, и трап роняет процесс. *)
	BEGIN
		ASSERT(size > 0, 20);
		stackDefault := size
	END SetDefaultStackSize;

	PROCEDURE DefaultStackSize* (): INTEGER;
	BEGIN
		RETURN stackDefault
	END DefaultStackSize;

	PROCEDURE Count* (): INTEGER;
	BEGIN
		RETURN alive
	END Count;

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
		флаг wake — иначе следующий Tasks.Park вернулся бы сразу и Wait
		стал бы холостым циклом. *)
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
		IF (current # NIL) & ~InFramework() & (Monitors.LockDepth() = 0) THEN
			Yield; RETURN TRUE
		ELSE
			RETURN FALSE
		END
	END poll;

BEGIN
	NEW(sched);
	NEW(schedHook);
	slice := defaultSlice; stackDefault := defaultStack; oneTry := TRUE;
	canaryCell := canaryVal; FindRunSlot;
	guardDir := NewSlabDir(); dir := guardDir;
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
		IF all.stackRec # NIL THEN
			IF all.stackRec.seg THEN Kernel.ReleaseSegments(all.stackRec) END;
			Kernel.UnregisterStack(all.stackRec); all.stackRec := NIL
		END;
		IF all.lo # 0 THEN GiveStack(all.dir, StackSizeOf(all), all.lo, all.hi); DEC(all.dir.live) END;
		all.lo := 0; all.hi := 0;
		all := all.next
	END;
	DrainPool;
	readyHead := NIL; readyTail := NIL; heap := NIL; heapN := 0; nSleeping := 0; SetCurrent(NIL);
	scheduled := FALSE; inStep := FALSE
END Tasks.
