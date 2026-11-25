# Overview by Example: ObxTasks3

A task group: a failing child cancels the others.

A parent starts three helpers. One fails; the other two, left alone, would work forever. A task
group ties their fates together: a member's failure cancels the others, and the parent does not end
while the children live. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxTasks3;
	IMPORT Tasks, Console, Strings;

	TYPE
		Child = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Task) g: Tasks.Group END;

	VAR kids: ARRAY 3 OF Child;

	PROCEDURE (c: Child) Run;
	BEGIN
		IF c.n = 1 THEN Tasks.Sleep(10); HALT(30) END;
		LOOP Tasks.Sleep(1000) END
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR i: INTEGER;
	BEGIN
		p.g := Tasks.NewGroup();
		FOR i := 0 TO 2 DO NEW(kids[i]); kids[i].n := i; Tasks.StartIn(kids[i], p.g) END
	END Run;

	PROCEDURE Do*;
		VAR p: Parent; i: INTEGER; s: ARRAY 8 OF CHAR;
	BEGIN
		NEW(p); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.GroupFailed(p.g) THEN Console.WriteStr("group failed") ELSE Console.WriteStr("group ok") END;
		Console.WriteLn;
		FOR i := 0 TO 2 DO
			Strings.IntToString(Tasks.State(kids[i]), s);
			Console.WriteStr("child state "); Console.WriteStr(s); Console.WriteLn
		END
	END Do;

END ObxTasks3.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Tasks3.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxTasks3.Do
```

In the console:

```sh
echo 'ObxTasks3.Do' | tools/run64.sh
```

The log shows:

```
Tasks: cancelled
Tasks: cancelled
group failed
child state 7
child state 6
child state 7
```

## What happened

The parent calls `Tasks.NewGroup` and gets its own group, then starts three children with
`Tasks.StartIn(kids[i], p.g)` and leaves `Run`. It does not end: by the group rule the owner waits
for the last member in state `Parked`.

The second child (`n = 1`) sleeps 10 ms and traps with `HALT(30)`. A task body executes under
`Kernel.Try`, so the trap ends only this task: its state is `Finished` (6), `Failed` is `TRUE`. The
trap marks the group failed and cancels the siblings. They sleep in `Sleep`; the cancellation wakes
them, `Sleep` checks the flag after the sleep and interrupts the task with trap 127 and the line
`Tasks: cancelled`. Their state is `Cancelled` (7). When the last member is removed, the parent
ends, and the command's loop finishes.

In this example we have seen a task group, cancellation by trap 127 at a waiting operation, and
that a trap in a task does not bring the program down but becomes flag `GroupFailed`.
