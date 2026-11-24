MODULE Test64ActionTrap;
(** Действие Services.Action, в котором случился трап, теряется — так было
	в исходном BlackBox: это способ остановить сломанное действие. Раньше
	Cleanup возвращал `executing` в очередь, и действие с постоянной
	ошибкой трапало на каждом шаге главного цикла.

	Сценарий прокручивается шагами Services.actionHook.Step, а не
	Loop.Start: под UnitRunner тест уже находится внутри Kernel.Try.
	Действие делает HALT сразу при первом же запуске; следующий Step
	зовёт Cleanup — и действия в очереди быть не должно. *)

	IMPORT Services, UnitAssert;

	TYPE
		Act = POINTER TO RECORD (Services.Action)
			runs: INTEGER
		END;

	VAR act: Act;

	PROCEDURE (a: Act) Do;
	BEGIN
		INC(a.runs);
		HALT(42)	(* трап внутри действия *)
	END Do;

	PROCEDURE Step;
	BEGIN
		Services.actionHook.Step
	END Step;

	PROCEDURE RunOne;
	BEGIN
		NEW(act); act.runs := 0;
		Services.DoLater(act, Services.now);
		UnitAssert.Traps(Step, "action must trap");
		UnitAssert.EqualInt(1, act.runs, "action ran exactly once");
		Services.actionHook.Step;	(* Cleanup: трапнувшее действие не возвращается *)
		UnitAssert.EqualInt(1, act.runs, "trapped action is not requeued")
	END RunOne;

	PROCEDURE Do*;
	BEGIN
		RunOne
	END Do;

END Test64ActionTrap.
