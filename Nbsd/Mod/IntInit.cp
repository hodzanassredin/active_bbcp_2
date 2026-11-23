MODULE NbsdIntInit;

	IMPORT
		Kernel,
		StdInterpreter (* Dialog.SetCallHook *) ,
		NbsdConsole (* Console.SetHook *) ,
		
		ConsFonts (* Fonts.SetHook; required for Texts *),
		Windows,
		ConsWindows (* Windows.SetHook *),
		NbsdDates (* Dates.SetHook *),
		NbsdIntDialog (* Dialog.SetShowHook *),
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
		
		Dialog.Call("ConsInterp.Run", " ", res)
		
	END Init;

BEGIN
	Init;
	Kernel.Quit(0)
END NbsdIntInit.
