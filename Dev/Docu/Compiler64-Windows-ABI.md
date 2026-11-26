# Windows callbacks and unwind in DevCompiler64

Implemented and tested under Wine on 2026-11-09. Windows callbacks and unwind
metadata support the [WinKernel implementation](../../Win/Docu/Kernel-x64.md).

## Declaring callbacks

Use `[ms64]` on both the procedure definition and its procedural type:

```componentpascal
TYPE Callback = PROCEDURE [ms64] (arg: SYSTEM.ADDRESS): INTEGER;
PROCEDURE [ms64] Handler (arg: SYSTEM.ADDRESS): INTEGER;
BEGIN
	RETURN 1
END Handler;
```

Direct calls, calls through a procedural variable, and calls from Windows into
CP are supported for scalar integer-class parameters/results. Use INTEGER for
Windows BOOL/DWORD/LONG and SYSTEM.ADDRESS for pointers, handles and pointer-sized
integers. VAR parameters must occupy a single address slot. An ordinary CP
procedure pointer is not interchangeable with an `[ms64]` procedure pointer.
Pass the address of a named callback with `SYSTEM.ADR(Handler)`; for a procedural
variable use its value, not the address of its storage slot.

Floating arguments/results, by-value aggregates, tagged VAR records/open arrays,
and nested callbacks requiring a static link are rejected with error 200.
The internal CP ABI is unchanged. `[ccall]` still means SysV AMD64.
This does not make the runtime thread-safe or implement COM guarded procedures.

## Generated code

`DevCPCamd64.Ms64Entry` emits a Windows entry wrapper before the private CP body.
The exported/code-pointer address refers to the wrapper. It saves RBX, RBP, RSI,
RDI, R12–R15 and XMM6–XMM15, copies RCX/RDX/R8/R9 and subsequent stack arguments
into CP's eight-byte stack slots, and calls the CP body. The CP body removes its
arguments. The wrapper restores the Windows nonvolatile registers and returns
RAX to Windows. Floating-point register arguments are intentionally unsupported.

The wrapper's fixed frame is 232 bytes including eight saved GPRs; 168 bytes
hold ten XMM values, the saved x87 control word and MXCSR, and alignment.
The wrapper restores both floating-point controls on normal return, including
when CP code performs floating-point arithmetic internally. RBP points 224 bytes above its fixed RSP.
Its prologue has matching PUSH_NONVOL, ALLOC_LARGE, SET_FPREG and SAVE_XMM128
codes. Its epilogue uses ADD RSP, constant / POP / RET.

Ordinary CP bodies use RBP-based unwind records. These reconstruct the CP frame
chain; the outer Windows wrapper restores the full Windows nonvolatile context.
CP is not changed into the Windows ABI internally. Module initializers and PE
startup also have frame records. Epilogues use LEA RSP,[RBP+offset] so Windows can
recognize them. The stack probing helper is a leaf using R10/R11; it touches each
page without moving RSP. Actual allocation remains in the framed caller.

Handwritten `[noframe]` code and SysV callback entries do not receive Windows
unwind records. Arbitrary assembler that changes RSP/RBP cannot be described by
the automatic CP frame record and must provide its own correct frame discipline.

## OCF and PE representation

`DevCPUnwind` collects records during code generation. `DevCPE.OutCode` appends
an optional trailer **after UseBlk**, only for processor 12. Existing block
positions, fixups, module/type layouts and OSF format are unchanged.

All trailer integers are little-endian 32-bit values:

| Field | Meaning |
| --- | --- |
| magic | 0x34365755, bytes `UW64` |
| version | 1 |
| count | Number of function records |
| start, end | Code-block-relative half-open function range |
| size | Number of bytes in the following aligned UNWIND_INFO |
| bytes | Version 1 UNWIND_INFO, flags 0, padded to four bytes |

The last three rows repeat `count` times. Ranges are increasing and disjoint.
Callbacks have separate records for their wrapper and private body.
`DevLnkLoad64` accepts legacy OCF ending after UseBlk, and validates trailer
version, bounds, ordering and record size. Legacy objects have no inferred unwind
coverage; recompile all CP modules intended for stack walking or exception recovery.

