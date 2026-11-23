Console

DEFINITION Console;

	PROCEDURE ReadLn (OUT text: ARRAY OF CHAR);
	PROCEDURE SetConsole (c: Console);
	PROCEDURE WriteChar (c: CHAR);
	PROCEDURE WriteLn;
	PROCEDURE WriteStr (IN text: ARRAY OF CHAR);

END Console.


PROCEDURE ReadLn (OUT text: ARRAY OF CHAR);
Wait for new line input.

PROCEDURE WriteChar (c: CHAR);
Write c to output.

PROCEDURE WriteLn;
Write new line to output.

PROCEDURE WriteStr (IN text: ARRAY OF CHAR);
Write text to output.


Example:

	

For Windows

DevLinker.LinkExe dos demo.exe := Kernel+ Utf WinKernel Files WinEnv WinFiles Console WinConsole ObxConsole 

For Linux:

DevLinker1.LinkElfExe Linux demo := Kernel+ Utf LinKernel Files LinEnv LinFiles Console LinConsole ObxConsole  

