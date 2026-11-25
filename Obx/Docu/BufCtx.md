# Overview by Example: ObxBufCtx

A bounded buffer on the monitor object: two operations, flags and a condition.

Two producers put items into a buffer of four places, two consumers take them. One producer puts two
items at a time and waits for the "room" flag; the other puts the same two items at once and waits
for the condition "there is room for both". The example shows how a task describes an operation on
a shared object, how that operation reaches `Exclusive` of the monitor object, what `Await` returns
and how waiting on flags differs from `AwaitCond`. The example belongs to the guide `Docu/Tasks.md`;
the pass and the monitor are described in detail in `docs/monitor-object.md`.

```
MODULE ObxBufCtx;
(** Ограниченный буфер на мониторе-объекте Tasks.Object.

	Операции Put и Get — записи-наследники Tasks.Op на стеке вызывающего.
	Заполненность и пустота — флаги Ready: ждущий говорит, какой флаг ему
	нужен, и просыпается только когда этот флаг появился. Второй способ —
	положить k предметов разом: условие-объект Room проверяет, что в
	буфере есть место для всех k. Первый способ — цикл по флагам, второй —
	AwaitCond. *)

	IMPORT Tasks, Console, Strings;

	CONST
		cap = 4;
		producers = 2;
		consumers = 2;
		per = 8;		(* предметов от каждого производителя *)
		k = 2;			(* сколько кладёт операция PutAll *)
		empty = 0;		(* флаг Ready: есть место *)
		full = 1;		(* флаг Ready: есть предмет *)

	TYPE
		PutOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) item: INTEGER END;
		PutAllFlagOp = RECORD (Tasks.Op) id, seq: INTEGER END;
		PutAllCondOp = RECORD (Tasks.Op) id, seq: INTEGER; room: Room END;

		Room = POINTER TO RECORD (Tasks.Cond) k: INTEGER END;

		Buffer = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		ProducerFlag = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		ProducerCond = POINTER TO RECORD (Tasks.Job) b: Buffer; id, n: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) b: Buffer; n: INTEGER END;

	VAR
		produced, consumed: INTEGER;
		nextSeq: ARRAY producers OF INTEGER;
		got: ARRAY producers OF INTEGER;
		broken: BOOLEAN;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE SayStr (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s); Console.WriteLn
	END SayStr;

	PROCEDURE (b: Buffer) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF b.count < cap THEN INCL(s, empty) END;
		IF b.count > 0 THEN INCL(s, full) END;
		RETURN s
	END Ready;

	PROCEDURE (r: Room) Ready (o: Tasks.Object): BOOLEAN;
		VAR b: Buffer;
	BEGIN
		b := o(Buffer);
		RETURN b.count + r.k <= cap
	END Ready;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
		VAR i: INTEGER; v: INTEGER;
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO
				IF ~b.Await(ctx, {empty}) THEN RETURN END
			END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO
				IF ~b.Await(ctx, {full}) THEN RETURN END
			END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		| op: PutAllFlagOp DO
			FOR i := 0 TO k - 1 DO
				WHILE b.count = cap DO
					IF ~b.Await(ctx, {empty}) THEN RETURN END
				END;
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		| op: PutAllCondOp DO
			op.room.k := k;
			WHILE ~op.room.Ready(b) DO
				IF ~b.AwaitCond(ctx, op.room) THEN RETURN END
			END;
			FOR i := 0 TO k - 1 DO
				b.ring[b.tail] := op.id * 100 + op.seq + i;
				b.tail := (b.tail + 1) MOD cap; INC(b.count)
			END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: ProducerFlag) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllFlagOp;
	BEGIN
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (p: ProducerCond) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutAllCondOp; room: Room;
	BEGIN
		NEW(room);
		i := 1;
		WHILE i <= p.n DO
			op.id := p.id; op.seq := i; op.room := room;
			p.b.Do(ctx, op);
			INC(produced, k); INC(i, k)
		END
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(ctx, op);
			IF op.item < 0 THEN broken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN broken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(consumed)
		END
	END Body;

	PROCEDURE Do*;
		VAR b: Buffer; pf: ProducerFlag; pc: ProducerCond; c: Consumer; i: INTEGER;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		produced := 0; consumed := 0; broken := FALSE;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END;
		FOR i := 1 TO consumers DO
			NEW(c); c.b := b; c.n := producers * per DIV consumers;
			Tasks.Start(c)
		END;
		NEW(pf); pf.b := b; pf.id := 0; pf.n := per; Tasks.Start(pf);
		NEW(pc); pc.b := b; pc.id := 1; pc.n := per; Tasks.Start(pc);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Say("produced ", produced);
		Say("consumed ", consumed);
		IF broken OR (produced # consumers * (producers * per DIV consumers)) OR
				(consumed # produced) THEN
			SayStr("order broken")
		ELSE
			SayStr("order ok")
		END
	END Do;

END ObxBufCtx.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/BufCtx.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxBufCtx.Do
```

