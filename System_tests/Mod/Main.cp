MODULE System_testsMain;
(** Проверки стенда, переведённые на подсистему Unit.

	Каждый сценарий трогает ядро, сборщик, реестр стеков или трапы,
	поэтому все помечены TestProc…: tools/unit.sh гонит модуль отдельным
	процессом, а провал/Quit одного сценария не уносит остальные.
	Сами сценарии остались в System_tests*, здесь только точки входа Unit. *)

	IMPORT System_testsBasic, System_testsHeap, System_testsTrap, System_testsActionTrap,
		System_testsGcStack, System_testsTrapStack, System_testsInHeap;

	PROCEDURE TestProcBasic*;
	BEGIN
		System_testsBasic.Do
	END TestProcBasic;

	PROCEDURE TestProcHeap*;
	BEGIN
		System_testsHeap.Do
	END TestProcHeap;

	PROCEDURE TestProcTrap*;
	BEGIN
		System_testsTrap.Do
	END TestProcTrap;

	PROCEDURE TestProcActionTrap*;
	BEGIN
		System_testsActionTrap.Do
	END TestProcActionTrap;

	PROCEDURE TestProcGcStack*;
	BEGIN
		System_testsGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		System_testsTrapStack.Do
	END TestProcTrapStack;

	PROCEDURE TestProcInHeap*;
	BEGIN
		System_testsInHeap.Do
	END TestProcInHeap;

END System_testsMain.
