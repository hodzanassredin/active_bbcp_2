MODULE CheckLinDragAuto64;

	IMPORT Kernel, Services;

	TYPE Start = POINTER TO RECORD (Services.Action) END;

	VAR start: Start;

	PROCEDURE (s: Start) Do;
		VAR module: Kernel.Module; command: Kernel.Command;
	BEGIN
		module := Kernel.ThisMod("CheckLinDrag64"); IF module = NIL THEN Kernel.Quit(81) END;
		command := Kernel.ThisCommand(module,"Start"); IF command = NIL THEN Kernel.Quit(82) END;
		command()
	END Do;
BEGIN
	NEW(start); Services.DoLater(start,Services.Ticks()+250)
END CheckLinDragAuto64.
