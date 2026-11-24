# Overview by Example: ObxBank1

Three clerks transfer money, the manager waits for them and stops the clock.

Three clerk tasks move money round a circle between three accounts and refuse a transfer for which
there is not enough money. The manager starts them into a group, waits for each with `Tasks.Join`,
prints the result and cancels the clock task. The example shows start, wait, sleep, cancellation,
and why a transfer without switch points is indivisible. The example belongs to chapter 2 of the
tutorial `Docu/Tut-Active-2.md`.

```
MODULE ObxBank1;
	IMPORT Tasks, Console, Strings;

	CONST n = 3;

	TYPE
		Clerk = POINTER TO RECORD (Tasks.Task) from, to, sum: INTEGER END;
		Clock = POINTER TO RECORD (Tasks.Task) END;
		Manager = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; refused: INTEGER; clock: Clock;

	PROCEDURE Total (): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0; FOR i := 0 TO n - 1 DO s := s + balance[i] END;
		RETURN s
	END Total;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	(* Перевод: проверка и обе записи без точки переключения между ними *)
	PROCEDURE Transfer (from, to, sum: INTEGER);
	BEGIN
		IF balance[from] >= sum THEN
			balance[from] := balance[from] - sum;
			balance[to] := balance[to] + sum
		ELSE
			INC(refused)
		END
	END Transfer;

	PROCEDURE (c: Clerk) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO Transfer(c.from, c.to, c.sum); Tasks.Yield END
	END Run;

	PROCEDURE (c: Clock) Run;
	BEGIN
		LOOP Tasks.Sleep(1000) END	(* «часы на стене»: работают, пока их не отменят *)
	END Run;

	PROCEDURE (m: Manager) Run;
		VAR g: Tasks.Group; c: ARRAY n OF Clerk; i: INTEGER;
	BEGIN
		g := Tasks.NewGroup();
		FOR i := 0 TO n - 1 DO
			NEW(c[i]); c[i].from := i; c[i].to := (i + 1) MOD n; c[i].sum := 40 + 30 * i;
			Tasks.StartIn(c[i], g)
		END;
		FOR i := 0 TO n - 1 DO Tasks.Join(c[i]) END;
		FOR i := 0 TO n - 1 DO Say("balance ", balance[i]) END;
		Say("refused ", refused);
		Say("total ", Total());
		Tasks.Cancel(clock)
	END Run;

	PROCEDURE Do*;
		VAR m: Manager;
	BEGIN
		balance[0] := 100; balance[1] := 100; balance[2] := 100; refused := 0;
		NEW(clock); Tasks.Start(clock);
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("clock state ", Tasks.State(clock))
	END Do;

END ObxBank1.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank1.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBank1.Do
```

In the console:

```sh
echo 'ObxBank1.Do' | tools/run64.sh
```

The log shows:

```
balance 200
balance 20
balance 80
refused 3
total 300
Tasks: cancelled
clock state 7
```

## What happened

The command `Do` starts the clock and the manager and turns the scheduler with `Tasks.Step` while
tasks are alive. The manager creates a group with `Tasks.NewGroup` and starts three clerks into it
with `Tasks.StartIn`; the transfer sums are 40, 70 and 100.

The clerks alternate: after each transfer a clerk calls `Tasks.Yield`. There is no switch point in
the procedure `Transfer`, so the balance check and both writes are indivisible: three transfers out
of fifteen are refused, no account went below zero, and the sum stayed 300.

The manager sleeps in `Tasks.Join` until each clerk finishes, prints the balances and calls
`Tasks.Cancel(clock)`. The clock sleeps in `Tasks.Sleep(1000)`; the cancellation wakes it, `Sleep`
traps with 127, and the system prints `Tasks: cancelled`. The state of the clock is 7,
`Tasks.Cancelled`.

In this example we have seen how to start tasks into a group and wait for them, how to cancel a
task that works forever, and why code between switch points needs no lock.
