# BlackBox Component Builder - Agent Guidelines

BlackBox Component Builder 2.0 (bbcb2) is development framework for Component Pascal programming language (description in file `Docu/CP-Lang.md`). This special version of framework uses utf8 text sources stored in `.cp`.

`OLD/` is an archived checkout. Do not compile sources or run tests from it.

`dev0Linux` is command line version of bbcb2, which includes compiler.

Run `./dev0Linux` from the project root directory, where the executable is located.

## Project Structure

Each subsystem is a folder in project directory.

Modules, documentation and compilation results organized as follows:

```
<Subsystem>/
  Mod/      - Source modules (.cp files in Component Pascal)
  Mod64/    - Source modules specific for AMD64 (.cp files in Component Pascal)
  Code/     - Compiled object code (.ocf binary files)
  Sym/      - Symbol files for binding modules (.osf binary files)
  Docu/     - Documentation in English (.md files)
  Docu/ru     - Documentation in Russian (.md files)
```

## How to find module sources?

Module name can be one word from upper letter.
In that case sources will be stored in subsystem "System".
For example module `Files` will be located in `System/Mod/Files.cp` or `System/Mod64/Files.cp` (for AMD64 target).

Also module name can be combined from several words (each from upper letter).
The first word is the name of subsystem.
All other words define file name with sources for that module.
For example module  `TextModels` will be located in `Text/Mod/Models.cp`.

## Running Commands

Commands are procedures without arguments exported by special mark `*`.

```componentpascal
MODULE ModuleName;

	IMPORT Log;

	PROCEDURE ProcName*;
	BEGIN
		Log.String("hello world"); Log.Ln
	END ProcName;

END ModuleName.
```

The interpreter `dev0Linux` can accept commands via stdin.

```bash
# Run a command
echo 'ModuleName.ProcName' | ./dev0Linux
```

## Compiling Modules

Use `DevCompiler.CompileThis` commands to compile:

```bash
# Compile specific modules
echo "DevCompiler.CompileThis DemoExternalVariable" | ./dev0Linux
```

## Rebuild the command-line host

Run `./pack-dev0-linux` to link and pack dev0Linux using the existing host,
DevLinker1, DevPacker and the fixed OCF list in the shell script. No Python is
required. It uses precompiled i386 objects in normal Code folders; compile
changed host modules with DevCompiler beforehand. It checks standalone startup
before replacing dev0Linux. The previous executable is saved in
`build/dev0-linux/dev0Linux.previous`. See `Dev/Docu/Dev0-Linux.md`.

## AMD64 source selection

Before every AMD64 compile or link session, enable `DevLibrarian.EnableDevLib(outputDirectory, 'Mod64')`. It selects `<Subsystem>/Mod64/<Name>.cp` when present, otherwise `<Subsystem>/Mod/<Name>.cp`. Module names have no `__x64` suffix. Code and symbol files go to the output directory; keep i386, Linux AMD64 and Windows AMD64 outputs separate. `DevLibrarian.DisableDevLib` restores the standard librarian. Each new `dev0Linux` process must enable the librarian again.

## Build Linux x64

Example how to enable Librarian for x64 (take modules from `Mod64` if exists) and put results of compilation to `BB64LIN` directory.

```bash
echo "DevLibrarian.EnableDevLib('build/BB64LIN', 'Mod64');
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket;
DevLinker1.LinkElfExe64 'build/BB64LIN/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader" | ./dev0Linux
for subsystem in Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket; do
	if [ -d "$subsystem/Rsrc" ]; then
		mkdir -p "build/BB64LIN/$subsystem"
		cp -R "$subsystem/Rsrc" "build/BB64LIN/$subsystem/"
	fi
done
cp -R Docu build/BB64LIN/
```

## Build Windows x64

Example how to enable Librarian for x64 (take modules from `Mod64` if exists) and put results of compilation to `BB64WIN` directory.

```bash
echo "DevLibrarian.EnableDevLib('build/BB64WIN', 'Mod64');
DevCompiler64.CompileSubs @Win Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Mdi;
DevLinker1.LinkPeExe64 'build/BB64WIN/blackbox.exe' := Kernel+ WinKernel Files WinFiles StdLoader WinLoader 1 Applogo.ico 1 BlackBox.res
DevLinker1.LinkPeExe64 'build/BB64WIN/blackboxMDI.exe' := Kernel+ WinKernel Files WinFiles StdLoader MdiLoader 1 Applogo.ico 1 BlackBox.res" | ./dev0Linux
for subsystem in Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Mdi; do
	if [ -d "$subsystem/Rsrc" ]; then
		mkdir -p "build/BB64WIN/$subsystem"
		cp -R "$subsystem/Rsrc" "build/BB64WIN/$subsystem/"
	fi
done
cp -R Docu build/BB64WIN/
```

## AMD64 tests

Run scripts from the repository root. They enable DevLibrarian in each compiler process:

```bash
python3 Dev/Test/check_mod64_layout.py
python3 Lin/Test/check_linux64.py --out build/blackbox-lin64
python3 Lin/Test/check_gtk4.py
python3 Win/Test/check_subsystem64.py --out build/blackbox-win64
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64 --test
```

The Linux test script links `blackbox-console` with LinIntInit for stdin commands. Current LinLoader always starts the GUI; do not assume `blackbox --console` selects console mode. See `Dev/Docu/Building-AMD64.md` for further checks and prerequisites.

Never run automatic mouse drag tests: a previous test crashed the user's system. Verify drag-and-drop manually. Ordinary GUI tests and console memory tests are separate from mouse drag simulation.

## Pass parameters to commands

Text after command can be accessed by reading `DevCommanders.par.text` structure from `DevCommanders.par.beg` to `DevCommanders.par.end` positions.

```bash
# Example, how to pass parameters to command
echo "DemoParameters.Do some parameters" | ./dev0Linux
```

See Demo/Mod/Parameters.cp module as an example.

## Pass external variables to module

```bash
# Example, how to pass external variables to BlackBox module
echo "DemoExternalVariable.Do" | ./dev0Linux --VAR_NAME="some data"
```

`VAR_NAME` can be any name accessed then in sources by Dialog.GetPar("VAR_NAME", val, res)

there:

- first argument is name of external variable
- second argument is variable, where result of read should be stored (type of variable should be `ARRAY n OF CHAR`) 
- third argument is INTEGER variable with result of varialbe access (0 if no error, 2 - not found, 3 - conversion error) 

See Demo/Mod/ExternalVariable.cp module as an example.

```bash
# Example, how to pass multiple parameters to BlackBox module
echo "DemoExternalVariable.Do" | ./dev0Linux --PAR_NAME1="some data" --PAR_NAME2="some data"
```

## Formatting of sources

Don't use spaces for tabulation.

Put empty line before new sources block (IMPORT, CONST, TYPE, PROCEDURE).
