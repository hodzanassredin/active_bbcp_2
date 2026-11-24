MODULE System_devCo;
(** Обёртки над C-помощником System_devCRaw: переключение стеков в одной нити
	через ucontext. Библиотека libtest64.so собирается tools/test.sh.

	Этап 2: обёртки регистрируют стек в реестре ядра (Kernel.RegisterStack)
	и зовут Kernel.SwitchSave/SwitchRestore вокруг «сырого» переключения —
	так контекст Kernel.Try, tryCount и trapStack остаются у своего стека. *)

	IMPORT SYSTEM, Kernel, System_devCRaw;

	VAR reg: Kernel.Stack;	(* результат RegisterStack (вызов функции как оператора запрещён) *)

	(** Создать контекст на стеке из mmap (со сторожевой страницей). *)
	PROCEDURE co_create* (entry, arg: SYSTEM.ADDRESS; size: LONGINT): INTEGER;
		VAR res: INTEGER; lo: SYSTEM.ADDRESS;
	BEGIN
		res := System_devCRaw.c_create(entry, arg, size);
		IF res = 0 THEN
			lo := System_devCRaw.c_stack_low();
			reg := Kernel.RegisterStack(lo, lo + System_devCRaw.c_stack_size())
		END;
		RETURN res
	END co_create;

	(** Создать контекст на готовом стеке (например, NEW-массиве в куче). *)
	PROCEDURE co_create_on* (base: SYSTEM.ADDRESS; size: LONGINT;
		entry, arg: SYSTEM.ADDRESS): INTEGER;
		VAR res: INTEGER;
	BEGIN
		res := System_devCRaw.c_create_on(base, size, entry, arg);
		IF res = 0 THEN reg := Kernel.RegisterStack(base, base + size) END;
		RETURN res
	END co_create_on;

	PROCEDURE co_create_raw* (entry, arg: SYSTEM.ADDRESS; size: LONGINT): INTEGER;
	(** Стек вне реестра ядра: проверка безопасной сборки мусора на
		незнакомом стеке (MarkLocals не трапает и не сканирует его). *)
	BEGIN
		RETURN System_devCRaw.c_create(entry, arg, size)
	END co_create_raw;

	(** Войти/продолжить сопрограмму (сохранить текущий контекст). *)
	PROCEDURE co_resume* (): INTEGER;
		VAR res: INTEGER;
	BEGIN
		Kernel.SwitchSave;
		res := System_devCRaw.c_resume();
		Kernel.SwitchRestore;
		RETURN res
	END co_resume;

	(** Вернуться из сопрограммы на главный стек. *)
	PROCEDURE co_yield* (): INTEGER;
		VAR res: INTEGER;
	BEGIN
		Kernel.SwitchSave;
		res := System_devCRaw.c_yield();
		Kernel.SwitchRestore;
		RETURN res
	END co_yield;

	PROCEDURE co_stack_base* (): SYSTEM.ADDRESS;
	BEGIN
		RETURN System_devCRaw.c_stack_base()
	END co_stack_base;

	PROCEDURE co_stack_size* (): LONGINT;
	BEGIN
		RETURN System_devCRaw.c_stack_size()
	END co_stack_size;

	PROCEDURE co_release* (): INTEGER;
		VAR res: INTEGER;
	BEGIN
		(* Запись реестра хранит сам владелец: ядру не нужен поиск по адресу. *)
		IF reg # NIL THEN Kernel.UnregisterStack(reg); reg := NIL END;
		res := System_devCRaw.c_release();
		RETURN res
	END co_release;

	PROCEDURE co_now* (): LONGINT;
	BEGIN
		RETURN System_devCRaw.c_now()
	END co_now;

END System_devCo.
