MODULE AsyncWorkLin;
(** Рабочие нити ОС для блокирующих вызовов: Linux x64.

	Делегат фасада AsyncWork. Пул нитей pthread: тело — [ccall] над
	[untagged] памятью, без NEW, без Kernel.Try и без журнала. Задания
	лежат в статической очереди под pthread-мьютексом; результат — тоже
	статические слоты. О готовности рабочая нить пишет байт в pipe,
	главный цикл видит его через Poller и будит задачу.

	Задача ждёт результата через Execute: Submit, Park, пробуждение из
	Scan. Главный цикл при этом работает.

	Install зовётся лениво из AsyncWork.Ensure при первом Execute: фасад
	находит делегат сам через Dialog.Call, базовый init пул не заводит. *)

	IMPORT S := SYSTEM, Tasks, Poller, LinThreads, LinNative, LinLibc, LinNet,
		AsyncWork;

	CONST
		maxWorkers = 4;
		maxJobs = 64;
		queueLen = 256;
		slotFree = 0; slotBusy = 1; slotDone = 2;
		(* O_WRONLY|O_CREAT|O_TRUNC *)
		writeFlags = {0, 6, 9};
		fileMode = {2,5,7,8};	(* 0644 *)

	TYPE
		Slot = RECORD [untagged]
			procAdr, arg: S.ADDRESS;
			status, state: INTEGER;
			done: BOOLEAN;
			taskAdr: S.ADDRESS
		END;

		PipeWaiter = POINTER TO RECORD (Poller.Waiter) END;

		LinImpl = POINTER TO RECORD (AsyncWork.Impl) END;

		(** Примитивы pthread в куче: глобальные записи модуля не получают
			гарантированного выравнивания, а futex требует его. *)
		Sync = POINTER TO RECORD
			mu: LinThreads.Mutex;
			cv: LinThreads.Cond
		END;

	VAR
		impl: LinImpl;
		started: BOOLEAN;
		sync: Sync;
		slots: ARRAY maxJobs OF Slot;
		q: ARRAY queueLen OF INTEGER;
		qh, qt, qn: INTEGER;
		stopping: BOOLEAN;
		threads: ARRAY maxWorkers OF LinThreads.Thread;
		nthreads: INTEGER;
		pipeRead, pipeWrite: INTEGER;
		waiter: PipeWaiter;
		drainBuf: ARRAY 64 OF SHORTCHAR;
		dummy, dummy2: INTEGER;

	(* --- очередь и сигнал ------------------------------------------------- *)

	PROCEDURE SignalPipe;
		VAR one: SHORTCHAR; res: INTEGER;
	BEGIN
		one := 0X;
		res := S.VAL(INTEGER, LinLibc.write(pipeWrite, S.ADR(one), 1))
	END SignalPipe;

	PROCEDURE DrainPipe;
		VAR n: INTEGER;
	BEGIN
		LOOP
			n := S.VAL(INTEGER, LinLibc.read(pipeRead, S.ADR(drainBuf[0]), LEN(drainBuf)));
			IF n <= 0 THEN EXIT END
		END
	END DrainPipe;

	(* --- рабочая нить ----------------------------------------------------- *)

	PROCEDURE [ccall] WorkerMain (arg: S.ADDRESS): S.ADDRESS;
		VAR id, res: INTEGER; p: AsyncWork.Proc; set: LinThreads.SigSet;
	BEGIN
		(* сигналы остаются главной нити *)
		res := LinThreads.sigemptyset(S.ADR(set));
		res := LinThreads.sigaddset(S.ADR(set), LinThreads.SIGINT);
		res := LinThreads.sigaddset(S.ADR(set), LinThreads.SIGTERM);
		res := LinThreads.pthread_sigmask(LinThreads.SIG_BLOCK, S.ADR(set), 0);
		LOOP
			res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
			WHILE (qn = 0) & ~stopping DO
				res := LinThreads.pthread_cond_wait(S.ADR(sync.cv), S.ADR(sync.mu))
			END;
			IF (qn = 0) & stopping THEN
				res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
				RETURN 0
			END;
			id := q[qh]; qh := (qh + 1) MOD queueLen; DEC(qn);
			res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
			p := S.VAL(AsyncWork.Proc, slots[id].procAdr);
			p(slots[id].arg);
			res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
			slots[id].done := TRUE;
			res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
			SignalPipe
		END
	END WorkerMain;

	(* --- результат -------------------------------------------------------- *)

	PROCEDURE (i: LinImpl) Scan;
	(** Разобрать завершённые задания и разбудить ждущие задачи. Главный стек. *)
		VAR k, ind, res: INTEGER; wake: ARRAY maxJobs OF S.ADDRESS; nwake: INTEGER;
	BEGIN
		nwake := 0;
		res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
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
		res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
		FOR k := 0 TO nwake - 1 DO
			Tasks.Unpark(S.VAL(Tasks.Task, wake[k]))
		END
	END Scan;

	PROCEDURE (w: PipeWaiter) Ready (events: SET);
	BEGIN
		DrainPipe;
		impl.Scan;
		Poller.Add(pipeRead, Poller.In, w)	(* регистрация одноразовая *)
	END Ready;

	(* --- задания ---------------------------------------------------------- *)

	PROCEDURE (i: LinImpl) Submit (procAdr, arg: S.ADDRESS): INTEGER;
	(** Поставить задание в очередь пула; -1 — нет свободного слота. *)
		VAR ind, id, res: INTEGER;
	BEGIN
		id := -1;
		res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
		FOR ind := 0 TO maxJobs - 1 DO
			IF (id < 0) & (slots[ind].state = slotFree) THEN id := ind END
		END;
		IF id >= 0 THEN
			IF qn >= queueLen THEN id := -1
			ELSE
				slots[id].state := slotBusy; slots[id].done := FALSE;
				slots[id].procAdr := procAdr; slots[id].arg := arg;
				slots[id].status := 0; slots[id].taskAdr := 0;
				q[qt] := id; qt := (qt + 1) MOD queueLen; INC(qn);
				res := LinThreads.pthread_cond_signal(S.ADR(sync.cv))
			END
		END;
		res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
		RETURN id
	END Submit;

	PROCEDURE (i: LinImpl) Done (id: INTEGER): BOOLEAN;
		VAR r: BOOLEAN; res: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
		r := slots[id].done;
		res := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
		RETURN r
	END Done;

	PROCEDURE (i: LinImpl) Result (id: INTEGER): INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		RETURN slots[id].status
	END Result;

	PROCEDURE (i: LinImpl) Release (id: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
		slots[id].state := slotFree; slots[id].done := FALSE;
		slots[id].procAdr := 0; slots[id].arg := 0; slots[id].taskAdr := 0;
		IF LinThreads.pthread_mutex_unlock(S.ADR(sync.mu)) # 0 THEN END
	END Release;

	PROCEDURE SetTask (id: INTEGER; t: Tasks.Task);
		VAR res: INTEGER;
	BEGIN
		res := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
		slots[id].taskAdr := S.VAL(S.ADDRESS, t);
		IF LinThreads.pthread_mutex_unlock(S.ADR(sync.mu)) # 0 THEN END
	END SetTask;

	PROCEDURE (i: LinImpl) Execute (procAdr, arg: S.ADDRESS): INTEGER;
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

	PROCEDURE (i: LinImpl) Workers (): INTEGER;
	BEGIN
		RETURN nthreads
	END Workers;

	(* --- прикладные задания (в рабочих нитях) ----------------------------- *)

	PROCEDURE (i: LinImpl) ReadFileJob (arg: S.ADDRESS);
		VAR r: AsyncWork.FileReqPtr; fd, n: INTEGER;
	BEGIN
		r := S.VAL(AsyncWork.FileReqPtr, arg);
		r.len := 0; r.status := -1;
		fd := LinLibc.open(S.VAL(LinLibc.PtrSTR, S.ADR(r.path[0])), {}, {});
		IF fd >= 0 THEN
			n := S.VAL(INTEGER, LinLibc.read(fd, S.ADR(r.buf[0]), AsyncWork.maxBuf));
			IF n > 0 THEN r.len := n; r.status := 0 ELSE r.status := -30 END;
			n := LinLibc.close(fd)
		END
	END ReadFileJob;

	PROCEDURE (i: LinImpl) WriteFileJob (arg: S.ADDRESS);
		VAR r: AsyncWork.FileReqPtr; fd, n: INTEGER;
	BEGIN
		r := S.VAL(AsyncWork.FileReqPtr, arg);
		r.status := -1;
		fd := LinLibc.open(S.VAL(LinLibc.PtrSTR, S.ADR(r.path[0])), writeFlags, {2,5,7,8});
		IF fd >= 0 THEN
			n := S.VAL(INTEGER, LinLibc.write(fd, S.ADR(r.buf[0]), r.len));
			IF n = r.len THEN r.status := 0 ELSE r.status := -20 END;
			n := LinLibc.close(fd)
		END
	END WriteFileJob;

	PROCEDURE (i: LinImpl) ResolveJob (arg: S.ADDRESS);
		VAR r: AsyncWork.AddrReqPtr; hints: LinNet.addrinfo;
			res, count: INTEGER; ai, p: LinNet.Ptraddrinfo;
	BEGIN
		r := S.VAL(AsyncWork.AddrReqPtr, arg);
		r.count := 0; r.status := -1;
		hints.ai_flags := {};
		hints.ai_family := LinNet.PF_UNSPEC;
		hints.ai_socktype := LinNet.SOCK_STREAM;
		hints.ai_protocol := LinNet.IPPROTO_TCP;
		hints.ai_addrlen := 0; hints.ai_addr := NIL; hints.ai_canonname := NIL; hints.ai_next := NIL;
		ai := NIL;
		res := LinNet.getaddrinfo(r.host, NIL, hints, ai);
		IF res = 0 THEN
			count := 0; p := ai;
			WHILE p # NIL DO INC(count); p := p.ai_next END;
			r.count := count; r.status := 0
		END;
		IF ai # NIL THEN LinNet.freeaddrinfo(ai) END
	END ResolveJob;

	(* --- запуск пула ------------------------------------------------------ *)

	PROCEDURE Install*;
	(** Запустить пул и поставить делегата в AsyncWork (зовёт LinIntInit). *)
		VAR fds: ARRAY 2 OF INTEGER; ind, res, fl: INTEGER;
	BEGIN
		IF started THEN RETURN END;
		started := TRUE;
		NEW(impl); NEW(sync);
		res := LinThreads.pthread_mutex_init(S.ADR(sync.mu), 0);
		res := LinThreads.pthread_cond_init(S.ADR(sync.cv), 0);
		qh := 0; qt := 0; qn := 0; stopping := FALSE;
		FOR ind := 0 TO maxJobs - 1 DO slots[ind].state := slotFree; slots[ind].done := FALSE END;
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(fds[0])));
		pipeRead := fds[0]; pipeWrite := fds[1];
		fl := LinNative.fcntl(pipeRead, LinNative.F_GETFL, 0);
		res := LinNative.fcntl(pipeRead, LinNative.F_SETFL, fl + LinNative.O_NONBLOCK);	(* O_NONBLOCK *)
		nthreads := 0;
		FOR ind := 0 TO maxWorkers - 1 DO
			threads[ind] := 0;
			IF LinThreads.pthread_create(threads[ind], 0, S.ADR(WorkerMain), 0) = 0 THEN
				INC(nthreads)
			END
		END;
		NEW(waiter);
		IF nthreads > 0 THEN Poller.Add(pipeRead, Poller.In, waiter) END;
		AsyncWork.Install(impl)
	END Install;

BEGIN
CLOSE
	IF nthreads > 0 THEN
		dummy := LinThreads.pthread_mutex_lock(S.ADR(sync.mu));
		stopping := TRUE;
		dummy := LinThreads.pthread_cond_broadcast(S.ADR(sync.cv));
		dummy := LinThreads.pthread_mutex_unlock(S.ADR(sync.mu));
		(* Дождаться нитей до освобождения памяти модуля: иначе они
		   вернутся в уже выгруженный код при завершении процесса. *)
		FOR dummy := 0 TO nthreads - 1 DO
			dummy2 := LinThreads.pthread_join(threads[dummy], 0)
		END;
		nthreads := 0
	END
END AsyncWorkLin.
