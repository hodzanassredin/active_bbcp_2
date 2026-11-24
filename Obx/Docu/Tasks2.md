# Overview by Example: ObxTasks2

A server and a hundred clients on the module's monitor.

A hundred clients send requests to a server and wait for answers. Each client is a task with
straight text: put a request, wait, add the answer. Exclusive procedures protect the shared request
queue, and waiting is written as an `ACTIVE.AWAIT` condition. The example belongs to the guide `Docu/Tasks.md`.

```
MODULE ObxTasks2;
	IMPORT ACTIVE, Tasks, Console, Strings;

	CONST clients = 100;

	TYPE
		Request = POINTER TO RECORD
			arg, res: INTEGER;
			done: BOOLEAN;
			next: Request
		END;
		Client = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Server = POINTER TO RECORD (Tasks.Task) END;

	VAR head, tail: Request; sum: INTEGER;

	PROCEDURE [exclusive] Submit (r: Request);
	BEGIN
		r.next := NIL;
		IF tail = NIL THEN head := r ELSE tail.next := r END;
		tail := r;
		ACTIVE.AWAIT(r.done)
	END Submit;

	PROCEDURE [exclusive] Take (VAR r: Request);
	BEGIN
		ACTIVE.AWAIT(head # NIL);
		r := head; head := r.next;
		IF head = NIL THEN tail := NIL END
	END Take;

	PROCEDURE [exclusive] Answer (r: Request; res: INTEGER);
	BEGIN
		r.res := res; r.done := TRUE
	END Answer;

	PROCEDURE (c: Client) Run;
		VAR r: Request;
	BEGIN
		NEW(r); r.arg := c.n;
		Submit(r);
		sum := sum + r.res
	END Run;

	PROCEDURE (s: Server) Run;
		VAR i: INTEGER; r: Request;
	BEGIN
		FOR i := 1 TO clients DO Take(r); Answer(r, r.arg * r.arg) END
	END Run;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; head := NIL; tail := NIL;
		NEW(s); Tasks.Start(s);
		FOR i := 1 TO clients DO NEW(c); c.n := i; Tasks.Start(c) END;
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

The command starts the server and a hundred clients. A client calls exclusive procedure `Submit`:
it appends the request to the queue and waits in `ACTIVE.AWAIT(r.done)`. While the condition is
false, the client releases the module's monitor and sleeps; the next client or the server takes
the monitor.

The server calls `Take` in a loop. If the queue is empty, `AWAIT(head # NIL)` puts it to sleep;
when a client leaves the monitor, the waiters' conditions are evaluated again, and the server
wakes. `Answer` writes the answer and sets `r.done`. Condition `r.done` refers to the client's
parameter and is evaluated on the waiting client's frame, so exactly the client whose request is
done wakes up.

Line `sum := sum + r.res` stands outside the monitor: there is no switch point between the read and
the write of `sum`. The result is the sum of the squares from 1 to 100.

In this example we have seen exclusive procedures of a module, waiting for a condition in
`ACTIVE.AWAIT` that uses the waiter's parameter, and that a hundred client tasks are written in the
same straight text as one.
