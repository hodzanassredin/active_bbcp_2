# Overview by Example: ObxActor6

Two nodes in one process: `Ask` across a node, refusal of a forged address, node loss.

The nodes `shop` and `bank` stand on the network in memory `ActorNetMem.NewAuto()`: frames between
them are encoded and decoded as between processes. The teller is the root actor of node `bank`. The
example belongs to the guide `Actor/Docu/Actors.md` (section 10).

```
MODULE ObxActor6;
	IMPORT Services, Tasks, Console, Strings, Stores,
		ActorCore, ActorRun, ActorNet, ActorNetMem;

	TYPE
		Query = POINTER TO RECORD (ActorNet.Msg) END;
		Amount = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;

		Teller = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		net: ActorNetMem.Net;
		shop, bank: ActorNet.Node;
		teller: ActorCore.Ref;	(* адрес на узле shop *)
		lost: BOOLEAN;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* удалённое письмо: поля пишет Write, читает Read *)
	PROCEDURE (m: Amount) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(m.n)
	END Write;

	PROCEDURE (m: Amount) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(m.n)
	END Read;

	PROCEDURE (a: Teller) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: Amount;
	BEGIN
		WITH m: Query DO NEW(r); r.n := 100; c.Respond(r)
		ELSE
		END
	END Receive;

	PROCEDURE NewTeller (): ActorCore.Actor;
		VAR a: Teller;
	BEGIN
		NEW(a); RETURN a
	END NewTeller;

	PROCEDURE (w: Watcher) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO c.Watch(teller)
		| m: ActorCore.Terminated DO
			Console.WriteStr("watcher: teller on node bank is gone"); Console.WriteLn; lost := TRUE
		ELSE
		END
	END Receive;

	PROCEDURE NewWatcher (): ActorCore.Actor;
		VAR w: Watcher;
	BEGIN
		NEW(w); RETURN w
	END NewWatcher;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR cap, forged: ARRAY 256 OF CHAR; r: ActorCore.Msg; bad: ActorCore.Ref; q: Query; i: INTEGER;
	BEGIN
		(* узел bank выдаёт пропуск к своему кассиру; shop знает только пропуск *)
		ActorNet.Export(bank, ActorCore.dir.Lookup(ActorCore.node, "bank"), cap);
		teller := ActorNet.Import(shop, cap);
		NEW(q);
		IF ActorCore.Ask(teller, q, 1000, r) = ActorCore.ok THEN
			Console.WriteStr("bank answers: "); WriteInt(r(Amount).n); Console.WriteLn
		END;
		(* подделка: имя то же, метка другая *)
		forged := cap$; i := LEN(forged$) - 1;
		IF forged[i] = "0" THEN forged[i] := "1" ELSE forged[i] := "0" END;
		bad := ActorNet.Import(shop, forged);
		IF ActorCore.Ask(bad, q, 200, r) = ActorCore.timedOut THEN
			Console.WriteStr("forged address: no answer, refused frames "); WriteInt(bank.refused); Console.WriteLn
		END;
		(* наблюдатель на shop; узел bank падает *)
		ASSERT(ActorCore.Spawn(NIL, ActorCore.NewProps(NewWatcher), "shop") # NIL, 100);
		Tasks.Sleep(20);
		ActorNetMem.Crash(net, "bank");
		WHILE ~lost DO Tasks.Sleep(1) END;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		lost := FALSE;
		net := ActorNetMem.NewAuto();
		shop := ActorNet.New("shop", net, 1234); ActorNetMem.Add(net, shop);
		bank := ActorNet.New("bank", net, 5678); ActorNetMem.Add(net, bank);
		ASSERT(ActorCore.Spawn(NIL, ActorCore.NewProps(NewTeller), "bank") # NIL, 100);
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor6.
```

In the console:

```sh
echo 'ObxActor6.Do' | tools/run64.sh
```

The log shows:

```
bank answers: 100
forged address: no answer, refused frames 2
watcher: teller on node bank is gone
```

## What Happened

Node `bank` handed out a capability to the teller (`Export`): the node name, the actor name and a
mark from its secret. Node `shop` knows only the capability (`Import`) and asks for the balance with
a plain `Ask`; the letters `Query` and `Amount` extend `ActorNet.Msg`, and `Amount` writes its field
in `Write` and reads it in `Read`.

The forged capability is the same name with another mark. Node `bank` refuses both frames that came
to it: the letter `Query` and the watch request that `Ask` sends for the reply address; `Ask` ends
with a time-out, and the teller executes nothing.

A watcher on node `shop` is subscribed to the teller. `ActorNetMem.Crash` brings node `bank` down,
the network tells node `shop` about the loss, and the watcher receives `Terminated`.

In this example we saw a capability address, `Ask` across a node and the node-loss signal.
