MODULE Check_devMain;
(** Три пробы Ивана из подсистемы Check, переведённые на Unit:
	форматирование REAL и Math (обычные), псевдонимы ccall в компиляторе
	(TestProc: трогает состояние DevCPM).

	Сами проверки остались в CheckRealFormat64/CheckMath64/CheckCCallAlias;
	их питоновские сценарии (System/Test/check_math64.py и др.) не удалены. *)

	IMPORT CheckMath64, CheckRealFormat64, CheckCCallAlias, UnitAssert;

	PROCEDURE TestRealFormat*;
	(** Проверки Strings.RealToString/RealToStringForm и Math.Exponent;
		внутри — ASSERT, поэтому трап валит тест. *)
	BEGIN
		CheckRealFormat64.Run;
		UnitAssert.True(TRUE, "RealFormat checks ran")
	END TestRealFormat;

	PROCEDURE TestMath*;
	(** Полный набор CheckMath64: считает проверки и провалы сам. *)
	BEGIN
		CheckMath64.Run;
		UnitAssert.True(CheckMath64.count > 1000, "Math suite ran");
		UnitAssert.EqualInt(0, CheckMath64.failures, "Math failures")
	END TestMath;

	PROCEDURE TestProcCCallAlias*;
	BEGIN
		CheckCCallAlias.Do;
		UnitAssert.True(TRUE, "ccall alias checks ran")
	END TestProcCCallAlias;

END Check_devMain.
