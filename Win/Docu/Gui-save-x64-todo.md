# Native Save dialogs: remaining investigation

Native Save/Save As/Save Copy As hooks are enabled in WinDialog, including
Save/Discard/Cancel on dirty close. The known repeated-dialog failure under Wine
6.0.3 is accepted for now and remains to be investigated. Basic Save, overwrite,
ODC import/readback and dirty-close checks succeeded during development, but
extended repeated-dialog tests failed intermittently. Enabling the feature does
not establish that this failure is fixed. The standard workspace regression does
not exercise the extended save sequence.

The reproducer edited a normal TextViews window, cancelled Save, saved an ODC,
edited and replaced it, attempted a readonly save, then used Save As and Save
Copy As. A process-owned SetTimer callback answered native dialogs. Both default
StdConfig and custom appConfigProcedure startup branches were exercised.

The intermittent failure exited with code 5 without a CP trap report. Wine SEH
tracing showed STATUS_ACCESS_VIOLATION at uxtheme.dll MSSTYLES_FindClass, RVA
0x9b06, inside a native modal dialog. WinKernel correctly declined to handle the
foreign fault. The fault appeared during Save Copy As or a readonly-error message
box. The available trace does not establish whether the cause is the native ABI,
GUI state, lifetime/reentrancy, or Wine.

Checks that did not identify a fix:

- OPENFILENAMEW layout and size (152 bytes) were checked.
- Eight repeated dialogs in a standalone mingw C program succeeded.
- Eight cancelled dialogs in a minimal statically linked CP program succeeded.
- Delaying the timer response, responding only once, and replacing stack dialog
  buffers with global buffers did not eliminate the full-framework failure.
- Wine heap tracing did not identify an invalid free or overwritten allocation.

Next, isolate a full-framework native dialog with an external driver, without a
CP timer callback; compare against a native owner-window reproducer. Check live
nonvolatile register preservation across the framework window callbacks if the
failure remains. Do not treat a few successful GUI runs as proof of a fix.
