MODULE MdiInit;
(**	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	(** the first dynamically-loaded module *)

	IMPORT Kernel, Dialog, Converters,
	
	(* Loading order *)
		StdInterpreter,
		WinRegistry, (* provides StdRegistry.Hook *)
		WinFonts, WinDates, WinMechanisms,
		
		WinConsole,
		WinDialog,
		MdiShowHook,
		(* note:
				don't put ConsLog or StdLog before MdiShowHook,
				it will break status messages
		*)
		WinClipboard,
		MdiMenus
		
	(* END loading order *)
		;

	PROCEDURE Init;
		VAR res: INTEGER; m: Kernel.Module;
	BEGIN
		WinClipboard.Install;
		
		Dialog.Call("WinGui.InstallCtrlBreakInterrupt", "Failed to set Ctrl+Break interrupt", res);
		
		MdiMenus.OpenApp;
		m := Kernel.ThisMod("DevDebug");
		IF m = NIL THEN
			Kernel.LoadMod("StdDebug")
		END;
		Converters.Register("Documents.ImportDocument", "Documents.ExportDocument", "", "odc", {});
		Dialog.Call("StdMenuTool.UpdateAllMenus", "", res);
		Kernel.LoadMod("OleServer");
		Dialog.Call("MdiConfig.Setup", "MdiConfig.Setup: ", res);
		MdiMenus.Run
	END Init;

BEGIN
	Init
END MdiInit.
