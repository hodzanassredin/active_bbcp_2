Utf

DEFINITION Utf;

	PROCEDURE StringToUtf8 (IN in: ARRAY OF CHAR;
		OUT out: ARRAY OF SHORTCHAR; OUT res: INTEGER);
	PROCEDURE Utf8ToString (IN in: ARRAY OF SHORTCHAR;
		OUT out: ARRAY OF CHAR; OUT res: INTEGER);

END Utf.

Module Utf providing conversion for Utf8 format.