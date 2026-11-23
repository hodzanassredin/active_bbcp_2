MODULE CheckWinKernel64Recursive;
	IMPORT Kernel, WinKernel;
	PROCEDURE BrokenViewer;
	BEGIN HALT(78) END BrokenViewer;
	PROCEDURE Run;
	BEGIN HALT(77) END Run;
BEGIN Kernel.InstallTrapViewer(BrokenViewer); Kernel.Start(Run)
END CheckWinKernel64Recursive.
