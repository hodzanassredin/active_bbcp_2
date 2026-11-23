MODULE CheckLinGtk4;

	IMPORT S := SYSTEM, Kernel, Ports, Dialog, Files, Controllers, Properties, Console, Strings, Windows, Views, StdWindows, StdApi, StdCmds,
		TextModels, TextViews, TextControllers, TextCmds, FormViews, FormModels, Controls, Fonts, LinFonts, LP := LinPorts, LB := LinBackends, GU := LinGtk4Util, IO := LinGtk4IO, GU4 := LinGtk4Util, C := LinCairo, Gtk := LinGtk4Gtk, Gdk := LinGtk4Gdk, GLib := LinGtk2GLib, O := LinGtk2GObject, GP := LinGtk4Print, Fenv := LinFenv, Utf;

	VAR response, responseTicks, draws: INTEGER; responseType: S.ADDRESS;

	PROCEDURE Fail (code: INTEGER);
		VAR n: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(code,n); Console.WriteStr("Linux GTK4 GUI FAILED "); Console.WriteStr(n); Console.WriteLn;
		Kernel.Quit(code)
	END Fail;

	PROCEDURE Trap;
		VAR n: ARRAY 32 OF CHAR;
	BEGIN
		Console.WriteStr("GTK4 trap: "); Strings.IntToString(Kernel.err,n); Console.WriteStr(n); Console.WriteLn; Fail(100)
	END Trap;

	PROCEDURE Select (view: Views.View);
		VAR win: Windows.Window;
	BEGIN
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(83) END;
		Windows.dir.Select(win,FALSE); win(StdWindows.Window).backend.Select
	END Select;

	PROCEDURE Pixel (port: LP.Port; x,y: INTEGER): INTEGER;
		VAR surface: C.cairo_surface_t; cr: C.cairo_t; data: S.ADDRESS; value: INTEGER;
	BEGIN
		surface := C.cairo_image_surface_create(C.CAIRO_FORMAT_ARGB32,1,1); cr := C.cairo_create(surface);
		C.cairo_translate(cr,-x,-y); port.Paint(cr); C.cairo_surface_flush(surface);
		data := C.cairo_image_surface_get_data(surface);
		S.GET(data,value); C.cairo_destroy(cr); C.cairo_surface_destroy(surface);
		RETURN value MOD 1000000H
	END Pixel;

	PROCEDURE Drawing;
		VAR port: LP.Port; rd: LP.Rider; res,i,j,k,width,reference,actual: INTEGER; font,base: Fonts.Font; object: S.ADDRESS;
	BEGIN
		IF Gtk.gtk_get_major_version() # 4 THEN Fail(101) END;
		NEW(port); port.Init(LinFonts.pixel,FALSE); port.SetDA(Gtk.gtk_drawing_area_new());
		object := O.g_object_ref_sink(S.VAL(O.GObject,port.da));
		port.SetSize(64,64); port.SetPainting(TRUE); rd := port.NewRider(); rd.SetRect(0,0,64,64);
		rd.DrawRect(2,2,10,10,Ports.fill,Ports.black);
		IF Pixel(port,4,4) # 0 THEN Fail(102) END;
		rd.MarkRect(2,2,10,10,Ports.fill,Ports.invert,TRUE);
		IF Pixel(port,4,4) # 0FFFFFFH THEN Fail(103) END;
		rd.MarkRect(2,2,10,10,Ports.fill,Ports.invert,FALSE);
		IF Pixel(port,4,4) # 0 THEN Fail(104) END;
		rd.SaveRect(2,2,10,10,res); IF res # 0 THEN Fail(105) END;
		rd.DrawRect(2,2,10,10,Ports.fill,Ports.RGBColor(255,0,0));
		IF Pixel(port,4,4) # 0FF0000H THEN Fail(106) END;
		rd.RestoreRect(2,2,10,10,TRUE); IF Pixel(port,4,4) # 0 THEN Fail(107) END;
		rd.Scroll(10,0); IF Pixel(port,14,4) # 0 THEN Fail(108) END;
		base := Fonts.dir.Default();
		FOR k := 0 TO 2 DO
			font := Fonts.dir.This(base.typeface,base.size+k*3*Ports.point,{Fonts.underline},base.weight);
			width := font.StringWidth(" ") DIV LinFonts.pixel;
			rd.DrawRect(0,0,64,64,Ports.fill,Ports.white);
			rd.DrawString(4,30,Ports.black," ",font);
			rd.DrawSpace(20,30,12,Ports.black,font);
			res := 0;
			FOR j := 20 TO 45 DO
				reference := Pixel(port,4+width DIV 2,j);
				IF reference # 0FFFFFFH THEN INC(res) END;
				FOR i := 22 TO 29 DO
					actual := Pixel(port,i,j);
					IF actual # reference THEN Fail(144) END
				END
			END;
			IF res = 0 THEN Fail(145) END
		END;
		O.g_object_unref(S.VAL(O.GObject,port.da))
	END Drawing;

	PROCEDURE [ccall] Respond (data: GLib.gpointer): GLib.gboolean;
		VAR list,item: GLib.GList; dialog: S.ADDRESS; rgba: Gtk.RGBA;
	BEGIN
		INC(responseTicks); IF responseTicks > 100 THEN Fail(113) END;
		list := GP.gtk_window_list_toplevels(); item := list;
		WHILE item # NIL DO
			dialog := S.VAL(S.ADDRESS,item.data);
			IF (O.g_type_check_instance_is_a(dialog,responseType) # 0) & (GP.gtk_widget_get_visible(dialog) # 0) THEN
				IF response = Gtk.GTK_RESPONSE_OK THEN
					rgba.red := SHORT(0.2); rgba.green := SHORT(0.4); rgba.blue := SHORT(0.6); rgba.alpha := 1;
					Gtk.gtk_color_chooser_set_rgba(S.VAL(Gtk.GtkDialog,dialog),rgba)
				END;
				GLib.g_list_free(list); GP.gtk_dialog_response(dialog,response); RETURN 0
			END;
			item := item.next
		END;
		GLib.g_list_free(list); RETURN 1
	END Respond;

	PROCEDURE Arm (type: S.ADDRESS; answer: INTEGER);
		VAR id: INTEGER;
	BEGIN responseTicks := 0; responseType := type; response := answer; id := GLib.g_timeout_add(100,Respond,0) END Arm;

	PROCEDURE ControlWord (): INTEGER;
		VAR environment: Fenv.Environment; res,word: INTEGER;
	BEGIN
		res := Fenv.feholdexcept(environment); IF res # 0 THEN Fail(119) END;
		word := environment[0]+256*environment[1];
		res := Fenv.fesetenv(environment); IF res # 0 THEN Fail(120) END;
		RETURN word
	END ControlWord;

	PROCEDURE Dialogs;
		VAR color,size,weight,cw: INTEGER; set: BOOLEAN; face: Fonts.Typeface; style: SET; loc: Files.Locator; name: Files.Name;
	BEGIN
		Arm(Gtk.gtk_color_chooser_dialog_get_type(),Gtk.GTK_RESPONSE_OK);
		Dialog.GetColor(Ports.black,color,set);
		IF ~set OR (color # Ports.RGBColor(51,102,153)) THEN Fail(114) END;
		Arm(Gtk.gtk_color_chooser_dialog_get_type(),Gtk.GTK_RESPONSE_CANCEL);
		Dialog.GetColor(Ports.red,color,set); IF set OR (color # Ports.red) THEN Fail(115) END;
		Arm(Gtk.gtk_font_chooser_dialog_get_type(),Gtk.GTK_RESPONSE_CANCEL);
		face := "Sans"; size := 12*Ports.point; weight := Fonts.normal; style := {}; color := Ports.black;
		Dialog.GetFont(TRUE,face,size,color,weight,style,set,"GTK4 font"); IF set THEN Fail(116) END;
		loc := Files.dir.This("");
		cw := ControlWord();
		Console.WriteStr("GTK4 open dialog"); Console.WriteLn;
		Arm(Gtk.gtk_file_chooser_dialog_get_type(),Gtk.GTK_RESPONSE_CANCEL);
		Dialog.GetIntSpec("",loc,name); IF name # "" THEN Fail(117) END;
		loc := Files.dir.This("");
		Arm(Gtk.gtk_file_chooser_dialog_get_type(),Gtk.GTK_RESPONSE_CANCEL);
		Console.WriteStr("GTK4 save dialog"); Console.WriteLn;
		Dialog.GetExtSpec("gtk3-test","txt",loc,name); IF name # "" THEN Fail(118) END;
		IF ControlWord() # cw THEN Fail(121) END
	END Dialogs;

	PROCEDURE ResizeTo (window: Gtk.GtkWindow; width,height: INTEGER);
		VAR i,res: INTEGER; n: ARRAY 32 OF CHAR;
	BEGIN
		Gtk.gtk_window_set_default_size(window,width,height);
		FOR i := 0 TO 40 DO res := GU4.Iterate(0); GLib.g_usleep(10000) END;
		IF (Gtk.gtk_widget_get_allocated_width(window) # width) OR (Gtk.gtk_widget_get_allocated_height(window) # height) THEN
			Strings.IntToString(Gtk.gtk_widget_get_allocated_width(window),n); Console.WriteStr("Actual window width: "); Console.WriteStr(n);
			Strings.IntToString(Gtk.gtk_widget_get_allocated_height(window),n); Console.WriteStr(" height: "); Console.WriteStr(n); Console.WriteLn;
			Fail(122)
		END
	END ResizeTo;

	PROCEDURE Resizing;
		VAR window: Gtk.GtkWindow; width,height,i,res: INTEGER;
	BEGIN
		window := LB.first; Gtk.gtk_window_unmaximize(window);
		FOR i := 0 TO 20 DO res := GU4.Iterate(0); GLib.g_usleep(10000) END;
		width := Gtk.gtk_widget_get_allocated_width(window); height := Gtk.gtk_widget_get_allocated_height(window);
		ResizeTo(window,width+80,height+60);
		ResizeTo(window,MAX(200,width-80),MAX(160,height-60)); ResizeTo(window,width,height)
	END Resizing;

	PROCEDURE ToolSize (form: Views.View);
		VAR win: Windows.Window; backend: LB.Backend; i,res: INTEGER;
	BEGIN
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # form) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(125) END;
		backend := win(StdWindows.Window).backend(LB.Backend);
		FOR i := 0 TO 40 DO res := GU4.Iterate(0); GLib.g_usleep(10000) END;
		IF (Gtk.gtk_widget_get_allocated_width(backend.wnd) < 300) OR (Gtk.gtk_widget_get_allocated_height(backend.wnd) < 120) THEN Fail(127) END
	END ToolSize;

	PROCEDURE FindVisible (form: Views.View);
		VAR model: TextModels.Model; view: TextViews.View; win: Windows.Window;
			f,g: Views.Frame; i,res,beg,end,x,y,before,marked,org,dy: INTEGER;
			left,right: TextViews.Location;
	BEGIN
		model := TextModels.dir.NewFromString("first needle, second needle");
		view := TextViews.dir.New(model); Views.OpenView(view); Select(view);
		TextControllers.SetCaret(model,0); StdWindows.Validate(NIL);
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(133) END;
		Select(form); StdWindows.Validate(NIL);
		FOR i := 0 TO 40 DO res := GU4.Iterate(0); GLib.g_usleep(10000) END;
		f := win(StdWindows.Window).frame;
		WHILE (f # NIL) & (f.view # view) DO
			g := Views.FrameAt(f,50*f.unit,50*f.unit); f := g
		END;
		IF f = NIL THEN Fail(134) END;
		view.GetThisLocation(f,6,left); view.GetThisLocation(f,12,right);
		x := (f.gx+(left.x+right.x) DIV 2) DIV f.unit; y := (f.gy+left.y) DIV f.unit;
		before := Pixel(win.port(LP.Port),x,y); view.PollOrigin(org,dy);
		IF org # 0 THEN Fail(138) END;
		TextCmds.find.find := "needle"; TextCmds.find.ignoreCase := FALSE;
		Controllers.SetCurrentPath(Controllers.targetPath);
		IF TextControllers.Focus() # view.ThisController() THEN Fail(137) END;
		TextCmds.FindFirst("~R"); Controllers.ResetCurrentPath;
		view.ThisController()(TextControllers.Controller).GetSelection(beg,end);
		IF (beg # 6) OR (end # 12) THEN Fail(135) END;
		IF ~f.mark THEN Fail(136) END;
		marked := Pixel(win.port(LP.Port),x,y);
		IF marked = before THEN Fail(139) END;
		Controllers.SetCurrentPath(Controllers.targetPath);
		TextCmds.FindFirst("~R"); Controllers.ResetCurrentPath;
		IF Pixel(win.port(LP.Port),x,y) # marked THEN Fail(140) END;
		Controllers.SetCurrentPath(Controllers.targetPath);
		TextCmds.FindAgain("~R"); Controllers.ResetCurrentPath;
		view.ThisController()(TextControllers.Controller).GetSelection(beg,end);
		IF (beg # 21) OR (end # 27) THEN Fail(141) END;
		IF Pixel(win.port(LP.Port),x,y) # before THEN Fail(142) END;
		view.PollOrigin(org,dy); IF org # 0 THEN Fail(143) END;
		Console.WriteStr("Visible first/repeated/next Find selection pixels OK without scrolling"); Console.WriteLn
	END FindVisible;

	PROCEDURE ScrollEnd;
		VAR model: TextModels.Model; view: TextViews.View; win: Windows.Window;
			text: ARRAY 8201 OF CHAR; i,j,k,org,dy,last,res,rx,ry,handled: INTEGER;
			 reference: GLib.gpointer; number: ARRAY 32 OF CHAR;
	BEGIN
		k := 0;
		FOR i := 0 TO 199 DO
			FOR j := 0 TO 39 DO text[k] := CHR(ORD("a")+j MOD 26); INC(k) END;
			text[k] := TextModels.line; INC(k)
		END; text[k] := 0X;
		model := TextModels.dir.NewFromString(text); view := TextViews.dir.New(model); Views.OpenView(view); Select(view);
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # view) DO win := Windows.dir.Next(win) END;
		IF win = NIL THEN Fail(129) END;
		StdWindows.Validate(win(StdWindows.Window));
		FOR i := 0 TO 249 DO
			StdWindows.Wheel(win(StdWindows.Window),Controllers.incLine,50,50,{});
			StdWindows.Validate(win(StdWindows.Window)); res := GU4.Iterate(0)
		END;
		view.PollOrigin(org,dy); Strings.IntToString(org,number); Console.WriteStr("Wheel end origin: "); Console.WriteStr(number); Console.WriteLn;
		IF org < model.Length()-82 THEN Fail(130) END;

	END ScrollEnd;

	PROCEDURE Verify*;
		VAR source,target: TextViews.View; model: TextModels.Model; form,view: Views.View;
			par: Dialog.Par; win: Windows.Window; rd: TextModels.Reader; reader: FormModels.Reader; ch: CHAR; i,controls,res: INTEGER; native: Gtk.PString; bytes: ARRAY 512 OF SHORTCHAR; external: ARRAY 128 OF CHAR; expected: ARRAY 32 OF CHAR;
	BEGIN
		Drawing;
		Kernel.InstallTrapViewer(Trap); IF ODD(ControlWord()) THEN Fail(150) END; Dialogs; Resizing; ScrollEnd;
		IF Windows.dir.First() = NIL THEN Fail(84) END;
		(* Incoming text is owned by the independent xclip process started by the runner. *)
		target := TextViews.dir.New(TextModels.dir.New()); Views.OpenView(target); Select(target);
		TextControllers.SetCaret(target.ThisModel(),0); StdCmds.Paste;
		i := 0;
		WHILE (target.ThisModel().Length() = 0) & (i < 1000) DO
			res := GU4.Iterate(0); GLib.g_usleep(1000); INC(i)
		END;
		expected := "External GTK4 — Ж";
		IF target.ThisModel().Length() # LEN(expected$) THEN Fail(109) END;
		rd := target.ThisModel().NewReader(NIL);
		FOR i := 0 TO LEN(expected$)-1 DO rd.ReadChar(ch); IF ch # expected[i] THEN Fail(110) END END;
		expected := "Linux  x64   GC Ж"; model := TextModels.dir.NewFromString(expected); source := TextViews.dir.New(model); Views.OpenView(source);
		Select(source); TextControllers.SetSelection(model,0,model.Length());
		StdWindows.Validate(NIL); Properties.IncEra;
		Controllers.SetCurrentPath(Controllers.targetPath);
		par.disabled := FALSE; StdCmds.UnderlineGuard(par); IF par.disabled THEN Fail(147) END;
		par.disabled := FALSE; StdCmds.ColorGuard(Ports.black,par); IF par.disabled THEN Fail(148) END;
		par.disabled := FALSE; StdCmds.TypefaceGuard(par); IF par.disabled THEN Fail(149) END;
		Controllers.ResetCurrentPath;
		StdCmds.Copy;
		native := GU4.ReadClipboardText();
		IF native = NIL THEN Fail(111) END;
		i := 0;
		WHILE (i < LEN(bytes)-1) & (native[i] # 0X) DO bytes[i] := native[i]; INC(i) END; bytes[i] := 0X;
		Utf.Utf8ToString(bytes,external,res); GLib.g_free(S.VAL(GLib.gpointer,native));
		IF (res # 0) OR (external # expected) THEN Fail(112) END;
		IF (SIZE(S.ADDRESS) = 8) & (S.VAL(S.ADDRESS,model) < 100000000L) THEN Fail(85) END;
		target := TextViews.dir.New(TextModels.dir.New()); Views.OpenView(target); Select(target);
		TextControllers.SetCaret(target.ThisModel(),0); StdCmds.Paste; StdWindows.Validate(NIL); Kernel.Collect;
		IF target.ThisModel().Length() # model.Length() THEN Fail(86) END;
		rd := target.ThisModel().NewReader(NIL);
		FOR i := 0 TO model.Length()-1 DO rd.ReadChar(ch); IF ch # expected[i] THEN Fail(87) END END;
		StdCmds.Undo; IF target.ThisModel().Length() # 0 THEN Fail(88) END;
		StdCmds.Redo; IF target.ThisModel().Length() # model.Length() THEN Fail(89) END;
		TextCmds.InitFindDialog;
		StdApi.OpenToolDialog("Text/Rsrc/Cmds","#Text:Find / Replace",form);
		IF (form = NIL) OR ~(form IS FormViews.View) THEN Fail(90) END;
		ToolSize(form); FindVisible(form);
		reader := form(FormViews.View).ThisModel().NewReader(NIL); controls := 0;
		LOOP
			reader.ReadView(view); IF view = NIL THEN EXIT END;
			IF view IS Views.Alien THEN Fail(91) END;
			IF view IS Controls.Control THEN INC(controls) END
		END;
		IF controls < 5 THEN Fail(92) END;
		StdWindows.Validate(NIL); Kernel.Collect;
		win := Windows.dir.First();
		WHILE win # NIL DO win.seq.SetDirty(FALSE); win := Windows.dir.Next(win) END;
		StdCmds.Exit; Console.WriteStr("Linux GTK4 GUI GC, clipboard, undo/redo and Find OK"); Console.WriteLn
	END Verify;
END CheckLinGtk4.
