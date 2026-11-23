MODULE CheckTestHr;
	IMPORT User32, Kernel32;
BEGIN 
	User32.MessageBoxA(0, "Hershel's BlackBox", "Make it as simple as possible, but not simpler", 0); 
	Kernel32.ExitProcess(0);
END CheckTestHr.

MODULE CheckHrUser32 ["user32.dll"];
	TYPE PtrSTR = POINTER TO ARRAY [untagged] OF SHORTCHAR;
	PROCEDURE [ms64] MessageBoxA* (hwnd: LONGINT; msg, title: PtrSTR; buttons: LONGINT);
END CheckHrUser32.

MODULE CheckHrKernel32 ["kernel32.dll"];
	PROCEDURE [ms64] ExitProcess* (code: LONGINT);
END CheckHrKernel32.

"HrPeFmt.Install; HrCompiler.ModuleTo('CheckTestHr', 'pe')"