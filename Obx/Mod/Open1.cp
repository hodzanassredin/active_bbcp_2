MODULE ObxOpen1;
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

	IMPORT Converters, Files, Views, Dialog, TextModels, TextMappers, TextViews;

	TYPE
		OpenAction = POINTER TO RECORD (Dialog.GetAction) END;
		SaveAction = POINTER TO RECORD (Dialog.GetAction)
			v: Views.View;
		END;

	PROCEDURE (a: SaveAction) Do;
	VAR res: INTEGER; conv: Converters.Converter; name: Files.Name;
	BEGIN
		IF ~a.cancel THEN
			IF a.converterName # "" THEN
				conv := Converters.list;
				WHILE (conv # NIL) & (conv.exp # a.converterName) DO conv := conv.next END
			ELSE
				conv := NIL
			END;
			name := a.name + "." + a.type;
			Views.Register(a.v, Views.dontAsk, a.loc, name, conv, res);
		END
	END Do;

	PROCEDURE (a: OpenAction) Do;
		VAR v: Views.View; t: TextModels.Model; f: TextMappers.Formatter; saveAction: SaveAction;
	BEGIN
		IF ~a.cancel THEN
			v := Views.OldView(a.loc, a.name);
			IF (v # NIL) & (v IS TextViews.View) THEN (* make sure it is a text view *)
				t := v(TextViews.View).ThisModel(); (* get the text view's model  *)
				f.ConnectTo(t);
				f.SetPos(t.Length());	(* set the formatter to the end of the text *)
				f.WriteString("appendix");	(* append a string *)
				NEW(saveAction);
				saveAction.v := v;
				Dialog.GetExtLocName("", "", NIL, saveAction)
			END
		END
	END Do;

	PROCEDURE Do*;	
		VAR action: OpenAction;
	BEGIN
		(* ask user for a file and open it as a view *)
		NEW(action);
		Dialog.GetIntLocName("odc", NIL, action)
	END Do;

END ObxOpen1.