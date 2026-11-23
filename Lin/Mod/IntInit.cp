MODULE LinIntInit;

	IMPORT
		Kernel,
		StdInterpreter (* Dialog.SetCallHook *) ,
		LinConsole (* Console.SetHook *) ,
		
		ConsFonts (* Fonts.SetHook; required for Texts *),
		Windows,
		ConsWindows (* Windows.SetHook *),
		LinDates (* Dates.SetHook *),
		LinIntDialog (* Dialog.SetShowHook *),
		StdDialog (* Views.SetViewHook *),
		
		StdLog, ConsLog, (* Log.SetHook *)
		
		Converters (* .odc *),
		Dialog;

	PROCEDURE Init;
		VAR res: INTEGER;
	BEGIN
		
		Converters.Register("Documents.ImportDocument", "Documents.ExportDocument", "", "odc", {});
		
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "utf8", {Converters.importAll});
		
		Converters.Register("StdTextConv.ImportText", "StdTextConv.ExportText", "TextViews.View", "txt", {});
		
		Windows.Init;  (* neet to set hooks *)

		(* Пул рабочих нитей этапа 8: фасад AsyncWork и делегат pthread. *)
		Dialog.Call("AsyncWorkLin.Install", "Failed to start worker pool", res);

		Dialog.Call("ConsInterp.Run", " ", res)
		
	END Init;

BEGIN
	Init;
	Kernel.Quit(0)
END LinIntInit.
