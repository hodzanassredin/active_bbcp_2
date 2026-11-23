# DevLinker1 Linux ELF64

`DevLinker1.LinkElfExe64` links Dev AMD64 OCF v2 modules into a native Linux
x86-64 executable. It uses the normal `Kernel` and `LinKernel`; neither bbrun64
nor the intermediate allocation kernel Kernel64 is required at runtime.

## Commands

Run dev0Linux from the repository root. With an existing compiled Linux x64 tree:

```componentpascal
DevLibrarian.EnableDevLib('build/blackbox-lin64', 'Mod64');
DevCompiler64.CompileThis Kernel LinKernel Files LinFiles StdLoader LinLoader
DevLinker1.LinkElfExe64 'build/blackbox-lin64/blackbox' := Kernel+ LinKernel Files LinFiles StdLoader LinLoader$ ~
DevLibrarian.DisableDevLib
```

`Kernel+` selects the runtime/allocation kernel. `$` selects the final module body;
without `$`, the final named root is the main module. Ordinary roots and all their
imports are linked recursively. Initialization follows dependency order and
termination reverses that order. Absolute and relative output paths are accepted.
Duplicate kernel/main markers, malformed commands, missing imports and non-AMD64
object files are rejected. ELF64 requires the Kernel+ marker and its exported
BootInfo variable.

Start the result normally for GUI. For stdin commands, link a separate console target with LinIntInit:

```sh
./build/blackbox-lin64/blackbox
# In a dev0Linux session with DevLibrarian enabled:
# DevLinker1.LinkElfExe64 'build/blackbox-lin64/blackbox-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit$ ~
printf 'CheckLinHeap64.Run\n' | ./build/blackbox-lin64/blackbox-console
```

The Code/Sym/resources trees remain necessary for modules loaded by StdLoader.
This is ordinary framework module loading, not a boot-time scan of all OCF files.
A fully linked standalone program needs only its native shared libraries.

`python3 Lin/Test/check_linux64.py --gui` compiles the subsystem tree, links the
ELF bootstrap and tests heap and GUI. `--skip-build --gui` checks an existing
compiled tree. Resource directories are copied by the full-build path. The full CompileSubs pass requires the platform-specific DevHeapSpy source.

## Implementation

- `DevLnkLoad64` reads OCF v2 and constructs module descriptors, imports, exports
  and relocations. Its private string type has a native open-array descriptor
  when the linker itself is compiled for AMD64.
- `DevLnkBase.GenInitElf64` emits the Linux/SysV process entry and calls
  `__libc_start_main`. This establishes libc state, environment and TLS correctly.
  Its CP startup frame contains BootInfo with the module list, argv and argc;
  Kernel receives that pointer before its body runs. LinKernel installs the
  platform hooks and normal GC before application initialization.
- `DevLnkWriteElf64` writes ELF64 ET_EXEC/EM_X86_64, program and section headers,
  PT_INTERP, DT_NEEDED libraries, dynamic symbols, SysV hash, RELA GLOB_DAT
  relocations and eager GOT-backed RIP-relative import stubs.
- Code is RX, data/metadata/BSS are RW, and GNU_STACK is non-executable.
  The system interpreter is `/lib64/ld-linux-x86-64.so.2`.
- LinLoader starts LinInit. The test script links LinIntInit as a separate console executable.
  StdLoader supplies subsequent OCF loading. Modules are located using the usual
  framework file/librarian rules.

The current format uses a fixed low image base 0x400000 and keeps code/metadata
below 2 GiB for existing disp32 relocations. Heap pointers remain full-width and
the normal large-address-space GC is used. PIE/ASLR for the executable image,
ELF shared-library output and non-Linux operating systems are outside this
command. There is no DWARF generation; BlackBox retains its OCF trap metadata.
The unsuffixed ELF commands still generate i386 images. PE64 remains supported.

## Verification

```sh
python3 Dev/Test/check_elf64.py
python3 Lin/Test/check_linux64.py --skip-build --gui
```

The standalone test compiles into an isolated tree and runs from /tmp without
OCF files there. It checks ELF headers/segments, imports and relocations with
readelf, executable permissions, argc/argv, environment, dependency initialization,
heap/GC, recovery from an intentional HALT(77), all Math checks and reverse-order termination. It asserts that Kernel64
is not loaded. The GUI test uses its own window and does not synthesize mouse
input. Native x64 self-linking and execution of the resulting ELF were also
verified, along with a Windows PE64 regression run.
