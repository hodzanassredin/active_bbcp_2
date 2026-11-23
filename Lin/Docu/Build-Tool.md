DevCompiler.CompileSubs @Lin
	Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket

DevLinker1.LinkElfExe Linux blackboxLinux := Kernel$+ Utf LinKernel Files LinEnv LinFiles LinGui StdLoader LinLoader 

DevLinker1.LinkElfExe Linux blackboxInterpLinux := Kernel$+ Utf LinKernel Files LinEnv LinFiles StdLoader LinIntLoader


Linux x64 (experimental) from repository only

"DevLibrarian.EnableDevLib('BB64LIN', 'Mod64')"

DevCompiler64.CompileSubs @Lin
	Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket

DevLinker1.LinkElfExe64 'BB64LIN/blackboxLinux' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader