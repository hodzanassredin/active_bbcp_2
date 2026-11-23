MODULE AsyncComputon;
(** Распределённые вычисления: компутоны (этап 9, образец RobustParallel).

	Computon — сериализуемый Stores.Store с методом Do. Он копируется в
	другой процесс BlackBox (своя куча и сборщик) и возвращается
	обратно; код Do с кучей поэтому исполняется не в рабочей нити этапа 8.

	Транспорт — локальные процессы: fork+exec blackbox-console, три pipe
	на исполнителя (команда на stdin, запросы, ответы). MPI в системе
	нет; интерфейс Push/Pull/Try от транспорта не зависит.

	Ожидание Pull из задачи — точка уступки: ответ забирает главный цикл
	через Poller, задача спит на Tasks.Park. При гибели исполнителя
	запрос повторяется на живом; ошибка Do возвращается вызывающему. *)

	IMPORT S := SYSTEM, Kernel, Tasks, Services, Poller, Stores, Files,
		LinNative, LinLibc, Dialog, Console, Strings, AsyncSelect, Utf,
		CommStreams, CommTCP;

	CONST
		maxPool = 16;
		maxReq = 128;
		maxFrame = 60000;
		kindWork = 0; kindStop = 1; kindUnload = 2;
		unloadBusy = 20;
		pullTimeout = 10000;
		trPipe = 0; trTcp = 1;
		tcpTimeout = 5000;

	TYPE
		Computon* = POINTER TO ABSTRACT RECORD (Stores.Store) END;
		Hook* = POINTER TO ABSTRACT RECORD END;

		Req = POINTER TO RReq;
		RReq = RECORD
			tag, rank, len, status: INTEGER;
			buf: POINTER TO ARRAY OF SHORTCHAR;
			c: Computon;
			waiting: Tasks.Task;
			wctx: AsyncSelect.Ctx;
			widx: INTEGER;
			used, done, unload: BOOLEAN
		END;

		WWait = POINTER TO RECORD (Poller.Waiter) idx: INTEGER END;

		WState = RECORD
			pid, inFd, outFd, inLen: INTEGER;
			dead: BOOLEAN;
			waiter: WWait;
			stream: CommStreams.Stream;	(* TCP: сокет; pipe: NIL *)
			buf: ARRAY maxFrame OF SHORTCHAR	(* накопитель ответов *)
		END;

		PullSignal = POINTER TO RECORD (AsyncSelect.Signal)
			rank, tag: INTEGER;
			req: Req; ctx: AsyncSelect.Ctx
		END;

		DoAction = RECORD (Services.SafeAction) c: Computon END;

		Argv = POINTER [untagged] TO ARRAY [untagged] OF LinLibc.PtrSTR;
		ShBuf = POINTER TO ARRAY OF SHORTCHAR;

	VAR
		workers: ARRAY maxPool OF WState;
		nworkers: INTEGER;
		started: BOOLEAN;
		transport: INTEGER;	(* trPipe или trTcp; выбор до Start *)
		reqs: ARRAY maxReq OF RReq;
		lastErr: INTEGER;
		hook: Hook;
		nextWorker: INTEGER;

		hdr: ARRAY 12 OF SHORTCHAR;
		outLen: INTEGER;
		payload: ARRAY maxFrame OF SHORTCHAR;	(* кадр на разбор *)
		outBuf: ARRAY maxFrame OF SHORTCHAR;	(* кадр на отправку *)

		exePath: ARRAY [untagged] 512 OF SHORTCHAR;
		procPath: ARRAY [untagged] 16 OF SHORTCHAR;
		argIn, argOut: ARRAY [untagged] 32 OF SHORTCHAR;
		argTcp: ARRAY [untagged] 64 OF SHORTCHAR;
		argv: ARRAY [untagged] 4 OF LinLibc.PtrSTR;
		cmdLine: ARRAY [untagged] 32 OF SHORTCHAR;

	(* абстрактные методы *)

	PROCEDURE (c: Computon) Do*, NEW, ABSTRACT;

	PROCEDURE (h: Hook) Failed* (rank, tag, err: INTEGER), NEW, ABSTRACT;

	(* --- числа в кадре ---------------------------------------------------- *)

	PROCEDURE PutInt (VAR b: ARRAY OF SHORTCHAR; pos, x: INTEGER);
	BEGIN
		b[pos] := S.VAL(SHORTCHAR, x MOD 256);
		b[pos+1] := S.VAL(SHORTCHAR, (x DIV 256) MOD 256);
		b[pos+2] := S.VAL(SHORTCHAR, (x DIV 65536) MOD 256);
		b[pos+3] := S.VAL(SHORTCHAR, (x DIV 16777216) MOD 256)
	END PutInt;

	PROCEDURE GetInt (VAR b: ARRAY OF SHORTCHAR; pos: INTEGER): INTEGER;
	BEGIN
		RETURN (ORD(b[pos]) MOD 256)
			+ 256 * (ORD(b[pos+1]) MOD 256)
			+ 65536 * (ORD(b[pos+2]) MOD 256)
			+ 16777216 * (ORD(b[pos+3]) MOD 256)
	END GetInt;

	(* --- сериализация ----------------------------------------------------- *)

	PROCEDURE Serialize* (c: Computon; OUT buf: POINTER TO ARRAY OF SHORTCHAR; OUT len: INTEGER);
	(* через временный файл Files + Stores; buf — копия на куче вызывающего *)
		VAR f: Files.File; wr: Stores.Writer; rd: Files.Reader;
			b: BYTE; i: INTEGER;
	BEGIN
		len := 0; buf := NIL;
		IF c = NIL THEN RETURN END;
		f := Files.dir.Temp();
		IF f = NIL THEN RETURN END;
		wr.ConnectTo(f);
		wr.WriteStore(c);
		f.Flush();
		len := f.Length();
		IF (len > 0) & (len <= maxFrame) THEN
			NEW(buf, len);
			rd := f.NewReader(NIL);
			FOR i := 0 TO len - 1 DO
				rd.ReadByte(b); buf^[i] := S.VAL(SHORTCHAR, b)
			END
		ELSE len := 0
		END
	END Serialize;

	PROCEDURE Deserialize* (VAR b: ARRAY OF SHORTCHAR; len: INTEGER): Computon;
		VAR f: Files.File; wr: Files.Writer; by: BYTE; i: INTEGER;
			rd: Stores.Reader; st: Stores.Store;
	BEGIN
		IF len <= 0 THEN RETURN NIL END;
		f := Files.dir.Temp();
		IF f = NIL THEN RETURN NIL END;
		wr := f.NewWriter(NIL);
		FOR i := 0 TO len - 1 DO
			by := S.VAL(BYTE, b[i]); wr.WriteByte(by)
		END;
		f.Flush();
		rd.ConnectTo(f);
		rd.ReadStore(st);
		IF st # NIL THEN
			WITH st: Computon DO RETURN st END
		END;
		RETURN NIL
	END Deserialize;

	(* --- запись в дескриптор ---------------------------------------------- *)

	PROCEDURE WriteAll (fd: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR off, n, errno, tries, res: INTEGER;
	BEGIN
		off := 0; tries := 0;
		WHILE off < len DO
			n := S.VAL(INTEGER, LinLibc.write(fd, S.ADR(b[off]), len - off));
			IF n > 0 THEN
				INC(off, n)
			ELSIF n < 0 THEN
				S.GET(LinLibc.__errno_location(), errno);
				IF errno = LinLibc.EAGAIN THEN	(* TCP: буфер отправки занят *)
					INC(tries);
					IF tries > 1000 THEN RETURN FALSE END;
					res := LinNative.usleep(1000)
				ELSE RETURN FALSE
				END
			ELSE RETURN FALSE
			END
		END;
		RETURN TRUE
	END WriteAll;

	PROCEDURE ReadAll (fd: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR off, n: INTEGER;
	BEGIN
		off := 0;
		WHILE off < len DO
			n := S.VAL(INTEGER, LinLibc.read(fd, S.ADR(b[off]), len - off));
			IF n <= 0 THEN RETURN FALSE END;
			INC(off, n)
		END;
		RETURN TRUE
	END ReadAll;

	PROCEDURE CloseFd (fd: INTEGER);
		VAR res: INTEGER;
	BEGIN
		res := LinLibc.close(fd)
	END CloseFd;

	(* --- запуск исполнителей ---------------------------------------------- *)

	PROCEDURE MakeArg (IN prefix: ARRAY OF SHORTCHAR; n: INTEGER; OUT dst: ARRAY OF SHORTCHAR);
		VAR i, j: INTEGER; d: ARRAY 16 OF CHAR;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (prefix[i] # 0X) DO dst[i] := prefix[i]; INC(i) END;
		Strings.IntToString(n, d);
		j := 0;
		WHILE (i < LEN(dst) - 1) & (d[j] # 0X) DO dst[i] := SHORT(d[j]); INC(i); INC(j) END;
		dst[i] := 0X
	END MakeArg;

	PROCEDURE MakeArgStr (IN prefix, value: ARRAY OF CHAR; OUT dst: ARRAY OF SHORTCHAR);
		VAR i, j: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (prefix[i] # 0X) DO dst[i] := SHORT(prefix[i]); INC(i) END;
		j := 0;
		WHILE (i < LEN(dst) - 1) & (value[j] # 0X) DO dst[i] := SHORT(value[j]); INC(i); INC(j) END;
		dst[i] := 0X
	END MakeArgStr;

	PROCEDURE SetTransport* (IN name: ARRAY OF CHAR): BOOLEAN;
	(** Выбрать транспорт до Start: "pipe" (локальные pipe, по умолчанию)
		или "tcp" (соединение по localhost). Программа Push/Pull не
		меняется. *)
	BEGIN
		IF name = "tcp" THEN transport := trTcp; RETURN TRUE
		ELSIF (name = "pipe") OR (name = "") THEN transport := trPipe; RETURN TRUE
		ELSE RETURN FALSE
		END
	END SetTransport;

	PROCEDURE CloseWorker (idx: INTEGER);
	(* Закрыть сокет или оба pipe; для TCP один дескриптор — общий. *)
	BEGIN
		IF workers[idx].stream # NIL THEN
			workers[idx].stream.Close; workers[idx].stream := NIL
		ELSE
			CloseFd(workers[idx].inFd); CloseFd(workers[idx].outFd)
		END
	END CloseWorker;

	PROCEDURE AcceptOne (l: CommStreams.Listener; OUT s: CommStreams.Stream): BOOLEAN;
	(** Принять одно соединение с тайм-аутом (Start идёт вне задачи). *)
		VAR t0: LONGINT; res: INTEGER;
	BEGIN
		s := NIL; t0 := Kernel.Time();
		LOOP
			l.Accept(s);
			IF s # NIL THEN RETURN TRUE END;
			IF Kernel.Time() - t0 > tcpTimeout THEN RETURN FALSE END;
			res := LinNative.usleep(1000)
		END
	END AcceptOne;

	PROCEDURE SpawnPipe (idx: INTEGER): BOOLEAN;
		VAR cmdP, reqP, respP: ARRAY 2 OF INTEGER; res, fl, pid, i: INTEGER; ok: BOOLEAN;
	BEGIN
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(cmdP[0])));
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(reqP[0])));
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(respP[0])));
		i := 0; WHILE cmdLine[i] # 0X DO INC(i) END;
		ok := WriteAll(cmdP[1], cmdLine, i);
		MakeArg("--ASYNC_IN=", reqP[0], argIn);
		MakeArg("--ASYNC_OUT=", respP[1], argOut);
		argv[0] := S.VAL(LinLibc.PtrSTR, S.ADR(exePath[0]));
		argv[1] := S.VAL(LinLibc.PtrSTR, S.ADR(argIn[0]));
		argv[2] := S.VAL(LinLibc.PtrSTR, S.ADR(argOut[0]));
		argv[3] := NIL;
		pid := LinLibc.fork();
		IF pid = 0 THEN	(* ребёнок: только async-signal-safe вызовы до exec *)
			res := LinNative.dup2(cmdP[0], 0);
			CloseFd(cmdP[0]);
			CloseFd(reqP[1]);
			CloseFd(respP[0]);
			res := LinLibc.execv(S.VAL(LinLibc.PtrSTR, S.ADR(exePath[0])),
				S.VAL(Argv, S.ADR(argv[0])));
			LinNative._exit(127)
		END;
		CloseFd(cmdP[0]); CloseFd(cmdP[1]);
		CloseFd(reqP[0]); CloseFd(respP[1]);
		IF pid < 0 THEN
			CloseFd(reqP[1]); CloseFd(respP[0]);
			RETURN FALSE
		END;
		workers[idx].pid := pid; workers[idx].inFd := reqP[1];
		workers[idx].outFd := respP[0]; workers[idx].dead := FALSE;
		workers[idx].inLen := 0; workers[idx].stream := NIL;
		fl := LinNative.fcntl(workers[idx].outFd, LinNative.F_GETFL, 0);
		res := LinNative.fcntl(workers[idx].outFd, LinNative.F_SETFL, fl + LinNative.O_NONBLOCK);
		NEW(workers[idx].waiter); workers[idx].waiter.idx := idx;
		Poller.Add(workers[idx].outFd, Poller.In, workers[idx].waiter);
		RETURN TRUE
	END SpawnPipe;

	PROCEDURE SpawnTcp (idx: INTEGER): BOOLEAN;
	(** Исполнитель подключается к слушателю управляющего процесса по
		localhost; дальше тот же поток кадров, что и на pipe. *)
		VAR cmdP: ARRAY 2 OF INTEGER; res, pid, i: INTEGER; ok: BOOLEAN;
			l: CommStreams.Listener; s: CommStreams.Stream; adr: CommStreams.Adr;
	BEGIN
		res := LinNative.pipe(S.VAL(LinNative.IntPtr, S.ADR(cmdP[0])));
		i := 0; WHILE cmdLine[i] # 0X DO INC(i) END;
		ok := WriteAll(cmdP[1], cmdLine, i);
		CommTCP.NewListener("127.0.0.1:0", l, res);
		IF (res # CommStreams.done) OR (l = NIL) THEN
			CloseFd(cmdP[0]); CloseFd(cmdP[1]); RETURN FALSE
		END;
		adr := l.LocalAdr();
		MakeArgStr("--ASYNC_TCP=", adr^, argTcp);
		argv[0] := S.VAL(LinLibc.PtrSTR, S.ADR(exePath[0]));
		argv[1] := S.VAL(LinLibc.PtrSTR, S.ADR(argTcp[0]));
		argv[2] := NIL;
		pid := LinLibc.fork();
		IF pid = 0 THEN	(* ребёнок: команда — на stdin, кадры — по TCP *)
			res := LinNative.dup2(cmdP[0], 0);
			CloseFd(cmdP[0]); CloseFd(cmdP[1]);
			res := LinLibc.execv(S.VAL(LinLibc.PtrSTR, S.ADR(exePath[0])),
				S.VAL(Argv, S.ADR(argv[0])));
			LinNative._exit(127)
		END;
		CloseFd(cmdP[0]); CloseFd(cmdP[1]);
		IF pid < 0 THEN l.Close(); RETURN FALSE END;
		IF ~AcceptOne(l, s) THEN
			l.Close();
			res := LinNative.kill(pid, LinNative.SIGKILL);
			RETURN FALSE
		END;
		l.Close();
		workers[idx].pid := pid; workers[idx].stream := s;
		workers[idx].inFd := s.Handle(); workers[idx].outFd := workers[idx].inFd;
		workers[idx].dead := FALSE; workers[idx].inLen := 0;
		NEW(workers[idx].waiter); workers[idx].waiter.idx := idx;
		Poller.Add(workers[idx].outFd, Poller.In, workers[idx].waiter);
		RETURN TRUE
	END SpawnTcp;

	PROCEDURE Spawn (idx: INTEGER): BOOLEAN;
	BEGIN
		IF transport = trTcp THEN RETURN SpawnTcp(idx) ELSE RETURN SpawnPipe(idx) END
	END Spawn;

	PROCEDURE WorkerAlive (rank: INTEGER): BOOLEAN;
	BEGIN
		RETURN (rank >= 0) & (rank < nworkers) & ~workers[rank].dead
	END WorkerAlive;

	PROCEDURE LiveWorker (except: INTEGER): INTEGER;
		VAR i, k: INTEGER;
	BEGIN
		FOR k := 1 TO nworkers DO
			i := (nextWorker + k) MOD nworkers;
			IF (i # except) & WorkerAlive(i) THEN
				nextWorker := i;
				RETURN i
			END
		END;
		RETURN -1
	END LiveWorker;

	(* --- запросы ---------------------------------------------------------- *)

	PROCEDURE ReqAt (i: INTEGER): Req;
	BEGIN
		RETURN S.VAL(Req, S.ADR(reqs[i]))
	END ReqAt;

	PROCEDURE WakeReq (req: Req);
		VAR t: Tasks.Task; ctx: AsyncSelect.Ctx; idx: INTEGER;
	BEGIN
		IF req.wctx # NIL THEN
			ctx := req.wctx; idx := req.widx; req.wctx := NIL;
			AsyncSelect.SignalReady(ctx, idx)
		ELSIF req.waiting # NIL THEN
			t := req.waiting; req.waiting := NIL; Tasks.Unpark(t)
		END
	END WakeReq;

	PROCEDURE FindReq (tag: INTEGER): Req;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO maxReq - 1 DO
			IF reqs[i].used & (reqs[i].tag = tag) THEN RETURN ReqAt(i) END
		END;
		RETURN NIL
	END FindReq;

	PROCEDURE (s: PullSignal) Arm (ctx: AsyncSelect.Ctx; idx: INTEGER): BOOLEAN;
	BEGIN
		s.ctx := ctx;
		s.req := FindReq(s.tag);
		IF s.req = NIL THEN RETURN TRUE END;	(* решения не будет: ошибка *)
		IF s.req.done THEN RETURN TRUE END;
		s.req.wctx := ctx; s.req.widx := idx;
		RETURN FALSE
	END Arm;

	PROCEDURE (s: PullSignal) Disarm;
	BEGIN
		IF (s.req # NIL) & (s.req.wctx = s.ctx) THEN s.req.wctx := NIL END
	END Disarm;

	PROCEDURE AltPull* (rank, tag: INTEGER): AsyncSelect.Signal;
	(** Альтернатива Select: готовность Pull(rank, tag). *)
		VAR s: PullSignal;
	BEGIN
		NEW(s); s.rank := rank; s.tag := tag; s.req := NIL; s.ctx := NIL;
		RETURN s
	END AltPull;

	PROCEDURE NewReq (tag: INTEGER): Req;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO maxReq - 1 DO
			IF ~reqs[i].used THEN
				reqs[i].used := TRUE; reqs[i].tag := tag;
				reqs[i].done := FALSE; reqs[i].c := NIL;
				reqs[i].buf := NIL; reqs[i].len := 0; reqs[i].status := 0;
				reqs[i].waiting := NIL; reqs[i].wctx := NIL; reqs[i].widx := 0;
				reqs[i].rank := -1; reqs[i].unload := FALSE;
				RETURN ReqAt(i)
			END
		END;
		RETURN NIL
	END NewReq;

	PROCEDURE SendFrame (rank, tag, kind: INTEGER; buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN;
		VAR ok: BOOLEAN;
	BEGIN
		IF ~WorkerAlive(rank) THEN RETURN FALSE END;
		PutInt(hdr, 0, len); PutInt(hdr, 4, tag); PutInt(hdr, 8, kind);
		ok := WriteAll(workers[rank].inFd, hdr, 12);
		IF ok & (len > 0) & (buf # NIL) THEN
			ok := WriteAll(workers[rank].inFd, buf^, len)
		END;
		RETURN ok
	END SendFrame;

	PROCEDURE FailReq (req: Req; err: INTEGER);
		VAR t: Tasks.Task;
	BEGIN
		IF (req # NIL) & ~req.done THEN
			req.status := err; req.done := TRUE;
			WakeReq(req)
		END
	END FailReq;

	PROCEDURE Resend (req: Req);
		VAR r: INTEGER;
	BEGIN
		IF (req = NIL) OR req.done THEN RETURN END;
		r := LiveWorker(req.rank);
		IF r < 0 THEN
			FailReq(req, -2);	(* исполнителей не осталось *)
			IF hook # NIL THEN hook.Failed(req.rank, req.tag, -2) END
		ELSE
			req.rank := r;
			IF ~SendFrame(r, req.tag, kindWork, req.buf, req.len) THEN FailReq(req, -3) END
		END
	END Resend;

	PROCEDURE Push* (c: Computon; rank, tag: INTEGER);
	(** Отправить компутон исполнителю rank (0..Size-1). *)
		VAR req: Req;
	BEGIN
		req := NewReq(tag);
		IF req = NIL THEN lastErr := -1; RETURN END;
		Serialize(c, req.buf, req.len);
		IF (req.buf = NIL) OR (req.len = 0) THEN
			req.used := FALSE; lastErr := -1; RETURN
		END;
		req.rank := rank;
		IF ~SendFrame(rank, tag, kindWork, req.buf, req.len) THEN Resend(req) END
	END Push;

	PROCEDURE PullTimeout* (rank, tag: INTEGER; ms: LONGINT): Computon;
	(** Дождаться результата; при гибели исполнителя — повтор.
		Ожидание — Sleep с проверкой: у задачи есть срок, журнал тупика
		не срабатывает ложно на чужом процессе. *)
		VAR req: Req; c: Computon; deadline: LONGINT;
	BEGIN
		ASSERT(Tasks.InTask(), 21);
		req := FindReq(tag);
		IF req = NIL THEN lastErr := -1; RETURN NIL END;
		deadline := Kernel.Time() + ms;
		WHILE ~req.done & (Kernel.Time() < deadline) DO Tasks.Sleep(10) END;
		IF ~req.done THEN FailReq(req, -4) END;	(* тайм-аут *)
		IF req.status = 0 THEN c := req.c; lastErr := 0
		ELSE lastErr := req.status; c := NIL
		END;
		req.used := FALSE; req.c := NIL; req.buf := NIL;
		IF (lastErr # 0) & (hook # NIL) THEN hook.Failed(rank, tag, lastErr) END;
		RETURN c
	END PullTimeout;

	PROCEDURE Pull* (rank, tag: INTEGER): Computon;
	BEGIN
		RETURN PullTimeout(rank, tag, pullTimeout)
	END Pull;

	PROCEDURE Try* (rank, tag: INTEGER; OUT c: Computon): BOOLEAN;
		VAR req: Req;
	BEGIN
		req := FindReq(tag);
		IF (req = NIL) OR ~req.done THEN c := NIL; RETURN FALSE END;
		IF req.status = 0 THEN c := req.c; lastErr := 0
		ELSE lastErr := req.status; c := NIL
		END;
		req.used := FALSE; req.c := NIL; req.buf := NIL;
		RETURN TRUE
	END Try;

	PROCEDURE Wait*;
	(** Интерфейс RobustParallel: все ожидания уже выражены Pull/Try. *)
	BEGIN
	END Wait;

	PROCEDURE LastError* (): INTEGER;
	BEGIN
		RETURN lastErr
	END LastError;

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		hook := h
	END SetHook;

	PROCEDURE LogMsg* (IN msg: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr("Computon: "); Console.WriteStr(msg); Console.WriteLn
	END LogMsg;

	PROCEDURE Unload* (IN module: ARRAY OF CHAR): BOOLEAN;
	(** Выгрузить код модуля у всех живых исполнителей, чтобы следующий
		Push загрузил обновлённый .ocf. FALSE — хотя бы один исполнитель
		отказал (модуль занят: импортирован или его кадр есть на стеке)
		или не ответил; код отказа — в LastError.
		Звать можно вне задачи: ответы собираются через Poller. *)
		VAR i, tag, len: INTEGER; deadline: LONGINT; req: Req; all, ready: BOOLEAN;
			buf: ShBuf;
	BEGIN
		lastErr := 0;
		IF ~started OR (nworkers = 0) THEN RETURN TRUE END;
		len := 0;
		WHILE (len < maxFrame - 1) & (module[len] # 0X) DO INC(len) END;
		IF len = 0 THEN lastErr := -1; RETURN FALSE END;
		NEW(buf, len);
		FOR i := 0 TO len - 1 DO buf[i] := SHORT(module[i]) END;
		all := TRUE;
		FOR i := 0 TO nworkers - 1 DO
			IF WorkerAlive(i) THEN
				tag := -1 - i;
				req := NewReq(tag);
				IF req = NIL THEN lastErr := -1; all := FALSE
				ELSE
					req.unload := TRUE; req.rank := i;
					IF ~SendFrame(i, tag, kindUnload, buf, len) THEN FailReq(req, -3) END
				END
			END
		END;
		deadline := Kernel.Time() + pullTimeout;
		LOOP
			ready := TRUE;
			FOR i := 0 TO maxReq - 1 DO
				IF reqs[i].used & reqs[i].unload & ~reqs[i].done THEN ready := FALSE END
			END;
			IF ready OR (Kernel.Time() >= deadline) THEN EXIT END;
			IF Poller.Wait(1) THEN END
		END;
		FOR i := 0 TO maxReq - 1 DO
			IF reqs[i].used & reqs[i].unload THEN
				IF ~reqs[i].done THEN reqs[i].status := -4; reqs[i].done := TRUE END;
				IF reqs[i].status # 0 THEN all := FALSE; lastErr := reqs[i].status END;
				reqs[i].used := FALSE; reqs[i].buf := NIL; reqs[i].c := NIL
			END
		END;
		RETURN all
	END Unload;

	PROCEDURE Size* (): INTEGER;
	BEGIN
		RETURN nworkers
	END Size;

	PROCEDURE Rank* (): INTEGER;
	BEGIN
		RETURN 0	(* управляющий процесс *)
	END Rank;

	PROCEDURE WorkerPid* (rank: INTEGER): INTEGER;
	BEGIN
		IF (rank >= 0) & (rank < nworkers) THEN RETURN workers[rank].pid ELSE RETURN -1 END
	END WorkerPid;

	PROCEDURE WorkerDead* (rank: INTEGER): BOOLEAN;
	BEGIN
		IF (rank >= 0) & (rank < nworkers) THEN RETURN workers[rank].dead ELSE RETURN TRUE END
	END WorkerDead;

	PROCEDURE Stop*;
	(** Погасить исполнителей. SIGCHLD в BlackBox игнорируется, поэтому
		waitpid не нужен (и в консольном x64 он небезопасен): закрываем
		pipes и убиваем процессы. *)
		VAR i, res: INTEGER; ok: BOOLEAN;
	BEGIN
		IF started THEN
			FOR i := 0 TO nworkers - 1 DO
				IF workers[i].waiter # NIL THEN Poller.Remove(workers[i].waiter) END;
				IF ~workers[i].dead THEN
					ok := SendFrame(i, 0, kindStop, NIL, 0);
					CloseWorker(i);
					res := LinNative.kill(workers[i].pid, LinNative.SIGKILL);
					workers[i].dead := TRUE
				END
			END
		END;
		started := FALSE; nworkers := 0
	END Stop;

	PROCEDURE Start* (n: INTEGER);
		VAR i: INTEGER;
	BEGIN
		ASSERT((1 <= n) & (n <= maxPool), 20);
		Stop;
		nworkers := 0;
		FOR i := 0 TO n - 1 DO
			IF Spawn(i) THEN INC(nworkers) END
		END;
		started := nworkers > 0;
	END Start;

	(* --- ответы исполнителей ---------------------------------------------- *)

	PROCEDURE MarkDead* (idx: INTEGER);
		VAR i: INTEGER;
	BEGIN
		IF workers[idx].dead THEN RETURN END;
		workers[idx].dead := TRUE;
		CloseWorker(idx);
		FOR i := 0 TO maxReq - 1 DO
			IF reqs[i].used & ~reqs[i].done & (reqs[i].rank = idx) THEN
				(* выгрузку не повторяем на другом исполнителе *)
				IF reqs[i].unload THEN FailReq(ReqAt(i), -2) ELSE Resend(ReqAt(i)) END
			END
		END
	END MarkDead;

	PROCEDURE ParseWorker (idx: INTEGER);
		VAR off, len, tag, st, rem, i: INTEGER; req: Req;
	BEGIN
		off := 0;
		LOOP
			IF workers[idx].inLen - off < 12 THEN EXIT END;
			len := GetInt(workers[idx].buf, off);
			tag := GetInt(workers[idx].buf, off + 4);
			st := GetInt(workers[idx].buf, off + 8);
			IF (len < 0) OR (len > maxFrame) THEN MarkDead(idx); RETURN END;
			IF workers[idx].inLen - off < 12 + len THEN EXIT END;
			req := FindReq(tag);
			IF (req # NIL) & ~req.done THEN
				IF req.unload THEN req.status := st	(* ответ без нагрузки *)
				ELSIF st = 0 THEN
					FOR i := 0 TO len - 1 DO payload[i] := workers[idx].buf[off + 12 + i] END;
					req.c := Deserialize(payload, len);
					IF req.c = NIL THEN req.status := -5 ELSE req.status := 0 END
				ELSE req.status := st
				END;
				req.done := TRUE;
				WakeReq(req)
			END;
			INC(off, 12 + len)
		END;
		rem := workers[idx].inLen - off;
		IF rem > 0 THEN
			FOR i := 0 TO rem - 1 DO workers[idx].buf[i] := workers[idx].buf[off + i] END
		END;
		workers[idx].inLen := rem
	END ParseWorker;

	PROCEDURE (w: WWait) Ready (events: SET);
		VAR idx, n: INTEGER; eof: BOOLEAN;
	BEGIN
		idx := w.idx;
		IF workers[idx].dead THEN RETURN END;
		eof := FALSE;
		LOOP
			n := S.VAL(INTEGER, LinLibc.read(workers[idx].outFd,
				S.ADR(workers[idx].buf[workers[idx].inLen]),
				maxFrame - workers[idx].inLen));
			IF n > 0 THEN
				INC(workers[idx].inLen, n);
				ParseWorker(idx)
			ELSIF n = 0 THEN eof := TRUE; EXIT
			ELSE EXIT
			END
		END;
		IF eof THEN MarkDead(idx)
		ELSIF ~workers[idx].dead THEN Poller.Add(workers[idx].outFd, Poller.In, w)
		END
	END Ready;

	(* --- процесс-исполнитель ---------------------------------------------- *)

	PROCEDURE (VAR a: DoAction) Do;
	BEGIN
		a.c.Do
	END Do;

	PROCEDURE NoopViewer;
	BEGIN
	END NoopViewer;

	PROCEDURE RunDo (c: Computon; OUT err: INTEGER);
		VAR a: DoAction;
	BEGIN
		a.c := c;
		Services.Try(a);
		IF a.trapped THEN err := 1 ELSE err := 0 END
	END RunDo;

	PROCEDURE UnloadModule (len: INTEGER): INTEGER;
	(** Выгрузить модуль в процессе-исполнителе. 0 — выгружен или не был
		загружен, unloadBusy — занят (импортирован или код есть на стеке). *)
		VAR name: ARRAY 128 OF CHAR; res: INTEGER; m, self: Kernel.Module;
	BEGIN
		IF (len <= 0) OR (len >= LEN(payload)) THEN RETURN -1 END;
		payload[len] := 0X;
		Utf.Utf8ToString(payload, name, res);
		IF res # 0 THEN RETURN -1 END;
		m := Kernel.ThisLoadedMod(name);
		IF m = NIL THEN RETURN 0 END;
		(* Свой транспортный модуль исполняется прямо сейчас (кадр
		   WorkerLoop на стеке): выгружать его нельзя. *)
		self := Kernel.ThisLoadedMod("AsyncComputon");
		IF m = self THEN RETURN unloadBusy END;
		Kernel.UnloadMod(m);
		IF Kernel.ThisLoadedMod(name) = NIL THEN RETURN 0 ELSE RETURN unloadBusy END
	END UnloadModule;

	PROCEDURE WorkerLoop*;
	(** Точка входа процесса-исполнителя; fds — из --ASYNC_IN/--ASYNC_OUT. *)
		VAR inFd, outFd, len, tag, kind, err, res, i, fl: INTEGER;
			val: ARRAY 64 OF CHAR; c: Computon; ok: BOOLEAN;
			tmp: POINTER TO ARRAY OF SHORTCHAR; tcps: CommStreams.Stream;
	BEGIN
		(* консольный x64 иначе считает фатальным любой трап, даже в Try *)
		Kernel.InstallTrapViewer(NoopViewer);
		Dialog.GetPar("ASYNC_TCP", val, res);
		IF res = 0 THEN
			(* TCP: один сокет на оба направления, адрес дал управляющий.
			   Сокет из CommTCP неблокирующий; в исполнителе кадры читает
			   только он сам, поэтому возвращаем блокирующий режим. *)
			CommTCP.NewStream("", val, tcps, res);
			IF (res # CommStreams.done) OR (tcps = NIL) THEN RETURN END;
			inFd := tcps.Handle(); outFd := inFd;
			fl := LinNative.fcntl(inFd, LinNative.F_GETFL, 0);
			res := LinNative.fcntl(inFd, LinNative.F_SETFL, fl - LinNative.O_NONBLOCK)
		ELSE
			Dialog.GetPar("ASYNC_IN", val, res);
			inFd := 0;
			IF res = 0 THEN Strings.StringToInt(val, inFd, res) END;
			Dialog.GetPar("ASYNC_OUT", val, res);
			outFd := 1;
			IF res = 0 THEN Strings.StringToInt(val, outFd, res) END
		END;
		LOOP
			IF ~ReadAll(inFd, hdr, 12) THEN EXIT END;
			len := GetInt(hdr, 0); tag := GetInt(hdr, 4); kind := GetInt(hdr, 8);
			IF kind = kindStop THEN EXIT END;
			IF (len < 0) OR (len > maxFrame) THEN EXIT END;
			IF kind = kindUnload THEN
				IF (len > 0) & ReadAll(inFd, payload, len) THEN
					err := UnloadModule(len); outLen := 0
				ELSE err := -1; outLen := 0
				END
			ELSE
				IF (len > 0) & ~ReadAll(inFd, payload, len) THEN EXIT END;
				c := Deserialize(payload, len);
				IF c = NIL THEN err := -1 ELSE RunDo(c, err) END;
				outLen := 0;
				IF err = 0 THEN
					Serialize(c, tmp, outLen);
					IF (tmp # NIL) & (outLen <= maxFrame) THEN
						FOR i := 0 TO outLen - 1 DO outBuf[i] := tmp^[i] END
					ELSE err := -6; outLen := 0
					END
				END
			END;
			PutInt(hdr, 0, outLen); PutInt(hdr, 4, tag); PutInt(hdr, 8, err);
			ok := WriteAll(outFd, hdr, 12);
			IF ok & (outLen > 0) THEN ok := WriteAll(outFd, outBuf, outLen) END;
			(* не держать в кадре объект выгруженного модуля: иначе
			   следующий Collect разыменует его таблицу методов *)
			c := NIL;
			IF ~ok THEN EXIT END
		END
	END WorkerLoop;

	(* --- инициализация ---------------------------------------------------- *)

	PROCEDURE Init;
		VAR i, n, res: INTEGER; val: ARRAY 32 OF CHAR;
	BEGIN
		FOR i := 0 TO maxReq - 1 DO reqs[i].used := FALSE END;
		FOR i := 0 TO maxPool - 1 DO
			workers[i].dead := TRUE; workers[i].pid := 0; workers[i].inLen := 0;
			workers[i].stream := NIL
		END;
		started := FALSE; nworkers := 0; nextWorker := 0; lastErr := 0; hook := NIL;
		transport := trPipe;
		Dialog.GetPar("ASYNC_TRANSPORT", val, res);
		IF res = 0 THEN IF ~SetTransport(val) THEN transport := trPipe END END;
		procPath := "/proc/self/exe";
		n := S.VAL(INTEGER, LinLibc.readlink(S.VAL(LinLibc.PtrSTR, S.ADR(procPath[0])),
			S.VAL(LinLibc.PtrSTR, S.ADR(exePath[0])), 511));
		IF n <= 0 THEN exePath[0] := 0X END;
		cmdLine := "AsyncComputon.WorkerLoop";
		i := 0; WHILE cmdLine[i] # 0X DO INC(i) END;
		cmdLine[i] := S.VAL(SHORTCHAR, 10); cmdLine[i+1] := 0X
	END Init;

BEGIN
	Init
END AsyncComputon.
