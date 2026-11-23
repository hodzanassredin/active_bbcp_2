Unicode

DEFINITION Unicode;

	PROCEDURE IsAlpha (ch: CHAR): BOOLEAN;
	PROCEDURE IsAlphaNumeric (ch: CHAR): BOOLEAN;
	PROCEDURE IsIdent (ch: CHAR): BOOLEAN;
	PROCEDURE IsIdentStart (ch: CHAR): BOOLEAN;
	PROCEDURE IsLower (ch: CHAR): BOOLEAN;
	PROCEDURE IsNumeric (ch: CHAR): BOOLEAN;
	PROCEDURE IsUpper (ch: CHAR): BOOLEAN;
	PROCEDURE Lower (ch: CHAR): CHAR;
	PROCEDURE ToLower (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR);
	PROCEDURE ToUpper (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR);
	PROCEDURE Upper (ch: CHAR): CHAR;

END Unicode.


Module Unicode providing identification and upper/lower case transformation for 16-bit Unicode character set.

PROCEDURE Upper (ch: CHAR): CHAR
Conversion to uppercase characters. Handles the entire 16-bit Unicode character set. Character values that have no uppercase equivalent are returned unchanged.

PROCEDURE Lower (ch: CHAR): CHAR
Conversion to lowercase characters. Handles the entire 16-bit Unicode character set. Character values that have no lowercase equivalent are returned unchanged.

PROCEDURE IsAlpha (ch: CHAR): BOOLEAN;
Returns TRUE if ch is a letter.

PROCEDURE IsUpper (ch: CHAR): BOOLEAN;
Returns TRUE if ch is an uppercase letter.

PROCEDURE IsLower (ch: CHAR): BOOLEAN;
Returns TRUE if ch is a lowercase letter.

PROCEDURE IsNumeric (ch: CHAR): BOOLEAN;
Returns TRUE if ch is a digit.

PROCEDURE IsAlphaNumeric (ch: CHAR): BOOLEAN;
Returns TRUE if ch is it a letter or a digit.

PROCEDURE IsIdentStart (ch: CHAR): BOOLEAN;
Returns TRUE if ch can be the first character of a Component Pascal identifier.

PROCEDURE IsIdent (ch: CHAR): BOOLEAN;
Returns TRUE if ch can be a second or further character of a Component Pascal identifier.

PROCEDURE ToLower (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR)
Converts string in to lowercase characters and returns the result in out. Handles the entire 16-bit Unicode character set. Character values that have no lowercase equivalent are unchanged. The same actual parameter may be passed for in and out.

Pre
Valid(in)	index trap

Post
Valid(out)

PROCEDURE ToUpper (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR)
Converts string in  to uppercase characters and returns the result in out. Handles the entire 16-bit Unicode character set. Character values that have no uppercase equivalent are unchanged. The same actual parameter may be passed for in and out.

Pre
Valid(in)	index trap

Post
Valid(out)

