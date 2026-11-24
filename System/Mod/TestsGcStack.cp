MODULE SystemTestsGcStack;
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

	IMPORT SYSTEM, Kernel, SystemTestsSupport, SystemTestsCo;

	TYPE Obj = POINTER TO RECORD tag: INTEGER; pad: ARRAY 8 OF LONGINT END;

	VAR finalized: BOOLEAN; fin4242: BOOLEAN; coError: INTEGER; coDone: BOOLEAN;
		rawDone: BOOLEAN; rawErr: INTEGER;

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
		SystemTestsSupport.Check(~finalized, "main stack: object finalized (control failed)");
		SystemTestsSupport.Check(adr # 0, "main stack: ADR lost");
		IF ~finalized THEN
			SystemTestsSupport.Check(o.tag = 111, "main stack: object corrupted")
		END
	END MainControl;

	PROCEDURE [ccall] Work (arg: SYSTEM.ADDRESS);
		VAR o: Obj; adr: SYSTEM.ADDRESS;
	BEGIN
		finalized := FALSE; fin4242 := FALSE; coError := 0;
		NEW(o); o.tag := 424242;
		adr := SYSTEM.ADR(o);	(* заставляет компилятор держать указатель в слоте стека *)
		SystemTestsSupport.Check(Kernel.InRegisteredStack(adr),
			"second stack is not in the kernel registry");
		Kernel.Collect; Kernel.Collect;
		IF fin4242 OR (o.tag # 424242) THEN coError := 1 END;
		coDone := TRUE
	END Work;

	PROCEDURE [ccall] RawWork (arg: SYSTEM.ADDRESS);
	(** Сборка мусора на стеке, которого нет в реестре ядра (обратный
		вызов из C): раньше MarkLocals трапал HALT(126). Теперь сборка
		отменяется целиком, поэтому объект, на который ссылается только
		этот стек, остаётся цел. *)
		VAR o: Obj;
	BEGIN
		NEW(o); o.tag := 777;
		SystemTestsSupport.Check(~Kernel.InRegisteredStack(SYSTEM.ADR(o)),
			"raw stack must not be in the kernel registry");
		Kernel.Collect; Kernel.Collect;
		IF o.tag = 777 THEN rawErr := 0 ELSE rawErr := 1 END;
		rawDone := TRUE
	END RawWork;

	PROCEDURE Do*;
		VAR res: INTEGER; base: SYSTEM.ADDRESS;
	BEGIN
		SystemTestsSupport.Begin("GcStack");
		finalized := FALSE; fin4242 := FALSE; coError := 0; coDone := FALSE;

		MainControl;

		res := SystemTestsCo.co_create(SYSTEM.ADR(Work), 0, 1024 * 1024);
		SystemTestsSupport.Check(res = 0, "co_create failed");
		base := SystemTestsCo.co_stack_base();
		SystemTestsSupport.Hex("second stack: ", SYSTEM.VAL(LONGINT, base));
		res := SystemTestsCo.co_resume();
		SystemTestsSupport.Check(res = 0, "co_resume failed");
		SystemTestsSupport.Check(coDone, "coroutine did not finish");
		SystemTestsSupport.Check(coError = 0,
			"object reachable only from second stack was collected");
		SystemTestsSupport.Check(~fin4242,
			"FINALIZE ran for object still reachable from second stack");
		res := SystemTestsCo.co_release();

		(* Незнакомый стек: сборка не должна трапать и не должна
		   освобождать объекты, на которые он ссылается. *)
		rawDone := FALSE; rawErr := 0;
		res := SystemTestsCo.co_create_raw(SYSTEM.ADR(RawWork), 0, 1024 * 1024);
		SystemTestsSupport.Check(res = 0, "raw co_create failed");
		res := SystemTestsCo.co_resume();
		SystemTestsSupport.Check(res = 0, "raw co_resume failed");
		SystemTestsSupport.Check(rawDone, "GC on an unregistered stack did not finish");
		SystemTestsSupport.Check(rawErr = 0,
			"GC on an unregistered stack touched a reachable object");
		res := SystemTestsCo.co_release();

		SystemTestsSupport.End
	END Do;

END SystemTestsGcStack.
