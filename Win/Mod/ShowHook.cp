MODULE WinShowHook;
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

	IMPORT Dialog, StdMenus, WinApi, WinFiles, WinGui;

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
		VAR mapped: ARRAY 512 OF CHAR; 
	BEGIN
		Dialog.MapParamString(str, p0, p1, p2, mapped);
		StdMenus.SetStatus(mapped)
	END ShowParamStatus;

	PROCEDURE SetShowHook*;
		VAR showHook: ShowHook;
	BEGIN
		NEW(showHook); Dialog.SetShowHook(showHook);
	END SetShowHook;

END WinShowHook.