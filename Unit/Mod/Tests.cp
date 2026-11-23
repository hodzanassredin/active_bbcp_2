MODULE UnitTests;
(** Самопроверка подсистемы Unit. Запускается обычным прогоном:
		echo 'UnitRunner.Run UnitTests' | blackbox-console	(в tools/test.sh) *)

	IMPORT UnitAssert, UnitRunner;

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
		UnitRunner.RunModuleCounts("UnitTestsProbe", "slow", TRUE, c);
		UnitAssert.EqualInt(4, c.passed, "probe passed (Pass, Fixtures, Traps, SlowPass)");
		UnitAssert.EqualInt(2, c.failed, "probe failed (Fail, UnexpectedTrap)");
		UnitAssert.EqualInt(1, c.empty, "probe empty");
		UnitAssert.EqualInt(1, c.skipped, "probe skipped (TestProc)");
	END TestNestedRunner;

END UnitTests.
