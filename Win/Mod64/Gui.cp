MODULE WinGui;
	IMPORT SYSTEM, A := WinApi;
	VAR interruptEnabled: BOOLEAN; mainWnd*: SYSTEM.ADDRESS; unit*: INTEGER;
	PROCEDURE MessageBox* (title, msg: ARRAY OF CHAR; buttons: SET): INTEGER;
	BEGIN RETURN A.MessageBoxW(mainWnd, msg, title, buttons) END MessageBox;

	PROCEDURE InstallCtrlBreakInterrupt*;
	BEGIN interruptEnabled := TRUE END InstallCtrlBreakInterrupt;
	PROCEDURE [code] Interrupt 08DH, 0F0H, 0C8H;
	PROCEDURE CheckInterrupt* (IN msg: A.MSG);
	BEGIN
		(* Cooperative interruption on the runtime thread; no foreign CP thread. *)
		IF interruptEnabled & (msg.message = A.WM_KEYDOWN) & (msg.wParam = 3)
			& (A.GetKeyState(A.VK_CONTROL) < 0) THEN Interrupt END
	END CheckInterrupt;
END WinGui.
