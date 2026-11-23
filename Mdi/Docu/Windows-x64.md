# BlackBox MDI on Windows AMD64

Build from the repository root:

```sh
python3 Win/Test/build_mdi64.py
```

The application is `build/blackbox-mdi-win64/blackbox.exe`. Keep the subsystem
Code and Rsrc directories beside the executable. On Windows, launch this exe
normally; under Wine:

```sh
wine build/blackbox-mdi-win64/blackbox.exe
```

The bootstrap contains Kernel, WinKernel, Files, WinFiles, StdLoader and
MdiLoader. MdiLoader dynamically loads MdiInit. MdiInit installs WinWindows and
MdiMenus, creates the main frame and MDICLIENT, applies MdiConfig and runs the
MDI message loop. WinInit and WinBackends are not loaded. The build shares the
common AMD64 framework compilation with build_blackbox64.py and then links the
MDI bootstrap. The normal WinInit build has its own output directory.

## Native sources

Mdi/Mod contains separate `Init`, `Loader`, `Menus`,
`ShowHook` and `Config` sources. The original i386 implementations are
unchanged. Menus and callbacks use 64-bit handles, WPARAM, LPARAM and LRESULT;
window objects are stored in native-width window slots. MdiMenus uses native
common controls without the i386 CTL3D shim. The loop processes native MDI
accelerators, framework actions, idle updates and cooperative Ctrl+Break.

WinWindows provides the MDI windows, while WinCFrames provides native
form controls. The current native clipboard uses WinClipboard/WinMechanisms;
it does not create the old hidden clipboard viewer window. ThisWindow therefore
checks whether such a viewer exists before accessing it. Config registers the
usual document and text converters and native raster support.

The x64 profile retains its existing restriction on embedded OLE objects.
MdiInit does not load OleServer, and MdiConfig does not register OleClient/OleServer
object converters. Native clipboard and file drops use the existing x64 services.

## Verification

```sh
python3 Win/Test/build_mdi64.py --out /tmp/blackbox-mdi64 --test
```

The test starts the MdiLoader executable, loads MdiInit and checks that WinBackends
is absent. It verifies visible MDI windows and menus; opening a command-line
filename containing spaces and Cyrillic; editing Cyrillic text, Undo and Redo;
saving/reopening a document; native clipboard roundtrip; tiling child windows;
Find/Replace form restoration; native button/edit callbacks and pointer slots;
and clean shutdown through the main window. PE layout and unwind metadata are
also checked. No mouse-driven drag-and-drop test is run.

The integration test passes under Wine. Native Windows 7 x64 execution still
needs verification. For a release build, run the build command without `--test`;
test builds close automatically after verification.

## Printing

WinDialog installs the StdDialog.PrintHook and uses WinPrinters as the active
printer directory. Print Setup and Print use the native PrintDlgW dialog,
preserving printer DEVMODE/DEVNAMES handles, page range, selection and copies.
Cancelled dialogs return without printing. WinPrinters provides the GDI job/page
lifecycle and the framework progress/cancel dialog at Win/Rsrc/Printing.odc;
the build now copies Win/Rsrc alongside the other framework resources.
PRINTDLGW and DOCINFOW are initialized fully before calling Windows.

With a virtual printer named PDF selected as the default in Wine:

```sh
python3 Win/Test/build_mdi64.py --out /tmp/mdi-print64 --test-printing
```

This additionally accepts/cancels the native setup and print dialogs, checks
page dimensions and copies, completes a native GDI page into printing-test.ps,
and exercises an aborted WinPrinters job. The host verifies the generated
PostScript page. The test requires the virtual PDF driver and never submits a
completed job to a physical printer. Physical printing on Windows requires an
installed printer driver and still needs verification there.

## Color selection

MdiConfig.Setup installs StdColorDialog.Init for Dialog.GetCol, used by
framework-drawn color controls. WinDialog implements Dialog.GetColor through
ChooseColorW for menu commands and native controls. CHOOSECOLORW has the
Windows x64 layout (72 bytes), with persistent storage for 16 custom colors.
Cancel preserves the input color; Ports.defaultColor starts with the system
text color. The MDI --test path checks OK, Cancel, the default color and
opening the framework color dialog without mouse simulation.

The i386 MdiConfig also installs StdColorDialog. Check the x32 path with
`python3 Win/Test/check_color32.py`; it uses DevCompiler and Mod, writes PE32
artifacts to a separate directory, and requires 32-bit Wine support.
