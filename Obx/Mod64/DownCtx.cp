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
