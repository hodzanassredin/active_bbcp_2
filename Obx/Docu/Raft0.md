# Overview by Example: ObxRaft0

Three Raft nodes in memory: an election, writes, a read, the leader's death and the return of a node.

Nodes A, B and C are `RaftNode` node actors on an `ActorNetMem` network; each has its own "sum" state
machine and its own durable storage in memory (`RaftStorage.NewMem`). The client is an actor on a
separate node K. The executor is `ActorSim`, and the example sends the clock ticks itself, so a run
repeats character for character. The example belongs to the guide `Raft/Docu/Raft.md` (section
"Node actor").

```
MODULE ObxRaft0;
	IMPORT Tasks, Services, Console, Strings,
		ActorCore, ActorNet, ActorNetMem, ActorSim, RaftCore, RaftSm, RaftStorage, RaftNode;

	CONST n = 3;

	TYPE
		(* автомат: сумма; команда — одно слагаемое *)
		Sum = POINTER TO RECORD (RaftSm.StateMachine) v: INTEGER END;
		(* местное: клиенту — кому и что послать *)
		Send = POINTER TO RECORD (ActorCore.Msg) to: ActorCore.Ref; m: ActorNet.Msg END;
		Client = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		net: ActorNetMem.Net;
		nodes: ARRAY n OF ActorNet.Node;
		cfgs: ARRAY n OF RaftNode.Config;
		own: ARRAY n OF ActorCore.Ref;	(* узлы-акторы, местные адреса для тиков *)
		refs: ARRAY n OF ActorCore.Ref;	(* они же с узла клиента *)
		client: ActorCore.Ref;
		reply: RaftNode.Reply;
		seq: INTEGER;
		finished: BOOLEAN;

	PROCEDURE WriteInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	PROCEDURE Int (b: RaftCore.Bytes): INTEGER;
	BEGIN
		RETURN b[0] MOD 256 + b[1] MOD 256 * 256
	END Int;

	PROCEDURE Bytes (v: INTEGER): RaftCore.Bytes;
		VAR b: RaftCore.Bytes;
	BEGIN
		NEW(b, 2); b[0] := SHORT(SHORT(v MOD 256)); b[1] := SHORT(SHORT(v DIV 256)); RETURN b
	END Bytes;

	PROCEDURE (s: Sum) Apply (index: INTEGER; IN cmd: ARRAY OF BYTE; beg, len: INTEGER): RaftCore.Bytes;
	BEGIN
		INC(s.v, cmd[beg]); RETURN Bytes(s.v)
	END Apply;

	PROCEDURE (s: Sum) Snapshot (): RaftCore.Bytes;
	BEGIN
		RETURN Bytes(s.v)	(* Query по умолчанию отдаёт снимок — это и есть чтение суммы *)
	END Snapshot;

	PROCEDURE (s: Sum) Restore (b: RaftCore.Bytes);
	BEGIN
		IF b = NIL THEN s.v := 0 ELSE s.v := Int(b) END
	END Restore;

	PROCEDURE NewSum (): RaftSm.StateMachine;
		VAR s: Sum;
	BEGIN
		NEW(s); RETURN s
	END NewSum;

	PROCEDURE (x: Client) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Send DO c.Tell(m.to, m.m)
		| m: RaftNode.Reply DO reply := m
		ELSE
		END
	END Receive;

	PROCEDURE NewClient (): ActorCore.Actor;
		VAR x: Client;
	BEGIN
		NEW(x); RETURN x
	END NewClient;

	PROCEDURE Settle;
	BEGIN
		REPEAT IF ActorSim.Drain(100000) THEN END UNTIL ~ActorNetMem.Pump(net) & ActorSim.Idle()
	END Settle;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER; a: RaftNode.Node;
	BEGIN
		FOR i := 0 TO n - 1 DO
			a := cfgs[i].actor;
			IF nodes[i].up & (a # NIL) & (a.nd # NIL) & (a.nd.role = RaftCore.leader) THEN RETURN i END
		END;
		RETURN -1
	END Leader;

	PROCEDURE Tick;
	(* тик часов каждому живому узлу, затем сеть до тишины *)
		VAR i: INTEGER; t: RaftNode.Tick;
	BEGIN
		FOR i := 0 TO n - 1 DO IF nodes[i].up THEN NEW(t); ActorCore.Tell(own[i], t) END END;
		Settle
	END Tick;

	PROCEDURE Elect;
	BEGIN
		WHILE Leader() < 0 DO Tick END;
		Tick;	(* пустая запись нового срока зафиксирована *)
		Console.WriteStr("leader: node "); Console.WriteStr(nodes[Leader()].name); Console.WriteLn
	END Elect;

	PROCEDURE Ask (i: INTEGER; m: ActorNet.Msg);
	(* послать m узлу i от имени клиента и дождаться ответа *)
		VAR s: Send;
	BEGIN
		reply := NIL; NEW(s); s.to := refs[i]; s.m := m; ActorCore.Tell(client, s);
		Settle; WHILE reply = NIL DO Tick END
	END Ask;

	PROCEDURE Add (v: INTEGER);
	(* команда «прибавить v»: сначала узлу A; не-лидер называет лидера *)
		VAR p: RaftNode.Propose; i: INTEGER;
	BEGIN
		INC(seq); i := 0;
		IF ~nodes[0].up THEN i := 1 END;
		LOOP
			NEW(p); p.client := 1; p.seq := seq; NEW(p.body, 1); p.body[0] := SHORT(SHORT(v));
			Ask(i, p);
			IF reply.res = RaftNode.ok THEN EXIT END;
			Console.WriteStr("  node "); Console.WriteStr(nodes[i].name);
			Console.WriteStr(": not the leader, ask node "); Console.WriteStr(nodes[reply.leader].name); Console.WriteLn;
			i := reply.leader
		END;
		Console.WriteStr("add "); WriteInt(v); Console.WriteStr(" -> sum "); WriteInt(Int(reply.data));
		Console.WriteStr(" (entry "); WriteInt(reply.index); Console.WriteStr(")"); Console.WriteLn
	END Add;

	PROCEDURE Read;
		VAR q: RaftNode.Query;
	BEGIN
		NEW(q); Ask(Leader(), q);
		Console.WriteStr("read -> sum "); WriteInt(Int(reply.data)); Console.WriteLn
	END Read;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR l, i, k: INTEGER; cap: ARRAY RaftNode.capLen OF CHAR; kn: ActorNet.Node;
	BEGIN
		kn := ActorNet.New("K", net, 99); ActorNetMem.Add(net, kn);
		client := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClient), "K");
		FOR i := 0 TO n - 1 DO
			ActorNet.Export(nodes[i], own[i], cap); refs[i] := ActorNet.Import(kn, cap);
			FOR k := 0 TO n - 1 DO cfgs[k].peers[i] := cap$ END
		END;
		Settle;
		Elect;
		Add(5); Add(3);
		Read;
		l := Leader();
		Console.WriteStr("node "); Console.WriteStr(nodes[l].name); Console.WriteStr(" crashes"); Console.WriteLn;
		ActorNetMem.Crash(net, nodes[l].name); Settle;
		Elect;
		Add(4);
		Read;
		ActorNet.Up(nodes[l]); own[l] := RaftNode.Start(cfgs[l]); Settle;
		FOR k := 1 TO 3 DO Tick END;
		Console.WriteStr("node "); Console.WriteStr(nodes[l].name); Console.WriteStr(" is back: term ");
		WriteInt(cfgs[l].actor.nd.term); Console.WriteStr(", applied up to entry "); WriteInt(cfgs[l].actor.nd.applied);
		Console.WriteStr(", sum "); WriteInt(cfgs[l].actor.sm.sm(Sum).v); Console.WriteLn;
		finished := TRUE
	END Body;

	PROCEDURE Do*;
		VAR t: Driver; i: INTEGER; name: ARRAY 2 OF CHAR;
	BEGIN
		ActorSim.Install; net := ActorNetMem.New(1); seq := 0;
		FOR i := 0 TO n - 1 DO
			name[0] := CHR(ORD("A") + i); name[1] := 0X;
			nodes[i] := ActorNet.New(name, net, 11 * (i + 1)); ActorNetMem.Add(net, nodes[i]);
			cfgs[i] := RaftNode.NewConfig(i, n, nodes[i], RaftStorage.NewMem(), NewSum);
			cfgs[i].seed := 7;
			own[i] := RaftNode.Start(cfgs[i])
		END;
		finished := FALSE;
		NEW(t); Tasks.Start(t);
		WHILE ~finished DO Services.actionHook.Step END
	END Do;

END ObxRaft0.
```

