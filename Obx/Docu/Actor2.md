# Overview by Example: ObxActor2

A bank: accounts and transfers under a supervisor, the `Resume` directive and the receive timeout.

Ten accounts and a thousand transfers between them are actors, children of one bank. Every hundredth
transfer carries a zero amount, and the account traps on a precondition. The example shows why the
accounts use the `Resume` directive: the total amount of money at the end equals the initial one.
The example belongs to the guide `Actor/Docu/Actors.md` (section 6).

```
MODULE ObxActor2;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	CONST
		accounts = 10;
		transfers = 1000;
		initial = 10000;	(* начальный остаток счёта *)
		badEvery = 100;	(* каждый badEvery-й перевод — с нулевой суммой *)

	TYPE
		Msg = ActorCore.Msg;
		Withdraw = POINTER TO RECORD (Msg) amount: INTEGER END;
		Deposit = POINTER TO RECORD (Msg) amount: INTEGER END;
		Done = POINTER TO RECORD (Msg) END;
		Refused = POINTER TO RECORD (Msg) END;
		GetBalance = POINTER TO RECORD (Msg) END;
		Balance = POINTER TO RECORD (Msg) n: INTEGER END;
		Report = POINTER TO RECORD (Msg) ok: BOOLEAN END;

		Account = POINTER TO RECORD (ActorCore.Actor) balance: INTEGER END;
		Transfer = POINTER TO RECORD (ActorCore.Actor)
			from, to: ActorCore.Ref;
			amount, stage: INTEGER
		END;
		Bank = POINTER TO RECORD (ActorCore.Actor) END;

		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		acct: ARRAY accounts OF ActorCore.Ref;
		nextFrom, nextTo, nextAmount: INTEGER;	(* параметры следующего перевода для фабрики *)
		finished, succeeded: INTEGER;
		total: INTEGER;
		done: Done; refused: Refused; getBalance: GetBalance;

	PROCEDURE WriteInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END WriteInt;

	(* --- счёт ------------------------------------------------------------- *)

	PROCEDURE (a: Account) Receive (c: ActorCore.Context; m: Msg);
		VAR b: Balance;
	BEGIN
		WITH m: Withdraw DO
			ASSERT(m.amount > 0, 20);
			IF a.balance >= m.amount THEN DEC(a.balance, m.amount); c.Respond(done)
			ELSE c.Respond(refused)
			END
		| m: Deposit DO
			ASSERT(m.amount > 0, 20);
			INC(a.balance, m.amount); c.Respond(done)
		| m: GetBalance DO
			NEW(b); b.n := a.balance; c.Respond(b)
		ELSE
		END
	END Receive;

	PROCEDURE NewAccount (): ActorCore.Actor;
		VAR a: Account;
	BEGIN
		NEW(a); a.balance := initial; RETURN a
	END NewAccount;

	(* --- перевод ---------------------------------------------------------- *)

	PROCEDURE Finish (t: Transfer; c: ActorCore.Context; ok: BOOLEAN);
		VAR r: Report;
	BEGIN
		NEW(r); r.ok := ok; c.Tell(c.Parent(), r);
		ActorCore.StopActor(c)
	END Finish;

	PROCEDURE (t: Transfer) Receive (c: ActorCore.Context; m: Msg);
		VAR w: Withdraw; d: Deposit;
	BEGIN
		WITH m: ActorCore.Started DO
			c.SetReceiveTimeout(200);
			NEW(w); w.amount := t.amount; c.Tell(t.from, w); t.stage := 1
		| m: Done DO
			IF t.stage = 1 THEN NEW(d); d.amount := t.amount; c.Tell(t.to, d); t.stage := 2
			ELSE Finish(t, c, TRUE)
			END
		| m: Refused DO Finish(t, c, FALSE)
		| m: ActorCore.ReceiveTimeout DO
			ASSERT(t.stage = 1, 100);	(* зачисление не трапует: деньги не теряются *)
			Finish(t, c, FALSE)
		ELSE
		END
	END Receive;

	PROCEDURE NewTransfer (): ActorCore.Actor;
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := acct[nextFrom]; t.to := acct[nextTo]; t.amount := nextAmount; t.stage := 0;
		RETURN t
	END NewTransfer;

	(* --- банк ------------------------------------------------------------- *)

	PROCEDURE (b: Bank) Receive (c: ActorCore.Context; m: Msg);
		VAR i, seed: INTEGER; p: ActorCore.Props; name: ARRAY 16 OF CHAR;
	BEGIN
		WITH m: ActorCore.Started DO
			p := ActorCore.NewProps(NewAccount);
			FOR i := 0 TO accounts - 1 DO
				Strings.IntToString(i, name); acct[i] := ActorCore.Spawn(c, p, "acct-" + name)
			END;
			p := ActorCore.NewProps(NewTransfer);
			seed := 12345;
			FOR i := 1 TO transfers DO
				seed := (seed * 1103 + 12345) MOD 65536; nextFrom := seed MOD accounts;
				seed := (seed * 1103 + 12345) MOD 65536; nextTo := seed MOD accounts;
				IF i MOD badEvery = 0 THEN nextAmount := 0 ELSE nextAmount := 1 + seed MOD 100 END;
				ASSERT(ActorCore.Spawn(c, p, "") # NIL, 100)
			END
		| m: Report DO
			INC(finished); IF m.ok THEN INC(succeeded) END
		ELSE
		END
	END Receive;

	PROCEDURE NewBank (): ActorCore.Actor;
		VAR b: Bank;
	BEGIN
		NEW(b); RETURN b
	END NewBank;

	(* --- проверка и команда ----------------------------------------------- *)

	PROCEDURE (d: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; r: Msg;
	BEGIN
		WHILE finished < transfers DO Tasks.Sleep(10) END;
		total := 0;
		FOR i := 0 TO accounts - 1 DO
			IF ActorCore.Ask(acct[i], getBalance, 1000, r) = ActorCore.ok THEN INC(total, r(Balance).n)
			ELSE total := MIN(INTEGER)
			END
		END;
		Console.WriteStr("bank: transfers "); WriteInt(transfers);
		Console.WriteStr(", done "); WriteInt(succeeded);
		Console.WriteStr(", refused "); WriteInt(transfers - succeeded); Console.WriteLn;
		Console.WriteStr("bank: total "); WriteInt(total);
		Console.WriteStr(", at start "); WriteInt(accounts * initial);
		IF total = accounts * initial THEN Console.WriteStr(": kept") ELSE Console.WriteStr(": BROKEN") END;
		Console.WriteLn;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; d: Driver;
	BEGIN
		NEW(done); NEW(refused); NEW(getBalance);
		finished := 0; succeeded := 0;
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		p := ActorCore.NewProps(NewBank); p.strategy := ActorSup.NewOneForOne(ActorCore.Resume, 0, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "bank") # NIL, 100);
		NEW(d); Tasks.Start(d);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor2.
```

