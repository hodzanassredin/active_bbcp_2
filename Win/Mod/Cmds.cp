MODULE WinCmds;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Views, Controllers, Dialog, Documents, Windows, StdCmds,
		HostFiles := WinFiles, HostClipboard := WinClipboard;


	(** Edit menu **)
	
	PROCEDURE PasteObject*;
	(** let focus document insert a copy of the clipboard's contents **)
		VAR ops: Controllers.PollOpsMsg; v: Views.View; w, h: INTEGER; s: BOOLEAN;
	BEGIN
		HostClipboard.cloneAttributes := FALSE;
		Controllers.PollOps(ops);
		IF Controllers.paste IN ops.valid THEN
			HostClipboard.GetClipView(ops.pasteType, v, w, h, s);
			IF v # NIL THEN
				Controllers.PasteView(v, w, h, TRUE)
			END
		END
	END PasteObject;

	PROCEDURE PasteToWindow*;
		VAR v: Views.View; w, h: INTEGER; d: Documents.Document; s: BOOLEAN;
	BEGIN
		HostClipboard.cloneAttributes := FALSE;
		HostClipboard.GetClipView("", v, w, h, s);
		IF v # NIL THEN
			d := Documents.dir.New(v, w, h);
			Views.OpenView(d)
		END
	END PasteToWindow;
	
	PROCEDURE OpenDoc* (file: ARRAY OF CHAR);
		VAR w: Windows.Window;
	BEGIN
		w := Windows.dir.Focus(FALSE);
		IF (w.loc # NIL) & (w.loc IS HostFiles.Locator) & (w.loc(HostFiles.Locator).path # "") THEN
			StdCmds.OpenDoc(w.loc(HostFiles.Locator).path + "\" + file)
		ELSE
			StdCmds.OpenDoc(file)
		END
	END OpenDoc;
	
	
	(* Guards *) 

	PROCEDURE PasteObjectGuard* (VAR par: Dialog.Par);
		VAR ops: Controllers.PollOpsMsg;
	BEGIN
		Controllers.PollOps(ops);
		IF ~(Controllers.paste IN ops.valid)
			OR ~HostClipboard.ConvertibleTo("") THEN par.disabled := TRUE END
	END PasteObjectGuard;
	
	PROCEDURE PasteToWindowGuard* (VAR par: Dialog.Par);
	BEGIN
		IF ~HostClipboard.ConvertibleTo("") THEN par.disabled := TRUE END
	END PasteToWindowGuard;


END WinCmds.
