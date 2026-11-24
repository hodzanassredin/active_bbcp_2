MODULE System_testsCoTests;
(** Точки входа Unit для тестов сопрограмм (этап 2) на i386.

	На amd64 те же сценарии идут через System_testsMain, но тот импортирует и
	System_testsInHeap (Cluster*-API есть только в System/Mod64/Kernel).
	Здесь только GcStack и TrapStack — им нужен C-помощник libtest32.so;
	tools/test32.sh собирает его через tools/i386-toolchain.sh, а без
	32-битного тулчейна пропускает этот модуль целиком. *)

	IMPORT System_testsGcStack, System_testsTrapStack;

	PROCEDURE TestProcGcStack*;
	BEGIN
		System_testsGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		System_testsTrapStack.Do
	END TestProcTrapStack;

END System_testsCoTests.
