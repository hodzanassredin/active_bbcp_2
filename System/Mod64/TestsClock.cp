MODULE SystemTestsClock;
(** Источник монотонных наносекунд для замеров SystemTests (clock_gettime в
	libtest64.so). i386-вариант — System/Mod/TestsClock.cp по Kernel.Time.
	Отдельный модуль, а не SystemTestsCo: задачи не тянут C-помощника
	сопрограмм ради одного таймера. *)

	IMPORT SystemTestsCRaw;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN SystemTestsCRaw.c_now()
	END now;

END SystemTestsClock.
