MODULE Test64Clock;
(** Источник монотонных наносекунд для замеров Test64 (clock_gettime в
	libtest64.so). i386-вариант — Test64/Mod/Clock.cp по Kernel.Time.
	Отдельный модуль, а не Test64Co: задачи не тянут C-помощника
	сопрограмм ради одного таймера. *)

	IMPORT Test64CRaw;

	PROCEDURE now* (): LONGINT;
	BEGIN
		RETURN Test64CRaw.c_now()
	END now;

END Test64Clock.
