# Overview by Example: ObxDownChan

Download and save on a channel with a pass.

The task is the same as in `ObxDownCtx`: the downloader puts twenty chunks, the saver takes them, a
deadline stops the work, and the second phase cancels the children explicitly. The difference is the
pipe: here it is `AsyncChan.Buffer(3)`, not a monitor object with flags. The downloader calls
`SendCtx`, the saver calls `RecvCtx`; both procedures return `FALSE` when the pass is stopped, and
the task unwinds without a trap. The code has no `Op` records, no `Ready` and no `Await`: the
channel takes the queue and the wait upon itself. The general notions of the pass are in
`Obx/Docu/BufCtx.md`, the monitor version is in `Obx/Docu/DownCtx.md`; the example belongs to the
guide `Docu/Async.md`.

```
MODULE ObxDownChan;
(** Скачать и сохранить на канале с пропуском.

	То же, что ObxDownCtx, но труба — AsyncChan.Buffer(3), а не
	монитор-объект с флагами. Загрузчик кладёт куски через SendCtx,
	сохранятель берёт их через RecvCtx; остановка возвращает FALSE и
	задача сворачивается. Родитель через Spawn и срок SetDeadline
	отменяет группу, когда срок кончился; вторая фаза отменяет детей явно.

	Сравнение кода: здесь нет ни Op-записей, ни Ready, ни Await — канал
	берёт очередь и ожидание на себя. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, Console, Strings;

	CONST
		cap = 3;
		total = 20;
		chunkMs = 5;
		deadlineMs = 40;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Downloader = POINTER TO RECORD (Tasks.Job)
			out: AsyncChan.Buffer; sent: INTEGER; stopped: BOOLEAN
		END;

		Saver = POINTER TO RECORD (Tasks.Job)
			in: AsyncChan.Buffer; saved: INTEGER; stopped: BOOLEAN
		END;

		DeadlineParent = POINTER TO RECORD (Tasks.Job)
			pipe: AsyncChan.Buffer; dl: Downloader; sv: Saver; expired: BOOLEAN
		END;

		CancelParent = POINTER TO RECORD (Tasks.Job)
			pipe: AsyncChan.Buffer; dl: Downloader; sv: Saver
		END;

		Killer = POINTER TO RECORD (Tasks.Task) dl, sv: Tasks.Task END;

	VAR
		deadlineExpired, deadlinePartial: BOOLEAN;
		cancelDl, cancelSv, cancelPartial: BOOLEAN;

	PROCEDURE NewNum (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewNum;

	PROCEDURE GetNum (m: AsyncSelect.Msg): INTEGER;
	BEGIN
		IF m = NIL THEN RETURN -1 END;
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END GetNum;

	PROCEDURE SayBool (IN what: ARRAY OF CHAR; yes: BOOLEAN);
	BEGIN
		Console.WriteStr(what);
		IF yes THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END;
		Console.WriteLn
	END SayBool;

	PROCEDURE (d: Downloader) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		LOOP
			IF i >= total THEN EXIT END;
			INC(i);
			Tasks.Sleep(chunkMs);		(* имитация источника *)
			IF ctx.Stopped() THEN EXIT END;
			IF ~d.out.SendCtx(ctx, NewNum(i)) THEN EXIT END;
			d.sent := i
		END;
		d.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (s: Saver) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			IF (s.saved >= total) OR ctx.Stopped() THEN EXIT END;
			IF ~s.in.RecvCtx(ctx, m) THEN EXIT END;
			n := GetNum(m);
			IF n > 0 THEN INC(s.saved) END
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (p: DeadlineParent) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, p.dl);
		Tasks.Spawn(ctx, p.sv);
		Tasks.JoinAll(ctx);
		p.expired := ctx.expired
	END Body;

	PROCEDURE (k: Killer) Run;
	BEGIN
		Tasks.Sleep(15);
		Tasks.Cancel(k.dl);
		Tasks.Cancel(k.sv)
	END Run;

	PROCEDURE (p: CancelParent) Body- (VAR ctx: Tasks.Ctx);
		VAR k: Killer;
	BEGIN
		NEW(k); k.dl := p.dl; k.sv := p.sv;
		Tasks.Spawn(ctx, p.dl);
		Tasks.Spawn(ctx, p.sv);
		Tasks.Spawn(ctx, k);
		Tasks.JoinAll(ctx)
	END Body;

	PROCEDURE Do*;
		VAR p1: DeadlineParent; p2: CancelParent;
	BEGIN
		NEW(p1); p1.pipe := AsyncChan.NewBuffer(cap);
		NEW(p1.dl); p1.dl.out := p1.pipe;
		NEW(p1.sv); p1.sv.in := p1.pipe;
		Tasks.Start(p1);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		deadlineExpired := p1.expired;
		deadlinePartial := p1.sv.saved < total;

		NEW(p2); p2.pipe := AsyncChan.NewBuffer(cap);
		NEW(p2.dl); p2.dl.out := p2.pipe;
		NEW(p2.sv); p2.sv.in := p2.pipe;
		Tasks.Start(p2);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		cancelDl := Tasks.State(p2.dl) = Tasks.Cancelled;
		cancelSv := Tasks.State(p2.sv) = Tasks.Cancelled;
		cancelPartial := p2.sv.saved < total;

		SayBool("channel deadline expired: ", deadlineExpired);
		SayBool("channel deadline partial: ", deadlinePartial);
		SayBool("channel cancel downloader: ", cancelDl);
		SayBool("channel cancel saver: ", cancelSv);
		SayBool("channel cancel partial: ", cancelPartial)
	END Do;

END ObxDownChan.
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/DownChan.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxDownChan.Do
```

