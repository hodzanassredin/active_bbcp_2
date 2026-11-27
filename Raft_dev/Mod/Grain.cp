MODULE Raft_devGrain;
(** Виртуальные акторы с составом из службы RaftMembership.

	Три узла службы состава (RaftNode с автоматом RaftMembership) на сети
	ActorNetMem; разрывы задаёт транспорт Cut. Участники — узлы
	ActorGrain, каждый со своим RaftGrainMembership: вступление, аренда,
	чтение списка и ActorGrain.ApplyMembers. Журнал акторов — общий
	ActorJournal.NewMem: размещение и живость проверяются отдельно, а
	свойства RaftJournal проверяет Raft_devPersist.

	Проверки: все узлы видят один состав; ключ уходит с вышедшего или
	упавшего узла; операция исполняется ровно один раз (баланс и номера
	операций); чтение списка идёт через ReadIndex узла Raft (после
	выхода состав меняется). Порчи моста (mutNoJoin, mutIgnoreList,
	mutAllAlive, mutNoRenew) и ActorGrain (mutNoApply, mutKeepAlive)
	обязаны ломать те же проверки. *)

	IMPORT Int64, Services, Tasks, Strings, Stores,
		ActorCore, ActorJournal, ActorPersist, ActorNet, ActorNetMem, ActorSim, ActorGrain,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, RaftMembership, RaftGrainMembership, UnitAssert;

	CONST
		ns = 3;	(* узлов службы *)
		ngMax = 4;	(* участников *)
		lease = 500; renewEvery = 10; refreshEvery = 2;
		timeoutTicks = 100; giveUpTicks = 300;
		maxOps = 64;
		bufLen = 4096;

	TYPE
		Bytes = Core.Bytes;

		Cut = POINTER TO RECORD (ActorNet.Transport)
			net: ActorNetMem.Net;
			n: INTEGER;
			name: ARRAY 16 OF ActorCore.Name;
			blocked: ARRAY 16, 16 OF BOOLEAN
		END;

		Deposit = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		Reply = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; balance: INTEGER END;
		Send = POINTER TO RECORD (ActorCore.Msg) to: ActorCore.Ref; m: ActorNet.Msg END;

		Deposited = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER END;

		Account = POINTER TO RECORD (ActorGrain.Actor)
			balance: INTEGER; ops: ARRAY maxOps OF LONGINT; n: INTEGER
		END;

		Client = POINTER TO RECORD (ActorCore.Actor) END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		net: ActorNetMem.Net; cut: Cut;
		seed, x, clock: LONGINT;
		gen, nextClient: INTEGER;
		snd: ARRAY ns OF ActorNet.Node;
		sref: ARRAY ns OF ActorCore.Ref;
		scfg: ARRAY ns OF RaftNode.Config;
		upS: ARRAY ns OF BOOLEAN;
		mutCur, gmCur: SET;
		mutOk: BOOLEAN;
		caps: RaftNode.Caps;
		gnd: ARRAY ngMax OF ActorNet.Node;
		cl: ARRAY ngMax OF ActorGrain.Cluster;
		gms: ARRAY ngMax OF RaftMembership.Client;
		gcfg: ARRAY ngMax OF RaftGrainMembership.Config;
		upG: ARRAY ngMax OF BOOLEAN;
		ng, genG: INTEGER;
		journal: ActorJournal.Journal;
		clientRef: ActorCore.Ref;
		driverDone: BOOLEAN;
		acked, replies, lastBalance: INTEGER;
		opSum, opKey: ARRAY maxOps OF INTEGER;
		opDone: ARRAY maxOps OF BOOLEAN;
		lastOp: LONGINT;
		runOk: BOOLEAN;
		runMsg: ARRAY 256 OF CHAR;

	(* --- обвязка ------------------------------------------------------------- *)

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Services.Ticks();
		WHILE ~flag & (Services.Ticks() - t <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END Until;

	PROCEDURE Drive (body: PROCEDURE; ms: LONGINT): BOOLEAN;
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		RETURN Until(driverDone, ms)
	END Drive;

	PROCEDURE Rand (k: INTEGER): INTEGER;
	BEGIN
		x := Int64.Mul(x, 6364136223846793005); INC(x, 1442695040888963407);
		RETURN SHORT(ASH(x, -33) MOD k)
	END Rand;

	(* --- транспорт с разрывами ------------------------------------------------ *)

	PROCEDURE (t: Cut) Index (IN name: ARRAY OF CHAR): INTEGER, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < t.n) & (t.name[i] # name) DO INC(i) END;
		IF i = t.n THEN RETURN -1 ELSE RETURN i END
	END Index;

	PROCEDURE (t: Cut) Add (IN name: ARRAY OF CHAR), NEW;
	BEGIN
		IF t.Index(name) < 0 THEN t.name[t.n] := name$; INC(t.n) END
	END Add;

	PROCEDURE (t: Cut) Send (IN from, to: ARRAY OF CHAR; IN frame: ARRAY OF BYTE; len: INTEGER);
		VAR a, b: INTEGER;
	BEGIN
		a := t.Index(from); b := t.Index(to);
		IF (a >= 0) & (b >= 0) & t.blocked[a, b] THEN RETURN END;
		t.net.Send(from, to, frame, len)
	END Send;

	PROCEDURE (t: Cut) Block (a, b: INTEGER), NEW;
	BEGIN t.blocked[a, b] := TRUE; t.blocked[b, a] := TRUE END Block;

	PROCEDURE (t: Cut) Heal, NEW;
		VAR i, j: INTEGER;
	BEGIN FOR i := 0 TO 15 DO FOR j := 0 TO 15 DO t.blocked[i, j] := FALSE END END END Heal;

	(* --- счёт ---------------------------------------------------------------- *)

	PROCEDURE (m: Deposit) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.op); wr.WriteInt(m.sum) END Write;

	PROCEDURE (m: Deposit) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.op); rd.ReadInt(m.sum) END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.op); wr.WriteInt(m.balance) END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.op); rd.ReadInt(m.balance) END Read;

	PROCEDURE (e: Deposited) Externalize- (VAR wr: Stores.Writer);
	BEGIN e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum) END Externalize;

	PROCEDURE (e: Deposited) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum)
	END Internalize;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO
			INC(a.balance, e.sum);
			IF a.n < LEN(a.ops) THEN a.ops[a.n] := e.op; INC(a.n) END
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Seen (op: LONGINT): BOOLEAN, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < MIN(a.n, maxOps)) & (a.ops[i] # op) DO INC(i) END;
		RETURN i < MIN(a.n, maxOps)
	END Seen;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Deposited; r: Reply;
	BEGIN
		WITH m: Deposit DO
			IF (m.op # 0) & ~a.Seen(m.op) THEN
				NEW(e); e.op := m.op; e.sum := m.sum; a.Persist(c, e, m.op)
			END;
			NEW(r); r.op := m.op; r.balance := a.balance; c.Respond(r)
		ELSE
		END
	END Handle;

	PROCEDURE (a: Account) Snapshot (): Stores.Store;
	BEGIN RETURN a END Snapshot;

	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
		VAR i: INTEGER;
	BEGIN
		wr.WriteVersion(0); wr.WriteInt(a.balance); wr.WriteInt(a.n);
		FOR i := 0 TO maxOps - 1 DO wr.WriteLong(a.ops[i]) END
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v, i: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(a.balance); rd.ReadInt(a.n);
		FOR i := 0 TO maxOps - 1 DO rd.ReadLong(a.ops[i]) END
	END Internalize;

	PROCEDURE (a: Account) Restore (s: Stores.Store);
		VAR i: INTEGER;
	BEGIN
		WITH s: Account DO
			a.balance := s.balance; a.n := s.n; FOR i := 0 TO maxOps - 1 DO a.ops[i] := s.ops[i] END
		ELSE
		END
	END Restore;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN NEW(a); RETURN a END NewAccount;

	PROCEDURE (k: Client) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Send DO c.Tell(m.to, m.m)
		| m: Reply DO
			INC(replies); lastBalance := m.balance; lastOp := m.op;
			IF (m.op > 0) & (m.op < maxOps) THEN
				IF ~opDone[SHORT(m.op)] THEN opDone[SHORT(m.op)] := TRUE; INC(acked) END
			END
		ELSE
		END
	END Receive;

	PROCEDURE NewClient (): ActorCore.Actor;
		VAR a: Client;
	BEGIN NEW(a); RETURN a END NewClient;

	PROCEDURE NewDeposit (op: LONGINT; sum: INTEGER): Deposit;
		VAR d: Deposit;
	BEGIN NEW(d); d.op := op; d.sum := sum; RETURN d END NewDeposit;

	(* --- сеть и узлы --------------------------------------------------------- *)

	PROCEDURE NodeNameS (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d); s := "S" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i)
	END NodeNameS;

	PROCEDURE NodeNameG (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d); s := "G" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i)
	END NodeNameG;

	PROCEDURE Round;
		VAR i, k: INTEGER; t: RaftNode.Tick;
	BEGIN
		INC(clock); RaftGrainMembership.time := clock;
		FOR i := 0 TO ns - 1 DO
			IF upS[i] THEN NEW(t); ActorCore.Tell(sref[i], t) END
		END;
		FOR i := 0 TO ng - 1 DO
			IF upG[i] & (gcfg[i] # NIL) THEN RaftGrainMembership.Tick(gcfg[i]) END
		END;
		FOR k := 0 TO 63 DO
			IF ActorNetMem.Pump(net) THEN END;
			IF ActorSim.Step() THEN END
		END
	END Round;

	PROCEDURE Rounds (k: INTEGER);
		VAR i: INTEGER;
	BEGIN FOR i := 1 TO k DO Round END END Rounds;

	PROCEDURE SetupService;
		VAR i, j: INTEGER; name: ActorCore.Name; cap: ARRAY RaftNode.capLen OF CHAR; d: ARRAY 16 OF CHAR;
	BEGIN
		INC(gen); ActorSim.Install; net := ActorNetMem.New(seed); NEW(cut); cut.net := net;
		FOR i := 0 TO ns - 1 DO
			NodeNameS(i, name); snd[i] := ActorNet.New(name, cut, 1000 + i); ActorNetMem.Add(net, snd[i]);
			cut.Add(name); upS[i] := TRUE;
			scfg[i] := RaftNode.NewConfig(i, ns, snd[i], RaftStorage.NewMem(), RaftMembership.NewMachine);
			scfg[i].electionTicks := 10; scfg[i].seed := seed + i * 37 + gen; scfg[i].tickMs := 0
		END;
		FOR i := 0 TO ns - 1 DO sref[i] := RaftNode.Start(scfg[i]) END;
		FOR i := 0 TO ns - 1 DO ActorNet.Export(snd[i], sref[i], cap); caps[i] := cap$ END;
		FOR i := 0 TO ns - 1 DO
			FOR j := 0 TO ns - 1 DO IF i # j THEN scfg[i].peers[j] := caps[j]$ END END
		END;
		journal := ActorJournal.NewMem();
		Strings.IntToString(gen, d); clientRef := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClient), "K" + d$);
		Rounds(30)
	END SetupService;

	PROCEDURE AddGrain (i: INTEGER);
		VAR name: ActorCore.Name; key: ARRAY 2 OF BYTE;
	BEGIN
		IF upG[i] THEN RETURN END;
		NodeNameG(i, name); gnd[i] := ActorNet.New(name, cut, 2000 + i); ActorNetMem.Add(net, gnd[i]);
		cut.Add(name);
		cl[i] := ActorGrain.New(gnd[i], journal); ActorGrain.Register(cl[i], "acct", NewAccount);
		INC(nextClient); gms[i] := RaftMembership.New(gnd[i], caps, ns, nextClient);
		gms[i].tickMs := 0; gms[i].timeoutTicks := timeoutTicks; gms[i].giveUpTicks := giveUpTicks;
		key[0] := SHORT(SHORT(i + 1)); key[1] := 3;
		gcfg[i] := RaftGrainMembership.NewConfig(i, cl[i], gms[i], key, 2, lease, renewEvery, refreshEvery, 0);
		IF RaftGrainMembership.Start(gcfg[i]) # NIL THEN END; upG[i] := TRUE;
		IF i >= ng THEN ng := i + 1 END
	END AddGrain;

	PROCEDURE DownGrain (i: INTEGER);
	BEGIN
		IF upG[i] THEN
			IF gms[i] # NIL THEN RaftMembership.Close(gms[i]); gms[i] := NIL END;
			ActorNetMem.Crash(net, gnd[i].name); upG[i] := FALSE
		END
	END DownGrain;


	PROCEDURE AliveCount (i: INTEGER): INTEGER;
		VAR j, k: INTEGER;
	BEGIN
		k := 0;
		FOR j := 0 TO ng - 1 DO
			IF (gnd[j] # NIL) & ActorGrain.Alive(cl[i], gnd[j].name) THEN INC(k) END
		END;
		RETURN k
	END AliveCount;




	PROCEDURE AllAlive (k, skip, maxRounds: INTEGER): BOOLEAN;
		VAR i, rounds: INTEGER; ok: BOOLEAN;
	BEGIN
		rounds := 0;
		REPEAT
			Rounds(1); INC(rounds); ok := TRUE;
			FOR i := 0 TO ng - 1 DO
				IF upG[i] & (i # skip) & (AliveCount(i) # k) THEN ok := FALSE END
			END;
		UNTIL ok OR (rounds >= maxRounds);
		RETURN ok
	END AllAlive;

	PROCEDURE OwnerIs (i: INTEGER; IN key: ARRAY OF CHAR): BOOLEAN;
		VAR o: ActorCore.Name;
	BEGIN
		ActorGrain.Owner(cl[0], "acct", key, o); RETURN o = gnd[i].name
	END OwnerIs;

	PROCEDURE KeyOwnedBy (i: INTEGER; OUT key: ActorCore.Name);
		VAR k: INTEGER; d: ARRAY 16 OF CHAR; found: BOOLEAN;
	BEGIN
		k := 1; found := FALSE; key := "";
		WHILE (k < 1000) & ~found DO
			Strings.IntToString(k, d); key := d$; found := OwnerIs(i, key);
			IF ~found THEN INC(k) END
		END;
		IF ~found THEN key := "" END
	END KeyOwnedBy;

	PROCEDURE Ask (ref: ActorCore.Ref; op: LONGINT; sum: INTEGER);
		VAR s: Send;
	BEGIN
		NEW(s); s.to := ref; s.m := NewDeposit(op, sum); ActorCore.Tell(clientRef, s)
	END Ask;

	PROCEDURE Settle;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO Round END
	END Settle;

	PROCEDURE SetupMutations (s: SET);
	BEGIN
		ActorGrain.mutations := s
	END SetupMutations;

	PROCEDURE Fail (IN what: ARRAY OF CHAR);
	BEGIN
		IF runOk THEN runOk := FALSE; runMsg := what$ END
	END Fail;

	PROCEDURE Teardown;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO ns - 1 DO IF upS[i] THEN ActorNet.Down(snd[i]) END END;
		FOR i := 0 TO ng - 1 DO
			IF upG[i] THEN
				IF gms[i] # NIL THEN RaftMembership.Close(gms[i]) END;
				ActorNet.Down(gnd[i])
			END
		END;
		Rounds(20);
		IF ActorSim.Drain(100000) THEN END;
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {};
		IF clientRef # NIL THEN ActorCore.StopActor(clientRef); clientRef := NIL END;
		FOR i := 0 TO ngMax - 1 DO gcfg[i] := NIL; gms[i] := NIL; cl[i] := NIL; gnd[i] := NIL; upG[i] := FALSE END;
		ng := 0
	END Teardown;



	(* --- проверки ------------------------------------------------------------ *)

	PROCEDURE Reset;
		VAR i: INTEGER;
	BEGIN
		acked := 0; replies := 0; lastBalance := 0; lastOp := -1;
		FOR i := 0 TO maxOps - 1 DO opDone[i] := FALSE; opSum[i] := 0; opKey[i] := 0 END;
		runOk := TRUE; runMsg := ""
	END Reset;

	PROCEDURE TestRun (body: PROCEDURE; ms: LONGINT): BOOLEAN;
	BEGIN
		RETURN Drive(body, ms)
	END TestRun;

	PROCEDURE BasicBody;
		VAR key: ActorCore.Name; ref: ActorCore.Ref; owner: ActorCore.Name;
	BEGIN
		Reset; clock := 0; SetupService;
		AddGrain(0); AddGrain(1);
		IF ~AllAlive(2, -1, 1500) THEN Fail("не все видят состав из двух") END;
		KeyOwnedBy(1, key);
		IF key = "" THEN Fail("ключ узла 1 не найден") END;
		ref := ActorGrain.Ref(cl[0], "acct", key);
		Ask(ref, 1, 100); Settle; Ask(ref, 2, 50); Settle;
		IF lastBalance # 150 THEN Fail("баланс после двух вкладов") END;
		RaftGrainMembership.Leave(gcfg[1]);
		IF ~AllAlive(1, 1, 1500) THEN Fail("вышедший узел остался живым") END;
		IF ActorGrain.Alive(cl[0], gnd[1].name) THEN Fail("вышедший жив для соседа") END;
		ActorGrain.Owner(cl[0], "acct", key, owner);
		IF owner = gnd[1].name THEN Fail("ключ остался у вышедшего") END;
		Ask(ref, 3, 25); Settle;
		IF lastBalance # 175 THEN Fail("баланс после переезда") END;
		Teardown
	END BasicBody;


	PROCEDURE TestMoveOnLeave*;
	(* Состав из службы: ключ уходит с вышедшего узла, состояние сохраняется,
		каждая операция исполнена один раз. *)
	BEGIN
		seed := 1; genG := 0;
		UnitAssert.True(TestRun(BasicBody, 20000), "run");
		UnitAssert.True(runOk, runMsg)
	END TestMoveOnLeave;

	PROCEDURE CrashBody;
		VAR key: ActorCore.Name; ref: ActorCore.Ref; owner: ActorCore.Name;
	BEGIN
		Reset; clock := 0; SetupService;
		AddGrain(0); AddGrain(1);
		IF ~AllAlive(2, -1, 1500) THEN Fail("не все видят состав из двух") END;
		KeyOwnedBy(1, key);
		IF key = "" THEN Fail("ключ узла 1 не найден") END;
		ref := ActorGrain.Ref(cl[0], "acct", key);
		Ask(ref, 1, 100); Settle;
		IF lastBalance # 100 THEN Fail("баланс до падения") END;
		DownGrain(1);
		IF ~AllAlive(1, 1, 2000) THEN Fail("аренда упавшего не истекла") END;
		IF ActorGrain.Alive(cl[0], gnd[1].name) THEN Fail("упавший жив") END;
		ActorGrain.Owner(cl[0], "acct", key, owner);
		IF owner = gnd[1].name THEN Fail("ключ остался у упавшего") END;
		ActorGrain.Owner(cl[0], "acct", key, owner);
		IF owner = gnd[1].name THEN Fail("ключ остался у упавшего") END;
		Ask(ref, 2, 25); Settle;
		IF lastBalance # 125 THEN Fail("баланс после переезда") END;
		Teardown
	END CrashBody;

	PROCEDURE TestCrashOwner*;
	(* Упавший владелец: аренда истекает, ключ переезжает, состояние из
		журнала; вернувшийся узел снова в составе. *)
	BEGIN
		seed := 2; genG := 0;
		UnitAssert.True(TestRun(CrashBody, 30000), "run");
		UnitAssert.True(runOk, runMsg)
	END TestCrashOwner;

	PROCEDURE MutBody (mut, gm: SET; OUT ok: BOOLEAN);
	BEGIN
		Reset; clock := 0; RaftMembership.mutations := mut; RaftGrainMembership.mutations := gm;
		SetupService;
		AddGrain(0); AddGrain(1);
		ok := AllAlive(2, -1, 800);
		IF ok THEN	(* без продления аренды состав распадётся за 200 тиков *)
			Rounds(700);
			IF ~AllAlive(2, -1, 300) THEN ok := FALSE END
		END;
		IF ok THEN
			DownGrain(1);
			ok := AllAlive(1, 1, 1600) & ~ActorGrain.Alive(cl[0], gnd[1].name);
		END;
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {};
		Teardown
	END MutBody;

	PROCEDURE^ MutBodyWrap;

	PROCEDURE MutTest (mut, gm: SET; OUT ok: BOOLEAN);
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := MutBodyWrap; Tasks.Start(d);
		IF Until(driverDone, 30000) THEN END;
		ok := mutOk
	END MutTest;

	PROCEDURE MutBodyWrap;
		VAR ok: BOOLEAN;
	BEGIN
		MutBody(mutCur, gmCur, ok); mutOk := ok
	END MutBodyWrap;


	(* --- применение списка к ActorGrain ------------------------------------- *)

	PROCEDURE DirectCheck (mut: SET): BOOLEAN;
	(* Пост: список из двух живых применяется, мёртвый член выпадает,
			вернувшийся снова живой. FALSE — поведение нарушено. *)
		VAR c0, c1: ActorGrain.Cluster; cap0, cap1: ARRAY ActorGrain.capLen OF CHAR;
			j: ActorJournal.Journal; n0, n1: ActorNet.Node; infos: ARRAY 2 OF ActorGrain.MemberInfo;
			ok: BOOLEAN; genD: INTEGER; nm0, nm1: ActorCore.Name; d: ARRAY 16 OF CHAR;
	BEGIN
		INC(gen); genD := gen; Strings.IntToString(genD, d);
		ActorSim.Install; net := ActorNetMem.New(1);
		nm0 := "D0" + d$; nm1 := "D1" + d$;
		n0 := ActorNet.New(nm0, net, 100); n1 := ActorNet.New(nm1, net, 101);
		ActorNetMem.Add(net, n0); ActorNetMem.Add(net, n1);
		j := ActorJournal.NewMem();
		c0 := ActorGrain.New(n0, j); ActorGrain.Register(c0, "acct", NewAccount);
		c1 := ActorGrain.New(n1, j); ActorGrain.Register(c1, "acct", NewAccount);
		ActorGrain.HostCap(c0, cap0); ActorGrain.HostCap(c1, cap1);
		ActorGrain.mutations := mut;
		infos[0].id := 0; infos[0].cap := cap0$; infos[0].alive := TRUE;
		infos[1].id := 1; infos[1].cap := cap1$; infos[1].alive := TRUE;
		ActorGrain.ApplyMembers(c0, infos, 2);
		ok := (ActorGrain.Members(c0) = 2) & ActorGrain.Alive(c0, n1.name);
		infos[1].alive := FALSE;
		ActorGrain.ApplyMembers(c0, infos, 2);
		ok := ok & ~ActorGrain.Alive(c0, n1.name);
		infos[1].alive := TRUE;
		ActorGrain.ApplyMembers(c0, infos, 2);
		ok := ok & ActorGrain.Alive(c0, n1.name);
		ActorGrain.mutations := {};
		RETURN ok
	END DirectCheck;

	PROCEDURE TestApplyMembers*;
	(* Список службы: добавление, выпадение по живости, возврат. *)
	BEGIN
		UnitAssert.True(DirectCheck({}), "применение списка")
	END TestApplyMembers;

	PROCEDURE TestMutApplyNoApply*;
	BEGIN UnitAssert.True(~DirectCheck({ActorGrain.mutNoApply}), "порча mutNoApply не поймана") END TestMutApplyNoApply;

	PROCEDURE TestMutApplyKeepAlive*;
	BEGIN UnitAssert.True(~DirectCheck({ActorGrain.mutKeepAlive}), "порча mutKeepAlive не поймана") END TestMutApplyKeepAlive;

	PROCEDURE TestMutApplyNoRejoin*;
	BEGIN UnitAssert.True(~DirectCheck({ActorGrain.mutNoRejoin}), "порча mutNoRejoin не поймана") END TestMutApplyNoRejoin;


	(* --- прогоны от зерна: разрыв службы, падение и выход ------------------ *)

	PROCEDURE BlockSvc (a, b: INTEGER);
		VAR ia, ib: INTEGER; sa, sb: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(a, sa); Strings.IntToString(b, sb);
		ia := cut.Index("S" + sa$); ib := cut.Index("S" + sb$);
		IF (ia >= 0) & (ib >= 0) THEN cut.Block(ia, ib) END
	END BlockSvc;

	PROCEDURE SeedBody (sd: LONGINT);
		VAR key: ActorCore.Name; ref: ActorCore.Ref; r: INTEGER; expect: INTEGER;
	BEGIN
		Reset; seed := sd; clock := 0; x := Int64.Mul(sd, 2654435761); INC(x, 88172645463325252);
		SetupService; AddGrain(0); AddGrain(1);
		IF ~AllAlive(2, -1, 1500) THEN Fail("состав не сошёлся") END;
		KeyOwnedBy(1, key);
		IF key = "" THEN Fail("ключ узла 1 не найден") END;
		ref := ActorGrain.Ref(cl[0], "acct", key);
		Ask(ref, 1, 100); Settle; expect := 100;
		IF lastBalance # expect THEN Fail("вклад до сбоя") END;
		r := Rand(3);
		IF r = 0 THEN	(* разрыв: меньшинство службы не слышит большинство *)
			BlockSvc(0, 1); BlockSvc(0, 2);
			Rounds(150);
			Ask(ref, 2, 1); Settle; INC(expect);
			IF lastBalance # expect THEN Fail("вклад в разрыве") END;
			cut.Heal; Rounds(100);
			Ask(ref, 3, 1); Settle; INC(expect);
			IF lastBalance # expect THEN Fail("вклад после лечения") END
		ELSIF r = 1 THEN	(* падение владельца: аренда истекает *)
			DownGrain(1);
			IF ~AllAlive(1, 1, 2000) THEN Fail("аренда упавшего не истекла") END;
			Ask(ref, 2, 1); Settle; INC(expect);
			IF lastBalance # expect THEN Fail("вклад после падения") END
		ELSE	(* выход владельца *)
			RaftGrainMembership.Leave(gcfg[1]);
			IF ~AllAlive(1, 1, 1500) THEN Fail("вышедший остался") END;
			Ask(ref, 2, 1); Settle; INC(expect);
			IF lastBalance # expect THEN Fail("вклад после выхода") END
		END;
		Teardown
	END SeedBody;

	PROCEDURE SeedsBody;
		VAR sd: LONGINT;
	BEGIN
		sd := 1;
		WHILE (sd <= 12) & runOk DO SeedBody(sd); INC(sd) END
	END SeedsBody;

	PROCEDURE TestSeeds*;
	(* 12 зёрен: разрыв службы (меньшинство), падение владельца, выход
		владельца; вклад исполняется ровно один раз. *)
		VAR d: Driver; t: LONGINT;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := SeedsBody; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 120000) DO Services.actionHook.Step END;
		UnitAssert.True(driverDone, "прогон завис");
		UnitAssert.True(runOk, runMsg)
	END TestSeeds;

	PROCEDURE TestBaseline*;
	(* Без порчи: состав сходится, упавший выпадает. *)
		VAR ok: BOOLEAN;
	BEGIN
		mutCur := {}; gmCur := {}; MutTest(mutCur, gmCur, ok);
		UnitAssert.True(ok, "исходный прогон не прошёл")
	END TestBaseline;

	PROCEDURE TestMutNoJoin*;
	(* Порча: мост не начинает вступление — составе не сходится. *)
		VAR ok: BOOLEAN;
	BEGIN
		mutCur := {}; gmCur := {RaftGrainMembership.mutNoJoin};
		MutTest(mutCur, gmCur, ok);
		UnitAssert.True(~ok, "порча mutNoJoin не поймана")
	END TestMutNoJoin;

	PROCEDURE TestMutIgnoreList*;
	(* Порча: ответ списка не применяется — узлы не видят друг друга. *)
		VAR ok: BOOLEAN;
	BEGIN
		mutCur := {}; gmCur := {RaftGrainMembership.mutIgnoreList};
		MutTest(mutCur, gmCur, ok);
		UnitAssert.True(~ok, "порча mutIgnoreList не поймана")
	END TestMutIgnoreList;


	PROCEDURE TestMutNoRenew*;
	(* Порча: аренда не продлевается — все выпадают. *)
		VAR ok: BOOLEAN;
	BEGIN
		mutCur := {}; gmCur := {RaftGrainMembership.mutNoRenew};
		MutTest(mutCur, gmCur, ok);
		UnitAssert.True(~ok, "порча mutNoRenew не поймана")
	END TestMutNoRenew;


	PROCEDURE ExpiredBody;
	(* Участник с истёкшей арендой не должен получать ключи: его ключ
		переезжает к живому, вклад доходит до него. *)
		VAR key, owner: ActorCore.Name;
	BEGIN
		Reset; clock := 0; SetupService; AddGrain(0); AddGrain(1);
		IF ~AllAlive(2, -1, 1500) THEN Fail("состав из двух не сошёлся") END;
		KeyOwnedBy(1, key);
		IF key = "" THEN Fail("ключ узла 1 не найден") END;
		DownGrain(1);
		IF ~AllAlive(1, 1, 2000) THEN Fail("аренда упавшего не истекла") END;
		IF ActorGrain.Alive(cl[0], gnd[1].name) THEN Fail("участник с истёкшей арендой считается живым") END;
		ActorGrain.Owner(cl[0], "acct", key, owner);
		IF owner = gnd[1].name THEN Fail("ключ остался у участника с истёкшей арендой") END;
		Ask(ActorGrain.Ref(cl[0], "acct", key), 1, 100); Settle;
		IF lastBalance # 100 THEN Fail("вклад не дошёл до живого владельца") END;
		Teardown
	END ExpiredBody;

	PROCEDURE TestExpiredLease*;
	(* Аренда истекла: ключ уходит, вклад исполняется живым узлом. *)
	BEGIN
		seed := 3; genG := 0;
		UnitAssert.True(TestRun(ExpiredBody, 40000), "run");
		UnitAssert.True(runOk, runMsg)
	END TestExpiredLease;

	PROCEDURE TestMutAllAlive*;
	(* Порча: мост считает всех живыми — ключ остаётся у участника с
		истёкшей арендой, вклад до него не доходит. *)
	BEGIN
		seed := 4; genG := 0;
		RaftGrainMembership.mutations := {RaftGrainMembership.mutAllAlive};
		UnitAssert.True(TestRun(ExpiredBody, 40000), "run");
		UnitAssert.True(~runOk, "порча mutAllAlive не поймана")
	END TestMutAllAlive;

	PROCEDURE TestMutNoExpire*;
	(* Порча службы: живость не смотрит на аренду — мёртвый владелец остаётся живым. *)
	BEGIN
		seed := 5; genG := 0;
		RaftMembership.mutations := {RaftMembership.mutNoExpire};
		UnitAssert.True(TestRun(ExpiredBody, 40000), "run");
		UnitAssert.True(~runOk, "порча mutNoExpire не поймана")
	END TestMutNoExpire;

	PROCEDURE TestMutGrainNoApply*;
	(* Порча ActorGrain: список не применяется. *)
		VAR ok: BOOLEAN;
	BEGIN
		mutCur := {}; gmCur := {};
		SetupMutations({ActorGrain.mutNoApply});
		MutTest(mutCur, gmCur, ok);
		SetupMutations({});
		UnitAssert.True(~ok, "порча mutNoApply не поймана")
	END TestMutGrainNoApply;



END Raft_devGrain.
