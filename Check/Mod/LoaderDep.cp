MODULE CheckLoaderDep;
	IMPORT State := CheckLoaderState;
	TYPE Base* = EXTENSIBLE RECORD value*: INTEGER END;
	VAR number*: INTEGER;
	PROCEDURE Twice* (n: INTEGER): INTEGER;
	BEGIN RETURN 2*n END Twice;
	PROCEDURE (VAR b: Base) Answer* (): INTEGER, NEW, EXTENSIBLE;
	BEGIN RETURN b.value END Answer;
BEGIN number := 21; INC(State.initialized)
CLOSE INC(State.terminated)
END CheckLoaderDep.
