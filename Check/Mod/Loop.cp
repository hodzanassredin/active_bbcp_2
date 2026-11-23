MODULE CheckLoop;

(* infinite loop to test Ctrl+Break *)

PROCEDURE Do*;
BEGIN
	WHILE TRUE DO END;
END Do;

END CheckLoop.

CheckLoop.Do