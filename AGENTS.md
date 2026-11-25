# BlackBox Component Builder - Agent Guidelines

BlackBox Component Builder 2.0 (bbcb2) is a development framework for Component Pascal
(description in `Docu/CP-Lang.md`). Sources are UTF-8 `.cp` files; the Active Component Pascal
extension is described in `Docu/ru/Active-CP-Lang.md`.

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

The repository script `tools/build64.sh` does this and more; see below.

## Building Windows x64

```sh
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
  `.work/i386dev`. Do not use the user's display (`DISPLAY=:0`); GUI tests use
  `gtk4-broadwayd`. Do not emulate the mouse.
- Do not push. Do not touch branches `active` and `ivan-dia261123`.

### Build and Run

```sh
tools/build64.sh                  # all subsystems, console + GUI
tools/build64.sh --clean          # erase .ocf/.osf in the installation
tools/build64.sh --console-only   # console only
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
compiles `Tasks`, `LinThreads`, `LinNative`, `Async` (separately, because it needs those modules),
`Robust`, `Actor` and `Demo`. It links `blackbox-console` and, unless `--console-only` is given, `blackbox`.

`tools/env.sh:clean_host_temp` removes `odc` names with five digits from `/tmp` before starting
`dev0Linux`: the packaged host still creates such names and probes at most 200 of them.

### Tests

Tests use subsystem `Unit` (`UnitAssert`, `UnitRunner`; rules in
`Unit/Docu/ru/Testing.md`). The manifest `tools/tests.txt` has the format
`<module>|<marks>|<timeout>|<extra modules>|<options>`. The fifth field is passed to
`CompileThis` (for example `^` for the yield-point key; `tools/unit.sh --opts '^'`).
`tools/test.sh` calls `tools/unit.sh` for each module: a separate process, success only with
`failed 0`. Tests live in separate subsystems next to the one under test: `<Sub>_dev`
(`Comm_dev` next to `Comm`), file `<Sub>_dev/Mod[64]/<Theme>.cp`, module `<Sub>_dev<Theme>`
(`System_dev/Mod/Tasks.cp` — `System_devTasks`; `Dev_dev/Mod/Yield.cp` — `Dev_devYield`); the
entry point without a theme is `<Sub>_devMain`, for example `System_devMain`. `UnitRunner.Run
System` selects loaded `System_dev*`; `all` selects every module whose name contains `_dev`.

