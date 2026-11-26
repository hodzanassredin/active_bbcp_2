MODULE ObxRaft1;
	IMPORT Tasks, Services, Console, Strings, Stores,
		ActorCore, ActorRun, ActorPersist, ActorNet, ActorNetMem,
		RaftCore, RaftStorage, RaftNode, RaftJournal;

	CONST n = 3;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Value = POINTER TO RECORD (ActorCore.Msg) v: INTEGER END;

		Added = POINTER TO RECORD (ActorPersist.Event) END;
		Counter = POINTER TO RECORD (ActorPersist.Actor) v: INTEGER END;

		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		net: ActorNetMem.Net;
		nodes: ARRAY n OF ActorNet.Node;
		cfgs: ARRAY n OF RaftNode.Config;
		own: ARRAY n OF ActorCore.Ref;
		caps: RaftNode.Caps;
		journal: RaftJournal.Client;
		counter: ActorCore.Ref;
		clients: INTEGER;
		finished: BOOLEAN;

	PROCEDURE WriteInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* событие «прибавлено 1»: полей нет *)
	PROCEDURE (e: Added) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0)
	END Externalize;

	PROCEDURE (e: Added) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v)
	END Internalize;

	PROCEDURE (a: Counter) Apply (e: ActorPersist.Event);
	BEGIN
		INC(a.v)
	END Apply;

	PROCEDURE (a: Counter) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Added; r: Value;
	BEGIN
		WITH m: Inc DO NEW(e); a.Persist(c, e, 0)	(* ответ — в Persisted *)
		| m: Get DO NEW(r); r.v := a.v; c.Respond(r)
		| m: ActorPersist.ReplayComplete DO
			Console.WriteStr("  counter is up with value "); WriteInt(a.v); Console.WriteLn
		ELSE
		END
	END Handle;

	PROCEDURE (a: Counter) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
	(* событие зафиксировано большинством узлов и применено: ответить на Inc *)
		VAR r: Value;
	BEGIN
		NEW(r); r.v := a.v; c.Respond(r)
	END Persisted;

	PROCEDURE NewCounter (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); a.journal := journal; a.stream := "counter"; RETURN a
	END NewCounter;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER; a: RaftNode.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			a := cfgs[i].actor;
			IF nodes[i].up & (a # NIL) & (a.nd # NIL) & (a.nd.role = RaftCore.leader) THEN RETURN i END
		END;
		RETURN -1
	END Leader;

	PROCEDURE StartCounter (i: INTEGER);
	(* счётчик — ребёнок узла-актора i: падает вместе с узлом *)
	BEGIN
		INC(clients); journal := RaftJournal.New(nodes[i], caps, n, clients);
		counter := ActorCore.Spawn(own[i](ActorCore.Context), ActorCore.NewProps(NewCounter), "counter")
	END StartCounter;

	PROCEDURE Call (m: ActorCore.Msg);
		VAR reply: ActorCore.Msg;
	BEGIN
		IF (ActorCore.Ask(counter, m, 30000, reply) = ActorCore.ok) & (reply IS Value) THEN
			Console.WriteStr("  counter = "); WriteInt(reply(Value).v); Console.WriteLn
		ELSE Console.WriteStr("  no answer"); Console.WriteLn
		END
	END Call;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR l, k, i: INTEGER; inc: Inc; get: Get;
	BEGIN
		WHILE Leader() < 0 DO Tasks.Sleep(5) END;
		l := Leader();
		Console.WriteStr("three nodes, a leader is elected"); Console.WriteLn;
		Console.WriteStr("counter starts on the leader's node"); Console.WriteLn;
		StartCounter(l);
		NEW(get); Call(get);
		FOR k := 1 TO 3 DO Console.WriteStr("inc"); Console.WriteLn; NEW(inc); Call(inc) END;
		Console.WriteStr("the leader's node crashes, the counter dies with it"); Console.WriteLn;
		ActorNetMem.Crash(net, nodes[l].name);
		i := (l + 1) MOD n;
		Console.WriteStr("counter starts on another node"); Console.WriteLn;
		StartCounter(i);
		NEW(get); Call(get);
		Console.WriteStr("inc"); Console.WriteLn; NEW(inc); Call(inc);
		ActorRun.Shutdown;
		finished := TRUE
	END Body;

	PROCEDURE Do*;
		VAR t: Driver; i, k: INTEGER; name: ARRAY 2 OF CHAR;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		net := ActorNetMem.NewAuto(); clients := 0;
		FOR i := 0 TO n - 1 DO
			name[0] := CHR(ORD("A") + i); name[1] := 0X;
			nodes[i] := ActorNet.New(name, net, 11 * (i + 1)); ActorNetMem.Add(net, nodes[i]);
			cfgs[i] := RaftNode.NewConfig(i, n, nodes[i], RaftStorage.NewMem(), RaftJournal.NewMachine);
			cfgs[i].tickMs := 5;	(* свои часы: тик каждые 5 мс, выборы — через 50 .. 100 мс *)
			own[i] := RaftNode.Start(cfgs[i]);
			ActorNet.Export(nodes[i], own[i], caps[i])
		END;
		FOR i := 0 TO n - 1 DO FOR k := 0 TO n - 1 DO cfgs[k].peers[i] := caps[i]$ END END;
		finished := FALSE;
		NEW(t); Tasks.Start(t);
		WHILE ~finished DO Services.actionHook.Step END
	END Do;

END ObxRaft1.
