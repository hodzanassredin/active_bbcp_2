MODULE Unit_devBadSetup;
(** Проба для самопроверки UnitRunner: SetupModule трапует, тесты модуля
	не исполняются, а помечаются пропущенными. Модуль не входит в манифест:
	его гоняет Unit_devMain вложенным прогоном. *)

	IMPORT UnitAssert;

	VAR ran*: INTEGER;	(* сколько тестов исполнилось *)

	PROCEDURE SetupModule*;
	BEGIN
		HALT(1)
	END SetupModule;

	PROCEDURE TestA*;
	BEGIN
		INC(ran); UnitAssert.True(TRUE, "a")
	END TestA;

	PROCEDURE TestB*;
	BEGIN
		INC(ran); UnitAssert.True(TRUE, "b")
	END TestB;

END Unit_devBadSetup.
