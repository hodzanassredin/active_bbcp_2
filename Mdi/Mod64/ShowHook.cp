MODULE MdiShowHook;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, Anton Dmitriev"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Dialog, Controllers, Windows, WinApi, WinWindows, WinDialog, WinGui;

	TYPE
		ShowHook = POINTER TO RECORD (Dialog.ShowHook) END;

	PROCEDURE (h: ShowHook) ShowParamMsg (IN str, p0, p1, p2: ARRAY OF CHAR);
	VAR res_: INTEGER; mapped: ARRAY 512 OF CHAR;
	BEGIN
		ASSERT(str # "", 20);
		Dialog.MapParamString(str, p0, p1, p2, mapped);
		res_ := WinGui.MessageBox(Dialog.appName, mapped, {4, 5});
	END ShowParamMsg;

	PROCEDURE (h: ShowHook) ShowParamStatus (IN str, p0, p1, p2: ARRAY OF CHAR);
		VAR res_: INTEGER; st: ARRAY 513 OF CHAR; w: Windows.Window;
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, st);
		st := " " + st;
		res_ := WinApi.SetWindowTextW(WinWindows.status, st);
		res_ := WinApi.UpdateWindow(WinWindows.status)
	END ShowParamStatus;

	PROCEDURE Init;
		VAR showHook: ShowHook;
	BEGIN
		NEW(showHook); Dialog.SetShowHook(showHook);
	END Init;

BEGIN
	Init
END MdiShowHook.