# Overview by Example: ObxBank0

An action and a task do the same work: a statement for three accounts.

The example prints the balance of three accounts, one account at a time, twice: first with an
action `Services.Action`, then with a task `Tasks.Task`. It shows how these two things differ when
the work is split into pieces. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxBank0;
	IMPORT Services, Tasks, Console, Strings;

	CONST n = 3;	(* число счетов *)

	TYPE
		Statement = POINTER TO RECORD (Services.Action) i: INTEGER END;
		StatementTask = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; done: BOOLEAN;

	PROCEDURE Line (IN who: ARRAY OF CHAR; i: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(who); Console.WriteStr(": account ");
		Strings.IntToString(i, s); Console.WriteStr(s);
		Console.WriteStr(" balance "); Strings.IntToString(balance[i], s); Console.WriteStr(s);
		Console.WriteLn
	END Line;

	(* Действие: один счёт за вызов, место в работе хранит поле i *)
	PROCEDURE (a: Statement) Do;
	BEGIN
		Line("action", a.i);
		INC(a.i);
		IF a.i < n THEN Services.DoLater(a, Services.now) ELSE done := TRUE END
	END Do;

	(* Задача: обычный цикл, место в работе лежит на её стеке *)
	PROCEDURE (t: StatementTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO Line("task", i); Tasks.Yield END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Statement; t: StatementTask;
	BEGIN
		balance[0] := 100; balance[1] := 250; balance[2] := 40;
		NEW(a); a.i := 0; done := FALSE; Services.DoLater(a, Services.now);
		WHILE ~done DO Services.actionHook.Step END;
		NEW(t); done := FALSE; Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END
	END Do;

END ObxBank0.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Bank0.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBank0.Do
```

In the console:

```sh
echo 'ObxBank0.Do' | tools/run64.sh
```

The log shows:

```
action: account 0 balance 100
action: account 1 balance 250
action: account 2 balance 40
task: account 0 balance 100
task: account 1 balance 250
task: account 2 balance 40
```

## What happened

The command `Do` puts the action `Statement` into the queue with `Services.DoLater(a, Services.now)`
and turns the main loop with calls of `Services.actionHook.Step`. Each pass of the main loop calls
`a.Do` once. `Do` prints account number `a.i`, increases `a.i` and, if the accounts have not run
out, puts itself into the queue again. The last call raises the flag `done`.

Then the command starts the task `StatementTask` and turns the same main loop. The task is executed
by the scheduler of `Tasks`, which is itself an action of the main loop. `Run` goes over the
accounts with a `FOR` loop and calls `Tasks.Yield` after each account; the variable `i` lies on the
stack of the task and survives the yield.

An action cannot wait: `Tasks.Sleep` inside `Do` traps with 21. A task waits anywhere in `Run`.

In this example we have seen that an action keeps its place in the work in the fields of the object
and starts each call from the beginning, while a task writes the same work as an ordinary loop, and
that tasks are executed inside the same main loop as actions.
