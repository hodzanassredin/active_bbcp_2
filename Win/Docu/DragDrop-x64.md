# Drag and drop on Windows AMD64

WinMechanisms implements the Windows OLE drag loop for BlackBox selections
and single views. WinBackends registers each document window as a drop target
and revokes it before destruction. WinKernel initializes OLE on the runtime thread.

Dragging inside BlackBox uses the standard Containers controller path. The hook
returns the destination frame, frame coordinates and copy/move/cancel operation;
Containers performs the actual edit, including local moves and undo scripts.
Dragging to another application exports the selection rather than the whole
container. The source deletes its selection only after a successful move.
Ctrl requests copy, Shift requests move, Esc cancels. Ctrl+Shift links and OLE
object embedding are not supported. Right-button drops offer Move/Copy/Cancel.

External data uses the same active formats as the clipboard: BlackBox Data/Info,
RTF, Unicode and ANSI text, and WMF. The first usable representation is imported;
Info alone cannot create a view. Containers decide whether to accept the type
and whether the destination permits editing. File drops (OLE CF_HDROP or shell
WM_DROPFILES) open files using StdCmds.OpenDoc and the registered converters.
Directories are ignored; file drops report copy, so the source file is retained.

## ABI and lifetime

The implementation uses explicit native vtables for IDataObject, IDropSource,
IDropTarget and IEnumFORMATETC with `[ms64]` callbacks. It does not depend on
compiler-generated COM interfaces. A rooted list retains each CP object until
its native reference count reaches zero. Enumerators retain the data object.

WinOle.FORMATETC has size 32; STGMEDIUM has size 24, with its pointer-sized union
at offset 8. The union is represented by the ADDRESS field `data` to preserve
native alignment. A POINTL passed by value occupies one 64-bit argument.

WinClipboard.PrepareTransfer renders independent media without changing the
clipboard. GetData clones each requested medium, including the WMF handle; the
receiver owns the returned clone and calls ReleaseStgMedium. Incoming native
media are released with ReleaseStgMedium after conversion, including when an
importer traps. Source rendering and target edits run inside Services.Try.
WM_DROPFILES ownership ends with DragFinish in the window procedure.

## Checks

From the repository root:

```sh
python3 Win/Test/check_drag64.py --out /tmp/blackbox-drag64
# or
make -f Win/Rsrc/Makefile.blackbox64 drag-test
```

The test uses real OLE drag loops and native mouse input under Wine. Its native
peer checks format enumeration, reference counts, repeated GetData, medium
layout/ownership, selection-only export, copy/move and cancellation. Incoming
data are also sent from a separate native process to check COM marshaling. Unicode, RTF attributes, WMF, unsupported formats, read-only targets
and both file-drop paths are checked. Full mouse gestures check local and
inter-window copy/move; local Undo/Redo and the right-button drop menu are
verified. The test-only native peer is not part of the production build.

These are Wine integration checks; native Windows and individual third-party
applications still require manual verification.
