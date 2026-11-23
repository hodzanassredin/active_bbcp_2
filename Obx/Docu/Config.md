Overview by Example: ObxConfig

This example demonstrates how to make specific config file for executable

ObxConfig sources

Select and compile module that set config variable appConfigProcedure:

MODULE ObxSetConfig;
	IMPORT Kernel;
	VAR
		appConfigProcedure*: Kernel.Name;
BEGIN
	appConfigProcedure := "ObxConfig.Setup"
END ObxSetConfig.

DevCompiler.CompileSelection

Prepare executable file:

DevLinker.Link exefilename.exe := Kernel$+ Utf WinKernel Files WinEnv WinFiles WinPackedFiles WinGui StdLoader ObxSetConfig WinLoader ~

If you wish to pack all modules, prepare list of loaded modules

DevPacker.ListLoadedModules

And add to this list all required extra modules and resources

Obx/Code/Config.ocf 
Obx/Code/BlackBox.ocf 
Obx/Rsrc/BlackBox.odc 
Obx/Docu/BB-Rules.odc

Then call DevPacker.PackThis in new document.


Similar for Linux and other operating systems

DevLinker1.LinkElfExe Linux exefilename := Kernel$+ Utf LinKernel Files LinEnv LinFiles LinPackedFiles LinGui StdLoader ObxSetConfig LinLoader



