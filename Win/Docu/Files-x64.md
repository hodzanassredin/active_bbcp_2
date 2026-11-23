# WinFiles AMD64

WinFiles implements Files for the Windows AMD64 runtime. The default root is
the executable directory; WinFiles.SetRoot changes it. Paths and names use UTF-16,
handles and addresses are 64-bit. As in the i386 WinFiles, each file uses at most
four 2 KiB buffers (8 KiB of cached file data). File length and rider positions
remain INTEGER: the supported limit is MAX(INTEGER), 2 GiB minus one byte.
Old rejects larger files with result 5; writes check position overflow.

## Buffering

Buffers are allocated lazily and shared by all riders of a carrier. Each holds
its block origin, valid length, dirty flag and 2 KiB byte array. A missing block
uses a free buffer or a round-robin victim; a dirty victim is written before it
is reused. Reads load only the required block through SetFilePointerEx/ReadFile.
Writers modify cached blocks. A shared buffer lookup on every chunk keeps readers
and writers coherent even after another rider has evicted a block; their logical
positions remain independent. Partial native reads/writes are handled explicitly.

New and Temp start without a disk handle. A dirty eviction or explicit Flush
creates a private file in GetTempPathW's directory, with FILE_ATTRIBUTE_TEMPORARY
and FILE_FLAG_DELETE_ON_CLOSE. Thus RAM use does not grow with file size.
Closing an unregistered carrier discards it; Windows deletes the backing file
when its handle closes or the process exits. Explicit Close is recommended.

## New files and registration

Directory.New(loc, ask) creates an anonymous writable carrier associated with
loc. It does not change a named disk file. NewWriter starts at the current end;
reusing a writer or reader on the same carrier preserves its position. Writers
can seek within the existing contents and overwrite or append bytes.

```componentpascal
loc := Files.dir.This("Demo");
f := Files.dir.New(loc, Files.dontAsk);
IF f # NIL THEN
	wr := f.NewWriter(NIL);
	wr.WriteBytes(data, 0, LEN(data));
	f.Register("Example", "bin", Files.dontAsk, res)
ELSE
	res := loc.res
END;
```

Register applies the default extension only when necessary and validates the
destination path. It flushes the carrier to its backing file, then writes a uniquely
named `.bbtmp-<pid>-<serial>.tmp` file in the destination directory, handles
partial transfers using a fixed 32 KiB copy buffer, calls FlushFileBuffers and
closes the destination handle.
Only then does MoveFileExW with REPLACE_EXISTING and WRITE_THROUGH publish the
new file. Normal overwrite needs no additional backend confirmation. Callers that need
overwrite confirmation must request it before Register.

A write/flush/commit failure returns an error without deleting or truncating the
previous named file. The staging file is removed on the normal failure path.
The anonymous carrier remains available for retry. On success, Register closes
the carrier and invalidates its riders, following the Files contract. Reopen the
registered name through Directory.Old. A crash can leave an uncommitted staging
file; it is never treated as the registered target.

Readonly targets are checked explicitly. With Files.ask and an installed
WinFiles.MessageBox callback, protection/sharing/resource errors offer Retry or
Cancel; cancellation returns 8. Without a GUI callback, errors are returned
directly. WinDialog installs this callback during GUI startup.

Directory.New validates the destination directory. An explicit loc.res=76 creates
missing directories recursively; Files.ask can request user confirmation to do
so. Files.dontAsk creates missing directories automatically, as in i386 WinFiles.
Files.ask without a GUI callback also creates them automatically. Directory.Delete supports
files and empty directories. Rename uses MoveFileExW, including replacement of an
existing writable target. SameFile compares normalized Windows paths without
case sensitivity.

## Existing files

Directory.Old(..., Files.shared) retains a GENERIC_READ/FILE_SHARE_READ handle.
Opening reads only the file size; data blocks are loaded on demand. NewWriter
returns NIL. A live shared handle prevents external in-place writes.

Before this backend replaces, renames or deletes a name with live shared
carriers, it copies each carrier's old contents into a private delete-on-close
backing file, using the same bounded 32 KiB copy buffer. Only after a successful
copy is the original handle released. Existing readers therefore retain all old
contents, including blocks they have not read yet. This costs disk space and
streaming I/O when a name is changed, rather than RAM proportional to file size.
External mutations can be blocked by the live Windows handles.

Directory.Old(..., Files.exclusive) obtains a GENERIC_READ|GENERIC_WRITE handle
without sharing. Flush writes only dirty blocks at their individual offsets and
calls FlushFileBuffers; it does not rewrite the whole file. Close flushes dirty
data and releases the handle. Unreachable carriers are closed by a finalizer.

A live exclusive handle blocks other opens, replacement, rename and deletion
until Close. Such operations return 6; this backend does not implement making a
live exclusive carrier anonymous during Register. Unlike registration of a new
version, exclusive Flush is an in-place update and is not transactional.

Flush/Close have no result parameter. Their I/O failures set loc.res and raise a
trap rather than allowing an apparently successful write. Register, New, Old,
Delete and Rename use Files result codes, including 1 invalid parameter,
2 missing file/path, 4 write protection, 5 I/O, 6 access/sharing, 8 cancellation,
80 memory and 81 resource exhaustion. Other native errors are negative.

Files.dir.Temp uses the same bounded buffer cache and lazy disk backing, including
for TextModels spill data. Close and successful Register invalidate a
carrier. No further reader/writer operations are allowed afterward.

## Differences from i386 WinFiles

The bounded cache, lazy spill, shared rider data and INTEGER position model are
ported from WinFiles. This is not a byte-for-byte port: x64 retains transactional
staging for Register and preserves shared carriers by copying to private backing
files rather than renaming a live named carrier to hidden state. A live exclusive
carrier remains exclusive until Close. The i386 handle-pressure recovery that
closes/reopens older file handles is not implemented here.

## GUI integration

WinDialog enables native Open and Save dialogs through the 152-byte
OPENFILENAMEW structure. Save, Save As and Save Copy As use the standard
StdCmds/Converters pipeline and retain the selected export converter (ODC by
default). Changed documents offer Save, Discard or Cancel when closing.
WinDialog.New opens an ordinary editable document that can be saved.

The repeated-dialog crash observed under Wine remains unresolved. The dialogs
are enabled with this known issue; see [GUI save investigation](Gui-save-x64-todo.md).
The standard GUI regression does not exercise this extended save sequence.

## Tests

From the repository root:

```sh
python3 Win/Test/check_files64.py
python3 Win/Test/build_blackbox64.py --test
```

The disk test uses its own generated FilesCase directory and validates binary
contents from Linux after Wine exits. It covers Unicode names, writes exceeding
64 MiB (a full 70 MiB roundtrip), a bounded CP heap allocation increase, sparse
fixtures at and beyond MAX(INTEGER), dirty-buffer eviction, independent and reused
riders, cross-block reads, shorter replacement, preserved uncached shared data,
locked/readonly targets, cancellation, retries, exclusive Flush/Close, finalizer
closure, temporary-file cleanup, directory creation, rename/delete, invalid names
and staging cleanup.

The separate GUI regression checks workspace startup, editing and shutdown in
both WinInit configuration paths. Tests pass under Wine; native Windows execution
remains unverified.
