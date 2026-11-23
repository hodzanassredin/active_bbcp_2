MODULE CheckHrTest;
	IMPORT CheckHrUser32, CheckHrKernel32;
BEGIN 
	CheckHrUser32.MessageBoxA(0, "Hershel's BlackBox", "Make it as simple as possible, but not simpler", 0);
	CheckHrKernel32.ExitProcess(0);
END CheckHrTest.

"HrPeFmt.Install; HrCompiler.ModuleTo('CheckHrTest', 'pe')"