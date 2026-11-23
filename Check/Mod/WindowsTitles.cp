MODULE CheckWindowsTitles;

IMPORT Log, Views, Windows;

PROCEDURE Do*;
VAR w: Windows.Window; title: Views.Title;
BEGIN
	w := Windows.dir.First();
	WHILE w # NIL DO
		w.GetTitle(title);
		Log.String(title); Log.Ln;
		w := Windows.dir.Next(w)
	END;
	
END Do;

END CheckWindowsTitles.

CheckWindowsTitles.Do

