MODULE Unit_devProbe;
(** Пробы для самопроверки UnitRunner: проходящий, трапующий, падающий,
	пустой и медленный тесты, а также фикстуры. Модуль не входит в
	манифест: Unit_devMain гоняет его вложенным прогоном и сверяет итоги. *)

	IMPORT UnitAssert;

	VAR
		moduleSetups, setups, teardowns: INTEGER;
		flag: INTEGER;

	PROCEDURE SetupModule*;
	BEGIN
		INC(moduleSetups)
	END SetupModule;

	PROCEDURE TeardownModule*;
	BEGIN
		flag := 0
	END TeardownModule;

	PROCEDURE Setup*;
	BEGIN
		INC(setups); flag := 1
	END Setup;

	PROCEDURE Teardown*;
	BEGIN
		INC(teardowns); flag := 0
	END Teardown;

	PROCEDURE Crash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END Crash;

	PROCEDURE TestPass*;
	BEGIN
		UnitAssert.EqualInt(3, 1 + 2, "one plus two")
	END TestPass;

	PROCEDURE TestFixtures*;
	BEGIN
		UnitAssert.EqualInt(1, moduleSetups, "SetupModule ran once");
		UnitAssert.True(flag = 1, "Setup ran before the test");
		UnitAssert.True(setups >= 1, "Setup counter")
	END TestFixtures;

	PROCEDURE TestTraps*;
	BEGIN
		UnitAssert.Traps(Crash, "Crash must trap")
	END TestTraps;

	PROCEDURE TestSlowPass*;
	BEGIN
		UnitAssert.True(TRUE, "slow test runs only when requested")
	END TestSlowPass;

	PROCEDURE TestProcSkip*;
	BEGIN
		UnitAssert.True(TRUE, "proc test is skipped by default")
	END TestProcSkip;

	PROCEDURE TestFail*;
	BEGIN
		UnitAssert.Fail("expected failure for self-check")
	END TestFail;

	PROCEDURE TestUnexpectedTrap*;
	BEGIN
		Crash
	END TestUnexpectedTrap;

	PROCEDURE TestEmpty*;
	BEGIN
	END TestEmpty;

END Unit_devProbe.
