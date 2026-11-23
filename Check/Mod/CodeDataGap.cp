MODULE CheckCodeDataGap;
	VAR j:INTEGER;

PROCEDURE Do1*; BEGIN FOR j:=0 TO 100000000-1 DO END; INC(j) END Do1;
PROCEDURE Do2*; BEGIN FOR j:=0 TO 100000000-1 DO END; INC(j) END Do2;
PROCEDURE Do3*; BEGIN FOR j:=0 TO 100000000-1 DO END; INC(j) END Do3;
PROCEDURE Do4*; BEGIN FOR j:=0 TO 100000000-1 DO END; INC(j) END Do4;

END CheckCodeDataGap.

There must be a gap between the code and the data,
to prevent put of data to the processor cache,
which leads to situation, that cache outdating very fast

"CheckTimedExecute.Run('CheckCodeDataGap.Do1')"

"CheckTimedExecute.Run('CheckCodeDataGap.Do2')"

"CheckTimedExecute.Run('CheckCodeDataGap.Do3')"

Run very long if there is no optimization
because of gap absence between code and data 

"CheckTimedExecute.Run('CheckCodeDataGap.Do4')"