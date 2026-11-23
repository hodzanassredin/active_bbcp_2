MODULE WinConfig;
	(* The standard configuration phases, with embedded OLE objects excluded. *)
	IMPORT WinRasters, StdCmds, WinPorts, Dialog;
	PROCEDURE SetupBefore*;
	BEGIN
		WinRasters.Install;
		WinPorts.noGDIplus := TRUE
	END SetupBefore;
	PROCEDURE SetupAfter*;
		VAR par: ARRAY 1024 OF CHAR; res: INTEGER;
	BEGIN
		Dialog.GetPar("", par, res);
		IF res = 0 THEN StdCmds.OpenDoc(par) END
	END SetupAfter;
END WinConfig.
