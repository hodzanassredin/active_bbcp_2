MODULE ObxChan2;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Event = POINTER TO RECORD (AsyncSelect.Msg) code: INTEGER END;
		Handler = POINTER TO RECORD (Tasks.Job) n: INTEGER END;

	VAR box: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Post (code: INTEGER);
	(* Вызывается с главного стека: ящик не заставляет ждать. *)
		VAR e: Event;
	BEGIN
		NEW(e); e.code := code; box.Send(e)
	END Post;

	PROCEDURE (h: Handler) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			box.Recv(m);
			WITH m: Event DO
				IF m.code = 0 THEN EXIT END;
				INC(h.n)
			END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR h: Handler; s: ARRAY 16 OF CHAR;
	BEGIN
		box := AsyncChan.NewMailbox();
		Post(1); Post(2); Post(3); Post(0);
		NEW(h); done := FALSE; Tasks.Start(h);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(h.n, s);
		Console.WriteStr("ObxChan2: events="); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan2.
