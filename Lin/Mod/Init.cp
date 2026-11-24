MODULE LinInit;
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
		SYSTEM, Kernel, Monitors, Services, Log, Dialog, Converters, Loop, Meta, 
		
	(* platform specific modules *)
		LinGtk4Bootstrap, GLib := LinGtk2GLib,
		GU := LinGtk4Util, Fenv := LinFenv, IO := LinGtk4IO, Gtk := LinGtk4Gtk,
		LinGui,	(* provides Kernel.GuiHook *)
		LinConsole,
		
	(* Loading order *)
		
		Windows, StdWindows,
		StdInterpreter,	(* provides Dialog.CallHook *)
		LinRegistry, (* provides StdRegistry.Hook *)
		LinFonts, StdStdCFrames, LinDates, LinMechanisms,
		LinBackends, (* provides StdWindows.WindowBackend *)
		LinClipboard, (* registers smth for the clipboard to work in Gtk *)
		LinLang, (* provides Dialog.LanguageHook *)
		StdCmds, 
		
		LinDialog (* provides  Dialog.DialogHook, .GetSpecHook, .ExtCallHook *)
		
	(* END loading order *)
		;

	PROCEDURE [ccall] Fake (_: GLib.gpointer): GLib.gboolean;
	BEGIN
		RETURN 1
	END Fake;

	PROCEDURE FetchEvent;
		VAR res_: INTEGER;
	BEGIN
		res_ := GU.Iterate(1);
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
		VAR res_: INTEGER; m: Kernel.Module; configCmd: Kernel.Name; environment: Fenv.Environment;
			runCmd: Kernel.Name;
	BEGIN
		res_ := Fenv.feholdexcept(environment);
		LinClipboard.Install;
		
		m := Kernel.ThisMod("DevDebug");
		IF m = NIL THEN Kernel.LoadMod("StdDebug") END;
		StdWindows.guardedCloseHook := StdCmds.CloseThis;
		
		IF ~ SearchVar("appConfigProcedure", configCmd) THEN
			configCmd := "StdConfig.Setup"
		END;
		
		Dialog.Call("LinConfig.SetupBefore", "Failed to call LinConfig.SetupBefore", res_);
		Dialog.Call(configCmd, "Failed to call " + configCmd, res_);
		Dialog.Call("LinConfig.SetupAfter", "Failed to call LinConfig.SetupAfter", res_);

		(* Тестовый хук: команда до главного цикла (например,
		   --BB_RUN=Demo_testsGui.Run). Она только заводит задачи и действия;
		   дальше работает Loop.Start. *)
		Dialog.GetPar("BB_RUN", runCmd, res_);
		IF res_ = 0 THEN
			Dialog.Call(runCmd, "Failed to call BB_RUN command", res_)
		END;
		
		res_ := GLib.g_timeout_add(Dialog.idlePeriod, Fake, 0);
		
		(* makes a GDK timeout event occur every idlePeriod (first param) milliseconds, which makes an iteration of the GTK main loop take place. Fake does nothing, actual processing is in the Loop below *)
		
		Dialog.RequestExit(Dialog.exitWithoutWindows);
		Loop.quantum := Dialog.idlePeriod;
		res_ := Fenv.fesetenv(environment);
		Loop.Start(FetchEvent, ValidateWindows, ExitCondition);
		
	END Init;

BEGIN
	Init;
	Kernel.Quit(0)
END LinInit.
