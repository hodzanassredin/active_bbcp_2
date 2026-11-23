MODULE LinPrinters;

	IMPORT S := SYSTEM, Kernel, Printers, Ports, Dialog, StdDialog, Fonts, Utf, Windows, StdWindows,
		LinBackends, HostFonts := LinFonts, LinPrintPorts, C := LinCairo,
		Gtk := LinGtk4Gtk, GU := LinGtk4Util, IO := LinGtk4IO, GP := LinGtk4Print, GLib := LinGtk2GLib,
		O := LinGtk2GObject, Libc := LinLibc;

	TYPE
		Printer = POINTER TO RECORD (Printers.Printer)
			port: LinPrintPorts.Port;
			surface: C.cairo_surface_t;
			active, pageOpen, keepPage: BOOLEAN;
			page, pages: INTEGER;
			title: ARRAY 512 OF SHORTCHAR
		END;
		Cleaner = POINTER TO RECORD (Kernel.TrapCleaner) END;
		Directory = POINTER TO RECORD (Printers.Directory) END;
		PrintHook = POINTER TO RECORD (StdDialog.PrintHook) END;

	VAR current: Printer;
		setup, settings, printer: S.ADDRESS;
		ranges: GP.Ranges; rangeCount, firstPage: INTEGER;
		path, outputPath: ARRAY 4096 OF SHORTCHAR;
		cleaner: Cleaner;
		direct, sent: BOOLEAN; sendError: INTEGER;

	PROCEDURE Unref (object: S.ADDRESS);
	BEGIN IF object # 0 THEN O.g_object_unref(S.VAL(O.GObject, object)) END END Unref;

	PROCEDURE Parent (): S.ADDRESS;
		VAR w: Windows.Window; backend: StdWindows.WindowBackend;
	BEGIN
		w := Windows.dir.First();
		WITH w: StdWindows.Window DO
			backend := w.backend;
			WITH backend: LinBackends.Backend DO RETURN S.VAL(S.ADDRESS, backend.wnd) ELSE END
		ELSE END;
		RETURN 0
	END Parent;

	PROCEDURE Dimensions;
		VAR width, height, left, top, printableWidth, printableHeight: INTEGER;
	BEGIN
		width := SHORT(ENTIER(GP.gtk_page_setup_get_paper_width(setup, 1)*Ports.point+0.5));
		height := SHORT(ENTIER(GP.gtk_page_setup_get_paper_height(setup, 1)*Ports.point+0.5));
		left := SHORT(ENTIER(GP.gtk_page_setup_get_left_margin(setup, 1)*Ports.point+0.5));
		top := SHORT(ENTIER(GP.gtk_page_setup_get_top_margin(setup, 1)*Ports.point+0.5));
		printableWidth := SHORT(ENTIER(GP.gtk_page_setup_get_page_width(setup, 1)*Ports.point+0.5));
		printableHeight := SHORT(ENTIER(GP.gtk_page_setup_get_page_height(setup, 1)*Ports.point+0.5));
		current.InitPrinter(-left, -top, width-left, height-top);
		current.port.SetSize(MAX(1, printableWidth DIV current.port.unit), MAX(1, printableHeight DIV current.port.unit))
	END Dimensions;

	PROCEDURE IsVirtual* (): BOOLEAN;
	BEGIN RETURN (printer # 0) & (GP.gtk_printer_is_virtual(printer) # 0) END IsVirtual;

	PROCEDURE PrinterName* (OUT name: ARRAY OF CHAR);
		VAR result: INTEGER;
	BEGIN
		name := "";
		IF printer # 0 THEN Utf.Utf8ToString(GP.gtk_printer_get_name(printer)^$, name, result) END
	END PrinterName;

	PROCEDURE DisposeJob;
		VAR ignored: INTEGER;
	BEGIN
		IF current.active & (current.port.cr # NIL) THEN
			C.cairo_destroy(current.port.cr); current.port.cr := NIL;
			C.cairo_surface_destroy(current.surface); current.surface := NIL;
			ignored := GLib.g_unlink(S.VAL(GLib.PString, S.ADR(path)));
			current.active := FALSE; current.pageOpen := FALSE; current.res := 1;
			direct := FALSE
		END
	END DisposeJob;

	PROCEDURE Abort*;
	BEGIN
		IF current.active & (current.port.cr # NIL) THEN
			Kernel.PopTrapCleaner(cleaner); DisposeJob
		END
	END Abort;

	PROCEDURE (c: Cleaner) Cleanup;
	BEGIN DisposeJob END Cleanup;

	PROCEDURE SetOutputFile* (IN name: ARRAY OF CHAR);
		VAR result: INTEGER;
	BEGIN
		ASSERT(~current.active, 20);
		ASSERT(LEN(name$) < 1300, 22);
		Utf.StringToUtf8(name, outputPath, result); ASSERT(result = 0, 21);
		direct := TRUE; firstPage := 0;
		IF ranges # NIL THEN GLib.g_free(S.VAL(GLib.gpointer, ranges)); ranges := NIL END;
		rangeCount := 0
	END SetOutputFile;

	PROCEDURE (p: Printer) SetOrientation (landscape: BOOLEAN);
	BEGIN
		IF landscape THEN GP.gtk_page_setup_set_orientation(setup, 1)
		ELSE GP.gtk_page_setup_set_orientation(setup, 0) END;
		Dimensions;
		IF p.active THEN C.cairo_pdf_surface_set_size(p.surface,
			GP.gtk_page_setup_get_paper_width(setup, 1), GP.gtk_page_setup_get_paper_height(setup, 1)) END
	END SetOrientation;

	PROCEDURE (p: Printer) OpenJob (VAR copies: INTEGER; name: ARRAY OF CHAR);
		VAR descriptor, ignored: INTEGER;
	BEGIN
		p.res := 0;
		IF p.active THEN p.res := 1; RETURN END;
		IF direct THEN path := outputPath$+"-XXXXXX"
		ELSE
			IF printer = 0 THEN p.res := 1; RETURN END;
			path := "/tmp/blackbox-print-XXXXXX"
		END;
		descriptor := GLib.g_mkstemp(S.VAL(GLib.PString, S.ADR(path)));
		IF descriptor < 0 THEN p.res := 1; RETURN END;
		ignored := Libc.close(descriptor);
		Utf.StringToUtf8(name, p.title, ignored);
		p.surface := C.cairo_pdf_surface_create(path,
			GP.gtk_page_setup_get_paper_width(setup, 1), GP.gtk_page_setup_get_paper_height(setup, 1));
		IF C.cairo_surface_status(p.surface) # 0 THEN
			C.cairo_surface_destroy(p.surface); p.surface := NIL; p.res := 1;
			ignored := GLib.g_unlink(S.VAL(GLib.PString, S.ADR(path))); RETURN
		END;
		p.port.cr := C.cairo_create(p.surface);
		IF C.cairo_status(p.port.cr) # 0 THEN
			C.cairo_destroy(p.port.cr); p.port.cr := NIL; C.cairo_surface_destroy(p.surface); p.surface := NIL;
			ignored := GLib.g_unlink(S.VAL(GLib.PString, S.ADR(path))); p.res := 1; RETURN
		END;
		p.active := TRUE; p.pages := 0;
		p.page := firstPage; p.pageOpen := FALSE; Kernel.PushTrapCleaner(cleaner);
		(* Native settings handle copies; Documents then renders each page once. *)
		IF direct THEN copies := 1 END
	END OpenJob;

	PROCEDURE (p: Printer) OpenPage;
		VAR i: INTEGER; scale: REAL;
	BEGIN
		IF ~p.active OR p.pageOpen THEN p.res := 1; RETURN END;
		p.keepPage := rangeCount = 0;
		FOR i := 0 TO rangeCount-1 DO
			IF (p.page >= ranges[i].first) & (p.page <= ranges[i].last) THEN p.keepPage := TRUE END
		END;
		C.cairo_save(p.port.cr);
		C.cairo_translate(p.port.cr, GP.gtk_page_setup_get_left_margin(setup, 1), GP.gtk_page_setup_get_top_margin(setup, 1));
		scale := p.port.unit/Ports.point;
		C.cairo_scale(p.port.cr, scale, scale);
		IF ~p.keepPage THEN C.cairo_rectangle(p.port.cr, 0, 0, 0, 0); C.cairo_clip(p.port.cr) END;
		p.pageOpen := TRUE
	END OpenPage;

	PROCEDURE (p: Printer) ClosePage;
	BEGIN
		IF p.active & p.pageOpen THEN
			C.cairo_restore(p.port.cr);
			IF p.keepPage THEN C.cairo_show_page(p.port.cr); INC(p.pages) END;
			IF C.cairo_status(p.port.cr) # 0 THEN p.res := 1 END;
			p.pageOpen := FALSE; INC(p.page)
		END
	END ClosePage;

	PROCEDURE [ccall] Sent (job, data, error: S.ADDRESS);
	BEGIN IF error = 0 THEN sendError := 0 ELSE sendError := 1 END; sent := TRUE END Sent;

	PROCEDURE (p: Printer) CloseJob;
		VAR nativeJob, jobSettings, error: S.ADDRESS; ok, ignored: INTEGER;
	BEGIN
		IF ~p.active OR (p.port.cr = NIL) THEN RETURN END;
		IF p.pageOpen THEN p.ClosePage END;
		C.cairo_destroy(p.port.cr); p.port.cr := NIL;
		C.cairo_surface_finish(p.surface);
		IF C.cairo_surface_status(p.surface) # 0 THEN p.res := 1 END;
		C.cairo_surface_destroy(p.surface); p.surface := NIL; Kernel.PopTrapCleaner(cleaner);
		IF direct THEN
			IF (p.res = 0) & (p.pages > 0) THEN
				ignored := GLib.g_rename(S.VAL(GLib.PString, S.ADR(path)), S.VAL(GLib.PString, S.ADR(outputPath)));
				IF ignored # 0 THEN p.res := 1 END
			END;
			ignored := GLib.g_unlink(S.VAL(GLib.PString, S.ADR(path)))
		END;
		IF ~direct THEN
			IF (p.res = 0) & (p.pages > 0) THEN
				jobSettings := GP.gtk_print_settings_copy(settings);
				(* The PDF already contains the requested ranges/selection. *)
				GP.gtk_print_settings_set_print_pages(jobSettings, 0);
				nativeJob := GP.gtk_print_job_new(p.title, printer, jobSettings, setup);
				Unref(jobSettings); error := 0;
				IF nativeJob = 0 THEN p.res := 1
				ELSE
					ok := GP.gtk_print_job_set_source_file(nativeJob, path, error);
					IF ok # 0 THEN
						sent := FALSE; sendError := 0;
						GP.gtk_print_job_send(nativeJob, S.ADR(Sent), 0, 0);
						WHILE ~sent DO ignored := GU.Iterate(1) END;
						p.res := sendError
					ELSE
						p.res := 1;
						IF error # 0 THEN GLib.g_error_free(S.VAL(GLib.GError, error)) END
					END;
					Unref(nativeJob)
				END
			END;
			ignored := GLib.g_unlink(S.VAL(GLib.PString, S.ADR(path)))
		END;
		direct := FALSE; p.active := FALSE;
		IF p.res # 0 THEN Dialog.ShowMsg("#System:FailedToOpenPrintJob") END
	END CloseJob;

	PROCEDURE (d: Directory) Default (): Printers.Printer;
	BEGIN RETURN current END Default;

	PROCEDURE (d: Directory) Current (): Printers.Printer;
	BEGIN RETURN current END Current;

	PROCEDURE (d: Directory) Available (): BOOLEAN;
	BEGIN RETURN ~current.active END Available;

	PROCEDURE (h: PrintHook) GetPage (p: Printers.Printer; VAR w, height: INTEGER);
		VAR l, t: INTEGER;
	BEGIN p.GetRect(l, t, w, height); DEC(w, l); DEC(height, t) END GetPage;

	PROCEDURE OrientationButtons (widget: Gtk.GtkWidget; VAR buttons: ARRAY OF Gtk.GtkWidget; VAR count: INTEGER);
		VAR child: Gtk.GtkWidget;
	BEGIN
		IF O.g_type_check_instance_is_a(S.VAL(S.ADDRESS,widget),Gtk.gtk_check_button_get_type()) # 0 THEN
			IF count < LEN(buttons) THEN buttons[count] := widget END; INC(count)
		END;
		child := Gtk.gtk_widget_get_first_child(widget);
		WHILE child # NIL DO OrientationButtons(child,buttons,count); child := Gtk.gtk_widget_get_next_sibling(child) END
	END OrientationButtons;

	PROCEDURE (h: PrintHook) PrintSetup;
		VAR new,dialog: S.ADDRESS; count,i,orientation,result: INTEGER;
			buttons: ARRAY 4 OF Gtk.GtkWidget;
	BEGIN
		IF current.active THEN RETURN END;
		dialog := GP.gtk_page_setup_unix_dialog_new("Page setup",Parent());
		GP.gtk_page_setup_unix_dialog_set_page_setup(dialog,setup);
		GP.gtk_page_setup_unix_dialog_set_print_settings(dialog,settings);
		count := 0; OrientationButtons(S.VAL(Gtk.GtkWidget,dialog),buttons,count);
		IF count = 4 THEN
			(* Some GTK4 builds leave the four orientation check buttons ungrouped. *)
			FOR i := 0 TO 3 DO Gtk.gtk_check_button_set_group(buttons[i],NIL) END;
			FOR i := 0 TO 3 DO Gtk.gtk_check_button_set_active(buttons[i],0) END;
			FOR i := 1 TO 3 DO Gtk.gtk_check_button_set_group(buttons[i],buttons[0]) END;
			orientation := GP.gtk_page_setup_get_orientation(setup);
			IF orientation = 1 THEN i := 2 ELSIF orientation = 2 THEN i := 1 ELSE i := orientation END;
			Gtk.gtk_check_button_set_active(buttons[i],1)
		END;
		result := GU.RunDialog(S.VAL(Gtk.GtkDialog,dialog));
		IF result = -5 THEN
			new := GP.gtk_page_setup_unix_dialog_get_page_setup(dialog);
			IF new # 0 THEN Unref(setup); setup := new; Dimensions END
		END;
		Gtk.gtk_window_destroy(S.VAL(Gtk.GtkWindow,dialog))
	END PrintSetup;

	PROCEDURE (h: PrintHook) PrintDialog (hasSelection: BOOLEAN;
		VAR from, to, copies: INTEGER; VAR selection: BOOLEAN);
		VAR dialog, new, newPrinter: S.ADDRESS; result, i: INTEGER;
	BEGIN
		copies := 0; selection := FALSE; from := 0; to := MAX(INTEGER);
		IF current.active THEN RETURN END;
		dialog := GP.gtk_print_unix_dialog_new("BlackBox", Parent());
		GP.gtk_print_unix_dialog_set_settings(dialog, settings);
		GP.gtk_print_unix_dialog_set_page_setup(dialog, setup);
		GP.gtk_print_unix_dialog_set_embed_page_setup(dialog, 1);
		GP.gtk_print_unix_dialog_set_manual_capabilities(dialog, {7});
		GP.gtk_print_unix_dialog_set_support_selection(dialog, 1);
		IF hasSelection THEN GP.gtk_print_unix_dialog_set_has_selection(dialog, 1)
		ELSE GP.gtk_print_unix_dialog_set_has_selection(dialog, 0) END;
		result := GU.RunDialog(S.VAL(Gtk.GtkDialog, dialog));
		IF result = -5 THEN
			newPrinter := GP.gtk_print_unix_dialog_get_selected_printer(dialog);
			IF newPrinter # 0 THEN
				Unref(printer); printer := newPrinter;
				new := O.g_object_ref(S.VAL(O.GObject, printer));
				new := GP.gtk_print_unix_dialog_get_settings(dialog); Unref(settings); settings := new;
				new := GP.gtk_print_unix_dialog_get_page_setup(dialog); new := GP.gtk_page_setup_copy(new);
				Unref(setup); setup := new; Dimensions;
				IF ranges # NIL THEN GLib.g_free(S.VAL(GLib.gpointer, ranges)); ranges := NIL END;
				rangeCount := 0; firstPage := 0;
				selection := hasSelection & (GP.gtk_print_settings_get_print_pages(settings) = 3);
				IF GP.gtk_print_settings_get_print_pages(settings) = 2 THEN
					ranges := GP.gtk_print_settings_get_page_ranges(settings, rangeCount);
					IF rangeCount > 0 THEN
						from := MAX(INTEGER); to := 0;
						FOR i := 0 TO rangeCount-1 DO
							from := MIN(from, ranges[i].first); to := MAX(to, ranges[i].last)
						END;
						firstPage := from
					END
				END;
				copies := MAX(1, GP.gtk_print_settings_get_n_copies(settings)); direct := FALSE
			END
		END;
		Gtk.gtk_window_destroy(S.VAL(Gtk.GtkWidget, dialog))
	END PrintDialog;

	PROCEDURE Install*;
		VAR dir: Directory; hook: PrintHook;
	BEGIN
		IF current # NIL THEN RETURN END;
		NEW(cleaner);
		setup := GP.gtk_page_setup_new(); settings := GP.gtk_print_settings_new();
		NEW(current); NEW(current.port); current.port.Init(HostFonts.pixel, Ports.printer);
		current.InitPort(current.port); Dimensions;
		NEW(dir); Printers.SetDir(dir); NEW(hook); StdDialog.SetPrintHook(hook)
	END Install;

CLOSE
	IF current # NIL THEN
		Abort; Unref(printer); Unref(settings); Unref(setup);
		IF ranges # NIL THEN GLib.g_free(S.VAL(GLib.gpointer, ranges)) END
	END
END LinPrinters.
