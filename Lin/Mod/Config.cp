MODULE LinConfig;

	IMPORT LinDialog, LinRastersPng, LinPrinters;

	PROCEDURE SetupBefore*;
	BEGIN
		LinRastersPng.Install;
		LinPrinters.Install
	END SetupBefore;
	
	(* StdConfig.Setup calling between in LinInit.Init *)
	
	PROCEDURE SetupAfter*;
	BEGIN
		(* perform self diagnostics 
			LinDialog.GtkSelfCheck;
		*)
	END SetupAfter;

END LinConfig.
