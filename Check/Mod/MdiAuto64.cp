MODULE CheckMdiAuto64;

	IMPORT Kernel, Services;

	TYPE Verify = POINTER TO RECORD (Services.Action) END;

	VAR verify: Verify;

	PROCEDURE (v: Verify) Do;
		VAR m: Kernel.Module; command: Kernel.Command;
	BEGIN
		m := Kernel.ThisMod("CheckMdiFramework64");
		IF m = NIL THEN Kernel.Quit(79) END;
		command := Kernel.ThisCommand(m, "Run");
		IF command = NIL THEN Kernel.Quit(80) END;
		command()
	END Do;

BEGIN
	NEW(verify); Services.DoLater(verify, Services.Ticks()+10)
END CheckMdiAuto64.
