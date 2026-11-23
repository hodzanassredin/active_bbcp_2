# WinKernel for AMD64

Updated 2026-11-11. `Win/Mod64/Kernel.cp` declares **WinKernel** and implements
`Kernel.Platform` from `System/Mod64/Kernel.cp`. It uses the main Kernel with GC,
not the minimal static-heap Kernel64.

Platform services and trap recovery are implemented in `Win/Mod64/Kernel.cp`.

## Modules and build

* `WinKernel`: Kernel.Platform adapter, platform services, exception handling and
  Kernel.Start/Try recovery boundaries.
* `WinApi`: unified `[ms64]` runtime, GUI and clipboard bindings.
* `WinKernelNtdll`, `WinOle`: platform imports and clipboard medium types.
  The `` sources declare modules `WinApi` and `WinOle`; full i386 COM
  interfaces are not yet implemented.

Use the DevCompiler64/DevLinker1 versions supporting
[Windows callbacks and unwind](../../Dev/Docu/Compiler64-Windows-ABI.md).
Run from the repository root and keep target Code/Sym separate from host files.

```sh
make -f Win/Rsrc/Makefile.kernel64 OUT=/tmp/winkernel-native-test
make -f Win/Rsrc/Makefile.kernel64 OUT=/tmp/winkernel-native-test test
```

`all` compiles three fixtures and validates their imports and unwind tables;
`test` also runs them under Wine. `Win/Test/check_kernel64.py` implements the
checks and accepts `--run-only`, `--build-only` and `--wine`. Compiler diagnostics
are checked explicitly because dev0Linux can return zero after compilation errors.

Equivalent direct compilation for the main integration fixture:

```sh
mkdir -p /tmp/winkernel-native-test

echo "DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('/tmp/winkernel-native-test', 'Mod64');
DevCompiler64.CompileThis Kernel WinApi WinOle WinKernelNtdll WinKernel CheckWinKernel64
DevLinker1.LinkPeExe64 '/tmp/winkernel-native-test/test.exe' := Kernel+ WinKernel CheckWinKernel64\$ ~" | ./dev0Linux

WINEDEBUG=-all wine64 /tmp/winkernel-native-test/test.exe
```

The backslash before `$` is shell quoting. Link names are `Kernel` and `WinKernel`,
whereas compilation names explicitly select their `` sources.

## Memory and platform services

Allocation metadata uses HeapAlloc/GetProcessHeap, so it works before the CP heap
is initialized and does not recurse into NEW. Code and module data remain below 2 GiB in the 0x10000000–0x70000000 range.
GC clusters are allocated above 4 GiB and support a multi-gigabyte live heap;
see [High-address heap](Heap-x64.md) for layout, size contracts and tests.
Each reservation has a page bitmap. Module memory can be freed in separate pieces;
pages are decommitted individually and the reservation is released after its last
live page. Invalid/duplicate releases are rejected. Multi-cluster allocation is
used; gradual commit is disabled.

Metadata becomes read-only; code becomes execute/read with an instruction-cache
flush. VirtualQuery implements readable-range checks. ThisDllObj retains its
INTEGER result contract by allocating cached low-address `jmp [rip+0]` stubs to
full 64-bit Windows function addresses. DLL variables are unsupported; loaded DLLs
and stubs are retained for process lifetime.

Other services use GetTickCount64, GetLocalTime, UTF-16→UTF-8 conversion and
WriteFile on stderr. OleInitialize/OleUninitialize run on the runtime owner thread.
StackBase comes from the x64 TEB at GS:[8], via a small inline instruction sequence.

## Exception recovery

`WinKernel.Catch` is a CP-defined `[ms64]` vectored exception callback.
The compiler supplies the Windows register-preserving wrapper and unwind records.
Only the runtime owner thread with an active WkRun boundary handles traps.

WkRun creates a stack-local frame linked to the previous active boundary, aligns
an embedded 1232-byte Windows AMD64 CONTEXT to 16 bytes, and calls
RtlCaptureContext. It then invokes the ordinary CP handler with three ADDRESS
arguments. WkRun itself has a `[ms64]` entry to establish a Windows-aligned boundary;
an inline AND RSP,-16 also aligns the internal call site after CP local allocation.
RBP remains unchanged, so locals and unwind information remain valid.

For a recognized trap, Catch records PC/SP/FP/value and calls Kernel.HandleTrap
while the faulting stack still exists. After the report and trap viewer return,
it marks the current frame resumed and copies its saved CONTEXT into the exception
context. Returning EXCEPTION_CONTINUE_EXECUTION asks Windows to resume at the
capture point. WkRun then skips the handler, restores the previous boundary and
returns 1. Kernel.Try returns; Kernel.Start starts its command again. Normal
completion returns 0. This context transfer does not perform foreign SEH/C++ cleanup.

