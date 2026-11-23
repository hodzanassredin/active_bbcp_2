MODULE CheckHeapSpyAuto;

	IMPORT Kernel, Services, Dialog;

	TYPE Verify = POINTER TO RECORD (Services.Action) END;

	VAR verify: Verify;

	PROCEDURE (v: Verify) Do;
		VAR module: Kernel.Module; command: Kernel.Command; name: ARRAY 32 OF CHAR; res: INTEGER;
	BEGIN
		module := Kernel.ThisMod("CheckHeapSpy"); IF module = NIL THEN Kernel.Quit(81) END;
		Dialog.GetPar("HEAPSPY_MANUAL",name,res); IF res # 0 THEN name := "Verify" ELSIF name = "4G" THEN name := "Open4G" ELSE name := "Open" END;
		command := Kernel.ThisCommand(module,name); IF command = NIL THEN Kernel.Quit(82) END;
		command()
	END Do;

BEGIN
	NEW(verify); Services.DoLater(verify,Services.Ticks()+250)
END CheckHeapSpyAuto.
