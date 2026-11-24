MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Task) END;
		Writer = POINTER TO RECORD (Tasks.Task) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Run;
	BEGIN
		got := o.ReadInt(); done := TRUE	(* ждёт, пока Writer не заполнит *)
	END Run;

	PROCEDURE (w: Writer) Run;
	BEGIN
		Tasks.Sleep(5); o.FillInt(42)
	END Run;

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
