# Overview by Example: ObxBank3

Two clerks enter the monitors of two accounts in different orders and fall into a deadlock.

A transfer is an exclusive method of the account: it enters the monitor of its own account and then
the monitor of the receiving account. One clerk transfers from `a` to `b`, the other from `b` to
`a`. The example shows the deadlock and the log that the scheduler prints. The example belongs to
chapter 3 of the tutorial `Docu/Tut-Active-3.md`.

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

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank3.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBank3.Do
```

In the console:

```sh
echo 'ObxBank3.Do' | tools/run64.sh   # output is not checked
```

The log shows the following (instead of `…` the address of the monitor is printed; it changes from
run to run, so `tools/check-examples.sh` does not execute the run line above):

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

## What happened

The first clerk enters the monitor `a`, withdraws 10 and calls `Tasks.Yield` without leaving the
monitor. The second clerk enters the monitor `b` and also yields. The first calls `b.Deposit` — the
monitor `b` is busy, so it waits at the entrance. The second calls `a.Deposit` — the monitor `a` is
busy, so it waits too.

There are no ready tasks, and nobody sleeps with a deadline. A scheduler step prints the log and
raises `Tasks.Deadlocked()`: each monitor, its owner and who stands at the entrance. The tasks got
their names from `Tasks.SetName`. The command sees the flag and cancels both clerks; the
cancellation traps them with code 127, and the monitors are released.

There will be no deadlock if transfers enter the monitors in the same order, for example by
increasing account number.

In this example we have seen how a deadlock arises on two monitors, how the scheduler log shows it,
and how a program can get out of it by cancellation.
