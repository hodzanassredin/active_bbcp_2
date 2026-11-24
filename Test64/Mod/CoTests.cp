MODULE Test64CoTests;
(** Точки входа Unit для тестов сопрограмм (этап 2) на i386.

	На amd64 те же сценарии идут через Test64Tests, но тот импортирует и
	Test64InHeap (Cluster*-API есть только в System/Mod64/Kernel).
	Здесь только GcStack и TrapStack — им нужен C-помощник libtest32.so;
	tools/test32.sh собирает его через tools/i386-toolchain.sh, а без
	32-битного тулчейна пропускает этот модуль целиком. *)

	IMPORT Test64GcStack, Test64TrapStack;

	PROCEDURE TestProcGcStack*;
	BEGIN
		Test64GcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		Test64TrapStack.Do
	END TestProcTrapStack;

END Test64CoTests.