```sh
tools/test.sh                 # all modules from tools/tests.txt
tools/test.sh --list
tools/test.sh System_devMain  # selected modules
tools/test32.sh               # i386: Unit, Files, Tasks, Monitors, Active, Yield, CoTests, UnloadGuard, Cas, Kv
tools/test-win64.sh           # Windows x64 under wine
tools/test-resolver.sh        # resolver in unshare -rmn
tools/test-robust.sh          # RobustParallel on 1/4/16 processes
tools/test-mpi.sh             # MPI transport, mpiexec -n 5
tools/test-gui.sh             # GUI under gtk4-broadwayd, no display
tools/check-reject.sh         # compiler rejection cases
tools/check-examples.sh --run # doc examples equal their module files; example pages' output
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

Production builds (`build64.sh`, `build32.sh`, `test-win64.sh`) do not include `*_dev`
subsystems: they are not in `CompileSubs`. Tests are built on demand by `tools/unit.sh` and
`tools/go64.sh` (x64), `tools/test32.sh` (i386), and `tools/test-win64.sh` (wine). The key `^` is
allowed for `Dev_dev*` modules: `FrameworkModule` compares the subsystem name exactly.

### Kernel and Language

- The extension adds tasks with their own stacks, monitors, `[exclusive]`, `ACTIVE.AWAIT` and the
  yield-point key `^`. The language report is `Docu/ru/Active-CP-Lang.md`.
- `System/Mod/Monitors.cp` implements monitors; the kernel keeps only `Kernel.Lock`, `Unlock`,
  `Await` as stubs over `Kernel.MonitorHook`. The scheduler is set with
  `Kernel.SetScheduler` and is a `Kernel.Scheduler` object (`current`, `park`, `unpark`, `name`,
  `poll`).
- `System/Mod/Tasks.cp` implements tasks for both amd64 and i386; the machine part (word size,
  frame layout, `[code] Switch`) is module `Taskarch`, one file per target in `System/Mod` and
  `System/Mod64`; `Switch` is inlined at the call site. Task
  stacks come from `Kernel.AllocStack/FreeStack` (`mmap` with a guard page in `LinKernel` and
  `VirtualAlloc` with `PAGE_NOACCESS` in `WinKernel`). A finished task returns its stack to the
  `Tasks` pool (`TakeStack`/`GiveStack`); `TrimPool` frees stacks above `poolMax`, at most
  `trimStep` per scheduler step. `Kernel.AllocStack` returns FALSE for a size above `maxStackScan`.
- The stack registry is in the kernel: `RegisterStack`, `RegisterSignalStack`,
  `UnregisterStack`, `StackOf` (a linear search of the list), `SwitchSave`, `SwitchRestore`. `MarkLocals` scans sleeping stacks;
  an unknown current stack cancels the collection.
- A trap inside `Kernel.Try` is not fatal in the console without a `trapViewer`:
  `Kernel.HandleTrap` calls `platform.FatalError` only when `tryCount = 0`. The framework does not
  replace the trap viewer; the console launcher passes the `quit` key to `UnitRunner.Run` to get
  an exit code. `Kernel.GetTrapViewer` is not exported.
- The compiler (`Dev/Mod`) recognizes the pseudo-module `ACTIVE` and `ACTIVE.AWAIT`, the flag
  `[exclusive]`, the hidden import `"$$"` and the yield-point key `^`. The flag is not written to
  the symbol file. `DevCPE` reads `BB_COMPILE_TIME` for deterministic `.ocf` files.
- `System/Mod/Services.cp` provides `Post`, `Wake`, `NextDeadline` and the action hook;
  `Cons/Mod/Interp.cp` steps the action hook between commands and while waiting for input.
- `System/Mod/Loop.cp` skips a periodic collection when `Kernel.AllocCount` has not changed since
  the previous one and no finalizer is pending.

### Async Library

The `Async` subsystem is built for AMD64 only, except `AsyncStm`, which is also built for i386
(`tools/build32.sh`); the core language extension works on i386.

- `Async/Mod`: `AsyncOnce`, `AsyncChan` (`Chan`, `Buffer`, `Mailbox`), `AsyncSelect`
  (`AltSend`/`AltRecv`/`AltTimeout`, `Select`), `AsyncProto`, `AsyncIO`, `AsyncStm`
  (transactions: `Atomically`, `Retry`, `OrElse`; it relies on `Tasks.BeginAtomic`).
- `System/Mod/Poller.cp` is a readiness registry; `LinConsole.WaitInput` polls stdin and the
  registered descriptors with one `poll`.
- `Async/Mod64/Work.cp` is the worker-pool facade; `AsyncWorkLin` (pthread, result pipe,
  `Poller`) and `AsyncWorkWin` (`CreateThread`, `CRITICAL_SECTION`, `Services` action) are the
  delegates. The delegate is installed lazily by `AsyncWork.Ensure` through `Dialog.Call`; the
  base init does not start a pool.
- `Async/Mod64/Computon.cp` runs computons in separate BlackBox processes over `pipe`, `tcp` or
  the external MPI transport (`Async/Mod64/Mpi.cp`); `Robust/Mod64/Parallel.cp` is the
  `RobustParallel` facade. `ROBUST_SIZE` sets the number of local executors plus the controller.
- Worker threads and computon processes must not allocate from the shared heap. A blocking OS
  call made directly from a task still freezes the framework; move it to `AsyncWork`.

### Cas Store

`Cas/Mod` (amd64 and i386): `CasHash` (SHA-256, `Id`), `CasStore` (abstract block store, in-memory
`Mem`, references with compare-and-swap `CasRef`), `CasFiles` (directory store: temp file and rename,
hash checked on read), `CasChunk` (content-defined cutting, Buzhash), `CasTree` (Merkle tree of a
stream), `CasChange` (change sets: `Commit`, `Rebase`, `Log`). Tests — `Cas_dev`; build lists `Cas`
before `Obx` (examples `ObxCas0..2` import it).

### GUI

`LinInit` reads the external variable `BB_RUN=Модуль.Процедура` and calls the command before
`Loop.Start`. `tools/test-gui.sh` and `tools/test-demo-gui.sh` start `gtk4-broadwayd` in their
own `XDG_RUNTIME_DIR` and unset `DISPLAY`. The system `broadwayd` is from GTK3 and crashes with a
GTK4 client; use `gtk4-broadwayd` (package `libgtk-4-bin`, unpacked into `.work/gtk4-bin`). The
mouse is not emulated.

### Windows x64

`WinKernel` provides `AllocateStackMem` (`VirtualAlloc` + `PAGE_NOACCESS`), `Get/SetTryContext`
and dynamic loading of libraries of interface modules. `tools/test-win64.sh` builds `@Win`, links
the console PE (`WinIntInit`, with the subsystem patched to console) and runs `Unit_devMain`
7/7, `System_devWinTasks` 5/5, `Async_devWinWorkers` 4/4 and `Async_devStm` 20/20 under wine.

### Limitations

- `pack-dev0-linux` is absent from the snapshot; `dev0Linux` is a binary and is not rebuilt.
  `tools/bootstrap.sh` rebuilds only the `Dev` subsystem.
- MPI: a rank death is not retried (mpiexec kills the world); `Unload` over MPI is refused; no
  GUI test for Windows under wine.
- `Async/Mod64` (channels, workers, computons) is not built for i386. The cluster index of the
  collector exists only in `Mod64`.
- Deferred finalizers, preemption by signal, OS threads executing Component Pascal code, and a
  load test of "1000 tasks for an hour" are not implemented.

### Documentation

Before writing or editing any `.md` file, read the documentation skill
[`.claude/skills/bb-docs/SKILL.md`](.claude/skills/bb-docs/SKILL.md): genres, order of exposition,
language rules, the "Interface" reference section (`DEFINITION`, Pre/Post), fact checking against the
code, and the checks to run before a commit.

| File | Content |
|---|---|
| `Docu/ru/Active-CP-Lang.md`, `Docu/Active-CP-Lang.md` | language report |
| `Docu/ru/Tasks.md`, `Docu/Tasks.md` | `Tasks` module |
| `Docu/ru/Async.md`, `Docu/Async.md` | `Async` library |
| `Cas/Docu/ru/Cas.md`, `Cas/Docu/Cas.md` | `Cas` content-addressed store |
| `Actor/Docu/ru/Actors.md`, `Actor/Docu/Actors.md` | `Actor` subsystem: actors and supervision |
| `Docu/ru/Tut-Active-TOC.md`, `Docu/Tut-Active-TOC.md` | tutorial: when to use actions, tasks, monitors, transactions, channels, selection, workers, computons (`ObxBank0`–`ObxBank6`) |
| `Kv/Docu/ru/Kv.md`, `Kv/Docu/Kv.md` | `Kv` key-value store with transactions |
| `Raft/Docu/ru/Raft.md`, `Raft/Docu/Raft.md` | `Raft` replicated log: core automaton, simulation, sessions, snapshots, node actor `RaftNode`, persistent-actor journal `RaftJournal` |
| `docs/implementation.md` | implementation |
| `docs/building.md` | build, run, tests, rollback |
| `docs/measurements.md` | measurements |
| `docs/kernel-plan.md` | future kernel work |
| `docs/low-alloc-style.md` | allocation inventory per operation and no-allocation style proposal |
| `docs/adr/README.md` | decision records (Russian only): options considered and why rejected |
| `Unit/Docu/ru/Testing.md`, `Unit/Docu/Testing.md` | test rules |
| `Demo/Docu/Sys-Map.md` | examples |
| `Obx/Docu/Sys-Map.md`, `Obx/Docu/ru/Sys-Map.md` | Overview by Example, sections Tasks, Monitors and Channels; Content-Addressed Store |
