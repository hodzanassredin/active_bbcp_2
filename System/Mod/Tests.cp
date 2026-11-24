MODULE SystemTests;
(** Проверки стенда, переведённые на подсистему Unit.

	Каждый сценарий трогает ядро, сборщик, реестр стеков или трапы,
	поэтому все помечены TestProc…: tools/unit.sh гонит модуль отдельным
	процессом, а провал/Quit одного сценария не уносит остальные.
	Сами сценарии остались в SystemTests*, здесь только точки входа Unit. *)

	IMPORT SystemTestsBasic, SystemTestsHeap, SystemTestsTrap, SystemTestsActionTrap,
		SystemTestsGcStack, SystemTestsTrapStack, SystemTestsInHeap;

	PROCEDURE TestProcBasic*;
	BEGIN
		SystemTestsBasic.Do
	END TestProcBasic;

	PROCEDURE TestProcHeap*;
	BEGIN
		SystemTestsHeap.Do
	END TestProcHeap;

	PROCEDURE TestProcTrap*;
	BEGIN
		SystemTestsTrap.Do
	END TestProcTrap;

	PROCEDURE TestProcActionTrap*;
	BEGIN
		SystemTestsActionTrap.Do
	END TestProcActionTrap;

	PROCEDURE TestProcGcStack*;
	BEGIN
		SystemTestsGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		SystemTestsTrapStack.Do
	END TestProcTrapStack;

	PROCEDURE TestProcInHeap*;
	BEGIN
		SystemTestsInHeap.Do
	END TestProcInHeap;

END SystemTests.
