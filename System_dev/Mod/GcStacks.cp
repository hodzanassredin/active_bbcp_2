MODULE System_devGcStacks;
(** Замер сборки мусора при многих спящих задачах.

	N задач создаются и паркуются в Tasks.Park; у каждой на стеке лежит
	локальная ссылка на свой объект (куча), больше на него не ссылается
	никто. Затем один Kernel.Collect, время — System_devClock (наносекунды).

	Команды: Do1000, Do10000, Do20000, Do50000, Do100000. Каждый запуск —
	отдельный процесс: задачи живут до конца процесса, повторять замер в
	одном процессе значило бы мерить уже разогретую кучу с прежними
	задачами.

	В манифест tools/tests.txt не входит: это замер, а не тест.

	Печать: "BENCH gcstacks n=<N> ns=<...> alloc=<...> tasks=<...>". *)

	IMPORT Kernel, Tasks, Console, Strings, System_devClock;

	CONST
		stackSize = 16 * 1024;	(* маленький стек: 100 тыс. задач — 1,6 ГБ адресов *)

	TYPE
		Node = POINTER TO RECORD
			tag: LONGINT;
			pad: ARRAY 4 OF LONGINT
		END;
		Sleeper = POINTER TO RECORD (Tasks.Task)
			id: INTEGER
		END;

	VAR
		parked: INTEGER;
		failed: BOOLEAN;

	PROCEDURE (t: Sleeper) Run;
		VAR p: Node;
	BEGIN
		NEW(p); p.tag := t.id;
		INC(parked);
		Tasks.Park;
		(* ссылка нужна после Park: компилятор обязан держать её на стеке *)
		IF (p = NIL) OR (p.tag # t.id) THEN failed := TRUE END
	END Run;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Bench (n: INTEGER);
		VAR i: INTEGER; t: Sleeper; t0, t1, ns: LONGINT;
	BEGIN
		Kernel.blockCollect := TRUE;	(* сборки во время создания задач не нужны *)
		parked := 0; failed := FALSE;
		FOR i := 1 TO n DO
			NEW(t); t.id := i; Tasks.SetStackSize(t, stackSize); Tasks.Start(t)
		END;
		WHILE parked < n DO Tasks.Step END;
		Kernel.blockCollect := FALSE;
		t0 := System_devClock.now();
		Kernel.Collect;
		t1 := System_devClock.now();
		ns := t1 - t0;
		W("BENCH gcstacks n="); N(n);
		W(" ns="); N(ns);
		W(" alloc="); N(Kernel.Allocated());
		W(" tasks="); N(Tasks.Count());
		IF failed THEN W(" FAILED") END;
		Console.WriteLn
	END Bench;

	PROCEDURE Do0*;
	BEGIN
		Bench(0)
	END Do0;

	PROCEDURE Do1000*;
	BEGIN
		Bench(1000)
	END Do1000;

	PROCEDURE Do10000*;
	BEGIN
		Bench(10000)
	END Do10000;

	PROCEDURE Do20000*;
	BEGIN
		Bench(20000)
	END Do20000;

	PROCEDURE Do50000*;
	BEGIN
		Bench(50000)
	END Do50000;

	PROCEDURE Do100000*;
	BEGIN
		Bench(100000)
	END Do100000;

END System_devGcStacks.
