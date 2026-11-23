MODULE CheckLinTextDragAuto;

	IMPORT Kernel, Services;

	TYPE Prepare = POINTER TO RECORD (Services.Action) END;

	VAR prepare: Prepare;

	PROCEDURE (p: Prepare) Do;
		VAR module: Kernel.Module; command: Kernel.Command;
	BEGIN
		module := Kernel.ThisMod("CheckLinTextDrag");
		IF module # NIL THEN
			command := Kernel.ThisCommand(module, "Open");
			IF command # NIL THEN command() END
		END
	END Do;

BEGIN
	NEW(prepare); Services.DoLater(prepare, Services.Ticks()+250)
END CheckLinTextDragAuto.
