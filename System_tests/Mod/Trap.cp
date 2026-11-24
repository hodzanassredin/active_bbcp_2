MODULE System_testsTrap;
(** Базовый тест трапов: Kernel.Try выполняет тело, ловит трап,
	trapCount растёт, после Try выполнение продолжается.
	Эталон для System_testsTrapStack. *)

	IMPORT SYSTEM, Kernel, System_testsSupport;

	VAR count0: INTEGER; bodyRan: BOOLEAN;

	PROCEDURE Probe (a, b, c: SYSTEM.ADDRESS);
	BEGIN
		bodyRan := TRUE
	END Probe;

	PROCEDURE Crash (a, b, c: SYSTEM.ADDRESS);
		TYPE R = RECORD x: INTEGER END;
		VAR p: POINTER TO R;
	BEGIN
		p := NIL; p.x := 1	(* SIGSEGV *)
	END Crash;

	PROCEDURE Do*;
	BEGIN
		System_testsSupport.Begin("Trap");

		bodyRan := FALSE; count0 := Kernel.trapCount;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(Probe)), 0, 0, 0);
		System_testsSupport.Check(bodyRan, "Try did not run its body");
		System_testsSupport.Check(Kernel.trapCount = count0, "unexpected trap in Probe");

		count0 := Kernel.trapCount;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(Crash)), 0, 0, 0);
		System_testsSupport.Check(Kernel.trapCount > count0, "Try did not catch the trap");
		Kernel.Collect;	(* ядро должно остаться живым после трапа *)
		System_testsSupport.End
	END Do;

END System_testsTrap.
