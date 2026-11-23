# Windows AMD64 clipboard

WinClipboard exchanges the active formats registered by the i386
WinConfig.SetupAfter, using Win32 clipboard APIs rather than CP COM interfaces.
The internal deep-copy path for BlackBox views is retained.

| Format | Import | Export |
| --- | --- | --- |
| BlackBox Data | Serialized view and model | Serialized view and model |
| BlackBox Info | Type and geometry used to select a converter | Type, geometry, singleton flag |
| Rich Text Format | Existing WinTextConv RTF parser | Existing WinTextConv RTF writer |
| CF_UNICODETEXT | UTF-16 text | UTF-16 text, Windows line endings |
| CF_TEXT | Existing WinTextConv text conversion | Existing WinTextConv text conversion |
| CF_METAFILEPICT | Existing WinPictures view/WMF renderer | Original WMF or rendered view |

The i386 WinConfig registrations for BITMAP and embedded OLE objects are
commented out; they are not added to this default profile. OLE object embedding
remains unavailable; [drag and drop](DragDrop-x64.md) reuses these converters. Native applications may also expose
formats synthesized by Windows.

## Lifetime and ownership

Copy renders formats eagerly, then uses a private hidden STATIC window as the
clipboard owner. It opens and empties the clipboard, publishes each prepared
handle with SetClipboardData, and closes the clipboard. Successfully published
handles belong to Windows; unsuccessful/unpublished handles are released locally.
Flush is a no-op because all data is already rendered, so the published clipboard
remains available after BlackBox exits. lastError exposes publication failures;
the GUI also displays a status message.

The private BlackBox view is used only while both the clipboard owner and
GetClipboardSequenceNumber still match this copy. External changes invalidate
that path, even if the old view remains referenced internally.

Import copies each selected medium while the clipboard is open, then closes it
before loading converters or constructing CP views. Global memory is copied into
an owned movable block with zero termination; a METAFILEPICT is deep-copied with
CopyMetaFileW, including its metafile handle. The original Windows-owned data is
never freed. The temporary copy is released after conversion. The RTF memory
reader is bounded by GlobalSize.

Converters are loaded through Meta so WinTextConv/WinPictures can refer back to
WinClipboard without a static import cycle. Their  sources retain the i386
conversion logic with address-sized native pointers and handles. The original
BlackBox Data header and Info record fields are retained. The native roundtrip
test checks a separate-process serialized payload; a live i386 BlackBox process
has not yet been tested against the x64 process.

## Unified WinApi

WinApi now contains runtime, User32, GDI and clipboard declarations.
WinGuiApi has been removed and its importers use WinApi. Duplicate declarations
were merged: native records use canonical Win32 field names; runtime address
arguments remain SYSTEM.ADDRESS where needed. Windows callbacks still use [ms64].
The build script rebuilds all dependent modules and removes obsolete generated
Win/Code/GuiApi.ocf and Win/Sym/GuiApi.osf files.

## Verification

From the repository root:

```sh
python3 Win/Test/check_clipboard64.py
# or
make -f Win/Rsrc/Makefile.blackbox64 clipboard-test
```

Requires dev0Linux, Wine and x86_64-w64-mingw32-gcc. The test builds a separate
native Windows publisher/reader and a dynamically loaded BlackBox GUI test.
It covers foreign Unicode, ANSI, styled RTF and WMF input, separate-process
BlackBox Data/Info roundtrips, actual Copy/Paste commands, picture insertion into
text, Undo/Redo, stale private-copy invalidation, and native reading of the
published formats after BlackBox exits. It uses the Wine system clipboard.

The normal workspace regression also checks internal clipboard copies. Native
Windows execution remains unverified. The known repeated Save-dialog issue is
unchanged; see [GUI save investigation](Gui-save-x64-todo.md).

Win32 ownership rules: [SetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata)
and [Clipboard Operations](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations).

The Wine test keeps a neutral USER32 process alive between participants. Without
it Wine may discard custom formats when its last GUI client exits, including
with builds made before drag-and-drop. The keeper does not own the clipboard.
