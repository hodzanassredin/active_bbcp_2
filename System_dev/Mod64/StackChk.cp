MODULE System_devStackChk;
(** Проверка границы стека задачи в прологе (ключ компилятора "%",
	amd64). Модуль компилируется с этим ключом (tools/tests.txt, столбец
	опций): глубокая рекурсия в задаче с малым стеком даёт трап 137
	«stack overflow» до сторожевой страницы, задача завершается с
	ошибкой, соседняя задача доживает до конца. *)

	IMPORT S := SYSTEM, Kernel, Tasks, UnitAssert;

	CONST
		small = 16 * 1024;

	TYPE
		Deep = POINTER TO RECORD (Tasks.Task) END;
		Calm = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

	VAR
		depth: INTEGER;
		lo: S.ADDRESS;	(* низ стека глубокой задачи *)

	PROCEDURE Rec (n: INTEGER): INTEGER;
		VAR a: ARRAY 64 OF INTEGER;
	BEGIN
		INC(depth); a[n MOD 64] := n;
		IF n = 0 THEN RETURN 0 END;
		RETURN Rec(n - 1) + a[n MOD 64]
	END Rec;

	PROCEDURE (t: Deep) Run;
		VAR x: INTEGER;
	BEGIN
		lo := Kernel.StackOf(S.ADR(x)).lo;
		x := Rec(100000)
	END Run;

	PROCEDURE (t: Calm) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO INC(t.n); Tasks.Yield END
	END Run;

	PROCEDURE Pump;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 < 5000) DO Tasks.Step END
	END Pump;

	PROCEDURE TestProcOverflowTraps137*;
		VAR d: Deep; c: Calm;
	BEGIN
		depth := 0;
		NEW(d); Tasks.SetStackSize(d, small); Tasks.Start(d);
		NEW(c); Tasks.Start(c);
		Pump;
		UnitAssert.True(Tasks.Failed(d), "deep task failed");
		UnitAssert.EqualInt(137, Kernel.err, "trap 137 stack overflow");
		(* Проверка в прологе ловит SP ещё внутри стека; сторожевая страница —
		   только когда запись ушла ниже lo. *)
		UnitAssert.True(Kernel.sp >= lo, "trap raised before SP left the stack");
		UnitAssert.EqualInt(10, c.n, "the other task finished");
		UnitAssert.True(depth > 10, "recursion ran")
	END TestProcOverflowTraps137;

END System_devStackChk.
