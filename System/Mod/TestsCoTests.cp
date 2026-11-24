MODULE SystemTestsCoTests;
(** Точки входа Unit для тестов сопрограмм (этап 2) на i386.

	На amd64 те же сценарии идут через SystemTests, но тот импортирует и
	SystemTestsInHeap (Cluster*-API есть только в System/Mod64/Kernel).
	Здесь только GcStack и TrapStack — им нужен C-помощник libtest32.so;
	tools/test32.sh собирает его через tools/i386-toolchain.sh, а без
	32-битного тулчейна пропускает этот модуль целиком. *)

	IMPORT SystemTestsGcStack, SystemTestsTrapStack;

	PROCEDURE TestProcGcStack*;
	BEGIN
		SystemTestsGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		SystemTestsTrapStack.Do
	END TestProcTrapStack;

END SystemTestsCoTests.
