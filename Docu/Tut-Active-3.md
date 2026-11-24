[← Chapter 2](Tut-Active-2.md) · [Contents](Tut-Active-TOC.md) · [Chapter 4 →](Tut-Active-4.md)

# 3. Shared data and a monitor

## 3.1. A wait in the middle of a transfer

The account is empty. Two customers want to withdraw 100 each and are ready to wait until the money
appears. Before handing out money the clerk checks the signature — this takes time, and for that
time the task falls asleep. After 10 ms a salary of 150 arrives, after another 30 ms — 50.

The naive solution: wait until there is enough money, check the signature, withdraw.

```
WHILE balance < n DO Tasks.Sleep(1) END;
Check;	(* signature check: Tasks.Sleep(5) *)
balance := balance - n
```

After the salary both customers see 150 and both go to check the signature. While the first waits
in `Check`, the second has also checked the balance and also waits. Both withdraw 100, and the
account has −50. The rule of chapter 2 does not help here: there is a wait between the check and
the withdrawal, and a wait is a switch point.

## 3.2. A monitor and AWAIT

The check "there is enough money" and the withdrawal must be one indivisible step, even if someone
was waiting before it. For this there is an *exclusive procedure* — a procedure with the flag
`[exclusive]`. It is tied to a *monitor*: an object that at any moment belongs to at most one task.
A task that calls an exclusive procedure while the monitor is busy waits at the entrance. All
module-level exclusive procedures share one monitor of the module; for a method the monitor is its
record, and different records have different monitors.

Inside an exclusive procedure one can wait for a condition: `ACTIVE.AWAIT(balance >= n)`. If the
condition is false, the task releases the monitor and falls asleep; when another task leaves the
monitor, the condition is checked again, and the waiting task gets the monitor back — now with a
true condition. This is "wait until money appears on the account", written in one line. The flag
and `AWAIT` are available to a module that imports `ACTIVE`; the rules are in sections 5 and 6 of
the language report `Docu/Active-CP-Lang.md`.

The module `ObxBank2` plays both versions:

```
MODULE ObxBank2;
	IMPORT ACTIVE, Tasks, Console, Strings;

	TYPE
		Buyer = POINTER TO RECORD (Tasks.Task) safe: BOOLEAN END;
		Payroll = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: INTEGER;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Check;
	BEGIN
		Tasks.Sleep(5)	(* кассир сверяет подпись: это ожидание *)
	END Check;

	(* Без монитора: между проверкой и списанием стоит ожидание *)
	PROCEDURE NaiveWithdraw (n: INTEGER);
	BEGIN
		WHILE balance < n DO Tasks.Sleep(1) END;
		Check;
		balance := balance - n
	END NaiveWithdraw;

	(* С монитором: проверка и списание в одной исключающей процедуре *)
	PROCEDURE [exclusive] Withdraw (n: INTEGER);
	BEGIN
		ACTIVE.AWAIT(balance >= n);
		balance := balance - n
	END Withdraw;

	PROCEDURE [exclusive] Deposit (n: INTEGER);
	BEGIN
		balance := balance + n
	END Deposit;

	PROCEDURE (b: Buyer) Run;
	BEGIN
		IF b.safe THEN Check; Withdraw(100) ELSE NaiveWithdraw(100) END;
		Say("paid 100, balance ", balance)
	END Run;

	PROCEDURE (p: Payroll) Run;
	BEGIN
		Tasks.Sleep(10); Deposit(150); Say("deposit 150, balance ", balance);
		Tasks.Sleep(30); Deposit(50); Say("deposit 50, balance ", balance)
	END Run;

	PROCEDURE Run (safe: BOOLEAN);
		VAR b1, b2: Buyer; p: Payroll;
	BEGIN
		balance := 0;
		NEW(b1); b1.safe := safe; NEW(b2); b2.safe := safe; NEW(p);
		Tasks.Start(b1); Tasks.Start(b2); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("final balance ", balance)
	END Run;

	PROCEDURE Do*;
	BEGIN
		Console.WriteStr("without monitor:"); Console.WriteLn; Run(FALSE);
		Console.WriteStr("with monitor:"); Console.WriteLn; Run(TRUE)
	END Do;

END ObxBank2.
```

The command prints:

```
without monitor:
deposit 150, balance 150
paid 100, balance 50
paid 100, balance -50
deposit 50, balance 0
final balance 0
with monitor:
deposit 150, balance 150
paid 100, balance 50
deposit 50, balance 100
paid 100, balance 0
final balance 0
```

