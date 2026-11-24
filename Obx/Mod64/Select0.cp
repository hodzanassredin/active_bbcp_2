MODULE ObxSelect0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Services, AsyncIO, Console;

	TYPE
		Reply = POINTER TO RECORD (AsyncSelect.Msg) END;
		Server = POINTER TO RECORD (Tasks.Task) delay: INTEGER; out: AsyncChan.Mailbox END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR fast, slow: ARRAY 16 OF CHAR; done: BOOLEAN;

	PROCEDURE (s: Server) Run;
		VAR r: Reply;
	BEGIN
		Tasks.Sleep(s.delay); NEW(r); s.out.Send(r)
	END Run;

	PROCEDURE Ask (delay: INTEGER; OUT res: ARRAY OF CHAR);
		VAR s: Server; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox(); Tasks.Start(s);
		AsyncChan.RecvAlt(s.out, ops[0]);
		AsyncChan.TimeoutAlt(50, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN res := "reply" ELSE res := "timeout" END
	END Ask;

	PROCEDURE (c: Client) Run;
	BEGIN
		Ask(10, fast); Ask(200, slow); done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: Client;
	BEGIN
		NEW(c); done := FALSE; Tasks.Start(c);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxSelect0: fast="); Console.WriteStr(fast);
		Console.WriteStr(" slow="); Console.WriteStr(slow); Console.WriteLn
	END Do;

END ObxSelect0.
