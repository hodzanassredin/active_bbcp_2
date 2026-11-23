MODULE AsyncWork;
(** Рабочие нити ОС для блокирующих вызовов (этап 8).

	Пул нитей pthread: тело — [ccall] над [untagged] памятью, без NEW,
	без Kernel.Try и без журнала. Задания лежат в статической очереди под
	pthread-мьютексом; результат — тоже статические слоты. О готовности
	рабочая нить пишет байт в pipe, главный цикл видит его через Poller
	(этап 7) и будит задачу.

	Задача ждёт результата через Execute: Submit, Park, пробуждение из
	Scan. Главный цикл при этом работает.

	Kernel.EnterWorker/LeaveWorker — отладочный счётчик обращений к куче
	из рабочей нити (для теста дисциплины). *)

	IMPORT S := SYSTEM, Kernel, Tasks, Poller, LinThreads, LinNative, LinLibc, LinNet;

	CONST
		maxWorkers = 4;
		maxJobs = 64;
		queueLen = 256;
		slotFree = 0; slotBusy = 1; slotDone = 2;
		maxPath = 256; maxBuf = 4096; maxHost = 128;
		(* O_WRONLY|O_CREAT|O_TRUNC *)
		writeFlags = {0, 6, 9};
		fileMode = {2,5,7,8};	(* 0644 *)

	TYPE
		Proc = PROCEDURE [ccall] (arg: S.ADDRESS);

		Slot = RECORD [untagged]
			procAdr, arg: S.ADDRESS;
			status, state: INTEGER;
			done: BOOLEAN;
			taskAdr: S.ADDRESS
		END;

		PipeWaiter = POINTER TO RECORD (Poller.Waiter) END;

		FileReqPtr = POINTER [untagged] TO FileReq;
		AddrReqPtr = POINTER [untagged] TO AddrReq;

		FileReq* = RECORD [untagged]
			path*: ARRAY maxPath OF SHORTCHAR;
			buf*: ARRAY maxBuf OF SHORTCHAR;
			len*, status*: INTEGER
		END;

		AddrReq* = RECORD [untagged]
			host*: ARRAY maxHost OF SHORTCHAR;
			count*, status*: INTEGER
		END;

	VAR
		mu: LinThreads.Mutex;
		cv: LinThreads.Cond;
		slots: ARRAY maxJobs OF Slot;
		q: ARRAY queueLen OF INTEGER;
		qh, qt, qn: INTEGER;
		stopping: BOOLEAN;
		threads: ARRAY maxWorkers OF LinThreads.Thread;
		nthreads: INTEGER;
		pipeRead, pipeWrite: INTEGER;
		waiter: PipeWaiter;
		drainBuf: ARRAY 64 OF SHORTCHAR;
		dummy: INTEGER;

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
		VAR id, res: INTEGER; p: Proc; set: LinThreads.SigSet;
	BEGIN
		(* сигналы остаются главной нити *)
		res := LinThreads.sigemptyset(S.ADR(set));
		res := LinThreads.sigaddset(S.ADR(set), LinThreads.SIGINT);
		res := LinThreads.sigaddset(S.ADR(set), LinThreads.SIGTERM);
		res := LinThreads.pthread_sigmask(LinThreads.SIG_BLOCK, S.ADR(set), 0);
		LOOP
			res := LinThreads.pthread_mutex_lock(S.ADR(mu));
			WHILE (qn = 0) & ~stopping DO
				res := LinThreads.pthread_cond_wait(S.ADR(cv), S.ADR(mu))
			END;
			IF (qn = 0) & stopping THEN
				res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
				RETURN 0
			END;
			id := q[qh]; qh := (qh + 1) MOD queueLen; DEC(qn);
			res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
			p := S.VAL(Proc, slots[id].procAdr);
			Kernel.EnterWorker;
			p(slots[id].arg);
			Kernel.LeaveWorker;
			res := LinThreads.pthread_mutex_lock(S.ADR(mu));
			slots[id].done := TRUE;
			res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
			SignalPipe
		END
	END WorkerMain;

	(* --- результат -------------------------------------------------------- *)

	PROCEDURE Scan*;
	(** Разобрать завершённые задания и разбудить ждущие задачи. Главный стек. *)
		VAR i, k, res: INTEGER; wake: ARRAY maxJobs OF S.ADDRESS; nwake: INTEGER;
	BEGIN
		nwake := 0;
		res := LinThreads.pthread_mutex_lock(S.ADR(mu));
		FOR i := 0 TO maxJobs - 1 DO
			IF (slots[i].state = slotBusy) & slots[i].done THEN
				slots[i].state := slotDone;
				IF slots[i].taskAdr # 0 THEN
					wake[nwake] := slots[i].taskAdr;
					slots[i].taskAdr := 0;
					INC(nwake)
				END
			END
		END;
		res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
		FOR k := 0 TO nwake - 1 DO
			Tasks.Unpark(S.VAL(Tasks.Task, wake[k]))
		END
	END Scan;

	PROCEDURE (w: PipeWaiter) Ready (events: SET);
	BEGIN
		DrainPipe;
		Scan;
		Poller.Add(pipeRead, Poller.In, w)	(* регистрация одноразовая *)
	END Ready;

	(* --- задания ---------------------------------------------------------- *)

	PROCEDURE Submit* (procAdr, arg: S.ADDRESS): INTEGER;
	(** Поставить задание в очередь пула; -1 — нет свободного слота. *)
		VAR i, id: INTEGER; res: INTEGER;
	BEGIN
		id := -1;
		res := LinThreads.pthread_mutex_lock(S.ADR(mu));
		FOR i := 0 TO maxJobs - 1 DO
			IF (id < 0) & (slots[i].state = slotFree) THEN id := i END
		END;
		IF id >= 0 THEN
			IF qn >= queueLen THEN id := -1
			ELSE
				slots[id].state := slotBusy; slots[id].done := FALSE;
				slots[id].procAdr := procAdr; slots[id].arg := arg;
				slots[id].status := 0; slots[id].taskAdr := 0;
				q[qt] := id; qt := (qt + 1) MOD queueLen; INC(qn);
				res := LinThreads.pthread_cond_signal(S.ADR(cv))
			END
		END;
		res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
		RETURN id
	END Submit;

	PROCEDURE Done* (id: INTEGER): BOOLEAN;
		VAR r: BOOLEAN; res: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		res := LinThreads.pthread_mutex_lock(S.ADR(mu));
		r := slots[id].done;
		res := LinThreads.pthread_mutex_unlock(S.ADR(mu));
		RETURN r
	END Done;

	PROCEDURE Result* (id: INTEGER): INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		RETURN slots[id].status
	END Result;

	PROCEDURE Release* (id: INTEGER);
		VAR res: INTEGER;
	BEGIN
		ASSERT((id >= 0) & (id < maxJobs), 20);
		res := LinThreads.pthread_mutex_lock(S.ADR(mu));
		slots[id].state := slotFree; slots[id].done := FALSE;
		slots[id].procAdr := 0; slots[id].arg := 0; slots[id].taskAdr := 0;
		IF LinThreads.pthread_mutex_unlock(S.ADR(mu)) # 0 THEN END
	END Release;

	PROCEDURE SetTask (id: INTEGER; t: Tasks.Task);
		VAR res: INTEGER;
	BEGIN
		res := LinThreads.pthread_mutex_lock(S.ADR(mu));
		slots[id].taskAdr := S.VAL(S.ADDRESS, t);
		IF LinThreads.pthread_mutex_unlock(S.ADR(mu)) # 0 THEN END
	END SetTask;

	PROCEDURE Execute* (procAdr, arg: S.ADDRESS): INTEGER;
	(** Исполнить задание в рабочей нити и дождаться; только из задачи. *)
		VAR id, status: INTEGER;
	BEGIN
		ASSERT(Tasks.InTask(), 21);
		id := Submit(procAdr, arg);
		IF id < 0 THEN RETURN -1 END;
		SetTask(id, Tasks.Current());
		WHILE ~Done(id) DO Tasks.Park END;
		status := Result(id);
		Release(id);
		RETURN status
	END Execute;

	(* --- прикладные задания (в рабочих нитях) ----------------------------- *)

	PROCEDURE [ccall] ReadFileJob* (arg: S.ADDRESS);
		VAR r: FileReqPtr; fd, n: INTEGER;
	BEGIN
		r := S.VAL(FileReqPtr, arg);
		r.len := 0; r.status := -1;
		fd := LinLibc.open(S.VAL(LinLibc.PtrSTR, S.ADR(r.path[0])), {}, {});
		IF fd >= 0 THEN
			n := S.VAL(INTEGER, LinLibc.read(fd, S.ADR(r.buf[0]), maxBuf));
			IF n > 0 THEN r.len := n; r.status := 0 ELSE r.status := -30 END;
			n := LinLibc.close(fd)
		END
	END ReadFileJob;

	PROCEDURE [ccall] WriteFileJob* (arg: S.ADDRESS);
		VAR r: FileReqPtr; fd, n: INTEGER;
	BEGIN
		r := S.VAL(FileReqPtr, arg);
		r.status := -1;
		fd := LinLibc.open(S.VAL(LinLibc.PtrSTR, S.ADR(r.path[0])), writeFlags, {2,5,7,8});
		IF fd >= 0 THEN
			n := S.VAL(INTEGER, LinLibc.write(fd, S.ADR(r.buf[0]), r.len));
			IF n = r.len THEN r.status := 0 ELSE r.status := -20 END;
			n := LinLibc.close(fd)
		END
	END WriteFileJob;

	PROCEDURE [ccall] ResolveJob* (arg: S.ADDRESS);
		VAR r: AddrReqPtr; hints: LinNet.addrinfo;
			res, count: INTEGER; ai, p: LinNet.Ptraddrinfo;
	BEGIN
		r := S.VAL(AddrReqPtr, arg);
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

	PROCEDURE Init;
		VAR fds: ARRAY 2 OF INTEGER; i, res, fl: INTEGER;
	BEGIN
		res := LinThreads.pthread_mutex_init(S.ADR(mu), 0);
		res := LinThreads.pthread_cond_init(S.ADR(cv), 0);
		qh := 0; qt := 0; qn := 0; stopping := FALSE;
		FOR i := 0 TO maxJobs - 1 DO slots[i].state := slotFree; slots[i].done := FALSE END;
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(fds[0])));
		pipeRead := fds[0]; pipeWrite := fds[1];
		fl := LinNative.fcntl(pipeRead, LinNative.F_GETFL, 0);
		res := LinNative.fcntl(pipeRead, LinNative.F_SETFL, fl + LinNative.O_NONBLOCK);	(* O_NONBLOCK *)
		nthreads := 0;
		FOR i := 0 TO maxWorkers - 1 DO
			threads[i] := 0;
			IF LinThreads.pthread_create(threads[i], 0, S.ADR(WorkerMain), 0) = 0 THEN
				INC(nthreads)
			END
		END;
		NEW(waiter);
		IF nthreads > 0 THEN Poller.Add(pipeRead, Poller.In, waiter) END
	END Init;

	PROCEDURE Workers* (): INTEGER;
	BEGIN
		RETURN nthreads
	END Workers;

BEGIN
	Init
CLOSE
	IF nthreads > 0 THEN
		dummy := LinThreads.pthread_mutex_lock(S.ADR(mu));
		stopping := TRUE;
		dummy := LinThreads.pthread_cond_broadcast(S.ADR(cv));
		dummy := LinThreads.pthread_mutex_unlock(S.ADR(mu))
	END
END AsyncWork.
