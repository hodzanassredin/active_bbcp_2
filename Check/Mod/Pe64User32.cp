MODULE CheckPe64User32 ["user32.dll"];

	IMPORT SYSTEM;

	TYPE PtrSTR = POINTER TO ARRAY [untagged] OF SHORTCHAR;

	PROCEDURE [ms64] MessageBoxA* (hwnd: SYSTEM.ADDRESS; msg, title: PtrSTR; buttons: INTEGER): INTEGER;

END CheckPe64User32.
