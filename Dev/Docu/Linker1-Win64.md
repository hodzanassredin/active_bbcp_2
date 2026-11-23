# Linking Windows x64 executables with DevLinker1

Implemented and tested on 2026-11-07. This is a native PE32+ linker for the OCF
format emitted by **DevCompiler64**, including dependency loading, metadata,
imports, exports, startup and relocation. The linker itself can run in the
32-bit `dev0Linux` process. It does not require the Windows SDK or a C linker.

## Command

```text
DevLinker1.LinkPeExe64 Program.exe := Kernel64+ Library Program$ ~
```

* The output name may include a quoted path. Parent directories must exist.
* `Kernel64+` selects and loads the module providing `NewRec` and `NewArr`.
  Place the marker anywhere in the list; all markers are parsed before loading.
  A kernel is not required for programs without allocation fixups.
* `Program$` selects the main module. Without `$`, the last listed root is used.
* Other names include roots. Their transitive imports are linked once, even if
  a module is repeated explicitly. Bodies run in dependency order. The main root
  is requested after the other roots; dependency order still takes precedence.
* Duplicate `$` or `+` markers are errors. `~` is optional at end of command.
* The current `Librarian.lib` determines where OCF files are found. There is no
  hardcoded `Hr/Demo/Code` search path.

`LinkPeExe`, `LinkPe` and `LinkPeDll` remain i386 commands; no implicit architecture
switch occurs. The i386 reader now diagnoses AMD64 input rather than treating it
as 32-bit data. There is no `LinkPeDll64` or dynamic-initialization variant yet.

Hr OCF has a different header, metadata layout and import-proxy scheme. It cannot
be passed to this command despite having the same processor identifier 12.
`HrPeFmt` and `HrCompiler` are not dependencies of this implementation.

## Build the tools

Run from the repository root. Use separate invocations so the second process loads
the newly compiled compiler support modules:

```sh
echo "DevCompiler.CompileThis DevCPM DevLibrarian" | ./dev0Linux
echo "DevCompiler.CompileThis DevCPCamd64 DevCPVamd64 DevCompiler64 DevLnkBase DevLnkLoad DevLnkLoad64 DevLnkWritePe DevLinker1" | ./dev0Linux
```

These commands compile the **tools for the host**. Compile the application with
`DevCompiler64`, and keep its AMD64 Code/Sym files in a separate output tree.

## Reproduce the executable test

The test sources are `CheckPe64Api`, `CheckPe64Dep`, and `CheckPe64Main`. They check
module initialization, imported variables/procedures, a virtual method inherited
from an imported record, `NEW` for a record and an array, a dense `CASE`, and
Windows API calls with both register and stack arguments.

```sh
mkdir -p /tmp/dev-pe64-test/Check/Code /tmp/dev-pe64-test/Check/Sym
mkdir -p /tmp/dev-pe64-test/System/Code /tmp/dev-pe64-test/System/Sym

echo "DevLibrarian.EnableDevLib('/tmp/dev-pe64-test', 'Mod64');
DevCompiler64.CompileThis Kernel64 CheckPe64Api CheckPe64Dep CheckPe64Main
DevLinker1.LinkPeExe64 '/tmp/dev-pe64-test/hello.exe' := Kernel64+ CheckPe64Dep CheckPe64Main$ ~" | ./dev0Linux

python3 Dev/Test/check_pe64.py /tmp/dev-pe64-test/hello.exe
WINEDEBUG=-all wine64 /tmp/dev-pe64-test/hello.exe
```

`System/Mod64/Kernel64.cp` defines the legacy standalone fixture kernel `Kernel64`. Framework builds use `System/Mod64/Kernel.cp` instead.
The fixture prints `DevLinker1 Windows x64 OK` and returns process exit code 0.
Nonzero fixture exit codes 41–46 identify a failed assertion. The PE is a GUI
subsystem image; the fixture writes to its inherited stdout handle, so run it from
a terminal/process with redirected or inherited standard output.

The Python checker independently reads PE headers, section bounds/BSS, 64-bit
import lookup/IAT entries, sorted exports, and DIR64 relocation targets. It is
specific to this integration fixture, not a general PE verifier.

Additional checks performed: repeated links in one process; duplicate roots;
automatically included imports; missing modules; duplicate main markers; i386 and
Hr OCF rejection; truncated headers; cyclic imports; successful linking after a
failure; and the existing i386 PE path. Rebasing the test image from 0x400000 to
0x14000000 using its relocation table also produced a working Wine executable.
Native Windows execution has not been tested.

## Windows API declarations

Use the explicit `[ms64]` calling convention with DevCompiler64:

```componentpascal
MODULE CheckPe64Api ["kernel32.dll"];
    IMPORT SYSTEM;
    PROCEDURE [ms64] GetStdHandle* (kind: INTEGER): SYSTEM.ADDRESS;
    PROCEDURE [ms64] WriteFile* (handle, buffer: SYSTEM.ADDRESS; size: INTEGER;
        VAR written: INTEGER; overlapped: SYSTEM.ADDRESS): INTEGER;
    PROCEDURE [ms64] ExitProcess* (code: INTEGER);
END CheckPe64Api.
```

