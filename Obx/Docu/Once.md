# Overview by Example: ObxOnce

The example shows the single-assignment variable `AsyncOnce.Once`: it is filled once and read any number of times; a read before filling waits.

Source of the module `ObxOnce` (`Obx/Mod64/Once.cp`):

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Job) END;
		Writer = POINTER TO RECORD (Tasks.Job) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		done := o.ReadInt(ctx, got)	(* ждёт, пока Writer не заполнит; FALSE — задачу остановили *)
	END Body;

	PROCEDURE (w: Writer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(5); o.FillInt(ctx, 42)
	END Body;

	PROCEDURE Do*;
		VAR r: Reader; w: Writer; s: ARRAY 16 OF CHAR;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); NEW(w); done := FALSE;
		Tasks.Start(r); Tasks.Start(w);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxOnce.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxOnce.Do
```

```sh
echo 'ObxOnce.Do' | tools/run64.sh
```

The log shows:

```
ObxOnce: 42
```

## What happened

1. `Do` creates the variable and two tasks: a reader and a writer.
2. The reader reaches `ReadInt` first and waits in `Await` on the `filled` flag.
3. The writer sleeps 5 ms and calls `FillInt(ctx, 42)`; leaving the operation wakes the reader.
4. The reader gets 42 and sets `done`.

More in `Docu/Async.md`.

In this example we have seen how one task promises a value to others and how a reader waits for what was promised.
