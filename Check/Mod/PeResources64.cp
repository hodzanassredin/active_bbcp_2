MODULE CheckPeResources64;

	IMPORT S := SYSTEM, A := CheckPeResourceApi64;

	VAR module, resource, data: S.ADDRESS; byte: BYTE;
		typ, name: ARRAY 32 OF CHAR;

	PROCEDURE Check (kind, id: S.ADDRESS; language, size, first: INTEGER);
	BEGIN
		resource := A.FindResourceExW(module, kind, id, language);
		IF resource = 0 THEN A.ExitProcess(20) END;
		IF (size >= 0) & (A.SizeofResource(module, resource) # size) THEN A.ExitProcess(40) END;
		data := A.LockResource(A.LoadResource(module, resource));
		IF data = 0 THEN A.ExitProcess(41) END;
		IF first >= 0 THEN S.GET(data, byte); IF byte MOD 256 # first THEN A.ExitProcess(42) END END
	END Check;

BEGIN
	module := A.GetModuleHandleW(0);
	IF A.LoadIconW(module, 1) = 0 THEN A.ExitProcess(1) END;
	IF A.LoadCursorW(module, 2) = 0 THEN A.ExitProcess(2) END;
	IF A.LoadImageW(module, 3, 0, 0, 0, 0) = 0 THEN A.ExitProcess(3) END;
	Check(16, 4, 1033, -1, -1);
	Check(24, 5, 1033, -1, ORD("<"));
	typ := "TYPELIB"; Check(S.ADR(typ), 6, 1033, 8, ORD("T"));
	Check(10, 100, 1033, 7, ORD("E")); Check(10, 100, 1049, 7, ORD("R"));
	typ := "ТЕСТ"; name := "ДАННЫЕ";
	resource := A.FindResourceExW(module, S.ADR(typ), S.ADR(name), 1033);
	IF (resource = 0) OR (A.SizeofResource(module, resource) # 6) THEN A.ExitProcess(43) END;
	A.ExitProcess(0)
END CheckPeResources64.
