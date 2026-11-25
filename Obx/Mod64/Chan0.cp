MODULE ObxChan0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Sender = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan END;
		Receiver = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Run;

	PROCEDURE (r: Receiver) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan0: sum="); Console.WriteStr(d); Console.WriteLn
	END Do;

END ObxChan0.