The current implementation supports **direct and indirect calls, and CP-defined callbacks** with integer, pointer,
ADDRESS and other scalar integer-class arguments/results, and single-address VAR
parameters. The first four argument slots go to RCX/RDX/R8/R9; further arguments
follow the 32-byte shadow area. RSP is aligned to 16 bytes before CALL. Temporary
argument storage and R12 are restored afterwards. Use explicitly sized Windows
API types: an address is 64 bits, a DWORD/BOOL remains INTEGER.

Floating-point and aggregate values, tagged VAR records/arrays and nested
callbacks are **not implemented**;
the backend rejects those cases with compiler error 200. Pass buffers explicitly
by address and size. `[ccall]` retains the existing SysV AMD64 meaning; neither it
nor `[ccall16]` is a substitute for `[ms64]`. The linker cannot infer or correct a
wrong calling convention from machine code.

## Loader and fixup model

`DevLnkLoad64` reads the native Dev layout: seven 32-bit header words, the import
list, metadata/descriptor/code blocks, six fixup groups, and the use block. Module
fields include term at 48, names at 120, imports at 136 and exports at 144. Export
entries are 24 bytes, with the descriptor reference at offset 16.

Metadata and descriptors share one linker data section; descriptor references
receive the metadata-size addend. Fixups are initially stored as section-relative
addends. Only after PE layout is known does `DevLnkBase.DoFixups` resolve them:

| OCF fixup | AMD64 patch |
|---|---|
| absolute (100) | 8-byte address, with DIR64 base relocation |
| relative (101) | signed disp32 |
| copy (102) | 8-byte resolved value from a descriptor/method slot |
| table/tableend (103/104) | 8-byte code-table address; stride 8 |
| ripBased (106,107,108,110,114) | disp32 including 0/1/2/4/8 trailing immediate bytes |

The RIP-relative expression is `target + addend - (patchAddress + 4 + immLen)`.
Normal inter-module imports check fingerprints and class/visibility. The implicit
allocation chains contain no fingerprint; `NewRec`/`NewArr` are resolved by name
and procedure class in the selected kernel, which must obey the compiler ABI.
DLL procedure imports use nearby `jmp [rip+disp32]` stubs pointing at 8-byte IAT
slots. DLL variables and runtime type descriptors are unsupported; a type use
with no runtime fixups needs no DLL symbol and is accepted.

Module descriptor links/import pointers are 8 bytes; refcounts stay 32 bits.
Global storage for each module is padded to 16-byte alignment. Cyclic module
imports, wrong architecture/header format, invalid fixup bounds/types and address
overflows produce diagnostics and do not register an output file.

## PE layout, startup and current limits

The shared PE writer selects AMD64/PE32+, a 240-byte optional header, 64-bit import
thunks, RIP-relative import stubs and DIR64 relocations for this command. Export
names are sorted as PE requires. BSS has VirtualSize but no file data. File and
section alignment remain 512 and 4096 bytes. The default subsystem is Windows GUI.

Startup aligns the stack, reserves shadow space, calls module bodies in dependency
order, runs terminators in reverse order, and calls `kernel32!ExitProcess(0)`.
It provides the module list in RBX before the selected kernel/main body, following
the linker startup convention. A runtime that requires a different initialization
contract must be adapted; no Linux argc/argv bootInfo or C runtime is synthesized.

This first implementation deliberately keeps image addresses below **2 GiB**:
Section.addr and Symbol.addr remain INTEGER, while pointer writes are eight bytes
and range calculations use LONGINT. Default ImageBase is 0x400000. PE64 sets IMAGE_FILE_LARGE_ADDRESS_AWARE so the runtime heap can use addresses
above 4 GiB. This is independent of keeping the executable image itself below
2 GiB. High-entropy VA and ASLR are not advertised. DIR64 supports rebasing
within the supported range; arbitrary high image bases are not supported.

Windows scalar callbacks and PE64 unwind metadata are now supported; see
[Compiler64 Windows ABI](Compiler64-Windows-ABI.md). TLS startup, language-specific
SEH handlers and a full Windows x64 BlackBox runtime remain separate work. Existing Linux-specific modules cannot simply be
linked as a Windows program. The included allocation test uses the existing
minimal static-heap `Kernel64`; it is not a port of the complete GC, loader, UI or
module-discovery runtime. This work enables standalone programs within these
limits; a complete Windows x64 framework remains separate work.

## Main Windows platform kernel

A separately tested [WinKernel x64 implementation](../../Win/Docu/Kernel-x64.md) now
provides the main Kernel.Platform services, GC integration and scoped trap recovery
with CP-defined Windows callbacks and context recovery.
See its build instructions and remaining dynamic-loader/UI limitations. The minimal Kernel64 fixture above remains useful as a small linker test.

## Resource files

Append resource IDs/files after the modules, for example:

```componentpascal
DevLinker1.LinkPeExe64 'program.exe' := Kernel+ WinKernel Program$ 1 BlackBox.res 1 Applogo.ico ~
```

See [PE resources](Linker1-Resources.md) for formats, languages, search paths and tests.