`tools/build64.sh` builds the module (the source is `Obx/Mod64/Actor2.cp`; it is for amd64 only).
In a BlackBox window a commander executes the command:

```
   ObxActor2.Do
```

In the console:

```sh
echo 'ObxActor2.Do' | tools/run64.sh
```

The log shows:

```
bank: transfers 1000, done 983, refused 17
bank: total 100000, at start 100000: kept
```

## What happened

The bank is a top-level actor with the `OneForOne` strategy and the `Resume` directive. On `Started`
it creates ten accounts holding 10,000 each and a thousand transfers. A linear congruential generator
with a fixed seed chooses the transfer parameters, so the transfers are the same from run to run.

A transfer is an actor with three steps. In `Started` it sets a receive timeout of 200 ms and sends
`Withdraw` to the source account. The answer `Done` brings it to the second step: `Deposit` to the
target account. The second `Done` means success, `Refused` means not enough money. The transfer
reports the result to its parent (`c.Parent()`) and stops itself.

A transfer with a zero amount brings the account down on `ASSERT(m.amount > 0, 20)`. The bank answers
`Resume`: the account's mailbox leaves the pause, the balance is kept, the culprit letter is skipped.
The transfer gets no answer, and after 200 ms without letters its handler gets `ReceiveTimeout`: the
transfer counts as refused. There are ten such transfers; seven more got `Refused`, because the
generator moves money around unevenly and some accounts run dry. No money is lost: the timeout is
possible only on the first step, before the withdrawal.

When all reports are in, the driver task asks for the balances with `Ask`, prints the check and calls
`ActorRun.Shutdown`. With `Restart` instead of `Resume` a new account instance would start with
10,000, and the total would change by all operations that passed through that account.

In this example we have seen a supervisor over many children, the `Resume` directive that keeps the
state, and the receive timeout as a way to learn that no answer will come.
