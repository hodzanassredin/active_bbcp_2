MODULE UnitAssert;
(** Проверки для подсистемы Unit: счётчик проверок и провалов,
	сообщение последнего провала в формате «ожидалось X, получено Y: msg».

	Все функции проверки увеличивают счётчик проверок; провал
	дополнительно увеличивает счётчик провалов и запоминает сообщение.
	UnitRunner снимает состояние до и после теста, поэтому вложенный
	прогон (самопроверка Unit_devMain) не портит внешний счёт. *)

	IMPORT SYSTEM, Strings, Kernel;

	TYPE
		Message* = ARRAY 256 OF CHAR;
		State* = RECORD
			checks-: LONGINT;
			failures-: INTEGER;
			msg-: Message
		END;

	VAR
		checks: LONGINT;
		failures: INTEGER;
		msg: Message;
		trapProc: PROCEDURE;
		trapDone: BOOLEAN;

	PROCEDURE Put (VAR s: Message; IN x: ARRAY OF CHAR);
	(* Дописать x к s; не поместившееся отбрасывается: длинные строки в
	   сообщении провала обрезаются, а не трапуют. *)
		VAR i, j: INTEGER;
	BEGIN
		i := 0; WHILE s[i] # 0X DO INC(i) END;
		j := 0;
		WHILE (i < LEN(s) - 1) & (j < LEN(x)) & (x[j] # 0X) DO s[i] := x[j]; INC(i); INC(j) END;
		s[i] := 0X
	END Put;

	PROCEDURE FailMsg (IN m: ARRAY OF CHAR);
	BEGIN
		msg := ""; Put(msg, m)
	END FailMsg;

	PROCEDURE AddFailure (IN m: ARRAY OF CHAR);
	BEGIN
		INC(failures); FailMsg(m)
	END AddFailure;

	PROCEDURE Reset*;
	BEGIN
		checks := 0; failures := 0; msg := ""
	END Reset;

	PROCEDURE Save* (OUT s: State);
	BEGIN
		s.checks := checks; s.failures := failures; s.msg := msg
	END Save;

	PROCEDURE Restore* (IN s: State);
	BEGIN
		checks := s.checks; failures := s.failures; msg := s.msg
	END Restore;

	PROCEDURE Checks* (): LONGINT;
	BEGIN
		RETURN checks
	END Checks;

	PROCEDURE Failures* (): INTEGER;
	BEGIN
		RETURN failures
	END Failures;

	PROCEDURE LastMessage* (OUT m: Message);
	BEGIN
		m := msg
	END LastMessage;

	(* --- проверки --------------------------------------------------------- *)

	PROCEDURE Checked*;
	(** Учесть одну проверку без условия (для адаптеров, которые сами
		печатают сообщение, например System_devSupport). *)
	BEGIN
		INC(checks)
	END Checked;

	PROCEDURE True* (cond: BOOLEAN; IN m: ARRAY OF CHAR);
	BEGIN
		INC(checks);
		IF ~cond THEN AddFailure(m) END
	END True;

	PROCEDURE Fail* (IN m: ARRAY OF CHAR);
	BEGIN
		INC(checks); AddFailure(m)
	END Fail;

	PROCEDURE NumberInto (x: LONGINT; VAR s: ARRAY OF CHAR);
	BEGIN
		Strings.IntToString(x, s)
	END NumberInto;

	PROCEDURE EqualInt* (expected, actual: LONGINT; IN m: ARRAY OF CHAR);
		VAR a, b: ARRAY 40 OF CHAR; s: Message;
	BEGIN
		INC(checks);
		IF expected # actual THEN
			NumberInto(expected, a); NumberInto(actual, b);
			s := "expected "; Put(s, a); Put(s, ", got "); Put(s, b);
			IF m # "" THEN Put(s, ": "); Put(s, m) END;
			AddFailure(s)
		END
	END EqualInt;

	PROCEDURE EqualReal* (expected, actual, eps: REAL; IN m: ARRAY OF CHAR);
		VAR a, b: ARRAY 40 OF CHAR; s: Message; ok: BOOLEAN;
	BEGIN
		INC(checks);
		IF (eps <= 0.0) OR (expected = 0.0) THEN ok := actual = expected
		ELSE ok := ABS(actual - expected) <= eps
		END;
		IF ~ok THEN
			Strings.RealToString(expected, a); Strings.RealToString(actual, b);
			s := "expected "; Put(s, a); Put(s, ", got "); Put(s, b);
			IF m # "" THEN Put(s, ": "); Put(s, m) END;
			AddFailure(s)
		END
	END EqualReal;

	PROCEDURE EqualStr* (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR);
		VAR s: Message;
	BEGIN
		INC(checks);
		IF expected # actual THEN
			s := "expected <"; Put(s, expected); Put(s, ">, got <"); Put(s, actual); Put(s, ">");
			IF m # "" THEN Put(s, ": "); Put(s, m) END;
			AddFailure(s)
		END
	END EqualStr;

	PROCEDURE EqualSet* (expected, actual: SET; IN m: ARRAY OF CHAR);
		VAR a, b: ARRAY 40 OF CHAR; s: Message;
	BEGIN
		INC(checks);
		IF expected # actual THEN
			NumberInto(SYSTEM.VAL(INTEGER, expected), a);
			NumberInto(SYSTEM.VAL(INTEGER, actual), b);
			s := "expected set "; Put(s, a); Put(s, ", got "); Put(s, b);
			IF m # "" THEN Put(s, ": "); Put(s, m) END;
			AddFailure(s)
		END
	END EqualSet;

	PROCEDURE NotNil* (p: ANYPTR; IN m: ARRAY OF CHAR);
		VAR s: Message;
	BEGIN
		INC(checks);
		IF p = NIL THEN s := "expected non-NIL pointer: "; Put(s, m); AddFailure(s) END
	END NotNil;

	PROCEDURE Same* (expected, actual: ANYPTR; IN m: ARRAY OF CHAR);
		VAR s: Message;
	BEGIN
		INC(checks);
		IF expected # actual THEN s := "expected the same reference: "; Put(s, m); AddFailure(s) END
	END Same;

	PROCEDURE TrapBody (a, b, c: SYSTEM.ADDRESS);
	BEGIN
		trapDone := FALSE; trapProc(); trapDone := TRUE
	END TrapBody;

	PROCEDURE Traps* (p: PROCEDURE; IN m: ARRAY OF CHAR);
		VAR s: Message;
	BEGIN
		INC(checks);
		IF p = NIL THEN AddFailure("Traps: procedure is NIL")
		ELSE
			trapProc := p; trapDone := FALSE;
			Kernel.Try(SYSTEM.VAL(Kernel.TryHandler, SYSTEM.ADR(TrapBody)), 0, 0, 0);
			IF trapDone THEN s := "expected a trap, but none happened: "; Put(s, m); AddFailure(s) END
		END
	END Traps;

BEGIN
	Reset
END UnitAssert.
