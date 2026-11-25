MODULE ObxRestart;
(** Задача создаётся один раз и запускается многократно.

	Одна задача — указатель в переменной модуля, созданный при загрузке.
	Команда Do запускает её n раз подряд. Набор N задач создан один раз
	массивом; Batch запускает всю пачку rounds раз.

	После прогрева повторный запуск не выделяет памяти: стек возвращается
	в пул и берётся оттуда, запись реестра стека привязывается заново.
	Output — число прогонов и выделений. *)

	IMPORT Tasks, Kernel, Console, Strings;

	CONST
		n = 10000;		(* прогонов одной задачи *)
		batchN = 8;		(* задач в наборе *)
		rounds = 100;		(* пачек *)

	TYPE
		RepeatTask = POINTER TO RECORD (Tasks.Task) runs: INTEGER END;

	VAR
		one: RepeatTask;
		batch: ARRAY batchN OF RepeatTask;
		k: INTEGER;

	PROCEDURE (t: RepeatTask) Run;
	BEGIN
		INC(t.runs)
	END Run;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Console.WriteStr(what); Strings.IntToString(x, s);
		Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Drain;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Drain;

	PROCEDURE Do*;
		VAR i: INTEGER; a0: LONGINT;
	BEGIN
		Tasks.Start(one); Drain;	(* прогрев *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO n DO Tasks.Start(one); Drain END;
		Say("single task runs=", one.runs);
		Say("single task allocations=", Kernel.AllocCount() - a0)
	END Do;

	PROCEDURE Batch*;
		VAR i: INTEGER; a0, total: LONGINT;
	BEGIN
		FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
		Drain;	(* прогрев всех стеков *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO rounds DO
			FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
			Drain
		END;
		total := 0;
		FOR k := 0 TO batchN - 1 DO INC(total, batch[k].runs) END;
		Say("batch runs=", total);
		Say("batch allocations=", Kernel.AllocCount() - a0)
	END Batch;

BEGIN
	NEW(one);
	FOR k := 0 TO batchN - 1 DO NEW(batch[k]) END
END ObxRestart.
