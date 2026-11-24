MODULE System_devCoTests;
(** Точки входа Unit для тестов сопрограмм (этап 2) на i386.

	На amd64 те же сценарии идут через System_devMain, но тот импортирует и
	System_devInHeap (Cluster*-API есть только в System/Mod64/Kernel).
	Здесь только GcStack и TrapStack — им нужен C-помощник libtest32.so;
	tools/test32.sh собирает его через tools/i386-toolchain.sh, а без
	32-битного тулчейна пропускает этот модуль целиком. *)

	IMPORT System_devGcStack, System_devTrapStack;

	PROCEDURE TestProcGcStack*;
	BEGIN
		System_devGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		System_devTrapStack.Do
	END TestProcTrapStack;

END System_devCoTests.