In the console:

```sh
echo 'ObxRaft0.Do' | tools/run64.sh
```

The log shows:

```
leader: node A
add 5 -> sum 5 (entry 2)
add 3 -> sum 8 (entry 3)
read -> sum 8
node A crashes
leader: node B
add 4 -> sum 12 (entry 5)
read -> sum 12
node A is back: term 2, applied up to entry 5, sum 12
```

## What Happened

At first there is no leader. After ten-odd ticks the election timeout of one node ran out; it raised
the term and collected votes — that is A. The new leader at once wrote an empty entry of its term
(entry 1), so the client's commands went to entries 2 and 3. The answer to `Propose` came after two
nodes of three had stored the entry and the leader had applied it to the state machine: the answer
holds the new sum.

The read (`Query`) does not write to the log: the leader asked a majority whether it is still the
leader (ReadIndex) and answered with the sum once the state machine had caught up with what was
committed.

Node A crashed. After the timeout B and C elected a new leader — B. The client asked B out of habit;
B was the leader and answered. The sum 12 shows that nothing committed before the crash was lost.

A came back from its storage (`RaftNode.Start` after `ActorNet.Up`): the term and the log come from
the disk, the state machine starts empty. The leader sent the missing entry 5, A applied entries
1 .. 5 and got the same sum 12.

In this example we saw an election, a write answered after commit, a linearizable read, and a crashed
node catching up.
