MODULE Test64Support;
(** Мини-каркас тестов Test64: маркеры для tools/test.sh.
	Каждый тест печатает  TEST64-PASS <имя>  или  TEST64-FAIL <имя> ...
	Вывод идёт через Console (сразу в stdout, не через StdLog). *)

	IMPORT Console, Strings, Kernel;

	VAR failures: INTEGER;
		name: ARRAY 64 OF CHAR;

	(* Консольный x64 BlackBox не устанавливает trapViewer ни в LinIntInit,
	   ни в LinGui: при trapViewer = NIL System.Kernel.HandleTrap считает
	   ЛЮБОЙ трап фатальным, даже внутри Kernel.Try (FatalError + Terminate(1)).
	   Пустой viewer возвращает обычную семантику Try; отчёт о трапе ядро
	   уже печатает само (DefaultTrapViewer + LogThisStack). *)
	PROCEDURE NoopViewer;
	BEGIN
	END NoopViewer;

	PROCEDURE Begin* (IN n: ARRAY OF CHAR);
	BEGIN
		Kernel.InstallTrapViewer(NoopViewer);
		name := n$; failures := 0;
		Console.WriteStr("TEST64 begin "); Console.WriteStr(name); Console.WriteLn
	END Begin;

	PROCEDURE Fail* (IN what: ARRAY OF CHAR);
	BEGIN
		INC(failures);
		Console.WriteStr("  FAIL "); Console.WriteStr(what); Console.WriteLn
	END Fail;

	PROCEDURE Check* (cond: BOOLEAN; IN what: ARRAY OF CHAR);
	BEGIN
		IF ~cond THEN Fail(what) END
	END Check;

	PROCEDURE Int* (IN what: ARRAY OF CHAR; x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Int;

	PROCEDURE Hex* (IN what: ARRAY OF CHAR; x: LONGINT);
		VAR i, d: INTEGER; buf: ARRAY 20 OF CHAR;
	BEGIN
		FOR i := 15 TO 0 BY -1 DO
			d := SHORT(x MOD 16); x := x DIV 16;
			IF d < 10 THEN buf[i] := CHR(ORD("0") + d)
			ELSE buf[i] := CHR(ORD("A") + d - 10) END
		END;
		buf[16] := 0X;
		Console.WriteStr(what); Console.WriteStr(buf); Console.WriteLn
	END Hex;

	PROCEDURE End*;
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		IF failures = 0 THEN
			Console.WriteStr("TEST64-PASS "); Console.WriteStr(name)
		ELSE
			Strings.IntToString(failures, s);
			Console.WriteStr("TEST64-FAIL "); Console.WriteStr(name);
			Console.WriteStr(" failures="); Console.WriteStr(s)
		END;
		Console.WriteLn
	END End;

END Test64Support.
