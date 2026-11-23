# Building and testing AMD64

Run `./dev0Linux` from the repository root. `OLD/` is an archived checkout,
not an input to current builds. Platform sources now have their normal names:
for example, `Win/Mod64/Api.cp` defines WinApi. Do not pass an architecture
suffix to CompileThis.

Every new host compiler or linker process must first run:

```componentpascal
DevLibrarian.EnableDevLib('build/blackbox-win64', 'Mod64');
```

DevLibrarian searches Mod64 first and falls back to Mod for shared modules.
It writes Code and Sym to the specified output tree. Enabling DevCompiler64
alone does not select Mod64. End a session with DevLibrarian.DisableDevLib
before returning to standard source/output selection. Keep Windows AMD64,
Linux AMD64 and i386 outputs separate.

See [AGENTS.md](../../AGENTS.md) for full CompileSubs and linker commands.
The scripts below enable the librarian in every compiler process, use the
current checkout, and copy the resources needed by the framework.

## Build scripts

```sh
python3 Lin/Test/check_linux64.py --out build/blackbox-lin64
python3 Win/Test/build_blackbox64.py --out build/blackbox-win64
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64
```

The Windows builders do not run the application unless requested with --test
or --run. The Linux script runs its console heap regression after building.
Linux's normal LinLoader starts LinInit. The Linux script also links
`blackbox-console` with LinIntInit; use it for stdin commands:

```sh
printf 'CheckLinHeap64.Run\n' | ./build/blackbox-lin64/blackbox-console
```

## Checks

```sh
python3 Dev/Test/check_mod64_layout.py
python3 Win/Test/check_api64.py --coverage-only
python3 Win/Test/check_kernel64.py --out build/winkernel64
python3 Win/Test/check_subsystem64.py --out build/win-subsystem64
python3 Win/Test/build_blackbox64.py --out build/blackbox-win64 --test
python3 Win/Test/build_mdi64.py --out build/blackbox-mdi-win64 --test
python3 System/Test/check_math64.py
```

The Math script expects the Linux tree and blackbox-console built above.
Win/Test/check_heap64.py checks high-address objects, collection, Meta,
callbacks and finalizers. --large on the Windows heap or Linux build script
also allocates and touches 4.25 GiB; it is opt-in.
Win/Test/check_files64.py and check_files_long64.py cover file buffering and
large file positions. Other scripts in Win/Test check clipboard, native API
layouts, the loader and profiler.

Prerequisites: Python 3, a working host dev0Linux, Linux x86-64 runtime
libraries, and Wine (`wine64`) for Windows execution tests. Native layout
checks and helper programs additionally need x86_64-w64-mingw32-gcc.
GUI tests require a display. The Linux --gui option runs the regular GUI
integration test; it does not simulate mouse dragging.

The Linux GUI uses GTK4 (`libgtk-4.so.1`), Cairo and
PangoCairo. The i386 GUI needs matching i386 runtime libraries. Run
`python3 Lin/Test/check_gtk4.py` to build and check both architectures,
including native dialogs and external clipboard text; this test also needs
`xclip`. See [GTK4 backend](../../Lin/Docu/GTK4.md).

Automatic mouse drag tests are disabled following a system crash. Do not run
the archived peer program; verify file and text drag-and-drop manually.

## Migration verification

The restored scripts and Check fixtures were adapted to Mod64. Verified with
the current checkout: full Linux CompileSubs and console heap test; Windows
kernel recovery and high-address heap tests; full Win compilation and ABI test; both WinInit GUI
configurations; MDI GUI test; and 7482 Math checks on each platform.
The other test scripts have been updated and syntax checked, but were not
all executed as part of this migration.
