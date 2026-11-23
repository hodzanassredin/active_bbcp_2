MODULE CheckLinPrintingAuto;

	IMPORT Kernel, Services;

	TYPE Action = POINTER TO RECORD (Services.Action) END;

	VAR action: Action;

	PROCEDURE (a: Action) Do;
		VAR module: Kernel.Module; command: Kernel.Command;
	BEGIN
		module := Kernel.ThisMod("CheckLinPrinting"); IF module = NIL THEN Kernel.Quit(98) END;
		command := Kernel.ThisCommand(module, "Run"); IF command = NIL THEN Kernel.Quit(99) END;
		command()
	END Do;

BEGIN
	NEW(action); Services.DoLater(action, Services.Ticks()+250)
END CheckLinPrintingAuto.
