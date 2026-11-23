MODULE WinConfig;
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

	IMPORT OleData, WinRasters, StdCmds, WinPorts, Dialog;

	PROCEDURE SetupBefore*;
	BEGIN
		WinRasters.Install;
		WinPorts.noGDIplus := Dialog.isWine;
	END SetupBefore;
	
	(* StdConfig.Setup calling between in LinInit.Init *)
	
	PROCEDURE SetupAfter*;
	VAR par: ARRAY 1024 OF CHAR; res: INTEGER;
	BEGIN
		OleData.Register("OleData.ImportInfo", "OleData.ExportInfo", "BlackBox Info", "", {OleData.info});
		OleData.Register("OleData.ImportNative", "OleData.ExportNative", "BlackBox Data", "", {});
		OleData.Register("WinTextConv.ImportDRichText", "WinTextConv.ExportDRichText", "Rich Text Format", "TextViews.View", {});
		(*OleData.Register("OleClient.ImportInfo", "OleClient.ExportInfo", "Object Descriptor", "", {OleData.info});
		OleData.Register("", "OleClient.Export", "Embedded Object", "OleClient.View", {OleData.storage});
		OleData.Register("OleClient.Import", "OleServer.Export", "Embed Source", "", {OleData.storage});
		OleData.Register("OleClient.Import", "", "Embedded Object", "", {OleData.storage});*)
		OleData.Register("WinTextConv.ImportDUnicode", "WinTextConv.ExportDUnicode", "UNICODETEXT", "TextViews.View", {});
		OleData.Register("WinTextConv.ImportDText", "WinTextConv.ExportDText", "TEXT", "TextViews.View", {});
		OleData.Register("WinPictures.ImportDPict", "WinPictures.ExportDPict", "METAFILEPICT", "WinPictures.View", {});
		(*OleData.Register("WinBitmaps.ImportDBitmap", "WinBitmaps.ExportDBitmap", "BITMAP", "WinBitmaps.View", {});
		OleData.Register("WinBitmaps.ImportDPictAsBitmap", "", "METAFILEPICT", "WinBitmaps.View", {});*)
		OleData.Register("", "OleData.ExportPicture", "METAFILEPICT", "", {});
		
		(* open document if it is passed as single paremiter in command line *)
		Dialog.GetPar("", par, res);
		IF res = 0 THEN
			StdCmds.OpenDoc(par)
		END;
	
	END SetupAfter;

END WinConfig.