In the console:

```sh
echo 'ObxBufCtx.Do' | tools/run64.sh
```

The log shows:

```
produced 16
consumed 16
order ok
```

## What happened

The pass. A task is the sequential execution of a procedure on its own stack. When a task calls an
operation of a shared object, the object must know which task calls it: to let the same task enter
again, to queue the task, to release the monitor during a wait and to notice cancellation. This
information is carried by the pass `Tasks.Ctx`. A pass cannot be created, extended or copied
outside `Tasks`: it is a `LIMITED RECORD`, and the compiler enforces this. `Job.Run` creates a pass
on the task's stack for every run and passes it to the body by `VAR`; the body passes the same pass
on to `Do`, `Await`, `Spawn`, `JoinAll` and `SetDeadline`. There is no copy, so every call sees one
state: whether the pass is cancelled, whether the deadline has expired, and which monitors the task
holds.

The operation. An operation is a record derived from `Tasks.Op`, declared on the caller's stack:
here they are `PutAllFlagOp`, `GetOp` and `PutAllCondOp`. The caller fills the fields and calls
`p.b.Do(ctx, op)`. `Do` takes the monitor (or queues the task for entry) and calls
`o.Exclusive(ctx, op)`. `Exclusive` dispatches on the record type with
`WITH op: PutAllFlagOp DO … | op: GetOp DO … END`. The fields carry data both ways: the consumer sets
`op.item := -1` and reads the item after `Do`. No memory is allocated for an operation: the record
lies on the stack. Branch `ELSE HALT(20)` catches a record kind the object cannot execute.

The wait. `Await(ctx, {empty})` is called only from `Exclusive`, under the monitor at depth 1. If the
needed flag is already in `Ready()`, `Await` returns `TRUE` at once. Otherwise it releases the
monitor, puts the task to sleep, takes the monitor again after the wake-up and repeats the check.
`TRUE` — the condition holds and the task holds the monitor; `FALSE` — the pass is stopped by
cancellation or by the deadline. On `FALSE` the object author leaves `Exclusive` by `RETURN`, and
the caller sees a plain `Do` that did nothing.

Why the caller has no `WHILE` loop. The producer body calls `Do` for every item and does not check
whether the buffer is full. The loop `WHILE b.count = cap` stands inside `Exclusive`, in the object,
and is written once for all callers. This is the intent: the caller states the operation, and the
owner of the state writes the wait and the repeated check. In example `ObxBank2`
(`Obx/Docu/Bank2.md`) the naive version, by contrast, polls the balance in the buyer's body and
loses indivisibility.

Flags and condition. `Ready()` returns a set of bits; a waiter names the bits it needs: `{empty}` or
`{full}`. The monitor wakes the first task whose `need * Ready() # {}`. This suffices when the
condition is one bit: "there is room", "there is an item". `PutAllCondOp` puts two items and needs
"there is room for both". This is a predicate with parameter `k`, not a state bit; the condition
object `Room.Ready(o)` computes it. `AwaitCond(ctx, op.room)` releases the monitor, sleeps and asks
`op.room.Ready(b)` again after the wake-up. Flags answer the question with one bit; `Cond` is the
general case: the condition looks at the whole state of the object and at the waiter's fields.

Cancellation and deadline. `Tasks.Cancel(t)` and `SetDeadline(ctx, ms)` do not trap in `Await`: it
returns `FALSE`, and `Exclusive` leaves. `Do` on a stopped pass does not execute the operation at
all. In this example the check stands before any change to the buffer, so a stop leaves no half
operation. If the task is sleeping in `Tasks.Sleep` at that moment, cancellation interrupts the
sleep with trap 127 and prints `Tasks: cancelled`; examples `ObxDownCtx` and `ObxDownChan` behave
this way.

Repeated start. A finished `Tasks.Job` can be started again: `Job.Run` creates a fresh pass and
calls `Body` anew. The stack returns to the pool and is taken from there; after warm-up a start
allocates no memory. `ObxRestart` shows this.

In this run `Do` creates the buffer, two consumers, a producer on flags and a producer on a
condition. The consumers wait for `{full}`; producer 0 puts two items and waits for `{empty}`;
producer 1 puts two items and waits for `Room.Ready`. Each producer makes four steps and puts eight
items; the two consumers take eight each. Hence `produced 16` and `consumed 16`. A consumer checks
the producer number and expects the next sequence of numbers; the check passes, hence `order ok`.

In this example we have seen why a task needs a pass, how a record operation reaches `Exclusive`,
what `Await` returns, why the caller writes no wait loop and how `AwaitCond` differs from flags.
