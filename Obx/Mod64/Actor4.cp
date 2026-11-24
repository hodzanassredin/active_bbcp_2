MODULE ObxActor4;
	IMPORT Services, Tasks, Console, Strings, Stores,
		ActorCore, ActorSup, ActorRun, ActorJournal, ActorPersist;

	TYPE
		Msg = ActorCore.Msg;

		Deposited = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;
		Withdrawn = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;

		Op = POINTER TO RECORD (Msg) deposit: BOOLEAN; sum: INTEGER; no: LONGINT END;
		Crash = POINTER TO RECORD (Msg) END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance: INTEGER;
			snapAt: LONGINT;	(* с какого снимка начато восстановление *)
			replayed: INTEGER	(* сколько событий применено при восстановлении *)
		END;
		Bank = POINTER TO RECORD (ActorCore.Actor) END;
		Clerk = POINTER TO RECORD (ActorCore.Actor) END;

		History = POINTER TO RECORD (ActorPersist.Visitor) END;

		Driver = POINTER TO RECORD (Tasks.Task) END;

	VAR
		acct, anna, boris: ActorCore.Ref;
		seen: LONGINT;	(* номер последнего события, видимый водителю *)
		balance, starts, replayed: INTEGER;
		snapAt: LONGINT;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* --- события и снимок ---------------------------------------------------- *)

	PROCEDURE (e: Deposited) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum)
	END Externalize;

	PROCEDURE (e: Deposited) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(e.sum) END
	END Internalize;

	PROCEDURE (e: Withdrawn) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum)
	END Externalize;

	PROCEDURE (e: Withdrawn) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(e.sum) END
	END Internalize;

	(* снимок — сам счёт: его кадр несёт остаток *)
	PROCEDURE (a: Account) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		a.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(a.balance)
	END Externalize;

	PROCEDURE (a: Account) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		a.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(a.balance) END
	END Internalize;

	PROCEDURE (a: Account) Restore (s: Stores.Store);
	BEGIN
		a.balance := s(Account).balance; a.snapAt := a.seq
	END Restore;

	(* --- счёт ------------------------------------------------------------- *)

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(a.balance, e.sum)
		| e: Withdrawn DO DEC(a.balance, e.sum)
		ELSE
		END;
		INC(a.replayed)
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR d: Deposited; w: Withdrawn;
	BEGIN
		WITH m: ActorPersist.ReplayComplete DO
			snapAt := a.snapAt; replayed := a.replayed; INC(starts)
		| m: Op DO
			IF m.deposit THEN NEW(d); d.sum := m.sum; a.Persist(c, d, m.no)
			ELSE NEW(w); w.sum := m.sum; a.Persist(c, w, m.no)
			END
		| m: Crash DO HALT(42)
		ELSE
		END;
		balance := a.balance; seen := a.seq
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.snapEvery := 4; RETURN a
	END NewAccount;

	PROCEDURE (b: Bank) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS ActorCore.Started THEN acct := ActorCore.Spawn(c, ActorCore.NewProps(NewAccount), "acct") END
	END Receive;

	PROCEDURE NewBank (): ActorCore.Actor;
		VAR b: Bank;
	BEGIN
		NEW(b); RETURN b
	END NewBank;

	(* операционист пересылает команду от своего имени: он и будет автором *)
	PROCEDURE (k: Clerk) Receive (c: ActorCore.Context; m: Msg);
	BEGIN
		IF m IS Op THEN c.Tell(acct, m) END
	END Receive;

	PROCEDURE NewClerk (): ActorCore.Actor;
		VAR k: Clerk;
	BEGIN
		NEW(k); RETURN k
	END NewClerk;

	(* --- история (аудит) ---------------------------------------------------- *)

	PROCEDURE (h: History) Visit (e: ActorPersist.Event);
	BEGIN
		Console.WriteStr("  "); WriteInt(e.meta.seq); Console.WriteStr(" ");
		Console.WriteStr(e.meta.author); Console.WriteStr(" op "); WriteInt(e.meta.cause);
		WITH e: Deposited DO Console.WriteStr(" deposit "); WriteInt(e.sum)
		| e: Withdrawn DO Console.WriteStr(" withdraw "); WriteInt(e.sum)
		ELSE
		END;
		Console.WriteLn
	END Visit;

	(* --- водитель --------------------------------------------------------- *)

	PROCEDURE Send (clerk: ActorCore.Ref; deposit: BOOLEAN; sum: INTEGER; no: LONGINT);
		VAR m: Op; want: LONGINT;
	BEGIN
		NEW(m); m.deposit := deposit; m.sum := sum; m.no := no;
		want := seen + 1; ActorCore.Tell(clerk, m);
		WHILE seen < want DO Tasks.Sleep(1) END	(* по одной операции: история в одном порядке *)
	END Send;

	PROCEDURE (d: Driver) Run;
		VAR i, s, res: INTEGER; crash: Crash; h: History; last: LONGINT;
	BEGIN
		WHILE (acct = NIL) OR (starts = 0) DO Tasks.Sleep(1) END;
		Send(anna, TRUE, 100, 101); Send(boris, TRUE, 50, 102);
		FOR i := 1 TO 4 DO Send(anna, FALSE, 10, 102 + i) END;
		Send(boris, TRUE, 25, 107); Send(boris, FALSE, 5, 108); Send(anna, FALSE, 20, 109);
		Send(boris, TRUE, 1, 110);
		Console.WriteStr("account: 10 operations, balance "); WriteInt(balance); Console.WriteLn;
		s := starts; NEW(crash); ActorCore.Tell(acct, crash);
		WHILE starts = s DO Tasks.Sleep(1) END;
		Console.WriteStr("account: restarted from the snapshot after event "); WriteInt(snapAt);
		Console.WriteStr(", replayed "); WriteInt(replayed);
		Console.WriteStr(", balance "); WriteInt(balance); Console.WriteLn;
		Console.WriteStr("account: history"); Console.WriteLn;
		NEW(h); ActorPersist.Read(ActorJournal.dir, "bank/acct", 1, h, last, res);
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; d: Driver;
	BEGIN
		ActorJournal.SetDir(ActorJournal.NewMem());	(* чистый журнал на каждый запуск *)
		acct := NIL; seen := 0; starts := 0;
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		p := ActorCore.NewProps(NewBank); p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		IF ActorCore.Spawn(NIL, p, "bank") = NIL THEN END;
		anna := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClerk), "anna");
		boris := ActorCore.Spawn(NIL, ActorCore.NewProps(NewClerk), "boris");
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor4.
