MODULE ObsdConfig;

	IMPORT ObsdDialog, ObsdRastersPng;

	PROCEDURE SetupBefore*;
	BEGIN
		ObsdRastersPng.Install
	END SetupBefore;
	
	(* StdConfig.Setup calling between in LinInit.Init *)
	
	PROCEDURE SetupAfter*;
	BEGIN
		(* perform self diagnostics 
			LinDialog.GtkSelfCheck;
		*)
	END SetupAfter;

END ObsdConfig.
