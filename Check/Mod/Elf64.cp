MODULE CheckElf64;

	IMPORT S := SYSTEM, Kernel, LinKernel, Libc := LinLibc, CheckMath64, CheckElf64State;

	VAR a: POINTER TO ARRAY OF INTEGER; i, traps: INTEGER; n: LONGINT;
		text: ARRAY 64 OF SHORTCHAR;

	PROCEDURE Trap;
	BEGIN INC(traps) END Trap;

	PROCEDURE Fault (a, b, c: S.ADDRESS);
	BEGIN HALT(77) END Fault;

BEGIN
	ASSERT(CheckElf64State.phase = 1, 106); CheckElf64State.phase := 2;
	ASSERT(Libc.getenv("ELF64_TEST_VALUE") # NIL, 107);
	ASSERT(LinKernel.argc = 2, 100);
	ASSERT(LinKernel.argv[1]^ = "argument", 108);
	ASSERT(Kernel.ThisLoadedMod("Kernel") # NIL, 101);
	ASSERT(Kernel.ThisLoadedMod("Kernel64") = NIL, 102);
	NEW(a, 100000); FOR i := 0 TO LEN(a)-1 DO a[i] := i END;
	Kernel.Collect;
	FOR i := 0 TO LEN(a)-1 DO ASSERT(a[i] = i, 103) END;
	Kernel.InstallTrapViewer(Trap); Kernel.Try(Fault, 0, 0, 0);
	ASSERT((traps = 1) & (Kernel.trapCount = 1), 109); Kernel.InstallTrapViewer(NIL);
	CheckMath64.Run; ASSERT(CheckMath64.failures = 0, 104);
	text := "DevLinker1 ELF64 native Kernel OK"; n := Libc.write(1, S.ADR(text), LEN(text$))
END CheckElf64.
