MODULE CheckSelectors;
(* off *)

IMPORT Log;

PROCEDURE Do*;
VAR p, res: INTEGER;
BEGIN
	
	Log.String("production"); Log.Ln;
	
END Do;

END CheckSelectors.

DevCompiler.CompileThis CheckSelectors(debug:on)
DevDebug.UnloadThis CheckSelectors
CheckSelectors.Do

DevCompiler.CompileThis CheckSelectors(debug:off)
DevDebug.UnloadThis CheckSelectors
CheckSelectors.Do