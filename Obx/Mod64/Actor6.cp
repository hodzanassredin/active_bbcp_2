MODULE ObxActor6;
	IMPORT Services, Tasks, Console, Strings, Stores,
		ActorCore, ActorRun, ActorNet, ActorNetMem;

	TYPE
		Query = POINTER TO RECORD (ActorNet.Msg) END;
		Amount = POINTER TO RECORD (ActorNet.Msg) n: INTEGER END;

		Teller = POINTER TO RECORD (ActorCore.Actor) END;
		Watcher = POINTER TO RECORD (ActorCore.Actor) END;
		Driver = POINTER TO RECORD (Tasks.Task) END;

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

	PROCEDURE (t: Driver) Run;
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
		IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewWatcher), "shop") = NIL THEN END;
		Tasks.Sleep(20);
		ActorNetMem.Crash(net, "bank");
		WHILE ~lost DO Tasks.Sleep(1) END;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		lost := FALSE;
		net := ActorNetMem.NewAuto();
		shop := ActorNet.New("shop", net, 1234); ActorNetMem.Add(net, shop);
		bank := ActorNet.New("bank", net, 5678); ActorNetMem.Add(net, bank);
		IF ActorCore.Spawn(NIL, ActorCore.NewProps(NewTeller), "bank") = NIL THEN END;
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor6.
