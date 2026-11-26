# BlackBox Component Builder - Agent Guidelines

BlackBox Component Builder 2.0 (bbcb2) is a development framework for Component Pascal
(description in `Docu/CP-Lang.md`). Sources are UTF-8 `.cp` files. This fork adds module `Tasks`:
cooperative tasks on their own stacks, the monitor object `Tasks.Object`, the pass `Tasks.Ctx`,
groups and `Spawn`. The language itself is not extended.

`OLD/` is an archived checkout. Do not compile sources or run tests from it.

`dev0Linux` is the command-line version of bbcb2 with the compiler. Run it from the repository
root, where the executable is located.

## Project Structure

Each subsystem is a folder in the project directory.

Modules, documentation and compilation results are organized as follows:

```
<Subsystem>/
  Mod/      - source modules (.cp files in Component Pascal)
  Mod64/    - source modules specific for AMD64 (.cp files in Component Pascal)
  Code/     - compiled object code (.ocf binary files)
  Sym/      - symbol files for binding modules (.osf binary files)
  Docu/     - documentation in English (.md files)
  Docu/ru   - documentation in Russian (.md files)
```

## How to Find Module Sources

A module name can be one word starting with an upper-case letter. In that case the sources are in
subsystem `System`. For example, module `Kernel` is in `System/Mod/Kernel.cp` or
`System/Mod64/Kernel.cp` (for the AMD64 target).

A module name can also consist of several words, each starting with an upper-case letter. The first
word is the subsystem name; the remaining words define the source file name. For example, module
`TextModels` is in `Text/Mod/Models.cp`.

## Running Commands

Commands are procedures without arguments exported with the mark `*`.

```componentpascal
MODULE ModuleName;

	IMPORT Log;

	PROCEDURE ProcName*;
	BEGIN
		Log.String("hello world"); Log.Ln
	END ProcName;

END ModuleName.
```

The interpreter `dev0Linux` accepts commands via stdin:

```sh
echo 'ModuleName.ProcName' | ./dev0Linux
```

## Compiling Modules

Use `DevCompiler.CompileThis` commands to compile:

```sh
echo "DevCompiler.CompileThis DemoExternalVariable" | ./dev0Linux
```

## AMD64 Source Selection

Before every AMD64 compile or link session, enable
`DevLibrarian.EnableDevLib(outputDirectory, 'Mod64')`. It selects `<Subsystem>/Mod64/<Name>.cp`
when present, otherwise `<Subsystem>/Mod/<Name>.cp`. Module names have no `__x64` suffix. Code and
symbol files go to the output directory; keep i386, Linux AMD64 and Windows AMD64 outputs separate.
`DevLibrarian.DisableDevLib` restores the standard librarian. Each new `dev0Linux` process must
enable the librarian again.

## Building Linux x64

```sh
echo "DevLibrarian.EnableDevLib('build/BB64LIN', 'Mod64');
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Unit;
DevCompiler64.CompileThis Taskarch;
DevCompiler64.CompileThis Tasks;
DevLinker1.LinkElfExe64 'build/BB64LIN/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader" | ./dev0Linux
for subsystem in Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Unit; do
	if [ -d "$subsystem/Rsrc" ]; then
		mkdir -p "build/BB64LIN/$subsystem"
		cp -R "$subsystem/Rsrc" "build/BB64LIN/$subsystem/"
	fi
done
cp -R Docu build/BB64LIN/
```

The repository script `tools/build64.sh` does this and more; see below.

## Building Windows x64

