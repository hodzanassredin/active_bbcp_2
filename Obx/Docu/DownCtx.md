# Overview by Example: ObxDownCtx

Download and save: a parent, children and a deadline.

The parent task starts two children: the downloader puts chunks into a buffer, the saver takes them.
Both children are `Job` tasks, and each has its own pass. The parent waits in `JoinAll`. In the
first phase the deadline `SetDeadline` expires before the stream of chunks ends, and the parent
cancels the children, reporting that the result is incomplete. In the second phase a separate task
cancels the children explicitly; their states are checked after `JoinAll`. The example shows
`Spawn`, `JoinAll`, a deadline and cancellation. The general notions of the pass and the operation
are in `Obx/Docu/BufCtx.md`; the example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxDownCtx;
(** Скачать и сохранить: родитель, дети и срок.

	Родительская задача с пропуском запускает через Tasks.Spawn две
	дочерние: загрузчик кладёт куски в буфер, сохранятель их берёт. Обе —
	тоже задачи с пропуском, у каждой свой Ctx. Родитель ждёт JoinAll.
	Первая фаза кончается сроком SetDeadline: родитель отменяет детей и
	сообщает, что результат неполный. Вторая фаза отменяет загрузчика и
	сохранителя явно; состояния детей проверяются после JoinAll. *)

	IMPORT Tasks, Console;

	CONST
		cap = 3;
		total = 20;		(* кусков в источнике *)
		chunkMs = 5;		(* задержка источника на кусок *)
		deadlineMs = 40;

	TYPE
		PutOp = RECORD (Tasks.Op) seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) seq: INTEGER; ok: BOOLEAN END;

		Pipe = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		Downloader = POINTER TO RECORD (Tasks.Job) pipe: Pipe END;
		Saver = POINTER TO RECORD (Tasks.Job) pipe: Pipe; saved: INTEGER END;
		DeadlineParent = POINTER TO RECORD (Tasks.Job) pipe: Pipe; sv: Saver END;
		CancelParent = POINTER TO RECORD (Tasks.Job) pipe: Pipe; sv: Saver; dl: Tasks.Task END;
		Killer = POINTER TO RECORD (Tasks.Task) dl, sv: Tasks.Task END;

	VAR
		deadlineExpired, deadlinePartial: BOOLEAN;
		cancelDl, cancelSv, cancelPartial: BOOLEAN;

	PROCEDURE SayBool (IN what: ARRAY OF CHAR; yes: BOOLEAN);
	BEGIN
		Console.WriteStr(what);
		IF yes THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END;
		Console.WriteLn
	END SayBool;

	PROCEDURE (p: Pipe) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF p.count < cap THEN INCL(s, 0) END;
		IF p.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (p: Pipe) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE p.count = cap DO
				IF ~p.Await(ctx, {0}) THEN RETURN END
			END;
			p.ring[p.tail] := op.seq;
			p.tail := (p.tail + 1) MOD cap; INC(p.count)
		| op: GetOp DO
			WHILE p.count = 0 DO
				IF ~p.Await(ctx, {1}) THEN op.ok := FALSE; RETURN END
			END;
			op.seq := p.ring[p.head];
			p.head := (p.head + 1) MOD cap; DEC(p.count); op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (d: Downloader) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO total DO
			Tasks.Sleep(chunkMs);		(* имитация источника *)
			IF ctx.Stopped() THEN RETURN END;
			op.seq := i; d.pipe.Do(ctx, op)
		END
	END Body;

	PROCEDURE (s: Saver) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		i := 0;
		LOOP
			IF (i >= total) OR ctx.Stopped() THEN EXIT END;
			op.ok := FALSE;
			s.pipe.Do(ctx, op);
			IF ~op.ok THEN EXIT END;
			INC(i)
		END;
		s.saved := i
	END Body;

	PROCEDURE (p: DeadlineParent) Body- (VAR ctx: Tasks.Ctx);
		VAR dl: Downloader; sv: Saver;
	BEGIN
		NEW(dl); dl.pipe := p.pipe;
		NEW(sv); sv.pipe := p.pipe;
		p.sv := sv;
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, dl);
		Tasks.Spawn(ctx, sv);
		Tasks.JoinAll(ctx);
		deadlineExpired := ctx.expired;
		deadlinePartial := sv.saved < total
	END Body;

	PROCEDURE (k: Killer) Run;
	BEGIN
		Tasks.Sleep(15);
		Tasks.Cancel(k.dl);
		Tasks.Cancel(k.sv)
	END Run;

	PROCEDURE (p: CancelParent) Body- (VAR ctx: Tasks.Ctx);
		VAR dl: Downloader; sv: Saver; k: Killer;
	BEGIN
		NEW(dl); dl.pipe := p.pipe;
		NEW(sv); sv.pipe := p.pipe;
		NEW(k); k.dl := dl; k.sv := sv;
		p.sv := sv; p.dl := dl;
		Tasks.Spawn(ctx, dl);
		Tasks.Spawn(ctx, sv);
		Tasks.Spawn(ctx, k);
		Tasks.JoinAll(ctx);
		cancelDl := Tasks.State(dl) = Tasks.Cancelled;
		cancelSv := Tasks.State(sv) = Tasks.Cancelled;
		cancelPartial := sv.saved < total
	END Body;

	PROCEDURE Do*;
		VAR pipe: Pipe; p1: DeadlineParent; p2: CancelParent;
	BEGIN
		NEW(pipe); pipe.head := 0; pipe.tail := 0; pipe.count := 0;
		NEW(p1); p1.pipe := pipe;
		Tasks.Start(p1);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		NEW(pipe); pipe.head := 0; pipe.tail := 0; pipe.count := 0;
		NEW(p2); p2.pipe := pipe;
		Tasks.Start(p2);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		SayBool("deadline expired: ", deadlineExpired);
		SayBool("deadline partial: ", deadlinePartial);
		SayBool("cancel downloader: ", cancelDl);
		SayBool("cancel saver: ", cancelSv);
		SayBool("cancel partial: ", cancelPartial)
	END Do;

