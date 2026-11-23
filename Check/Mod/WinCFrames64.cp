MODULE CheckWinCFrames64;

	IMPORT S := SYSTEM, Kernel, WinKernel, A := WinApi, WinGui, StdCFrames;

	VAR buttons, fields, clicks, edits, nativePointers: INTEGER;

	PROCEDURE Fail (code: INTEGER);
		VAR text: ARRAY 64 OF CHAR;
	BEGIN
		text := "WinCFrames x64 FAILED"; WinKernel.WkWrite(S.ADR(text), LEN(text$));
		Kernel.Quit(code)
	END Fail;

	PROCEDURE Click (f: StdCFrames.PushButton);
	BEGIN INC(clicks) END Click;

	PROCEDURE Edit (f: StdCFrames.Field; IN text: ARRAY OF CHAR);
	BEGIN IF text = "Native Ж" THEN INC(edits) END END Edit;

	PROCEDURE [ms64] Inspect (wnd: A.HWND; data: A.LPARAM): A.BOOL;
		VAR name: ARRAY 64 OF CHAR; result: INTEGER; frame: StdCFrames.Frame;
			child, ignored: S.ADDRESS;
			oldClick: PROCEDURE (f: StdCFrames.PushButton);
			oldEdit: PROCEDURE (f: StdCFrames.Field; IN text: ARRAY OF CHAR);
	BEGIN
		result := A.GetClassNameW(wnd, name, LEN(name));
		IF name = "Oberon Ctrl" THEN
			frame := S.VAL(StdCFrames.Frame, A.GetWindowLongPtrW(wnd, 0));
			IF frame = NIL THEN Fail(111) END;
			ignored := A.SetWindowLongPtrW(wnd, 0, 4294967297L);
			IF A.GetWindowLongPtrW(wnd, 0) # 4294967297L THEN Fail(114) END;
			ignored := A.SetWindowLongPtrW(wnd, 0, S.VAL(S.ADDRESS, frame));
			INC(nativePointers);
			child := A.GetWindow(wnd, 5); IF child = 0 THEN Fail(112) END;
			WITH frame: StdCFrames.PushButton DO
				INC(buttons);
				IF ~frame.disabled THEN
					oldClick := frame.Do; frame.Do := Click;
					ignored := A.SendMessageW(wnd, A.WM_COMMAND, A.BN_CLICKED * 65536, child);
					frame.Do := oldClick
				END
			| frame: StdCFrames.Field DO
				INC(fields);
				IF ~frame.disabled & ~frame.readOnly THEN
					oldEdit := frame.Set; frame.Set := Edit;
					result := A.SetWindowTextW(child, "Native Ж");
					ignored := A.SendMessageW(wnd, A.WM_COMMAND, A.EN_CHANGE * 65536, child);
					frame.Set := oldEdit; frame.Update
				END
			ELSE
			END
		END;
		RETURN 1
	END Inspect;

	PROCEDURE [ms64] InspectWindow (wnd: A.HWND; data: A.LPARAM): A.BOOL;
		VAR result: INTEGER;
	BEGIN
		result := A.EnumChildWindows(wnd, Inspect, 0); RETURN 1
	END InspectWindow;

	PROCEDURE Install*;
		VAR module: Kernel.Module;
	BEGIN
		module := Kernel.ThisMod("WinCFrames"); IF module = NIL THEN Fail(110) END
	END Install;

	PROCEDURE Verify*;
		VAR result: INTEGER; text: ARRAY 64 OF CHAR;
	BEGIN
		buttons := 0; fields := 0; clicks := 0; edits := 0; nativePointers := 0;
		result := A.EnumChildWindows(WinGui.mainWnd, Inspect, 0);
		result := A.EnumThreadWindows(A.GetCurrentThreadId(), InspectWindow, 0);
		IF (buttons = 0) OR (fields = 0) OR (clicks = 0) OR (edits = 0)
			OR (nativePointers = 0) OR (Kernel.trapCount # 0) THEN Fail(113) END;
		text := "WinCFrames x64 native controls OK"; WinKernel.WkWrite(S.ADR(text), LEN(text$))
	END Verify;

END CheckWinCFrames64.
