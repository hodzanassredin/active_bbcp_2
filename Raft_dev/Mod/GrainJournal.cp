MODULE Raft_devGrainJournal;
(** Виртуальные акторы ActorGrain на журнале RaftJournal под ActorSim.

	Кластер журнала из трёх узлов RaftNode с автоматом RaftJournal и
	служба состава из трёх узлов с автоматом RaftMembership живут на одной
	ActorNetMem; разрывы задаёт транспорт Cut. Участники гранул (G0..G3)
	берут состав из службы (RaftGrainMembership) и пишут через свой
	клиент RaftJournal. Клиенты — акторы в процессе: один пишущий на ключ
	(операции по порядку, повтор неподтверждённой с тем же номером) и
	один читающий (сверяет ответ с последней подтверждённой записью).

	Эталон журнала — тот же автомат RaftJournal, применённый к решённым
	записям Raft по порядку индексов. Инварианты каждого шага и в конце:
	- каждая операция записана ровно один раз: в потоке нет двух событий
	  с одним номером, подтверждённая клиенту операция есть в эталоне;
	- подтверждённые записи ключа — одна цепь: prev события равен его
	  номеру минус один; две копии не пишут мимо друг друга;
	- ни один ответ не вернул состояние старше последней подтверждённой
	  записи: перед письмом актор сверяет seq с Last (ReadIndex) и
	  догоняет поток;
	- сумма сохраняется: баланс ключа — сумма его событий, итог — сумма
	  подтверждённых клиентам операций.

	Порчи (RaftJournal.mutNoWait, mutNoExpected, RaftNode.mutLocalQuery,
	ActorPersist.mutNoFresh, mutNoCatchUp, mutKeepOnConflict) обязаны ловиться.

	Обычный прогон — 100 зёрен, длинный — Raft_devGrainJournal.Long
	(2000 зёрен). *)

	IMPORT Log, Services, Tasks, Strings, Stores,
		ActorCore, ActorJournal, ActorCodec, ActorPersist, ActorNet, ActorNetMem, ActorSim, ActorGrain,
		Core := RaftCore, RaftSm, RaftStorage, RaftNode, RaftJournal, RaftMembership, RaftGrainMembership,
		UnitAssert;

	CONST
		jn = 3;	(* узлов журнала *)
		sn = 3;	(* узлов службы состава *)
		gnMax = 4;	(* участников гранул *)
		nk = 3;	(* ключей *)
		nw = 4;	(* вкладов на ключ *)
		nsteps = 2 * nw;	(* вклад, чтение, вклад, ... *)
		maxReads = 4;
		maxOp = 32;
		lease = 200; renewEvery = 8; refreshEvery = 3;
		clientTimeout = 40; writerPeriod = 2; readerPeriod = 5;
		pumpPerRound = 64;

	TYPE
		Bytes = Core.Bytes;
		Name = ActorCore.Name;

		Deposit* = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; sum: INTEGER END;
		Reply* = POINTER TO RECORD (ActorNet.Msg) op: LONGINT; balance: INTEGER END;
		Read* = POINTER TO RECORD (ActorNet.Msg) tag: LONGINT END;
		ReadReply* = POINTER TO RECORD (ActorNet.Msg) tag: LONGINT; balance: INTEGER END;

		Deposited* = POINTER TO RECORD (ActorPersist.Event) op: LONGINT; sum: INTEGER; prev: LONGINT END;

		Account = POINTER TO RECORD (ActorGrain.Actor)
			balance: INTEGER; seen: ARRAY maxOp OF BOOLEAN
		END;

		Writer = POINTER TO RECORD (ActorCore.Actor)
			k, step, expect, kind, curSum, pin, opBase: INTEGER;
			op, tag: LONGINT;
			waiting, done: BOOLEAN;
			deadline: LONGINT
		END;

		Reader = POINTER TO RECORD (ActorCore.Actor)
			k, reads, pin: INTEGER;
			tag, expectAtSend: LONGINT;
			waiting, done: BOOLEAN;
			deadline: LONGINT
		END;

		CTick = POINTER TO RECORD (ActorCore.Msg) END;

		Cut = POINTER TO RECORD (ActorNet.Transport)
			net: ActorNetMem.Net;
			n: INTEGER;
			name: ARRAY 16 OF Name;
			blocked: ARRAY 16, 16 OF BOOLEAN
		END;

		Trace = POINTER TO RECORD (RaftNode.Tracer) END;
		Collect = POINTER TO RECORD (ActorJournal.Sink) k: INTEGER END;

		Driver = POINTER TO RECORD (Tasks.Task) body: PROCEDURE END;

	VAR
		net: ActorNetMem.Net; cut: Cut;
		seed, x, now, time: LONGINT;
		gen: INTEGER;

		jnd: ARRAY jn OF ActorNet.Node;
		jref: ARRAY jn OF ActorCore.Ref;
		jcfg: ARRAY jn OF RaftNode.Config;
		jup: ARRAY jn OF BOOLEAN;
		jcaps: RaftNode.Caps;

		snd: ARRAY sn OF ActorNet.Node;
		sref: ARRAY sn OF ActorCore.Ref;
		scfg: ARRAY sn OF RaftNode.Config;
		sup: ARRAY sn OF BOOLEAN;
		scaps: RaftNode.Caps;

		gnd: ARRAY gnMax OF ActorNet.Node;
		cl: ARRAY gnMax OF ActorGrain.Cluster;
		gjm: ARRAY gnMax OF RaftJournal.Client;
		gms: ARRAY gnMax OF RaftMembership.Client;
		gcfg: ARRAY gnMax OF RaftGrainMembership.Config;
		gup: ARRAY gnMax OF BOOLEAN;
		gn, nextJid, nextMid: INTEGER;

		wref: ARRAY nk OF ActorCore.Ref;
		rref: ARRAY nk OF ActorCore.Ref;
		writersDone, readersDone: INTEGER;
		pendingW, pendingR: INTEGER;
		driverDone: BOOLEAN;
		nreply, nreadreply: INTEGER;
		mutJ, mutN, mutP: SET;
		pinOf: ARRAY nk OF INTEGER; avoid: INTEGER;
		islands: BOOLEAN; healAt: LONGINT; bmoves: LONGINT;
		fCrashJ, fCrashS, fCrashG, fPartJ, fPartS, fCut, fAdd, fLeave, fSteps: INTEGER;
		pauseW: ARRAY nk OF BOOLEAN; readGate: ARRAY nk OF INTEGER;
		splitMode: BOOLEAN;
		xref, yref: ActorCore.Ref;
		pendingPin, pendingOpBase: INTEGER;

		ref: RaftSm.Sessions;
		decided: Core.Entries; dlast, refApplied: INTEGER;
		refLast: ARRAY nk OF LONGINT;
		refSum: ARRAY nk OF INTEGER;
		opSeen: ARRAY nk, maxOp OF BOOLEAN;
		refN: ARRAY nk OF INTEGER;
		expectedSum: ARRAY nk OF INTEGER;

		ok: BOOLEAN; why: ARRAY 256 OF CHAR;
		dbg: ARRAY 256 OF CHAR; dbgSent, dbgReplies, dbgBal: INTEGER;

	(* --- служебное ----------------------------------------------------------- *)

	PROCEDURE Rand (k: INTEGER): INTEGER;
	BEGIN
		x := x * 6364136223846793005 + 1442695040888963407;
		RETURN SHORT(ASH(x, -33) MOD k)
	END Rand;

	PROCEDURE Violation (IN what: ARRAY OF CHAR);
		VAR d: ARRAY 32 OF CHAR;
	BEGIN
		IF ok THEN
			ok := FALSE; Strings.IntToString(seed, d); why := what$ + " (seed " + d$ + ")"
		END
	END Violation;

	PROCEDURE KeyName (k: INTEGER; OUT s: ActorJournal.Name);
		VAR d: ARRAY 4 OF CHAR;
	BEGIN
		Strings.IntToString(k, d); s := "acct." + d$
	END KeyName;

	PROCEDURE KeyChar (k: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 4 OF CHAR;
	BEGIN
		Strings.IntToString(k, d); s := d$
	END KeyChar;

	PROCEDURE Ch (i: INTEGER): CHAR;
	BEGIN RETURN CHR(ORD("0") + i) END Ch;

	PROCEDURE ClientName (IN prefix: ARRAY OF CHAR; i: INTEGER; OUT s: ActorCore.Name);
		VAR n: INTEGER;
	BEGIN
		s := prefix$; n := LEN(s$); s[n] := Ch(i); s[n + 1] := 0X
	END ClientName;

	(* --- эталон журнала ------------------------------------------------------- *)

	PROCEDURE Same (IN a, b: Core.Entry): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		IF a.term # b.term THEN RETURN FALSE END;
		IF (a.data = NIL) OR (b.data = NIL) THEN RETURN (a.data = NIL) & (b.data = NIL) END;
		IF LEN(a.data^) # LEN(b.data^) THEN RETURN FALSE END;
		i := 0; WHILE (i < LEN(a.data^)) & (a.data[i] = b.data[i]) DO INC(i) END;
		RETURN i = LEN(a.data^)
	END Same;

	PROCEDURE Decide (k: INTEGER; IN e: Core.Entry);
		VAR a: Core.Entries; j: INTEGER;
	BEGIN
		IF k > dlast THEN
			IF k >= LEN(decided^) THEN
				NEW(a, 2 * k + 16); FOR j := 0 TO dlast DO a[j] := decided[j] END; decided := a
			END;
			FOR j := dlast + 1 TO k - 1 DO decided[j].term := -1 END;
			dlast := k; decided[k] := e
		ELSIF decided[k].term = -1 THEN decided[k] := e
		ELSIF ~Same(decided[k], e) THEN Violation("под индексом решены две разные записи")
		END
	END Decide;

	PROCEDURE (s: Collect) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
		VAR st: Stores.Store; res, k: INTEGER;
	BEGIN
		k := s.k;
		ActorCodec.dir.Decode(body, len, st, res);
		IF (res # ActorCodec.ok) OR ~(st IS Deposited) THEN Violation("событие эталона не читается"); RETURN FALSE END;
		IF m.seq # refLast[k] + 1 THEN Violation("пропуск или повтор номера события"); RETURN FALSE END;
		IF st(Deposited).prev # m.seq - 1 THEN
			Violation("запись сделана не из состояния перед ней: две копии писали мимо expected"); RETURN FALSE
		END;
		IF (st(Deposited).op < 1) OR (st(Deposited).op >= maxOp) THEN Violation("номер операции вне предела"); RETURN FALSE END;
		IF opSeen[k, SHORT(st(Deposited).op)] THEN Violation("операция записана дважды"); RETURN FALSE END;
		opSeen[k, SHORT(st(Deposited).op)] := TRUE;
		refLast[k] := m.seq; INC(refN[k]); INC(refSum[k], st(Deposited).sum);
		RETURN TRUE
	END Event;

	PROCEDURE Advance;
		VAR out: Core.Bytes; dup: BOOLEAN; k, res: INTEGER; s: Collect; st: ActorJournal.Name;
	BEGIN
		WHILE (refApplied < dlast) & (decided[refApplied + 1].term # -1) DO
			INC(refApplied); ref.Apply(refApplied, decided[refApplied].term, decided[refApplied].data, out, dup)
		END;
		FOR k := 0 TO nk - 1 DO
			IF ~ok THEN RETURN END;
			KeyName(k, st); NEW(s); s.k := k;
			ref.sm(RaftJournal.Machine).mem.Read(st, refLast[k] + 1, s, res)
		END
	END Advance;

	PROCEDURE (t: Trace) Applied (id, index: INTEGER; IN e: Core.Entry);
	BEGIN
		Decide(index, e); Advance
	END Applied;

	(* --- события и счёт ------------------------------------------------------- *)

	PROCEDURE (m: Deposit) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.op); wr.WriteInt(m.sum) END Write;

	PROCEDURE (m: Deposit) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.op); rd.ReadInt(m.sum) END Read;

	PROCEDURE (m: Reply) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.op); wr.WriteInt(m.balance) END Write;

	PROCEDURE (m: Reply) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.op); rd.ReadInt(m.balance) END Read;

	PROCEDURE (m: Read) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.tag) END Write;

	PROCEDURE (m: Read) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.tag) END Read;

	PROCEDURE (m: ReadReply) Write- (VAR wr: Stores.Writer);
	BEGIN wr.WriteLong(m.tag); wr.WriteInt(m.balance) END Write;

	PROCEDURE (m: ReadReply) Read- (VAR rd: Stores.Reader);
	BEGIN rd.ReadLong(m.tag); rd.ReadInt(m.balance) END Read;

	PROCEDURE (e: Deposited) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteLong(e.op); wr.WriteInt(e.sum); wr.WriteLong(e.prev)
	END Externalize;

	PROCEDURE (e: Deposited) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadLong(e.op); rd.ReadInt(e.sum); rd.ReadLong(e.prev)
	END Internalize;

	PROCEDURE (a: Account) Seen (op: LONGINT): BOOLEAN, NEW;
	BEGIN
		RETURN (op >= 1) & (op < maxOp) & a.seen[SHORT(op)]
	END Seen;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO
			INC(a.balance, e.sum);
			IF (e.op >= 1) & (e.op < maxOp) THEN a.seen[SHORT(e.op)] := TRUE END
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: ActorCore.Msg);
		VAR e: Deposited; r: Reply; rr: ReadReply;
	BEGIN
		WITH m: Deposit DO
			IF a.Seen(m.op) THEN
				NEW(r); r.op := m.op; r.balance := a.balance; c.Respond(r)
			ELSE
				NEW(e); e.op := m.op; e.sum := m.sum; e.prev := a.seq; a.Persist(c, e, m.op)
			END
		| m: Read DO
			NEW(rr); rr.tag := m.tag; rr.balance := a.balance; c.Respond(rr)
		ELSE
		END
	END Handle;

	PROCEDURE (a: Account) Persisted (c: ActorCore.Context; e: ActorPersist.Event);
		VAR r: Reply;
	BEGIN
		NEW(r); r.op := e(Deposited).op; r.balance := a.balance; c.Respond(r)
	END Persisted;

	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
		VAR i: INTEGER;
	BEGIN
		wr.WriteVersion(0); wr.WriteInt(a.balance);
		FOR i := 0 TO maxOp - 1 DO wr.WriteBool(a.seen[i]) END
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v, i: INTEGER;
	BEGIN
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(a.balance);
		FOR i := 0 TO maxOp - 1 DO rd.ReadBool(a.seen[i]) END
	END Internalize;

	PROCEDURE (a: Account) Restore (s: Stores.Store);
		VAR i: INTEGER;
	BEGIN
		WITH s: Account DO
			a.balance := s.balance; FOR i := 0 TO maxOp - 1 DO a.seen[i] := s.seen[i] END
		ELSE
		END
	END Restore;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN NEW(a); RETURN a END NewAccount;

	(* --- транспорт ----------------------------------------------------------- *)

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
	BEGIN
		IF (a >= 0) & (b >= 0) THEN t.blocked[a, b] := TRUE; t.blocked[b, a] := TRUE END
	END Block;

	PROCEDURE (t: Cut) Heal, NEW;
		VAR i, j: INTEGER;
	BEGIN
		FOR i := 0 TO 15 DO FOR j := 0 TO 15 DO t.blocked[i, j] := FALSE END END
	END Heal;

	(* --- имена и узлы --------------------------------------------------------- *)

	PROCEDURE NodeNameJ (i: INTEGER; OUT s: ActorCore.Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d); s := "J" + d$ + "x"; s[LEN(s$) - 1] := CHR(ORD("a") + i)
	END NodeNameJ;

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

	PROCEDURE NewTrace (): RaftNode.Tracer;
		VAR t: Trace;
	BEGIN NEW(t); RETURN t END NewTrace;

	PROCEDURE StartJournal;
		VAR i, j: INTEGER; name: Name; cap: ARRAY RaftNode.capLen OF CHAR;
	BEGIN
		FOR i := 0 TO jn - 1 DO
			NodeNameJ(i, name); jnd[i] := ActorNet.New(name, cut, 1000 + i); ActorNetMem.Add(net, jnd[i]);
			cut.Add(name); jup[i] := TRUE;
			jcfg[i] := RaftNode.NewConfig(i, jn, jnd[i], RaftStorage.NewMem(), RaftJournal.NewMachine);
			jcfg[i].electionTicks := 10; jcfg[i].seed := seed + i * 37 + gen; jcfg[i].tickMs := 0;
			jcfg[i].tracer := NewTrace()
		END;
		FOR i := 0 TO jn - 1 DO jref[i] := RaftNode.Start(jcfg[i]) END;
		FOR i := 0 TO jn - 1 DO ActorNet.Export(jnd[i], jref[i], cap); jcaps[i] := cap$ END;
		FOR i := 0 TO jn - 1 DO
			FOR j := 0 TO jn - 1 DO IF i # j THEN jcfg[i].peers[j] := jcaps[j]$ END END
		END
	END StartJournal;

	PROCEDURE StartService;
		VAR i, j: INTEGER; name: Name; cap: ARRAY RaftNode.capLen OF CHAR;
	BEGIN
		FOR i := 0 TO sn - 1 DO
			NodeNameS(i, name); snd[i] := ActorNet.New(name, cut, 3000 + i); ActorNetMem.Add(net, snd[i]);
			cut.Add(name); sup[i] := TRUE;
			scfg[i] := RaftNode.NewConfig(i, sn, snd[i], RaftStorage.NewMem(), RaftMembership.NewMachine);
			scfg[i].electionTicks := 10; scfg[i].seed := seed + 100 + i * 53 + gen; scfg[i].tickMs := 0
		END;
		FOR i := 0 TO sn - 1 DO sref[i] := RaftNode.Start(scfg[i]) END;
		FOR i := 0 TO sn - 1 DO ActorNet.Export(snd[i], sref[i], cap); scaps[i] := cap$ END;
		FOR i := 0 TO sn - 1 DO
			FOR j := 0 TO sn - 1 DO IF i # j THEN scfg[i].peers[j] := scaps[j]$ END END
		END
	END StartService;

	PROCEDURE NewGrainNode (i: INTEGER);
		VAR key: ARRAY 2 OF BYTE; name: Name;
	BEGIN
		IF gup[i] THEN RETURN END;
		IF gnd[i] = NIL THEN
			NodeNameG(i, name); gnd[i] := ActorNet.New(name, cut, 2000 + i); ActorNetMem.Add(net, gnd[i]);
			cut.Add(name)
		ELSE ActorNet.Up(gnd[i])
		END;
		INC(nextJid); gjm[i] := RaftJournal.New(gnd[i], jcaps, jn, nextJid);
		gjm[i].tickMs := 0; gjm[i].timeoutTicks := 8; gjm[i].giveUpTicks := 120;
		cl[i] := ActorGrain.New(gnd[i], gjm[i]); ActorGrain.Register(cl[i], "acct", NewAccount);
		INC(nextMid); gms[i] := RaftMembership.New(gnd[i], scaps, sn, nextMid);
		gms[i].tickMs := 0; gms[i].timeoutTicks := 10; gms[i].giveUpTicks := 100;
		key[0] := SHORT(SHORT(i + 1)); key[1] := 3;
		gcfg[i] := RaftGrainMembership.NewConfig(i, cl[i], gms[i], key, 2, lease, renewEvery, refreshEvery, 0);
		IF RaftGrainMembership.Start(gcfg[i]) = NIL THEN END;
		gup[i] := TRUE;
		IF i >= gn THEN gn := i + 1 END
	END NewGrainNode;

	PROCEDURE TickGrain (i: INTEGER);
	BEGIN
		IF gcfg[i] # NIL THEN RaftGrainMembership.Tick(gcfg[i]) END;
		IF gjm[i] # NIL THEN RaftJournal.Tick(gjm[i]) END
	END TickGrain;

	PROCEDURE DownGrain (i: INTEGER);
	BEGIN
		IF gup[i] THEN
			IF gms[i] # NIL THEN RaftMembership.Close(gms[i]); gms[i] := NIL END;
			IF gjm[i] # NIL THEN gjm[i].Close; gjm[i] := NIL END;
			cl[i] := NIL; gcfg[i] := NIL;
			ActorNetMem.Crash(net, gnd[i].name); gup[i] := FALSE
		END
	END DownGrain;

	PROCEDURE StopJournal (i: INTEGER);
	BEGIN
		IF jup[i] THEN ActorNetMem.Crash(net, jnd[i].name); jup[i] := FALSE END
	END StopJournal;

	PROCEDURE RestartJournal (i: INTEGER);
	BEGIN
		IF ~jup[i] & (ActorCore.dir.Lookup(ActorCore.node, jnd[i].name) = NIL) THEN
			ActorNet.Up(jnd[i]); jref[i] := RaftNode.Start(jcfg[i]); jup[i] := TRUE
		END
	END RestartJournal;

	PROCEDURE StopService (i: INTEGER);
	BEGIN
		IF sup[i] THEN ActorNetMem.Crash(net, snd[i].name); sup[i] := FALSE END
	END StopService;

	PROCEDURE RestartService (i: INTEGER);
	BEGIN
		IF ~sup[i] & (ActorCore.dir.Lookup(ActorCore.node, snd[i].name) = NIL) THEN
			ActorNet.Up(snd[i]); sref[i] := RaftNode.Start(scfg[i]); sup[i] := TRUE
		END
	END RestartService;

	(* --- клиенты ------------------------------------------------------------- *)

	PROCEDURE LiveCluster (pin, skip: INTEGER): INTEGER;
		VAR i, k, pick, count: INTEGER;
	BEGIN
		IF (pin >= 0) & (pin < gn) & gup[pin] THEN RETURN pin END;
		count := 0; FOR i := 0 TO gn - 1 DO IF gup[i] & (i # skip) THEN INC(count) END END;
		IF count = 0 THEN
			count := 0; FOR i := 0 TO gn - 1 DO IF gup[i] THEN INC(count) END END;
			IF count = 0 THEN RETURN -1 END;
			pick := Rand(count);
			FOR i := 0 TO gn - 1 DO
				IF gup[i] THEN IF pick = 0 THEN RETURN i ELSE DEC(pick) END END
			END
		ELSE
			pick := Rand(count);
			FOR i := 0 TO gn - 1 DO
				IF gup[i] & (i # skip) THEN IF pick = 0 THEN RETURN i ELSE DEC(pick) END END
			END
		END;
		RETURN -1
	END LiveCluster;

	PROCEDURE SendWrite (w: Writer; c: ActorCore.Context);
		VAR ci: INTEGER; d: Deposit; r: ActorCore.Ref; key: ActorCore.Name;
	BEGIN
		ci := LiveCluster(w.pin, avoid);
		IF ci < 0 THEN RETURN END;
		KeyChar(w.k, key); r := ActorGrain.Ref(cl[ci], "acct", key);
		NEW(d); d.op := w.op; d.sum := w.curSum;
		w.waiting := TRUE; w.deadline := now + clientTimeout;
		c.Tell(r, d)
	END SendWrite;

	PROCEDURE SendRead (w: Writer; c: ActorCore.Context);
		VAR ci: INTEGER; q: Read; r: ActorCore.Ref; key: ActorCore.Name;
	BEGIN
		ci := LiveCluster(w.pin, avoid);
		IF ci < 0 THEN RETURN END;
		KeyChar(w.k, key); r := ActorGrain.Ref(cl[ci], "acct", key);
		NEW(q); q.tag := w.tag;
		w.waiting := TRUE; w.deadline := now + clientTimeout;
		c.Tell(r, q)
	END SendRead;

	PROCEDURE (w: Writer) StartStep (c: ActorCore.Context), NEW;
	BEGIN
		w.kind := w.step MOD 2;
		IF w.kind = 0 THEN
			w.op := w.opBase + w.step DIV 2 + 1; w.curSum := 1 + w.step DIV 2; SendWrite(w, c)
		ELSE
			INC(w.tag); w.curSum := expectedSum[w.k]; SendRead(w, c)
		END
	END StartStep;

	PROCEDURE (w: Writer) Tick (c: ActorCore.Context), NEW;
	BEGIN
		IF w.done OR (pauseW[w.k] & (w.opBase = 0)) THEN RETURN END;
		IF w.waiting THEN
			IF now >= w.deadline THEN
				IF w.kind = 0 THEN SendWrite(w, c) ELSE SendRead(w, c) END
			END
		ELSE w.StartStep(c)
		END
	END Tick;

	PROCEDURE (w: Writer) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR expect: INTEGER;
	BEGIN
		IF m IS CTick THEN w.Tick(c); RETURN END;
		WITH m: Reply DO
			INC(nreply);
			IF w.waiting & (w.kind = 0) & (m.op = w.op) THEN
				expect := w.expect + w.curSum;
				IF ~opSeen[w.k, SHORT(m.op)] THEN Violation("подтверждённая операция не записана в журнал")
				ELSIF splitMode THEN
					w.expect := m.balance; IF m.balance > expectedSum[w.k] THEN expectedSum[w.k] := m.balance END;
					INC(w.step); w.waiting := FALSE;
					IF w.step >= nsteps THEN w.done := TRUE; INC(writersDone) END
				ELSIF m.balance # expect THEN Violation("ответ на вклад не равен сумме подтверждённых записей")
				ELSIF refSum[w.k] # expect THEN Violation("подтверждённая запись не попала в эталон")
				ELSE
					w.expect := m.balance; expectedSum[w.k] := m.balance;
					INC(w.step); w.waiting := FALSE;
					IF w.step >= nsteps THEN w.done := TRUE; INC(writersDone) END
				END
			END
		| m: ReadReply DO
			INC(nreadreply);
			IF w.waiting & (w.kind = 1) & (m.tag = w.tag) THEN
				IF ~splitMode & (m.balance < w.curSum) THEN Violation("ответ-чтение старше подтверждённой записи")
				ELSE
					INC(w.step); w.waiting := FALSE;
					IF w.step >= nsteps THEN w.done := TRUE; INC(writersDone) END
				END
			END
		ELSE
		END
	END Receive;

	PROCEDURE NewWriter (): ActorCore.Actor;
		VAR w: Writer;
	BEGIN NEW(w); w.k := pendingW; w.pin := -1; w.opBase := 0; RETURN w END NewWriter;

	PROCEDURE NewWriterX (): ActorCore.Actor;
		VAR w: Writer;
	BEGIN NEW(w); w.k := pendingW; w.pin := pendingPin; w.opBase := pendingOpBase; RETURN w END NewWriterX;

	PROCEDURE (r: Reader) Tick (c: ActorCore.Context), NEW;
		VAR ci: INTEGER; q: Read; ref: ActorCore.Ref; key: ActorCore.Name;
	BEGIN
		IF r.done THEN RETURN END;
		IF r.waiting THEN
			IF now >= r.deadline THEN
				ci := LiveCluster(pinOf[r.k], -1);
				IF ci >= 0 THEN
					KeyChar(r.k, key); ref := ActorGrain.Ref(cl[ci], "acct", key);
					NEW(q); q.tag := r.tag; r.deadline := now + clientTimeout; c.Tell(ref, q)
				END
			END
		ELSIF r.reads >= maxReads THEN r.done := TRUE; INC(readersDone)
		ELSIF expectedSum[r.k] >= readGate[r.k] THEN
			INC(r.tag); r.expectAtSend := expectedSum[r.k]; INC(dbgSent);
			ci := LiveCluster(pinOf[r.k], -1);
			IF ci >= 0 THEN
				KeyChar(r.k, key); ref := ActorGrain.Ref(cl[ci], "acct", key);
				NEW(q); q.tag := r.tag;
				r.waiting := TRUE; r.deadline := now + clientTimeout; c.Tell(ref, q)
			END
		ELSE	(* ждём разрешения чтения *)
		END
	END Tick;

	PROCEDURE (r: Reader) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS CTick THEN r.Tick(c); RETURN END;
		WITH m: ReadReply DO
			INC(nreadreply); INC(dbgReplies); IF r.k = 0 THEN dbgBal := m.balance END;
			IF r.waiting & (m.tag = r.tag) THEN
				IF m.balance < r.expectAtSend THEN Violation("читатель получил состояние старше подтверждённой записи")
				ELSE
					INC(r.reads); r.waiting := FALSE;
					IF r.reads >= maxReads THEN r.done := TRUE; INC(readersDone) END
				END
			END
		ELSE
		END
	END Receive;

	PROCEDURE NewReader (): ActorCore.Actor;
		VAR r: Reader;
	BEGIN NEW(r); r.k := pendingR; r.pin := -1; RETURN r END NewReader;

	(* --- продвижение ---------------------------------------------------------- *)

	PROCEDURE TickClients;
		VAR i: INTEGER; t: CTick;
	BEGIN
		IF now MOD writerPeriod = 0 THEN
			FOR i := 0 TO nk - 1 DO IF wref[i] # NIL THEN NEW(t); ActorCore.Tell(wref[i], t) END END
		END;
		IF now MOD readerPeriod = 0 THEN
			FOR i := 0 TO nk - 1 DO IF rref[i] # NIL THEN NEW(t); ActorCore.Tell(rref[i], t) END END
		END;
		IF (now MOD writerPeriod = 0) & (xref # NIL) THEN NEW(t); ActorCore.Tell(xref, t) END;
		IF (now MOD writerPeriod = 0) & (yref # NIL) THEN NEW(t); ActorCore.Tell(yref, t) END
	END TickClients;

	PROCEDURE Round;
		VAR i, k: INTEGER; t: RaftNode.Tick;
	BEGIN
		INC(now); INC(time); RaftGrainMembership.time := time;
		TickClients;
		FOR i := 0 TO jn - 1 DO IF jup[i] THEN NEW(t); ActorCore.Tell(jref[i], t) END END;
		FOR i := 0 TO sn - 1 DO IF sup[i] THEN NEW(t); ActorCore.Tell(sref[i], t) END END;
		FOR i := 0 TO gn - 1 DO IF gup[i] & (gcfg[i] # NIL) THEN TickGrain(i) END END;
		FOR k := 1 TO pumpPerRound DO
			IF ActorNetMem.Pump(net) THEN END;
			IF ActorSim.Step() THEN END
		END
	END Round;

	PROCEDURE Rounds (k: INTEGER);
		VAR i: INTEGER;
	BEGIN FOR i := 1 TO k DO Round END END Rounds;

	PROCEDURE AllSeen (n: INTEGER): BOOLEAN;
		VAR i: INTEGER; yes: BOOLEAN;
	BEGIN
		yes := TRUE;
		FOR i := 0 TO gn - 1 DO
			IF gup[i] & (cl[i] # NIL) & (ActorGrain.AliveCount(cl[i]) # n) THEN yes := FALSE END
		END;
		RETURN yes
	END AllSeen;

	PROCEDURE WaitSeen (n, max: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE ~AllSeen(n) & (i < max) DO Round; INC(i) END;
		RETURN AllSeen(n)
	END WaitSeen;

	(* --- прогон --------------------------------------------------------------- *)

	PROCEDURE HealAll;
	(* Лечение: разрывы сняты, упавшие узлы подняты, лишний участник убран. *)
		VAR i: INTEGER;
	BEGIN
		cut.Heal; islands := FALSE;
		IF ActorSim.Drain(10000) THEN END;
		FOR i := 0 TO jn - 1 DO RestartJournal(i) END;
		FOR i := 0 TO sn - 1 DO RestartService(i) END;
		IF (gn > 3) & gup[3] THEN DownGrain(3) END;
		FOR i := 0 TO 2 DO
			IF ~gup[i] & (gnd[i] # NIL) & (ActorCore.dir.Lookup(ActorCore.node, gnd[i].name) = NIL) THEN NewGrainNode(i) END
		END;
		Rounds(400)
	END HealAll;

	PROCEDURE Setup;
		VAR i: INTEGER; nm: Name;
	BEGIN
		INC(gen); ActorSim.Install; net := ActorNetMem.New(seed); NEW(cut); cut.net := net;
		StartJournal; StartService;
		FOR i := 0 TO 2 DO NewGrainNode(i) END;
		IF ~WaitSeen(3, 4000) THEN Violation("состав из трёх не сошёлся") END;
		FOR i := 0 TO nk - 1 DO
			pendingW := i; ClientName("W", i, nm);
			wref[i] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWriter), nm);
			pendingR := i; ClientName("R", i, nm);
			rref[i] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewReader), nm)
		END;
		Rounds(20)
	END Setup;

	PROCEDURE Finish;
		VAR i, k: INTEGER;
	BEGIN
		HealAll;
		IF ~splitMode THEN
			FOR k := 0 TO nk - 1 DO
				IF expectedSum[k] # refSum[k] THEN Violation("баланс ключа не равен сумме подтверждённых вкладов") END;
				IF refN[k] # nw THEN Violation("число записей ключа не равно числу вкладов") END
			END
		END;
		FOR i := 0 TO gn - 1 DO
			IF gup[i] THEN
				IF gms[i] # NIL THEN RaftMembership.Close(gms[i]); gms[i] := NIL END;
				IF gjm[i] # NIL THEN gjm[i].Close; gjm[i] := NIL END;
				ActorNet.Down(gnd[i]); gup[i] := FALSE
			END
		END;
		FOR i := 0 TO jn - 1 DO IF jup[i] THEN ActorNet.Down(jnd[i]); jup[i] := FALSE END END;
		FOR i := 0 TO sn - 1 DO IF sup[i] THEN ActorNet.Down(snd[i]); sup[i] := FALSE END END;
		FOR i := 0 TO nk - 1 DO
			IF wref[i] # NIL THEN ActorCore.StopActor(wref[i]); wref[i] := NIL END;
			IF rref[i] # NIL THEN ActorCore.StopActor(rref[i]); rref[i] := NIL END
		END;
		IF xref # NIL THEN ActorCore.StopActor(xref); xref := NIL END;
		IF yref # NIL THEN ActorCore.StopActor(yref); yref := NIL END;
		IF ActorSim.Drain(100000) THEN END
	END Finish;

	PROCEDURE Reset;
		VAR i, j: INTEGER;
	BEGIN
		gn := 0; nextJid := 0; nextMid := 0; writersDone := 0; readersDone := 0; nreply := 0; nreadreply := 0;
		avoid := -1; splitMode := FALSE; xref := NIL; yref := NIL; islands := FALSE; healAt := 0; bmoves := 0;
		dlast := 0; refApplied := 0;
		NEW(decided, 64); decided[0].term := 0;
		ref := RaftSm.New(RaftJournal.NewMachine());
		FOR i := 0 TO nk - 1 DO
			refLast[i] := 0; refSum[i] := 0; refN[i] := 0; expectedSum[i] := 0; pinOf[i] := -1;
			pauseW[i] := FALSE; readGate[i] := 0;
			FOR j := 0 TO maxOp - 1 DO opSeen[i, j] := FALSE END;
			wref[i] := NIL; rref[i] := NIL;
			gnd[i] := NIL; cl[i] := NIL; gjm[i] := NIL; gms[i] := NIL; gcfg[i] := NIL; gup[i] := FALSE
		END;
		FOR i := nk TO gnMax - 1 DO
			gnd[i] := NIL; cl[i] := NIL; gjm[i] := NIL; gms[i] := NIL; gcfg[i] := NIL; gup[i] := FALSE
		END
	END Reset;

	PROCEDURE^ StepFaults;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Body;
		VAR d: ARRAY 32 OF CHAR; msg: ARRAY 256 OF CHAR;
	BEGIN
		Setup;
		bmoves := 0;
		WHILE (bmoves < 120) & ok DO StepFaults; Round; INC(bmoves) END;
		IF ok THEN HealAll END;
		WHILE ((writersDone < nk) OR (readersDone < nk)) & (bmoves < 3000) & ok DO
			Round; INC(bmoves)
		END;
		IF (writersDone < nk) OR (readersDone < nk) THEN
			msg := "прогон не завершился: w="; Strings.IntToString(writersDone, d); msg := msg + d$;
			msg := msg + " r="; Strings.IntToString(readersDone, d); msg := msg + d$;
			msg := msg + " ev="; Strings.IntToString(refN[0] + refN[1] + refN[2], d); msg := msg + d$;
			msg := msg + " replies="; Strings.IntToString(nreply, d); msg := msg + d$;
			msg := msg + " reads="; Strings.IntToString(nreadreply, d); msg := msg + d$;
			Violation(msg)
		END;
		Finish
	END Body;

	PROCEDURE RunSeed (sd: LONGINT; OUT good: BOOLEAN);
		VAR d: Driver; t: LONGINT;
	BEGIN
		seed := sd; x := sd * 2654435761 + 88172645463325252; now := 0; time := 0;
		ok := TRUE; why := "";
		Reset;
		RaftJournal.mutations := mutJ; RaftNode.mutations := mutN; ActorPersist.mutations := mutP;
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {};
		driverDone := FALSE; NEW(d); d.body := Body; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= 60000) DO Services.actionHook.Step END;
		IF ~driverDone & ok THEN Violation("прогон завис") END;
		good := driverDone & ok;
		RaftJournal.mutations := {}; RaftNode.mutations := {}; ActorPersist.mutations := {};
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {}
	END RunSeed;

	(* --- сбои в прогонах ------------------------------------------------------ *)

	PROCEDURE^ BlockNames (IN a, b: Name);
	PROCEDURE^ BlockFromService (i: INTEGER);
	PROCEDURE^ OwnerIdx (ci, k: INTEGER): INTEGER;

	PROCEDURE GrainUp (): INTEGER;
		VAR i, k: INTEGER;
	BEGIN k := 0; FOR i := 0 TO gn - 1 DO IF gup[i] THEN INC(k) END END; RETURN k END GrainUp;

	PROCEDURE JournalUp (): INTEGER;
		VAR i, k: INTEGER;
	BEGIN k := 0; FOR i := 0 TO jn - 1 DO IF jup[i] THEN INC(k) END END; RETURN k END JournalUp;

	PROCEDURE ServiceUp (): INTEGER;
		VAR i, k: INTEGER;
	BEGIN k := 0; FOR i := 0 TO sn - 1 DO IF sup[i] THEN INC(k) END END; RETURN k END ServiceUp;

	PROCEDURE PartitionJournal;
		VAR i, x: INTEGER; na, nb: Name;
	BEGIN
		i := Rand(jn);
		IF jup[i] THEN
			NodeNameJ(i, na);
			FOR x := 0 TO jn - 1 DO IF x # i THEN NodeNameJ(x, nb); BlockNames(na, nb) END END;
			islands := TRUE; healAt := bmoves + 10 + Rand(30)
		END
	END PartitionJournal;

	PROCEDURE PartitionService;
		VAR i, x: INTEGER; na, nb: Name;
	BEGIN
		i := Rand(sn);
		IF sup[i] THEN
			NodeNameS(i, na);
			FOR x := 0 TO sn - 1 DO IF x # i THEN NodeNameS(x, nb); BlockNames(na, nb) END END;
			islands := TRUE; healAt := bmoves + 10 + Rand(30)
		END
	END PartitionService;

	PROCEDURE CutOwner;
		VAR ci, k, ow: INTEGER;
	BEGIN
		k := Rand(nk); ci := LiveCluster(-1, -1);
		IF ci < 0 THEN RETURN END;
		ow := OwnerIdx(ci, k);
		IF ow >= 0 THEN
			BlockFromService(ow); islands := TRUE; healAt := bmoves + 20 + Rand(30)
		END
	END CutOwner;

	PROCEDURE Fault;
		VAR i: INTEGER;
	BEGIN
		CASE Rand(100) OF
			| 0..10: IF JournalUp() > 1 THEN i := Rand(jn); IF jup[i] THEN StopJournal(i); INC(fCrashJ) END END
		| 11..19: i := Rand(jn); IF ~jup[i] THEN RestartJournal(i) END
		| 20..29: IF ServiceUp() > 1 THEN i := Rand(sn); IF sup[i] THEN StopService(i); INC(fCrashS) END END
		| 30..37: i := Rand(sn); IF ~sup[i] THEN RestartService(i) END
		| 38..47: IF GrainUp() > 2 THEN i := Rand(3); IF gup[i] THEN DownGrain(i); INC(fCrashG) END END
		| 48..55: i := Rand(gn); IF ~gup[i] THEN NewGrainNode(i) END
		| 56..70: IF ~islands THEN PartitionJournal; INC(fPartJ) END
		| 71..80: IF ~islands THEN PartitionService; INC(fPartS) END
		| 81..90: IF ~islands THEN CutOwner; INC(fCut) END
		ELSE
		END
	END Fault;

	PROCEDURE StepFaults;
	BEGIN
		INC(fSteps);
		IF islands & (bmoves >= healAt) THEN cut.Heal; islands := FALSE END;
		IF bmoves < 120 THEN
			IF bmoves MOD 3 = 0 THEN Fault END;
			IF bmoves = 6 THEN IF gn < 4 THEN NewGrainNode(3); INC(fAdd) END END;
			IF bmoves = 60 THEN
				IF gup[3] & (gcfg[3] # NIL) THEN RaftGrainMembership.Leave(gcfg[3]); INC(fLeave) END
			END
		END
	END StepFaults;

	(* --- сценарии с разрывами ------------------------------------------------ *)

	PROCEDURE BlockNames (IN a, b: Name);
	BEGIN cut.Block(cut.Index(a), cut.Index(b)) END BlockNames;

	PROCEDURE BlockFromService (i: INTEGER);
		VAR x: INTEGER; nm: Name;
	BEGIN
		FOR x := 0 TO sn - 1 DO NodeNameS(x, nm); BlockNames(gnd[i].name, nm) END
	END BlockFromService;

	PROCEDURE BlockJournalExcept (i, keep: INTEGER);
		VAR x: INTEGER; nm: Name;
	BEGIN
		FOR x := 0 TO jn - 1 DO
			IF x # keep THEN NodeNameJ(x, nm); BlockNames(gnd[i].name, nm) END
		END
	END BlockJournalExcept;

	PROCEDURE SplitJournal (keep: INTEGER);
		VAR x: INTEGER; na, nb: Name;
	BEGIN
		NodeNameJ(keep, na);
		FOR x := 0 TO jn - 1 DO IF x # keep THEN NodeNameJ(x, nb); BlockNames(na, nb) END END
	END SplitJournal;

	PROCEDURE OwnerIdx (ci, k: INTEGER): INTEGER;
		VAR key, onm: Name; i: INTEGER;
	BEGIN
		IF (ci < 0) OR (ci >= gn) OR (cl[ci] = NIL) THEN RETURN -1 END;
		KeyChar(k, key); ActorGrain.Owner(cl[ci], "acct", key, onm);
		i := 0; WHILE (i < gn) & ((gnd[i] = NIL) OR (gnd[i].name # onm)) DO INC(i) END;
		IF (i >= gn) OR (gnd[i] = NIL) THEN RETURN -1 END;
		RETURN i
	END OwnerIdx;

	PROCEDURE WaitSum (k, v, max: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (expectedSum[k] < v) & (i < max) & ok DO Round; INC(i) END;
		RETURN expectedSum[k] >= v
	END WaitSum;

	PROCEDURE AllDead (ow: INTEGER): BOOLEAN;
		VAR i: INTEGER; yes: BOOLEAN;
	BEGIN
		yes := TRUE;
		FOR i := 0 TO gn - 1 DO
			IF gup[i] & (i # ow) & (cl[i] # NIL) & ActorGrain.Alive(cl[i], gnd[ow].name) THEN yes := FALSE END
		END;
		RETURN yes
	END AllDead;

	PROCEDURE WaitExpireAll (ow: INTEGER): BOOLEAN;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < lease + 400) & ~AllDead(ow) & ok DO Round; INC(i) END;
		RETURN AllDead(ow)
	END WaitExpireAll;

	PROCEDURE FreshIndex (ow: INTEGER): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO gn - 1 DO IF gup[i] & (i # ow) THEN RETURN i END END;
		RETURN -1
	END FreshIndex;

	PROCEDURE ExtraName (IN prefix: ARRAY OF CHAR; OUT s: Name);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(gen, d); s := prefix + d$
	END ExtraName;

	PROCEDURE BeginScript (sd: LONGINT);
	BEGIN
		seed := sd; x := sd * 2654435761 + 88172645463325252; now := 0; time := 0;
		ok := TRUE; why := "";
		Reset;
		RaftJournal.mutations := mutJ; RaftNode.mutations := mutN; ActorPersist.mutations := mutP;
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {}
	END BeginScript;

	PROCEDURE Script (body: PROCEDURE; ms: LONGINT; OUT good: BOOLEAN);
		VAR d: Driver; t: LONGINT;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body; Tasks.Start(d);
		t := Services.Ticks();
		WHILE ~driverDone & (Services.Ticks() - t <= ms) DO Services.actionHook.Step END;
		IF ~driverDone & ok THEN Violation("сценарий завис") END;
		good := driverDone & ok;
		RaftJournal.mutations := {}; RaftNode.mutations := {}; ActorPersist.mutations := {};
		RaftMembership.mutations := {}; RaftGrainMembership.mutations := {}; ActorGrain.mutations := {}
	END Script;

	PROCEDURE StaleBody;
	(* Владелец ключа отрезан от службы и большинства журнала; другой
		владелец подтверждает новую запись, читатель идёт через застывший
		взгляд отрезанного узла. Верный код не отвечает устаревшим
		состоянием, порча чтения — отвечает. *)
		VAR k, ow, fr, m, i: INTEGER; d: ARRAY 32 OF CHAR;
	BEGIN
		k := 0; readGate[k] := 100000;	(* читатель ключа ждёт, пока не позовут *)
		Setup;
		IF ~ok THEN Finish; RETURN END;
		IF ~WaitSum(k, 1, 3000) THEN Violation("первый вклад не подтверждён"); Finish; RETURN END;
		ow := OwnerIdx(0, k);
		IF ow < 0 THEN Violation("владелец ключа не участник"); Finish; RETURN END;
		fr := FreshIndex(ow);
		IF fr < 0 THEN Violation("нет свежего участника"); Finish; RETURN END;
		pauseW[k] := TRUE; avoid := ow;
		BlockFromService(ow);
		m := Rand(jn); BlockJournalExcept(ow, m); SplitJournal(m);
		IF ~WaitExpireAll(ow) THEN Violation("отрезанный владелец не истёк у всех") END;
		pauseW[k] := FALSE;
		IF ~WaitSum(k, 3, 5000) THEN Violation("вклад после переезда не подтверждён") END;
		pinOf[k] := ow; readGate[k] := 3; dbgSent := 0; dbgReplies := 0; dbgBal := -1;
		Rounds(600);
		IF ok THEN
			dbg := "ow="; Strings.IntToString(ow, d); dbg := dbg + d$ + " exp="; Strings.IntToString(expectedSum[k], d); dbg := dbg + d$;
			dbg := dbg + " sent="; Strings.IntToString(dbgSent, d); dbg := dbg + d$ + " rep="; Strings.IntToString(dbgReplies, d); dbg := dbg + d$;
			dbg := dbg + " bal="; Strings.IntToString(dbgBal, d); dbg := dbg + d$ + " alive_ow=";
			IF ActorGrain.Alive(cl[0], gnd[ow].name) THEN dbg := dbg + "1" ELSE dbg := dbg + "0" END
		END;
		cut.Heal; avoid := -1; pinOf[k] := -1; readGate[k] := 0;
		Rounds(1500);
		Finish
	END StaleBody;

	PROCEDURE BehindBody;
	(* Владелец отрезан только от службы: журнал ему виден, и Last
		показывает ушедший вперёд поток. Верный код догоняет его и отвечает
		свежим состоянием, порча догона — старым. *)
		VAR k, ow, fr, i: INTEGER; d: ARRAY 32 OF CHAR;
	BEGIN
		k := 0; readGate[k] := 100000;
		Setup;
		IF ~ok THEN Finish; RETURN END;
		IF ~WaitSum(k, 1, 3000) THEN Violation("первый вклад не подтверждён"); Finish; RETURN END;
		ow := OwnerIdx(0, k);
		IF ow < 0 THEN Violation("владелец ключа не участник"); Finish; RETURN END;
		fr := FreshIndex(ow);
		IF fr < 0 THEN Violation("нет свежего участника"); Finish; RETURN END;
		pauseW[k] := TRUE; avoid := ow;
		BlockFromService(ow);
		IF ~WaitExpireAll(ow) THEN Violation("отрезанный владелец не истёк у всех") END;
		pauseW[k] := FALSE;
		IF ~WaitSum(k, 3, 5000) THEN Violation("вклад после переезда не подтверждён") END;
		pinOf[k] := ow; readGate[k] := 3; dbgSent := 0; dbgReplies := 0; dbgBal := -1;
		Rounds(600);
		IF ok THEN
			dbg := "ow="; Strings.IntToString(ow, d); dbg := dbg + d$ + " exp="; Strings.IntToString(expectedSum[k], d); dbg := dbg + d$;
			dbg := dbg + " sent="; Strings.IntToString(dbgSent, d); dbg := dbg + d$ + " rep="; Strings.IntToString(dbgReplies, d); dbg := dbg + d$;
			dbg := dbg + " bal="; Strings.IntToString(dbgBal, d); dbg := dbg + d$
		END;
		cut.Heal; avoid := -1; pinOf[k] := -1; readGate[k] := 0;
		Rounds(1500);
		Finish
	END BehindBody;

	PROCEDURE SplitBody;
	(* Две копии ключа пишут одновременно: застывший владелец и новый.
		Верный код оставляет одну цепь записей, порчи — вторую запись мимо
		expected или подтверждение без журнала. *)
		VAR k, ow, fr: INTEGER; nm: Name; t: CTick;
	BEGIN
		k := 0;
		splitMode := TRUE; readGate[k] := 100000;
		Setup;
		IF ~ok THEN Finish; RETURN END;
		IF ~WaitSum(k, 1, 3000) THEN Violation("первый вклад не подтверждён"); Finish; RETURN END;
		ow := OwnerIdx(0, k);
		IF ow < 0 THEN Violation("владелец ключа не участник"); Finish; RETURN END;
		pauseW[k] := TRUE;
		BlockFromService(ow);
		fr := FreshIndex(ow);
		IF fr < 0 THEN Violation("нет свежего участника"); Finish; RETURN END;
		IF ~WaitExpireAll(ow) THEN Violation("отрезанный владелец не истёк у всех") END;
		IF OwnerIdx(fr, k) = ow THEN Violation("ключ не переехал") END;
		pendingW := k; pendingPin := ow; pendingOpBase := 10;
		ExtraName("X", nm); xref := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWriterX), nm);
		pendingPin := fr; pendingOpBase := 20;
		ExtraName("Y", nm); yref := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWriterX), nm);
		NEW(t); ActorCore.Tell(xref, t); NEW(t); ActorCore.Tell(yref, t);
		Rounds(600);
		cut.Heal; avoid := -1; readGate[k] := 0; splitMode := TRUE;
		Rounds(200);
		Finish
	END SplitBody;

	(* --- тесты ---------------------------------------------------------------- *)

	PROCEDURE Caught (mj, mn, mp: SET): BOOLEAN;
		VAR sd: LONGINT; good, found: BOOLEAN;
	BEGIN
		mutJ := mj; mutN := mn; mutP := mp; found := FALSE; sd := 1; ok := TRUE;
		WHILE (sd <= 20) & ~found DO
			RunSeed(sd, good); IF ~ok THEN found := TRUE END; INC(sd)
		END;
		mutJ := {}; mutN := {}; mutP := {};
		RETURN found
	END Caught;

	PROCEDURE ScriptCaught (mj, mn, mp: SET; body: PROCEDURE; OUT caught: BOOLEAN);
		VAR sd: LONGINT; good: BOOLEAN;
	BEGIN
		caught := FALSE; sd := 101;
		WHILE (sd <= 120) & ~caught DO
			mutJ := mj; mutN := mn; mutP := mp; BeginScript(sd); Script(body, 60000, good);
			IF ~good THEN caught := TRUE END; INC(sd)
		END;
		IF ~caught THEN why := dbg$ END;
		mutJ := {}; mutN := {}; mutP := {}
	END ScriptCaught;

	PROCEDURE ScriptOk (body: PROCEDURE; OUT good: BOOLEAN);
	BEGIN
		mutJ := {}; mutN := {}; mutP := {}; BeginScript(101); Script(body, 60000, good)
	END ScriptOk;

	PROCEDURE TestMutNoWait*;
	BEGIN
		UnitAssert.True(Caught({RaftJournal.mutNoWait}, {}, {}), "порча mutNoWait не поймана")
	END TestMutNoWait;

	PROCEDURE TestStaleBaseline*;
		VAR good: BOOLEAN;
	BEGIN
		ScriptOk(StaleBody, good);
		UnitAssert.True(good, why)
	END TestStaleBaseline;

	PROCEDURE TestMutLocalQuery*;
		VAR caught: BOOLEAN;
	BEGIN
		ScriptCaught({}, {RaftNode.mutLocalQuery}, {}, StaleBody, caught);
		UnitAssert.True(caught, why)
	END TestMutLocalQuery;

	PROCEDURE TestBehindBaseline*;
		VAR good: BOOLEAN;
	BEGIN
		ScriptOk(BehindBody, good);
		UnitAssert.True(good, why)
	END TestBehindBaseline;

	PROCEDURE TestMutNoCatchUp*;
		VAR caught: BOOLEAN;
	BEGIN
		ScriptCaught({}, {}, {ActorPersist.mutNoCatchUp}, BehindBody, caught);
		UnitAssert.True(caught, why)
	END TestMutNoCatchUp;

	PROCEDURE TestMutNoFresh*;
		VAR caught: BOOLEAN;
	BEGIN
		ScriptCaught({}, {}, {ActorPersist.mutNoFresh}, StaleBody, caught);
		UnitAssert.True(caught, why)
	END TestMutNoFresh;

	PROCEDURE TestSplitBaseline*;
		VAR good: BOOLEAN;
	BEGIN
		ScriptOk(SplitBody, good);
		UnitAssert.True(good, why)
	END TestSplitBaseline;

	PROCEDURE TestMutNoExpected*;
		VAR caught: BOOLEAN;
	BEGIN
		ScriptCaught({RaftJournal.mutNoExpected}, {}, {}, SplitBody, caught);
		UnitAssert.True(caught, why)
	END TestMutNoExpected;

	PROCEDURE TestMutKeepOnConflict*;
		VAR caught: BOOLEAN;
	BEGIN
		ScriptCaught({}, {}, {ActorPersist.mutKeepOnConflict}, SplitBody, caught);
		UnitAssert.True(caught, why)
	END TestMutKeepOnConflict;

	PROCEDURE TestSeeds*;
		VAR sd: LONGINT; good: BOOLEAN; d: ARRAY 32 OF CHAR; msg: ARRAY 256 OF CHAR;
	BEGIN
		ok := TRUE; why := ""; sd := 1;
		fCrashJ := 0; fCrashS := 0; fCrashG := 0; fPartJ := 0; fPartS := 0; fCut := 0; fAdd := 0; fLeave := 0; fSteps := 0;
		WHILE (sd <= 100) & ok DO RunSeed(sd, good); INC(sd) END;
		UnitAssert.True(ok, why);
		IF ~ok THEN RETURN END;
		msg := "сбои: J"; Strings.IntToString(fCrashJ, d); msg := msg + d$ + " S"; Strings.IntToString(fCrashS, d); msg := msg + d$;
		msg := msg + " G"; Strings.IntToString(fCrashG, d); msg := msg + d$ + " pJ"; Strings.IntToString(fPartJ, d); msg := msg + d$;
		msg := msg + " steps"; Strings.IntToString(fSteps, d); msg := msg + d$ + " mv"; Strings.IntToString(bmoves, d); msg := msg + d$;

		msg := msg + " pS"; Strings.IntToString(fPartS, d); msg := msg + d$ + " cut"; Strings.IntToString(fCut, d); msg := msg + d$;
		msg := msg + " add"; Strings.IntToString(fAdd, d); msg := msg + d$ + " leave"; Strings.IntToString(fLeave, d); msg := msg + d$;
		UnitAssert.True((fCrashJ > 0) & (fCrashS > 0) & (fCrashG > 0) & (fPartJ > 0) & (fPartS > 0) & (fCut > 0) & (fAdd > 0) & (fLeave > 0), msg)
	END TestSeeds;

	PROCEDURE Long*;
	(** Долгий прогон: 2000 зёрен; печатает, сколько прошло и чем кончилось. *)
		VAR sd: LONGINT; good: BOOLEAN; d: ARRAY 32 OF CHAR; msg: ARRAY 256 OF CHAR;
	BEGIN
		ok := TRUE; why := ""; sd := 1;
		WHILE (sd <= 2000) & ok DO RunSeed(sd, good); INC(sd) END;
		DEC(sd);
		msg := "Raft_devGrainJournal.Long: seeds "; Strings.IntToString(sd, d); msg := msg + d$;
		IF ok THEN msg := msg + ", ok" ELSE msg := msg + ", failed: " + why END;
		Log.String(msg); Log.Ln
	END Long;

END Raft_devGrainJournal.
