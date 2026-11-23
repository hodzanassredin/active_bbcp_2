# Compiling the Win subsystem for AMD64

All modules in Win/Mod have a compilable Windows AMD64 selection. Modules with
native layouts, callbacks or pointer arithmetic use a separate `` source;
architecture-independent modules keep their common source. Original i386 sources
remain available to DevCompiler.

In the command-line host, select the destination before compiling:

```componentpascal
DevLibrarian.EnableDevLib('build/blackbox-win64', 'Mod64')
DevCompiler64.CompileThis ConsFonts ConsWindows ConsLog SqlDrivers StdTabViews
DevCompiler64.CompileSubs @Win Win
DevLibrarian.DisableDevLib
```

The common framework symbols must already exist in that destination; the usual
`Win/Test/build_blackbox64.py` build supplies them. Console initialization needs
ConsFonts, ConsWindows and ConsLog; the SQL implementations need SqlDrivers;
WinTabFrames needs StdTabViews. They are dependencies outside the Win subsystem.

## Native interfaces

WinApi includes printer/GDI structures and functions, window management,
Property Sheet records and callbacks, and the OLE support types used by bindings.
WinDlg provides native printing records alongside file dialogs. WinOle,
WinOleAut, WinOleCtl, WinOleDlg and WinRpc include the remaining declarations
with Windows x64 calling conventions and natural alignment. WinOle retains the
native STGMEDIUM.data slot consumed by the existing clipboard implementation.

WinGL, WinGLUtil, WinGLAux, WinGdip and WinCmc have native pointer types and
callback declarations. Their DLLs must be available when an application uses
these APIs; compilation does not supply external libraries such as GLAUX.

WinSql uses 64-bit handles, SQLLEN/SQLULEN lengths and row counts. WinSqlOdbc and
WinSqlOdbc3 use matching native output storage. Their framework-facing row/column
indices and blob lengths remain INTEGER; oversized blob results are checked
before conversion. The exported rowcount is LONGINT in the AMD64 variants.

WinWindows and WinTabFrames store pointers with Get/SetWindowLongPtrW, reserve
8-byte window slots, and use native-width message parameters and callback
results. WinWindows is the alternative MDI implementation; the default x64 GUI
continues to use StdWindows/WinBackends. Its clipboard uses the existing native
WinClipboard/WinMechanisms integration. WinPrinters and WinPackedFiles also have
separate AMD64 implementations. WinFiles exports the shared FullName alias and
caseSens locator field needed by packed file lookup; Windows lookup remains
case-insensitive.

## Verification

```sh
python3 Win/Test/check_subsystem64.py
python3 Win/Test/build_blackbox64.py --out build/blackbox-win64 --native-controls --test
python3 Win/Test/check_clipboard64.py
```

The subsystem test builds a fresh workspace, compiles every Win module, checks
that none were skipped, verifies C layout assertions against MinGW headers and
runs matching Component Pascal checks under Wine. Covered layouts include
DEVMODEW, DOCINFOW, PRINTDLGW, OPENFILENAMEW, VARIANT, SAFEARRAY, STATSTG,
FORMATETC, STGMEDIUM, Property Sheets, RPC_MESSAGE and the NDR stub records.
It exercises VariantInit/Clear, CoTaskMemAlloc/Realloc/Free, GDI+ startup/shutdown
and ODBC environment allocation/free.

The GUI regression covers both startup configurations with native controls.
Compilation and these checks do not constitute full runtime testing of MDI,
physical printing, RPC, OpenGL, MAPI, database drivers or COM method invocation.
Native Windows 7 x64 testing is still required.
