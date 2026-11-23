MODULE CheckLinGtk4Auto;

	IMPORT Kernel, Services;

	TYPE Verify = POINTER TO RECORD (Services.Action) END;

	VAR verify: Verify;

	PROCEDURE (v: Verify) Do;
		VAR module: Kernel.Module; command: Kernel.Command;
	BEGIN
		module := Kernel.ThisMod("CheckLinGtk4"); IF module = NIL THEN Kernel.Quit(81) END;
		command := Kernel.ThisCommand(module,"Verify"); IF command = NIL THEN Kernel.Quit(82) END;
		command()
	END Do;
BEGIN
	NEW(verify); Services.DoLater(verify,Services.Ticks()+250)
END CheckLinGtk4Auto.