```sh
echo "DevLibrarian.EnableDevLib('build/BB64WIN', 'Mod64');
DevCompiler64.CompileSubs @Win Win System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket;
DevCompiler64.CompileThis Taskarch;
DevCompiler64.CompileThis Tasks;
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

## AMD64 Tests

Run scripts from the repository root. They enable DevLibrarian in each compiler process:

```sh
python3 Dev/Test/check_mod64_layout.py
python3 Lin/Test/check_linux64.py --out build/blackbox-lin64
python3 Lin/Test/check_gtk4.py
python3 Win/Test/check_subsystem64.py --out build/blackbox-win64
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64 --test
```

The Linux test script links `blackbox-console` with LinIntInit for stdin commands. Current
LinLoader always starts the GUI; do not assume `blackbox --console` selects console mode. See
`Dev/Docu/Building-AMD64.md` for further checks and prerequisites.

Never run automatic mouse drag tests: a previous test crashed the user's system. Verify
drag-and-drop manually. Ordinary GUI tests and console memory tests are separate from mouse drag
simulation.

## Passing Parameters to Commands

Text after a command is available in the `DevCommanders.par` structure from
`DevCommanders.par.beg` to `DevCommanders.par.end`.

```bash
echo "DemoParameters.Do some parameters" | ./dev0Linux
```

See `Demo/Mod/Parameters.cp` as an example.

## Passing External Variables to a Module

```bash
echo "DemoExternalVariable.Do" | ./dev0Linux --VAR_NAME="some data"
```

`VAR_NAME` can be any name read in the sources by `Dialog.GetPar("VAR_NAME", val, res)`, where:

- the first argument is the external variable name;
- the second is a variable of type `ARRAY n OF CHAR` for the value;
- the third is an INTEGER variable with the access result (0 — no error, 2 — not found, 3 —
  conversion error).

See `Demo/Mod/ExternalVariable.cp` as an example.

```bash
echo "DemoExternalVariable.Do" | ./dev0Linux --PAR_NAME1="some data" --PAR_NAME2="some data"
```

## Source Formatting

Do not use spaces for indentation. Put an empty line before each new source block (`IMPORT`,
`CONST`, `TYPE`, `PROCEDURE`).

## Repository Layout and Rules

- Sources are `.cp`; `OLD/` is not built. The default installation directory is `.work/BB64LIN`
  (not in git); `BB64INSTALL` overrides it. The console is `blackbox-console` (`LinIntInit`), the
  GUI is `blackbox` (`LinLoader`); `blackbox --console` does not select console mode.
- No sudo and no `apt install`. The 32-bit C toolchain is downloaded with `apt-get download` into
  `.work/i386dev`. Do not use the user's display (`DISPLAY=:0`). Do not emulate the mouse.
- Do not push.

### Build and Run

```sh
tools/build64.sh                  # all subsystems, console + GUI + help .odc
tools/build64.sh --clean          # erase .ocf/.osf in the installation
tools/build64.sh --console-only   # console only
tools/build64.sh --no-help        # skip Docu/*.md -> .odc conversion
tools/gui-world.sh                # .odc help alone, after a build
echo 'Команда' | tools/run64.sh
tools/go64.sh System_devBasic         # amd64 CompileThis
tools/go32.sh DevCPE                  # i386 CompileThis -> .work/i386
tools/build32.sh                      # i386 world and console in build/BB32LIN
tools/i386-toolchain.sh               # 32-bit C toolchain without sudo (gcc flags)
tools/bootstrap.sh                    # three passes of Dev, B and C byte-identical
tools/bench-gc.sh System_devGcBench.DoSmall   # 1 and 10 million
tools/iso.sh tools/build64.sh         # any command with its own /tmp (parallel copies)
tools/worktree.sh ../active_bb-x x    # parallel worktree: links, bootstrap, build64
```

`tools/build64.sh` compiles `Mod64` when present, writes `Code`/`Sym` to the installation, then
compiles `Taskarch` and `Tasks`. It links `blackbox-console` and, unless `--console-only` is given,
`blackbox`. When `Obx` is built and `--no-help` is not given, it calls `tools/gui-world.sh` to turn
`Docu/*.md` and `Mod/*.cp` into `.odc` with markup and links, and checks `Sys-Map.odc` through
`Views.OldView`.

`tools/env.sh:clean_host_temp` removes `odc` names with 5 or 9 digits from `/tmp` before starting
`dev0Linux`.

### Tests

Tests use subsystem `Unit` (`UnitAssert`, `UnitRunner`; rules in
`Unit/Docu/ru/Testing.md`). The manifest `tools/tests.txt` has the format
`<module>|<marks>|<timeout>|<extra modules>|<options>`. The fifth field is passed to
`CompileThis`. `tools/test.sh` calls `tools/unit.sh` for each module: a separate process, success
only with `failed 0`. Tests live in separate subsystems next to the one under test: `<Sub>_dev`,
file `<Sub>_dev/Mod[64]/<Theme>.cp`, module `<Sub>_dev<Theme>` (`System_dev/Mod/Tasks.cp` —
`System_devTasks`); the entry point without a theme is `<Sub>_devMain`, for example
`System_devMain`. `UnitRunner.Run System` selects loaded `System_dev*`; `all` selects every module
whose name contains `_dev`.

```sh
tools/test.sh                 # all modules from tools/tests.txt
tools/test.sh --list
tools/test.sh System_devMain  # selected modules
tools/test32.sh               # i386: Unit, Files, Tasks, CoTests, UnloadGuard, OpenArr, PtrInit
tools/test-win64.sh           # Windows x64 under wine
tools/check-reject.sh         # negative checks of ordinary language rules on Tasks
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

Production builds (`build64.sh`, `build32.sh`, `test-win64.sh`) do not include `*_dev`
subsystems: they are not in `CompileSubs`. Tests are built on demand by `tools/unit.sh` and
`tools/go64.sh` (x64), `tools/test32.sh` (i386), and `tools/test-win64.sh` (wine).

### Kernel and Tasks

- `System/Mod/Tasks.cp` implements tasks for both amd64 and i386; the machine part (word size,
  frame layout, `[code] Switch`) is module `Taskarch`, one file per target in `System/Mod` and
  `System/Mod64`; `Switch` is inlined at the call site. Task stacks come from
  `Kernel.AllocStack/FreeStack` (`mmap` with a guard page in `LinKernel` and `VirtualAlloc` with
  `PAGE_NOACCESS` in `WinKernel`).
- `Tasks.Object` is a monitor object: `Do` passes `Tasks.Ctx` and a `Tasks.Op` request to the
  method `Exclusive-`; `Await`/`AwaitCond` wait for flags or a condition object. A `Tasks.Job`
  runs its body once, a `Tasks.Task` can be started again.
- A finished task returns its stack to the `Tasks` pool (`TakeStack`/`GiveStack`); `TrimPool`
  frees stacks above `poolMax`, at most `trimStep` per scheduler step. `Kernel.AllocStack`
  returns FALSE for a size above `maxStackScan`.
- The stack registry is in the kernel: `RegisterStack`, `RegisterSignalStack`, `UnregisterStack`,
  `StackOf` (a linear search of the list), `SwitchSave`, `SwitchRestore`. `MarkLocals` scans
  sleeping stacks; an unknown current stack cancels the collection.
- A trap inside `Kernel.Try` is not fatal in the console without a `trapViewer`:
  `Kernel.HandleTrap` calls `platform.FatalError` only when `tryCount = 0`. The framework does not
  replace the trap viewer; the console launcher passes the `quit` key to `UnitRunner.Run` to get
  an exit code. `Kernel.GetTrapViewer` is not exported.
- The compiler (`Dev/Mod`) is the snapshot `f03eaab7` plus three fixes: 8-byte slots for local
  pointers and OUT-parameters on amd64, exact OUT-parameter offsets, and 8-byte lengths of a
  multidimensional open array. `DevCPE` reads `BB_COMPILE_TIME` for deterministic `.ocf` files.
- `System/Mod/Services.cp` provides the action hook; `Cons/Mod/Interp.cp` steps it between
  commands and while waiting for input (via `Console.WaitInput`).
- `System/Mod/Loop.cp` skips a periodic collection when `Kernel.AllocCount` has not changed since
  the previous one and no finalizer is pending.

### Windows x64

`WinKernel` provides `AllocateStackMem` (`VirtualAlloc` + `PAGE_NOACCESS`) and
`Get/SetTryContext`. `tools/test-win64.sh` builds `@Win`, links the console PE (`WinIntInit`, with
the subsystem patched to console) and runs `Unit_devMain` 7/7, `System_devWinTasks` 5/5,
`Dev_devOpenArr` 8/8 and `Dev_devPtrInit` 7/7 under wine.

### Limitations

- `pack-dev0-linux` is absent from the snapshot; `dev0Linux` is a binary and is not rebuilt.
  `tools/bootstrap.sh` rebuilds only the `Dev` subsystem.
- No GUI test for Windows under wine.
- The cluster index of the collector exists only in `Mod64`.
- Deferred finalizers, preemption by signal, OS threads executing Component Pascal code, and a
  load test of "1000 tasks for an hour" are not implemented.

### Documentation

Before writing or editing any `.md` file, read the documentation skill
[`.claude/skills/bb-docs/SKILL.md`](.claude/skills/bb-docs/SKILL.md): genres, order of exposition,
language rules, the "Interface" reference section (`DEFINITION`, Pre/Post), fact checking against the
code, and the checks to run before a commit.

| File | Content |
|---|---|
| `Docu/ru/Tasks.md`, `Docu/Tasks.md` | `Tasks` module |
| `docs/implementation.md` | implementation |
| `docs/building.md` | build, run, tests, rollback |
| `docs/measurements.md` | measurements |
| `docs/gc-many-stacks.md` | GC with many sleeping tasks: cause of the quadratic cost, shared candidate buffer, before/after measurements |
| `docs/kernel-plan.md` | future kernel work |
| `docs/stacks-study.md` | task stacks: where time goes, stack depth, switching and CPU counters |
| `docs/speed-main.md` | task speed in the main branch: stack providers, before/after measurements |
| `docs/monitor-object.md` | monitor object and pass in `Tasks`: signatures, rules, examples, measurements, divergences |
| `docs/adr/README.md` | decision records (Russian only): options considered and why rejected |
| `Unit/Docu/ru/Testing.md`, `Unit/Docu/Testing.md` | test rules |
| `Obx/Docu/Sys-Map.md` | Overview by Example |
