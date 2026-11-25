# Overview by Example: ObxBufObj

A bounded buffer on a monitor that lives in the base class: several producers and consumers.

The buffer holds four items. A producer that finds it full waits for room; a consumer that finds
it empty waits for an item. The operations `Put` and `Get` are records derived from
`AsyncObject.Op`, and the method `Do` of the base class enters the monitor, calls `Exclusive`,
which tells the operations apart with `WITH`, and exits. A descendant writes only the operation
body, so it cannot forget the monitor; waiting is written as `WHILE ~condition DO o.Wait END`.
The module uses no language extension. The study is `docs/object-form-study.md`.

```
MODULE ObxBufObj;
(** Ограниченный буфер на мониторе в базовом классе.

	Операции Put и Get — записи-наследники AsyncObject.Op; их разбирает
	Exclusive. Полный буфер и пустой буфер — ожидание условия в цикле
	WHILE ~условие DO o.Wait END. Несколько производителей и
	потребителей работают с одним буфером. *)

	IMPORT AsyncObject, Tasks, Console, Strings;

	CONST
		cap = 4;		(* мест в буфере *)
		producers = 2;
		consumers = 2;
		per = 8;		(* предметов от каждого производителя *)

	TYPE
		PutOp = RECORD (AsyncObject.Op)
			id, seq: INTEGER
		END;
		GetOp = RECORD (AsyncObject.Op)
			item: INTEGER		(* id * 100 + seq *)
		END;

		Buffer = POINTER TO RECORD (AsyncObject.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		Producer = POINTER TO RECORD (Tasks.Task)
			b: Buffer; id, n: INTEGER
		END;
		Consumer = POINTER TO RECORD (Tasks.Task)
			b: Buffer; id, n: INTEGER
		END;

	VAR
		produced, consumed: INTEGER;
		nextSeq: ARRAY producers OF INTEGER;
		got: ARRAY producers OF INTEGER;
		broken: BOOLEAN;

	PROCEDURE (b: Buffer) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO b.Wait END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO b.Wait END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(op);
			INC(produced)
		END
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(op);
			IF op.item < 0 THEN broken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN broken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(consumed)
		END
	END Run;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Do*;
		VAR b: Buffer; p: Producer; c: Consumer; i: INTEGER;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		produced := 0; consumed := 0; broken := FALSE;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END;
		FOR i := 0 TO consumers - 1 DO
			NEW(c); c.b := b; c.n := producers * per DIV consumers;
			Tasks.Start(c)
		END;
		FOR i := 0 TO producers - 1 DO
			NEW(p); p.b := b; p.id := i; p.n := per;
			Tasks.Start(p)
		END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("produced ", produced);
		Say("consumed ", consumed);
		IF broken OR (produced # consumers * (producers * per DIV consumers)) OR
				(consumed # produced) THEN
			Console.WriteStr("order broken"); Console.WriteLn
		ELSE
			Console.WriteStr("order ok"); Console.WriteLn
		END
	END Do;

END ObxBufObj.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/BufObj.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBufObj.Do
```

In the console:

```sh
echo 'ObxBufObj.Do' | tools/run64.sh
```

The log shows:

```
produced 16
consumed 16
order ok
```

## What happened

1. `Do` creates one buffer and starts two consumers and two producers.
2. A producer builds a `PutOp` on its own stack and calls `buffer.Do(op)`. `Do` enters the
   monitor and calls `Exclusive`; when the ring is full, `Exclusive` calls `buffer.Wait`, which
   releases the monitor and parks the producer.
3. A consumer does the same with a `GetOp`; when the ring is empty it waits in the same way.
4. Each exit from `Do` wakes every waiter of the buffer. The waiters recheck their condition, so
   a wake does not promise that the condition is true.
5. Every item is taken once, in the order of its producer; the final line reports 16 items and
   `order ok`.

In this example we saw how a monitor is kept in a base class: the programmer writes a record for
the operation and a body for `Exclusive`, and enters and leaves the monitor never by hand.
