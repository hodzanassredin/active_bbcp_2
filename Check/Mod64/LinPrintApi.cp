MODULE CheckLinPrintApi ["libc.so.6"];

	IMPORT SYSTEM;

	TYPE Parser* = PROCEDURE [ccall] (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): REAL;

	PROCEDURE [ccall] strtod* (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): REAL;
	PROCEDURE [ccall] strtof* (IN text: ARRAY [untagged] OF SHORTCHAR; tail: SYSTEM.ADDRESS): SHORTREAL;

END CheckLinPrintApi.
