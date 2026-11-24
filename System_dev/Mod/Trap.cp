MODULE System_devTrap;
(** Базовый тест трапов: Kernel.Try выполняет тело, ловит трап,
	trapCount растёт, после Try выполнение продолжается.
	Эталон для System_devTrapStack. *)

	IMPORT SYSTEM, Kernel, System_devSupport;

	VAR count0: INTEGER; bodyRan, consoleBodyRan, consoleReturned: BOOLEAN;

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

	PROCEDURE ConsoleCrash (a, b, c: SYSTEM.ADDRESS);
	BEGIN
		consoleBodyRan := TRUE;
		HALT(42)
	END ConsoleCrash;

	PROCEDURE TryInConsole*;
	(** Красный тест (п.1.6): в консоли зритель трапов не установлен, но
		трап внутри Kernel.Try не должен завершать процесс; фатален только
		трап вне какого-либо Try. *)
		VAR n0: INTEGER;
	BEGIN
		System_devSupport.Begin("TryInConsole");
		consoleBodyRan := FALSE; consoleReturned := FALSE; n0 := Kernel.trapCount;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(ConsoleCrash)), 0, 0, 0);
		consoleReturned := TRUE;
		System_devSupport.Check(consoleBodyRan, "Try did not run its body");
		System_devSupport.Check(consoleReturned, "control did not return from Try");
		System_devSupport.Check(Kernel.trapCount > n0, "Try did not catch the trap");
		System_devSupport.End
	END TryInConsole;

	PROCEDURE Do*;
	BEGIN
		System_devSupport.Begin("Trap");

		bodyRan := FALSE; count0 := Kernel.trapCount;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(Probe)), 0, 0, 0);
		System_devSupport.Check(bodyRan, "Try did not run its body");
		System_devSupport.Check(Kernel.trapCount = count0, "unexpected trap in Probe");

		count0 := Kernel.trapCount;
		Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(Crash)), 0, 0, 0);
		System_devSupport.Check(Kernel.trapCount > count0, "Try did not catch the trap");
		Kernel.Collect;	(* ядро должно остаться живым после трапа *)
		System_devSupport.End
	END Do;

END System_devTrap.
