MODULE Unit_devMain;
(** Самопроверка подсистемы Unit. Запускается обычным прогоном:
		echo 'UnitRunner.Run Unit_devMain' | blackbox-console	(в tools/test.sh) *)

	IMPORT Files, TextModels, DevCommanders, UnitAssert, UnitRunner, Unit_devBadSetup;

	CONST
		junitName = "unit-junit-cyr.xml";

	TYPE
		Obj = POINTER TO RECORD x: INTEGER END;

	PROCEDURE Crash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END Crash;

	PROCEDURE TestAssertEqual*;
		VAR o: Obj;
	BEGIN
		NEW(o);
		UnitAssert.EqualInt(42, 40 + 2, "integers");
		UnitAssert.EqualReal(0.5, 1.0 / 2.0, 1.0E-12, "reals");
		UnitAssert.EqualStr("abc", "ab" + "c", "strings");
		UnitAssert.EqualSet({1, 3}, {1, 3}, "sets");
		UnitAssert.NotNil(o, "allocated object is not NIL")
	END TestAssertEqual;

	PROCEDURE TestAssertTraps*;
	BEGIN
		UnitAssert.Traps(Crash, "NIL dereference traps")
	END TestAssertTraps;

	PROCEDURE TestTempDir*;
		VAR p: ARRAY 64 OF CHAR;
	BEGIN
		UnitRunner.TempDir(p);
		UnitAssert.EqualStr("UnitTmp", p, "temporary directory path")
	END TestTempDir;

	PROCEDURE TestNestedRunner*;
	(** Вложенный прогон проб: проверяем классификацию PASS/FAIL/EMPTY/SKIP. *)
		VAR c: UnitRunner.Counts;
	BEGIN
		UnitRunner.RunModuleCounts("Unit_devProbe", "slow", TRUE, c);
		UnitAssert.EqualInt(4, c.passed, "probe passed (Pass, Fixtures, Traps, SlowPass)");
		UnitAssert.EqualInt(2, c.failed, "probe failed (Fail, UnexpectedTrap)");
		UnitAssert.EqualInt(1, c.empty, "probe empty");
		UnitAssert.EqualInt(1, c.skipped, "probe skipped (TestProc)");
	END TestNestedRunner;

	PROCEDURE TestSetupModuleTrapSkips*;
	(** SetupModule трапнул: тесты модуля не идут и помечены пропущенными. *)
		VAR c: UnitRunner.Counts;
	BEGIN
		Unit_devBadSetup.ran := 0;
		UnitRunner.RunModuleCounts("Unit_devBadSetup", "", TRUE, c);
		UnitAssert.EqualInt(0, Unit_devBadSetup.ran, "tests did not run");
		UnitAssert.EqualInt(2, c.skipped, "tests skipped");
		UnitAssert.EqualInt(1, c.failed, "SetupModule failure counted")
	END TestSetupModuleTrapSkips;

	PROCEDURE TestLongStringsDoNotTrap*;
	(** Сообщение провала обрезается, а не трапует на длинных строках. *)
		VAR a, b: ARRAY 201 OF CHAR; i: INTEGER; s: UnitAssert.State; m: UnitAssert.Message;
	BEGIN
		FOR i := 0 TO 199 DO a[i] := "a"; b[i] := "b" END;
		a[200] := 0X; b[200] := 0X;
		UnitAssert.Save(s);
		UnitAssert.EqualStr(a, b, "two long strings");
		UnitAssert.LastMessage(m);
		UnitAssert.Restore(s);
		UnitAssert.True(m # "", "failure message kept")
	END TestLongStringsDoNotTrap;

	PROCEDURE TestJUnitUtf8*;
	(** JUnit пишет знаки в UTF-8: «п» — байты D0 BF. Вложенный прогон
		пробы с провалом; итоги внешнего прогона JUnit здесь не нужен. *)
		VAR p: DevCommanders.Par; w: TextModels.Writer; i: INTEGER; cmd: ARRAY 80 OF CHAR;
			f: Files.File; r: Files.Reader; b, prev: BYTE; found: BOOLEAN; loc: Files.Locator;
	BEGIN
		NEW(p); p.text := TextModels.dir.New(); w := p.text.NewWriter(NIL);
		cmd := "Unit_devCyr junit=" + junitName + " noquit";
		i := 0; WHILE cmd[i] # 0X DO w.WriteChar(cmd[i]); INC(i) END;
		p.beg := 0; p.end := p.text.Length();
		DevCommanders.par := p;
		UnitRunner.Run;
		loc := Files.dir.This("");
		f := Files.dir.Old(loc, junitName, Files.shared);
		UnitAssert.True(f # NIL, "JUnit file written");
		found := FALSE;
		IF f # NIL THEN
			r := f.NewReader(NIL); prev := 0;
			WHILE ~r.eof DO
				r.ReadByte(b);
				IF ~r.eof & (prev = -48) & (b = -65) THEN found := TRUE END;	(* D0 BF *)
				prev := b
			END;
			f.Close;
			Files.dir.Delete(loc, junitName)
		END;
		UnitAssert.True(found, "Cyrillic encoded as UTF-8")
	END TestJUnitUtf8;

END Unit_devMain.
