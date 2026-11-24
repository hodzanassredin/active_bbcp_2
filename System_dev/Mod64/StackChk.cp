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
		Calm = POINTER TO RECORD (Tasks.Task) n: INTEGER; lo, hi: S.ADDRESS END;
		Trapper = POINTER TO RECORD (Tasks.Task) kind: INTEGER END;

	VAR
		depth: INTEGER;
		lo: S.ADDRESS;	(* низ стека глубокой задачи *)
		fillLo, fillHi: S.ADDRESS;	(* залитая шаблоном часть стека трапующей задачи *)
		zeroDiv: INTEGER;

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
		VAR i: INTEGER; st: Kernel.Stack;
	BEGIN
		st := Kernel.StackOf(S.ADR(i)); t.lo := st.lo; t.hi := st.hi;
		FOR i := 1 TO 10 DO INC(t.n); Tasks.Yield END
	END Run;

	PROCEDURE (t: Trapper) Run;
	(* Заливает шаблоном стек ниже своего кадра (с запасом 1 КБ под вызовы до
	   трапа) и трапует: kind 0 — HALT (SIGILL), 1 — деление на ноль (SIGFPE),
	   2 — ASSERT (SIGILL). Обработка трапа не должна трогать залитое. *)
		CONST pat = 5A5A5A5AH;
		VAR x, a: S.ADDRESS; v: INTEGER;
	BEGIN
		fillHi := S.ADR(x) - 1024; fillLo := Kernel.stkLo + 4096;
		a := fillLo; WHILE a < fillHi DO S.PUT(a, pat); INC(a, 4) END;
		IF t.kind = 0 THEN HALT(42)
		ELSIF t.kind = 1 THEN v := 7 DIV zeroDiv; x := v
		ELSE ASSERT(zeroDiv # 0, 43)
		END
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

	PROCEDURE TestSlabNeighboursSurvive*;
	(* Стеки одного размера нарезаются из одной плиты подряд (Tasks.TakeStack).
	   Переполнение задачи в середине плиты не задевает соседей. *)
		CONST n = 6;
		VAR d: Deep; c: ARRAY 2 * n OF Calm; i, j: INTEGER;
	BEGIN
		depth := 0;
		FOR i := 0 TO n - 1 DO NEW(c[i]); Tasks.SetStackSize(c[i], small); Tasks.Start(c[i]) END;
		NEW(d); Tasks.SetStackSize(d, small); Tasks.Start(d);
		FOR i := n TO 2 * n - 1 DO NEW(c[i]); Tasks.SetStackSize(c[i], small); Tasks.Start(c[i]) END;
		Pump;
		UnitAssert.True(Tasks.Failed(d), "deep task failed");
		UnitAssert.EqualInt(137, Kernel.err, "trap 137 stack overflow");
		FOR i := 0 TO 2 * n - 1 DO
			UnitAssert.EqualInt(10, c[i].n, "neighbour finished");
			UnitAssert.True(~Tasks.Failed(c[i]), "neighbour not failed");
			UnitAssert.True(c[i].hi - c[i].lo >= small, "stack not smaller than asked");
			FOR j := 0 TO i - 1 DO
				UnitAssert.True((c[j].hi <= c[i].lo) OR (c[i].hi <= c[j].lo), "stacks do not overlap")
			END
		END
	END TestSlabNeighboursSurvive;

	PROCEDURE TestDefaultStackSize*;
		VAR c: Calm; old: INTEGER;
	BEGIN
		old := Tasks.DefaultStackSize();
		Tasks.SetDefaultStackSize(32 * 1024);
		NEW(c); Tasks.Start(c);
		Tasks.SetDefaultStackSize(old);
		Pump;
		UnitAssert.EqualInt(10, c.n, "task finished");
		UnitAssert.EqualInt(32 * 1024, c.hi - c.lo, "default size applied")
	END TestDefaultStackSize;

	PROCEDURE TestProcTrapOffTaskStack*;
	(* Трап задачи (SIGILL, SIGFPE) обрабатывается на альтернативном стеке:
	   стек задачи ниже точки трапа не тронут. *)
		CONST pat = 5A5A5A5AH;
		VAR t: Trapper; k: INTEGER; a: S.ADDRESS; x: INTEGER; bad: INTEGER;
	BEGIN
		zeroDiv := 0;
		FOR k := 0 TO 2 DO
			NEW(t); t.kind := k; Tasks.SetStackSize(t, 64 * 1024); Tasks.Start(t);
			Pump;
			UnitAssert.True(Tasks.Failed(t), "task trapped");
			UnitAssert.True(fillHi - fillLo > 32 * 1024, "pattern area filled");
			bad := 0; a := fillLo;
			WHILE a < fillHi DO S.GET(a, x); IF x # pat THEN INC(bad) END; INC(a, 4) END;
			UnitAssert.EqualInt(0, bad, "trap handling left task stack below trap point intact")
		END
	END TestProcTrapOffTaskStack;

END System_devStackChk.
