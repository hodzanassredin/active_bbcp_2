# Overview by Example: ObxChan1

The example shows the bounded buffer `AsyncChan.Buffer`. The producer is faster than the consumer, and a buffer of four messages lets it run ahead, but no further than four messages.

Source of the module `ObxChan1` (`Obx/Mod64/Chan1.cp`):

```
MODULE ObxChan1;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; max: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO
			NEW(m); m.n := i; p.c.Send(m);
			IF p.c.count > p.max THEN p.max := p.c.count END
		END;
		p.c.Send(NIL)
	END Body;

	PROCEDURE (k: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			k.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO k.sum := k.sum + m.n END;
			Tasks.Sleep(1)	(* потребитель медленнее производителя *)
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Buffer; p: Producer; k: Consumer; s: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewBuffer(4);
		NEW(p); p.c := c; NEW(k); k.c := c; done := FALSE;
		Tasks.Start(p); Tasks.Start(k);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxChan1: sum="); Strings.IntToString(k.sum, s); Console.WriteStr(s);
		Console.WriteStr(" max="); Strings.IntToString(p.max, s); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan1.
```

The command (a commander in windowed BlackBox, an input line in the console):

```
   ObxChan1.Do
```

```sh
echo 'ObxChan1.Do' | tools/run64.sh
```

The log shows:

```
ObxChan1: sum=55 max=4
```

## What happened

1. `Do` creates a buffer of capacity 4 and two tasks.
2. The producer puts messages while the buffer is not full; after each `Send` it remembers the largest value of `count`.
3. The fifth `Send` finds the buffer full and waits.
4. The consumer takes a message, sleeps a millisecond and takes the next; every freed place wakes the producer.
5. The sum of the numbers from 1 to 10 is 55; the largest fill is 4, the capacity of the buffer.

More in `Docu/Async.md`.

In this example we have seen how a buffer smooths out the difference in speed of two tasks and how its capacity bounds the reserve.
