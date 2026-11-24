# Overview by Example: ObxTasks4

Deadlock: the scheduler prints who waits for what.

A task waits for an `Unpark` nobody will send. An OS thread in this position would just hang. The
task scheduler notices that there are live tasks and nothing to wake them, and prints a log. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxTasks4;
	IMPORT Tasks;

	TYPE Lonely = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (l: Lonely) Run;
	BEGIN
		Tasks.Park	(* разбудить некому *)
	END Run;

	PROCEDURE Do*;
		VAR l: Lonely;
	BEGIN
		NEW(l); Tasks.SetName(l, "lonely"); Tasks.Start(l);
		Tasks.Step;
		IF Tasks.Deadlocked() THEN Tasks.Cancel(l); Tasks.Step END
	END Do;

END ObxTasks4.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Tasks4.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxTasks4.Do
```

In the console:

```sh
echo 'ObxTasks4.Do' | tools/run64.sh
```

The log shows:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  (no monitors)
Tasks: cancelled
```

## What happened

The command starts task `lonely` and makes one step. The task calls `Tasks.Park` and falls
asleep. There are no ready tasks, no sleepers with a time, but there is a live task, so the step
prints the line `Tasks: deadlock: …` and the monitor dump (there are no monitors here) and raises
`Tasks.Deadlocked()`.

The log is a diagnosis, not a crash. The command cancels the task and makes another step: `Cancel`
wakes it, `Park` checks the flag after the sleep and interrupts the task with trap 127.

In this example we have seen how the scheduler detects a complete deadlock and how to get out of
it by cancellation. A deadlock in which even one task sleeps with a time is not detected this way.
