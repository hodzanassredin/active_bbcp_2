# Overview by Example: ObxTasks0

Two tasks increment a shared counter without a monitor.

Two tasks add one to the shared variable `count` a thousand times each. In a system with OS
threads such a counter loses increments: a thread is interrupted between the read and the write.
The example shows why this does not happen here, and thereby the main property of tasks. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxTasks0;
	IMPORT Tasks, Console, Strings;

	TYPE Counter = POINTER TO RECORD (Tasks.Task) END;

	VAR count: INTEGER;

	PROCEDURE (c: Counter) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 1000 DO
			x := count; count := x + 1;	(* чтение и запись без точки переключения между ними *)
			IF i MOD 10 = 0 THEN Tasks.Yield END
		END
	END Run;

	PROCEDURE Do*;
		VAR a, b: Counter; s: ARRAY 16 OF CHAR;
	BEGIN
		count := 0;
		NEW(a); NEW(b); Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(count, s);
		Console.WriteStr("count = "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxTasks0.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Tasks0.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxTasks0.Do
```

In the console:

```sh
echo 'ObxTasks0.Do' | tools/run64.sh
```

The log shows:

```
count = 2000
```

## What happened

Command `Do` creates two `Counter` tasks and starts them: `Tasks.Start` allocates a stack for each
and puts it into the ready queue. The command executes outside a task, so it drives the scheduler
itself: `Tasks.Step` executes ready tasks until `Tasks.Count()` becomes zero.

The first task reads `count` into `x` and writes `x + 1`. Between the read and the write there is
no switch point — no call of `Yield`, `Sleep`, `Park`, `Join`, no entry into a busy monitor — and
the second task cannot get in. Every tenth increment the task calls `Tasks.Yield` and goes to the
end of the queue; then the second task executes. The tasks alternate, but no increment is torn,
and the result is always 2000.

Move `Tasks.Yield` between `x := count` and `count := x + 1`, and the second task reads the old
value, and the result is smaller. The compiler does not check this.

In this example we have seen how to declare and start a task, how a command outside a task waits
for tasks by calling `Tasks.Step`, and why code between two switch points is indivisible.
