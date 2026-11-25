MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Job) END;
		Writer = POINTER TO RECORD (Tasks.Job) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		done := o.ReadInt(ctx, got)	(* ждёт, пока Writer не заполнит; FALSE — задачу остановили *)
	END Body;

	PROCEDURE (w: Writer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(5); o.FillInt(ctx, 42)
	END Body;

	PROCEDURE Do*;
		VAR r: Reader; w: Writer; s: ARRAY 16 OF CHAR;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); NEW(w); done := FALSE;
		Tasks.Start(r); Tasks.Start(w);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxOnce.
