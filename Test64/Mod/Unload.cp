MODULE Test64Unload;
(** Тесты выгрузки кода у исполнителей компутонов (этап 10).

	Test64UnloadCode — компутон y = x*x. Тест перекомпилирует его
	в версию y = x*x + 1000, зовёт AsyncComputon.Unload и проверяет,
	что следующий Push исполняется уже новым кодом. Отдельно
	проверяется отказ: занятый (исполняемый) модуль не выгружается. *)

	IMPORT Kernel, Tasks, Services, AsyncComputon, AsyncIO,
		DevCompiler64, TextModels, UnitAssert, Test64UnloadCode;

	CONST
		tagV1 = 301; tagV2 = 302;

	TYPE
		RunTask = POINTER TO RECORD (Tasks.Task) tag, y: INTEGER; ok: BOOLEAN END;

	VAR
		doneFlag: INTEGER;

	PROCEDURE PumpUntil (ms: LONGINT; cond: PROCEDURE (): BOOLEAN): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE ~cond() & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step;
			AsyncIO.Poll(1)
		END;
		RETURN cond()
	END PumpUntil;

	PROCEDURE DoneCond (): BOOLEAN;
	BEGIN RETURN doneFlag = 1 END DoneCond;

	PROCEDURE (t: RunTask) Run;
		VAR r: AsyncComputon.Computon;
	BEGIN
		AsyncComputon.Push(Test64UnloadCode.New(3), 0, t.tag);
		r := AsyncComputon.PullTimeout(0, t.tag, 20000);
		t.y := -1; t.ok := r # NIL;
		IF r # NIL THEN t.y := Test64UnloadCode.Y(r) END;
		doneFlag := 1
	END Run;

	(* --- перекомпиляция модуля -------------------------------------------- *)

	PROCEDURE Put (w: TextModels.Writer; IN s: ARRAY OF CHAR);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE s[i] # 0X DO w.WriteChar(s[i]); INC(i) END;
		w.WriteChar(0DX)
	END Put;

	PROCEDURE WriteSource (w: TextModels.Writer; v2: BOOLEAN);
	BEGIN
		Put(w, "MODULE Test64UnloadCode;");
		Put(w, "");
		Put(w, "  IMPORT AsyncComputon, Stores;");
		Put(w, "");
		Put(w, "  TYPE");
		Put(w, "    Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;");
		Put(w, "");
		Put(w, "  PROCEDURE New* (x: INTEGER): AsyncComputon.Computon;");
		Put(w, "    VAR c: Square;");
		Put(w, "  BEGIN");
		Put(w, "    NEW(c); c.x := x; c.y := -1; RETURN c");
		Put(w, "  END New;");
		Put(w, "");
		Put(w, "  PROCEDURE Y* (c: AsyncComputon.Computon): INTEGER;");
		Put(w, "    VAR y: INTEGER;");
		Put(w, "  BEGIN");
		Put(w, "    y := -1;");
		Put(w, "    WITH c: Square DO y := c.y END;");
		Put(w, "    RETURN y");
		Put(w, "  END Y;");
		Put(w, "");
		Put(w, "  PROCEDURE (c: Square) Do;");
		Put(w, "  BEGIN");
		IF v2 THEN Put(w, "    c.y := c.x * c.x + 1000")
		ELSE Put(w, "    c.y := c.x * c.x")
		END;
		Put(w, "  END Do;");
		Put(w, "");
		Put(w, "  PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);");
		Put(w, "  BEGIN");
		Put(w, "    wr.WriteVersion(0);");
		Put(w, "    wr.WriteInt(c.x); wr.WriteInt(c.y)");
		Put(w, "  END Externalize;");
		Put(w, "");
		Put(w, "  PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);");
		Put(w, "    VAR version: INTEGER;");
		Put(w, "  BEGIN");
		Put(w, "    rd.ReadVersion(0, 0, version);");
		Put(w, "    rd.ReadInt(c.x); rd.ReadInt(c.y)");
		Put(w, "  END Internalize;");
		Put(w, "");
		Put(w, "END Test64UnloadCode.")
	END WriteSource;

	PROCEDURE Recompile (v2: BOOLEAN; OUT ok: BOOLEAN);
		VAR m: TextModels.Model; w: TextModels.Writer; error: BOOLEAN;
	BEGIN
		m := TextModels.dir.New();
		w := m.NewWriter(NIL);
		WriteSource(w, v2);
		DevCompiler64.CompileText(m, 0, error);
		ok := ~error
	END Recompile;

	(* --- тест ------------------------------------------------------------- *)

	PROCEDURE RunV (tag: INTEGER; OUT t: RunTask; OUT reached: BOOLEAN);
	BEGIN
		doneFlag := 0; NEW(t); t.tag := tag; Tasks.Start(t);
		reached := PumpUntil(20000, DoneCond)
	END RunV;

	PROCEDURE TestUnloadCode*;
		VAR t: RunTask; ok, reached, busy: BOOLEAN;
	BEGIN
		IF ~AsyncComputon.SetTransport("pipe") THEN ASSERT(FALSE, 20) END;
		AsyncComputon.Start(2);
		UnitAssert.EqualInt(2, AsyncComputon.Size(), "two executors");

		(* версия 1 *)
		RunV(tagV1, t, reached);
		UnitAssert.True(reached, "version 1 computed");
		UnitAssert.True(t.ok, "version 1 result returned");
		UnitAssert.EqualInt(9, t.y, "version 1 code: 3 * 3");

		(* новый код и выгрузка старого у всех исполнителей *)
		Recompile(TRUE, ok);
		UnitAssert.True(ok, "version 2 recompiled");

		busy := AsyncComputon.Unload("AsyncComputon");
		UnitAssert.True(~busy, "own transport module is refused");
		UnitAssert.EqualInt(20, AsyncComputon.LastError(), "refusal code 20");

		busy := AsyncComputon.Unload("Kernel");
		UnitAssert.True(~busy, "module imported by the executor is refused");

		UnitAssert.True(AsyncComputon.Unload("Test64UnloadCode"), "code unloaded everywhere");
		UnitAssert.EqualInt(0, AsyncComputon.LastError(), "unload reports success");

		(* версия 2: исполнитель загрузил новый .ocf *)
		RunV(tagV2, t, reached);
		UnitAssert.True(reached, "version 2 computed");
		UnitAssert.True(t.ok, "version 2 result returned");
		UnitAssert.EqualInt(1009, t.y, "reloaded code: 3 * 3 + 1000");

		(* вернуть исходный .ocf для следующих прогонов *)
		Recompile(FALSE, ok);
		UnitAssert.True(ok, "version 1 restored");
		AsyncComputon.Stop
	END TestUnloadCode;

END Test64Unload.
