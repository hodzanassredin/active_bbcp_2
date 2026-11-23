MODULE CheckElf64State;

	IMPORT S := SYSTEM, Libc := LinLibc;

	VAR phase*: INTEGER; text: ARRAY 64 OF SHORTCHAR; n: LONGINT;

BEGIN
	phase := 1
CLOSE
	ASSERT(phase = 2, 105);
	text := "ELF64 terminators OK"; n := Libc.write(1, S.ADR(text), LEN(text$))
END CheckElf64State.
