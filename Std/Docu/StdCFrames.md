StdStdCFrames

Cross-platform realization of controls for StdCFrames intrerface



- `Update()` method is called when control contents and/or state are updated by the external source;
that's why we're calling `c.Update^` everywhere
we should check for a label/text change there for edit controls, etc.

- `Close()` is called when the control is going to die; we should free any resources there

- `SetOffset()` is called when the control is moved placed/moved; it was used to "adapt" the
original native controls

- `DblClickOk()` is called to check if doubleclick is accepted.
seems to be used only for various list boxes (listbox, selection, combo).

- `UpdateList()` is called i don't know when yet, for ListBox, ComboBox, SelectionBox, and TreeFrame.
seems to be called when we need to repopulate the internal list storage.

- `UpdateRange()` in SelectionBox seems to do this:
IF (op = Dialog.set) OR (from # TO) THEN c.Update; RETURN END;
otherwise invert the selection

- `Select()` in SelectionBox does nothing

- `GetSelection()` in SelectionBox returns [0..MAX(INTEGER)]. for ComboBox it operates on the editor.

- TreeFrame overrides `GetSize (OUT w, h: INTEGER)` for some reason.

- `.noRedraw` field means that instead of calling `Views.Update(c, Views.rebuildFrames);`

on state change, the code in `Controls` module will call `Update()` method instead.
i.e. it expects that the control will redraw itself in its updater.

the original code does lazy allocation of native controls in `Restore()`, so we can do the same.

note that ListBox can work as a combobox w/o editor when its size is small enough.
also, ComboBox displays its predefinted contents as a listbox when it is big enough.
