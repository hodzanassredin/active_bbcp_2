# Обзор на примерах: ObxRaft0

Три узла Raft в памяти: выборы, запись, чтение, гибель лидера и возвращение узла.

Узлы A, B и C — узлы-акторы `RaftNode` на сети `ActorNetMem`; у каждого свой автомат «сумма» и своё
стойкое хранилище в памяти (`RaftStorage.NewMem`). Клиент — актор на отдельном узле K. Исполнитель —
`ActorSim`, тики часов шлёт сам пример, поэтому прогон повторяется знак в знак. Пример относится к
руководству `Raft/Docu/ru/Raft.md` (раздел «Узел-актор»).

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
		Driver = POINTER TO RECORD (Tasks.Task) END;

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

	PROCEDURE (t: Driver) Run;
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
	END Run;

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

В консоли:

```sh
echo 'ObxRaft0.Do' | tools/run64.sh
```

В журнале появится:

```
leader: node A
add 5 -> sum 5 (entry 2)
add 3 -> sum 8 (entry 3)
read -> sum 8
node A crashes
leader: node C
  node B: not the leader, ask node C
add 4 -> sum 12 (entry 5)
read -> sum 12
node A is back: term 2, applied up to entry 5, sum 12
```

## Что произошло

Сначала лидера нет. После десяти с лишним тиков у одного из узлов кончился тайм-аут выборов, он
поднял срок и собрал голоса — это A. Новый лидер сразу записал пустую запись своего срока (запись 1),
поэтому команды клиента легли под номерами 2 и 3. Ответ на `Propose` пришёл после того, как запись
сохранили два узла из трёх и лидер применил её к автомату: в ответе — новая сумма.

Чтение (`Query`) не пишет в журнал: лидер спросил у большинства, лидер ли он ещё (ReadIndex), и
ответил суммой после того, как автомат догнал зафиксированное.

Узел A упал. Через тайм-аут B и C выбрали нового лидера — C. Клиент по привычке спросил B; B ответил
«не лидер» и назвал C, клиент повторил команду у C. Сумма 12 — зафиксированное до падения не
потерялось.

A поднялся из своего хранилища (`RaftNode.Start` после `ActorNet.Up`): срок и журнал — с диска, автомат —
пустой. Лидер дослал недостающую запись 5, A применил записи 1 .. 5 и получил ту же сумму 12.

В этом примере мы увидели выборы, запись с ответом после фиксации, линеаризуемое чтение, подсказку
лидера и догон упавшего узла.
