MODULE CheckMaxMemory;

	IMPORT Log;

	VAR a: ARRAY 128 OF POINTER TO ARRAY OF INTEGER;

	PROCEDURE Do*;
		VAR i: INTEGER;
	BEGIN
		i := 0;
		WHILE (i < LEN(a)) DO
			NEW(a[i], 1024 * 1024 * 4);
			IF a[i] # NIL THEN
				a[i, 0] := i
			ELSE
				i := LEN(a)
			END;
			INC(i);
		END;
		(* check memory read *)
		FOR i := 0 TO LEN(a) - 1 DO
			IF a[i] # NIL THEN
				Log.Int(a[i, 0]); Log.Ln;
			END;
		END;
	END Do;

END CheckMaxMemory.

CheckMaxMemory.Do
		