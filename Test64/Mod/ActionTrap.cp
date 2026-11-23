MODULE Test64ActionTrap;
(** Действие Services.Action, в котором случился трап, не должно
	пропадать молча (этап 1, Services.Cleanup: `executing` возвращается
	в очередь).

	Сценарий прокручивается шагами Services.actionHook.Step, а не
	Loop.Start: под UnitRunner тест уже находится внутри Kernel.Try, и
	собственный цикл событий перехватил бы трап иначе. Шаги:
	  1) действие выполняется и переставляет себя в очередь;
	  2) на втором запуске трапает (UnitAssert.Traps ловит);
	  3) следующий Step сначала зовёт Cleanup, затем действие
	     выполняется ещё раз — и в варианте с DoLater до трапа, и без него.
	Регрессия: без возврата `executing` третий запуск не состоялся бы. *)

	IMPORT Services, UnitAssert;

	TYPE
		Act = POINTER TO RECORD (Services.Action)
			runs: INTEGER;
			requeue: BOOLEAN;
			trapped: BOOLEAN
		END;

	VAR act: Act;

	PROCEDURE Crash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END Crash;

	PROCEDURE (a: Act) Do;
	BEGIN
		INC(a.runs);
		IF a.runs = 2 THEN
			IF a.requeue THEN Services.DoLater(a, Services.now) END;
			a.trapped := TRUE;
			Crash	(* трап внутри действия *)
		ELSIF a.runs < 2 THEN
			Services.DoLater(a, Services.now)	(* дожить до второго запуска *)
		END
	END Do;

	PROCEDURE Step;
	BEGIN
		Services.actionHook.Step
	END Step;

	PROCEDURE RunOne (requeue: BOOLEAN);
	BEGIN
		NEW(act); act.runs := 0; act.requeue := requeue; act.trapped := FALSE;
		Services.DoLater(act, Services.now);
		Services.actionHook.Step;	(* 1-й запуск: действие живёт и просит ещё *)
		UnitAssert.EqualInt(1, act.runs, "first run");
		UnitAssert.Traps(Step, "action must trap on the second run");
		UnitAssert.EqualInt(2, act.runs, "second run reached the trap");
		UnitAssert.True(act.trapped, "action did trap");
		Services.actionHook.Step;	(* Cleanup + 3-й запуск *)
		UnitAssert.EqualInt(3, act.runs, "action survived the trap and ran again")
	END RunOne;

	PROCEDURE Do*;
	BEGIN
		RunOne(TRUE);	(* DoLater до трапа *)
		RunOne(FALSE);	(* без DoLater: возвращает Cleanup *)
	END Do;

END Test64ActionTrap.
