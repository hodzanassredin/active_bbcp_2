# BlackBox Windows x64

The Windows AMD64 build starts through the same WinInit.Init procedure as i386.
It uses the standard StdConfig workspace: menus, tiled document tracks, Log,
status bar and the standard BlackBox controls. OLE drag and drop is available; OLE object embedding remains excluded.

The GC heap uses addresses above 4 GiB and supports more than 4 GiB of live
data. See [High-address heap](Heap-x64.md) for allocation limits and checks.

## Build and run

Run from the repository root on Linux:

```sh
python3 Win/Test/build_blackbox64.py
WINEDEBUG=-all wine64 build/blackbox-win64/blackbox.exe
```

Equivalent make targets:

```sh
make -f Win/Rsrc/Makefile.blackbox64
make -f Win/Rsrc/Makefile.blackbox64 run
make -f Win/Rsrc/Makefile.blackbox64 test
```

The script accepts `--out DIRECTORY`, `--wine PROGRAM`, `--run` and `--test`.
It compiles with DevCompiler64 through dev0Linux and links with
DevLinker1.LinkPeExe64. OCF/OSF output is isolated from the host compiler. The
System, Std, Text and Form Rsrc trees are copied alongside the generated Code trees;
the standard workspace needs these menus, strings and dialog resources.

On Windows, copy blackbox.exe and the generated System, Std, Text, Form and Win Code
and Rsrc directories, preserving their relative paths. Sym directories are only
needed for further compilation. Runtime paths are relative to the executable,
not the working directory. Native Windows execution remains unverified; the
integration tests pass under Wine. This build includes the framework and its
standard UI components, not the complete Dev subsystem or every optional module.

## WinInit.Init

Its body is identical to Win/Mod/Init.cp, including the original sequence:

1. Install WinClipboard and WinShowHook.
2. Invoke WinGui.InstallCtrlBreakInterrupt.
3. Load DevDebug, falling back to StdDebug, and install guarded window closing.
4. Find the exported variable appConfigProcedure via Meta, defaulting to
   StdConfig.Setup.
5. Invoke WinConfig.SetupBefore, the selected configuration, and
   WinConfig.SetupAfter.
6. Create the idle timer using Dialog.idlePeriod, request the configured exit
   policy, and call Loop.Start(WinStep, ValidateWindows, ExitCondition).

Init has no demo document, hard-coded idle period or test-specific branch.
The build script checks its body against i386 after removing comments and
whitespace. Platform adaptations are outside Init: `[code]` FPU helpers, the
pointer-sized DispatchMessageW result and WM_QUIT exit value.

WinRegistry installs StdRegistry preferences and the metric-system setting.
WinDates supplies local/UTC time, timezone bias and date/time formatting.
StdStdCFrames supplies the standard controls. WinConfig.SetupBefore installs the
PNG/GDI raster backend; SetupAfter opens a document supplied on the command line.
WinEnv implements Windows argument/environment lookup. WinShowHook sends status
messages to the standard status bar. WinDialog installs host-locator hooks,
confirmation dialogs and native Open/Save dialogs.

The bootstrap PE contains Kernel, WinKernel, Files, WinFiles, StdLoader and
WinLoader. WinLoader calls Kernel.ThisMod("WinInit"); WinInit and its GUI
components remain OCF files loaded by StdLoader. The explicit `` sources
still provide ordinary module names such as WinInit and WinPorts.

WinKernel keeps its exception handler installed through module termination and
finalization. WkExit removes it immediately before ExitProcess.

## Platform scope

Handles, WPARAM, LPARAM and LRESULT are pointer-sized. Windows callbacks use
`[ms64]`; native records use the required alignment. Backend pointers use
Get/SetWindowLongPtrW and eight window-extra bytes. WinApi combines the
runtime and GUI bindings; the separate WinGuiApi module has been removed.

WinFiles supports disk snapshots, exclusive updates, transactional registration,
file/directory enumeration, rename/delete and buffered temporary files used by
TextModels. The cache holds up to four 2 KiB blocks per carrier; the INTEGER
length/position limit is 2 GiB minus one byte. See [WinFiles AMD64](Files-x64.md)
for persistence, locking, error codes and tests. Native Save/Save As/Save Copy As
and saving on dirty close are enabled. A known repeated-dialog crash under Wine
remains unresolved; see [GUI save investigation](Gui-save-x64-todo.md).

