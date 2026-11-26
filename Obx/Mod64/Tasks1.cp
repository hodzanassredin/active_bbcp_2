MODULE ObxTasks1;
(** Задачи нижнего уровня: пример показывает Park, Unpark, Sleep и Join. *)
	IMPORT Tasks, Console;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) who: Tasks.Task END;
		Main = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Console.WriteStr("sleeper: park"); Console.WriteLn;
		Tasks.Park;
		Console.WriteStr("sleeper: woken"); Console.WriteLn
	END Run;

	PROCEDURE (w: Waker) Run;
	BEGIN
		Tasks.Sleep(20);
		Console.WriteStr("waker: unpark"); Console.WriteLn;
		Tasks.Unpark(w.who)
	END Run;

	PROCEDURE (m: Main) Run;
		VAR s: Sleeper; w: Waker;
	BEGIN
		NEW(s); Tasks.Start(s);
		NEW(w); w.who := s; Tasks.Start(w);
		Tasks.Join(s); Tasks.Join(w);
		Console.WriteStr("main: both finished"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR m: Main;
	BEGIN
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxTasks1.