Recognized errors include CP HALT/ASSERT encodings, bad reads/writes/calls, integer
division errors and selected FPU exceptions. Faults must originate in main-image
code or protected CP allocations; bad indirect-call targets are recognized through
the CP return address. Foreign DLL first-chance exceptions, unknown exceptions,
noncontinuable exceptions and stack exhaustion are left to Windows. A second
fault in the reporter exits with code 127. Installation/removal is idempotent.

The code uses the standard Windows AMD64 layouts: CONTEXT RSP=152, RBP=160,
RIP=248; EXCEPTION_RECORD information starts at 32; MEMORY_BASIC_INFORMATION is
48 bytes. Context flags from RtlCaptureContext determine the restored state.

## Validation and remaining limits

Tested under Wine:

| Fixture | Expected result |
| --- | --- |
| CheckWinKernel64 | Three deliberate trap reports, `WinKernel x64 OK`, exit 0 |
| CheckWinKernel64Recovery | Start restart and normal return; 24 trap recoveries, nested boundaries, local GC roots, 20 KB frames, actual calls through cached DLL stubs, time and partial-page frees; exit 0 |
| CheckWinKernel64Recursive | Trap viewer itself faults; exit 127 |

Every executable is checked for exactly the three system DLL imports and valid
sorted `.pdata`/`.xdata`. Native Windows execution remains unverified.

The runtime remains single-threaded. This is not general SEH cleanup across
arbitrary native frames. Dynamic OCF loading and unwind registration are supported
by [StdLoader AMD64](../../Std/Docu/Loader-x64.md) and [WinFiles AMD64](Files-x64.md) with shared snapshots, disk writes and
buffered temporary files with lazy disk backing. The [standard BlackBox workspace](BlackBox-x64.md)
now starts through dynamically loaded WinInit. COM guarded procedures, Ctrl+Break,
TLS and the remaining WinApi/WinOle integration are separate from this runtime.
The exception handler stays installed through Kernel.Quit finalization and is removed by WkExit.

References: [RtlCaptureContext](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlcapturecontext),
[vectored exception handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling).

## Windows 7 startup and the large-address flag

DevLinker1 must set IMAGE_FILE_LARGE_ADDRESS_AWARE (COFF Characteristics bit 5)
for PE64. Keeping image/code metadata below 2 GiB does not remove this requirement:
WinKernel starts GC clusters at 4 GiB. Windows limits a process without this flag
to the low 2 GiB, while Wine may still allow the high allocations.

A Windows 7 x64 startup dump identified this omission. The executable text matched
the local build. WinFiles.Init called Kernel.NewRec for its Directory, received
NIL, then passed address zero as the GetModuleFileNameW output buffer. The native
copy routine in ntdll+0x4e78d faulted while writing to address zero. No GUI module
had loaded yet. The PE writer now sets bit 5 (Characteristics 0x226 instead of
0x206), and the PE-layout regression rejects PE64 images without it.
WinFiles also checks the allocation before invoking the API and reports failure
through Kernel.FatalError rather than passing a null buffer to Windows.

The corrected build passes the high-address heap, resource and GUI tests under
Wine; confirmation on the affected Windows 7 machine remains a separate check.

## Startup trace without a working CP heap

Build with `python3 Win/Test/build_blackbox64.py --out build/win64-trace --trace`.
The linker roots `CheckWinStartup64` before WinKernel; it enables WinTrace using
only static/stack buffers and Windows API calls. The log is created beside the
executable as `blackbox-startup.log`, and each line is flushed immediately.
Ordinary builds include the small helper but never open a log unless enabled.

The trace records WinKernel/OLE/platform initialization, native allocation
requests/results/errors and attempt counts, the first WinFiles Directory NEW,
WinInit loading failures and the process exit code. It preserves GetLastError
around its own file writes. Allocation retries are summarized to avoid excessive
I/O during the low-address code allocator's search.

`build/blackbox-win64/blackbox-win7-diagnostic.zip` contains a diagnostic executable
and `run-diagnostic.cmd`. Extract beside an existing BlackBox Code/resource tree.
The launcher also saves `blackbox-console.log` and `blackbox-exit.log`. These logs
are useful when Kernel.FatalError terminates normally and WER creates no dump.

### Native memory-query buffer alignment

The next Windows 7 trace showed successful heap allocation at 0x100000000 and
WinFiles initialization, followed by loader result 6 for Services. The second
low-address allocation encountered the first allocation (VirtualAlloc error 487),
then VirtualQuery failed with error 998 (ERROR_NOACCESS). This prevented the
allocator from advancing to the next available region.

CP local offsets do not guarantee native alignment when the frame base itself
is only four-byte aligned. QueryMemory now uses an oversized stack byte buffer,
rounds its actual address up to 16 bytes, calls VirtualQuery there and copies the
48-byte result to the CP record. AllocatePages and WkReadable both use it. The
kernel regression includes consecutive low-address allocations, requiring the
occupied-address query path. The v2 startup trace identifies the aligned build
and reports the buffer address if a memory query still fails.
