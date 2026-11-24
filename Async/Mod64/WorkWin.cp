MODULE AsyncWorkWin;
(** Рабочие нити ОС для блокирующих вызовов: Windows x64.

	Делегат фасада AsyncWork. Пул из 4 нитей CreateThread; задания и
	результаты — статические слоты в [untagged]-памяти, очередь под
	CRITICAL_SECTION. Готовность — счётчик done: главный цикл забирает готовые задания
	действием Services (без блокировки окон). Действие
	само переставляется через 5 мс, пока в пуле есть занятые слоты.

	Очередь ждёт manual-reset событие: Submit поднимает его, рабочая нить
	сбрасывает под критической секцией, когда очередь опустела. Так
	несколько заданий и остановка не теряют пробуждение.

	Задания — те же [ccall]-процедуры фасада; тела используют Win32
	(CreateFileA/ReadFile/WriteFile, getaddrinfo из WS2_32), поэтому
	куча и каркас не трогаются.

	Install зовётся лениво из AsyncWork.Ensure при первом Execute или Submit; базовый
	init (WinIntInit/WinInit) пул не заводит. *)

	IMPORT S := SYSTEM, Tasks, Services, AsyncWork, A := WinApi, Ws := AsyncWinSock;

	CONST
		maxWorkers = 4;
		maxJobs = 64;
		queueLen = 256;
		slotFree = 0; slotBusy = 1; slotDone = 2;
		pollMs = 5;	(* период действия Services, пока есть занятые слоты *)

		genericRead = {31}; genericWrite = {30};
		createAlways = 2; openExisting = 3;
		fileAttrNormal = {7};
		invalidHandle = -1;

	TYPE
		Slot = RECORD [untagged]
			procAdr, arg: S.ADDRESS;
			status, state: INTEGER;
			done: BOOLEAN;
			taskAdr: S.ADDRESS
		END;

		WinImpl = POINTER TO RECORD (AsyncWork.Impl) END;

		(** Критическая секция и события в куче: глобальные записи модуля
			не получают гарантированного выравнивания, а CRITICAL_SECTION
			требует 8 байт. *)
		Sync = POINTER TO RECORD
			cs: A.RTL_CRITICAL_SECTION;
			workEvent: A.HANDLE
		END;

		PollAction = POINTER TO RECORD (Services.Action) END;

	VAR
		impl: WinImpl;
		sync: Sync;
		action: PollAction;
		started: BOOLEAN;
		slots: ARRAY maxJobs OF Slot;
		q: ARRAY queueLen OF INTEGER;
		qh, qt, qn: INTEGER;
		stopping: BOOLEAN;
		threads: ARRAY maxWorkers OF A.HANDLE;
		nthreads: INTEGER;
		wsaStarted: BOOLEAN;

	(* --- рабочая нить ----------------------------------------------------- *)

	PROCEDURE [ms64] WorkerMain (arg: S.ADDRESS): INTEGER;
		VAR id, ind, res: INTEGER; stop: BOOLEAN; p: AsyncWork.Proc;
	BEGIN
		LOOP
			res := A.WaitForSingleObject(sync.workEvent, A.INFINITE);
			A.EnterCriticalSection(sync.cs);
			IF qn > 0 THEN
				id := q[qh]; qh := (qh + 1) MOD queueLen; DEC(qn);
				IF qn = 0 THEN ind := A.ResetEvent(sync.workEvent) END;
				A.LeaveCriticalSection(sync.cs);
				p := S.VAL(AsyncWork.Proc, slots[id].procAdr);
				p(slots[id].arg);
				A.EnterCriticalSection(sync.cs);
				slots[id].done := TRUE;
				A.LeaveCriticalSection(sync.cs)
			ELSE
				ind := A.ResetEvent(sync.workEvent);
				stop := stopping;
				A.LeaveCriticalSection(sync.cs);
				IF stop THEN RETURN 0 END
			END
		END
	END WorkerMain;

	(* --- результат -------------------------------------------------------- *)

	PROCEDURE (i: WinImpl) Scan;
	(** Разобрать завершённые задания и разбудить ждущие задачи. Главный стек. *)
		VAR ind, k, res: INTEGER; wake: ARRAY maxJobs OF S.ADDRESS; nwake: INTEGER;
	BEGIN
		nwake := 0;
		A.EnterCriticalSection(sync.cs);
		FOR ind := 0 TO maxJobs - 1 DO
			IF (slots[ind].state = slotBusy) & slots[ind].done THEN
				slots[ind].state := slotDone;
				IF slots[ind].taskAdr # 0 THEN
					wake[nwake] := slots[ind].taskAdr;
					slots[ind].taskAdr := 0;
					INC(nwake)
				END
			END
		END;
		A.LeaveCriticalSection(sync.cs);
		FOR k := 0 TO nwake - 1 DO
			Tasks.Unpark(S.VAL(Tasks.Task, wake[k]))
		END
	END Scan;

	PROCEDURE Pending (): BOOLEAN;
	(** Есть ли занятые слоты: тогда действие должно прийти ещё раз. *)
		VAR ind: INTEGER; r: BOOLEAN;
	BEGIN
		r := FALSE;
		A.EnterCriticalSection(sync.cs);
		FOR ind := 0 TO maxJobs - 1 DO
			IF slots[ind].state = slotBusy THEN r := TRUE END
		END;
		A.LeaveCriticalSection(sync.cs);
		RETURN r
	END Pending;

	PROCEDURE (a: PollAction) Do;
	(** Главный цикл Windows: забрать готовность и переставить себя. *)
	BEGIN
		impl.Scan;
		IF Pending() THEN Services.DoLater(a, Services.Ticks() + pollMs) END
	END Do;

	PROCEDURE Kick;
	(** Разбудить главный цикл: он разберёт результат на ближайшем шаге. *)
	BEGIN
		IF action # NIL THEN Services.DoLater(action, Services.now) END
	END Kick;

	(* --- задания ---------------------------------------------------------- *)

	PROCEDURE (i: WinImpl) Submit (procAdr, arg: S.ADDRESS): INTEGER;
	(** Поставить задание в очередь пула; -1 — нет свободного слота. *)
		VAR ind, id: INTEGER;
	BEGIN
		id := -1;
		A.EnterCriticalSection(sync.cs);
		FOR ind := 0 TO maxJobs - 1 DO
			IF (id < 0) & (slots[ind].state = slotFree) THEN id := ind END
		END;
		IF id >= 0 THEN
			IF qn >= queueLen THEN id := -1
			ELSE
				slots[id].state := slotBusy; slots[id].done := FALSE;
				slots[id].procAdr := procAdr; slots[id].arg := arg;
				slots[id].status := 0; slots[id].taskAdr := 0;
				q[qt] := id; qt := (qt + 1) MOD queueLen; INC(qn)
			END
		END;
		A.LeaveCriticalSection(sync.cs);
		IF id >= 0 THEN
			ind := A.SetEvent(sync.workEvent);
			Kick
		END;
		RETURN id
	END Submit;

	PROCEDURE (i: WinImpl) Done (id: INTEGER): BOOLEAN;
		VAR r: BOOLEAN;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		A.EnterCriticalSection(sync.cs);
		r := slots[id].done;
		A.LeaveCriticalSection(sync.cs);
		RETURN r
	END Done;

	PROCEDURE (i: WinImpl) Result (id: INTEGER): INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		RETURN slots[id].status
	END Result;

	PROCEDURE (i: WinImpl) Release (id: INTEGER);
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		A.EnterCriticalSection(sync.cs);
		slots[id].state := slotFree; slots[id].done := FALSE;
		slots[id].procAdr := 0; slots[id].arg := 0; slots[id].taskAdr := 0;
		A.LeaveCriticalSection(sync.cs)
	END Release;

	PROCEDURE SetTask (id: INTEGER; t: Tasks.Task);
	BEGIN
		A.EnterCriticalSection(sync.cs);
		slots[id].taskAdr := S.VAL(S.ADDRESS, t);
		A.LeaveCriticalSection(sync.cs)
	END SetTask;

	PROCEDURE (i: WinImpl) Execute (procAdr, arg: S.ADDRESS): INTEGER;
	(** Исполнить задание в рабочей нити и дождаться; только из задачи. *)
		VAR id, status: INTEGER;
	BEGIN
		ASSERT(Tasks.InTask(), 21);
		id := i.Submit(procAdr, arg);
		IF id < 0 THEN RETURN -1 END;
		SetTask(id, Tasks.Current());
		WHILE ~i.Done(id) DO Tasks.Park END;
		status := i.Result(id);
		i.Release(id);
		RETURN status
	END Execute;

	PROCEDURE (i: WinImpl) Workers (): INTEGER;
	BEGIN
		RETURN nthreads
	END Workers;

	(* --- прикладные задания (в рабочих нитях) ----------------------------- *)

	PROCEDURE (i: WinImpl) ReadFileJob (arg: S.ADDRESS);
		VAR r: AsyncWork.FileReqPtr; h: A.HANDLE; n, ok: INTEGER;
	BEGIN
		r := S.VAL(AsyncWork.FileReqPtr, arg);
		r.len := 0; r.status := -1;
		h := A.CreateFileA(S.VAL(A.PtrSTR, S.ADR(r.path[0])), genericRead, {},
			NIL, openExisting, fileAttrNormal, 0);
		IF h # invalidHandle THEN
			ok := A.ReadFile(h, S.ADR(r.buf[0]), AsyncWork.maxBuf, n, NIL);
			IF (ok # 0) & (n > 0) THEN r.len := n; r.status := 0 ELSE r.status := -30 END;
			ok := A.CloseHandle(h)
		END
	END ReadFileJob;

	PROCEDURE (i: WinImpl) WriteFileJob (arg: S.ADDRESS);
		VAR r: AsyncWork.FileReqPtr; h: A.HANDLE; n, ok: INTEGER;
	BEGIN
		r := S.VAL(AsyncWork.FileReqPtr, arg);
		r.status := -1;
		h := A.CreateFileA(S.VAL(A.PtrSTR, S.ADR(r.path[0])), genericWrite, {},
			NIL, createAlways, fileAttrNormal, 0);
		IF h # invalidHandle THEN
			ok := A.WriteFile(h, S.ADR(r.buf[0]), r.len, n, NIL);
			IF (ok # 0) & (n = r.len) THEN r.status := 0 ELSE r.status := -20 END;
			ok := A.CloseHandle(h)
		END
	END WriteFileJob;

	PROCEDURE (i: WinImpl) ResolveJob (arg: S.ADDRESS);
		VAR r: AsyncWork.AddrReqPtr; hints: Ws.AddrInfo;
			res, count: INTEGER; ai, p: Ws.AddrInfoPtr;
	BEGIN
		r := S.VAL(AsyncWork.AddrReqPtr, arg);
		r.count := 0; r.status := -1;
		IF ~wsaStarted THEN RETURN END;
		hints.flags := 0; hints.family := Ws.pfUnspec;
		hints.socktype := Ws.sockStream; hints.protocol := Ws.ipProtoTcp;
		hints.addrlen := 0; hints.canonname := 0; hints.addr := 0; hints.next := 0;
		ai := NIL;
		res := Ws.getaddrinfo(S.VAL(S.ADDRESS, S.ADR(r.host[0])), 0, hints, ai);
		IF res = 0 THEN
			count := 0; p := ai;
			WHILE p # NIL DO INC(count); p := S.VAL(Ws.AddrInfoPtr, p.next) END;
			r.count := count; r.status := 0
		END;
		IF ai # NIL THEN Ws.freeaddrinfo(ai) END
	END ResolveJob;

	(* --- запуск пула ------------------------------------------------------ *)

	PROCEDURE Install*;
	(** Запустить пул и поставить делегата в AsyncWork (зовёт init платформы). *)
		VAR ind, tid: INTEGER; th: A.HANDLE; data: Ws.WSAData;
	BEGIN
		IF started THEN RETURN END;
		started := TRUE;
		NEW(impl); NEW(sync);
		A.InitializeCriticalSection(sync.cs);
		sync.workEvent := A.CreateEventA(NIL, 1, 0, NIL);	(* manual reset *)
		qh := 0; qt := 0; qn := 0; stopping := FALSE;
		FOR ind := 0 TO maxJobs - 1 DO
			slots[ind].state := slotFree; slots[ind].done := FALSE; slots[ind].taskAdr := 0
		END;
		nthreads := 0;
		FOR ind := 0 TO maxWorkers - 1 DO
			th := A.CreateThread(NIL, 0, WorkerMain, 0, {}, tid);
			threads[ind] := th;
			IF th # 0 THEN INC(nthreads) END
		END;
		wsaStarted := Ws.WSAStartup(Ws.version, data) = 0;	(* версия 2.2 *)
		NEW(action);
		AsyncWork.Install(impl)
	END Install;

BEGIN
CLOSE
	IF started THEN
		A.EnterCriticalSection(sync.cs);
		stopping := TRUE;
		A.LeaveCriticalSection(sync.cs);
		IF A.SetEvent(sync.workEvent) # 0 THEN END
	END
END AsyncWorkWin.