WinClipboard exchanges the active i386 formats with other Windows applications:
Unicode/ANSI text, RTF, metafiles and BlackBox Data/Info. Internal deep copies are
retained. See [Windows clipboard](Clipboard-x64.md) for formats, ownership and
tests. Drag and drop works inside BlackBox and with native Windows data objects;
file drops open documents. See [Drag and drop](DragDrop-x64.md). OLE embedding
remains unavailable. Rendering uses GDI; GDI+ cannot be enabled in this profile.
Printing is reported unavailable. Native font/color selection is not implemented;
the standard BlackBox controls and color selector are available.

Dialog.OpenExternal opens documents and URIs through Windows Shell associations;
Dialog.RunExternal starts programs with Unicode command lines. See
[External applications](External-x64.md) for behavior and tests.

Ctrl+Break is cooperative: after installation, WinStep recognizes Ctrl+Break key
messages on the runtime thread and reports trap 200. It cannot interrupt a CPU
loop that never returns to message processing. The i386 watcher thread that
suspends a running CP thread has not been ported.

## Integration tests

```sh
python3 Win/Test/build_blackbox64.py --test
```

Two executables exercise the real startup sequence:

- blackbox-default.exe exports no appConfigProcedure. A Services action verifies
  the workspace after the default StdConfig.Setup has completed. A document
  argument also verifies opening System/Rsrc/About.odc in WinConfig.SetupAfter.
- blackbox.exe links CheckWinGuiConfig64, which exports appConfigProcedure =
  "CheckWinFramework64.Setup". The callback runs StdConfig.Setup and schedules
  verification, testing Meta's variable lookup and custom configuration dispatch.

Neither path changes WinInit.Init. Test components are excluded from normal
builds, and BLACKBOX_GUI_TEST is no longer used.

Tests verify resource-based menus/workspace construction, the StdDebug fallback,
configuration dependencies, native record layouts, PNG backend registration,
file/directory enumeration, idempotent filename extensions and document arguments,
the standard Find/Replace form with a controller and non-alien controls,
date hooks, font callbacks, Unicode
insertion, Undo/Redo, internal clipboard copies, resizing and normal finalization
with no traps. Both bootstrap unwind tables are validated,
and execution starts from a different working directory.

Expected output contains `BlackBox x64 framework OK` for each executable and
`all framework checks passed`, with exit code zero. See also
[StdLoader AMD64](../../Std/Docu/Loader-x64.md) and [WinKernel AMD64](Kernel-x64.md).

## Form resources

The standard Find/Replace command opens Text/Rsrc/Cmds.odc. Its serialized root
is FormViews.StdView, with FormModels.StdModel, FormControllers.StdController and
Controls descendants. The build includes FormModels, FormViews, FormControllers
and FormCmds and copies Form/Rsrc alongside the other resource trees.

A missing Form/Code tree causes Stores to preserve these objects as alien views.
Views.Alien draws a grey rectangle with crossed diagonals; StdApi.ThisMask reports
`container expected` because this placeholder is not a Containers.View. The
resource file can be present and its window can open even when the required code
modules are missing. Restart the rebuilt application and reopen the dialog after
installing the generated Form directory.

## Native control frames

The build also includes `WinCtl` and the AMD64 implementation of `WinCFrames`.
`StdStdCFrames` remains the default backend. Execute `WinCFrames.Install` before
opening a new form to select native Windows controls. Existing forms retain their
current frames; reopen them after switching the backend.

`WinCFrames` uses Windows x64 callbacks, pointer-sized window data and tree item
handles. Numeric message results are narrowed explicitly; handles stay native.
The API declarations include native draw/measure notifications and scroll data.

Run the integration test without synthesizing mouse input:

```sh
python3 Win/Test/build_blackbox64.py --out /tmp/blackbox-cframes64 --native-controls --test
```

Both bootstrap variants open the Find/Replace form using native control frames.
The test checks button notifications, Unicode edit notifications, pointer-sized
window data and absence of traps. Structure sizes are checked during compilation.
Other control types still require interactive visual testing on Windows.

The port also exposed an AMD64 code generation error: signed INTEGER-to-LONGINT
extension could leave a stale RAX load-cache entry and corrupt a later field
access in the same argument list. MOVSXD now goes through `GenExtMove`, which
invalidates that cache consistently. Opening the native Find/Replace fields
exercises this regression.
