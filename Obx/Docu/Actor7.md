# Overview by Example: ObxActor7

A saga: a transfer between nodes with the coordinator crashing in the middle.

Accounts on nodes A and B and the transfer coordinator on node C are persistent actors with journals
in memory; a journal survives a node crash, as a disk does. The network loses 30 % of frames, lots
come from seed 7, and `ActorSim` serves the mailboxes, so the run repeats step by step. The example
belongs to the guide `Actor/Docu/Actors.md` (section 10).

```
MODULE ObxActor7;
	IMPORT Tasks, Services, Console, Strings, Stores,
		ActorCore, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorSim;

	CONST
		seed = 7;
		begun = 0; withdrawn = 1; done = 2; refunding = 3; refunded = 4; failed = 5;

	TYPE
		Msg = ActorCore.Msg;

		(* между узлами *)
		Move = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		Reply = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; ok: BOOLEAN END;
		(* на узле координатора *)
		Begin = POINTER TO RECORD (Msg) sum: INTEGER END;
		Tick = POINTER TO RECORD (Msg) END;
		(* события *)
		Moved = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER; ok: BOOLEAN END;
		Step = POINTER TO RECORD (ActorPersist.Event) sum, state: INTEGER END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance: INTEGER; ops: ARRAY 8 OF LONGINT; oks: ARRAY 8 OF BOOLEAN; n: INTEGER
		END;
		Coord = POINTER TO RECORD (ActorPersist.Actor)
			started: BOOLEAN; sum, state: INTEGER; from, to: ActorCore.Ref
		END;

		Sum = POINTER TO RECORD (ActorPersist.Visitor) total: INTEGER END;
		Driver = POINTER TO RECORD (Tasks.Task) END;

	VAR
		net: ActorNetMem.Net;
		na, nb, nc: ActorNet.Node;
		ja, jb, jc: ActorJournal.Journal;
		capA, capB: ARRAY 256 OF CHAR;
		state: INTEGER;
		finished: BOOLEAN;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	PROCEDURE StateName (s: INTEGER);
	BEGIN
		CASE s OF
		| begun: Console.WriteStr("begun") | withdrawn: Console.WriteStr("withdrawn")
		| done: Console.WriteStr("done") | refunding: Console.WriteStr("refunding")
		| refunded: Console.WriteStr("refunded") | failed: Console.WriteStr("failed")
		END
	END StateName;

	(* --- письма и события ------------------------------------------------- *)

	PROCEDURE (m: Move) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteInt(m.sum)
	END Write;

	PROCEDURE (m: Move) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadInt(m.sum)
	END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteLong(m.op); wr.WriteBool(m.ok)
	END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadLong(m.op); rd.ReadBool(m.ok)
	END Read;

	PROCEDURE (e: Moved) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum); wr.WriteBool(e.ok)
	END Externalize;

	PROCEDURE (e: Moved) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum); rd.ReadBool(e.ok)
	END Internalize;

	PROCEDURE (e: Step) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum); wr.WriteInt(e.state)
	END Externalize;

	PROCEDURE (e: Step) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.sum); rd.ReadInt(e.state)
	END Internalize;

	PROCEDURE (v: Sum) Visit (e: ActorPersist.Event);
	BEGIN
		WITH e: Moved DO IF e.ok THEN INC(v.total, e.sum) END ELSE END
	END Visit;

	(* --- счёт: повтор операции получает прежний итог ------------------------- *)

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Moved DO
			IF e.ok THEN INC(a.balance, e.sum) END;
			a.ops[a.n] := e.op; a.oks[a.n] := e.ok; INC(a.n)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR e: Moved; r: Reply; i: INTEGER;
	BEGIN
		WITH m: Move DO
			i := 0; WHILE (i < a.n) & (a.ops[i] # m.op) DO INC(i) END;
			IF i = a.n THEN
				NEW(e); e.op := m.op; e.sum := m.sum; e.ok := a.balance + m.sum >= 0;
				a.Persist(c, e, m.op)
			END;
			NEW(r); r.op := m.op; r.ok := a.oks[i]; c.Respond(r)
		ELSE
		END
	END Handle;

	PROCEDURE NewA (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := 1000; a.journal := ja; RETURN a
	END NewA;

	PROCEDURE NewB (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := 1000; a.journal := jb; RETURN a
	END NewB;

	(* --- координатор: шаг саги — событие; текущий шаг повторяется по Tick ---- *)

	PROCEDURE (k: Coord) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Step DO k.started := TRUE; k.sum := e.sum; k.state := e.state; state := e.state
		ELSE
		END
	END Apply;

	PROCEDURE (k: Coord) Command (c: ActorCore.Context), NEW;
		VAR m: Move;
	BEGIN
		NEW(m);
		CASE k.state OF
		| begun: m.op := 1; m.sum := -k.sum; c.Tell(k.from, m)
		| withdrawn: m.op := 2; m.sum := k.sum; c.Tell(k.to, m)
		| refunding: m.op := 3; m.sum := k.sum; c.Tell(k.from, m)
		ELSE
		END
	END Command;

	PROCEDURE (k: Coord) Go (c: ActorCore.Context; s: INTEGER), NEW;
		VAR e: Step;
	BEGIN
		NEW(e); e.sum := k.sum; e.state := s; k.Persist(c, e, s);
		Console.WriteStr("  coordinator: "); StateName(s); Console.WriteLn;
		k.Command(c)
	END Go;

	PROCEDURE (k: Coord) Handle (c: ActorCore.Context; m: Msg);
	BEGIN
		WITH m: Begin DO IF ~k.started THEN k.sum := m.sum; k.Go(c, begun) END
		| m: Tick DO k.Command(c)
		| m: Reply DO
			IF (m.op = 1) & (k.state = begun) THEN
				IF m.ok THEN k.Go(c, withdrawn) ELSE k.Go(c, failed) END
			ELSIF (m.op = 2) & (k.state = withdrawn) THEN
				IF m.ok THEN k.Go(c, done) ELSE k.Go(c, refunding) END
			ELSIF (m.op = 3) & (k.state = refunding) & m.ok THEN k.Go(c, refunded)
			END
		ELSE
		END
	END Handle;

	PROCEDURE NewCoord (): ActorCore.Actor;
		VAR k: Coord;
	BEGIN
		NEW(k); k.journal := jc;
		k.from := ActorNet.Import(nc, capA); k.to := ActorNet.Import(nc, capB);
		RETURN k
	END NewCoord;

	(* --- водитель: сеть теряет кадры; узел C падает посреди саги --------------- *)

	PROCEDURE Spin (max: INTEGER);
	(* max шагов: письма ящиков и кадры сети по очереди *)
	BEGIN
		WHILE max > 0 DO
			IF ActorSim.Step() THEN END;
			IF ActorNetMem.Pump(net) THEN END;
			DEC(max)
		END
	END Spin;

	PROCEDURE Balance (j: ActorJournal.Journal; IN name: ARRAY OF CHAR): INTEGER;
		VAR v: Sum; last: LONGINT; res: INTEGER;
	BEGIN
		NEW(v); ActorPersist.Read(j, name, 1, v, last, res); RETURN 1000 + v.total
	END Balance;

	PROCEDURE (t: Driver) Run;
		VAR coord: ActorCore.Ref; b: Begin; tick: Tick; round: INTEGER;
	BEGIN
		ActorNet.Export(na, ActorCore.Spawn(NIL, ActorCore.NewProps(NewA), "A"), capA);
		ActorNet.Export(nb, ActorCore.Spawn(NIL, ActorCore.NewProps(NewB), "B"), capB);
		coord := ActorCore.Spawn(NIL, ActorCore.NewProps(NewCoord), "C");
		NEW(b); b.sum := 300; ActorCore.Tell(coord, b);
		NEW(tick); round := 0;
		WHILE ~((state = done) OR (state = refunded) OR (state = failed)) DO
			INC(round); Spin(4);
			IF round MOD 4 = 0 THEN ActorCore.Tell(coord, tick) END;	(* повтор текущего шага *)
			IF (state = withdrawn) & nc.up & (nc.incarnation = 1) THEN
				Console.WriteStr("node C crashes"); Console.WriteLn;
				ActorNetMem.Crash(net, "C"); Spin(10);
				ActorNet.Up(nc);
				Console.WriteStr("node C restarts, coordinator replays its journal"); Console.WriteLn;
				coord := ActorCore.Spawn(NIL, ActorCore.NewProps(NewCoord), "C")
			END
		END;
		Spin(50);
		Console.WriteStr("A = "); WriteInt(Balance(ja, "A"));
		Console.WriteStr(", B = "); WriteInt(Balance(jb, "B"));
		Console.WriteStr(", lost frames "); WriteInt(net.lost); Console.WriteLn;
		finished := TRUE
	END Run;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		ActorSim.Install;
		net := ActorNetMem.New(seed); net.plan.loss := 300;	(* 30% кадров теряется *)
		ja := ActorJournal.NewMem(); jb := ActorJournal.NewMem(); jc := ActorJournal.NewMem();
		na := ActorNet.New("A", net, 11); ActorNetMem.Add(net, na);
		nb := ActorNet.New("B", net, 22); ActorNetMem.Add(net, nb);
		nc := ActorNet.New("C", net, 33); ActorNetMem.Add(net, nc);
		state := -1; finished := FALSE;
		Console.WriteStr("transfer 300 from A to B"); Console.WriteLn;
		NEW(t); Tasks.Start(t);
		WHILE ~finished DO Services.actionHook.Step END
	END Do;

END ObxActor7.
```

In the console:

```sh
echo 'ObxActor7.Do' | tools/run64.sh
```

The log shows:

```
transfer 300 from A to B
  coordinator: begun
  coordinator: withdrawn
node C crashes
node C restarts, coordinator replays its journal
  coordinator: done
A = 700, B = 1300, lost frames 1
```

## What Happened

The coordinator recorded the step `begun` and sent account A the command "withdraw 300" with
operation number 1. The account recorded an event and replied. The coordinator recorded `withdrawn`
and sent account B "deposit 300".

At this point node C crashed: the command or the reply may have been lost. The node came up, the
coordinator read its journal and was again at the step `withdrawn`. On a tick it repeats the command
of the current step; if account B has already executed operation 2, it answers with the old outcome
and does not deposit twice. The sum of the accounts stayed 2000.

Had account B refused the deposit, the coordinator would move to `refunding` and return the money to
A with operation 3, by the same scheme: record the step, send, repeat until answered.

In this example we saw a saga with a persistent coordinator and an idempotent retry.
