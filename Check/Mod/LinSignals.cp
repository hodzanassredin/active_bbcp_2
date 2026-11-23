MODULE CheckLinSignals;

	IMPORT S := SYSTEM, Kernel, LinKernel, Libc := LinLibc, Api := CheckLinSignalApi, Console;

	TYPE Node = POINTER TO RECORD value: INTEGER; next: Node END;

	VAR viewed: INTEGER; nested, allowNested, sawAltStack: BOOLEAN;

	PROCEDURE Fault (a, b, c: S.ADDRESS);
	BEGIN HALT(77) END Fault;

	PROCEDURE NestedFault (a, b, c: S.ADDRESS);
	BEGIN HALT(78) END NestedFault;

	PROCEDURE Viewer;
		VAR before, after: Libc.stack_t;
	BEGIN
		INC(viewed);
		ASSERT(~Kernel.IsReadable(1, 2));
		ASSERT(Libc.sigaltstack(NIL, before) = 0);
		IF before.ss_flags * {0} # {} THEN sawAltStack := TRUE END;
		LinKernel.InstallSignals;
		ASSERT(Libc.sigaltstack(NIL, after) = 0);
		ASSERT((before.ss_sp = after.ss_sp) & (before.ss_size = after.ss_size));
		IF allowNested & ~nested THEN nested := TRUE; Kernel.Try(NestedFault, 0, 0, 0) END
	END Viewer;

	PROCEDURE BadRead (a, b, c: S.ADDRESS);
		VAR x: INTEGER;
	BEGIN S.GET(1, x); HALT(99) END BadRead;

	PROCEDURE Bus (a, b, c: S.ADDRESS);
		VAR res: INTEGER;
	BEGIN res := Api.raise(Libc.SIGBUS); HALT(99) END Bus;

	PROCEDURE LocalRoots;
		VAR root, p: Node; i: INTEGER;
	BEGIN
		root := NIL;
		FOR i := 0 TO 999 DO NEW(p); p.value := i; p.next := root; root := p END;
		Kernel.Collect;
		FOR i := 999 TO 0 BY -1 DO ASSERT(root # NIL); ASSERT(root.value = i); root := root.next END;
		ASSERT(root = NIL)
	END LocalRoots;

	PROCEDURE Run*;
		VAR before, after: Libc.stack_t; action: Libc.sigaction_t; count, res: INTEGER;
	BEGIN
		viewed := 0; nested := FALSE; allowNested := FALSE; sawAltStack := FALSE;
		Console.WriteStr("signals: stack"); Console.WriteLn;
		ASSERT(Libc.sigaltstack(NIL, before) = 0);
		ASSERT(before.ss_size >= 1024 * 1024);
		LinKernel.InstallSignals; LinKernel.InstallSignals;
		ASSERT(Libc.sigaltstack(NIL, after) = 0);
		ASSERT((before.ss_sp = after.ss_sp) & (before.ss_size = after.ss_size));
		Console.WriteStr("signals: SIGURG"); Console.WriteLn;
		ASSERT(Libc.sigaction(Libc.SIGURG, NIL, action) = 0);
		ASSERT(S.VAL(S.ADDRESS, action.sa_sigaction) = 0);
		res := Api.raise(Libc.SIGURG); ASSERT(res = 0);
		Console.WriteStr("signals: GC"); Console.WriteLn;
		LocalRoots;
		ASSERT(~Kernel.IsReadable(1, 2));
		Console.WriteStr("signals: traps"); Console.WriteLn;
		Kernel.InstallTrapViewer(Viewer); count := Kernel.trapCount;
		Kernel.Try(BadRead, 0, 0, 0);
		ASSERT(sawAltStack, 90);
		Kernel.Try(Bus, 0, 0, 0);
		allowNested := TRUE;
		Kernel.Try(Fault, 0, 0, 0);
		ASSERT(Kernel.trapCount = count + 4, 92);
		ASSERT(viewed >= 3, 93);
		LocalRoots;
		Console.WriteStr("Linux signals, nested traps and local GC roots OK"); Console.WriteLn
	END Run;

END CheckLinSignals.
