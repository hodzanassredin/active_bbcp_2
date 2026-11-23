MODULE Test64GcStack;
(** Сборка мусора на втором стеке (этап 2).

	Стек сопрограммы выделяется mmap (вне кучи CP), переключение —
	через ucontext (tools/test64.c). На втором стеке создаётся объект,
	единственная ссылка на него — локальная переменная; вызывается
	Kernel.Collect. Ядро обязано просканировать зарегистрированный стек
	от FP до его верха и сохранить объект.

	ФИНАЛИЗАТОРЫ: MainControl оставляет после себя мусор с FINALIZE
	(метка 111). Его финализатор законно срабатывает позже, на сборке
	в сопрограмме, поэтому FINALIZE запоминает отдельный флаг для метки
	424242; дефектом считается только финализатор объекта второго стека.

	Было (этап 0, kernel-report §2.4а, §8.1): MarkLocals при FP вне
	[baseStack-64МБ, baseStack] молча пропускал скан, и объект терялся.
	Теперь реестр стеков (Kernel.RegisterStack/UnregisterStackAt) и скан
	всех зарегистрированных стеков. *)

	IMPORT SYSTEM, Kernel, Test64Support, Test64Co;

	TYPE Obj = POINTER TO RECORD tag: INTEGER; pad: ARRAY 8 OF LONGINT END;

	VAR finalized: BOOLEAN; fin4242: BOOLEAN; coError: INTEGER; coDone: BOOLEAN;

	PROCEDURE (o: Obj) FINALIZE;
	BEGIN
		finalized := TRUE;
		IF o.tag = 424242 THEN fin4242 := TRUE END
	END FINALIZE;

	(* Контроль: тот же сценарий на главном стеке должен быть зелёным. *)
	PROCEDURE MainControl;
		VAR o: Obj; adr: SYSTEM.ADDRESS;
	BEGIN
		finalized := FALSE; fin4242 := FALSE;
		NEW(o); o.tag := 111;
		adr := SYSTEM.ADR(o);
		Kernel.Collect; Kernel.Collect;
		Test64Support.Check(~finalized, "main stack: object finalized (control failed)");
		Test64Support.Check(adr # 0, "main stack: ADR lost");
		IF ~finalized THEN
			Test64Support.Check(o.tag = 111, "main stack: object corrupted")
		END
	END MainControl;

	PROCEDURE [ccall] Work (arg: SYSTEM.ADDRESS);
		VAR o: Obj; adr: SYSTEM.ADDRESS;
	BEGIN
		finalized := FALSE; fin4242 := FALSE; coError := 0;
		NEW(o); o.tag := 424242;
		adr := SYSTEM.ADR(o);	(* заставляет компилятор держать указатель в слоте стека *)
		Test64Support.Check(Kernel.InRegisteredStack(adr),
			"second stack is not in the kernel registry");
		Kernel.Collect; Kernel.Collect;
		IF fin4242 OR (o.tag # 424242) THEN coError := 1 END;
		coDone := TRUE
	END Work;

	PROCEDURE Do*;
		VAR res: INTEGER; base: SYSTEM.ADDRESS;
	BEGIN
		Test64Support.Begin("GcStack");
		finalized := FALSE; fin4242 := FALSE; coError := 0; coDone := FALSE;

		MainControl;

		res := Test64Co.co_create(SYSTEM.ADR(Work), 0, 1024 * 1024);
		Test64Support.Check(res = 0, "co_create failed");
		base := Test64Co.co_stack_base();
		Test64Support.Hex("second stack: ", SYSTEM.VAL(LONGINT, base));
		res := Test64Co.co_resume();
		Test64Support.Check(res = 0, "co_resume failed");
		Test64Support.Check(coDone, "coroutine did not finish");
		Test64Support.Check(coError = 0,
			"object reachable only from second stack was collected");
		Test64Support.Check(~fin4242,
			"FINALIZE ran for object still reachable from second stack");
		res := Test64Co.co_release();
		Test64Support.End
	END Do;

END Test64GcStack.
