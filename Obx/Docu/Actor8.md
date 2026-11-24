# Overview by Example: ObxActor8

An account as a virtual actor on two nodes: access by account number, a node crash, continuation on
the other node.

Nodes A and B stand on a network in memory, each with an `ActorGrain` cluster with the kind
`"acct"`; the journal is shared. The client addresses the account by number `"42"` through the
cluster of node B. The example belongs to the guide `Actor/Docu/Actors.md` (section 11).

```
MODULE ObxActor8;
	IMPORT Tasks, Services, Console, Strings, Stores,
		ActorCore, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorSim, ActorGrain;

	TYPE
		Msg = ActorCore.Msg;

		(* между узлами *)
		Deposit = POINTER TO RECORD (ActorNet.Msg) sum: INTEGER END;
		Balance = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;
		(* местное: клиенту — кому и что послать *)
		Send = POINTER TO RECORD (Msg) to: ActorCore.Ref; m: ActorNet.Msg END;

		Deposited = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;

		Account = POINTER TO RECORD (ActorGrain.Actor) balance: INTEGER END;
		Client = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Task) END;

	VAR
		net: ActorNetMem.Net;
		a, b: ActorNet.Node;
		ca, cb: ActorGrain.Cluster;
		client: ActorCore.Ref;
		finished: BOOLEAN;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	PROCEDURE (m: Deposit) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Deposit) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Balance) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: Balance) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
	END Read;

	PROCEDURE (e: Deposited) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum)
	END Externalize;

	PROCEDURE (e: Deposited) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.sum)
	END Internalize;

	(* счёт: виртуальный актор, вид "acct", ключ — номер счёта *)
	PROCEDURE (x: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(x.balance, e.sum) ELSE END
	END Apply;

	PROCEDURE (x: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR e: Deposited; r: Balance; node: ARRAY 2 OF CHAR;
	BEGIN
		WITH m: Deposit DO
			NEW(e); e.sum := m.sum; x.Persist(c, e, 0);
			NEW(r); r.n := x.balance; c.Respond(r)
		| m: ActorPersist.ReplayComplete DO
			Console.WriteStr("  account "); Console.WriteStr(x.key);
			node[0] := c.name[0]; node[1] := 0X;	(* имя ящика — "узел/acct.ключ" *)
			Console.WriteStr(" is up on node "); Console.WriteStr(node); Console.WriteLn
		ELSE
		END
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR x: Account;
	BEGIN
		NEW(x); RETURN x
	END NewAccount;

	PROCEDURE (x: Client) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Send DO c.Tell(m.to, m.m)
		| m: Balance DO Console.WriteStr("  balance "); WriteInt(m.n); Console.WriteLn
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

	PROCEDURE Pay (to: ActorCore.Ref; sum: INTEGER);
		VAR s: Send; d: Deposit;
	BEGIN
		Console.WriteStr("deposit "); WriteInt(sum); Console.WriteLn;
		NEW(d); d.sum := sum; NEW(s); s.to := to; s.m := d; ActorCore.Tell(client, s); Settle
	END Pay;

	PROCEDURE (t: Driver) Run;
		VAR cap: ARRAY 256 OF CHAR; acct: ActorCore.Ref; owner: ActorCore.Name;
	BEGIN
		ActorGrain.HostCap(ca, cap); ActorGrain.Member(cb, cap);
		ActorGrain.HostCap(cb, cap); ActorGrain.Member(ca, cap);
		client := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClient), "client");
		Settle;
		acct := ActorGrain.Ref(cb, "acct", "42");	(* адрес — номер счёта, не узел; клиент обращается через B *)
		Pay(acct, 100); Pay(acct, 50);
		ActorGrain.Owner(cb, "acct", "42", owner);
		Console.WriteStr("node "); Console.WriteStr(owner); Console.WriteStr(" crashes"); Console.WriteLn;
		ActorNetMem.Crash(net, owner); Settle;
		Pay(acct, 25);
		finished := TRUE
	END Run;

	PROCEDURE Do*;
		VAR t: Driver; j: ActorJournal.Journal;
	BEGIN
		ActorSim.Install; net := ActorNetMem.New(1);
		j := ActorJournal.NewMem();	(* журнал общий для узлов, как база *)
		a := ActorNet.New("A", net, 11); ActorNetMem.Add(net, a);
		b := ActorNet.New("B", net, 22); ActorNetMem.Add(net, b);
		ca := ActorGrain.New(a, j); ActorGrain.Register(ca, "acct", NewAccount);
		cb := ActorGrain.New(b, j); ActorGrain.Register(cb, "acct", NewAccount);
		finished := FALSE;
		NEW(t); Tasks.Start(t);
		WHILE ~finished DO Services.actionHook.Step END
	END Do;

END ObxActor8.
```

In the console:

```sh
echo 'ObxActor8.Do' | tools/run64.sh
```

The log shows:

```
deposit 100
  account 42 is up on node A
  balance 100
deposit 50
  balance 150
node A crashes
deposit 25
  account 42 is up on node B
  balance 175
```

## What Happened

By rendezvous hashing the owner of key `"42"` is node A. The first letter came to host A in an
envelope, and the host brought up the activation `A/acct.42`; the stream `acct.42` was empty, so the
account started at zero. The second letter found the activation active.

Node A crashed. Host B received `Terminated` of host A with reason `byNodeLoss` and considers A dead;
now B owns the key. The third letter brought up the activation `B/acct.42` from the same stream
`acct.42`: the balance 150 was restored, and the deposit made it 175.

In this example we saw an address by kind and key, activation from the journal, and a key moving when
its node is lost.
