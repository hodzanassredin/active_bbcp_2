# Overview by Example: ObxTasks2

A server and a hundred clients on the module's monitor.

A hundred clients send requests to a server and wait for answers. Each client is a task with
straight text: put a request, wait, add the answer. Operations over the monitor-object `Queue`
protect the shared request queue, and waiting is written as an `AwaitCond` condition. The example
belongs to the guide `Docu/Tasks.md`.

```
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
```

The module is built by `tools/build64.sh` (the source is `Obx/Mod64/Tasks2.cp`; it is for amd64
only). In the BlackBox window a commander executes the command:

```
   ObxTasks2.Do
```

In the console:

```sh
echo 'ObxTasks2.Do' | tools/run64.sh
```

The log shows:

```
sum = 338350
```

## What happened

The command starts the server and a hundred clients. A client calls the operation `SubmitOp`:
it appends the request to the queue and waits in `AwaitCond` with a `Done` condition for this
request. While the condition is false, the client releases the monitor and sleeps; the next client
or the server takes the monitor.

The server calls `TakeOp` in a loop. If the queue is empty, `Await` on flag `0` puts it to sleep;
when a client leaves the monitor, the waiters recheck their conditions, and the server wakes.
`AnswerOp` writes the answer and sets `r.done`. The `Done` condition holds the client's request, so
exactly the client whose request is done wakes up.

Line `sum := sum + r.res` stands outside the monitor: there is no switch point between the read and
the write of `sum`. The result is the sum of the squares from 1 to 100.

In this example we have seen operations of a shared monitor-object, waiting in `AwaitCond` with a
condition that uses the waiter's request, and that a hundred client tasks are written in the same
straight text as one.
