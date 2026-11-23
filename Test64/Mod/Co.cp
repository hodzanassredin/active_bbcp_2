MODULE Test64Co;
(** i386-вариант обёрток Test64Co (amd64-вариант — Test64/Mod64/Co.cp).

	32-битного помощника libtest64.so (ucontext) в стенде нет — нет
	32-битного C-тулчейна. Сопрограммы этапа 2 на i386 не проверяются;
	единственный нужный Test64Tasks источник времени — co_now, он
	строится по Kernel.Time (миллисекунды, переведённые в наносекунды).
	Остальные процедуры оставлены заглушками ради совместимости имён. *)

	IMPORT SYSTEM, Kernel;

	PROCEDURE co_create* (entry, arg: SYSTEM.ADDRESS; size: LONGINT): INTEGER;
	BEGIN
		RETURN -1
	END co_create;

	PROCEDURE co_create_on* (base: SYSTEM.ADDRESS; size: LONGINT;
		entry, arg: SYSTEM.ADDRESS): INTEGER;
	BEGIN
		RETURN -1
	END co_create_on;

	PROCEDURE co_resume* (): INTEGER;
	BEGIN
		RETURN -1
	END co_resume;

	PROCEDURE co_yield* (): INTEGER;
	BEGIN
		RETURN -1
	END co_yield;

	PROCEDURE co_stack_base* (): SYSTEM.ADDRESS;
	BEGIN
		RETURN 0
	END co_stack_base;

	PROCEDURE co_stack_size* (): LONGINT;
	BEGIN
		RETURN 0
	END co_stack_size;

	PROCEDURE co_release* (): INTEGER;
	BEGIN
		RETURN -1
	END co_release;

	PROCEDURE co_now* (): LONGINT;
	BEGIN
		RETURN Kernel.Time() * 1000000
	END co_now;

END Test64Co.
