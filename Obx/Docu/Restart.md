# Overview by Example: ObxRestart

A task is created once and started many times.

One task lies in a module variable and is created at load time; command `Do` starts it ten thousand
times in a row. A set of eight tasks is created once as an array; command `Batch` starts the whole
set one hundred times. After warm-up a repeated start allocates no memory: the stack returns to the
pool and is taken from there, and the stack registry entry is bound again. The example shows a
repeated start of a finished task and the cost of this path. The general notions of a task and a
pass are in `Obx/Docu/BufCtx.md`; the example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxRestart;
(** Задача создаётся один раз и запускается многократно.

	Одна задача — указатель в переменной модуля, созданный при загрузке.
	Команда Do запускает её n раз подряд. Набор N задач создан один раз
	массивом; Batch запускает всю пачку rounds раз.

	После прогрева повторный запуск не выделяет памяти: стек возвращается
	в пул и берётся оттуда, запись реестра стека привязывается заново.
	Output — число прогонов и выделений. *)

	IMPORT Tasks, Kernel, Console, Strings;

	CONST
		n = 10000;		(* прогонов одной задачи *)
		batchN = 8;		(* задач в наборе *)
		rounds = 100;		(* пачек *)

	TYPE
		RepeatTask = POINTER TO RECORD (Tasks.Task) runs: INTEGER END;

	VAR
		one: RepeatTask;
		batch: ARRAY batchN OF RepeatTask;
		k: INTEGER;

	PROCEDURE (t: RepeatTask) Run;
	BEGIN
		INC(t.runs)
	END Run;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Console.WriteStr(what); Strings.IntToString(x, s);
		Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Drain;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Drain;

	PROCEDURE Do*;
		VAR i: INTEGER; a0: LONGINT;
	BEGIN
		Tasks.Start(one); Drain;	(* прогрев *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO n DO Tasks.Start(one); Drain END;
		Say("single task runs=", one.runs);
		Say("single task allocations=", Kernel.AllocCount() - a0)
	END Do;

	PROCEDURE Batch*;
		VAR i: INTEGER; a0, total: LONGINT;
	BEGIN
		FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
		Drain;	(* прогрев всех стеков *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO rounds DO
			FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
			Drain
		END;
		total := 0;
		FOR k := 0 TO batchN - 1 DO INC(total, batch[k].runs) END;
		Say("batch runs=", total);
		Say("batch allocations=", Kernel.AllocCount() - a0)
	END Batch;

BEGIN
	NEW(one);
	FOR k := 0 TO batchN - 1 DO NEW(batch[k]) END
END ObxRestart.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Restart.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxRestart.Do
```

In the console:

```sh
echo 'ObxRestart.Do' | tools/run64.sh
```

The log shows:

```
single task runs=10001
single task allocations=0
```

The second command starts the set:

```
   ObxRestart.Batch
```

```sh
echo 'ObxRestart.Batch' | tools/run64.sh
```

It prints:

```
batch runs=808
batch allocations=0
```

## What happened

Task `RepeatTask` is derived from `Tasks.Task`; its `Run` method only increases the run counter.
Object `one` is created in the module initialization, before the first command. `Do` first starts
the task once and runs the scheduler until the queue is empty: this is the warm-up. Then it records
`Kernel.AllocCount()` and starts the same object ten thousand more times, each time draining the
queue. The counter shows 10001: one warm-up run and ten thousand runs.

`Tasks.Start` on a finished task creates no new object and allocates no new stack. The stack of the
finished task has returned to the pool; `Start` takes it from there and binds the stack registry
entry again. That is why the difference of `Kernel.AllocCount()` after warm-up is zero. The
allocation figure per run, measured separately, is 196–207 ns against 507–523 ns when a new object
is created every time (`docs/monitor-object.md`, section 5).

`Batch` works the same way, only with eight tasks. Eight runs warm up all eight stacks, then one
hundred rounds start every task once and wait for an empty queue. In total 8 · 100 = 800 runs plus
eight warm-up runs, hence 808. Allocations after warm-up are zero again: the eight stacks stay in
the pool and are reused.

A repeated start also works for a task with a pass, `Tasks.Job`: `Job.Run` creates a fresh pass,
resets cancellation and the deadline and calls `Body` anew. `ObxRestart` uses a plain `Tasks.Task`
without a pass, so its run is cheaper: there is neither a pass nor any monitor.

Cancellation and a deadline belong to one run. A finished or cancelled task is started again as a
new one: `Run` does not remember the cancellation of the previous run. Counter `runs` is a field of
the object, so it accumulates 10001.

In this example we have seen that a finished task can be started again with the same object, that
the stack is taken from the pool, and that after warm-up a repeated start allocates no memory.
