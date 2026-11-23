# Windows AMD64 profiler

Compile `WinApi`, `WinMM`, `DevProfiler` and `DevProfiler__Win` with
DevCompiler64 after `DevLibrarian.EnableDevLib(outputDirectory, 'Mod64')`.
The librarian selects `Dev/Mod64/Profiler__Win.cp`;
`Dev/Mod/Profiler__Win.cp` remains the i386 implementation.

The native timer callback uses Windows x64 argument widths. It briefly suspends
the duplicated main-thread handle, reads CONTEXT_CONTROL, records Rip and resumes
the thread. CONTEXT storage is explicitly aligned to 16 bytes. Its native size
is 1232 bytes; ContextFlags, Rsp and Rip are at offsets 48, 152 and 248.

Stopping uses TIME_KILL_SYNCHRONOUS before closing the thread handle. Timer
resolution requests are balanced, including failure paths. GetSampleCount
returns the number of successful samples since the latest StartRecording; read
it after stopping. The callback allocates no CP objects.

DevProfiler uses the module's full code range and guards procedure counters when
reference metadata has no matching procedure. This also handles modules without
procedure references.

The profiler needs the initialized framework. Run the automated regression with:

```sh
python3 Win/Test/check_profiler64.py
```

It independently checks CONTEXT layout against MinGW headers and obtains timer
samples during two start/stop cycles after WinInit. The test passed under Wine;
native Windows 7 validation remains necessary.
