MODULE Tasks;
(** Задачи со своим стеком в одной нити ОС (этап 3).

	Задача — расширяемая запись с методом Run. Start кладёт её в очередь
	готовых; планировщик — действие Services.Action: исполняет готовые
	задачи не дольше timeSlice миллисекунд и возвращает управление
	главному циклу. Спящие ждут срока через Kernel.Time.

	Память под стек выделяет ядро: Kernel.AllocStack (mmap + сторожевая
	страница в LinKernel, ленивые страницы). Переключатель — [code]
	последовательность в этом модуле: сохраняет rax, rbx, rbp, rsi, rdi,
	r8-r15 и rsp, восстанавливает регистры другого стека. Внешней .so нет.

	Контекст исполнения (Kernel.Try, trapStack, tryCount) уже лежит в
	записи стека ядра: SwitchSave/SwitchRestore вокруг сырого
	переключения. Трап в задаче ловится своим Kernel.Try в TaskBoot,
	задача завершается, остальные живут.

	Отмена — флаг; каждая точка ожидания проверяет его и трапает
	(ASSERT(FALSE, 21)). Ожидание вне задачи, внутри действия или
	рассылки домена — трап с текстом в Console.

	Ограничения (см. docs/stand.md, «Решения без владельца», §9):
	- одна нить ОС; мониторов нет, философы используют Park/Unpark;
	- вытеснения нет: отмена и Yield работают только в точках ожидания;
	- API с Run без параметров, поэтому проверка ABB «VAR-параметры не
	  ссылаются в чужой стек» сводится к проверке, что сам объект задачи
	  не лежит на стеке. *)

	IMPORT S := SYSTEM, Kernel, Services, Models, Console, Strings;

	CONST
		nameLen = 64;
		defaultStack = 256 * 1024;
		defaultSlice = 5;	(* мс на шаг планировщика *)
		pollPeriod = 5;		(* мс: как часто ключ уступки поднимает флаг Poll *)

		(** состояния задачи **)
		New* = 0; Ready* = 1; Running* = 2; Sleeping* = 3;
		Waiting* = 4; Parked* = 5; Finished* = 6; Cancelled* = 7;

		savedRegs = 13;		(* rax rbx rbp rsi rdi r8-r15 *)
		entryOff = savedRegs * 8;	(* адрес возврата после ret *)
		stackHead = entryOff + 16;	(* запас выравнивания до верха *)

	TYPE
		Task* = POINTER TO ABSTRACT RECORD
			next: Task;		(* список всех задач *)
			rnext: Task;		(* очередь готовых *)
			snext: Task;		(* список спящих *)
			wnext: Task;		(* очередь ждущих эту задачу *)
			waiters: Task;
			sp: S.ADDRESS;		(* сохранённый rsp *)
			lo, hi: S.ADDRESS;	(* границы стека [lo, hi) *)
			stackRec: Kernel.Stack;
			state: INTEGER;
			deadline: LONGINT;	(* до какого Kernel.Time спать *)
			timeSlice: LONGINT;
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

		SchedAction = POINTER TO RECORD (Services.Action) END;

	VAR
		all, readyHead, readyTail, sleepers: Task;
		current: Task;
		schedSP: S.ADDRESS;
		sched: SchedAction;
		scheduled, inStep: BOOLEAN;
		alive: INTEGER;		(* незавершённых задач *)
		slice: LONGINT;
		lastPoll: LONGINT;	(* когда Kernel.RequestPoll поднимался последний раз *)
		switches: LONGINT;	(* переключений стека, диагностика *)
		deadlocked: BOOLEAN;	(* все ждут, сроков нет; журнал уже напечатан *)

	PROCEDURE (t: Task) Run* (), NEW, ABSTRACT;

	PROCEDURE^ EnsureScheduled;
	PROCEDURE^ CancelGroup (g: Group);

	(* --- текст в Console -------------------------------------------------- *)

	PROCEDURE Msg (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr("Tasks: "); Console.WriteStr(s); Console.WriteLn
	END Msg;

	PROCEDURE Fail (IN s: ARRAY OF CHAR);
	BEGIN
		Msg(s); ASSERT(FALSE, 20)
	END Fail;

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

	(* --- [code] переключатель --------------------------------------------- *)

	PROCEDURE [code] Switch (VAR from, to: S.ADDRESS)
	(*
		entry: rax = &from, [rsp] = &to.

		mov rcx,[rsp]        ; &to (rsp ещё указывает на параметр)
		add rsp,8            ; параметр снят
		lea r11,[rip+end]    ; адрес продолжения после ret
		push r11             ; синтетический адрес возврата
		push rax             ; сохранить &from
		push rbx; push rbp; push rsi; push rdi
		push r8; push r9; push r10; push r11
		push r12; push r13; push r14; push r15
		mov rdx,[rsp+96]     ; &from (rax лежит выше всех)
		mov [rdx],rsp        ; from^ := rsp
		mov rsp,[rcx]        ; rsp := to^
		pop r15; pop r14; pop r13; pop r12
		pop r11; pop r10; pop r9; pop r8
		pop rdi; pop rsi; pop rbp; pop rbx; pop rax
		ret
	*)
		048H, 08BH, 00CH, 024H,		(* mov rcx,[rsp] *)
		048H, 083H, 0C4H, 008H,		(* add rsp,8 *)
		04CH, 08DH, 01DH, 038H, 000H, 000H, 000H,	(* lea r11,[rip+56] — адрес после ret *)
		041H, 053H,			(* push r11: синтетический адрес возврата *)
		050H,				(* push rax (&from) *)
		053H, 055H, 056H, 057H,
		041H, 050H, 041H, 051H, 041H, 052H, 041H, 053H,
		041H, 054H, 041H, 055H, 041H, 056H, 041H, 057H,
		048H, 08BH, 054H, 024H, 060H,	(* mov rdx,[rsp+96] *)
		048H, 089H, 022H,		(* mov [rdx],rsp *)
		048H, 08BH, 021H,		(* mov rsp,[rcx] *)
		041H, 05FH, 041H, 05EH, 041H, 05DH, 041H, 05CH,
		041H, 05BH, 041H, 05AH, 041H, 059H, 041H, 058H,
		05FH, 05EH, 05DH, 05BH, 058H,
		0C3H;

	PROCEDURE SwitchTo (VAR from, to: S.ADDRESS);
	(** Сырое переключение с сохранением контекста ядра своего стека. *)
	BEGIN
		INC(switches);
		Kernel.SwitchSave;
		Switch(from, to);
		Kernel.SwitchRestore
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
			t.state := Ready; Enqueue(t); deadlocked := FALSE; EnsureScheduled
		END
	END MakeReady;

	PROCEDURE IsDone (t: Task): BOOLEAN;
	BEGIN
		RETURN (t.state = Finished) OR (t.state = Cancelled)
	END IsDone;

	PROCEDURE WakeSleepers (now: LONGINT);
		VAR t, prev, nx: Task;
	BEGIN
		prev := NIL; t := sleepers;
		WHILE t # NIL DO
			nx := t.snext;
			IF t.state # Sleeping THEN	(* разбужен отменой/Unpark *)
				IF prev = NIL THEN sleepers := nx ELSE prev.snext := nx END
			ELSIF t.deadline <= now THEN
				IF prev = NIL THEN sleepers := nx ELSE prev.snext := nx END;
				t.snext := NIL; t.state := Ready; Enqueue(t)
			ELSE prev := t
			END;
			t := nx
		END
	END WakeSleepers;

	PROCEDURE WakeWaiters (t: Task);
		VAR w: Task;
	BEGIN
		WHILE t.waiters # NIL DO
			w := t.waiters; t.waiters := w.wnext; w.wnext := NIL;
			IF w.state = Waiting THEN w.state := Ready; Enqueue(w) END
		END
	END WakeWaiters;

	(* --- проверки точек ожидания ------------------------------------------ *)

	PROCEDURE InFramework* (): BOOLEAN;
	(** Идёт обработчик действия (кроме нашего планировщика) или
		рассылка по домену. Экспортируется для проверок и отладки. *)
	BEGIN
		RETURN Services.executingAction & (S.VAL(LONGINT, Services.executing) # S.VAL(LONGINT, sched))
			OR Models.InDomainCast()
	END InFramework;

	PROCEDURE CheckWait (IN what: ARRAY OF CHAR);
	BEGIN
		IF current = NIL THEN
			Msg(what); Fail("wait outside a task")
		END;
		IF current.cancel THEN
			Msg("cancelled"); ASSERT(FALSE, 21)
		END;
		IF InFramework() THEN
			Msg(what); Fail("wait inside an action handler or domain broadcast")
		END
	END CheckWait;

	(* --- завершение и запуск ---------------------------------------------- *)

	PROCEDURE Finish;
	(** Вызывается сTaskBoot на стеке задачи: перейти на главный стек. *)
	BEGIN
		SwitchTo(current.sp, schedSP);
		(* сюда не возвращаемся: задача завершена *)
		HALT(22)
	END Finish;

	PROCEDURE TaskBody (a, b, c: S.ADDRESS);
	(** bodyOk — поле задачи, а не глобал: иначе завершившаяся соседняя
		задача выставила бы «успех» трапнувшей (общий счётчик этапа 3). *)
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
			SwitchTo(t.sp, schedSP)
		END
	END WaitOwnGroup;

	PROCEDURE TaskBoot;
	(** Точка входа нового стека (адрес возврата из Switch). *)
		VAR t: Task;
	BEGIN
		Kernel.SwitchRestore;
		t := current;
		IF t.cancel THEN
			t.state := Cancelled
		ELSE
			t.bodyOk := FALSE;
			Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TaskBody)), 0, 0, 0);
			IF t.cancel THEN
				t.state := Cancelled
			ELSIF ~t.bodyOk THEN
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

	PROCEDURE InitStack (t: Task): BOOLEAN;
		VAR lo, hi, sp: S.ADDRESS; size: INTEGER;
	BEGIN
		size := t.stackSize;
		IF size = 0 THEN size := defaultStack END;
		IF ~Kernel.AllocStack(size, lo, hi) THEN RETURN FALSE END;
		t.lo := lo; t.hi := hi;
		t.stackRec := Kernel.RegisterStack(lo, hi);
		IF t.stackRec = NIL THEN Kernel.FreeStack(lo, hi); t.lo := 0; RETURN FALSE END;
		(* 13 нулевых слотов регистров (mmap уже нулевой), адрес входа,
		   8 байт запаса: sp % 16 = 8, после 13 pop и ret rsp % 16 = 8 *)
		sp := hi - stackHead;
		S.PUT(sp + entryOff, S.ADR(TaskBoot));
		t.sp := sp;
		RETURN TRUE
	END InitStack;

	PROCEDURE CheckTask (t: Task);
	BEGIN
		IF t = NIL THEN Fail("NIL task") END;
		IF t.state # New THEN Fail("task already started or finished") END;
		(* ABB-проверка: объект задачи не должен лежать на стеке (например,
		   если его адрес пришёл из VAR-параметра). *)
		IF Kernel.StackOf(S.VAL(S.ADDRESS, S.ADR(t^))) # NIL THEN
			Fail("task object is allocated on a stack")
		END
	END CheckTask;

	PROCEDURE StartIn* (t: Task; g: Group);
	(** Запустить задачу; если g # NIL — зарегистрировать её в группе. *)
	BEGIN
		CheckTask(t);
		IF ~InitStack(t) THEN Fail("no stack memory (platform does not support it)") END;
		IF t.timeSlice <= 0 THEN t.timeSlice := defaultSlice END;
		IF g # NIL THEN t.group := g; INC(g.alive) END;
		t.state := Ready; Enqueue(t); deadlocked := FALSE;
		t.next := all; all := t;
		INC(alive);
		EnsureScheduled
	END StartIn;

	PROCEDURE Start* (t: Task);
	BEGIN
		StartIn(t, NIL)
	END Start;

	PROCEDURE Reap (t: Task);
	(** Освободить стек завершённой задачи (уже на главном стеке). *)
	BEGIN
		IF t.stackRec # NIL THEN Kernel.UnregisterStack(t.stackRec); t.stackRec := NIL END;
		Kernel.FreeStack(t.lo, t.hi);
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
		SwitchTo(current.sp, schedSP)
	END Yield;

	PROCEDURE Sleep* (ms: LONGINT);
	BEGIN
		CheckWait("Sleep");
		IF ms <= 0 THEN Yield; RETURN END;
		current.deadline := Kernel.Time() + ms;
		current.state := Sleeping;
		current.snext := sleepers; sleepers := current;
		SwitchTo(current.sp, schedSP)
	END Sleep;

	PROCEDURE Join* (t: Task);
	(** Дождаться завершения задачи (или её отмены). *)
	BEGIN
		CheckWait("Join");
		IF t = NIL THEN Fail("Join: NIL task") END;
		WHILE ~IsDone(t) DO
			current.wnext := t.waiters; t.waiters := current;
			current.state := Waiting;
			SwitchTo(current.sp, schedSP);
			CheckWait("Join")
		END
	END Join;

	PROCEDURE Park*;
	(** Усыпить себя до Unpark. Пробуждение до Park не теряется. *)
	BEGIN
		CheckWait("Park");
		IF current.wake THEN current.wake := FALSE; RETURN END;
		current.state := Parked;
		SwitchTo(current.sp, schedSP);
		CheckWait("Park")
	END Park;

	PROCEDURE Unpark* (t: Task);
	BEGIN
		IF t = NIL THEN Fail("Unpark: NIL task") END;
		t.wake := TRUE;
		IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
	END Unpark;

	PROCEDURE Cancel* (t: Task);
	(** Отмена: флаг проверяется в точках ожидания; спящую/ждущую будим. *)
	BEGIN
		IF t = NIL THEN Fail("Cancel: NIL task") END;
		IF ~IsDone(t) THEN
			t.cancel := TRUE;
			IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
		END
	END Cancel;

	(* --- группа ----------------------------------------------------------- *)

	PROCEDURE NewGroup* (): Group;
		VAR g: Group;
	BEGIN
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
		VAR t: Task; min: LONGINT;
	BEGIN
		min := -1; t := sleepers;
		WHILE t # NIL DO
			IF (t.state = Sleeping) & ((min < 0) OR (t.deadline < min)) THEN min := t.deadline END;
			t := t.snext
		END;
		RETURN min
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
				Kernel.DumpMonitors
			END
		ELSE
			deadlocked := FALSE
		END
	END CheckDeadlock;

	PROCEDURE Step*;
	(** Исполнить готовые задачи, не дольше slice мс. Вызывается со дна
		главного цикла (действие планировщика) или тестом. *)
		VAR t: Task; t0: LONGINT;
	BEGIN
		IF current # NIL THEN Fail("Step called from a task") END;
		IF inStep THEN Fail("Step is not reentrant") END;
		Services.SetTrapGuardCleanup;	(* страховка от трапа в прошлом действии *)
		inStep := TRUE;
		t0 := Kernel.Time();
		LOOP
			IF Kernel.Time() - lastPoll >= pollPeriod THEN	(* ключ точек уступки *)
				Kernel.RequestPoll; lastPoll := Kernel.Time()
			END;
			WakeSleepers(Kernel.Time());
			t := Dequeue();
			IF t = NIL THEN CheckDeadlock; EXIT END;
			current := t; t.state := Running;
			SwitchTo(schedSP, t.sp);
			current := NIL;
			IF IsDone(t) THEN Reap(t) END;
			IF Kernel.Time() - t0 >= slice THEN EXIT END
		END;
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
	BEGIN
		IF (t # NIL) & (t.state = New) THEN t.stackSize := size END
	END SetStackSize;

	PROCEDURE SetTimeSlice* (t: Task; ms: LONGINT);
	BEGIN
		IF (t # NIL) & (t.state = New) THEN t.timeSlice := ms END
	END SetTimeSlice;

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

	PROCEDURE List*;
	(** Список задач для отладки: имя, состояние, стек, срок. *)
		VAR t: Task; s: ARRAY 16 OF CHAR; st: ARRAY 16 OF CHAR;
	BEGIN
		t := all;
		WHILE t # NIL DO
			StateName(t.state, st);
			Console.WriteStr("Tasks: "); IF t.name # "" THEN Console.WriteStr(t.name) ELSE Console.WriteStr("?") END;
			Console.WriteStr(" "); Console.WriteStr(st);
			Strings.IntToString(S.VAL(LONGINT, t.lo), s); Console.WriteStr(" stack="); Console.WriteStr(s);
			Strings.IntToString(S.VAL(LONGINT, t.hi) - S.VAL(LONGINT, t.lo), s);
			Console.WriteStr("+"); Console.WriteStr(s);
			IF t.state = Sleeping THEN
				Strings.IntToString(t.deadline, s); Console.WriteStr(" deadline="); Console.WriteStr(s)
			END;
			Console.WriteLn;
			t := t.next
		END
	END List;

(* --- связь с ядром: мониторы этапа 4 ---------------------------------- *)

	PROCEDURE CurHook (): S.ADDRESS;
	(** Токен текущей задачи для Kernel.Lock/Wait/Await; 0 — главный стек. *)
	BEGIN
		IF current # NIL THEN RETURN S.VAL(S.ADDRESS, S.ADR(current^)) ELSE RETURN 0 END
	END CurHook;

	PROCEDURE UnparkHook (a: S.ADDRESS);
	(** Пробуждение ядром (передача монитора): НЕ трогать пользовательский
		флаг wake — иначе следующий Tasks.Park вернулся бы сразу и Wait
		стал бы холостым циклом. *)
		VAR t: Task;
	BEGIN
		IF a # 0 THEN
			t := S.VAL(Task, a);
			IF t.state IN {Sleeping, Waiting, Parked} THEN MakeReady(t) END
		END
	END UnparkHook;

	PROCEDURE ReadyHook (): BOOLEAN;
	BEGIN
		RETURN HasReady()
	END ReadyHook;

	PROCEDURE DeadlineHook (): LONGINT;
	BEGIN
		RETURN NextDeadline()
	END DeadlineHook;

	PROCEDURE NameHook (a: S.ADDRESS; OUT s: ARRAY OF CHAR);
	BEGIN
		IF a = 0 THEN s := "?"
		ELSE GetName(S.VAL(Task, a), s)
		END
	END NameHook;

	PROCEDURE YieldHook;
	(** Уступка из Kernel.Poll: только в задаче и вне каркаса. Отмена
		проверяется в Yield (CheckWait): Poll — тоже точка отмены. *)
	BEGIN
		IF (current # NIL) & ~InFramework() THEN Yield END
	END YieldHook;

	PROCEDURE StackHook (a: S.ADDRESS): Kernel.Stack;
	(** Запись стека задачи: ядро добавляет взятие монитора в её цепочку
		при передаче владения спящей задаче. *)
		VAR t: Task;
	BEGIN
		IF a # 0 THEN
			t := S.VAL(Task, a); RETURN t.stackRec
		ELSE
			RETURN NIL
		END
	END StackHook;

BEGIN
	NEW(sched);
	slice := defaultSlice;
	scheduled := FALSE; inStep := FALSE; deadlocked := FALSE;
	lastPoll := Kernel.Time();
	Kernel.SetScheduler(CurHook, Park, UnparkHook, ReadyHook, DeadlineHook, NameHook, StackHook, YieldHook)
END Tasks.
