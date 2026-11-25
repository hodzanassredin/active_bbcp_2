# Overview by Example: ObxTasks1

Waiting: `Park` and `Unpark`, `Sleep`, `Join`.

Tasks have to wait: for a pause, for a signal from another task, for another task's end. The
example combines three kinds of waiting in one program. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxTasks1;
	IMPORT Tasks, Console;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) who: Tasks.Task END;
		Main = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Console.WriteStr("sleeper: park"); Console.WriteLn;
		Tasks.Park;
		Console.WriteStr("sleeper: woken"); Console.WriteLn
	END Run;

	PROCEDURE (w: Waker) Run;
	BEGIN
		Tasks.Sleep(20);
		Console.WriteStr("waker: unpark"); Console.WriteLn;
		Tasks.Unpark(w.who)
	END Run;

	PROCEDURE (m: Main) Run;
		VAR s: Sleeper; w: Waker;
	BEGIN
		NEW(s); Tasks.Start(s);
		NEW(w); w.who := s; Tasks.Start(w);
		Tasks.Join(s); Tasks.Join(w);
		Console.WriteStr("main: both finished"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR m: Main;
	BEGIN
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxTasks1.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Tasks1.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxTasks1.Do
```

In the console:

```sh
echo 'ObxTasks1.Do' | tools/run64.sh
```

The log shows:

```
sleeper: park
waker: unpark
sleeper: woken
main: both finished
```

## What happened

The command starts one task `Main`: waiting is allowed only in a task, and `Join` from a command
would trap 20 with the text `Tasks: wait outside a task`. `Main` starts a `Sleeper` and a `Waker`
and waits for both with `Tasks.Join`.

The sleeper prints `sleeper: park` and calls `Tasks.Park`: it sleeps until somebody calls `Unpark`
for it. The waker sleeps 20 ms in `Tasks.Sleep`; meanwhile the scheduler has nothing to execute,
and steps idle until the time is up. Awake, the waker prints `waker: unpark` and wakes the sleeper.
The sleeper prints `sleeper: woken` and ends. `Join` returns when both tasks have ended, and `Main`
prints the last line.

Had the waker called `Unpark` before the sleeper called `Park`, the signal would not be lost:
`Unpark` sets the wake-up flag, and `Park` would return at once.

In this example we have seen three kinds of waiting, the rule "wait only in a task", and that an
`Unpark` signal is kept until the next `Park`.
