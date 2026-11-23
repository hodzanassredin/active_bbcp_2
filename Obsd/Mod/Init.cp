MODULE ObsdInit;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	(** the first dynamically-loaded module *)

	IMPORT
		SYSTEM, Kernel, Services, Log, Dialog, Converters, Loop, Meta, 

	(* platform specific modules *)
		GLib := ObsdGtk2GLib,
		Gtk := ObsdGtk2Gtk,
		ObsdGui,	(* provides Kernel.GuiHook *)
		ObsdConsole,
		
	(* Loading order *)
		
		Windows, StdWindows,
		StdInterpreter,	(* provides Dialog.CallHook *)
		ObsdRegistry, (* provides StdRegistry.Hook *)
		ObsdFonts, StdStdCFrames, ObsdDates, ObsdMechanisms,
		ObsdBackends, 	(* provides StdWindows.WindowBackend *)
		ObsdClipboard,	(* registers smth for the clipboard to work in Gtk *)
		ObsdLang,	(* provides Dialog.LanguageHook *)
		StdCmds, 
		
		ObsdDialog	(* provides  Dialog.DialogHook, .GetSpecHook, .ExtCallHook *)
		
	(* END loading order *)
		;

	PROCEDURE [ccall] Fake (_: GLib.gpointer): GLib.gboolean;
	BEGIN
		RETURN 1
	END Fake;

	PROCEDURE FetchEvent;
		VAR res_: INTEGER;
	BEGIN
		res_ := Gtk.gtk_main_iteration();
	END FetchEvent;

	PROCEDURE ValidateWindows;
	BEGIN
		StdWindows.Validate(NIL);
	END ValidateWindows;

	PROCEDURE ExitCondition(): BOOLEAN;
	BEGIN
		RETURN (Windows.dir.First() = NIL) & Dialog.exit
	END ExitCondition;

	PROCEDURE SearchVar (IN name: Kernel.Name; OUT cmd: Kernel.Name): BOOLEAN;
		VAR m: Kernel.Module; module, variable: Meta.Item; ok: BOOLEAN;
			var: RECORD (Meta.Value) cmd: Kernel.Name END;
	BEGIN
		m := Kernel.modList;
		ok := FALSE;
		WHILE (m # NIL) & ~ ok DO
			IF ~ (m.refcnt < 0) THEN
				Meta.Lookup(m.name$, module);
				IF module.obj = Meta.modObj THEN
					module.Lookup(name, variable);
					IF variable.obj = Meta.varObj THEN
						variable.GetVal(var, ok);
						IF ok THEN
							cmd := var.cmd
						END
					END
				END
			END;
			m := m.next
		END;
		RETURN ok
	END SearchVar;

	PROCEDURE Init;
		VAR res_: INTEGER; m: Kernel.Module; configCmd: Kernel.Name;
	BEGIN
		ObsdClipboard.Install;
		
		m := Kernel.ThisMod("DevDebug");
		IF m = NIL THEN Kernel.LoadMod("StdDebug") END;
		StdWindows.guardedCloseHook := StdCmds.CloseThis;
		
		IF ~ SearchVar("appConfigProcedure", configCmd) THEN
			configCmd := "StdConfig.Setup"
		END;
		
		Dialog.Call("ObsdConfig.SetupBefore", "Failed to call ObsdConfig.SetupBefore", res_);
		Dialog.Call(configCmd, "Failed to call " + configCmd, res_);
		Dialog.Call("ObsdConfig.SetupAfter", "Failed to call ObsdConfig.SetupAfter", res_);
		
		res_ := GLib.g_timeout_add(Dialog.idlePeriod, Fake, 0);
		
		(* makes a GDK timeout event occur every idlePeriod (first param) milliseconds, which makes an iteration of the GTK main loop take place. Fake does nothing, actual processing is in the Loop below *)
		
		Dialog.RequestExit(Dialog.exitWithoutWindows);
		Loop.quantum := Dialog.idlePeriod;
		Loop.Start(FetchEvent, ValidateWindows, ExitCondition);
		
	END Init;

BEGIN
	Init;
	Kernel.Quit(0)
END ObsdInit.
