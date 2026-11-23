MODULE CheckInterpreter;

	(* check new StdInterpreter *)
	
	IMPORT Log;
	
	PROCEDURE LogReal* (r: REAL);
	BEGIN
		Log.Real(r); Log.Ln;
	END LogReal;
	
	PROCEDURE LogString* (str: ARRAY OF CHAR);
	BEGIN
		Log.String(str); Log.Ln;
	END LogString;

END CheckInterpreter.

"CheckInterpreter.LogReal(3.14)"

"CheckInterpreter.LogString(`check new type of quotes`)"