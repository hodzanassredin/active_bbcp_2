MODULE Loop;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, A. Dmitriev, I. Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Kernel, Services, Log; 

	CONST
		gcCycle = 100;
	
	TYPE
		ExitGuard* = PROCEDURE (): BOOLEAN;
	
	VAR
		gc: INTEGER;	(* how many events must be executed before next GC *)
		Validate, Iterate: PROCEDURE;
		Guard: ExitGuard;
		quantum*: LONGINT; (** minimal indivisible interval between Services.Action invocations *)
	
	PROCEDURE Collect;
	BEGIN
		gc := 0
	END Collect;

	PROCEDURE Loop;
		VAR exit: BOOLEAN; now, itstime: LONGINT;
	BEGIN
		(*
			immediates are executed as part of current command;
			the current command ends with window validation (delayed restore mechanism);
			actions marked Services.now or set for specific time are executed as soon as possible
			after the current command, but no sooner than quantum milliseconds
			after a previous actions invocation
		*)
		itstime := 0;
		REPEAT
			Services.actionHook.Loop; (* immediates *)
			IF Validate # NIL THEN Validate END;
			exit := Guard();
			IF (gc <= 0) OR Kernel.WouldFinalize() & ~exit THEN
				Kernel.Collect; gc := gcCycle;
			END;
			now := Kernel.Time();
			IF now >= itstime THEN 
				itstime := now + quantum;
				IF ~Log.synch THEN Log.FlushBuf END;
				Services.actionHook.Step;
				IF Validate # NIL THEN Validate ELSE DEC(gc) END;
			END;
			Iterate;
		UNTIL exit
	END Loop;

	PROCEDURE Start* (eventIterator, viewsValidator: PROCEDURE; exitGuard: ExitGuard);
	BEGIN
		ASSERT(eventIterator # NIL, 20); ASSERT(exitGuard # NIL, 21);
		Iterate := eventIterator;
		Guard := exitGuard;
		Kernel.InstallCleaner(Collect);
		Validate := viewsValidator;
		Kernel.Start(Loop);
		Kernel.RemoveCleaner(Collect)
	END Start;

BEGIN
	gc := gcCycle;
	quantum := 10
END Loop.