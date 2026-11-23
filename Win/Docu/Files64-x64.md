# WinFiles64 AMD64

`Win/Mod64/Files64.cp` implements the `Files64` interface on Windows AMD64.
It is a separate backend from `WinFiles`: lengths and reader/writer positions
are LONGINT, including directory listings. Native handles use SYSTEM.ADDRESS;
GetFileSizeEx and SetFilePointerEx preserve the full file size and offset.
The i386 source `Win/Mod/Files64.cp` remains unchanged.

WinInit imports WinFiles64, whose initialization installs Files64.dir. Standalone
applications must import WinFiles64 themselves. Compile Files64 and
WinFiles64 before modules that use this interface. Locators are compatible
with WinFiles.Locator and ordinary Files locators; both interfaces use the same
root directory. Changing WinFiles.SetRoot also affects subsequent Files64 lookups.

The backend uses the [WinFiles buffering and registration scheme](Files-x64.md):
four lazily allocated 2 KiB buffers per carrier, disk-backed temporary files,
streamed transactional registration, and preserved shared readers when replacing
files. There is no 2 GiB or 64 MiB backend limit. Writes check LONGINT overflow;
actual limits depend on the filesystem and available disk space. Riders can seek
within the current file and append at its end. Explicit Close is recommended.

GetModDate, NofFiles and SetErr are retained. GUI error prompts use the current
WinFiles.MessageBox hook unless a WinFiles64.MessageBox hook is explicitly set.
I/O failures from Flush/Close report through SetErr and trap; operations with a
result parameter return Files64 result codes, as in WinFiles.

## Verification

```sh
python3 Win/Test/check_files_long64.py
python3 Win/Test/build_blackbox64.py --test
```

The disk test verifies sparse files exceeding 4 GiB, writes crossing both 2 GiB
and 4 GiB, appending, EOF, directory lengths, modification dates and handle counts.
It also checks Unicode names, buffer eviction, independent riders, streamed
70 MiB writes with bounded heap growth, registration, shared snapshots,
readonly/locked files, retry/cancel and temporary-file cleanup. Host-side checks
verify the resulting bytes and sizes. Sparse fixtures do not require writing
4 GiB of test data. LONGINT arithmetic regression cases also cover the compiler
optimizations used by the position overflow checks.

The GUI test checks that WinInit installs Files64.dir in both startup paths.
These tests run under Wine; native Windows testing is still required.
