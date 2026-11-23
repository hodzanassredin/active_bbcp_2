MODULE CheckLinScrollAuto;

	IMPORT Services, Kernel;

	TYPE Open = POINTER TO RECORD (Services.Action) END;

	VAR open: Open;

	PROCEDURE (a: Open) Do;
	VAR module: Kernel.Module; command: Kernel.Command;
	BEGIN
		module := Kernel.ThisMod("CheckLinScroll");
		IF module # NIL THEN command := Kernel.ThisCommand(module,"Open"); IF command # NIL THEN command() END END
	END Do;

BEGIN
	NEW(open); Services.DoLater(open,Services.Ticks()+500)
END CheckLinScrollAuto.
