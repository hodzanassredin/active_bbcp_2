MODULE CheckLinPrinting;

	IMPORT S := SYSTEM, Kernel, Console, Strings, Utf, Dialog, Fonts, Ports, Printers, Printing,
		Files, StdDialog, StdCmds, StdWindows, Windows, Views, TextModels, TextViews,
		LinPrinters, Native := CheckLinPrintApi, GP := LinGtk4Print, GLib := LinGtk2GLib, O := LinGtk2GObject;

	VAR mode, ticks, found: INTEGER; configured: S.ADDRESS;
		virtualName: ARRAY 256 OF SHORTCHAR;
		base: ARRAY 2048 OF CHAR; uri: ARRAY 4096 OF SHORTCHAR;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
		VAR text: ARRAY 32 OF CHAR;
	BEGIN IF ~ok THEN Console.WriteStr("Linux printing FAILED "); Strings.IntToString(code, text); Console.WriteStr(text); Console.WriteLn; Kernel.Quit(code) END END Check;

	PROCEDURE Trap;
	BEGIN Check(FALSE, 100) END Trap;

	PROCEDURE [ccall] FindVirtual (printer, data: S.ADDRESS): INTEGER;
	BEGIN
		IF GP.gtk_printer_is_virtual(printer) # 0 THEN
			virtualName := GP.gtk_printer_get_name(printer)^$; RETURN 1
		END;
		RETURN 0
	END FindVirtual;

	PROCEDURE [ccall] Respond (data: GLib.gpointer): GLib.gboolean;
		VAR list, item: GLib.GList; dialog, type, settings, printer: S.ADDRESS;
	BEGIN
		INC(ticks); Check(ticks < 100, 101);
		IF (mode = 0) OR (mode = 3) THEN type := GP.gtk_page_setup_unix_dialog_get_type()
		ELSE type := GP.gtk_print_unix_dialog_get_type() END;
		list := GP.gtk_window_list_toplevels(); item := list;
		WHILE item # NIL DO
			dialog := S.VAL(S.ADDRESS, item.data);
			IF (O.g_type_check_instance_is_a(dialog,type) # 0) & (GP.gtk_widget_get_visible(dialog) # 0) THEN
				IF mode = 3 THEN
					IF configured # dialog THEN
						settings := GP.gtk_page_setup_copy(GP.gtk_page_setup_unix_dialog_get_page_setup(dialog));
						GP.gtk_page_setup_set_orientation(settings,1);
						GP.gtk_page_setup_unix_dialog_set_page_setup(dialog,settings);
						O.g_object_unref(S.VAL(O.GObject,settings)); configured := dialog;
						GLib.g_list_free(list); RETURN 1
					END;
					settings := GP.gtk_page_setup_unix_dialog_get_page_setup(dialog);
					INC(found); GLib.g_list_free(list); GP.gtk_dialog_response(dialog,-5); RETURN 0
				END;
				IF mode < 2 THEN
					INC(found); GLib.g_list_free(list); GP.gtk_dialog_response(dialog, -6); RETURN 0
				END;
				IF configured # dialog THEN
					settings := GP.gtk_print_settings_new();
					GP.gtk_print_settings_set_printer(settings, virtualName);
					GP.gtk_print_settings_set(settings, "output-uri", uri);
					GP.gtk_print_settings_set(settings, "output-file-format", "pdf");
					IF mode = 4 THEN GP.gtk_print_settings_set(settings, "print-pages", "all")
					ELSE GP.gtk_print_settings_set(settings, "print-pages", "ranges") END;
					GP.gtk_print_settings_set(settings, "page-ranges", "0,2");
					GP.gtk_print_unix_dialog_set_settings(dialog, settings);
					O.g_object_unref(S.VAL(O.GObject, settings)); configured := dialog
				END;
				printer := GP.gtk_print_unix_dialog_get_selected_printer(dialog);
				IF (printer # 0) & (GP.gtk_printer_is_virtual(printer) # 0) THEN
					INC(found); GLib.g_list_free(list); GP.gtk_dialog_response(dialog, -5); RETURN 0
				END
			END;
			item := item.next
		END;
		GLib.g_list_free(list); RETURN 1
	END Respond;

	PROCEDURE Arm (operation: INTEGER);
		VAR id: INTEGER;
	BEGIN mode := operation; ticks := 0; found := 0; configured := 0; id := GLib.g_timeout_add(100, Respond, 0) END Arm;

	PROCEDURE Page (p: Printers.Printer; IN title: ARRAY OF CHAR);
		VAR rd: Ports.Rider; w, h, x: INTEGER; font: Fonts.Font;
			points: ARRAY 4 OF Ports.Point;
	BEGIN
		p.OpenPage; Check(p.res = 0, 102); rd := p.ThisPort().NewRider();
		p.ThisPort().GetSize(w, h); rd.SetRect(0, 0, w, h);
		font := Fonts.dir.Default();
		rd.DrawString(40, 70, Ports.black, title, font);
		rd.DrawString(40, 100, Ports.blue, "Печать Linux — Unicode Ж", font);
		rd.DrawRect(40, 130, 180, 180, Ports.fill, Ports.red);
		rd.DrawOval(200, 130, 320, 180, 2, Ports.green);
		points[0].x := 40; points[0].y := 210;
		points[1].x := 100; points[1].y := 260;
		points[2].x := 180; points[2].y := 160;
		points[3].x := 240; points[3].y := 210;
		rd.DrawPath(points, 4, 2, Ports.blue, Ports.openBezier);
		font := Fonts.dir.This(font.typeface,font.size,{Fonts.underline},font.weight);
		rd.DrawString(40,300,Ports.black,"1231231",font);
		x := 40+font.StringWidth("1231231") DIV p.ThisPort().unit;
		rd.DrawSpace(x,300,40,Ports.black,font);
		rd.DrawString(x+40,300,Ports.black,"123123123",font);
		p.ClosePage; Check(p.res = 0, 103)
	END Page;

	PROCEDURE Run*;
		VAR variant: ARRAY 8 OF CHAR; p: Printers.Printer; copies, from, to, result, w, h: INTEGER; selection: BOOLEAN;
			model: TextModels.Model; view: TextViews.View; win: Windows.Window;
			par: Printing.Par; image: Views.View; encoded: GLib.PString; error: GLib.GError; filename: ARRAY 4096 OF SHORTCHAR; real: REAL; short: SHORTREAL;
			parse: Native.Parser;
	BEGIN
		Kernel.InstallTrapViewer(Trap);
		real := Native.strtod("1.25", 0); Check(real = 1.25, 118);
		short := Native.strtof("2.5", 0); Check(short = 2.5, 119);
		real := Native.strtod("1.25", 0)+Native.strtod("2.5", 0); Check(real = 3.75, 120);
		parse := Native.strtod; real := parse("3.5", 0); Check(real = 3.5, 121);
		Dialog.GetPar("BB_PRINT_DIR", base, result); Check(result = 0, 104);
		Check((Printers.dir # NIL) & Printers.dir.Available(), 105);
		p := Printers.dir.Current(); Check(p # NIL, 106);
		p.ThisPort().GetSize(w, h); Check((w > 500) & (h > 500), 107);
		Arm(0); StdDialog.PrintSetup; Check(found = 1, 108);
		Arm(3); StdDialog.PrintSetup; Check(found = 1, 130);
		p.ThisPort().GetSize(w, h); Check(w > h, 131); p.SetOrientation(FALSE);
		Arm(1); StdDialog.PrintDialog(FALSE, from, to, copies, selection); Check((found = 1) & (copies = 0), 109);
		LinPrinters.SetOutputFile(base+"/vector.pdf"); copies := 1; p.OpenJob(copies, "Vector"); Check(p.res = 0, 110);
		Page(p, "Vector portrait page"); p.SetOrientation(TRUE); Page(p, "Vector landscape page"); p.CloseJob; Check(p.res = 0, 111);
		model := TextModels.dir.NewFromString("Document pipeline — Печать Ж"); view := TextViews.dir.New(model); Views.OpenView(view);
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		Check(win # NIL, 112); Windows.dir.Select(win, FALSE); win(StdWindows.Window).backend.Select;
		LinPrinters.SetOutputFile(base+"/document.pdf"); par := Printing.NewDefaultPar("Document pipeline");
		Printing.PrintView(win.doc, par); Check(p.res = 0, 113);
		LinPrinters.SetOutputFile(base+"/copies.pdf"); par := Printing.NewDefaultPar("Copies");
		par := Printing.NewPar(par.page, par.header, par.footer, 2); Printing.PrintView(win.doc, par); Check(p.res = 0, 132);
		LinPrinters.SetOutputFile(base+"/document.pdf"); copies := 1; p.OpenJob(copies, "Abort"); Check(p.res = 0, 122);
		Page(p, "ABORTED PRINT JOB"); LinPrinters.Abort; Check((p.res = 1) & Printers.dir.Available(), 123);
		LinPrinters.SetOutputFile(base+"/missing/fail.pdf"); p.OpenJob(copies, "Invalid path"); Check(p.res # 0, 124); p.CloseJob;
		image := Views.OldView(Files.dir.This(base), "image.png"); Check(image # NIL, 125);
		LinPrinters.SetOutputFile(base+"/image.pdf"); Printing.PrintView(image, Printing.NewDefaultPar("PNG image")); Check(p.res = 0, 126);
		virtualName := ""; GP.gtk_enumerate_printers(S.ADR(FindVirtual), 0, 0, 1); Check(virtualName # "", 129);
		Dialog.GetPar("BB_PRINT_MENU", variant, result);
		IF (result = 0) & (variant = "1") THEN
			Utf.StringToUtf8(base+"/menu.pdf", filename, result); Check(result = 0, 133)
		ELSE
			Utf.StringToUtf8(base+"/gtk-file.pdf", filename, result); Check(result = 0, 127)
		END;
		error := NIL; encoded := GLib.g_filename_to_uri(S.VAL(GLib.PString, S.ADR(filename)), NIL, error); Check(encoded # NIL, 128);
		uri := encoded^$; GLib.g_free(S.VAL(GLib.gpointer, encoded));
		IF variant = "1" THEN
			Arm(4); StdCmds.Print; Check(p.res = 0, 135)
		ELSE
			Arm(2); StdDialog.PrintDialog(FALSE, from, to, copies, selection);
			Check(LinPrinters.IsVirtual() & (copies = 1) & (from = 0) & (to = 2), 114);
			p.SetOrientation(FALSE); p.OpenJob(copies, "GTK file backend"); Check(p.res = 0, 115);
			Page(p, "Range page ONE"); Page(p, "Range page TWO"); Page(p, "Range page THREE"); p.CloseJob; Check(p.res = 0, 116)
		END;
		win := Windows.dir.First(); WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
		Check(Kernel.trapCount = 0, 117);
		Console.WriteStr("Linux printing PDF, native dialogs, ranges and GTK file backend OK"); Console.WriteLn;
		StdCmds.Exit
	END Run;

END CheckLinPrinting.
