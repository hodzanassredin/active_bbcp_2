MODULE Test64ActionTrap;
(** КРАСНЫЙ ТЕСТ (этап 0): действие Services.Action, в котором случился
	трап, исчезает из очереди, и об этом никто не узнаёт.

	Семантика (System/Mod/Services.odc, IterateOverActions):
	перед Exec действие вынимают из candidates (p.next := NIL); если Do
	вызовет DoLater(a, ...), действие вернётся в actions и после трапа
	переживёт Cleanup. Если Do не успело вызвать DoLater до трапа,
	действие пропадает молча: оно уже не в списках, владельцу ничего не
	сообщается (нет ни повторного запуска, ни флага, ни сообщения).

	Тест измеряет оба варианта и считает дефектом именно молчаливую
	потерю: после трапа действие, не переставившее себя в очередь,
	не исполняется; никакого признака этого события нет. *)

	IMPORT SYSTEM, Kernel, Services, Loop, Test64Support;

	TYPE
		Act = POINTER TO RECORD (Services.Action)
			runs: INTEGER;
			requeue: BOOLEAN;
			trapped: BOOLEAN
		END;

	VAR act: Act; loops: INTEGER; deadline: LONGINT; done: BOOLEAN; dummy: INTEGER;

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
			(* Переставить себя в очередь ДО трапа или уже после — суть варианта. *)
			IF a.requeue THEN Services.DoLater(a, Services.now) END;
			a.trapped := TRUE;
			Crash	(* трап внутри действия *)
		ELSIF a.runs >= 3 THEN
			done := (a = act)	(* выжившее действие выполнилось ещё раз *)
		ELSE
			Services.DoLater(a, Services.now)	(* дожить до второго запуска *)
		END
	END Do;

	PROCEDURE Iterate;
	BEGIN
	END Iterate;

	PROCEDURE Guard (): BOOLEAN;
	BEGIN
		INC(loops);
		RETURN done OR (Kernel.Time() >= deadline)
	END Guard;

	(* Прокрутить цикл событий: действия исполняет Services.actionHook.Step,
	   который вызывает Loop.Loop. Ждём: done (действие пережило трап и
	   выполнилось ещё раз) либо истечения msec. *)
	PROCEDURE Pump (msec: LONGINT);
	BEGIN
		loops := 0; done := FALSE; deadline := Kernel.Time() + msec;
		Loop.Start(Iterate, NIL, Guard)
	END Pump;

	PROCEDURE Run1 (requeue: BOOLEAN): INTEGER;
	BEGIN
		NEW(act); act.runs := 0; act.requeue := requeue; act.trapped := FALSE;
		Services.DoLater(act, Services.now);
		Pump(400);
		RETURN act.runs
	END Run1;

	PROCEDURE Do*;
		VAR runsRequeue, runsPlain: INTEGER;
	BEGIN
		Test64Support.Begin("ActionTrap");

		(* Вариант 1: Do вызывало DoLater до трапа — действие должно выжить. *)
		runsRequeue := Run1(TRUE);
		Test64Support.Int("requeue-before-trap: runs=", runsRequeue);
		Test64Support.Check(act.trapped, "action did not trap");
		Test64Support.Check(runsRequeue >= 3,
			"action lost despite DoLater before the trap");

		(* Вариант 2: Do трапает до DoLater — действие пропадает. *)
		runsPlain := Run1(FALSE);
		Test64Support.Int("no-requeue: runs=", runsPlain);
		Test64Support.Check(act.trapped, "action did not trap (2)");
		IF runsPlain = 2 THEN
			Test64Support.Fail("trapped action vanished silently (no re-run, no signal)")
		END;

		Test64Support.End
	END Do;

END Test64ActionTrap.
