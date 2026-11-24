MODULE System_testsClock;
(** Источник монотонных наносекунд для замеров System_tests (clock_gettime в
	libtest64.so). i386-вариант — System_tests/Mod/Clock.cp по Kernel.Time.
	Отдельный модуль, а не System_testsCo: задачи не тянут C-помощника
	сопрограмм ради одного таймера. *)

	IMPORT System_testsCRaw;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN System_testsCRaw.c_now()
	END now;

END System_testsClock.
