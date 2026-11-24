MODULE System_devSupport;
(** Мини-каркас тестов System_dev: маркеры для tools/test.sh.
	Каждый тест печатает  SYSTEMTESTS-PASS <имя>  или  SYSTEMTESTS-FAIL <имя> ...
	Вывод идёт через Console (сразу в stdout, не через StdLog). *)

	IMPORT Console, Strings, UnitAssert;

	VAR failures: INTEGER;
		name: ARRAY 64 OF CHAR;

	(* Зритель трапов не трогаем: Kernel.Try ловит трап и без него,
	   отчёт о трапе ядро печатает само (SigRepHeader + LogThisStack). *)
	PROCEDURE Begin* (IN n: ARRAY OF CHAR);
	BEGIN
		name := n$; failures := 0;
		Console.WriteStr("SYSTEMTESTS begin "); Console.WriteStr(name); Console.WriteLn
	END Begin;

	PROCEDURE Fail* (IN what: ARRAY OF CHAR);
	BEGIN
		INC(failures);
		UnitAssert.Fail(what);	(* подсистема Unit: провал и счётчик проверок *)
		Console.WriteStr("  FAIL "); Console.WriteStr(what); Console.WriteLn
	END Fail;

	PROCEDURE Check* (cond: BOOLEAN; IN what: ARRAY OF CHAR);
	BEGIN
		IF cond THEN UnitAssert.Checked ELSE Fail(what) END
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
			Console.WriteStr("SYSTEMTESTS-PASS "); Console.WriteStr(name)
		ELSE
			Strings.IntToString(failures, s);
			Console.WriteStr("SYSTEMTESTS-FAIL "); Console.WriteStr(name);
			Console.WriteStr(" failures="); Console.WriteStr(s)
		END;
		Console.WriteLn
	END End;

END System_devSupport.