In the console:

```sh
echo 'ObxDownChan.Do' | tools/run64.sh
```

The log shows:

```
Tasks: cancelled
Tasks: cancelled
channel deadline expired: yes
channel deadline partial: yes
channel cancel downloader: yes
channel cancel saver: yes
channel cancel partial: yes
```

## What happened

Channel `AsyncChan.Buffer` holds up to three messages. `SendCtx` puts a message into a non-full
buffer and returns at once; into a full buffer it waits until the saver frees a place. `RecvCtx`
takes the first message from a non-empty buffer; from an empty one it waits. Both procedures receive
the pass and can be interrupted: on a stop `SendCtx` does not send the message and returns `FALSE`,
`RecvCtx` returns `FALSE` and `NIL`. Inside, both make one selection alternative and call
`SelectCtx`; on a stop `SelectCtx` withdraws the alternative and returns −1.

The first phase repeats the schedule of `ObxDownCtx`. The parent sets a deadline of 40 ms, starts
the downloader and the saver with `Spawn` and waits in `JoinAll`. The downloader sleeps 5 ms per
chunk and does not manage to put twenty chunks: the deadline expires, `JoinAll` cancels the group,
and both tasks leave on `FALSE` from `SendCtx` and `RecvCtx`. Hence
`channel deadline expired: yes` and `channel deadline partial: yes`.

The second phase also starts `Killer`, an ordinary task with method `Run`. After 15 ms it calls
`Tasks.Cancel` for the downloader and the saver. Cancellation sets the flag and wakes the task; the
channel sees the stop at the next switch point, withdraws the alternative and returns `FALSE`. After
`JoinAll` the parent checks `Tasks.State`: both tasks are in state `Cancelled`, and the saver took
fewer than twenty chunks. Hence the three `yes` answers in the second group.

The lines `Tasks: cancelled` are printed by module `Tasks` when a cancelled task wakes up inside
`Tasks.Sleep`. Here there are two: the downloader sleeps in each of the two phases, and both
cancellations interrupt the sleep with trap 127. The saver waits in `RecvCtx`, where a stop returns
`FALSE` without a line in the log.

The comparison with `ObxDownCtx` is visible in the code. There the pipe is an object with two
operations and `Ready`, and the wait is `Await`; here the pipe is a channel, and the wait is hidden
in `SendCtx` and `RecvCtx`. The caller writes equally briefly, but the owner of the state differs:
in the first case the object's author writes it, in the second the channel library does.

In this example we have seen how the same parent, children, deadline and cancellation are written on
a channel with a pass, and how a channel differs from a monitor object in the amount of caller code.
