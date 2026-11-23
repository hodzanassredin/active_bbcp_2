# Dragging selections between documents

LinMechanisms.TrackToDrop uses Controllers.PollDrop to route the pointer from
the source frame into the destination window and its controller. The standard
StdWindows forwarder converts source-frame coordinates through screen pixels
into destination-frame coordinates. The destination controller checks whether
the offered view/selection can be inserted and draws its insertion marker.
The source no longer forces every drop back into its own frame.

Release moves the selection. Ctrl requests a copy; a source with noCaret always
copies. Escape, an incompatible target, or release outside a BlackBox window
cancels. A native GDK window check prevents dropping into a covered BlackBox
window behind another application. LinPorts records Escape while consuming input
events during tracking. The destination marker is cleared and the cursor restored
on completion/cancellation. Containers performs the actual copy/move and the
framework's existing edit/undo operations after tracking returns.

The changes apply to the shared LinPorts and LinMechanisms sources used by i386
and AMD64. Both GDK bindings export gdk_window_get_toplevel. External URI/file
opening remains in LinFileDrops and is a separate mechanism.

## Build checks

The complete Linux AMD64 subsystem build and the ordinary heap/GUI regression
passed, including clipboard, Undo/Redo and Find/Replace. LinPorts and
LinMechanisms also compile with the i386 compiler in a fresh host process.
No automatic pointer drag was performed. Manual transfer and Ctrl-copy between
the two test documents were confirmed by the user.

## Manual verification

Restart the Linux GUI to load the updated platform modules, compile
CheckLinTextDrag and execute:

```componentpascal
CheckLinTextDrag.Open
```

It opens Drag source with selected Cyrillic text and an empty Drag target. Drag
the selection into the empty document. It should disappear from the source and
appear in the target. Repeat with Ctrl: the source should retain its text.
Also check Escape, release outside BlackBox, Undo/Redo in the affected documents,
and a local drag within one document. CheckLinTextDrag.Report logs the lengths of
the two test models. Re-running Open creates a fresh pair of documents.

A separate blackbox-text-drag executable can link CheckLinTextDragAuto before
LinLoader to prepare these documents after ordinary GUI initialization. It does
not synthesize pointer events or drag automatically. No mouse automation is used:
a previous automated drag test caused a system failure and must not be repeated.
