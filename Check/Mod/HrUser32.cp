MODULE CheckHrUser32 ["user32.dll"];
	TYPE PtrSTR = POINTER TO ARRAY [untagged] OF SHORTCHAR;
	PROCEDURE [ms64] MessageBoxA* (hwnd: LONGINT; msg, title: PtrSTR; buttons: LONGINT);
END CheckHrUser32.