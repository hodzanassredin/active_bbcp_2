MODULE CheckPe64Api ["kernel32.dll"];

	IMPORT SYSTEM;

	PROCEDURE [ms64] GetStdHandle* (kind: INTEGER): SYSTEM.ADDRESS;

	PROCEDURE [ms64] WriteFile* (handle, buffer: SYSTEM.ADDRESS; size: INTEGER; VAR written: INTEGER; overlapped: SYSTEM.ADDRESS): INTEGER;

	PROCEDURE [ms64] ExitProcess* (code: INTEGER);

END CheckPe64Api.
