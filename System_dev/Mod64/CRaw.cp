MODULE System_devCRaw ["libtest64.so"];
(** Только внешние объявления tools/test64.c (amd64; i386-вариант —
	System_dev/Mod/CRaw.cp с libtest32.so). В Component Pascal модуль,
	который содержит объявления без тела ([ccall] с именем символа),
	нельзя смешивать с процедурами, у которых есть тело. Поэтому
	обёртки с реестром стеков живут в System_devCo, а здесь — «сырой» C.

	Библиотеку libtest64.so собирают tools/unit.sh и tools/bench-gc.sh. *)

	IMPORT SYSTEM;

	PROCEDURE [ccall] c_create* ["t64_co_create"] (entry, arg: SYSTEM.ADDRESS; size: LONGINT): INTEGER;
	PROCEDURE [ccall] c_create_on* ["t64_co_create_on"] (base: SYSTEM.ADDRESS; size: LONGINT;
		entry, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE [ccall] c_resume* ["t64_co_resume"] (): INTEGER;
	PROCEDURE [ccall] c_yield* ["t64_co_yield"] (): INTEGER;
	PROCEDURE [ccall] c_stack_base* ["t64_co_stack_base"] (): SYSTEM.ADDRESS;
	PROCEDURE [ccall] c_stack_low* ["t64_co_stack_low"] (): SYSTEM.ADDRESS;
	PROCEDURE [ccall] c_stack_high* ["t64_co_stack_high"] (): SYSTEM.ADDRESS;
	PROCEDURE [ccall] c_stack_size* ["t64_co_stack_size"] (): LONGINT;
	PROCEDURE [ccall] c_release* ["t64_co_release"] (): INTEGER;
	PROCEDURE [ccall] c_now* ["test64_ns"] (): LONGINT;

END System_devCRaw.
