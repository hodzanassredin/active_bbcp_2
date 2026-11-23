MODULE MdiConfig;

	IMPORT Kernel, Dialog, Converters, OleData, WinRasters, WinPorts;
	
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
		
		
		OleData.Register("OleData.ImportInfo", "OleData.ExportInfo", "BlackBox Info", "", {OleData.info});
		OleData.Register("OleData.ImportNative", "OleData.ExportNative", "BlackBox Data", "", {});
		OleData.Register("WinTextConv.ImportDRichText", "WinTextConv.ExportDRichText", "Rich Text Format", "TextViews.View", {});
		OleData.Register("OleClient.ImportInfo", "OleClient.ExportInfo", "Object Descriptor", "", {OleData.info});
		OleData.Register("", "OleClient.Export", "Embedded Object", "OleClient.View", {OleData.storage});
		OleData.Register("OleClient.Import", "OleServer.Export", "Embed Source", "", {OleData.storage});
		OleData.Register("OleClient.Import", "", "Embedded Object", "", {OleData.storage});
		OleData.Register("WinTextConv.ImportDUnicode", "WinTextConv.ExportDUnicode", "UNICODETEXT", "TextViews.View", {});
		OleData.Register("WinTextConv.ImportDText", "WinTextConv.ExportDText", "TEXT", "TextViews.View", {});
		OleData.Register("WinPictures.ImportDPict", "WinPictures.ExportDPict", "METAFILEPICT", "WinPictures.View", {});
		OleData.Register("WinBitmaps.ImportDBitmap", "WinBitmaps.ExportDBitmap", "BITMAP", "WinBitmaps.View", {});
		OleData.Register("WinBitmaps.ImportDPictAsBitmap", "", "METAFILEPICT", "WinBitmaps.View", {});
		OleData.Register("", "OleData.ExportPicture", "METAFILEPICT", "", {});
		
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
