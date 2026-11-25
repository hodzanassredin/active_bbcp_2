MODULE Demo_devAllocOps;
(** Замеры выделений на одну операцию: каналы, выбор, задачи, письма
	акторам, транспорт ActorNetMem, кодек, Raft и Kv. Печатает строки
	ALLOC <имя> ops=... allocsPer1000=... bytesPerOp=... nsPerOp=...
	Счётчики — Kernel.AllocCount (число успешных NewBlock) и
	Kernel.Allocated (байты блоков). Числа имеют смысл только при
	записи нагрузки (load average) рядом; это замер, а не тест.

	Запуск: echo 'Demo_devAllocOps.Run' | tools/run64.sh *)

	IMPORT SYSTEM, Kernel, Tasks, Services, Console, Strings,
		AsyncChan, AsyncSelect, ActorCore, ActorRun, ActorNetMem, ActorCodec, ActorJournal,
		Stores, RaftCore, RaftStorage, KvStore, KvPages, KvLin, KvDb, System_devClock;

	CONST
		channelOps = 100000;
		selectOps = 100000;
		spawnOps = 20000;
		tellOps = 100000;
		askOps = 10000;
		frameOps = 10000;
		codecOps = 10000;
		raftOps = 1000;
		kvOps = 1000;
		commitOps = 100;
		stack = 64 * 1024;
		raftN = 3; poolSize = 100000;

	TYPE
		M = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Pinger = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER; m: M END;
		Ponger = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;
		Prod = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER; m: M END;
		Cons = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		SSend = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER; m: M END;
		SRecv = POINTER TO RECORD (Tasks.Task) cs: ARRAY 2 OF AsyncChan.Chan; n: INTEGER END;
		Short = POINTER TO RECORD (Tasks.Task) END;

		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Responder = POINTER TO RECORD (ActorCore.Actor) END;
		Ping = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Answer = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Body = PROCEDURE;
		Driver = POINTER TO RECORD (Tasks.Task) body: Body END;

		Small = POINTER TO RECORD (Stores.Store) n: INTEGER END;

	VAR
		sum, got, counterReached, asksDone: INTEGER;
		driverDone: BOOLEAN;
		a0, b0, t0: LONGINT;
		msg: M;
		ping: Ping; answer: Answer; counter: ActorCore.Ref; responder: ActorCore.Ref;
		net: ActorNetMem.Net;
		driverBody: Body;
		nodes: ARRAY raftN OF RaftCore.Node;
		disk: ARRAY raftN OF RaftStorage.Storage;
		pool: ARRAY poolSize OF RaftCore.Msg;
		head, tail: INTEGER;

	(* --- общий вывод ------------------------------------------------------ *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ops: INTEGER);
		VAR a, b, ns: LONGINT;
	BEGIN
		a := Kernel.AllocCount() - a0; b := Kernel.Allocated() - b0;
		ns := System_devClock.now() - t0;
		W("ALLOC "); W(name); W(" ops="); N(ops);
		W(" allocsPer1000="); N(a * 1000 DIV ops);
		W(" bytesPerOp="); N(b DIV ops);
		W(" nsPerOp="); N(ns DIV ops); Console.WriteLn
	END Report;

	PROCEDURE Start ();
	(* Пред: работа только предстоит; Пост: a0, b0, t0 — начало замера. *)
	BEGIN
		a0 := Kernel.AllocCount(); b0 := Kernel.Allocated(); t0 := System_devClock.now()
	END Start;

	PROCEDURE Go (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, stack); Tasks.Start(t)
	END Go;

	PROCEDURE Pump;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Pump;

	(* --- каналы ----------------------------------------------------------- *)

	PROCEDURE (t: Pinger) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.a.Send(t.m); t.b.Recv(x) END;
		INC(sum)
	END Run;

	PROCEDURE (t: Ponger) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.a.Recv(x); t.b.Send(x) END
	END Run;

	PROCEDURE Rendezvous;
		VAR p: Pinger; q: Ponger; n: INTEGER;
	BEGIN
		NEW(msg); msg.n := 1; sum := 0; n := 1000;
		NEW(p); NEW(q); p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan();
		p.n := n; p.m := msg; q.a := p.a; q.b := p.b; q.n := n;
		Go(p); Go(q); Pump;	(* разогрев: списки ожиданий и кольца *)
		NEW(p); NEW(q); p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan();
		p.n := channelOps; p.m := msg; q.a := p.a; q.b := p.b; q.n := channelOps;
		Go(p); Go(q);
		Start; Pump; Report("chan-rendezvous", channelOps)
	END Rendezvous;

	PROCEDURE (t: Prod) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Send(t.m) END
	END Run;

	PROCEDURE (t: Cons) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); INC(sum, x(M).n MOD 2) END
	END Run;

	PROCEDURE Buffer;
		VAR p: Prod; q: Cons; c: AsyncChan.Buffer;
	BEGIN
		NEW(msg); msg.n := 1; sum := 0;
		NEW(p); NEW(q); p.m := msg; p.n := 1000; q.n := 1000;
		c := AsyncChan.NewBuffer(64); p.c := c; q.c := c;
		Go(p); Go(q); Pump;
		NEW(p); NEW(q); p.m := msg; p.n := channelOps; q.n := channelOps;
		c := AsyncChan.NewBuffer(64); p.c := c; q.c := c;
		Go(p); Go(q);
		Start; Pump; Report("chan-buffer64", channelOps)
	END Buffer;

	PROCEDURE (t: SSend) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Send(t.m) END
	END Run;

	PROCEDURE (t: SRecv) Run;
		VAR i, j, w: INTEGER; m: AsyncSelect.Msg; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		FOR i := 1 TO t.n DO
			FOR j := 0 TO 1 DO AsyncChan.RecvAlt(t.cs[j], ops[j]) END;
			w := AsyncChan.Select(ops); INC(got, ops[w].got(M).n)
		END
	END Run;

	PROCEDURE Select2;
		VAR r: SRecv; s: SSend; j: INTEGER;
	BEGIN
		NEW(msg); msg.n := 1; got := 0;
		NEW(r); r.n := 1000; NEW(s); s.n := 500; s.m := msg;
		FOR j := 0 TO 1 DO r.cs[j] := AsyncChan.NewChan() END;
		Go(r); FOR j := 0 TO 1 DO NEW(s); s.c := r.cs[j]; s.n := 500; s.m := msg; Go(s) END;
		Pump;
		NEW(r); r.n := selectOps; FOR j := 0 TO 1 DO r.cs[j] := AsyncChan.NewChan() END;
		Go(r); FOR j := 0 TO 1 DO NEW(s); s.c := r.cs[j]; s.n := selectOps DIV 2; s.m := msg; Go(s) END;
		Start; Pump; Report("select2", selectOps)
	END Select2;

	PROCEDURE (t: Short) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE Spawn;
		VAR i: INTEGER; t: Short;
	BEGIN
		sum := 0;
		FOR i := 1 TO 1000 DO NEW(t); Go(t) END; Pump;	(* разогрев пула стеков *)
		Start;
		FOR i := 1 TO spawnOps DO NEW(t); Go(t) END;
		Pump;	(* задачи должны отработать до конца замера *)
		Report("task-spawn", spawnOps)
	END Spawn;

	(* --- акторы ----------------------------------------------------------- *)

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS Ping THEN INC(a.n); INC(counterReached) END
	END Receive;

	PROCEDURE NewCounter (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END NewCounter;

	PROCEDURE (a: Responder) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS Ping THEN c.Respond(answer) END
	END Receive;

	PROCEDURE NewResponder (): ActorCore.Actor;
		VAR a: Responder;
	BEGIN
		NEW(a); RETURN a
	END NewResponder;

	PROCEDURE (d: Driver) Run;
	BEGIN
		d.body; driverDone := TRUE
	END Run;

	PROCEDURE Drive (body: Body);
		VAR d: Driver;
	BEGIN
		driverDone := FALSE; NEW(d); d.body := body;
		Tasks.SetStackSize(d, stack); Tasks.Start(d);
		WHILE ~driverDone DO Tasks.Step END
	END Drive;

	PROCEDURE TellBody;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 100 DO ActorCore.Tell(counter, ping) END;
		WHILE counterReached < 100 DO Tasks.Yield END;
		counterReached := 0;
		Start;
		FOR i := 1 TO tellOps DO ActorCore.Tell(counter, ping) END;
		WHILE counterReached < tellOps DO Tasks.Yield END;
		Report("actor-tell", tellOps)
	END TellBody;

	PROCEDURE Tell;
	BEGIN
		counterReached := 0;
		NEW(ping); ping.n := 1;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(NewCounter), "");
		Drive(TellBody)
	END Tell;

	PROCEDURE AskBody;
		VAR i, res: INTEGER; reply: ActorCore.Msg;
	BEGIN
		FOR i := 1 TO 100 DO res := ActorCore.Ask(responder, ping, 1000, reply) END;
		asksDone := 0;
		Start;
		FOR i := 1 TO askOps DO res := ActorCore.Ask(responder, ping, 1000, reply); INC(asksDone) END;
		Report("actor-ask", askOps)
	END AskBody;

	PROCEDURE Ask;
	BEGIN
		NEW(ping); ping.n := 1; NEW(answer); answer.n := 7;
		responder := ActorCore.Spawn(NIL, ActorCore.NewProps(NewResponder), "");
		Drive(AskBody)
	END Ask;

	(* --- сеть и кодек ----------------------------------------------------- *)

	PROCEDURE NetSend;
		VAR i, len: INTEGER; frame: ARRAY 64 OF BYTE;
	BEGIN
		net := ActorNetMem.New(1); len := LEN(frame);
		FOR i := 0 TO 99 DO net.Send("a", "b", frame, len) END;
		Start;
		FOR i := 1 TO frameOps DO net.Send("a", "b", frame, len) END;
		Report("netmem-frame64", frameOps)
	END NetSend;

	PROCEDURE (s: Small) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		s.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(s.n)
	END Externalize;

	PROCEDURE (s: Small) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		s.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(s.n) END
	END Internalize;

	PROCEDURE Codec;
		VAR s: Small; data: ActorJournal.Bytes; len, i, res: INTEGER; d: Stores.Store;
	BEGIN
		NEW(s); s.n := 5;
		FOR i := 1 TO 100 DO ActorCodec.dir.Encode(s, data, len) END;
		Start;
		FOR i := 1 TO codecOps DO ActorCodec.dir.Encode(s, data, len) END;
		Report("codec-encode", codecOps);
		ActorCodec.dir.Encode(s, data, len);
		FOR i := 1 TO 100 DO ActorCodec.dir.Decode(data, len, d, res) END;
		Start;
		FOR i := 1 TO codecOps DO ActorCodec.dir.Decode(data, len, d, res) END;
		Report("codec-decode", codecOps)
	END Codec;

	(* --- Raft ------------------------------------------------------------- *)

	PROCEDURE Post (m: RaftCore.Msg);
		VAR nx: RaftCore.Msg;
	BEGIN
		WHILE m # NIL DO
			nx := m.next; m.next := NIL;
			IF nodes[m.to] # NIL THEN pool[tail] := m; tail := (tail + 1) MOD poolSize END;
			m := nx
		END
	END Post;

	PROCEDURE Process (i: INTEGER);
		VAR r: RaftCore.Ready; res: INTEGER;
	BEGIN
		WHILE (nodes[i] # NIL) & RaftCore.HasReady(nodes[i]) DO
			RaftCore.GetReady(nodes[i], r);
			IF r.hard OR (r.from <= r.last) OR (r.snap # NIL) THEN disk[i].Save(nodes[i], r, res) END;
			Post(r.msgs);
			RaftCore.Advance(nodes[i], r)
		END
	END Process;

	PROCEDURE Drain;
		VAR m: RaftCore.Msg; end, i: INTEGER;
	BEGIN
		WHILE head # tail DO
			end := tail;
			WHILE head # end DO
				m := pool[head]; pool[head] := NIL; head := (head + 1) MOD poolSize;
				IF nodes[m.to] # NIL THEN RaftCore.Step(nodes[m.to], m) END
			END;
			FOR i := 0 TO raftN - 1 DO Process(i) END
		END
	END Drain;

	PROCEDURE StartNode (i: INTEGER);
		VAR t, v, si, st, last, res: INTEGER; snap: RaftCore.Bytes; log: RaftCore.Entries;
	BEGIN
		disk[i].Load(t, v, si, st, snap, log, last, res);
		nodes[i] := RaftCore.New(i, raftN, 1, 10, t, v, si, st, snap, log, last)
	END StartNode;

	PROCEDURE Leader (): INTEGER;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO raftN - 1 DO
			IF (nodes[i] # NIL) & (nodes[i].role = RaftCore.leader) THEN RETURN i END
		END;
		RETURN -1
	END Leader;

	PROCEDURE Elect;
		VAR i, t: INTEGER;
	BEGIN
		t := 0;
		WHILE ((Leader() < 0) OR (nodes[Leader()].commit < nodes[Leader()].last)) & (t < 10000) DO
			FOR i := 0 TO raftN - 1 DO IF nodes[i] # NIL THEN RaftCore.Tick(nodes[i]); Process(i) END END;
			Drain; INC(t)
		END
	END Elect;

	PROCEDURE Raft;
		VAR i, l, proposed, base, idx: INTEGER; d: RaftCore.Bytes;
	BEGIN
		head := 0; tail := 0;
		FOR i := 0 TO raftN - 1 DO disk[i] := RaftStorage.NewMem(); StartNode(i) END;
		Elect; l := Leader(); base := nodes[l].applied; proposed := 0;
		Start;
		WHILE nodes[l].applied - base < raftOps DO
			WHILE (proposed < raftOps) & (proposed - (nodes[l].applied - base) < 1) DO
				NEW(d, 16); idx := RaftCore.Propose(nodes[l], d); INC(proposed)
			END;
			Process(l); Drain
		END;
		Report("raft-propose-apply", raftOps)
	END Raft;

	(* --- Kv --------------------------------------------------------------- *)

	PROCEDURE Key (i: INTEGER; VAR k: ARRAY OF BYTE);
	BEGIN
		KvPages.PutByte(k, 0, 0); KvPages.PutByte(k, 1, 0);
		KvPages.PutByte(k, 2, i DIV 256); KvPages.PutByte(k, 3, i MOD 256)
	END Key;

	PROCEDURE Kv;
		VAR st: KvStore.Store; db: KvDb.Db; tx: KvDb.Tx; i, res, found: INTEGER;
			k: ARRAY 4 OF BYTE; v: ARRAY 100 OF BYTE; got: KvDb.Bytes;
	BEGIN
		KvLin.Delete("allocops.db"); KvLin.Open("allocops.db", st, res);
		KvDb.Open(st, db, res); ASSERT(res = KvDb.ok, 100);
		tx := db.Begin(TRUE);
		Start;
		FOR i := 0 TO kvOps - 1 DO Key(i, k); tx.Put(k, v) END;
		Report("kv-put", kvOps);
		tx.Commit(res); ASSERT(res = KvDb.ok, 101);

		tx := db.Begin(FALSE); found := 0;
		FOR i := 0 TO kvOps - 1 DO Key(i, k); IF tx.Get(k, got) THEN INC(found) END END;
		tx.Abort;
		tx := db.Begin(FALSE); found := 0;
		Start;
		FOR i := 0 TO kvOps - 1 DO Key(i, k); IF tx.Get(k, got) THEN INC(found) END END;
		Report("kv-get", kvOps);
		tx.Abort;

		Start;
		FOR i := 1 TO commitOps DO
			tx := db.Begin(TRUE); Key(i, k); tx.Put(k, v); tx.Commit(res); ASSERT(res = KvDb.ok, 102)
		END;
		Report("kv-commit-sync", commitOps);
		db.Close; KvLin.Delete("allocops.db")
	END Kv;

	(* --- команда ---------------------------------------------------------- *)

	PROCEDURE Run*;
	(** Без сборки: Kernel.Allocated — живой объём кучи и убывает при
		Collect; с blockCollect он только растёт и даёт байты на операцию. *)
	BEGIN
		Kernel.blockCollect := TRUE;
		Rendezvous; Buffer; Select2; Spawn;
		ActorRun.Start(1);
		Tell; Ask;
		ActorRun.Stop;
		NetSend; Codec; Raft; Kv;
		Kernel.blockCollect := FALSE
	END Run;

END Demo_devAllocOps.
