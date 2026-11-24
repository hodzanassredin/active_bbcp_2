MODULE System_testsWinTasks;
(** Тесты задач и мониторов на Windows x64 (под wine).

	Отдельный модуль, а не System_testsTasks: тот импортирует System_testsCo с
	ucontext/libtest64.so, которых на Windows нет. Здесь только
	переносимая часть: стеки задач (VirtualAlloc + PAGE_NOACCESS),
	переключение, Yield, мониторы Kernel.Lock/Wait/Unlock, изоляция
	трапа и Join. *)

	IMPORT S := SYSTEM, Kernel, Tasks, UnitAssert, Console, Strings;

	CONST
		cap = 4;

	TYPE
		Counter = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Task) END;
		Consumer = POINTER TO RECORD (Tasks.Task) END;
		Bad = POINTER TO RECORD (Tasks.Task) END;
		Good = POINTER TO RECORD (Tasks.Task) END;
		Sleeper = POINTER TO RECORD (Tasks.Task) woke: BOOLEAN END;
		Joiner = POINTER TO RECORD (Tasks.Task) target: Tasks.Task; done: BOOLEAN END;
		Buf = POINTER TO RECORD item: ARRAY cap OF INTEGER; head, tail, n: INTEGER END;

	VAR
		b: Buf;
		produced, consumed, sum, goodCount: INTEGER;
		dlg: ARRAY 64 OF CHAR;

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE (c: Counter) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 50 DO INC(c.n); Tasks.Yield END
	END Run;

	PROCEDURE TestProcYield*;
		VAR a, b: Counter;
	BEGIN
		NEW(a); NEW(b); Tasks.Start(a); Tasks.Start(b);
		UnitAssert.True(Pump(5000), "yield tasks finish");
		UnitAssert.EqualInt(100, a.n + b.n, "both tasks counted all turns")
	END TestProcYield;

	PROCEDURE Put (x: INTEGER);
	BEGIN
		Kernel.Lock(b);
		WHILE b.n = cap DO Kernel.Wait(b) END;
		b.item[b.tail] := x;
		b.tail := (b.tail + 1) MOD cap;
		INC(b.n);
		Kernel.Unlock(b)
	END Put;

	PROCEDURE Get (OUT x: INTEGER);
	BEGIN
		Kernel.Lock(b);
		WHILE b.n = 0 DO Kernel.Wait(b) END;
		x := b.item[b.head];
		b.head := (b.head + 1) MOD cap;
		DEC(b.n);
		Kernel.Unlock(b)
	END Get;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 100 DO Put(i) END;
		produced := 1
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 100 DO
			Get(x);
			IF x # i THEN consumed := -1000 END;
			INC(sum, x); INC(consumed)
		END
	END Run;

	PROCEDURE TestProcProducerConsumer*;
		VAR p: Producer; c: Consumer;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.n := 0;
		produced := 0; consumed := 0; sum := 0;
		NEW(p); NEW(c); Tasks.Start(p); Tasks.Start(c);
		UnitAssert.True(Pump(10000), "buffer tasks finish");
		UnitAssert.EqualInt(1, produced, "producer finished");
		UnitAssert.EqualInt(100, consumed, "consumer got all items in order");
		UnitAssert.EqualInt(5050, sum, "sum is correct")
	END TestProcProducerConsumer;

	PROCEDURE (b: Bad) Run;
	BEGIN
		ASSERT(FALSE, 20)
	END Run;

	PROCEDURE (g: Good) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO INC(goodCount); Tasks.Yield END
	END Run;

	PROCEDURE TestProcTrapIsolation*;
		VAR b: Bad; g: Good;
	BEGIN
		goodCount := 0;
		NEW(b); NEW(g); Tasks.Start(b); Tasks.Start(g);
		UnitAssert.True(Pump(5000), "tasks after a trap finish");
		UnitAssert.True(Tasks.Failed(b), "trapped task is marked failed");
		UnitAssert.EqualInt(10, goodCount, "other task was not affected")
	END TestProcTrapIsolation;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Tasks.Sleep(20); s.woke := TRUE
	END Run;

	PROCEDURE (j: Joiner) Run;
	BEGIN
		Tasks.Join(j.target);
		j.done := TRUE
	END Run;

	PROCEDURE TestProcJoin*;
		VAR s: Sleeper; j: Joiner;
	BEGIN
		NEW(s); s.woke := FALSE; Tasks.Start(s);
		NEW(j); j.target := s; j.done := FALSE; Tasks.Start(j);
		UnitAssert.True(Pump(2000), "join task finishes");
		UnitAssert.True(s.woke, "sleeper woke");
		UnitAssert.True(j.done, "join returned after the sleeper finished")
	END TestProcJoin;

	PROCEDURE TestProcStackMem*;
		VAR lo, hi: S.ADDRESS; ok: BOOLEAN;
	BEGIN
		ok := Kernel.AllocStack(64 * 1024, lo, hi);
		Console.WriteStr("System_testsWinTasks: stack lo="); Strings.IntToString(S.VAL(LONGINT, lo), dlg);
		Console.WriteStr(dlg); Console.WriteStr(" hi="); Strings.IntToString(S.VAL(LONGINT, hi), dlg);
		Console.WriteStr(dlg); Console.WriteLn;
		UnitAssert.True(ok, "VirtualAlloc stack memory allocated");
		UnitAssert.True(lo # 0, "stack low bound is non-zero");
		IF ok THEN Kernel.FreeStack(lo, hi) END
	END TestProcStackMem;

END System_testsWinTasks.
