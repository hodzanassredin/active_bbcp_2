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