END ObxDownCtx.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/DownCtx.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxDownCtx.Do
```

In the console:

```sh
echo 'ObxDownCtx.Do' | tools/run64.sh
```

The log shows:

```
Tasks: cancelled
Tasks: cancelled
deadline expired: yes
deadline partial: yes
cancel downloader: yes
cancel saver: yes
cancel partial: yes
```

## What happened

The pipe is the monitor object `Pipe` with a ring of three places; `PutOp` puts a chunk, `GetOp`
takes one. Both operations wait for a flag in `Await`: the downloader waits for `{0}` ("there is
room"), the saver for `{1}` ("there is an item"). This is the same monitor object as in `ObxBufCtx`,
with one producer and one consumer.

The parent is a `Job` task; its body receives the pass by `VAR`. `SetDeadline(ctx, 40)` sets the
deadline on this pass. `Spawn(ctx, dl)` starts a child task and puts it into the pass's group; the
same happens with the saver. Each child gets its own pass from `Job.Run`, and the group links the
children to the parent's pass. `JoinAll(ctx)` waits until the group becomes empty. When the
parent's deadline expires, `JoinAll` cancels the group and waits until the children finish.

The downloader sleeps 5 ms per chunk and must put twenty chunks, so it would work for about 100 ms.
The deadline of 40 ms expires earlier, so in the first phase `deadlineExpired` is true and
`deadlinePartial` is true: the saver managed to take fewer than twenty chunks. Before each send the
downloader checks `ctx.Stopped()` and leaves the loop on a stop. The saver checks the same in its
loop. The stop is visible in a wait too: `Await` returns `FALSE`, the operation is not executed, and
`Exclusive` leaves.

The lines `Tasks: cancelled` are printed by module `Tasks` itself when a cancelled task wakes up
inside `Tasks.Sleep`. Here there are two: the downloader sleeps in `Sleep` in each of the two
phases, and both cancellations interrupt its sleep with trap 127. The saver waits in `Await` at
that moment; there cancellation returns `FALSE` without a line in the log. A task cancelled during
a wait ends in state `Cancelled` even if its body left by a normal `RETURN`.

The second phase is arranged differently. `CancelParent` starts the downloader, the saver and
`Killer`, an ordinary `Tasks.Task` with method `Run`. `Killer` sleeps 15 ms and calls
`Tasks.Cancel` for both children. `Cancel` sets the flag and wakes the task; the next switch point
notices the flag. After `JoinAll` the parent reads `Tasks.State`: both tasks are in state
`Cancelled`, and the saver managed to take fewer than twenty chunks. Hence the three `yes` answers
in the second group.

The state can be checked because the task object lives on: after finishing, the scheduler removes
the task from the count and returns the stack to the pool, but the object itself stays with the
program. That is why `Tasks.State(dl)` and `sv.saved` are read after `JoinAll`.

Without a deadline and without explicit cancellation the downloader would put all twenty chunks,
the saver would take them, and `JoinAll` would return after both tasks finished; then `expired` and
`partial` would be false.

In this example we have seen how a parent pass starts children with `Spawn`, how `JoinAll` waits for
the group, how a deadline stops the whole pass, and how cancellation and the deadline show in the
states of the child tasks.
