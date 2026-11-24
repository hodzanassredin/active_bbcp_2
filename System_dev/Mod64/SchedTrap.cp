MODULE System_devSchedTrap;
(** Трап самого планировщика: внутри прохода Tasks.Step, когда текущей
	задачи нет (Tasks.Current() = NIL). Вызвать его тест может только через
	код, который Step зовёт на своём стеке: здесь — консоль. При тупике
	(живая задача ждёт, готовых и сроков нет) Step печатает сообщение через
	Console, и подставленная консоль трапует один раз.
	Проверяется: отчёт о трапе один, следующий Step исполняет задачи,
	Kernel.Try главного стека после этого ловит свой трап. *)

	IMPORT S := SYSTEM, Kernel, Console, Tasks, Libc := LinLibc, UnitAssert;

	TYPE
		TrapCons = POINTER TO RECORD (Console.Console) END;
		Sleeper = POINTER TO RECORD (Tasks.Task) done: BOOLEAN END;
		Quick = POINTER TO RECORD (Tasks.Task) done: BOOLEAN END;

	VAR
		armed: BOOLEAN;	(* следующая печать трапует *)
		trappedInTask: BOOLEAN;	(* трап пришёл при Tasks.Current() # NIL *)
		mainCaught: BOOLEAN;
		nilp: POINTER TO RECORD x: INTEGER END;

	PROCEDURE Out (IN s: ARRAY OF CHAR);
		VAR b: ARRAY 256 OF SHORTCHAR; i: INTEGER; r: LONGINT;
	BEGIN
		i := 0; WHILE (i < LEN(b) - 1) & (s[i] # 0X) DO b[i] := SHORT(s[i]); INC(i) END;
		IF i > 0 THEN r := Libc.write(1, S.ADR(b), i) END
	END Out;

	PROCEDURE (c: TrapCons) WriteStr- (IN s: ARRAY OF CHAR);
	BEGIN
		IF armed THEN
			armed := FALSE; trappedInTask := Tasks.Current() # NIL;
			HALT(77)
		END;
		Out(s)
	END WriteStr;

	PROCEDURE (c: TrapCons) WriteChar- (ch: CHAR);
		VAR s: ARRAY 2 OF CHAR;
	BEGIN
		s[0] := ch; s[1] := 0X; Out(s)
	END WriteChar;

	PROCEDURE (c: TrapCons) WriteLn-;
	BEGIN
		Out(0AX)
	END WriteLn;

	PROCEDURE (c: TrapCons) ReadLn- (OUT s: ARRAY OF CHAR);
	BEGIN
		s := ""
	END ReadLn;

	PROCEDURE (t: Sleeper) Run;
	BEGIN
		Tasks.Park; t.done := TRUE
	END Run;

	PROCEDURE (t: Quick) Run;
	BEGIN
		Tasks.Yield; t.done := TRUE
	END Run;

	PROCEDURE MainTrap (a, b, c: S.ADDRESS);
	BEGIN
		mainCaught := TRUE;
		nilp.x := 1;	(* трап: NIL *)
		mainCaught := FALSE
	END MainTrap;

	PROCEDURE TestProcSchedulerTrap*;
		VAR c: TrapCons; p: Sleeper; q: Quick; n, i: INTEGER;
	BEGIN
		(* Процесс теста отдельный (TestProc…): консоль не восстанавливается,
		   печать после теста идёт прямо в stdout. *)
		NEW(c); Console.SetConsole(c);
		NEW(p); Tasks.Start(p);
		Tasks.Step;	(* p запарковалась *)
		UnitAssert.True(Tasks.Deadlocked(), "first pass already saw the deadlock");
		(* Тупик печатается один раз до пробуждения; Start снимает признак.
		   Задача q исполняется, после неё — снова тупик и печать в том же проходе. *)
		NEW(q); Tasks.Start(q);
		n := Kernel.trapCount; armed := TRUE;
		FOR i := 1 TO 3 DO Tasks.Step END;
		UnitAssert.True(q.done, "q finished");
		UnitAssert.True(~armed, "scheduler printed and trapped");
		UnitAssert.True(~trappedInTask, "trap came with no current task");
		UnitAssert.EqualInt(n + 1, Kernel.trapCount, "scheduler trap reported once");
		UnitAssert.True(~Tasks.InTask(), "no task left current after the trap");
		(* (2) следующий Step работает: новая задача и запаркованная исполняются. *)
		NEW(q); Tasks.Start(q); Tasks.Unpark(p);
		FOR i := 1 TO 5 DO Tasks.Step END;
		UnitAssert.True(q.done, "new task ran after scheduler trap");
		UnitAssert.True(p.done, "parked task ran after scheduler trap");
		UnitAssert.EqualInt(0, Tasks.Count(), "no task lost");
		(* (3) главный стек: свой Kernel.Try ловит свой трап. *)
		n := Kernel.trapCount;
		Kernel.Try(MainTrap, 0, 0, 0);
		UnitAssert.True(mainCaught, "main Try caught its trap");
		UnitAssert.EqualInt(n + 1, Kernel.trapCount, "main trap reported once")
	END TestProcSchedulerTrap;

END System_devSchedTrap.
