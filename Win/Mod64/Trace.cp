MODULE WinTrace;

	IMPORT S := SYSTEM, A := WinApi;

	VAR handle: S.ADDRESS;

	PROCEDURE Write* (IN text: ARRAY OF CHAR);
		VAR bytes: ARRAY 512 OF BYTE; i, n, written, result, error: INTEGER;
	BEGIN
		IF (handle = 0) OR (handle = -1) THEN RETURN END;
		error := A.GetLastError(); i := 0; n := MIN(LEN(text$), LEN(bytes)-2);
		WHILE i < n DO bytes[i] := SHORT(SHORT(ORD(text[i]) MOD 128)); INC(i) END;
		bytes[i] := 13; bytes[i+1] := 10;
		result := A.WriteFile(handle, S.ADR(bytes), i+2, written, NIL);
		result := A.FlushFileBuffers(handle); A.SetLastError(error)
	END Write;

	PROCEDURE Value* (IN label: ARRAY OF CHAR; value: S.ADDRESS);
		VAR text: ARRAY 160 OF CHAR; i, j, digit: INTEGER;
	BEGIN
		IF handle = 0 THEN RETURN END;
		i := 0;
		WHILE (label[i] # 0X) & (i < LEN(text)-22) DO text[i] := label[i]; INC(i) END;
		text[i] := " "; text[i+1] := "0"; text[i+2] := "x"; INC(i, 3);
		j := 15;
		WHILE j >= 0 DO
			digit := SHORT(value MOD 16); value := value DIV 16;
			IF digit < 10 THEN text[i+j] := CHR(ORD("0")+digit)
			ELSE text[i+j] := CHR(ORD("A")+digit-10) END;
			DEC(j)
		END;
		text[i+16] := 0X; Write(text)
	END Value;

	PROCEDURE Start*;
		VAR path: ARRAY 1024 OF CHAR; file: ARRAY 32 OF CHAR; i, j, n: INTEGER;
	BEGIN
		n := A.GetModuleFileNameW(0, S.ADR(path), LEN(path));
		IF (n = 0) OR (n >= LEN(path)) THEN RETURN END;
		i := n;
		WHILE (i > 0) & (path[i-1] # "\") & (path[i-1] # "/") DO DEC(i) END;
		file := "blackbox-startup.log"; j := 0;
		WHILE (file[j] # 0X) & (i < LEN(path)-1) DO path[i] := file[j]; INC(i); INC(j) END;
		path[i] := 0X;
		handle := A.CreateFileW(path, {30}, {0,1}, NIL, 2, {7}, 0);
		IF handle = -1 THEN
			handle := 0; RETURN
		END;
		Write("BlackBox Windows x64 startup trace v2 (aligned VirtualQuery)");
		Value("process ID", A.GetCurrentProcessId())
	END Start;

END WinTrace.
