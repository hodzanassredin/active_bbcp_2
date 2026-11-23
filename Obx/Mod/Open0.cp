MODULE ObxOpen0;
(**
	project	= "BlackBox2.0, diffs from 1.7.2 marked by green"
	organization	= "www.oberon.ch, blackbox.oberon.org"
	contributors	= "Oberon microsystems, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Files, Converters, Views, Dialog, TextModels, TextMappers, TextViews;

	TYPE
		OpenAction = POINTER TO RECORD (Dialog.GetAction) END;

	PROCEDURE (a: OpenAction) Do;
	VAR v: Views.View; t: TextModels.Model; f: TextMappers.Formatter;
	BEGIN
		IF ~a.cancel THEN
			v := Views.OldView(a.loc, a.name);
			IF (v # NIL) & (v IS TextViews.View) THEN (* make sure it is a text view *)
				t := v(TextViews.View).ThisModel(); (* get the text view's model  *)
				f.ConnectTo(t);
				f.SetPos(t.Length());	(* set the formatter to the end of the text *)
				f.WriteString("appendix");	(* append a string *)
				Views.OpenView(v)	(* open the text view in a window *)
			END
		END
	END Do;

	PROCEDURE Do*;
		VAR action: OpenAction;
	BEGIN
		(* ask user for a file and open it as a view *)
		NEW(action);
		Dialog.GetIntLocName("", NIL, action);
	END Do;

END ObxOpen0.