MODULE MdiConfig;

	IMPORT Kernel, Dialog, Converters, WinRasters, WinPorts;
	
	VAR
		res: INTEGER;
	
	PROCEDURE Setup*;
		VAR res: INTEGER; par: ARRAY 16 OF CHAR;
	BEGIN
		
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "utf8", {Converters.importAll});
		Converters.Register("StdTextConv.ImportText", "StdTextConv.ExportText", "TextViews.View", "txt", {});
		Converters.Register("StdTextConv.ImportUnicode", "StdTextConv.ExportUnicode", "TextViews.View", "utf", {});
		Converters.Register("StdTextConv.ImportDosText", "", "TextViews.View", "txt", {});
		Converters.Register("StdTextConv.ImportHex", "", "TextViews.View", "dat", {Converters.importAll});
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "xml", {});
		Converters.Register("StdTextConv.ImportUtf8", "StdTextConv.ExportUtf8", "TextViews.View", "html", {});
		Converters.Register("DevBrowser.ImportSymFile", "", "TextViews.View", "osf", {});
		Converters.Register("DevBrowser.ImportCodeFile", "", "TextViews.View", "ocf", {});
		Converters.Register("WinBitmaps.ImportBitmap", "WinBitmaps.ExportBitmap", "WinBitmaps.StdView", "bmp", {});
		Converters.Register("", "XhtmlExporter.ExportText", "TextViews.View", "html", {});
		Converters.Register("StdRasters.Import", "", "StdRasters.View", "png", {});
		
		
		
		Dialog.Call("Windows.Init", "", res);
		
		Dialog.Call("StdLog.Open", "", res);
		Dialog.Call("StdColorDialog.Init", "", res);
		WinRasters.Install;
		WinPorts.noGDIplus := Dialog.isWine;
		
		(*
			allows to catch erros if you compile app with 'dos' key like this
			DevLinker.Link dos MyApp.exe := ... 
			and then run it like this 
			MyApp.exe -DEBUG >log.txt 2>&1
		*)
		Dialog.GetPar("DEBUG", par, res);
		Kernel.debug := res = 0;
		
	END Setup;

END MdiConfig.
