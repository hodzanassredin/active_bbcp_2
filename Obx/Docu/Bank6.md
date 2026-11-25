# Overview by Example: ObxBank6

The cashier waits for a request, a closing signal or silence; the answer is a single-assignment variable.

The cashier waits for three events at once with the selection `AsyncChan.Select`: a request in the
window, a closing signal and 50 ms of silence. The client gets the answer through a single-assignment
variable `AsyncOnce.Once`, a promise of a result. The example belongs to chapter 6 of the tutorial,
`Docu/Async.md`.

```
MODULE ObxBank6;
	IMPORT AsyncChan, AsyncSelect, AsyncOnce, AsyncIO, Services, Tasks, Console, Strings;

	TYPE
		Request = POINTER TO RECORD (AsyncSelect.Msg)
			sum: INTEGER;
			answer: AsyncOnce.Once	(* обещание: остаток после снятия или -1 *)
		END;
		Cashier = POINTER TO RECORD (Tasks.Job) END;
		Client = POINTER TO RECORD (Tasks.Job) id, delay, sum: INTEGER END;
		Manager = POINTER TO RECORD (Tasks.Job) END;

	VAR window, closing: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (c: Cashier) Body- (VAR ctx: Tasks.Ctx);
		VAR balance: INTEGER; ops: ARRAY 3 OF AsyncSelect.Op; r: AsyncSelect.Msg;
	BEGIN
		balance := 100;
		LOOP
			AsyncChan.RecvAlt(window, ops[0]);	(* заявка *)
			AsyncChan.RecvAlt(closing, ops[1]);	(* касса закрывается *)
			AsyncChan.TimeoutAlt(50, ops[2]);	(* 50 мс никого *)
			CASE AsyncChan.Select(ops) OF
			| 0:
				r := ops[0].got;
				WITH r: Request DO
					IF balance >= r.sum THEN
						balance := balance - r.sum; r.answer.FillInt(ctx, balance)
					ELSE
						r.answer.FillInt(ctx, -1)
					END
				END
			| 1: EXIT
			| 2: Console.WriteStr("cashier: nobody for 50 ms"); Console.WriteLn
			END
		END;
		Say("cashier: closed, balance ", balance);
		done := TRUE
	END Body;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Request; rest: INTEGER;
	BEGIN
		Tasks.Sleep(c.delay);
		NEW(r); r.sum := c.sum; r.answer := AsyncOnce.New();
		window.Send(r);
		IF ~r.answer.ReadInt(ctx, rest) THEN RETURN END;	(* ждать исполнения обещания; FALSE — задачу остановили *)
		IF rest >= 0 THEN Say("client got money, left ", rest) ELSE Say("client refused, id ", c.id) END
	END Body;

	PROCEDURE (m: Manager) Body- (VAR ctx: Tasks.Ctx);
		VAR stop: Request;
	BEGIN
		Tasks.Sleep(110); NEW(stop); closing.Send(stop)
	END Body;

	PROCEDURE Do*;
		VAR k: Cashier; c1, c2: Client; m: Manager;
	BEGIN
		window := AsyncChan.NewMailbox(); closing := AsyncChan.NewMailbox(); done := FALSE;
		NEW(k); Tasks.Start(k);
		NEW(c1); c1.id := 1; c1.delay := 0; c1.sum := 70; Tasks.Start(c1);
		NEW(c2); c2.id := 2; c2.delay := 80; c2.sum := 70; Tasks.Start(c2);
		NEW(m); Tasks.Start(m);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END
	END Do;

END ObxBank6.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank6.cp`; it is for amd64
only). In a BlackBox window the command is executed by a commander:

```
   ObxBank6.Do
```

In the console:

```sh
echo 'ObxBank6.Do' | tools/run64.sh
```

The log shows:

```
client got money, left 30
cashier: nobody for 50 ms
client refused, id 2
cashier: closed, balance 30
```

## What happened

The cashier fills in three alternatives: `RecvAlt(window)`, `RecvAlt(closing)` and `TimeoutAlt(50)`,
and calls `Select`. The first client arrives at once; `Select` returns 0, the cashier withdraws 70
and fulfils the promise with `r.answer.FillInt(30)`. The client that was waiting in `ReadInt` wakes
up.

For the next 50 ms nobody comes, and `Select` returns 2. After 80 ms the second client arrives, the
money is not enough, and the promise is filled with the number −1. After 110 ms the manager puts a
message into `closing`, `Select` returns 1, and the cashier leaves the loop.

The timeout of a selection is run by a `Services` action, so the command spins the main loop
`Services.actionHook.Step` and between passes waits for I/O with `AsyncIO.Poll(1)`.

In this example we have seen how one task waits for several events at once and learns which one
happened, and how a single-assignment variable passes one result.
