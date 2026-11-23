MODULE Test64TrapStack;
(** КРАСНЫЙ ТЕСТ (этап 0): трап на главном стеке, пока на втором стеке
	открыт Kernel.Try, уходит в чужой Try.

	Сценарий:
	  1. главный стек открывает Kernel.Try(M) и входит в сопрограмму;
	  2. сопрограмма открывает свой Kernel.Try(S) и уступает главному
	     (co_yield) — контекст S остаётся открытым, указатель на него
	     лежит в глобальной currentTryContext (Lin/Mod64/Kernel.odc:81);
	  3. главный стек вызывает трап без собственного вложенного Try.

	Правильно: трап принадлежит главному стеку и ловится M.
	Ожидаемый дефект (kernel-report §3.1, §8.3): siglongjmp уходит в
	последний открытый Try — S на стеке сопрограммы; обработка идёт
	на втором стеке, главный Try «не видит» трап. *)

	IMPORT SYSTEM, Kernel, Test64Support, Test64Co;

	VAR count0, stages, dummy: INTEGER;
		coCaught, mainResumed, coFinished: BOOLEAN;

	PROCEDURE CoTryProc (a, b, c: SYSTEM.ADDRESS);
	BEGIN
		INC(stages, 1);
		Test64Support.Int("co: before yield, stages=", stages);
		dummy := Test64Co.co_yield();	(* уступаем главному стеку внутри Try *)
		INC(stages, 100);
		Test64Support.Int("co: after yield, stages=", stages)
	END CoTryProc;

	PROCEDURE [ccall] CoBody (arg: SYSTEM.ADDRESS);
	BEGIN
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(CoTryProc)), 0, 0, 0);
		(* Try вернулся без явного возобновления со стороны главного стека —
		   значит, в его контекст прыгнул трап, случившийся на другом стеке. *)
		IF ~mainResumed THEN
			coCaught := TRUE;
			Test64Support.Fail("trap on main stack was caught by the second stack");
			Test64Support.Int("stages=", stages);
			Test64Support.End;	(* печатает TEST64-FAIL *)
			(* После siglongjmp на чужой стек возврат через uc_link/swapcontext
			   задевает libc (вторичный SIGSEGV SI_KERNEL), поэтому честно
			   завершаем процесс: вердикт уже напечатан. *)
			Kernel.Quit(42)
		END;
		coFinished := TRUE;
		dummy := Test64Co.co_yield()	(* штатный возврат на главный стек *)
	END CoBody;

	PROCEDURE Crash;
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1
	END Crash;

	PROCEDURE MainTryBody (a, b, c: SYSTEM.ADDRESS);
		VAR res: INTEGER;
	BEGIN
		INC(stages, 10);
		res := Test64Co.co_create(SYSTEM.ADR(CoBody), 0, 1024 * 1024);
		res := Test64Co.co_resume();	(* сопрограмма открыла свой Try и уступила *)
		Test64Support.Int("main: after co_resume, stages=", stages);
		INC(stages, 20);
		IF ~coCaught THEN
			Crash;	(* корректно: ловит этот (главный) Try, сюда не возвращаемся *)
			INC(stages, 1000)	(* дефект: управление вернулось через второй стек *)
		END
	END MainTryBody;

	PROCEDURE Do*;
		VAR res: INTEGER;
	BEGIN
		Test64Support.Begin("TrapStack");
		count0 := Kernel.trapCount;
		coCaught := FALSE; mainResumed := FALSE; coFinished := FALSE; stages := 0;

		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(MainTryBody)), 0, 0, 0);
		Test64Support.Int("main try returned, stages=", stages);

		IF ~coFinished THEN	(* корректный путь: дать второму стеку свернуть свой Try *)
			mainResumed := TRUE;
			res := Test64Co.co_resume()
		END;

		Test64Support.Int("stages=", stages);
		Test64Support.Check(Kernel.trapCount > count0, "trap did not happen");
		Test64Support.Check(~coCaught, "trap on main stack was caught by the second stack");
		Test64Support.Check(coFinished, "second stack did not finish");
		Test64Support.End
	END Do;

END Test64TrapStack.
