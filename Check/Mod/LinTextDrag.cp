MODULE CheckLinTextDrag;

	IMPORT Log, Windows, Views, StdWindows, TextModels, TextViews, TextControllers;

	VAR source, target: TextModels.Model;

	PROCEDURE Open*;
		VAR view, sourceView: TextViews.View; win: Windows.Window;
	BEGIN
		source := TextModels.dir.NewFromString("Перетащи это выделение");
		sourceView := TextViews.dir.New(source); Views.OpenAux(sourceView, "Drag source");
		target := TextModels.dir.New(); view := TextViews.dir.New(target);
		Views.OpenAux(view, "Drag target"); StdWindows.Validate(NIL);
		win := Windows.dir.First();
		WHILE (win # NIL) & (win.doc.ThisView() # sourceView) DO win := Windows.dir.Next(win) END;
		IF win # NIL THEN
			Windows.dir.Select(win, FALSE); win(StdWindows.Window).backend.Select
		END;
		TextControllers.SetSelection(source, 0, source.Length());
		Log.String("Перетащи выделение из Drag source в Drag target. Ctrl — копирование, Esc — отмена."); Log.Ln
	END Open;

	PROCEDURE Report*;
	BEGIN
		IF (source # NIL) & (target # NIL) THEN
			Log.String("Drag source: "); Log.Int(source.Length());
			Log.String("; Drag target: "); Log.Int(target.Length()); Log.Ln
		END
	END Report;

END CheckLinTextDrag.
