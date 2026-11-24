MODULE System_devClock;
(** Источник монотонных наносекунд для замеров System_dev (clock_gettime в
	libtest64.so). i386-вариант — System_dev/Mod/Clock.cp по Kernel.Time.
	Отдельный модуль, а не System_devCo: задачи не тянут C-помощника
	сопрограмм ради одного таймера. *)

	IMPORT System_devCRaw;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN System_devCRaw.c_now()
	END now;

END System_devClock.
