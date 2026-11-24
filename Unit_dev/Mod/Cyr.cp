MODULE Unit_devCyr;
(** Проба для самопроверки JUnit: провал с кириллицей в сообщении. Модуль не
	входит в манифест: его гоняет Unit_devMain вложенным прогоном. *)

	IMPORT UnitAssert;

	PROCEDURE TestFailRu*;
	BEGIN
		UnitAssert.Fail("провал")
	END TestFailRu;

END Unit_devCyr.
