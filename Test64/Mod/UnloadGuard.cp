MODULE Test64UnloadGuard;
(** Красный тест этапа 2: Kernel.UnloadMod не выгружает модуль, пока его
	код есть в кадрах стека.

	Модуль-мишень Test64UnloadGuardCode ни кем не импортируется (тест
	зовёт его через Meta), поэтому refcnt = 0 и решение принимает
	StackUsesModule/FrameInModule. Проверяются два случая: кадр мишени на
	главном стеке и кадр только на стеке спящей задачи. В конце —
	положительный контроль: без кадров тот же модуль выгружается.

	Без исправления ядра (выравнивание FP по 4 и сохранённый FP спящего
	стека) первый случай ронял процесс, а второй выгружал занятый модуль. *)

	IMPORT Kernel, Meta, Tasks, UnitAssert;

	CONST codeMod = "Test64UnloadGuardCode";

	PROCEDURE CallCode (IN proc: ARRAY OF CHAR; OUT ok: BOOLEAN);
		VAR mi, it: Meta.Item;
	BEGIN
		ok := FALSE;
		Meta.Lookup(codeMod, mi);
		IF mi.Valid() THEN
			mi.Lookup(proc, it);
			IF it.Valid() THEN it.Call(ok) END
		END
	END CallCode;

	PROCEDURE EnsureCode (): BOOLEAN;
	(** Загрузить мишень, если её ещё нет в мире (refcnt остаётся 0). *)
		VAR mi: Meta.Item;
	BEGIN
		Meta.Lookup(codeMod, mi);
		RETURN mi.Valid()
	END EnsureCode;

	PROCEDURE CodeLoaded (): BOOLEAN;
	BEGIN
		RETURN Kernel.ThisLoadedMod(codeMod) # NIL
	END CodeLoaded;

	PROCEDURE TestProcUnloadMainFrame*;
	(** Процедура мишени исполняется на главном стеке и сама просит
		выгрузить свой модуль. *)
		VAR ok: BOOLEAN;
	BEGIN
		UnitAssert.True(EnsureCode(), "target module loaded");
		CallCode("UnloadSelf", ok);
		UnitAssert.True(ok, "target procedure called");
		UnitAssert.True(CodeLoaded(), "module with a live main-stack frame is not unloaded");
		UnitAssert.True(Tasks.Count() = 0, "no tasks leaked")
	END TestProcUnloadMainFrame;

	PROCEDURE TestProcUnloadTaskFrame*;
	(** Кадр мишени есть только на стеке спящей задачи. *)
		VAR ok: BOOLEAN; m: Kernel.Module;
	BEGIN
		UnitAssert.True(EnsureCode(), "target module loaded");
		CallCode("StartParked", ok);
		UnitAssert.True(ok, "task started");
		Tasks.Step;	(* мишень уходит в Tasks.Park *)
		UnitAssert.True(Tasks.Count() > 0, "task is alive and parked");
		m := Kernel.ThisLoadedMod(codeMod);
		UnitAssert.True(m # NIL, "target module found");
		Kernel.UnloadMod(m);
		IF CodeLoaded() THEN
			UnitAssert.True(TRUE, "module with a live task-stack frame is not unloaded");
			CallCode("Release", ok);
			UnitAssert.True(ok, "task released");
			UnitAssert.True(Tasks.Count() = 0, "task finished");
			(* положительный контроль: без кадров модуль выгружается *)
			m := Kernel.ThisLoadedMod(codeMod);
			Kernel.UnloadMod(m);
			UnitAssert.True(~CodeLoaded(), "unused module is unloaded")
		ELSE
			UnitAssert.True(FALSE, "module with a live task-stack frame is not unloaded")
		END
	END TestProcUnloadTaskFrame;

END Test64UnloadGuard.
