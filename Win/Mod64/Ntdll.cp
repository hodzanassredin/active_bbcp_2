MODULE WinNtdll ["ntdll.dll"];
	IMPORT S := SYSTEM;
	PROCEDURE [ms64] RtlCaptureContext* (context: S.ADDRESS);
	PROCEDURE [ms64] RtlAddFunctionTable* (table: S.ADDRESS; count: INTEGER; base: S.ADDRESS): BOOLEAN;
	PROCEDURE [ms64] RtlDeleteFunctionTable* (table: S.ADDRESS): BOOLEAN;
	PROCEDURE [ms64] RtlLookupFunctionEntry* (pc: S.ADDRESS; VAR base: S.ADDRESS; history: S.ADDRESS): S.ADDRESS;
	PROCEDURE [ms64] RtlVirtualUnwind* (flags: INTEGER; base, pc, entry, context: S.ADDRESS;
		VAR data, frame: S.ADDRESS; pointers: S.ADDRESS): S.ADDRESS;
END WinNtdll.
