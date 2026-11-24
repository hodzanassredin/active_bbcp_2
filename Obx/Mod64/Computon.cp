MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Services, AsyncIO, Console, Strings;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (cl: Client) Run;
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		IF r # NIL THEN
			WITH r: Square DO Strings.IntToString(r.y, d) END
		ELSE
			Strings.IntToString(AsyncComputon.LastError(), d)
		END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Do;

END ObxComputon.
