# Linux x64 build and heap regression

The shared System/Mod64/Kernel.cp collector is used by both LinKernel and
WinKernel. Linux already allocates GC clusters with unrestricted mmap; module
code and metadata remain in the low arena required by the current relocations.

## Build

Run the host dev0Linux from the project root. The build uses:

```componentpascal
DevLibrarian.EnableDevLib('build/blackbox-lin64', 'Mod64');
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket
DevLinker1.LinkElfExe64 'build/blackbox-lin64/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader$ ~
DevLibrarian.DisableDevLib
```

The executable is linked directly as ELF64. Neither bbrun64 nor Kernel64 is
required. `LinLoader` starts the GUI. The test script links a separate
`blackbox-console` executable with LinIntInit for stdin commands, and copies
subsystem Rsrc directories and Docu. See
[DevLinker1 ELF64](../../Dev/Docu/Linker1-Elf64.md).

DevLibrarian must be enabled with `Mod64` in every host compiler session. It
selects the AMD64 source when present and otherwise falls back to shared Mod.

## Reproduce checks

```sh
python3 Lin/Test/check_linux64.py --large --gui
# Check an existing subsystem build:
python3 Lin/Test/check_linux64.py --skip-build --large --gui
```

`--gui` requires an accessible display. `--large` allocates and touches 4.25 GiB
of live payload, so sufficient free memory is required. The default heap test
uses 128 MiB of byte arrays.

CheckLinHeap64 checks pointers above 4 GiB, records/arrays containing pointers,
VAR fields, virtual dispatch, type guards, GC, finalizers, Meta.New,
Meta.GetVal/ParamCallVal and address arguments through Kernel.Try. The large run
also checks 64-bit heap totals, data after a full collection, release of clusters
and subsequent allocation. An overflowing maximum-size request must fail safely.

The GUI run uses ordinary LinInit/StdConfig startup. It checks a high-address text
model, Copy/Paste, Undo/Redo and a real FormViews Find/Replace form with controls,
with explicit collections. A temporary startup fixture is removed in finally;
the normal blackbox executable does not automatically run tests.

Build and runtime logs are written into the output directory. Linux x64 console,
4.25 GiB live heap and GUI checks passed on 2026-11-11.
