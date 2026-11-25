# Overview by Example: ObxBankCtx

A transfer between accounts: a repeated entry into a monitor and two monitors in hand.

Two buyers withdraw one hundred each from account A, the payroll deposits two sums into it, and one
task transfers fifty from account A to account B. The transfer is an operation on account A; inside
it the same task enters the monitor of A once more and takes the monitor of B without releasing A.
The example shows a repeated entry, work with two monitors at once, and a wait for the balance
written inside an operation. The general notions of the pass, the operation and `Await` are in
`Obx/Docu/BufCtx.md`; the example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxBankCtx;
(** Счёт на мониторе-объекте: перевод повторным входом.

	Перевод — операция Transfer над счётом A. Её Exclusive уже идёт под
	монитором A и зовёт A.Do со списанием: это повторный вход, глубина
	становится 2. Затем под тем же внешним монитором берётся монитор B.
	Снятие ждёт остатка, как обычная операция; на глубине 1 монитор
	отпускается при выходе. *)

	IMPORT Tasks, Console, Strings;

	TYPE
		WithdrawOp = RECORD (Tasks.Op) amount: INTEGER; ok: BOOLEAN END;
		DepositOp = RECORD (Tasks.Op) amount: INTEGER END;
		TransferOp = RECORD (Tasks.Op) to: Account; amount: INTEGER; ok: BOOLEAN END;

		Account = POINTER TO RECORD (Tasks.Object)
			balance: INTEGER
		END;

		Buyer = POINTER TO RECORD (Tasks.Job) a: Account; amount: INTEGER END;
		Payroll = POINTER TO RECORD (Tasks.Job) a: Account; first, second: INTEGER END;
		Transfer = POINTER TO RECORD (Tasks.Job) from, to: Account; amount: INTEGER END;

	VAR
		transferDone: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (a: Account) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF a.balance > 0 THEN INCL(s, 0) END;
		RETURN s
	END Ready;

	PROCEDURE (a: Account) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR w: WithdrawOp; d: DepositOp;
	BEGIN
		WITH op: WithdrawOp DO
			WHILE a.balance < op.amount DO
				IF ~a.Await(ctx, {0}) THEN op.ok := FALSE; RETURN END
			END;
			DEC(a.balance, op.amount); op.ok := TRUE
		| op: DepositOp DO
			INC(a.balance, op.amount)
		| op: TransferOp DO
			(* Повторный вход в тот же монитор: глубина 2. *)
			w.amount := op.amount; w.ok := FALSE;
			a.Do(ctx, w);
			IF ~w.ok THEN RETURN END;
			(* Монитор A остаётся у задачи, пока берётся монитор B. *)
			d.amount := op.amount;
			op.to.Do(ctx, d);
			op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (b: Buyer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: WithdrawOp;
	BEGIN
		Tasks.Sleep(5);
		op.amount := b.amount; op.ok := FALSE;
		b.a.Do(ctx, op)
	END Body;

	PROCEDURE (p: Payroll) Body- (VAR ctx: Tasks.Ctx);
		VAR op: DepositOp;
	BEGIN
		op.amount := p.first; p.a.Do(ctx, op);
		Tasks.Sleep(10);
		op.amount := p.second; p.a.Do(ctx, op)
	END Body;

	PROCEDURE (t: Transfer) Body- (VAR ctx: Tasks.Ctx);
		VAR op: TransferOp;
	BEGIN
		Tasks.Sleep(15);
		op.to := t.to; op.amount := t.amount; op.ok := FALSE;
		t.from.Do(ctx, op);
		transferDone := op.ok
	END Body;

	PROCEDURE Do*;
		VAR a, b: Account; x, y: Buyer; p: Payroll; t: Transfer;
	BEGIN
		NEW(a); a.balance := 200;
		NEW(b); b.balance := 0;
		NEW(x); x.a := a; x.amount := 100;
		NEW(y); y.a := a; y.amount := 100;
		NEW(p); p.a := a; p.first := 50; p.second := 50;
		NEW(t); t.from := a; t.to := b; t.amount := 50;
		transferDone := FALSE;
		Tasks.Start(t); Tasks.Start(x); Tasks.Start(y); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("A = ", a.balance);
		Say("B = ", b.balance);
		IF transferDone THEN
			Console.WriteStr("transfer ok"); Console.WriteLn
		ELSE
			Console.WriteStr("transfer failed"); Console.WriteLn
		END
	END Do;

END ObxBankCtx.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/BankCtx.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBankCtx.Do
```

In the console:

```sh
echo 'ObxBankCtx.Do' | tools/run64.sh
```

The log shows:

```
A = 50
B = 50
transfer ok
```

## What happened

An account is the monitor object `Account` with field `balance`. The operations on it are records
`WithdrawOp`, `DepositOp` and `TransferOp`; each is declared on the caller's stack. An ordinary
entry into the monitor is always the same: `a.Do(ctx, op)` takes the monitor and calls
`Account.Exclusive`, which dispatches on the record type with `WITH`.

The transfer is more involved. `Transfer` calls `t.from.Do(ctx, op)`, that is, operation
`TransferOp` on A. `Exclusive` already runs under the monitor of A at depth 1, and inside it calls
`a.Do(ctx, w)` with a withdrawal. This is a repeated entry of the same task: `Do` sees that the
owner of the monitor is the task itself, does not queue the task, and increases the depth to 2. This
is why the pass carries information about the task: otherwise the object could not tell a repeated
entry from a foreign one. Leaving at depth 2 decrements the counter, and only leaving at depth 1
releases the monitor.

After a successful withdrawal the transfer takes the monitor of B: `op.to.Do(ctx, d)`. The monitor
of A stays with the task: `Exclusive` has not returned yet. So the task holds two monitors at once.
The rule about sleeping with monitors in hand allows a wait only if every held object answered
`TRUE` to `HoldsWhileWaiting`; the default answer is `FALSE`, and a wait traps 26. Here the task
does not wait while it holds both monitors, so the rule does not fire. The capture order is one
way: A first, then B. If another task took them in the opposite order, the two tasks could get
stuck; this is shown in `ObxBank3`.

The withdrawal inside the transfer may wait for the balance in `Await`, but `Await` at depth 2 of
the same monitor traps 23. The example is arranged so that by the time of the transfer A has enough
money, and the nested withdrawal does not wait. An ordinary operation at depth 1 can wait for the
balance: it releases the monitor, sleeps and checks the balance again after the wake-up.

The schedule of the run is this. The transfer task sleeps 15 ms. The buyers sleep 5 ms and then
withdraw 100 each. The payroll deposits 50 at once, sleeps 10 ms and deposits another 50. Account A
held 200; after two deposits and two withdrawals 100 remains. The transfer at 15 ms withdraws 50
from A and deposits 50 into B. The result is `A = 50`, `B = 50`. Every operation completes as a
whole, because the check and the change stand inside one exclusive procedure; `transfer ok`
confirms that the transfer received `TRUE` from both operations.

The withdrawal and the deposit are two operations on two objects. If the pass is stopped between
them, `Do` on account B will not execute the deposit, and the withdrawn money will not arrive at B:
a transfer over two monitors is not atomic. The example does not check this case; a transfer as one
transaction with rollback is shown in `ObxStm0`.

In this example we have seen a repeated entry into the same monitor through the pass, the capture of
a second monitor without releasing the first, and that the wait for the balance lives inside the
operation, not in the caller's body.
