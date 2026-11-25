MODULE ObxChan1;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; max: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (p: Producer) Run;
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO
			NEW(m); m.n := i; p.c.Send(m);
			IF p.c.count > p.max THEN p.max := p.c.count END
		END;
		p.c.Send(NIL)
	END Run;

	PROCEDURE (k: Consumer) Run;
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			k.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO k.sum := k.sum + m.n END;
			Tasks.Sleep(1)	(* потребитель медленнее производителя *)
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Buffer; p: Producer; k: Consumer; s: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewBuffer(4);
		NEW(p); p.c := c; NEW(k); k.c := c; done := FALSE;
		Tasks.Start(p); Tasks.Start(k);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxChan1: sum="); Strings.IntToString(k.sum, s); Console.WriteStr(s);
		Console.WriteStr(" max="); Strings.IntToString(p.max, s); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan1.
