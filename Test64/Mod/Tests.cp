MODULE Test64Tests;
(** Проверки стенда, переведённые на подсистему Unit.

	Каждый сценарий трогает ядро, сборщик, реестр стеков или трапы,
	поэтому все помечены TestProc…: tools/unit.sh гонит модуль отдельным
	процессом, а провал/Quit одного сценария не уносит остальные.
	Сами сценарии остались в Test64*, здесь только точки входа Unit. *)

	IMPORT Test64Basic, Test64Heap, Test64Trap, Test64ActionTrap,
		Test64GcStack, Test64TrapStack, Test64InHeap;

	PROCEDURE TestProcBasic*;
	BEGIN
		Test64Basic.Do
	END TestProcBasic;

	PROCEDURE TestProcHeap*;
	BEGIN
		Test64Heap.Do
	END TestProcHeap;

	PROCEDURE TestProcTrap*;
	BEGIN
		Test64Trap.Do
	END TestProcTrap;

	PROCEDURE TestProcActionTrap*;
	BEGIN
		Test64ActionTrap.Do
	END TestProcActionTrap;

	PROCEDURE TestProcGcStack*;
	BEGIN
		Test64GcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		Test64TrapStack.Do
	END TestProcTrapStack;

	PROCEDURE TestProcInHeap*;
	BEGIN
		Test64InHeap.Do
	END TestProcInHeap;

END Test64Tests.
