# Overview by Example: ObxActor3

An account with a journal in a Kv base: a trap, a restart and reopening the base do not change the balance.

The account is a persistent actor (`ActorPersist.Actor`). The handler does not change the balance
itself: it writes an event, and `Apply` changes the balance. The journal is a `Kv` base in the
file `/tmp/obxactor3.kv` (`ActorKvJournal`); each event write is committed to the medium (`Sync`). A withdrawal carries an operation number, and a
repeat of the same operation does not withdraw again. The example belongs to the guide
`Actor/Docu/Actors.md` (section 8).

```
MODULE ObxActor3;
	IMPORT Services, Tasks, Console, Strings, Stores, KvStore, KvDb, KvLin,
		ActorCore, ActorSup, ActorRun, ActorJournal, ActorPersist, ActorKvJournal;

	CONST
		file = "/tmp/obxactor3.kv";	(* база Kv с журналом *)

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
			ops: ARRAY 16 OF LONGINT; nOps: INTEGER	(* последние 16 списаний *)
		END;
		Bank = POINTER TO RECORD (ActorCore.Actor) END;

		Driver = POINTER TO RECORD (Tasks.Job) END;

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
		i := 0; WHILE (i < MIN(a.nOps, 16)) & (a.ops[i] # op) DO INC(i) END;
		RETURN i < MIN(a.nOps, 16)
	END Done;

	PROCEDURE (a: Account) Apply (e: ActorPersist.Event);
	BEGIN
		WITH e: Deposited DO INC(a.balance, e.sum)
		| e: Withdrawn DO DEC(a.balance, e.sum); a.ops[a.nOps MOD 16] := e.op; INC(a.nOps)
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
		VAR st: KvStore.Store; db: KvDb.Db; res: INTEGER;
	BEGIN
		KvLin.Open(file, st, res); ASSERT(res = KvStore.ok, 100);
		KvDb.Open(st, db, res); ASSERT(res = KvDb.ok, 101);
		journal := ActorKvJournal.New(db)
	END OpenJournal;

	PROCEDURE StartBank;
		VAR p: ActorCore.Props;
	BEGIN
		acct := NIL;
		p := ActorCore.NewProps(NewBank); p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "bank") # NIL, 100);
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

	PROCEDURE (d: Driver) Body- (VAR ctx: Tasks.Ctx);
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
		StopBank; journal.Close;	(* как при новом запуске: в памяти ничего не осталось *)
		OpenJournal; StartBank;
		Show("after reopening the base");
		ActorCore.Tell(acct, w);
		Show("op 1 sent again");
		StopBank; journal.Close;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR d: Driver;
	BEGIN
		KvLin.Delete(file);
		starts := 0; OpenJournal;
		IF ActorRun.n = 0 THEN ActorRun.Start(2) END;
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor3.
```

`tools/build64.sh` builds the module (the source is `Obx/Mod64/Actor3.cp`; it is for amd64 only).
In a BlackBox window a commander executes the command:

```
   ObxActor3.Do
```

In the console:

```sh
echo 'ObxActor3.Do' | tools/run64.sh
```

The log shows:

```
account: deposit 100, withdraw 30 twice as op 1: balance 70, events 2
account: after a trap and restart: balance 70, events 2
account: after reopening the base: balance 70, events 2
account: op 1 sent again: balance 70, events 2
```

## What Happened

The bank is a parent with the `Restart` strategy; its child `acct` writes events to the stream
`"bank/acct"`. The driver deposits 100 and asks twice to withdraw 30 with operation number 1. The
first withdrawal is written as a `Withdrawn` event with the operation number; on the second one
`Done(1)` is already true, and no event is written. The journal holds two events, the balance is 70.

The `Crash` command fails the account with trap 42. The supervisor makes a new instance with the
factory, and in `Started` the new instance reads the stream from the start: 100, then −30. The table
of done operations is restored by the same `Apply`, because it is part of the event and not a
separate state.

Then the driver stops the bank and closes the journal: nothing of the account remains in the process
memory, as on a new run of the program. The base is opened again (`KvLin.Open`, `KvDb.Open`): opening takes
the last whole meta, and the account events are read by a cursor over the keys `"e" + stream`. The
new account recovers the same balance, and a
repeat of operation 1 again changes nothing.

In this example we saw an event instead of a change in place, recovery after a trap and after
reopening the base, and a repeated command that does not withdraw twice.
