MODULE FbsdConfig;

	IMPORT FbsdDialog, FbsdRastersPng;

	PROCEDURE SetupBefore*;
	BEGIN
		FbsdRastersPng.Install
	END SetupBefore;
	
	(* StdConfig.Setup calling between in LinInit.Init *)
	
	PROCEDURE SetupAfter*;
	BEGIN
	END SetupAfter;

END FbsdConfig.
