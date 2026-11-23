# Linux printing

Linux GUI startup installs LinPrinters from LinConfig.SetupBefore. It supplies
Printers.Directory and StdDialog.PrintHook for both i386 and AMD64. The usual
Print Setup and Print commands now open GTK's native dialogs, and the Print
command renders the selected BlackBox document through the existing
Printing/Documents pagination pipeline.

## Using the dialogs

Print Setup selects paper, orientation and margins. The native margins define
the printable area; the document's own layout and header/footer margins remain
controlled by BlackBox Page Setup. Print selects a printer, all pages or page
ranges, and the copy count supported by the selected backend. Selection is
exposed when StdCmds reports a printable selection. Cancel submits no job.

The GTK file backend provides Print to File; select PDF and an output filename.
CUPS queues are listed by GTK's CUPS backend. BlackBox passes a generated PDF
to GtkPrintJob, without running a shell or constructing an lpr command. GTK
handles the selected printer and its backend settings. Completion means that
the backend accepted the data; it does not mean that paper has been printed.
Physical printer output has not been verified by the automated tests.

GTK4, Cairo, PangoCairo and the appropriate GTK print backends must be installed.
The GTK file and CUPS backend modules must be available in the distribution
GTK4 installation. CUPS must be configured for physical printers. A physical printer is not required for PDF output.
Settings are retained for the current process, not saved as per-user defaults.

## Implementation

- LinGtk3Print contains GTK Unix print bindings; Mod64 provides native-width
  calls. LinConfig installs LinPrinters only for the GUI; console startup does
  not require a printer or a print dialog.
- LinPrinters manages paper setup, printer settings, job/page lifetime and
  submission. Noncontiguous ranges are filtered during PDF generation and the
  native range setting is reset before submission, avoiding double filtering.
- LinPrintPorts implements a printer-mode Ports.Port/Rider using Cairo. Text,
  Unicode, rectangles, ellipses and paths remain vector content. Pango handles
  glyph shaping. Interactive marks and screen buffer operations are omitted.
- LinRastersPng paints pixbufs into the Cairo print context at their intrinsic
  DPI, preserving original image resolution instead of screen-sized rasterizing
  the entire page. PNG stream/native byte ownership is released explicitly.
- Temporary jobs are deleted after submission or abort. Direct PDF output uses
  an adjacent temporary file and rename on success, preserving an existing file
  when rendering is aborted. A trap cleaner releases active Cairo resources.
- DevCPCamd64 bridges SysV floating-point results from XMM0 to CP's x87 stack.
  The print regression covers REAL, SHORTREAL, an indirect foreign call and
  arithmetic with two foreign results. Windows ms64 floating-point interfaces
  remain outside that change.

For programmatic PDF generation without a dialog:

```componentpascal
	LinPrinters.SetOutputFile("/tmp/document.pdf");
	Printing.PrintView(view, Printing.NewDefaultPar("Document"));
```

Check `Printers.dir.Current().res` after printing. SetOutputFile applies to the
next job only; it is not an alternative global printer directory. Abort releases
an active render job without publishing its temporary PDF. Already queued CUPS
jobs must be managed through the system print queue.

## Build and verification

Build the framework with the normal Linux CompileSubs instructions and
DevLibrarian's Mod64 selection for AMD64. i386 uses Mod and DevCompiler. The
AMD64 backend itself must first be rebuilt in the i386 host when updating its
source; restart dev0Linux before using the rebuilt compiler modules.

```sh
python3 Lin/Test/check_printing.py
# Reuse existing compiled subsystem trees, rebuilding the print modules:
python3 Lin/Test/check_printing.py --skip-build
python3 Lin/Test/check_printing.py --arch x64 --skip-build
```

Default output trees are build/blackbox-lin32 and build/blackbox-lin64. A display
and distribution Poppler utilities (pdfinfo, pdftotext, pdfimages) are required.
The test only selects a virtual GTK printer; it never submits to a physical
queue and does not simulate mouse input. It verifies native dialog OK/Cancel,
orientation, direct PDF, Unicode, PNG, copy count, abort preservation, ranges,
GTK file submission and the actual StdCmds.Print menu command. Generated PDFs
and compiler/run logs remain in each build directory. The normal bootstrap
is not replaced by the automatically closing printing-test executable.

The current implementation and tests target GTK4 on Linux. Physical CUPS
printers, driver-specific options and other Unix platforms need separate checks.

[Русская документация](ru/Printing.md)
