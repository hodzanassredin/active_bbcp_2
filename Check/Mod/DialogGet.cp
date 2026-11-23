MODULE CheckDialogGet;

	IMPORT Dialog, Files, Log, StdFilesBrowser;

	TYPE
		Action = POINTER TO RECORD (Dialog.GetAction) END;

	(* usual way *)

	PROCEDURE Get*;
	VAR a: Action; loc: Files.Locator; name: Files.Name; path: Dialog.String;
	BEGIN
		NEW(a);
		Dialog.GetIntSpec("txt", loc, name);
		Dialog.GetLocPath(loc, path);
		Log.String("path: " +path); Log.Ln;
		Log.String("name: " + name); Log.Ln;
	END Get;

	(* async way *)

	PROCEDURE (a: Action) Do;
	VAR path: Dialog.String;
	BEGIN
		IF ~ a.cancel THEN
			Dialog.GetLocPath(a.loc, path);
			Log.String("path: " +path); Log.Ln;
			Log.String("name: " + a.name); Log.Ln;
		END
	END Do;

	PROCEDURE GetIntLocName*;
	VAR a: Action;
	BEGIN
		NEW(a);
		(* a.converterName := "StdTextConv.ImportText"; *)
		Dialog.GetIntLocName("txt,html", NIL, a)
	END GetIntLocName;
	
	PROCEDURE GetIntLocName2*;
	VAR a: Action;
	BEGIN
		NEW(a);
		Dialog.GetIntLocName("", NIL, a)
	END GetIntLocName2;

	PROCEDURE GetExtLocName*;
	VAR a: Action;
	BEGIN
		NEW(a);
		(* a.selectConverter := "StdTextConv.ExportText"; *)
		Dialog.GetExtLocName("без названия", "txt,utf8", Files.dir.This(""), a)
	END GetExtLocName;

	PROCEDURE GetLoc*;
	VAR a: Action;
	BEGIN
		NEW(a);
		Dialog.GetLoc(Files.dir.This(""), a)
	END GetLoc;

END CheckDialogGet.

CheckDialogGet.GetIntLocName

CheckDialogGet.GetIntLocName2

CheckDialogGet.GetIntLocName
