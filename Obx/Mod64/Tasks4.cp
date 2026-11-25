MODULE ObxTasks4;
	IMPORT Tasks;

	TYPE Lonely = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (l: Lonely) Run;
	BEGIN
		Tasks.Park	(* разбудить некому *)
	END Run;

	PROCEDURE Do*;
		VAR l: Lonely;
	BEGIN
		NEW(l); Tasks.SetName(l, "lonely"); Tasks.Start(l);
		Tasks.Step;
		IF Tasks.Deadlocked() THEN Tasks.Cancel(l); Tasks.Step END
	END Do;

END ObxTasks4.
