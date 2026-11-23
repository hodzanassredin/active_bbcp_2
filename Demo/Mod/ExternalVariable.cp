MODULE DemoExternalVariable;

IMPORT Dialog, Log;

PROCEDURE Do*;
	VAR val: ARRAY 1024 OF CHAR; res: INTEGER;
BEGIN
	(* catch external variable NAME *)
	Dialog.GetPar("VAR_NAME", val, res);
	IF res = 0 THEN
		Log.String("got VAR_NAME = " + val); Log.Ln;
	ELSIF res = 2 THEN
		Log.String("VAR_NAME not found"); Log.Ln;
	ELSIF res = 3 THEN
		Log.String("VAR_NAME conversation error"); Log.Ln;
	END;
END Do;

END DemoExternalVariable.
