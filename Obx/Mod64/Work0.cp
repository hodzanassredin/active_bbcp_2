MODULE ObxWork0;
	IMPORT S := SYSTEM, AsyncWork, Tasks, Services, AsyncIO, Console, Strings;

	TYPE Asker = POINTER TO RECORD (Tasks.Task) END;

	VAR req: AsyncWork.AddrReq; status: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Asker) Run;
	BEGIN
		req.host := "localhost"; req.count := 0; req.status := -1;
		status := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Asker; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(a); done := FALSE; Tasks.Start(a);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxWork0: status="); Strings.IntToString(req.status, s); Console.WriteStr(s);
		Console.WriteLn
	END Do;

END ObxWork0.
