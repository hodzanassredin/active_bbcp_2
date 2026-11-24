MODULE Async_devIO;
(** Тесты ввода-вывода: прямой стиль на localhost.

	Эхо-сервер и 1000 одновременных соединений; пока соединения открыты
	и молчат, процессор в покое не занят (poll спит). Отдельно —
	HTTP-подобный ответ. Все задачи ждут готовности дескриптора через
	AsyncIO/Poller; главный стек крутит действия Services и Poll. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, CommStreams, CommTCP, AsyncIO,
		Libc := LinLibc, UnitAssert, Strings, Console;

	CONST
		clients = 1000;
		msgLen = 5;
		cpuLimitMs = 250;	(* из 1000 мс покоя *)
		echoMode = 1; httpMode = 2;

	TYPE
		Server = POINTER TO RECORD (Tasks.Task)
			l: CommStreams.Listener; limit, mode: INTEGER
		END;
		Handler = POINTER TO RECORD (Tasks.Task)
			s: CommStreams.Stream; mode: INTEGER
		END;
		Client = POINTER TO RECORD (Tasks.Task)
			adr: ARRAY 64 OF CHAR; id: INTEGER
		END;
		HClient = POINTER TO RECORD (Tasks.Task)
			adr: ARRAY 64 OF CHAR; id: INTEGER
		END;
		Acceptor = POINTER TO RECORD (Tasks.Task)
			l: CommStreams.Listener; ok: BOOLEAN; elapsed: LONGINT
		END;

	VAR
		stopServer, release: BOOLEAN;
		accepted, echoed, connOk, served, httpOk: INTEGER;
		cl: ARRAY clients OF Client;
		cpuMs: LONGINT;
		dbg: ARRAY 64 OF CHAR;

	(* --- насос главного цикла --------------------------------------------- *)

	PROCEDURE PumpFor (ms: LONGINT);
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE Kernel.Time() - t0 <= ms DO
			Services.actionHook.Step;
			AsyncIO.Poll(5)
		END
	END PumpFor;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE EchoReady (): BOOLEAN;
	BEGIN RETURN echoed >= clients END EchoReady;

	PROCEDURE HttpReady (): BOOLEAN;
	BEGIN RETURN httpOk >= 3 END HttpReady;

	PROCEDURE NoTasks (): BOOLEAN;
	BEGIN RETURN Tasks.Count() = 0 END NoTasks;

	PROCEDURE CopyAdr (IN src: ARRAY OF CHAR; OUT dst: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(dst) - 1) & (src[i] # 0X) DO dst[i] := src[i]; INC(i) END;
		dst[i] := 0X
	END CopyAdr;

	(* --- обмен ------------------------------------------------------------ *)

	PROCEDURE ReadN (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; n: INTEGER): INTEGER;
		VAR beg, k: INTEGER;
	BEGIN
		beg := 0;
		WHILE beg < n DO
			AsyncIO.Read(s, x, beg, n - beg, k);
			IF k <= 0 THEN RETURN beg END;
			INC(beg, k)
		END;
		RETURN beg
	END ReadN;

	PROCEDURE WriteN (s: CommStreams.Stream; IN x: ARRAY OF BYTE; n: INTEGER): INTEGER;
		VAR beg, k: INTEGER;
	BEGIN
		beg := 0;
		WHILE beg < n DO
			AsyncIO.Write(s, x, beg, n - beg, k);
			IF k <= 0 THEN RETURN beg END;
			INC(beg, k)
		END;
		RETURN beg
	END WriteN;

	PROCEDURE WriteStrN (s: CommStreams.Stream; IN text: ARRAY OF SHORTCHAR): INTEGER;
		VAR x: ARRAY 128 OF BYTE; i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(x)) & (text[i] # 0X) DO
			x[i] := S.VAL(BYTE, ORD(text[i])); INC(i)
		END;
		RETURN WriteN(s, x, i)
	END WriteStrN;

	PROCEDURE WriteCrlf (s: CommStreams.Stream);
		VAR x: ARRAY 2 OF BYTE; n: INTEGER;
	BEGIN
		x[0] := 13; x[1] := 10;
		n := WriteN(s, x, 2)
	END WriteCrlf;

	(* --- сервер ----------------------------------------------------------- *)

	PROCEDURE (t: Handler) Run;
		VAR x: ARRAY 128 OF BYTE; n, state: INTEGER;
	BEGIN
		IF t.mode = echoMode THEN
			n := ReadN(t.s, x, msgLen);
			IF n = msgLen THEN
				IF WriteN(t.s, x, msgLen) = msgLen THEN INC(served) END
			END;
			(* держим соединение открытым, пока клиент не отпустит *)
			n := ReadN(t.s, x, 1)
		ELSE
			(* HTTP: ждём пустую строку (13,10,13,10) *)
			state := 0;
			LOOP
				n := ReadN(t.s, x, 1);
				IF n # 1 THEN EXIT END;
				CASE state OF
					0: IF x[0] = 0DH THEN state := 1 END
				| 1: IF x[0] = 0AH THEN state := 2 ELSIF x[0] # 0DH THEN state := 0 END
				| 2: IF x[0] = 0DH THEN state := 3 ELSE state := 0 END
				| 3: IF x[0] = 0AH THEN EXIT ELSE state := 0 END
				END
			END;
			n := WriteStrN(t.s, "HTTP/1.0 200 OK"); WriteCrlf(t.s);
			n := WriteStrN(t.s, "Content-Length: 2"); WriteCrlf(t.s); WriteCrlf(t.s);
			n := WriteStrN(t.s, "ok")
		END;
		t.s.Close()
	END Run;

	PROCEDURE (t: Server) Run;
		VAR s: CommStreams.Stream; h: Handler;
	BEGIN
		WHILE ~stopServer & (accepted < t.limit) DO
			IF AsyncIO.Accept(t.l, s, 200) THEN
				INC(accepted);
				NEW(h); h.s := s; h.mode := t.mode;
				Tasks.SetStackSize(h, 64 * 1024);
				Tasks.Start(h)
			END
		END;
		t.l.Close()
	END Run;

	(* --- клиент эха ------------------------------------------------------- *)

	PROCEDURE (t: Client) Run;
		VAR s: CommStreams.Stream; res, n: INTEGER;
			x: ARRAY 128 OF BYTE;
			t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		LOOP
			CommTCP.NewStream("", t.adr, s, res);
			IF (res = CommStreams.done) & (s # NIL) THEN
				x[0] := S.VAL(BYTE, ORD("a") + t.id MOD 26);
				x[1] := S.VAL(BYTE, ORD("0") + t.id MOD 10);
				x[2] := S.VAL(BYTE, ORD("A") + t.id MOD 26);
				x[3] := 33; x[4] := 63;
				n := WriteN(s, x, msgLen);
				IF n = msgLen THEN
					n := ReadN(s, x, msgLen);
					IF n = msgLen THEN
						INC(connOk); INC(echoed);
						(* ждём освобождения, не занимая цикл *)
						WHILE ~release DO Tasks.Park END;
						s.Close();
						RETURN
					END
				END;
				s.Close()
			END;
			IF Kernel.Time() - t0 > 15000 THEN RETURN END;
			Tasks.Sleep(5)
		END
	END Run;

	PROCEDURE NewEchoServer (limit: INTEGER; OUT l: CommStreams.Listener; OUT srv: Server): BOOLEAN;
		VAR res: INTEGER;
	BEGIN
		CommTCP.NewListener("127.0.0.1:0", l, res);
		IF (res # CommStreams.done) OR (l = NIL) THEN RETURN FALSE END;
		NEW(srv); srv.l := l; srv.limit := limit; srv.mode := echoMode;
		Tasks.Start(srv);
		RETURN TRUE
	END NewEchoServer;

	(* --- 1000 соединений и покой ------------------------------------------ *)

	PROCEDURE TestEcho1000*;
		VAR l: CommStreams.Listener; srv: Server; adr: CommStreams.Adr;
			i: INTEGER; c0, c1: Libc.clock_t;
	BEGIN
		stopServer := FALSE; release := FALSE;
		accepted := 0; echoed := 0; connOk := 0; served := 0; cpuMs := 0;
		UnitAssert.True(NewEchoServer(clients, l, srv), "echo server starts");
		adr := l.LocalAdr();
		FOR i := 0 TO clients - 1 DO
			NEW(cl[i]); CopyAdr(adr^, cl[i].adr); cl[i].id := i;
			Tasks.SetStackSize(cl[i], 64 * 1024);
			Tasks.Start(cl[i])
		END;
		UnitAssert.True(PumpUntil(30000, EchoReady), "1000 clients echoed");
		UnitAssert.EqualInt(clients, connOk, "all clients connected");
		(* 1000 соединений открыты и молчат; меряем процессорное время *)
		c0 := Libc.clock();
		PumpFor(1000);
		c1 := Libc.clock();
		cpuMs := (c1 - c0) * 1000 DIV Libc.CLOCKS_PER_SEC;
		UnitAssert.True(cpuMs < cpuLimitMs, "idle CPU below limit");
		Console.WriteStr("Async_devIO: idle cpu ms = "); Strings.IntToString(cpuMs, dbg);
		Console.WriteStr(dbg); Console.WriteLn;
		(* отпустить всех и погасить сервер *)
		release := TRUE;
		FOR i := 0 TO clients - 1 DO Tasks.Unpark(cl[i]) END;
		stopServer := TRUE;
		UnitAssert.True(PumpUntil(15000, NoTasks), "all tasks finish");
		UnitAssert.EqualInt(clients, served, "server echoed 1000")
	END TestEcho1000;

	(* --- HTTP ------------------------------------------------------------- *)

	PROCEDURE (t: HClient) Run;
		VAR s: CommStreams.Stream; res, n, len: INTEGER; x: ARRAY 128 OF BYTE;
			prev: INTEGER; ok: BOOLEAN;
	BEGIN
		CommTCP.NewStream("", t.adr, s, res);
		IF (res = CommStreams.done) & (s # NIL) THEN
			IF WriteStrN(s, "GET / HTTP/1.0") > 0 THEN
				WriteCrlf(s); WriteCrlf(s);
				len := 0; prev := -1; ok := FALSE;
				LOOP
					n := ReadN(s, x, 1);
					IF n # 1 THEN EXIT END;
					INC(len);
					IF (prev = ORD("o")) & (x[0] = ORD("k")) THEN ok := TRUE; EXIT END;
					prev := x[0];
					IF len > 200 THEN EXIT END
				END;
				IF ok THEN INC(httpOk) END
			END;
			s.Close()
		END
	END Run;

	PROCEDURE TestHttp*;
		VAR l: CommStreams.Listener; srv: Server; res, i: INTEGER;
			adr: CommStreams.Adr; hc: HClient;
	BEGIN
		stopServer := FALSE; httpOk := 0; accepted := 0;
		CommTCP.NewListener("127.0.0.1:0", l, res);
		UnitAssert.True((res = CommStreams.done) & (l # NIL), "http listener");
		NEW(srv); srv.l := l; srv.limit := 3; srv.mode := httpMode;
		Tasks.Start(srv);
		adr := l.LocalAdr();
		FOR i := 0 TO 2 DO
			NEW(hc); CopyAdr(adr^, hc.adr); hc.id := i; Tasks.Start(hc)
		END;
		UnitAssert.True(PumpUntil(10000, HttpReady), "http responses");
		stopServer := TRUE;
		UnitAssert.True(PumpUntil(5000, NoTasks), "http tasks finish");
		UnitAssert.EqualInt(3, httpOk, "three HTTP replies")
	END TestHttp;

	PROCEDURE (t: Acceptor) Run;
		VAR s: CommStreams.Stream; t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		t.ok := AsyncIO.Accept(t.l, s, 100);
		t.elapsed := Kernel.Time() - t0
	END Run;

	PROCEDURE TestAcceptTimeout*;
	(** Accept без клиента возвращается по сроку. *)
		VAR l: CommStreams.Listener; res: INTEGER; a: Acceptor;
	BEGIN
		CommTCP.NewListener("127.0.0.1:0", l, res);
		UnitAssert.True((res = CommStreams.done) & (l # NIL), "listener");
		NEW(a); a.l := l; a.ok := TRUE; a.elapsed := -1; Tasks.Start(a);
		UnitAssert.True(PumpUntil(2000, NoTasks), "accept returned");
		UnitAssert.True(~a.ok, "no connection accepted");
		UnitAssert.True((a.elapsed >= 100) & (a.elapsed < 1000), "returned by the deadline");
		Tasks.Cancel(a); IF PumpUntil(1000, NoTasks) THEN END;	(* уборка после провала *)
		l.Close
	END TestAcceptTimeout;

END Async_devIO.
