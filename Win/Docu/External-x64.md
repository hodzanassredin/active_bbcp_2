# External applications on Windows AMD64

WinDialog installs Dialog.ExtCallHook during initialization. Both
Dialog.OpenExternal and Dialog.RunExternal are available in the normal WinInit
workspace.

`Dialog.OpenExternal(name)` opens a document, directory, executable or URI using
Windows Shell associations (`ShellExecuteExW`, verb `open`). The input is a single
name or URI, not a command line. Under Wine, the configured Wine associations
select the handler, including the browser for HTTP/HTTPS links.

`Dialog.RunExternal(command)` uses `CreateProcessW` with a writable copy of the
Unicode command line. Quote an executable path containing spaces. It does not
implicitly invoke a command shell. The process starts independently; BlackBox
closes its process and thread handles without waiting for the program to exit.
`WinDialog.hideExtRunWindow := TRUE` hides the next RunExternal launch and is then
reset, matching the i386 interface.

Both methods run on the framework thread. No CP code is started on a foreign
Windows thread. OpenExternal uses SEE_MASK_NOASYNC to finish Shell startup and
consume its arguments before returning; it does not wait for the opened program
to exit. The existing WinKernel OLE initialization supplies the thread's COM
initialization.

Failures open the BlackBox Log and append the name and Windows error description.
This avoids the repeated native-dialog crash observed in Wine uxtheme.
`WinDialog.lastExternalError` is zero on success and contains the error on
failure. Missing associations are handled as ordinary Shell errors, rather than
compiler/runtime traps. WinApi provides native AMD64 layouts for
SHELLEXECUTEINFOW (112 bytes), STARTUPINFOW (104) and PROCESS_INFORMATION (24).

## Checks

```sh
python3 Win/Test/check_external64.py --out /tmp/blackbox-external64
# or
make -f Win/Rsrc/Makefile.blackbox64 external-test
```

The test registers private file and URI associations with a native handler,
checks Unicode paths and quoted arguments, verifies hidden startup and checks
that failed opens and launches append an error to the Log. Temporary registry entries use a unique
name and are removed in finally. They are placed directly in Wine's HKCR because
Wine 6 does not merge the test's HKCU Software/Classes keys into HKCR.

The test does not open the user's browser or change existing associations.
Execution is verified under Wine; native Windows remains unverified.
