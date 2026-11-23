ConsLog

Log implementation for console.


Example:

	

You can run this example from GUI version of framework.

ConsHello0.Do

The message will appear in the Log window, because StdLog implementation is used.
And it appears twice, because Do also runs in BEGIN section during module load process.

To use ConsLog implementation add it in the linked modules with required dependencies.


For Windows:

DevLinker.LinkExe dos hello0.exe := Kernel+ Utf WinKernel Files WinEnv WinFiles Log Services Math Strings Fonts Librarian Dialog Console WinConsole ConsLog ConsHello0  

For Linux:

DevLinker1.LinkElfExe Linux hello0 := Kernel+ Utf LinKernel Files LinEnv LinFiles Log Services Math Strings Fonts Librarian Dialog Console LinConsole ConsLog ConsHello0  


