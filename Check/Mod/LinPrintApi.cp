MODULE CheckLinPrintApi ["libc.so.6"];

	IMPORT SYSTEM;

	TYPE Parser* = PROCEDURE [ccall16] (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): REAL;

	PROCEDURE [ccall16] strtod* (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): REAL;
	PROCEDURE [ccall16] strtof* (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): SHORTREAL;

END CheckLinPrintApi.
