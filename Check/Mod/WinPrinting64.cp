MODULE CheckWinPrinting64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, WinGui, WinPrinters,
		Printers, StdDialog, WinPorts, Files, WinFiles;

	VAR mode, closed, ticks: INTEGER;
		timer: S.ADDRESS;
		emptyDoc: A.DOCINFOW;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
	BEGIN IF ~ok THEN Kernel.Quit(code) END END Check;

	PROCEDURE [ms64] InspectDialog (dialog: A.HWND; data: A.LPARAM): A.BOOL;
		VAR ignored: S.ADDRESS; name: ARRAY 64 OF CHAR; result: INTEGER;
	BEGIN
		result := A.GetClassNameW(dialog, name, LEN(name));
		IF (name = "#32770") & (A.GetWindow(dialog, 4) = WinGui.mainWnd) THEN
			result := A.KillTimer(WinGui.mainWnd, timer); timer := 0; INC(closed);
			ignored := A.SendMessageW(dialog, A.WM_COMMAND, mode, 0); RETURN 0
		END;
		RETURN 1
	END InspectDialog;

	PROCEDURE [ms64] CloseDialog (wnd: A.HWND; msg: INTEGER; id: S.ADDRESS; time: INTEGER);
		VAR result: INTEGER;
	BEGIN
		INC(ticks); IF ticks > 50 THEN Kernel.Quit(160) END;
		result := A.EnumThreadWindows(A.GetCurrentThreadId(), InspectDialog, 0)
	END CloseDialog;

	PROCEDURE Arm (button: INTEGER);
	BEGIN
		mode := button; closed := 0; ticks := 0;
		timer := A.SetTimer(WinGui.mainWnd, 777, 100, CloseDialog); Check(timer # 0, 161)
	END Arm;

	PROCEDURE Run*;
		VAR p: Printers.Printer; from, to, copies, w, h, result: INTEGER;
			selection: BOOLEAN; names, dm, adr: S.ADDRESS; offset: SHORTINT;
			device: A.PtrWSTR; nativeMode: A.PtrDEVMODEW; dc: A.HDC; info: A.DOCINFOW;
			output: ARRAY 1200 OF CHAR; loc: Files.Locator; text: ARRAY 80 OF CHAR;
	BEGIN
		Check(Printers.dir.Available(), 162); p := Printers.dir.Current(); Check(p # NIL, 163);
		WinPrinters.GetPage(p, w, h); Check((w > 0) & (h > 0), 164);
		Arm(2); StdDialog.PrintSetup; Check((closed = 1) & (timer = 0), 165);
		Arm(1); StdDialog.PrintSetup; Check((closed = 1) & (timer = 0), 166);
		Arm(2); copies := -1; StdDialog.PrintDialog(FALSE, from, to, copies, selection);
		Check((closed = 1) & (copies = 0), 167);
		Arm(1); StdDialog.PrintDialog(FALSE, from, to, copies, selection);
		Check((closed = 1) & (copies > 0) & (from = 0) & (to >= from), 168);
		WinPrinters.GetCurrent(names, dm); adr := A.GlobalLock(names); Check(adr # 0, 169);
		S.GET(adr+2, offset); device := S.VAL(A.PtrWSTR, adr+offset*SIZE(CHAR));
		(* Only exercise a job on the virtual PDF printer; abort before committing. *)
		Check(device^$ = "PDF", 170);
		nativeMode := S.VAL(A.PtrDEVMODEW, A.GlobalLock(dm)); Check(nativeMode # NIL, 176);
		dc := A.CreateDCW("WINSPOOL", device, NIL, nativeMode^);
		result := A.GlobalUnlock(dm); result := A.GlobalUnlock(names); Check(dc # 0, 177);
		loc := Files.dir.This(""); output := loc(WinFiles.Locator).path + "\printing-test.ps";
		info := emptyDoc; info.cbSize := SIZE(A.DOCINFOW);
		info.lpszDocName := "BlackBox x64 file output test"; info.lpszOutput := output;
		result := A.StartDocW(dc, info); Check(result > 0, 178);
		result := A.StartPage(dc); Check(result > 0, 179);
		result := A.TextOutW(dc, 100, 100, "BlackBox x64", 12); Check(result # 0, 180);
		result := A.EndPage(dc); Check(result > 0, 181);
		result := A.EndDoc(dc); Check(result > 0, 182); result := A.DeleteDC(dc);

		p := Printers.dir.Current(); copies := 1; p.OpenJob(copies, "BlackBox x64 print regression");
		Check(p.res = 0, 171); p.OpenPage; Check(p.res = 0, 172);
		result := A.TextOutW(p.ThisPort()(WinPorts.Port).dc, 100, 100, "BlackBox x64", 12);
		Check(result # 0, 173); p.ClosePage; Check(p.res = 0, 174);
		WinPrinters.printing.Cancel; p.CloseJob;
		Check(Kernel.trapCount = 0, 175);
		text := "Windows x64 print dialogs and aborted PDF job OK";
		WinKernel.WkWrite(S.ADR(text), LEN(text$))
	END Run;

END CheckWinPrinting64.
