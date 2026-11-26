MODULE ObxTasks2;
	IMPORT Tasks, Console, Strings;

	CONST clients = 100;

	TYPE
		Request = POINTER TO RECORD
			arg, res: INTEGER;
			done: BOOLEAN;
			next: Request
		END;

		SubmitOp = RECORD (Tasks.Op) r: Request; cond: Done END;
		TakeOp = RECORD (Tasks.Op) r: Request END;
		AnswerOp = RECORD (Tasks.Op) r: Request; res: INTEGER END;

		Done = POINTER TO RECORD (Tasks.Cond) r: Request END;

		Queue = POINTER TO RECORD (Tasks.Object)
			head, tail: Request
		END;

		Client = POINTER TO RECORD (Tasks.Job) q: Queue; n: INTEGER END;
		Server = POINTER TO RECORD (Tasks.Job) q: Queue END;

	VAR queue: Queue; sum: INTEGER;

	PROCEDURE (c: Done) Ready (o: Tasks.Object): BOOLEAN;
	BEGIN
		RETURN c.r.done
	END Ready;

	PROCEDURE (q: Queue) Ready* (): SET;
	BEGIN
		IF q.head # NIL THEN RETURN {0} ELSE RETURN {} END
	END Ready;

	PROCEDURE (q: Queue) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: SubmitOp DO
			op.r.next := NIL;
			IF q.tail = NIL THEN q.head := op.r ELSE q.tail.next := op.r END;
			q.tail := op.r;
			op.cond.r := op.r;
			IF ~q.AwaitCond(ctx, op.cond) THEN RETURN END
		| op: TakeOp DO
			IF ~q.Await(ctx, {0}) THEN op.r := NIL; RETURN END;
			op.r := q.head; q.head := op.r.next;
			IF q.head = NIL THEN q.tail := NIL END
		| op: AnswerOp DO
			op.r.res := op.res; op.r.done := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Request; cond: Done; op: SubmitOp;
	BEGIN
		NEW(r); r.arg := c.n; NEW(cond);
		op.r := r; op.cond := cond;
		c.q.Do(ctx, op);
		sum := sum + r.res
	END Body;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; take: TakeOp; answer: AnswerOp;
	BEGIN
		FOR i := 1 TO clients DO
			take.r := NIL; s.q.Do(ctx, take);
			answer.r := take.r; answer.res := take.r.arg * take.r.arg;
			s.q.Do(ctx, answer)
		END
	END Body;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; NEW(queue);
		NEW(s); s.q := queue; Tasks.Start(s);
		FOR i := 1 TO clients DO
			NEW(c); c.q := queue; c.n := i; Tasks.Start(c)
		END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(sum, str);
		Console.WriteStr("sum = "); Console.WriteStr(str); Console.WriteLn
	END Do;

END ObxTasks2.
