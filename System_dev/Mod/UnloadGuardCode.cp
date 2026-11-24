MODULE System_devUnloadGuardCode;
(** Модуль-мишень для System_devUnloadGuard (запрет выгрузки
	модуля с живым кадром).

	Тест загружает его через Meta, поэтому никто его не импортирует:
	refcnt = 0, и Kernel.UnloadMod доходит до проверки кадров на стеках,
	а не отказывает сразу по refcnt. Процедура UnloadSelf пробует
	выгрузить свой же модуль с главного стека, Run/Park оставляет кадр
	модуля на стеке спящей задачи. *)

	IMPORT Kernel, Tasks;

	TYPE ParkTask = POINTER TO RECORD (Tasks.Task) END;

	VAR task: ParkTask;

	PROCEDURE Self (): Kernel.Module;
	BEGIN
		RETURN Kernel.ThisLoadedMod("System_devUnloadGuardCode")
	END Self;

	PROCEDURE (t: ParkTask) Run;
	BEGIN
		Tasks.Park
	END Run;

	PROCEDURE UnloadSelf*;
	(** Кадр этого модуля сейчас на главном стеке; выгрузка должна быть отвергнута. *)
	BEGIN
		Kernel.UnloadMod(Self())
	END UnloadSelf;

	PROCEDURE StartParked*;
	(** Завести задачу; после Tasks.Step её Run останется в Tasks.Park. *)
	BEGIN
		NEW(task); Tasks.Start(task)
	END StartParked;

	PROCEDURE Release*;
	(** Разбудить задачу и довести её до конца (тест зовёт после проверки). *)
	BEGIN
		Tasks.Unpark(task); Tasks.Step
	END Release;

END System_devUnloadGuardCode.
