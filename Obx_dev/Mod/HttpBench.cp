MODULE Obx_devHttpBench;
(** Замер трёх клиентов на одном локальном сервере: N последовательных
	запросов, время по Kernel.Time, выделения по Kernel.AllocCount и
	живой прирост кучи по Kernel.Allocated после полной сборки.
	Модуль не входит в tools/tests.txt; его зовёт tools/test-http-examples.sh
	--measure. *)

	IMPORT Kernel, Strings, Dialog, Console,
		ObxHttpTask, ObxHttpCodec, ObxHttpCodecTask;

	CONST
		bodySize = 8192;

	VAR
		body: ARRAY bodySize OF BYTE;

	PROCEDURE IntPar (IN name: ARRAY OF CHAR; def: INTEGER): INTEGER;
		VAR s: ARRAY 32 OF CHAR; res, v: INTEGER;
	BEGIN
		Dialog.GetPar(name, s, res);
		IF (res = 0) & (s # "") THEN
			Strings.StringToInt(s, v, res);
			IF res = 0 THEN RETURN v END
		END;
		RETURN def
	END IntPar;

	PROCEDURE Run (which: INTEGER);
		VAR url: ARRAY 512 OF CHAR; res, n, i, status, bodyLen, err, errors: INTEGER;
			t0, t1, a0, a1, aMid, c0, c1: LONGINT; t: ARRAY 32 OF CHAR;
	BEGIN
		Dialog.GetPar("HTTP_URL", url, res);
		IF (res # 0) OR (url = "") THEN
			Console.WriteStr("Obx_devHttpBench: задайте HTTP_URL"); Console.WriteLn; RETURN
		END;
		n := IntPar("HTTP_N", 1000);
		(* прогрев: первые запросы заводят потоки, буферы и т.п. *)
		FOR i := 0 TO 9 DO
			CASE which OF
				0: ObxHttpTask.Get(url$, status, body, bodyLen, err)
			| 1: ObxHttpCodec.Get(url$, status, body, bodyLen, err)
			| 2: ObxHttpCodecTask.Get(url$, status, body, bodyLen, err)
			END
		END;
		Kernel.Collect; a0 := Kernel.Allocated(); c0 := Kernel.AllocCount();
		t0 := Kernel.Time(); errors := 0;
		FOR i := 0 TO n - 1 DO
			CASE which OF
				0: ObxHttpTask.Get(url$, status, body, bodyLen, err)
			| 1: ObxHttpCodec.Get(url$, status, body, bodyLen, err)
			| 2: ObxHttpCodecTask.Get(url$, status, body, bodyLen, err)
			END;
			IF err # ObxHttpTask.errNone THEN INC(errors) END
		END;
		t1 := Kernel.Time();
		aMid := Kernel.Allocated();
		Kernel.Collect; a1 := Kernel.Allocated(); c1 := Kernel.AllocCount();
		Console.WriteStr("bench style=");
		CASE which OF
			0: Console.WriteStr("task")
		| 1: Console.WriteStr("codec")
		| 2: Console.WriteStr("codecTask")
		END;
		Console.WriteStr(" n="); Strings.IntToString(n, t); Console.WriteStr(t);
		Console.WriteStr(" ms="); Strings.IntToString(t1 - t0, t); Console.WriteStr(t);
		Console.WriteStr(" errors="); Strings.IntToString(errors, t); Console.WriteStr(t);
		Console.WriteStr(" allocs="); Strings.IntToString(c1 - c0, t); Console.WriteStr(t);
		Console.WriteStr(" churn="); Strings.IntToString(aMid - a0, t); Console.WriteStr(t);
		Console.WriteStr(" live="); Strings.IntToString(a1 - a0, t); Console.WriteStr(t);
		Console.WriteLn
	END Run;

	PROCEDURE Task*;
	BEGIN Run(0)
	END Task;

	PROCEDURE Codec*;
	BEGIN Run(1)
	END Codec;

	PROCEDURE CodecTask*;
	BEGIN Run(2)
	END CodecTask;

END Obx_devHttpBench.
