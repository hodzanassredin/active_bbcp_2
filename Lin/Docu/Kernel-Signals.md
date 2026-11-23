# Linux signal handling

Both `Lin/Mod/Kernel.cp` and `Lin/Mod64/Kernel.cp` use a dedicated signal
stack of at least 1 MiB. `InitSignalStack` registers it once during kernel
initialization. The exported `InstallSignals` only installs handlers; it can
therefore be called again after GTK initialization or from a trap viewer
without attempting to replace an active signal stack.

BlackBox handles SIGINT, SIGILL, SIGFPE, SIGBUS, SIGPIPE, SIGTERM and SIGSEGV.
SIGSEGV uses the alternative stack. SIGCHLD is ignored with SA_NOCLDWAIT,
preserving the existing child-process policy. Other signals retain their
existing disposition; native-library service signals must not be translated
into BlackBox traps.

A fault during an `IsReadable` probe returns FALSE without logging a trap.
The trap handler saves and restores `trap.top`, and does not assert while
repairing an unset boundary on the alternative stack. Recursive traps at the
same program counter still terminate the process to prevent an endless loop.
SIGBUS is reported as an invalid memory access; a user-generated SIGBUS has
no fault address, so its reported address is zero.

The i386 stack boundary remains the value supplied by the universal kernel
at initialization. The AMD64-specific update in `Start` is not copied to
i386. Likewise, native address trampolines and MAP_32BIT allocations remain
AMD64-specific. The i386 universal kernel routes diagnostics through platform
methods installed by `Setup`; it has no `Kernel.SetErr` entry point.

## Regression checks

Run from the repository root:

```sh
python3 Lin/Test/check_signals.py
```

This builds both architectures in separate directories and checks:

- alternative-stack capacity and repeated handler installation;
- handler installation from a SIGSEGV trap viewer on the active signal stack;
- SIGURG retaining its default disposition;
- invalid-address probes, SIGSEGV, SIGBUS and nested HALT traps;
- local linked objects surviving garbage collection before and after traps.

`--skip-build` reuses existing subsystem dependencies, but recompiles LinKernel
and the test modules and relinks the test executables. Expected trap reports
are allowed in `signals-run.log`; success requires the final test marker and
a zero exit code. The fixture intentionally triggers a nested trap only at
the end, because the universal kernel suppresses further viewer calls after
a viewer itself traps. It is intended for the isolated test process.

For GTK and application regressions, also run `Lin/Test/check_printing.py`
and `Lin/Test/check_linux64.py --gui`. None of these tests simulate mouse
dragging. The printing test uses GTK's virtual file printer only.
