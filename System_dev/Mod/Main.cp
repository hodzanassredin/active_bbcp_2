MODULE System_devMain;
(** Проверки стенда, переведённые на подсистему Unit.

	Каждый сценарий трогает ядро, сборщик, реестр стеков или трапы,
	поэтому все помечены TestProc…: tools/unit.sh гонит модуль отдельным
	процессом, а провал/Quit одного сценария не уносит остальные.
	Сами сценарии остались в System_dev*, здесь только точки входа Unit. *)

	IMPORT System_devBasic, System_devHeap, System_devTrap, System_devActionTrap,
		System_devGcStack, System_devTrapStack, System_devInHeap;

	PROCEDURE TestProcBasic*;
	BEGIN
		System_devBasic.Do
	END TestProcBasic;

	PROCEDURE TestProcHeap*;
	BEGIN
		System_devHeap.Do
	END TestProcHeap;

	PROCEDURE TestProcTrap*;
	BEGIN
		System_devTrap.Do
	END TestProcTrap;

	PROCEDURE TestProcActionTrap*;
	BEGIN
		System_devActionTrap.Do
	END TestProcActionTrap;

	PROCEDURE TestProcGcStack*;
	BEGIN
		System_devGcStack.Do
	END TestProcGcStack;

	PROCEDURE TestProcTrapStack*;
	BEGIN
		System_devTrapStack.Do
	END TestProcTrapStack;

	PROCEDURE TestProcInHeap*;
	BEGIN
		System_devInHeap.Do
	END TestProcInHeap;

	PROCEDURE TestProcTryInConsole*;
	(** Трап внутри Kernel.Try в консоли без trap viewer'а: процесс жив. *)
	BEGIN
		System_devTrap.TryInConsole
	END TestProcTryInConsole;

END System_devMain.
