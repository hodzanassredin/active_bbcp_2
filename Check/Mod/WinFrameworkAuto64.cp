MODULE CheckWinFrameworkAuto64;
	(* Schedule verification without exporting appConfigProcedure: this executable
		exercises WinInit's default StdConfig.Setup branch. *)
	IMPORT Kernel, Services;
	TYPE Verify = POINTER TO RECORD (Services.Action) END;
	VAR verify: Verify;
	PROCEDURE (v: Verify) Do;
		VAR mod: Kernel.Module; command: Kernel.Command;
	BEGIN
		mod := Kernel.ThisMod("CheckWinFramework64");
		IF mod = NIL THEN Kernel.Quit(79) END;
		command := Kernel.ThisCommand(mod, "Verify");
		IF command = NIL THEN Kernel.Quit(80) END;
		command()
	END Do;
BEGIN
	NEW(verify); Services.DoLater(verify, Services.Ticks()+10)
END CheckWinFrameworkAuto64.
