MODULE CheckMultiline;

	(**
		
		StdStdCFrames
		
		"StdCmds.OpenToolDialog('Check/Rsrc/Multiline.odc', 'test')"
		
	**)

	IMPORT Strings, Dialog, Log;

	VAR
		test*: ARRAY 2048 OF CHAR;

	PROCEDURE Print*;
	BEGIN
		Log.String(test); Log.Ln;
	END Print;

	PROCEDURE AssignManyLines*;
	VAR i: INTEGER; str: ARRAY 16 OF CHAR;
	BEGIN
		test := "";
		FOR i := 0 TO 30 DO
			Strings.IntToString(i, str);
			test := test + "line " + str + 0DX + 0X
		END;
		Dialog.UpdateString(test);
	END AssignManyLines;

END CheckMultiline.

CheckMultiline.AssignManyLines

