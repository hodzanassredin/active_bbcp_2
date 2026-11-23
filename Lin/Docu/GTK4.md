# GTK4 Linux backend

The active Linux GUI uses GTK4 (tested with the installed GTK 4 runtime on
both i386 and AMD64). Debian/Ubuntu runtime packages are `libgtk-4-1` and
`libgtk-4-1:i386`. GTK4, GLib/GObject, GIO, Pango/PangoCairo, GdkPixbuf and
Cairo are required. GTK3 and GTK2 are not loaded into the GUI process.
The file-drop interface uses `GdkFileList` and needs GTK 4.6 or newer.

## Native interfaces

`LinGtk4Gtk` and `LinGtk4Gdk` declare GTK4 widgets, surfaces and input APIs.
All GDK4 events are opaque and accessed through getters; no GTK3 event record
layout is used. `GdkRGBA` has four SHORTREAL fields in GTK4. `LinGtk4IO`
provides the GIO/main-context operations. `LinGtk4Util` connects signals,
implements modal response waits and obtains X11 surface coordinates through
`LinGtk4X11` and `LinX11`. Existing `LinGtk2GLib`, `LinGtk2GObject`,
`LinGtk2Pango` and `LinGtk2Keysyms` names refer to independent libraries or
constants; they do not load GTK2. Unused older GTK interface modules remain
available as source but are not part of the active GUI.

## Rendering and window size

A GtkDrawingArea is the single child of each native window. Its draw function
receives the actual canvas allocation and the Cairo context. Native allocation
changes update the BlackBox window with a recursion guard. Initial/default
window size is independent of the canvas minimum size, so both growth and
shrinking work, including tool dialogs.

LinPorts retains a Cairo image surface as its backing store. GTK4 snapshots
present the surface. Queuing a widget redraw does not invalidate BlackBox
content; only accumulated dirty regions cause StdWindows updates. XOR marks,
saved rectangles, scrolling and Pango/Cairo text drawing retain their existing
semantics. Text and space decorations use the same Pango rendering path,
clipped horizontally without scaling the font.

## Input and drag-and-drop

A GtkEventControllerLegacy feeds opaque GDK4 input events into the existing
BlackBox key, tracking and wheel handlers. Fractional wheel deltas accumulate
per window. During synchronous BlackBox tracking, Rider.Input iterates the
main context, polls the native pointer and permits frame-clock presentation.
Event callbacks suppress recursive mouse tracking and record Escape.

External file drops use GtkDropTarget with GdkFileList and COPY action. Each
local regular filename is converted from UTF-8 and queued for StdApi.OpenDoc
after the native drop callback finishes.
Remote file objects are ignored. Internal text/view dragging still uses the
BlackBox mechanisms. No automatic mouse-drag tests are allowed.

X11 global coordinates are obtained from GdkSurface/XTranslateCoordinates;
this preserves cross-window hit testing. On non-X11 displays GetOrigin reports
that global coordinates are unavailable. Wayland-specific cross-window
tracking has not been verified.

## Clipboard and native dialogs

GdkClipboard replaces the GTK selection protocol. Text is published as UTF-8;
external text is read asynchronously, converted into a TextView and pasted on
the target path. The local clipboard retains copied BlackBox views while the
native clipboard is locally owned. Clipboard ownership changes clear that
local view. Color, font and file chooser dialogs use GTK4 APIs; file chooser
paths use GFile, not removed GTK3 filename methods.

LinGtk4Bootstrap loads and initializes libxml2 under a masked FPU environment
before loading GTK4. This prevents xmlXPathInit's intentional NaN/Infinity
computations from trapping when first requested by a GTK4 font-preview worker.
Native GTK initialization and configuration each restore the caller's FPU
environment on completion.
RunDialog waits for the response signal through the GLib main context. Native
font-dialog construction and main-context iterations save/mask/restore the
floating-point environment. Backend callbacks enter the saved managed mode
inside Services.Try, then restore the native environment before returning.
The draw callback includes both BlackBox updates and the final Cairo surface
presentation in this boundary. On i386, the compiler installs its FPU control
word in procedures using floating-point arithmetic, including LinPorts.Paint.
Returning from Paint directly into GTK4 would leave invalid-operation traps
enabled in native code; GTK4 can then leave a pending exception which traps
on the next floating-point instruction. Restoring the native environment only
around the document update is insufficient.

Printing retains Cairo PDF output and GTK4 Unix print jobs, including page
ranges, copies and CUPS/virtual printers. Page setup explicitly reconstructs
the four orientation buttons' group, because the installed GTK4 build can
leave several buttons active after programmatic setup. PDF and virtual file
printer tests cover this behavior; no physical printer is exercised.

## Build and verification

From the repository root:

```sh
python3 Lin/Test/check_gtk4.py
python3 Lin/Test/check_printing.py --out32 build/blackbox-gtk4-lin32 --out64 build/blackbox-gtk4-lin64 --skip-build
python3 Lin/Test/check_signals.py --out32 build/blackbox-gtk4-lin32 --out64 build/blackbox-gtk4-lin64 --skip-build
python3 Lin/Test/check_linux64.py --out build/blackbox-gtk4-lin64 --skip-build --gui
```

The GTK4 runner builds all Linux GUI subsystems for x32 and x64 in separate
output trees. Every compiler/link process enables DevLibrarian with Mod or
Mod64 as appropriate. Normal executables are named `blackbox`; the closing
GUI regression executables are named `gtk4-gui-test`. Logs remain alongside
the executables, including a list of loaded libraries that excludes GTK2/3.
`check_gtk3.py` is a compatibility entry point for the new GTK4 runner.

Tests cover Cairo pixels, decoration alignment, reversible marks, saved areas,
scrolling, native dialog responses, FPU restoration, grow/shrink, tool sizes,
external Unicode clipboard text, local copy/paste, attribute guards for text
with multiple spaces, undo/redo, GC, and visible Find selection pixels.
Text dragging, wheel scrolling after its callback fix, and file drops from the
file manager were manually confirmed on the x64 X11 desktop during migration.
Keyboard/IME and other display backends still need target-specific manual
coverage. Transfers must be performed by the user, without mouse automation.

See the official [GTK3 to GTK4 migration guide](https://docs.gtk.org/gtk4/migrating-3to4.html).
