MODULE ObxConfig;

	IMPORT Converters, Windows, StdCmds, StdWindows, StdDocuments, StdFilesBrowser;

	PROCEDURE Setup*;
	BEGIN
		StdWindows.Init;
		StdDocuments.Install;
		StdFilesBrowser.SetupPlaces;
		
		Converters.Register(
			"StdDocuments.ImportDocument", 
			"StdDocuments.ExportDocument", "", "odc", {});
		
		StdCmds.OpenAuxDialog("Obx/Rsrc/BlackBox", "BlackBox Dialog");
	END Setup;

END ObxConfig.



