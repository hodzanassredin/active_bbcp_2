MODULE WinConsole;
	IMPORT S := SYSTEM, Console, A := WinApi;
	TYPE Cons = POINTER TO RECORD (Console.Console) END;
	VAR cons: Cons; input, output: S.ADDRESS;
	PROCEDURE Print (IN text: ARRAY OF CHAR);
		VAR data: ARRAY 4096 OF SHORTCHAR; chars, bytes, written, result, offset: INTEGER;
	BEGIN
		offset := 0;
		WHILE offset < LEN(text$) DO
			chars := MIN(1000, LEN(text$)-offset);
			bytes := A.WideCharToMultiByte(1, 0, S.ADR(text[offset]), chars, S.ADR(data), LEN(data), 0, 0);
			IF bytes <= 0 THEN RETURN END;
			result := A.WriteFile(output, S.ADR(data), bytes, written, NIL);
			IF (result = 0) OR (written # bytes) THEN RETURN END;
			INC(offset, chars)
		END
	END Print;
	PROCEDURE (c: Cons) WriteStr (IN text: ARRAY OF CHAR);
	BEGIN Print(text) END WriteStr;
	PROCEDURE (c: Cons) WriteChar (ch: CHAR);
		VAR text: ARRAY 2 OF CHAR;
	BEGIN text[0] := ch; text[1] := 0X; Print(text) END WriteChar;
	PROCEDURE (c: Cons) WriteLn;
	BEGIN Print(0DX+0AX) END WriteLn;
	PROCEDURE (c: Cons) ReadLn (OUT text: ARRAY OF CHAR);
		VAR bytes: ARRAY 4096 OF SHORTCHAR; count, read, result: INTEGER;
	BEGIN
		count := 0;
		LOOP
			result := A.ReadFile(input, S.ADR(bytes[count]), 1, read, NIL);
			IF (result = 0) OR (read = 0) THEN EXIT END;
			INC(count);
			IF (bytes[count-1] = 0AX) OR (count = LEN(bytes)) THEN EXIT END
		END;
		result := A.MultiByteToWideChar(1, 0, S.ADR(bytes), count, S.ADR(text), LEN(text)-1);
		IF result < 0 THEN result := 0 END; text[result] := 0X
	END ReadLn;
BEGIN
	input := A.GetStdHandle(-10); output := A.GetStdHandle(-11);
	NEW(cons); Console.SetConsole(cons)
END WinConsole.
