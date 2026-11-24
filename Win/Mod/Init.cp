MODULE WinInit;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	(** the first dynamically-loaded module *)

	IMPORT
		SYSTEM, Kernel, Services, Log, Dialog, Converters, Loop, Meta, 
	
	(* platform specific modules *)
		WinApi,
		WinConsole,
	
	(* Loading order *)
		
		Windows, StdWindows,
		StdInterpreter, 
		WinRegistry, (* provides StdRegistry.Hook *)
		WinFonts, StdStdCFrames, WinDates, WinMechanisms, 
		WinBackends,
		WinClipboard, 
		(*WinLang,*)
		StdCmds,
		WinCmds,
		
		WinDialog,
		WinShowHook
		(* ConsLog or StdLog before WinDialog will break status messages *)
		
	(* END loading order *)
		;

	PROCEDURE [1] FINIT 0DBH, 0E3H;
	PROCEDURE [1] FCLEX 0DBH, 0E2H;
	PROCEDURE [1] FLDCW 0D9H, 06DH, 0FCH;	(* -4, FP *)
	PROCEDURE [1] FSTCW 0D9H, 07DH, 0FCH;	(* -4, FP *)
	PROCEDURE [1] FSTSW 0DDH, 07DH, 0FCH;	(* -4, FP *)

	PROCEDURE CheckFpu;
		VAR cw: SET;
	BEGIN
		FSTCW;
		IF cw * {0..5, 8..11} # {1, 2, 3, 4, 5, 8, 9} THEN
			cw := cw - {0..5, 8..11} + {1, 2, 3, 4, 5, 8, 9};
			FCLEX; FLDCW
		END
	END CheckFpu;
	
	PROCEDURE WinStep;		(* adapted from MdiMenus.Loop *)
		VAR res_: INTEGER; msg: WinApi.MSG; f: SET;
	BEGIN
		IF WinApi.GetMessageW(msg, 0, 0, 0) # 0 THEN
			CheckFpu;
			res_ := WinApi.TranslateMessage(msg);
			res_ := WinApi.DispatchMessageW(msg)
		ELSE
			(* GetMessageW = 0 => WM_QUIT received *)
			Kernel.Quit(msg.wParam)	 (* never returns *)
		END
	END WinStep;
	
	PROCEDURE ValidateWindows;
	BEGIN
		StdWindows.Validate(NIL);
	END ValidateWindows;
	
	PROCEDURE ExitCondition(): BOOLEAN;
	BEGIN
		RETURN (Windows.dir.First() = NIL) & Dialog.exit
	END ExitCondition;

	PROCEDURE SearchVar (IN name: Kernel.Name; OUT cmd: Kernel.Name): BOOLEAN;
		VAR m: Kernel.Module; module, variable: Meta.Item; ok: BOOLEAN;
			var: RECORD (Meta.Value) cmd: Kernel.Name END;
	BEGIN
		m := Kernel.modList;
		ok := FALSE;
		WHILE (m # NIL) & ~ ok DO
			IF ~ (m.refcnt < 0) THEN
				Meta.Lookup(m.name$, module);
				IF module.obj = Meta.modObj THEN
					module.Lookup(name, variable);
					IF variable.obj = Meta.varObj THEN
						variable.GetVal(var, ok);
						IF ok THEN
							cmd := var.cmd
						END
					END
				END
			END;
			m := m.next
		END;
		RETURN ok
	END SearchVar;

	PROCEDURE Init;
		VAR res_: INTEGER; m: Kernel.Module; configCmd: Kernel.Name;
	BEGIN
		WinClipboard.Install;
		WinShowHook.SetShowHook;

		(* Пул рабочих нитей этапа 8 стартует лениво при первом
		   AsyncWork.Execute; базовый init его не заводит. *)

		Dialog.Call("WinGui.InstallCtrlBreakInterrupt", "Failed to set Ctrl+Break interrupt", res_);
		
		m := Kernel.ThisMod("DevDebug");
		IF m = NIL THEN
			Kernel.LoadMod("StdDebug")
		END;
		StdWindows.guardedCloseHook := StdCmds.CloseThis;
		
		(*Kernel.LoadMod("OleServer");*)
		
		IF ~ SearchVar("appConfigProcedure", configCmd) THEN
			configCmd := "StdConfig.Setup"
		END;
		
		Dialog.Call("WinConfig.SetupBefore", "Failed to call WinConfig.SetupBefore", res_);
		Dialog.Call(configCmd, "Failed to call " + configCmd, res_);
		Dialog.Call("WinConfig.SetupAfter", "Failed to call WinConfig.SetupAfter", res_);
		
		ASSERT(0 # WinApi.SetTimer(0, 1, Dialog.idlePeriod, NIL));
		(* Set a timer for no window (0) => WM_TIMER posted to thread queue => forces GetMessage to return and DispatchMessage to, well, ignore it since no window is specified => causes WinStep to return => causes Services.Actions to be processed *)
		
		Dialog.RequestExit(Dialog.exitWithoutWindows);
		Loop.quantum := Dialog.idlePeriod;
		Loop.Start(WinStep, ValidateWindows, ExitCondition)
	END Init;
		
BEGIN 
	Init;
	Kernel.Quit(0)
END WinInit.