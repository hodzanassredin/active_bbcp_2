MODULE WinGui;
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

	(* Any application using the GUI on Windows should has this module linked. *)

	IMPORT SYSTEM, Kernel, WinApi, WinFiles, WinKernel;
	
	VAR
		mainWnd*, unit*: INTEGER;
	
	
	PROCEDURE MessageBox* (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
		VAR res: INTEGER;
	BEGIN
		res := WinApi.MessageBoxW(mainWnd, msg, title, buttons);
		IF res = 0 THEN (* win2000/XP *)
			res := WinApi.LoadLibraryA("COMCTL32.dll");
			res := WinApi.MessageBoxW(mainWnd, msg, title, buttons)
		END;
		RETURN res
	END MessageBox;

	(* -------------------- keyboard interrupt handling --------------------- *)
	
	PROCEDURE KeyboardWatcher (main: INTEGER): INTEGER;	(* runs in a thread *)
		TYPE P = PROCEDURE (w: INTEGER): INTEGER;
		VAR isHungAppWindow: P; h: WinApi.HANDLE; res_: INTEGER;
			msg: WinApi.MSG; a: INTEGER; context: WinApi.CONTEXT; mod: Kernel.Module;
			top: INTEGER; needBreak: BOOLEAN;
	BEGIN
		isHungAppWindow := NIL;
		needBreak := FALSE;
		h := WinApi.LoadLibraryA("user32.dll");
		IF h # 0 THEN
			isHungAppWindow := SYSTEM.VAL(P, WinApi.GetProcAddress(h, "IsHungAppWindow"))
		END;
		IF isHungAppWindow # NIL THEN
			res_ := WinApi.RegisterHotKey(0, 1, WinApi.MOD_CONTROL, WinApi.VK_CANCEL);
			(* message loop *)
			WHILE WinApi.GetMessageA(msg, 0, 0, 0) # 0 DO
				IF msg.message = WinApi.WM_HOTKEY THEN
					IF msg.wParam = 1 THEN
						top := WinApi.GetForegroundWindow();
						IF (top # 0) & (isHungAppWindow(top) # 0)
							& (mainWnd # 0) & (isHungAppWindow(mainWnd) # 0) THEN
								needBreak := TRUE
						END
					END
				END;
				IF needBreak THEN
					res_ := WinApi.SuspendThread(main);
					context.ContextFlags := {0, 16};
					res_ := WinApi.GetThreadContext(main, context);
					mod := Kernel.modList; a := context.Eip;
					WHILE (mod # NIL) & ((a < mod.code) OR (a >= mod.code + mod.csize)) DO
						mod := mod.next
					END;
					IF (mod # NIL) & (mod.name = "Kernel") THEN mod := NIL END;
					IF mod # NIL THEN
						WinKernel.interrupted := TRUE;
						INCL(SYSTEM.VAL(SET, context.EFlags), 8);	(* set trap flag *)
						res_ := WinApi.SetThreadContext(main, context);
						needBreak := FALSE
					END;
					res_ := WinApi.ResumeThread(main);
					WinApi.Sleep(1);
					WinKernel.interrupted := FALSE
				END;
				res_ := WinApi.TranslateMessage(msg);
				res_ := WinApi.DispatchMessageA(msg);
				WinApi.Sleep(1)
			END;
			res_ := WinApi.UnregisterHotKey(0, 1)
		END;
		RETURN 0
	END KeyboardWatcher;

	PROCEDURE InstallCtrlBreakInterrupt*;
		VAR res, id: INTEGER; t, main: WinApi.HANDLE;
	BEGIN
		res := WinApi.DuplicateHandle(WinApi.GetCurrentProcess(), WinApi.GetCurrentThread(),
					WinApi.GetCurrentProcess(), main, {1, 3, 4, 16..19}, 0, {});
		t := WinApi.CreateThread(NIL, 4096, KeyboardWatcher, main, {}, id);
	END InstallCtrlBreakInterrupt;

	PROCEDURE Init;
	BEGIN
		WinFiles.MessageBox := NIL;
		WinKernel.SetMsgBoxProc(MessageBox);
	END Init;

BEGIN
	Init
END WinGui.