`DevLnkWritePe` writes sorted RUNTIME_FUNCTION triples to read-only `.pdata`,
UNWIND_INFO to read-only `.xdata`, and PE data-directory entry 3 to the function
table. All fields are RVAs, so DIR64 relocation entries are unnecessary for these
sections. A spare DWORD after each info record accommodates older binutils readers.
The linker adds the startup frame itself. The image still must fit below 2 GiB.

## Build and validation

Run from the repository root. Host compiler modules are built with the normal
compiler. Target objects go to a separate directory.

```sh
echo "DevCompiler.CompileThis DevCPUnwind DevCPE DevCPCamd64 DevCPVamd64 DevCompiler64 DevLnkBase DevLnkLoad DevLnkLoad64 DevLnkWritePe DevLnkWriteElf DevLnkWriteElfStatic DevLinker1" | ./dev0Linux
mkdir -p /tmp/callback64-test

echo "DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('/tmp/callback64-test', 'Mod64');
DevCompiler64.CompileThis Kernel64 CheckCallback64Api CheckCallback64Native
DevLinker1.LinkPeExe64 '/tmp/callback64-test/native.exe' := Kernel64+ CheckCallback64Native\$ ~" | ./dev0Linux

WINEDEBUG=-all wine64 /tmp/callback64-test/native.exe
python3 Dev/Test/check_unwind64.py /tmp/callback64-test/native.exe
```

`native.exe` imports **only kernel32.dll**. Exit code zero verifies
InitOnceExecuteOnce callbacks, a 64-bit argument, direct and indirect CP-to-ms64
calls, and a CP-defined vectored exception callback handling RaiseException.
A C toolchain is not required for this example.

For deeper ABI validation, build the **test-only** probe DLL:

```sh
x86_64-w64-mingw32-gcc -shared -O0 -g -Wall -Wextra -Werror \
  -o /tmp/callback64-test/callback64_probe.dll Dev/Test/callback64_probe.c

echo "DevLibrarian.EnableDevLib('/tmp/callback64-test', 'Mod64');
DevCompiler64.CompileThis CheckCallback64Probe CheckCallback64
DevLinker1.LinkPeExe64 '/tmp/callback64-test/callback.exe' := Kernel64+ CheckCallback64\$ ~" | ./dev0Linux
WINEDEBUG=-all wine64 /tmp/callback64-test/callback.exe
```

The probe checks RtlVirtualUnwind at every instruction boundary in the wrapper
prologue and restore/epilogue, including the saved GPRs and each XMM register.
It calls a six-argument callback, checks values above 32 bits and floating-point control
preservation, walks live CP/body/
wrapper frames, and walks an actual Windows exception context. The CP callback
also uses a 20,000-byte local array to exercise stack probing.
`check_unwind64.py` validates function ordering, ranges, section flags and the
emitted unwind-code subset. Existing CheckPe64Main and CheckWinKernel64 tests
also pass with the new compiler. Native Windows execution still needs validation.

## Remaining WinKernel integration

WinKernel now uses a CP `[ms64]` VEH callback and direct Windows system imports.
Kernel.Try/Start recovery uses RtlCaptureContext and a restored exception context;
see the runtime documentation for tests and restrictions. Unwind metadata remains a description of stack
reconstruction, not an automatic catch handler or foreign-frame cleanup mechanism.

For dynamically loaded OCF modules, [StdLoader AMD64](../../Std/Docu/Loader.md)
parses UW64 and calls Kernel.RegisterCode. WinKernel registers a persistent
RUNTIME_FUNCTION table with RtlAddFunctionTable and removes it with
RtlDeleteFunctionTable before freeing code/table storage. Static LinkPeExe64
images are registered by Windows through the PE exception directory.
Language-specific SEH handlers, cleanup scopes, TLS and cross-thread runtime
entry remain separate work.

References: [x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention),
[x64 exception handling](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64),
[x64 prolog and epilog](https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog).
