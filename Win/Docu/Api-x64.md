# WinApi for Windows AMD64

Win/Mod64/Api.cp provides every exported function declared in Win/Mod/Api.cp:
2248 baseline functions, plus 11 existing AMD64 helpers (2259 total). DLL and entry
names are preserved. This expansion added the 1969 previously missing baseline
functions and their constants, records and callback types. Api.cp is unchanged.

All foreign procedures and callbacks use the Windows x64 `ms64` convention.
Existing AMD64 declarations used by the framework retain their signatures.
SIZE_T/SSIZE_T, pointer-sized integers, handles and user callback parameters use
SYSTEM.ADDRESS; DWORD, LONG, BOOL, flags and IDs remain 32-bit. ReadProcessMemory,
WriteProcessMemory, heap/local memory operations and affinity masks use native
64-bit storage where required by the Windows headers. Native-sized aliases such
as SIZE_T, ULONG_PTR, DWORD_PTR and LONG_PTR are exported for callers.

Records use natural Windows x64 alignment unless their file format requires
explicit packing. LUID is a two-DWORD record with 4-byte alignment, rather than an
8-byte-aligned integer; this matters for TOKEN_PRIVILEGES. CONTEXT addresses must
still be explicitly aligned to 16 bytes by callers. MENUITEMINFO includes the
bitmap field, and NOTIFYICONDATA includes the extended Windows 7 fields.

PE32+ defaults use IMAGE_OPTIONAL_HEADER64 and IMAGE_NT_HEADERS64; explicit
IMAGE_OPTIONAL_HEADER32 and IMAGE_NT_HEADERS32 layouts are also available.
IMAGE_IMPORT_DESCRIPTOR and IMAGE_EXPORT_DIRECTORY contain 32-bit RVAs even in
PE32+. TLS virtual addresses are 64-bit; AMD64 runtime function entries contain
three 32-bit fields. IMAGE_LOAD_CONFIG_DIRECTORY uses the Windows 7 x64 layout.

These bindings cover the APIs exposed by Api.cp, not every function in the
Windows SDK. Old API names and ANSI aliases are retained. Runtime availability
still depends on the Windows version and the supplying DLL. An exported
prototype does not implement a missing OS function. Record-by-value calls are
subject to the AMD64 compiler's existing support; helpers such as
WindowFromPoint64/ChildWindowFromPoint64 are retained for packed POINT arguments.

## Verification

```sh
python3 Win/Test/check_api64.py --coverage-only
python3 Win/Test/check_api64.py --out /tmp/win-api64
python3 Win/Test/check_subsystem64.py --out /tmp/win-subsystem64
python3 Win/Test/build_mdi64.py --out /tmp/win-mdi64 --test-printing
python3 Win/Test/build_blackbox64.py --out /tmp/win-gui64 --native-controls --test
```

The coverage check strips nested comments, verifies all baseline function names
and DLL/entry bindings, and checks the calling convention. The ABI test compares
32 record sizes and key field offsets with independent MinGW C assertions. Native
calls cover heap/local memory, process memory reads/writes with 64-bit byte
counts, VirtualQueryEx and an EnumWindows callback receiving a value above 4 GiB.
It also verifies PE32/PE32+, security, shell, printer and message record layouts.
These checks, the complete Win compilation, the ordinary GUI and MDI/printing
regressions pass under Wine. The test suite does not invoke all 2259 functions.
