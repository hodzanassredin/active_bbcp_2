MODULE CheckWinColor64;

	IMPORT S := SYSTEM, Kernel, Dialog, Windows, Views, Ports, WinKernel, WinGui, WinDlg, A := WinApi;

	TYPE Action = POINTER TO RECORD (Dialog.GetColorAction) END;

	VAR mode, closed, ticks: INTEGER; timer: S.ADDRESS;

	PROCEDURE Check (ok: BOOLEAN; code: INTEGER);
	BEGIN IF ~ok THEN Kernel.Quit(code) END END Check;

	PROCEDURE (a: Action) Do;
	BEGIN END Do;

	PROCEDURE [ms64] Inspect (wnd: A.HWND; data: A.LPARAM): A.BOOL;
		VAR name: ARRAY 32 OF CHAR; result: INTEGER; ignored: S.ADDRESS;
	BEGIN
		result := A.GetClassNameW(wnd, name, LEN(name));
		IF (name = "#32770") & (A.GetWindow(wnd, 4) = WinGui.mainWnd) THEN
			result := A.KillTimer(WinGui.mainWnd, timer); timer := 0; INC(closed);
			ignored := A.SendMessageW(wnd, A.WM_COMMAND, mode, 0); RETURN 0
		END;
		RETURN 1
	END Inspect;

	PROCEDURE [ms64] Tick (wnd: A.HWND; msg: INTEGER; id: S.ADDRESS; time: INTEGER);
		VAR result: INTEGER;
	BEGIN
		INC(ticks); Check(ticks < 50, 180);
		result := A.EnumThreadWindows(A.GetCurrentThreadId(), Inspect, 0)
	END Tick;

	PROCEDURE Arm (button: INTEGER);
	BEGIN
		mode := button; ticks := 0; closed := 0;
		timer := A.SetTimer(WinGui.mainWnd, 778, 100, Tick); Check(timer # 0, 181)
	END Arm;

	PROCEDURE CountWindows (): INTEGER;
		VAR n: INTEGER; w: Windows.Window;
	BEGIN
		n := 0; w := Windows.dir.First();
		WHILE w # NIL DO INC(n); w := Windows.dir.Next(w) END;
		RETURN n
	END CountWindows;

	PROCEDURE Run*;
		VAR color, n: INTEGER; set: BOOLEAN; a: Action; text: ARRAY 80 OF CHAR;
			record: WinDlg.CHOOSECOLORW;
	BEGIN
		Check(SIZE(WinDlg.CHOOSECOLORW) = 72, 182);
		Check(S.ADR(record.lpCustColors)-S.ADR(record) = 32, 183);
		Check(S.ADR(record.lCustData)-S.ADR(record) = 48, 184);
		Arm(1); Dialog.GetColor(Ports.RGBColor(17, 34, 51), color, set);
		Check(set & (color = Ports.RGBColor(17, 34, 51)) & (closed = 1) & (timer = 0), 185);
		Arm(2); Dialog.GetColor(Ports.red, color, set);
		Check(~set & (color = Ports.red) & (closed = 1) & (timer = 0), 186);
		Arm(1); Dialog.GetColor(Ports.defaultColor, color, set);
		Check(set & (color = A.GetSysColor(8)) & (closed = 1) & (timer = 0), 187);
		Check(Dialog.getcolorhook # NIL, 188);
		n := CountWindows(); NEW(a); Dialog.GetCol(Ports.blue, a);
		Check(CountWindows() = n+1, 189);
		Check(Kernel.trapCount = 0, 190);
		text := "Windows x64 native and framework color dialogs OK";
		WinKernel.WkWrite(S.ADR(text), LEN(text$))
	END Run;

END CheckWinColor64.
