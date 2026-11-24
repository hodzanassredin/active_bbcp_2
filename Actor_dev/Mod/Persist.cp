MODULE Actor_devPersist;
(** Тесты персистентности акторов: журнал, кодек, восстановление, снимки,
	конфликт версий, оборванный хвост файла, повтор команды, читатель.

	Счёт "bank/acct" — ребёнок супервизора "bank", который перезапускает
	его после любого трапа, кроме decodeTrap (тогда — остановка). Команды
	шлются Tell, ответы — через глобальные переменные. *)

	IMPORT Kernel, Services, Files, Stores, CasStore,
		ActorCore, ActorRun, ActorJournal, ActorCodec, ActorPersist, ActorFileJournal,
		KvStore, KvDb, KvFault, KvLin, ActorKvJournal, UnitAssert;

	CONST
		stream = "bank/acct";
		maxOps = 8;	(* помнятся последние maxOps номеров операций *)

	TYPE
		Msg = ActorCore.Msg;

		(* события *)
		Deposited = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;
		Withdrawn = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER; op: LONGINT END;
		Versioned = POINTER TO RECORD (ActorPersist.Event) a, b: INTEGER END;

		(* команды *)
		Deposit = POINTER TO RECORD (Msg) sum: INTEGER; boomBefore, boomAfter: BOOLEAN END;
		Withdraw = POINTER TO RECORD (Msg) sum: INTEGER; op: LONGINT END;
		Get = POINTER TO RECORD (Msg) END;
		Snap = POINTER TO RECORD (Msg) END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance: INTEGER;
			ops: ARRAY maxOps OF LONGINT; nOps: INTEGER
		END;

		Bank = POINTER TO RECORD (ActorCore.Actor) END;
		Clerk = POINTER TO RECORD (ActorCore.Actor) END;	(* пересылает команды от своего имени *)
		Strategy = POINTER TO RECORD (ActorCore.Strategy) END;

		Sum = POINTER TO RECORD (ActorPersist.Visitor)
			deposits, withdrawals, events: INTEGER;
			lastSeq, lastCause: LONGINT; author: ActorJournal.Name; time: LONGINT
		END;

		Raw = POINTER TO RECORD (ActorJournal.Sink) n: INTEGER END;

	VAR
		jr: ActorJournal.Journal;
		snaps: CasStore.Store;
		snapEvery: INTEGER;
		acct: ActorCore.Ref;
		replays, applied, restores, handled, reason, firstMsg: INTEGER;
		bal: INTEGER; got: BOOLEAN;
		writeVer: INTEGER;

	(* --- обвязка ---------------------------------------------------------- *)

	PROCEDURE Until (VAR flag: BOOLEAN; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE ~flag & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN flag
	END Until;

	PROCEDURE UntilInt (VAR x: INTEGER; want: INTEGER; ms: LONGINT): BOOLEAN;
		VAR t: LONGINT;
	BEGIN
		t := Kernel.Time();
		WHILE (x < want) & (Kernel.Time() - t <= ms) DO Services.actionHook.Step END;
		RETURN x >= want
	END UntilInt;

	PROCEDURE Idle (ms: LONGINT);
		VAR never: BOOLEAN;
	BEGIN
		never := FALSE; IF Until(never, ms) THEN END
	END Idle;

	(* --- события ---------------------------------------------------------- *)

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

	PROCEDURE (e: Withdrawn) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum); wr.WriteLong(e.op)
	END Externalize;

	PROCEDURE (e: Withdrawn) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.sum); rd.ReadLong(e.op)
	END Internalize;

	(* Правило версий: версия 1 добавила поле b; кадр версии 0 читается
	   по старой раскладке, b = -1. *)
	PROCEDURE (e: Versioned) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(writeVer); wr.WriteInt(e.a);
		IF writeVer >= 1 THEN wr.WriteInt(e.b) END
	END Externalize;

	PROCEDURE (e: Versioned) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 1, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(e.a);
		IF v >= 1 THEN rd.ReadInt(e.b) ELSE e.b := -1 END
	END Internalize;

	PROCEDURE NewDeposited (sum: INTEGER): Deposited;
		VAR e: Deposited;
	BEGIN
		NEW(e); e.sum := sum; RETURN e
	END NewDeposited;

	(* --- счёт ------------------------------------------------------------- *)

	PROCEDURE (a: Account) Done (op: LONGINT): BOOLEAN, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < MIN(a.nOps, maxOps)) & (a.ops[i] # op) DO INC(i) END;
		RETURN i < MIN(a.nOps, maxOps)
	END Done;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		INC(applied);
		WITH e: Deposited DO INC(a.balance, e.sum)
		| e: Withdrawn DO
			DEC(a.balance, e.sum);
			a.ops[a.nOps MOD maxOps] := e.op; INC(a.nOps)	(* отсечение: новый номер вытесняет самый старый *)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
		VAR i: INTEGER;
	BEGIN
		a.Externalize^(wr); wr.WriteVersion(0);
		wr.WriteInt(a.balance); wr.WriteInt(a.nOps);
		FOR i := 0 TO MIN(a.nOps, maxOps) - 1 DO wr.WriteLong(a.ops[i]) END
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v, i: INTEGER;
	BEGIN
		a.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF rd.cancelled THEN RETURN END;
		rd.ReadInt(a.balance); rd.ReadInt(a.nOps);
		FOR i := 0 TO MIN(a.nOps, maxOps) - 1 DO rd.ReadLong(a.ops[i]) END
	END Internalize;

	PROCEDURE (a: Account) Restore (s: Stores.Store);
		VAR i: INTEGER;
	BEGIN
		INC(restores);
		WITH s: Account DO
			a.balance := s.balance; a.nOps := s.nOps;
			FOR i := 0 TO MIN(a.nOps, maxOps) - 1 DO a.ops[i] := s.ops[i] END
		END
	END Restore;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR w: Withdrawn;
	BEGIN
		IF firstMsg = 0 THEN IF m IS ActorPersist.ReplayComplete THEN firstMsg := 1 ELSE firstMsg := 2 END END;
		WITH m: ActorPersist.ReplayComplete DO INC(replays)
		| m: Deposit DO
			IF m.boomBefore THEN HALT(42) END;
			a.Persist(c, NewDeposited(m.sum), 0);
			IF m.boomAfter THEN HALT(42) END
		| m: Withdraw DO
			IF ~a.Done(m.op) THEN	(* повтор той же операции — без второго списания *)
				NEW(w); w.sum := m.sum; w.op := m.op; a.Persist(c, w, m.op)
			END
		| m: Snap DO a.SaveSnapshot(c)
		| m: Get DO bal := a.balance; got := TRUE
		ELSE
		END;
		INC(handled)
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.journal := jr; a.snapEvery := snapEvery; RETURN a
	END NewAccount;

	PROCEDURE (b: Bank) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS ActorCore.Started THEN acct := ActorCore.Spawn(c, ActorCore.NewProps(NewAccount), "acct") END
	END Receive;

	PROCEDURE (k: Clerk) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS Withdraw THEN c.Tell(acct, m) END
	END Receive;

	PROCEDURE NewClerk (): ActorCore.Actor;
		VAR k: Clerk;
	BEGIN
		NEW(k); RETURN k
	END NewClerk;

	PROCEDURE NewBank (): ActorCore.Actor;
		VAR b: Bank;
	BEGIN
		NEW(b); RETURN b
	END NewBank;

	PROCEDURE (s: Strategy) Decide (child: ActorCore.Ref; r: INTEGER; VAR st: ActorCore.Stats): INTEGER;
	BEGIN
		reason := r;
		IF r = ActorPersist.decodeTrap THEN RETURN ActorCore.Stop ELSE RETURN ActorCore.Restart END
	END Decide;

	PROCEDURE StartBankNoWait;
		VAR p: ActorCore.Props; s: Strategy;
	BEGIN
		acct := NIL; replays := 0;
		NEW(s); p := ActorCore.NewProps(NewBank); p.strategy := s;
		IF ActorCore.Spawn(NIL, p, "bank") = NIL THEN END
	END StartBankNoWait;

	PROCEDURE StartBank;
	BEGIN
		StartBankNoWait;
		UnitAssert.True(UntilInt(replays, 1, 2000), "account recovered")
	END StartBank;

	PROCEDURE NewGet (): Get;
		VAR g: Get;
	BEGIN
		NEW(g); RETURN g
	END NewGet;

	PROCEDURE Balance (): INTEGER;
	BEGIN
		got := FALSE; ActorCore.Tell(acct, NewGet());
		UnitAssert.True(Until(got, 2000), "balance answered");
		RETURN bal
	END Balance;

	PROCEDURE Dep (sum: INTEGER; before, after: BOOLEAN);
		VAR d: Deposit;
	BEGIN
		NEW(d); d.sum := sum; d.boomBefore := before; d.boomAfter := after; ActorCore.Tell(acct, d)
	END Dep;

	PROCEDURE Wd (sum: INTEGER; op: LONGINT);
		VAR w: Withdraw;
	BEGIN
		NEW(w); w.sum := sum; w.op := op; ActorCore.Tell(acct, w)
	END Wd;

	PROCEDURE Restart;
	(* Пост: счёт упал по трапу 42 и восстановлен супервизором. *)
	BEGIN
		replays := 0; Dep(0, TRUE, FALSE);
		UnitAssert.True(UntilInt(replays, 1, 2000), "restarted and recovered")
	END Restart;

	PROCEDURE StopBank;
		VAR r: ActorCore.Ref; t: LONGINT;
	BEGIN
		r := ActorCore.dir.Lookup(ActorCore.node, "bank");
		IF r # NIL THEN
			ActorCore.StopActor(r); t := Kernel.Time();
			WHILE (ActorCore.dir.Lookup(ActorCore.node, "bank") # NIL) & (Kernel.Time() - t < 2000) DO
				Services.actionHook.Step
			END
		END
	END StopBank;

	(* --- читатель ---------------------------------------------------------- *)

	PROCEDURE (s: Sum) Visit (e: ActorPersist.Event);
	BEGIN
		INC(s.events);
		WITH e: Deposited DO INC(s.deposits, e.sum)
		| e: Withdrawn DO INC(s.withdrawals, e.sum)
		ELSE
		END;
		s.lastSeq := e.meta.seq; s.lastCause := e.meta.cause; s.author := e.meta.author$; s.time := e.meta.time
	END Visit;

	PROCEDURE (r: Raw) Event (IN m: ActorJournal.Meta; IN body: ARRAY OF BYTE; len: INTEGER): BOOLEAN;
	BEGIN
		INC(r.n); RETURN TRUE
	END Event;

	PROCEDURE Frame (e: Stores.Store; OUT d: ActorJournal.Bytes; OUT len: INTEGER);
	BEGIN
		ActorCodec.dir.Encode(e, d, len)
	END Frame;

	PROCEDURE AppendRaw (j: ActorJournal.Journal; IN s: ARRAY OF CHAR; e: Stores.Store; OUT res: INTEGER);
	(* Пост: e дописано в поток s мимо актора. *)
		VAR m: ActorJournal.Meta; d: ActorJournal.Bytes; len: INTEGER;
	BEGIN
		Frame(e, d, len);
		m.seq := j.Last(s) + 1; m.time := ActorJournal.Now(); m.cause := 0; m.author := "test";
		j.Append(s, m.seq - 1, m, d, len, res)
	END AppendRaw;

	(* --- фикстуры --------------------------------------------------------- *)

	PROCEDURE Setup*;
	BEGIN
		jr := ActorJournal.NewMem(); snaps := CasStore.NewMem(); snapEvery := 0; acct := NIL;
		replays := 0; applied := 0; restores := 0; handled := 0; reason := 0; firstMsg := 0;
		writeVer := 0;
		ActorRun.Start(2)
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		ActorRun.Shutdown; Idle(5);
		IF jr # NIL THEN jr.Close END
	END Teardown;


	(* --- журнал и кодек ---------------------------------------------------- *)

	PROCEDURE TestAppendConflictWritesNothing*;
		VAR res: INTEGER; m: ActorJournal.Meta; d: ActorJournal.Bytes; len: INTEGER; r: Raw;
	BEGIN
		AppendRaw(jr, "s", NewDeposited(1), res);
		UnitAssert.EqualInt(ActorJournal.ok, res, "first append");
		Frame(NewDeposited(2), d, len);
		m.seq := 1; m.author := "other";
		jr.Append("s", 0, m, d, len, res);	(* ожидали пустой поток *)
		UnitAssert.EqualInt(ActorJournal.conflict, res, "stale expected gives conflict");
		UnitAssert.EqualInt(1, jr.Last("s"), "nothing written on conflict");
		NEW(r); jr.Read("s", 1, r, res);
		UnitAssert.EqualInt(1, r.n, "one event in the stream")
	END TestAppendConflictWritesNothing;

	PROCEDURE TestVersionRule*;
		VAR v: Versioned; d: ActorJournal.Bytes; len, res: INTEGER; s: Stores.Store;
	BEGIN
		NEW(v); v.a := 5; v.b := 6;
		writeVer := 0; Frame(v, d, len);
		ActorCodec.dir.Decode(d, len, s, res);
		UnitAssert.EqualInt(ActorCodec.ok, res, "old version reads");
		UnitAssert.EqualInt(5, s(Versioned).a, "old field");
		UnitAssert.EqualInt(-1, s(Versioned).b, "new field gets its default");
		writeVer := 1; Frame(v, d, len);
		ActorCodec.dir.Decode(d, len, s, res);
		UnitAssert.EqualInt(6, s(Versioned).b, "current version reads all fields");
		writeVer := 2; Frame(v, d, len);
		ActorCodec.dir.Decode(d, len, s, res);
		UnitAssert.EqualInt(ActorCodec.unknown, res, "future version is unknown, not a trap");
		UnitAssert.True(s = NIL, "no object for unknown")
	END TestVersionRule;

	PROCEDURE Rename (VAR d: ActorJournal.Bytes; len: INTEGER): BOOLEAN;
	(* Пост: в кадре имя типа "Deposited" заменено на "Depositex". *)
		VAR i, k: INTEGER; pat: ARRAY 16 OF CHAR; ok: BOOLEAN;
	BEGIN
		pat := "Deposited";
		FOR i := 0 TO len - 9 DO
			ok := TRUE;
			FOR k := 0 TO 8 DO IF d[i + k] # SHORT(ORD(pat[k])) THEN ok := FALSE END END;
			IF ok THEN d[i + 8] := SHORT(ORD("x")); RETURN TRUE END
		END;
		RETURN FALSE
	END Rename;

	PROCEDURE TestUnknownEventType*;
		VAR d: ActorJournal.Bytes; len, res: INTEGER; s: Stores.Store; m: ActorJournal.Meta;
			sum: Sum; last: LONGINT;
	BEGIN
		Frame(NewDeposited(7), d, len);
		UnitAssert.True(Rename(d, len), "type name found in the frame");
		ActorCodec.dir.Decode(d, len, s, res);
		UnitAssert.EqualInt(ActorCodec.unknown, res, "unknown type gives res, not a trap");
		m.seq := 1; m.author := "test";
		jr.Append(stream, 0, m, d, len, res);
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualInt(ActorJournal.corrupt, res, "reader reports an unreadable frame");
		UnitAssert.EqualInt(0, last, "nothing read");
		StartBankNoWait;
		UnitAssert.True(UntilInt(reason, 1, 2000), "account failed");
		UnitAssert.EqualInt(ActorPersist.decodeTrap, reason, "recovery fails with decodeTrap")
	END TestUnknownEventType;

	(* --- восстановление ---------------------------------------------------- *)

	PROCEDURE TestRecoverSameState*;
	BEGIN
		StartBank;
		UnitAssert.EqualInt(1, firstMsg, "ReplayComplete comes first");
		Dep(10, FALSE, FALSE); Dep(20, FALSE, FALSE); Wd(5, 1);
		UnitAssert.EqualInt(25, Balance(), "balance before restart");
		Restart;
		UnitAssert.EqualInt(25, Balance(), "same balance after restart");
		StopBank; StartBank;
		UnitAssert.EqualInt(25, Balance(), "same balance in a new actor")
	END TestRecoverSameState;

	PROCEDURE TestCommandsWaitForReplay*;
		VAR i, res: INTEGER;
	BEGIN
		FOR i := 1 TO 2000 DO AppendRaw(jr, stream, NewDeposited(1), res) END;
		StartBankNoWait;
		WHILE acct = NIL DO Services.actionHook.Step END;
		got := FALSE; ActorCore.Tell(acct, NewGet());	(* письмо приходит во время восстановления *)
		UnitAssert.True(Until(got, 5000), "answered");
		UnitAssert.EqualInt(1, firstMsg, "ReplayComplete before the command");
		UnitAssert.EqualInt(2000, bal, "command saw the recovered state")
	END TestCommandsWaitForReplay;

	PROCEDURE TestSnapshotPlusTailEqualsFull*;
		VAR i, res: INTEGER; sum: Sum; last: LONGINT; seq: LONGINT; d: ActorJournal.Bytes; len: INTEGER;
	BEGIN
		snapEvery := 3; StartBank;
		FOR i := 1 TO 7 DO Dep(i, FALSE, FALSE) END;
		UnitAssert.EqualInt(28, Balance(), "balance");
		jr.LoadSnapshot(stream, seq, d, len, res);
		UnitAssert.EqualInt(ActorJournal.ok, res, "snapshot saved");
		UnitAssert.EqualInt(6, seq, "snapshot after event 6");
		applied := 0; restores := 0;
		Restart;
		UnitAssert.EqualInt(1, restores, "recovery used the snapshot");
		UnitAssert.EqualInt(1, applied, "only the tail replayed");
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualInt(sum.deposits, Balance(), "snapshot + tail = replay from zero")
	END TestSnapshotPlusTailEqualsFull;

	PROCEDURE TestTrapAfterPersistKeepsEvent*;
	BEGIN
		StartBank;
		Dep(10, FALSE, FALSE);
		replays := 0; Dep(5, FALSE, TRUE);
		UnitAssert.True(UntilInt(replays, 1, 2000), "restarted");
		UnitAssert.EqualInt(42, reason, "failed with the handler's trap");
		UnitAssert.EqualInt(15, Balance(), "event written before the trap survives")
	END TestTrapAfterPersistKeepsEvent;

	PROCEDURE TestTrapBeforePersistLosesEvent*;
	BEGIN
		StartBank;
		Dep(10, FALSE, FALSE);
		replays := 0; Dep(5, TRUE, FALSE);
		UnitAssert.True(UntilInt(replays, 1, 2000), "restarted");
		UnitAssert.EqualInt(10, Balance(), "no event, no change");
		UnitAssert.EqualInt(1, jr.Last(stream), "journal has one event")
	END TestTrapBeforePersistLosesEvent;

	PROCEDURE TestConflictFailsActor*;
		VAR res: INTEGER;
	BEGIN
		StartBank;
		Dep(10, FALSE, FALSE);
		UnitAssert.EqualInt(10, Balance(), "balance");
		AppendRaw(jr, stream, NewDeposited(100), res);	(* второй писатель *)
		UnitAssert.EqualInt(ActorJournal.ok, res, "foreign append");
		replays := 0; Dep(1, FALSE, FALSE);
		UnitAssert.True(UntilInt(replays, 1, 2000), "restarted after conflict");
		UnitAssert.EqualInt(ActorPersist.conflictTrap, reason, "conflict is the failure reason");
		UnitAssert.EqualInt(110, Balance(), "recovery sees the foreign event, the command is not applied");
		UnitAssert.EqualInt(2, jr.Last(stream), "conflicting event not written")
	END TestConflictFailsActor;

	PROCEDURE TestIdempotentRetry*;
		VAR res: INTEGER; sum: Sum; last: LONGINT;
	BEGIN
		StartBank;
		Dep(100, FALSE, FALSE);
		Wd(30, 77); Wd(30, 77);
		UnitAssert.EqualInt(70, Balance(), "withdrawn once");
		Restart;
		Wd(30, 77);
		UnitAssert.EqualInt(70, Balance(), "retry after restart withdraws nothing");
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualInt(30, sum.withdrawals, "one withdrawal event in the journal")
	END TestIdempotentRetry;

	PROCEDURE TestReaderCatchesUp*;
		VAR res: INTEGER; sum: Sum; last: LONGINT;
	BEGIN
		StartBank;
		Dep(1, FALSE, FALSE); Dep(2, FALSE, FALSE); Dep(3, FALSE, FALSE);
		IF Balance() = 6 THEN END;
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualInt(3, last, "reader at 3");
		Dep(4, FALSE, FALSE); Wd(5, 9);
		IF Balance() = 5 THEN END;
		ActorPersist.Read(jr, stream, last + 1, sum, last, res);
		UnitAssert.EqualInt(5, last, "reader caught up");
		UnitAssert.EqualInt(5, sum.events, "each event seen once");
		UnitAssert.EqualInt(10, sum.deposits, "deposits");
		UnitAssert.EqualInt(5, sum.withdrawals, "withdrawals");
		ActorPersist.Read(jr, stream, last + 1, sum, last, res);
		UnitAssert.EqualInt(5, last, "nothing new")
	END TestReaderCatchesUp;

	PROCEDURE TestMetadataReadable*;
		VAR res: INTEGER; sum: Sum; last: LONGINT;
	BEGIN
		StartBank;
		Wd(1, 4242);
		IF Balance() = -1 THEN END;
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualStr(stream, sum.author, "author is the actor");
		UnitAssert.EqualInt(4242, sum.lastCause, "cause is the operation number");
		UnitAssert.EqualInt(1, sum.lastSeq, "seq");
		UnitAssert.True(sum.time > 20000101000000, "time written")
	END TestMetadataReadable;

	PROCEDURE TestAuthorIsSender*;
		VAR res: INTEGER; sum: Sum; last: LONGINT; k: ActorCore.Ref; w: Withdraw;
	BEGIN
		StartBank;
		k := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClerk), "clerk");
		NEW(w); w.sum := 1; w.op := 5; ActorCore.Tell(k, w);
		UnitAssert.True(UntilInt(handled, 2, 2000), "command handled");
		NEW(sum); ActorPersist.Read(jr, stream, 1, sum, last, res);
		UnitAssert.EqualStr("clerk", sum.author, "author is the sender of the command")
	END TestAuthorIsSender;

	(* --- файл ---------------------------------------------------------------- *)

	PROCEDURE FileLoc (): Files.Locator;
	BEGIN
		RETURN Files.dir.This("/tmp/actor_dev_persist")
	END FileLoc;

	PROCEDURE OpenFile (IN name: Files.Name): ActorJournal.Journal;
		VAR j: ActorJournal.Journal; res: INTEGER;
	BEGIN
		j := ActorFileJournal.Open(FileLoc(), name, snaps, res);
		UnitAssert.EqualInt(ActorJournal.ok, res, "file opened");
		RETURN j
	END OpenFile;

	PROCEDURE Spoil (IN name: Files.Name; pos: INTEGER; IN b: ARRAY OF BYTE; len: INTEGER);
	(* Пост: в файл name с позиции pos (-1 — в конец) записаны b[0 .. len-1]. *)
		VAR f: Files.File; w: Files.Writer;
	BEGIN
		f := Files.dir.Old(FileLoc(), name, Files.exclusive);
		UnitAssert.True(f # NIL, "file exists");
		w := f.NewWriter(NIL);
		IF pos < 0 THEN w.SetPos(f.Length()) ELSE w.SetPos(pos) END;
		w.WriteBytes(b, 0, len); f.Flush; f.Close
	END Spoil;

	PROCEDURE FileLen (IN name: Files.Name): INTEGER;
		VAR f: Files.File; n: INTEGER;
	BEGIN
		f := Files.dir.Old(FileLoc(), name, Files.shared); n := f.Length(); f.Close; RETURN n
	END FileLen;

	PROCEDURE TestFileSurvivesReopen*;
		VAR i, res: INTEGER; sum: Sum; last: LONGINT;
	BEGIN
		Files.dir.Delete(FileLoc(), "reopen.jrn");
		jr := OpenFile("reopen.jrn");
		FOR i := 1 TO 3 DO AppendRaw(jr, "a", NewDeposited(i), res) END;
		AppendRaw(jr, "b", NewDeposited(100), res);
		jr.Close; jr := OpenFile("reopen.jrn");
		UnitAssert.EqualInt(3, jr.Last("a"), "stream a");
		UnitAssert.EqualInt(1, jr.Last("b"), "stream b");
		NEW(sum); ActorPersist.Read(jr, "a", 1, sum, last, res);
		UnitAssert.EqualInt(6, sum.deposits, "events read back");
		UnitAssert.EqualStr("test", sum.author, "metadata read back")
	END TestFileSurvivesReopen;

	PROCEDURE TestTornTailDropped*;
		VAR i, res, len1: INTEGER; junk: ARRAY 8 OF BYTE; sum: Sum; last: LONGINT;
	BEGIN
		Files.dir.Delete(FileLoc(), "torn.jrn");
		jr := OpenFile("torn.jrn");
		FOR i := 1 TO 3 DO AppendRaw(jr, "a", NewDeposited(i), res) END;
		jr.Close; len1 := FileLen("torn.jrn");
		junk[0] := -5BH; junk[1] := 100; junk[2] := 0; junk[3] := 0; junk[4] := 0; junk[5] := 1; junk[6] := 2;
		Spoil("torn.jrn", -1, junk, 7);	(* заголовок записи без тела: обрыв при записи *)
		jr := OpenFile("torn.jrn");
		UnitAssert.EqualInt(3, jr.Last("a"), "torn tail dropped");
		AppendRaw(jr, "a", NewDeposited(10), res);
		UnitAssert.EqualInt(ActorJournal.ok, res, "append after the dropped tail");
		jr.Close; jr := OpenFile("torn.jrn");
		UnitAssert.EqualInt(4, jr.Last("a"), "new event replaced the tail");
		NEW(sum); ActorPersist.Read(jr, "a", 1, sum, last, res);
		UnitAssert.EqualInt(16, sum.deposits, "all good events");
		jr.Close;
		junk[0] := -1; Spoil("torn.jrn", FileLen("torn.jrn") - 5, junk, 1);	(* порча тела последней записи *)
		jr := OpenFile("torn.jrn");
		UnitAssert.EqualInt(3, jr.Last("a"), "record with a bad checksum dropped");
		UnitAssert.True(len1 > 0, "file had data")
	END TestTornTailDropped;

	PROCEDURE TestFileActorRecovers*;
	BEGIN
		Files.dir.Delete(FileLoc(), "acct.jrn");
		jr := OpenFile("acct.jrn"); snapEvery := 2;
		StartBank;
		Dep(10, FALSE, FALSE); Dep(20, FALSE, FALSE); Dep(30, FALSE, FALSE);
		UnitAssert.EqualInt(60, Balance(), "balance");
		StopBank; jr.Close; jr := OpenFile("acct.jrn");
		restores := 0; applied := 0;
		StartBank;
		UnitAssert.EqualInt(60, Balance(), "same balance from the reopened file");
		UnitAssert.EqualInt(1, restores, "snapshot from the Cas store");
		UnitAssert.EqualInt(1, applied, "tail after the snapshot")
	END TestFileActorRecovers;

	PROCEDURE TestOldOperationsForgotten*;
	(* Таблица повторов помнит последние maxOps номеров: повтор свежей
	   операции не списывает, повтор вытесненной — списывает снова. *)
		VAR i: INTEGER;
	BEGIN
		snapEvery := 3; StartBank;
		Dep(1000, FALSE, FALSE);
		FOR i := 1 TO maxOps + 1 DO Wd(1, i) END;
		UnitAssert.EqualInt(1000 - maxOps - 1, Balance(), "each operation once");
		Restart;
		Wd(1, maxOps + 1);
		UnitAssert.EqualInt(1000 - maxOps - 1, Balance(), "recent retry withdraws nothing");
		Wd(1, 1);
		UnitAssert.EqualInt(1000 - maxOps - 2, Balance(), "forgotten operation withdraws again")
	END TestOldOperationsForgotten;

	(* --- журнал в Kv ------------------------------------------------------------ *)

	PROCEDURE KvOn (st: KvStore.Store): ActorJournal.Journal;
		VAR db: KvDb.Db; res: INTEGER;
	BEGIN
		KvDb.Open(st, db, res);
		UnitAssert.EqualInt(KvDb.ok, res, "kv opened");
		RETURN ActorKvJournal.New(db)
	END KvOn;

	PROCEDURE TestKvConflictWritesNothing*;
		VAR res: INTEGER; m: ActorJournal.Meta; d: ActorJournal.Bytes; len: INTEGER; r: Raw;
	BEGIN
		jr := KvOn(KvFault.New());
		AppendRaw(jr, "s", NewDeposited(1), res);
		UnitAssert.EqualInt(ActorJournal.ok, res, "first append");
		Frame(NewDeposited(2), d, len); m.seq := 1; m.author := "other";
		jr.Append("s", 0, m, d, len, res);
		UnitAssert.EqualInt(ActorJournal.conflict, res, "stale expected gives conflict");
		UnitAssert.EqualInt(1, jr.Last("s"), "nothing written on conflict");
		AppendRaw(jr, "s2", NewDeposited(5), res);	(* поток, чьё имя продолжает имя "s" *)
		UnitAssert.EqualInt(1, jr.Last("s"), "neighbour stream does not move the last number");
		NEW(r); jr.Read("s", 1, r, res);
		UnitAssert.EqualInt(1, r.n, "one event in the stream")
	END TestKvConflictWritesNothing;

	PROCEDURE TestKvActorRecovers*;
		VAR st: KvFault.Store; i: INTEGER;
	BEGIN
		st := KvFault.New(); jr := KvOn(st); snapEvery := 3;
		StartBank;
		FOR i := 1 TO 7 DO Dep(i, FALSE, FALSE) END;
		Wd(5, 1); Wd(5, 1);
		UnitAssert.EqualInt(23, Balance(), "balance");
		StopBank; jr.Close; st.PowerOff(0); jr := KvOn(st);
		restores := 0; applied := 0;
		StartBank;
		UnitAssert.EqualInt(23, Balance(), "same balance after power off and reopen");
		UnitAssert.EqualInt(1, restores, "snapshot from Kv");
		UnitAssert.EqualInt(2, applied, "tail after the snapshot");
		Wd(5, 1);
		UnitAssert.EqualInt(23, Balance(), "retry after reopen withdraws nothing")
	END TestKvActorRecovers;

	PROCEDURE TestKvConflictFailsActor*;
		VAR res: INTEGER;
	BEGIN
		jr := KvOn(KvFault.New());
		StartBank;
		Dep(10, FALSE, FALSE);
		UnitAssert.EqualInt(10, Balance(), "balance");
		AppendRaw(jr, stream, NewDeposited(100), res);
		replays := 0; Dep(1, FALSE, FALSE);
		UnitAssert.True(UntilInt(replays, 1, 2000), "restarted after conflict");
		UnitAssert.EqualInt(ActorPersist.conflictTrap, reason, "conflict is the failure reason");
		UnitAssert.EqualInt(110, Balance(), "recovery sees the foreign event")
	END TestKvConflictFailsActor;

	PROCEDURE TestKvTwoActorsOneBase*;
		VAR solo, bank: ActorCore.Ref; sum: Sum; last: LONGINT; res: INTEGER;
	BEGIN
		jr := KvOn(KvFault.New());
		StartBank; bank := acct;
		replays := 0;
		solo := ActorCore.Spawn(NIL, ActorCore.NewProps(NewAccount), "solo");
		UnitAssert.True(UntilInt(replays, 1, 2000), "solo recovered");
		acct := solo; Dep(7, FALSE, FALSE); Dep(8, FALSE, FALSE);
		acct := bank; Dep(100, FALSE, FALSE);
		UnitAssert.EqualInt(100, Balance(), "bank account");
		acct := solo; UnitAssert.EqualInt(15, Balance(), "solo account");
		UnitAssert.EqualInt(2, jr.Last("solo"), "solo stream");
		UnitAssert.EqualInt(1, jr.Last(stream), "bank stream");
		NEW(sum); ActorPersist.Read(jr, "solo", 1, sum, last, res);
		UnitAssert.EqualInt(15, sum.deposits, "streams do not mix")
	END TestKvTwoActorsOneBase;

	PROCEDURE TestKvTornAppend*;
	(* Обрыв питания в каждой точке записи третьего события: после открытия
	   поток равен «до» (2 события) или «после» (3), и все события читаются. *)
		VAR st: KvFault.Store; k, res, before, after: INTEGER; sum: Sum; last: LONGINT; done: BOOLEAN;
	BEGIN
		k := 0; before := 0; after := 0; done := FALSE;
		WHILE ~done & (k < 200) DO
			st := KvFault.New(); jr := KvOn(st);
			AppendRaw(jr, "a", NewDeposited(1), res); AppendRaw(jr, "a", NewDeposited(2), res);
			st.Arm(k, k MOD 100);
			AppendRaw(jr, "a", NewDeposited(4), res);
			done := res = ActorJournal.ok;	(* ok — событие зафиксировано: оно обязано пережить питание *)
			jr.Close; st.PowerOff(k + 1); jr := KvOn(st);
			NEW(sum); ActorPersist.Read(jr, "a", 1, sum, last, res);
			UnitAssert.EqualInt(ActorJournal.ok, res, "stream readable");
			IF done THEN UnitAssert.EqualInt(3, last, "append answered ok: the event survived") END;
			IF last = 2 THEN INC(before); UnitAssert.EqualInt(3, sum.deposits, "before")
			ELSE INC(after); UnitAssert.EqualInt(3, last, "after"); UnitAssert.EqualInt(7, sum.deposits, "after")
			END;
			jr.Close; jr := NIL; INC(k)
		END;
		UnitAssert.True(done, "write finished without a fault in the end");
		UnitAssert.True(before > 0, "some faults leave the stream as before")
	END TestKvTornAppend;

	PROCEDURE TestKvFileReopen*;
		VAR st: KvStore.Store; res, i: INTEGER; sum: Sum; last: LONGINT;
	BEGIN
		KvLin.Delete("/tmp/actor_dev_kv.db");
		KvLin.Open("/tmp/actor_dev_kv.db", st, res); jr := KvOn(st);
		FOR i := 1 TO 3 DO AppendRaw(jr, "a", NewDeposited(i), res) END;
		jr.Close;
		KvLin.Open("/tmp/actor_dev_kv.db", st, res); jr := KvOn(st);
		UnitAssert.EqualInt(3, jr.Last("a"), "events kept in the file");
		NEW(sum); ActorPersist.Read(jr, "a", 1, sum, last, res);
		UnitAssert.EqualInt(6, sum.deposits, "events read back");
		UnitAssert.EqualStr("test", sum.author, "metadata read back")
	END TestKvFileReopen;

END Actor_devPersist.
