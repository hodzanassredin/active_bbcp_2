MODULE CheckPeResourceApi64 ["kernel32.dll"];

	IMPORT SYSTEM;

	PROCEDURE [ms64] GetModuleHandleW* (name: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	PROCEDURE [ms64] FindResourceExW* (module, typ, name: SYSTEM.ADDRESS; language: INTEGER): SYSTEM.ADDRESS;
	PROCEDURE [ms64] LoadResource* (module, resource: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	PROCEDURE [ms64] LockResource* (resource: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	PROCEDURE [ms64] SizeofResource* (module, resource: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE [ms64] LoadIconW* ["USER32.dll", "LoadIconW"] (module, name: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	PROCEDURE [ms64] LoadCursorW* ["USER32.dll", "LoadCursorW"] (module, name: SYSTEM.ADDRESS): SYSTEM.ADDRESS;
	PROCEDURE [ms64] LoadImageW* ["USER32.dll", "LoadImageW"] (module, name: SYSTEM.ADDRESS; kind, width, height, flags: INTEGER): SYSTEM.ADDRESS;
	PROCEDURE [ms64] ExitProcess* (code: INTEGER);

END CheckPeResourceApi64.
