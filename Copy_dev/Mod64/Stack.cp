MODULE Copy_devStack;
(** Тесты и замеры пробы CopyStack.

	Объект исполняется на общем стеке исполнителя; на время ожидания
	участок стека уезжает в кучу и возвращается на те же адреса.
	Проверяются: локальные переменные всех видов на глубине 1/5/50,
	VAR-параметр во внешний кадр, вложенная процедура со статической
	связью, обмен производителя и потребителя, сборка при спящих, трап до
	и после Wait, Kernel.Try вокруг ждущего кода, 100 тыс. ждущих.

	Замеры (Bench…) не входят в прогон Unit: их зовут отдельными
	командами. *)

	IMPORT S := SYSTEM, Kernel, CopyStack, Tasks, UnitAssert, System_devCRaw,
		Console, Strings;

	CONST
		depthOne = 1; depthFive = 5; depthFifty = 50;
		pcItems = 100;
		many = 100000;

	TYPE
		Rec = RECORD a: INTEGER; b: REAL; p: S.ADDRESS END;

		Holder = POINTER TO RECORD tag: INTEGER; data: ARRAY 64 OF INTEGER END;

		Deep = POINTER TO RECORD (CopyStack.Object)
			depth, err: INTEGER
		END;

		GcObj = POINTER TO RECORD (CopyStack.Object)
			err: INTEGER
		END;

		Churner = POINTER TO RECORD (CopyStack.Object) END;

		TrapObj = POINTER TO RECORD (CopyStack.Object)
			when, reached: INTEGER; h: Holder
		END;

		TryObj = POINTER TO RECORD (CopyStack.Object)
			caught, done: BOOLEAN; h: Holder
		END;

		Box = POINTER TO RECORD
			n, in, out: INTEGER;
			data: ARRAY 4 OF INTEGER
		END;

		Prod = POINTER TO RECORD (CopyStack.Object)
			box: Box; cons: S.ADDRESS; left: INTEGER
		END;

		Cons = POINTER TO RECORD (CopyStack.Object)
			box: Box; prod: S.ADDRESS; sum, next, err: INTEGER
		END;

		Waiter = POINTER TO RECORD (CopyStack.Object)
			id, done: INTEGER
		END;

		Pong = POINTER TO RECORD (CopyStack.Object)
			mate: Pong; role, depth, rounds, err: INTEGER
		END;

		MinPong = POINTER TO RECORD (CopyStack.Object)
			mate: MinPong; role, rounds, err: INTEGER
		END;

		PingTask = POINTER TO RECORD (Tasks.Task) rounds: INTEGER END;

		Sleeper = POINTER TO RECORD (Tasks.Task) END;

		NestObj = POINTER TO RECORD (CopyStack.Object)
			inner: NestObj; err: INTEGER
		END;

	VAR
		churn: POINTER TO ARRAY OF Holder;
		box: Box;
		waiters: POINTER TO ARRAY OF Waiter;
		woken: POINTER TO ARRAY OF BOOLEAN;
		manyErr: INTEGER;

	(* --- вывод ------------------------------------------------------------ *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s) END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Ln;
	BEGIN Console.WriteLn END Ln;

	PROCEDURE Step1;
		VAR n: INTEGER;
	BEGIN n := CopyStack.Step() END Step1;

	(* --- глубина и локальные переменные ----------------------------------- *)

	PROCEDURE CheckLevel (o: Deep; d: INTEGER; VAR outer: INTEGER);
		VAR num, i: INTEGER; r: REAL; rec: Rec; arr: ARRAY 8 OF INTEGER;

		PROCEDURE Inner;
		(* Вложенная процедура: статическая связь на кадр CheckLevel. *)
		BEGIN
			IF (num # d * 10) OR (rec.a # d) OR (arr[0] # d) OR (ABS(rec.b - d * 0.5) > 1.0E-6) THEN o.err := 10 + d END
		END Inner;

	BEGIN
		num := d * 10; r := d * 0.5; rec.a := d; rec.b := d * 0.5; rec.p := S.ADR(outer);
		i := 0; WHILE i < LEN(arr) DO arr[i] := d + i; INC(i) END;
		IF d > 0 THEN
			CheckLevel(o, d - 1, outer)
		ELSE
			Inner;
			o.Wait;
			Inner;
			outer := 2000	(* через VAR-цепочку — в кадр Exclusive *)
		END;
		(* После возврата из ожидания или рекурсии всё на месте. *)
		IF (num # d * 10) OR (ABS(r - d * 0.5) > 1.0E-6) OR (rec.a # d) OR (ABS(rec.b - d * 0.5) > 1.0E-6) THEN o.err := 20 + d END;
		IF (arr[0] # d) OR (arr[7] # d + 7) THEN o.err := 30 + d END;
		IF outer # 2000 THEN o.err := 40 + d END
	END CheckLevel;

	PROCEDURE (o: Deep) Exclusive-;
		VAR outer: INTEGER;
	BEGIN
		outer := 1000;
		CheckLevel(o, o.depth - 1, outer);
		IF outer # 2000 THEN o.err := 50 END
	END Exclusive;

	PROCEDURE RunDeep (depth: INTEGER): INTEGER;
		VAR o: Deep;
	BEGIN
		NEW(o); o.depth := depth; o.err := 0;
		CopyStack.Start(o);
		Step1;
		IF CopyStack.State(o) # CopyStack.Waiting THEN RETURN 1 END;
		o.Wake;
		Step1;
		IF CopyStack.State(o) # CopyStack.Finished THEN RETURN 2 END;
		IF CopyStack.IsFailed(o) THEN RETURN 3 END;
		RETURN o.err
	END RunDeep;

	PROCEDURE TestProcDepth*;
	BEGIN
		UnitAssert.EqualInt(0, RunDeep(depthOne), "depth 1: locals, VAR, static link");
		UnitAssert.EqualInt(0, RunDeep(depthFive), "depth 5");
		UnitAssert.EqualInt(0, RunDeep(depthFifty), "depth 50")
	END TestProcDepth;

	(* --- сборка мусора при спящем ----------------------------------------- *)

	PROCEDURE (o: GcObj) Exclusive-;
		VAR h: Holder; i: INTEGER;
	BEGIN
		NEW(h); h.tag := 12345;
		i := 0; WHILE i < LEN(h.data) DO h.data[i] := i; INC(i) END;
		Kernel.Collect; Kernel.Collect;
		IF (h.tag # 12345) OR (h.data[7] # 7) THEN o.err := 1 END;
		o.Wait;
		IF h.tag # 12345 THEN o.err := 3
		ELSIF h.data[7] # 7 THEN o.err := 4
		ELSIF h.data[63] # 63 THEN o.err := 5
		END
	END Exclusive;

	PROCEDURE (o: Churner) Exclusive-;
		VAR pad: ARRAY 2048 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < LEN(pad) DO pad[i] := i; INC(i) END
	END Exclusive;

	PROCEDURE HeapChurn (n: INTEGER);
		VAR i, j: INTEGER;
	BEGIN
		NEW(churn, n);
		i := 0;
		WHILE i < n DO
			NEW(churn[i]); churn[i].tag := -1;
			j := 0; WHILE j < LEN(churn[i].data) DO churn[i].data[j] := -2; INC(j) END;
			INC(i)
		END
	END HeapChurn;

	PROCEDURE TestProcGc*;
		VAR o: GcObj; c: Churner; adr: S.ADDRESS;
	BEGIN
		(* Адрес заставляет компилятор держать o в слоте стека: сборщик
		   сканирует память, а не регистры. *)
		NEW(o); o.err := 0; adr := S.ADR(o);
		CopyStack.Start(o);
		Step1;
		UnitAssert.EqualInt(CopyStack.Waiting, CopyStack.State(o), "object sleeps");
		(* Другой объект затирает общий стек: копия — единственный корень. *)
		NEW(c); CopyStack.Start(c); Step1;
		Kernel.Collect; Kernel.Collect;
		HeapChurn(2000);
		Kernel.Collect;
		o.Wake;
		Step1;
		UnitAssert.EqualInt(CopyStack.Finished, CopyStack.State(o), "object finished");
		UnitAssert.EqualInt(0, o.err, "heap object survived collection while sleeping");
		churn := NIL
	END TestProcGc;

	(* --- трап до и после ожидания ----------------------------------------- *)

	PROCEDURE (o: TrapObj) Exclusive-;
	BEGIN
		o.reached := 1;
		IF o.when = 0 THEN o.h := NIL; o.h.tag := 1 END;
		o.reached := 2;
		o.Wait;
		IF o.when = 1 THEN o.h := NIL; o.h.tag := 1 END;
		o.reached := 3
	END Exclusive;

	PROCEDURE TrapAlive (): BOOLEAN;
	(* После трапа другой объект работает. *)
		VAR w: Waiter;
	BEGIN
		NEW(w); CopyStack.Start(w);
		Step1;
		IF CopyStack.State(w) = CopyStack.Waiting THEN w.Wake; Step1 END;
		RETURN CopyStack.State(w) = CopyStack.Finished
	END TrapAlive;

	PROCEDURE TestProcTrap*;
		VAR o: TrapObj;
	BEGIN
		(* Трап до Wait. *)
		NEW(o); o.when := 0; o.reached := 0;
		CopyStack.Start(o);
		Step1;
		UnitAssert.EqualInt(CopyStack.Failed, CopyStack.State(o), "trap before Wait: failed");
		UnitAssert.True(CopyStack.IsFailed(o), "trap before Wait: flag");
		UnitAssert.EqualInt(1, o.reached, "trap before Wait: wait not reached");
		UnitAssert.True(TrapAlive(), "trap before Wait: others work");
		(* Трап после Wait. *)
		NEW(o); o.when := 1; o.reached := 0;
		CopyStack.Start(o);
		Step1;
		UnitAssert.EqualInt(CopyStack.Waiting, CopyStack.State(o), "trap after Wait: sleeps");
		o.Wake;
		Step1;
		UnitAssert.EqualInt(CopyStack.Failed, CopyStack.State(o), "trap after Wait: failed");
		UnitAssert.EqualInt(2, o.reached, "trap after Wait: resumed first");
		UnitAssert.True(TrapAlive(), "trap after Wait: others work")
	END TestProcTrap;

	(* --- вложенный Do ---------------------------------------------------- *)

	PROCEDURE (o: NestObj) Exclusive-;
	BEGIN
		IF o.inner # NIL THEN o.inner.Do END;	(* запрещено: трап 30 *)
		o.err := 1
	END Exclusive;

	PROCEDURE TestProcNested*;
	(** Вложенный Do запрещён: внешний объект ловит трап и кончается
		Failed, тело дальше не идёт, остальные объекты живы. *)
		VAR a, b: NestObj;
	BEGIN
		NEW(a); NEW(b); a.inner := b; b.inner := NIL; a.err := 0; b.err := 0;
		CopyStack.Start(a); Step1;
		UnitAssert.EqualInt(CopyStack.Failed, CopyStack.State(a), "nested Do: outer failed");
		UnitAssert.EqualInt(0, a.err, "nested Do: body did not continue");
		UnitAssert.True(TrapAlive(), "nested Do: others work")
	END TestProcNested;

	(* --- Kernel.Try вокруг ждущего кода ----------------------------------- *)

	PROCEDURE TryBody (a, b, c: S.ADDRESS);
		VAR o: TryObj;
	BEGIN
		o := S.VAL(TryObj, a);
		o.Wait;
		o.h := NIL; o.h.tag := 1	(* трап после пробуждения, внутри Try *)
	END TryBody;

	PROCEDURE (o: TryObj) Exclusive-;
	BEGIN
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(TryBody)), S.VAL(S.ADDRESS, o), 0, 0);
		o.caught := TRUE;
		o.Wait;
		o.done := TRUE
	END Exclusive;

	PROCEDURE TestProcTryWait*;
		VAR o: TryObj;
	BEGIN
		NEW(o); o.caught := FALSE; o.done := FALSE;
		CopyStack.Start(o);
		Step1;
		UnitAssert.EqualInt(CopyStack.Waiting, CopyStack.State(o), "Try: waits inside Try");
		o.Wake;
		Step1;
		UnitAssert.True(o.caught, "Try: trap after wait caught by its own Try");
		UnitAssert.EqualInt(CopyStack.Waiting, CopyStack.State(o), "Try: waits again after trap");
		o.Wake;
		Step1;
		UnitAssert.True(o.done, "Try: continued after caught trap");
		UnitAssert.EqualInt(CopyStack.Finished, CopyStack.State(o), "Try: finished")
	END TestProcTryWait;

	(* --- производитель и потребитель -------------------------------------- *)

	PROCEDURE (p: Prod) Exclusive-;
		VAR i: INTEGER; c: Cons;
	BEGIN
		c := S.VAL(Cons, p.cons);
		i := 1;
		WHILE i <= p.left DO
			WHILE p.box.in - p.box.out = LEN(p.box.data) DO p.Wait END;
			p.box.data[p.box.in MOD LEN(p.box.data)] := i;
			INC(p.box.in);
			c.Wake;
			INC(i)
		END;
		p.box.n := p.box.in;
		c.Wake
	END Exclusive;

	PROCEDURE (c: Cons) Exclusive-;
		VAR v: INTEGER; p: Prod;
	BEGIN
		p := S.VAL(Prod, c.prod);
		LOOP
			IF c.box.in = c.box.out THEN
				IF c.box.n > 0 THEN EXIT END;
				c.Wait
			ELSE
				v := c.box.data[c.box.out MOD LEN(c.box.data)];
				INC(c.box.out);
				INC(c.sum, v);
				IF v # c.next THEN c.err := v END;
				INC(c.next);
				p.Wake
			END
		END
	END Exclusive;

	PROCEDURE TestProcDrop*;
	(** Ждущий объект, на который не осталось ссылок: сборщик освобождает
		его, финализатор снимает копию с учёта, исполнитель продолжает
		работать. *)
		VAR o: Waiter; f0, f1: LONGINT; w: Waiter;
	BEGIN
		f0 := CopyStack.Finalized();
		NEW(o); CopyStack.Start(o); Step1;
		UnitAssert.EqualInt(CopyStack.Waiting, CopyStack.State(o), "drop: object sleeps");
		o := NIL;
		Kernel.Collect; Kernel.Collect; Kernel.Collect;
		CopyStack.Trim;
		f1 := CopyStack.Finalized();
		UnitAssert.True(f1 > f0, "drop: finalizer ran");
		NEW(w); CopyStack.Start(w); Step1;
		IF CopyStack.State(w) = CopyStack.Waiting THEN w.Wake; Step1 END;
		UnitAssert.EqualInt(CopyStack.Finished, CopyStack.State(w), "drop: executor still works")
	END TestProcDrop;

	PROCEDURE TestProcProdCons*;
		VAR i, expect: INTEGER; prod: Prod; cons: Cons;
	BEGIN
		NEW(box); box.n := 0; box.in := 0; box.out := 0;
		NEW(prod); prod.box := box; prod.left := pcItems;
		NEW(cons); cons.box := box;
		prod.cons := S.VAL(S.ADDRESS, cons); cons.prod := S.VAL(S.ADDRESS, prod);
		cons.err := 0; cons.next := 1; cons.sum := 0;
		CopyStack.Start(cons); CopyStack.Start(prod);
		i := 0;
		WHILE CopyStack.HasReady() & (i < 100000) DO i := i + CopyStack.Step() END;
		expect := pcItems * (pcItems + 1) DIV 2;
		UnitAssert.EqualInt(CopyStack.Finished, CopyStack.State(prod), "producer finished");
		UnitAssert.EqualInt(CopyStack.Finished, CopyStack.State(cons), "consumer finished");
		UnitAssert.EqualInt(0, cons.err, "consumer saw values in order");
		UnitAssert.EqualInt(pcItems, box.out, "everything consumed");
		UnitAssert.EqualInt(expect, cons.sum, "sum of values")
	END TestProcProdCons;

	(* --- 100 тыс. ждущих -------------------------------------------------- *)

	PROCEDURE (o: Waiter) Exclusive-;
	BEGIN
		o.Wait;
		o.done := o.id + 1
	END Exclusive;

	PROCEDURE ShuffleWakeTest (n: INTEGER);
		VAR i, k, unused: INTEGER; seed: LONGINT;
	BEGIN
		NEW(waiters, n); NEW(woken, n);
		i := 0;
		WHILE i < n DO
			NEW(waiters[i]); waiters[i].id := i; waiters[i].done := 0; woken[i] := FALSE;
			CopyStack.Start(waiters[i]);
			INC(i)
		END;
		(* Все доходят до Wait. *)
		CopyStack.SetSlice(0);
		unused := 0;
		WHILE CopyStack.HasReady() DO unused := unused + CopyStack.Step() END;
		manyErr := 0;
		i := 0;
		WHILE i < n DO
			IF CopyStack.State(waiters[i]) # CopyStack.Waiting THEN manyErr := 1 END;
			INC(i)
		END;
		(* Случайный порядок пробуждения, зерно фиксировано. *)
		seed := 12345;
		k := 0;
		WHILE k < n DO
			seed := (seed * 1103515245 + 12345) MOD 2147483648;
			i := SHORT(seed MOD n);
			IF ~woken[i] THEN
				woken[i] := TRUE;
				waiters[i].Wake;
				unused := unused + CopyStack.Step();
				IF (CopyStack.State(waiters[i]) # CopyStack.Finished) OR (waiters[i].done # i + 1) THEN manyErr := 2 END;
				INC(k)
			END
		END;
		i := 0;
		WHILE i < n DO
			IF CopyStack.State(waiters[i]) # CopyStack.Finished THEN manyErr := 3 END;
			INC(i)
		END
	END ShuffleWakeTest;

	PROCEDURE TestSlowMany*;
	BEGIN
		ShuffleWakeTest(many);
		UnitAssert.EqualInt(0, manyErr, "100k waiters: creation, random wake, values")
	END TestSlowMany;

	(* --- замеры ----------------------------------------------------------- *)

	PROCEDURE Level (o: Pong; d, rounds: INTEGER);
		VAR pad: ARRAY 128 OF INTEGER; i: INTEGER;
	BEGIN
		i := 0; WHILE i < LEN(pad) DO pad[i] := d; INC(i) END;
		IF d > 0 THEN
			Level(o, d - 1, rounds)
		ELSIF o.role = 0 THEN
			WHILE rounds > 0 DO
				o.Wait;
				o.mate.Wake;
				DEC(rounds)
			END
		ELSE
			WHILE rounds > 0 DO
				o.mate.Wake;
				o.Wait;
				DEC(rounds)
			END
		END;
		IF (pad[0] # d) OR (pad[LEN(pad) - 1] # d) THEN o.err := 1 END
	END Level;

	PROCEDURE (o: Pong) Exclusive-;
	BEGIN
		Level(o, o.depth, o.rounds)
	END Exclusive;

	PROCEDURE (t: PingTask) Run;
		VAR i: INTEGER;
	BEGIN
		i := 0; WHILE i < t.rounds DO Tasks.Yield; INC(i) END
	END Run;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Tasks.Sleep(600000)
	END Run;

	PROCEDURE BenchPair (depth, rounds: INTEGER);
		VAR a, b: Pong; t0, dt, sw0, sw1: LONGINT; seg: INTEGER; go: BOOLEAN;
	BEGIN
		NEW(a); NEW(b); a.mate := b; b.mate := a;
		a.role := 0; b.role := 1; a.depth := depth; b.depth := depth;
		a.rounds := rounds; b.rounds := rounds; a.err := 0; b.err := 0;
		CopyStack.SetSlice(100000);
		sw0 := CopyStack.Switches();
		t0 := System_devCRaw.c_now();
		CopyStack.Start(a); CopyStack.Start(b);
		go := TRUE;
		WHILE go & CopyStack.HasReady() DO
			IF CopyStack.Step() = 0 THEN go := FALSE END
		END;
		dt := System_devCRaw.c_now() - t0;
		sw1 := CopyStack.Switches();
		seg := CopyStack.SegLen(a);
		W("BENCH pair-copy depth="); N(depth); W(" seg="); N(seg);
		W(" rounds="); N(rounds); W(" ns/switch="); N(dt DIV MAX(1, sw1 - sw0));
		W(" err="); N(a.err + b.err); Ln
	END BenchPair;

	PROCEDURE (o: MinPong) Exclusive-;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		IF o.role = 0 THEN
			WHILE i < o.rounds DO o.Wait; o.mate.Wake; INC(i) END
		ELSE
			WHILE i < o.rounds DO o.mate.Wake; o.Wait; INC(i) END
		END
	END Exclusive;

	PROCEDURE BenchMin (rounds: INTEGER);
	(** Пара без рекурсии: наименьший участок, который даёт Do и Try. *)
		VAR a, b: MinPong; t0, dt, sw0, sw1: LONGINT; go: BOOLEAN;
	BEGIN
		NEW(a); NEW(b); a.mate := b; b.mate := a;
		a.role := 0; b.role := 1; a.rounds := rounds; b.rounds := rounds;
		a.err := 0; b.err := 0;
		CopyStack.SetSlice(100000);
		sw0 := CopyStack.Switches();
		t0 := System_devCRaw.c_now();
		CopyStack.Start(a); CopyStack.Start(b);
		go := TRUE;
		WHILE go & CopyStack.HasReady() DO
			IF CopyStack.Step() = 0 THEN go := FALSE END
		END;
		dt := System_devCRaw.c_now() - t0;
		sw1 := CopyStack.Switches();
		W("BENCH pair-copy min seg="); N(CopyStack.SegLen(a));
		W(" rounds="); N(rounds); W(" ns/switch="); N(dt DIV MAX(1, sw1 - sw0));
		W(" err="); N(a.err + b.err); Ln
	END BenchMin;

	PROCEDURE BenchTasks (rounds: INTEGER);
		VAR a, b: PingTask; t0, dt, sw0, sw1: LONGINT;
	BEGIN
		NEW(a); NEW(b); a.rounds := rounds; b.rounds := rounds;
		sw0 := Tasks.Switches();
		t0 := System_devCRaw.c_now();
		Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		dt := System_devCRaw.c_now() - t0;
		sw1 := Tasks.Switches();
		W("BENCH pair-tasks-yield rounds="); N(rounds); W(" ns/switch=");
		N(dt DIV MAX(1, sw1 - sw0)); Ln
	END BenchTasks;

	PROCEDURE Bench*;
	BEGIN
		W("CopyStack bench"); Ln;
		BenchMin(20000);
		BenchPair(0, 20000);
		BenchPair(5, 20000);
		BenchPair(26, 20000);
		BenchTasks(20000)
	END Bench;

	PROCEDURE BenchMem (n: INTEGER);
		VAR i: INTEGER; r0, r1, a0, a1, t0, dt: LONGINT;
	BEGIN
		Kernel.Collect; Kernel.Collect;
		t0 := System_devCRaw.c_now();
		Kernel.Collect;
		W("BENCH collect-base n="); N(n); W(" ms="); N((System_devCRaw.c_now() - t0) DIV 1000000); Ln;
		r0 := System_devCRaw.c_rss(); a0 := Kernel.Allocated();
		t0 := System_devCRaw.c_now();
		NEW(waiters, n);
		i := 0;
		WHILE i < n DO
			NEW(waiters[i]); waiters[i].id := i; waiters[i].done := 0;
			CopyStack.Start(waiters[i]);
			INC(i)
		END;
		CopyStack.SetSlice(0);
		WHILE CopyStack.HasReady() DO i := i + CopyStack.Step() END;
		dt := System_devCRaw.c_now() - t0;
		r1 := System_devCRaw.c_rss(); a1 := Kernel.Allocated();
		W("BENCH waiters n="); N(n); W(" rss/ea="); N((r1 - r0) DIV n);
		W(" heap/ea="); N((a1 - a0) DIV n); W(" ns/create="); N(dt DIV n);
		W(" seg="); N(CopyStack.SegLen(waiters[0])); Ln;
		t0 := System_devCRaw.c_now();
		Kernel.Collect;
		W("BENCH collect n="); N(n); W(" ms="); N((System_devCRaw.c_now() - t0) DIV 1000000); Ln;
		t0 := System_devCRaw.c_now();
		i := 0;
		WHILE i < n DO waiters[i].Wake; INC(i) END;
		WHILE CopyStack.HasReady() DO i := i + CopyStack.Step() END;
		dt := System_devCRaw.c_now() - t0;
		W("BENCH wake n="); N(n); W(" ns/ea="); N(dt DIV n); Ln;
		waiters := NIL; woken := NIL;
		Kernel.Collect; Kernel.Collect;
		CopyStack.Trim;
		Kernel.Collect; Kernel.Collect;
		W("BENCH heap-after n="); N(n); W(" heap="); N(Kernel.Allocated() - a0); Ln
	END BenchMem;

	PROCEDURE BenchMemory*;
	BEGIN
		BenchMem(10000);
		BenchMem(100000)
	END BenchMemory;

	PROCEDURE Bench1M*;
	(** Миллион ждущих: RSS, время создания, сборка, пробуждение. *)
	BEGIN
		BenchMem(1000000)
	END Bench1M;

	PROCEDURE Bench500k*;
	BEGIN
		BenchMem(500000)
	END Bench500k;

	PROCEDURE BenchTasksMemory (n, stackSize: INTEGER);
	(* Все задачи должны дойти до Sleep: иначе их страницы не тронуты и
	   RSS не показывает цену стека. Ждём n переключений (уход в Sleep). *)
		VAR i: INTEGER; r0, r1, n0, ms0, dt, sw0: LONGINT; s: Sleeper;
	BEGIN
		r0 := System_devCRaw.c_rss();
		n0 := System_devCRaw.c_now();
		i := 0;
		WHILE i < n DO
			NEW(s); Tasks.SetStackSize(s, stackSize); Tasks.Start(s);
			INC(i)
		END;
		sw0 := Tasks.Switches();
		ms0 := Kernel.Time();
		REPEAT Tasks.Step UNTIL (Tasks.Switches() - sw0 >= n) OR (Kernel.Time() - ms0 > 60000);
		dt := System_devCRaw.c_now() - n0;
		r1 := System_devCRaw.c_rss();
		W("BENCH tasks n="); N(n); W(" stack="); N(stackSize);
		W(" rss/ea="); N((r1 - r0) DIV n); W(" ns/create="); N(dt DIV n);
		W(" sw="); N(Tasks.Switches() - sw0); Ln
	END BenchTasksMemory;

	PROCEDURE BenchTasksMem*;
	(** Одна конфигурация на процесс: иначе спящие задачи прошлой волны
		утяжеляют сборку следующей. *)
	BEGIN
		BenchTasksMemory(10000, 0)
	END BenchTasksMem;

	PROCEDURE BenchTasks10k256*;
	BEGIN BenchTasksMemory(10000, 0) END BenchTasks10k256;

	PROCEDURE BenchTasks10k4k*;
	BEGIN BenchTasksMemory(10000, 4096) END BenchTasks10k4k;

	PROCEDURE BenchTasks100k256*;
	BEGIN BenchTasksMemory(100000, 0) END BenchTasks100k256;

	PROCEDURE BenchTasks100k4k*;
	BEGIN BenchTasksMemory(100000, 4096) END BenchTasks100k4k;

	(* --- мутации ---------------------------------------------------------- *)

	PROCEDURE MutRun*;
	(** Один объект ждёт, затем его будят. Правильный вариант печатает
		PASS, испорченный — трап или FAIL. *)
		VAR o: Deep;
	BEGIN
		NEW(o); o.depth := 5; o.err := 0;
		CopyStack.Start(o);
		Step1;
		IF CopyStack.State(o) # CopyStack.Waiting THEN W("FAIL no-wait"); Ln; HALT(30) END;
		o.Wake;
		Step1;
		IF (CopyStack.State(o) = CopyStack.Finished) & (o.err = 0) THEN W("PASS"); Ln
		ELSE W("FAIL"); Ln; HALT(31) END
	END MutRun;

	PROCEDURE MutGc*;
	(** Спящий объект с указателем только на стеке; другой объект затирает
		стек, затем сборка и мусор в куче. *)
		VAR o: GcObj; c: Churner; adr: S.ADDRESS;
	BEGIN
		NEW(o); o.err := 0; adr := S.ADR(o);
		CopyStack.Start(o); Step1;
		NEW(c); CopyStack.Start(c); Step1;
		Kernel.Collect; Kernel.Collect;
		HeapChurn(2000);
		Kernel.Collect;
		o.Wake; Step1;
		IF (CopyStack.State(o) = CopyStack.Finished) & (o.err = 0) THEN W("PASS"); Ln
		ELSE W("FAIL"); Ln; HALT(32) END
	END MutGc;

	PROCEDURE MutTrap*;
	(** Трап до Wait: объект обязан кончить Failed. *)
		VAR o: TrapObj;
	BEGIN
		NEW(o); o.when := 0; o.reached := 0;
		CopyStack.Start(o); Step1;
		IF CopyStack.State(o) = CopyStack.Failed THEN W("PASS"); Ln
		ELSE W("FAIL state="); N(CopyStack.State(o)); Ln; HALT(33) END
	END MutTrap;

	PROCEDURE Mut*;
	(** Все три контрольных прогона без мутаций: PASS и код 0. *)
	BEGIN
		MutRun; MutGc; MutTrap
	END Mut;

BEGIN
	churn := NIL; waiters := NIL; woken := NIL
END Copy_devStack.
