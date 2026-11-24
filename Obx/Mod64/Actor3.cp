MODULE ObxActor3;
	IMPORT Services, Tasks, Console, Strings, Stores, Files, CasStore,
		ActorCore, ActorSup, ActorRun, ActorJournal, ActorPersist, ActorFileJournal;

	CONST
		dir = "/tmp/obxactor3";
		file = "account.jrn";

	TYPE
		Msg = ActorCore.Msg;

		(* события: что уже случилось со счётом *)
		Deposited = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;
		Withdrawn = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER; op: LONGINT END;

		(* команды: что счёт просят сделать *)
		Deposit = POINTER TO RECORD (Msg) sum: INTEGER END;
		Withdraw = POINTER TO RECORD (Msg) sum: INTEGER; op: LONGINT END;
		Crash = POINTER TO RECORD (Msg) END;
		GetBalance = POINTER TO RECORD (Msg) END;
		Balance = POINTER TO RECORD (Msg) n: INTEGER END;

		Account = POINTER TO RECORD (ActorPersist.Actor)
			balance: INTEGER;
			ops: ARRAY 16 OF LONGINT; nOps: INTEGER	(* выполненные списания *)
		END;
		Bank = POINTER TO RECORD (ActorCore.Actor) END;

		Driver = POINTER TO RECORD (Tasks.Task) END;

	VAR
		journal: ActorJournal.Journal;
		acct: ActorCore.Ref;
		starts: INTEGER;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* --- события ---------------------------------------------------------- *)

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
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum); wr.WriteLong(e.op)
	END Externalize;

	PROCEDURE (e: Withdrawn) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(e.sum); rd.ReadLong(e.op) END
	END Internalize;

	(* --- счёт ------------------------------------------------------------- *)

	PROCEDURE (a: Account) Done (op: LONGINT): BOOLEAN, NEW;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE (i < a.nOps) & (a.ops[i] # op) DO INC(i) END;
		RETURN i < a.nOps
	END Done;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(a.balance, e.sum)
		| e: Withdrawn DO DEC(a.balance, e.sum); a.ops[a.nOps] := e.op; INC(a.nOps)
		ELSE
		END
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR d: Deposited; w: Withdrawn; b: Balance;
	BEGIN
		WITH m: ActorPersist.ReplayComplete DO INC(starts)
		| m: Deposit DO NEW(d); d.sum := m.sum; a.Persist(c, d, 0)
		| m: Withdraw DO
			IF ~a.Done(m.op) THEN NEW(w); w.sum := m.sum; w.op := m.op; a.Persist(c, w, m.op) END
		| m: Crash DO HALT(42)
		| m: GetBalance DO NEW(b); b.n := a.balance; c.Respond(b)
		ELSE
		END
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.journal := journal; RETURN a
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

	(* --- водитель --------------------------------------------------------- *)

	PROCEDURE OpenJournal;
		VAR res: INTEGER;
	BEGIN
		journal := ActorFileJournal.Open(Files.dir.This(dir), file, CasStore.NewMem(), res);
		ASSERT(res = ActorJournal.ok, 100)
	END OpenJournal;

	PROCEDURE StartBank;
		VAR p: ActorCore.Props;
	BEGIN
		acct := NIL;
		p := ActorCore.NewProps(NewBank); p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		IF ActorCore.Spawn(NIL, p, "bank") = NIL THEN END;
		WHILE acct = NIL DO Tasks.Sleep(1) END
	END StartBank;

	PROCEDURE StopBank;
	BEGIN
		ActorCore.StopActor(ActorCore.dir.Lookup(ActorCore.node, "bank"));
		WHILE ActorCore.dir.Lookup(ActorCore.node, "bank") # NIL DO Tasks.Sleep(1) END
	END StopBank;

	PROCEDURE Ask (): INTEGER;
		VAR g: GetBalance; r: Msg;
	BEGIN
		NEW(g);
		IF ActorCore.Ask(acct, g, 1000, r) = ActorCore.ok THEN RETURN r(Balance).n ELSE RETURN MIN(INTEGER) END
	END Ask;

	PROCEDURE Show (IN what: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr("account: "); Console.WriteStr(what);
		Console.WriteStr(": balance "); WriteInt(Ask());
		Console.WriteStr(", events "); WriteInt(journal.Last("bank/acct")); Console.WriteLn
	END Show;

	PROCEDURE (d: Driver) Run;
		VAR dep: Deposit; w: Withdraw; crash: Crash; s: INTEGER;
	BEGIN
		StartBank;
		NEW(dep); dep.sum := 100; ActorCore.Tell(acct, dep);
		NEW(w); w.sum := 30; w.op := 1; ActorCore.Tell(acct, w);
		ActorCore.Tell(acct, w);	(* повтор той же операции *)
		Show("deposit 100, withdraw 30 twice as op 1");
		s := starts; NEW(crash); ActorCore.Tell(acct, crash);
		WHILE starts = s DO Tasks.Sleep(1) END;
		Show("after a trap and restart");
		StopBank; journal.Close;	(* как при падении процесса: в памяти ничего не осталось *)
		OpenJournal; StartBank;
		Show("after reopening the file");
		ActorCore.Tell(acct, w);
		Show("op 1 sent again");
		StopBank; journal.Close;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR d: Driver;
	BEGIN
		Files.dir.Delete(Files.dir.This(dir), file);
		starts := 0; OpenJournal;
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor3.