The result in both cases is 0, but on the way the naive version went below zero and the version
with the monitor did not: the second customer waited for the second deposit. In the safe version
the signature check is before the entry into the monitor. It is allowed to hold the monitor while a
task sleeps, but then all the other clerks wait at the entrance; a monitor is held briefly.

Why is `Deposit` exclusive if it has no wait? Because `AWAIT` rechecks the conditions when someone
leaves the monitor. A deposit through an exclusive procedure wakes the waiting customer at once. A
monitor protects data only from those who enter the monitor themselves; a field that is changed
bypassing the exclusive procedures is not protected by it, and the compiler does not check this.

## 3.3. Deadlock

Now each account has its own monitor: a transfer is a method of the account, exclusive on its own
account. A transfer from `a` to `b` enters the monitor of `a`, withdraws, then calls `b.Deposit`
and enters the monitor of `b`. Two clerks transfer at the same time: one from `a` to `b`, the other
from `b` to `a`. The first holds `a` and waits for `b`; the second holds `b` and waits for `a`.
Neither will ever continue. Such a situation is called a *deadlock*.

```
MODULE ObxBank3;
	IMPORT ACTIVE, Tasks, Console;

	TYPE
		Account = POINTER TO RECORD balance: INTEGER END;
		Clerk = POINTER TO RECORD (Tasks.Task) from, to: Account END;

	VAR a, b: Account; c1, c2: Clerk;

	PROCEDURE [exclusive] (acc: Account) Deposit (n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance + n
	END Deposit;

	PROCEDURE [exclusive] (acc: Account) TransferTo (to: Account; n: INTEGER), NEW;
	BEGIN
		acc.balance := acc.balance - n;
		Tasks.Yield;	(* держит монитор acc и уступает *)
		to.Deposit(n)	(* вход во второй монитор *)
	END TransferTo;

	PROCEDURE (c: Clerk) Run;
	BEGIN
		c.from.TransferTo(c.to, 10)
	END Run;

	PROCEDURE Do*;
	BEGIN
		NEW(a); a.balance := 100; NEW(b); b.balance := 100;
		NEW(c1); c1.from := a; c1.to := b; Tasks.SetName(c1, "clerk a->b");
		NEW(c2); c2.from := b; c2.to := a; Tasks.SetName(c2, "clerk b->a");
		Tasks.Start(c1); Tasks.Start(c2);
		WHILE (Tasks.Count() > 0) & ~Tasks.Deadlocked() DO Tasks.Step END;
		IF Tasks.Deadlocked() THEN
			Console.WriteStr("deadlock, cancelling"); Console.WriteLn;
			Tasks.Cancel(c1); Tasks.Cancel(c2);
			WHILE Tasks.Count() > 0 DO Tasks.Step END
		END
	END Do;

END ObxBank3.
```

An OS thread in such a situation would simply hang. The scheduler sees the deadlock: there are live
tasks, there are no ready ones, and nobody sleeps with a deadline. Once, it prints a log "who waits
for what" and raises the flag `Tasks.Deadlocked()`:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  monitor …: held by clerk a->b count=1
    enters clerk b->a
  monitor …: held by clerk b->a count=1
    enters clerk a->b
deadlock, cancelling
Tasks: cancelled
Tasks: cancelled
```

The log reads like this: the monitor of the first account is held by `clerk a->b`, and `clerk b->a`
stands at its entrance; with the second it is the other way round. The tasks got their names from
`Tasks.SetName`; without names the log is harder to understand. Instead of `…` the address of the
monitor is printed; it changes from run to run. The command notices the deadlock and cancels both
clerks; the cancellation takes them off the queues and releases the monitors.

The cure for such a deadlock is to always enter the monitors in the same order. For example, a
transfer first enters the monitor of the account with the lower number, then of the higher one,
wherever the money goes. The language does not check the order. The scheduler catches only a full
deadlock: if at least one task sleeps with a deadline, there will be no log, although part of the
tasks stand forever (`Docu/Tasks.md`, section 6).

## 3.4. Cost and limits

Entry into and exit from a free monitor cost 62–78 ns (`docs/measurements.md`, section 2.2). This
is more than in Go, but for a money transfer it goes unnoticed.

A monitor works while the data of one action lie under one monitor. As soon as a transfer touches
two objects with their own monitors, an order of entry appears, and with it the threat of
deadlock. And a monitor does not save from another trouble: if the transfer traps between the
withdrawal and the deposit, the money disappears — the monitor is released, and what was withdrawn
does not come back. A transaction removes both troubles; the next chapter is about it.
