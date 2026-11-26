MODULE ObxActor5;
	IMPORT Services, Tasks, Console, Strings, Stores,
		ActorCore, ActorRun, ActorJournal, ActorPersist;

	CONST
		accounts = 3;

	TYPE
		Msg = ActorCore.Msg;

		Moved = POINTER TO RECORD (ActorPersist.Event) sum: INTEGER END;	(* > 0 — приход, < 0 — расход *)

		Move = POINTER TO RECORD (Msg) sum: INTEGER END;
		Refresh = POINTER TO RECORD (Msg) END;
		Printed = POINTER TO RECORD (Msg) END;

		Account = POINTER TO RECORD (ActorPersist.Actor) balance: INTEGER END;

		(* сторона чтения: свой взгляд на те же события, своё состояние *)
		Turnover = RECORD last: LONGINT; credit, debit: INTEGER END;
		Report = POINTER TO RECORD (ActorCore.Actor) t: ARRAY accounts OF Turnover END;
		Adder = POINTER TO RECORD (ActorPersist.Visitor) t: POINTER TO Turnover; n: INTEGER END;

		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		acct: ARRAY accounts OF ActorCore.Ref;
		report: ActorCore.Ref;
		seen: ARRAY accounts OF LONGINT;

	PROCEDURE WriteInt (x: LONGINT);
		VAR s: ARRAY 24 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	PROCEDURE Name (i: INTEGER; OUT s: ActorCore.Name);
	BEGIN
		Strings.IntToString(i, s); s := "acct-" + s
	END Name;

	(* --- сторона записи: счёт ------------------------------------------------ *)

	PROCEDURE (e: Moved) Externalize- (VAR wr: Stores.Writer);
	BEGIN
		e.Externalize^(wr); wr.WriteVersion(0); wr.WriteInt(e.sum)
	END Externalize;

	PROCEDURE (e: Moved) Internalize- (VAR rd: Stores.Reader);
		VAR v: INTEGER;
	BEGIN
		e.Internalize^(rd); IF rd.cancelled THEN RETURN END;
		rd.ReadVersion(0, 0, v); IF ~rd.cancelled THEN rd.ReadInt(e.sum) END
	END Internalize;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		INC(a.balance, e(Moved).sum)
	END Apply;

	PROCEDURE (a: Account) Handle (c: ActorCore.Context; m: Msg);
		VAR e: Moved; i: INTEGER;
	BEGIN
		WITH m: Move DO
			IF a.balance + m.sum >= 0 THEN NEW(e); e.sum := m.sum; a.Persist(c, e, 0) END
		ELSE
		END;
		i := ORD(c.name[LEN(c.name$) - 1]) - ORD("0"); seen[i] := a.seq
	END Handle;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); RETURN a
	END NewAccount;

	(* --- сторона чтения: отчёт «обороты по счетам» ----------------------------- *)

	PROCEDURE (v: Adder) Visit (e: ActorPersist.Event);
		VAR s: INTEGER;
	BEGIN
		s := e(Moved).sum;
		IF s > 0 THEN INC(v.t.credit, s) ELSE DEC(v.t.debit, s) END;
		INC(v.n)
	END Visit;

	PROCEDURE (r: Report) Receive (c: ActorCore.Context; m: Msg);
		VAR i, res, n: INTEGER; v: Adder; name: ActorCore.Name; p: Printed;
	BEGIN
		IF m IS Refresh THEN
			n := 0;
			FOR i := 0 TO accounts - 1 DO
				Name(i, name);
				NEW(v); NEW(v.t); v.t^ := r.t[i];
				ActorPersist.Read(ActorJournal.dir, name, r.t[i].last + 1, v, v.t.last, res);	(* только новые события *)
				r.t[i] := v.t^; INC(n, v.n)
			END;
			Console.WriteStr("report: read "); WriteInt(n); Console.WriteStr(" new events"); Console.WriteLn;
			FOR i := 0 TO accounts - 1 DO
				Name(i, name);
				Console.WriteStr("  "); Console.WriteStr(name);
				Console.WriteStr(" credit "); WriteInt(r.t[i].credit);
				Console.WriteStr(" debit "); WriteInt(r.t[i].debit);
				Console.WriteStr(" up to event "); WriteInt(r.t[i].last); Console.WriteLn
			END;
			NEW(p); c.Respond(p)
		END
	END Receive;

	PROCEDURE NewReport (): ActorCore.Actor;
		VAR r: Report;
	BEGIN
		NEW(r); RETURN r
	END NewReport;

	(* --- водитель --------------------------------------------------------- *)

	PROCEDURE Send (i, sum: INTEGER);
		VAR m: Move; want: LONGINT;
	BEGIN
		NEW(m); m.sum := sum; want := seen[i] + 1; ActorCore.Tell(acct[i], m);
		WHILE seen[i] < want DO Tasks.Sleep(1) END
	END Send;

	PROCEDURE Print;
		VAR r: Msg; f: Refresh;
	BEGIN
		NEW(f); ASSERT(ActorCore.Ask(report, f, 1000, r) = ActorCore.ok, 100)
	END Print;

	PROCEDURE (d: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO accounts - 1 DO WHILE seen[i] < 0 DO Tasks.Sleep(1) END END;	(* счета восстановлены *)
		FOR i := 0 TO accounts - 1 DO Send(i, 100 * (i + 1)) END;
		Send(0, -30); Send(1, -50);
		Print;
		Send(0, 20); Send(2, -100); Send(2, -100);
		Print;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR i: INTEGER; name: ActorCore.Name; d: Driver;
	BEGIN
		ActorJournal.SetDir(ActorJournal.NewMem());
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		FOR i := 0 TO accounts - 1 DO
			Name(i, name); seen[i] := -1; acct[i] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewAccount), name)
		END;
		report := ActorCore.Spawn(NIL, ActorCore.NewProps(NewReport), "report");
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor5.
