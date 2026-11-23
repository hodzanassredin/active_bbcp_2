MODULE NbsdConfig;

	IMPORT NbsdDialog, NbsdRastersPng;

	PROCEDURE SetupBefore*;
	BEGIN
		NbsdRastersPng.Install
	END SetupBefore;
	
	(* StdConfig.Setup calling between in LinInit.Init *)
	
	PROCEDURE SetupAfter*;
	BEGIN
		(* perform self diagnostics 
			LinDialog.GtkSelfCheck;
		*)
	END SetupAfter;

END NbsdConfig.
