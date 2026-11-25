# Overview by Example: ObxRaft1

A persistent counter actor whose event journal is Raft: the counter survives the death of its node.

Three `RaftNode` nodes with their own clocks (a tick every 5 ms) hold the `RaftJournal` journal: each
event of the actor is a Raft entry. The counter is an `ActorPersist.Actor` with `journal` =
`RaftJournal.Client`; it lives as a child of a node actor and dies with the node. The executor is the
`ActorRun` pool. `Persist` does not wait for the commit: it hands the event to the journal, and the
counter replies to `Inc` in `Persisted` when a majority of nodes has stored the entry; new messages
are stashed until then. The example belongs to the guide `Raft/Docu/Raft.md` (section "Journal of persistent
actors").

```
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

		Driver = POINTER TO RECORD (Tasks.Task) END;

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

	PROCEDURE (t: Driver) Run;
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
	END Run;

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
```

In the console:

```sh
echo 'ObxRaft1.Do' | tools/run64.sh
```

The log shows:

```
three nodes, a leader is elected
counter starts on the leader's node
  counter is up with value 0
  counter = 0
inc
  counter = 1
inc
  counter = 2
inc
  counter = 3
the leader's node crashes, the counter dies with it
counter starts on another node
  counter is up with value 3
  counter = 3
inc
  counter = 4
```

## What Happened

The counter started on the leader's node and read its stream `counter` through Raft (`LoadSnapshot`
and `Read` are ReadIndex reads): the stream is empty, the value is 0. Each `inc` is a `Persist`: the
event went to the leader as a Raft command, and the answer "written" came only after a majority of
nodes had stored the entry and the leader had applied it to its copy of the journal; then the counter
applied the event and replied to `Inc` in `Persisted`.

The leader's node crashed, and the counter with it. The other two nodes elected a new leader. A new
incarnation of the counter on another node, with a new journal client, read the stream through the
new leader — value 3 — and went on: 4. Confirmed events were not lost although the node that wrote
them is gone.

In this example we saw `ActorPersist` on top of Raft: a write waits for a majority, recovery goes
through a linearizable read, and a node's death loses nothing confirmed.
