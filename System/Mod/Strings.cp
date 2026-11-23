MODULE Strings;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT Math;

	CONST
		charCode* = -1; decimal* = 10; hexadecimal* = -2; roman*= -3;
		digitspace* = 08FX;
		showBase* = TRUE; hideBase* = FALSE;
		minLongIntRev = "8085774586302733229";	(* reversed string of -MIN(LONGINT) *)

	VAR
		maxExp: INTEGER;
		maxDig: INTEGER;
		factor: REAL;	(* 10^maxDig *)
		digits: ARRAY 17 OF CHAR;
		decimalSign: CHAR;


	(* integer conversions *)

	PROCEDURE IntToString* (x: LONGINT; OUT s: ARRAY OF CHAR);
		VAR j, k: INTEGER; ch: CHAR; a: ARRAY 32 OF CHAR;
	BEGIN
		IF x # MIN(LONGINT) THEN
			IF x < 0 THEN s[0] := "-"; k := 1; x := -x ELSE k := 0 END;
			j := 0; REPEAT a[j] := CHR(x MOD 10 + ORD("0")); x := x DIV 10; INC(j) UNTIL x = 0
		ELSE
			a := minLongIntRev; s[0] := "-"; k := 1;
			j := 0; WHILE a[j] # 0X DO INC(j) END
		END;
		ASSERT(k + j < LEN(s), 23);
		REPEAT DEC(j); ch := a[j]; s[k] := ch; INC(k) UNTIL j = 0;
		s[k] := 0X
	END IntToString;

	PROCEDURE IntToStringForm* (x: LONGINT; form, minWidth: INTEGER; fillCh: CHAR;
														showBase: BOOLEAN; OUT s: ARRAY OF CHAR);
		VAR base, i, j, k, si: INTEGER; mSign: BOOLEAN; a: ARRAY 128 OF CHAR; c1, c5, c10: CHAR;
	BEGIN
		ASSERT((form = charCode) OR (form = hexadecimal) OR (form = roman) OR ((form >= 2) & (form <= 16)), 20);
		ASSERT(minWidth >= 0, 22);
		IF form = charCode THEN base := 16
		ELSIF form = hexadecimal THEN base := 16
		ELSE base := form
		END;
		
		IF form = roman THEN
			ASSERT((x > 0) & (x < 3999), 21); 
			base := 1000; i := 0; mSign := FALSE;
			WHILE (base > 0) & (x > 0) DO
				IF base = 1 THEN c1 := "I"; c5 := "V"; c10 := "X"
				ELSIF base = 10 THEN c1 := "X"; c5 := "L"; c10 := "C"
				ELSIF base = 100 THEN c1 := "C"; c5 := "D"; c10 := "M"
				ELSE c1 := "M"
				END;
				k := SHORT(x DIV base); x := x MOD base;
				IF k IN {4, 9} THEN a[i] := c1; INC(i) END;
				IF k IN {4 .. 8} THEN a[i] := c5; INC(i) END;
				IF k = 9 THEN a[i] := c10; INC(i)
				ELSIF k IN {1 .. 3, 6 .. 8} THEN
					j := k MOD 5;
					REPEAT a[i] := c1; INC(i); DEC(j) UNTIL j = 0
				END;
				base := base DIV 10
			END
		ELSIF (form = hexadecimal) OR (form = charCode) THEN
			i := 0; mSign := FALSE;
			IF showBase THEN DEC(minWidth) END;
			REPEAT
				a[i] := digits[x MOD base]; x := x DIV base; INC(i)
			UNTIL (x = 0) OR (x = -1) OR (i = LEN(a));
			IF x = -1 THEN fillCh := "F" END
		ELSE
			IF x < 0 THEN
				i := 0; mSign := TRUE; DEC(minWidth);
				REPEAT
					IF x MOD base = 0 THEN
						a[i] := digits[0]; x := x DIV base
					ELSE
						a[i] := digits[base - x MOD base]; x := x DIV base + 1
					END;
					INC(i)
				UNTIL (x = 0) OR (i = LEN(a))
			ELSE
				i := 0; mSign := FALSE;
				REPEAT
					a[i] := digits[x MOD base]; x := x DIV base; INC(i)
				UNTIL (x = 0) OR (i = LEN(a))
			END;
			IF showBase THEN DEC(minWidth);
				IF base < 10 THEN DEC(minWidth) ELSE DEC(minWidth,2) END
			END
		END;
		si := 0;
		IF mSign & (fillCh = "0") & (si < LEN(s)) THEN s[si] := "-"; INC(si); mSign := FALSE END;
		WHILE minWidth > i DO
			IF si < LEN(s) THEN s[si] := fillCh; INC(si) END;
			DEC(minWidth)
		END;
		IF mSign & (si < LEN(s)) THEN s[si] := "-"; INC(si) END;
		IF form = roman THEN
			j := 0;
			WHILE j < i DO 
				IF si < LEN(s) THEN s[si] := a[j]; INC(si) END; 
				INC(j)
			END
		ELSE
			REPEAT DEC(i);
				IF si < LEN(s) THEN s[si] := a[i]; INC(si) END
			UNTIL i = 0
		END;
		IF showBase & (form # roman) THEN
			IF (form = charCode) & (si < LEN(s)) THEN s[si] := "X"; INC(si)
			ELSIF (form = hexadecimal) & (si < LEN(s)) THEN s[si] := "H"; INC(si)
			ELSIF (form < 10) & (si < LEN(s)-1) THEN s[si] := "%"; s[si+1] := digits[base]; INC(si, 2)
			ELSIF (si < LEN(s) - 2) THEN
				s[si] := "%"; s[si+1] := digits[base DIV 10]; s[si+2] := digits[base MOD 10]; INC(si, 3)
			END
		END;
		IF si < LEN(s) THEN s[si] := 0X ELSE HALT(23) END
	END IntToStringForm;

	PROCEDURE StringToInt* (IN s: ARRAY OF CHAR; OUT x: INTEGER; OUT res: INTEGER);
		CONST hexLimit = MAX(INTEGER) DIV 8 + 1;
		VAR i, j, k, digits: INTEGER; ch, top: CHAR; neg: BOOLEAN; base: INTEGER;
	BEGIN
		res := 0; i := 0; ch := s[0];
		WHILE (ch # 0X) & (ch <= " ") OR (ch = 8BX) OR (ch = 8FX) OR (ch = 0A0X) DO	(* ignore leading blanks *)
			INC(i); ch := s[i]
		END;
		j := i; top := "0";
		WHILE (ch # 0X) & (ch # "H") & (ch # "X") & (ch # "%") DO 
			IF ch > top THEN top := ch END;
			INC(j); ch := s[j]
		END;
		IF (ch = "H") OR (ch = "X") THEN
			x := 0; ch := s[i];
			IF ("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F") THEN
				WHILE ch = "0" DO INC(i); ch := s[i] END;
				digits := 0;
				WHILE (res = 0) & (("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F")) DO
					IF ch < "A" THEN k := ORD(ch) - ORD("0")
					ELSE k := ORD(ch) - ORD("A") + 10
					END;
					IF digits < 8 THEN
						x := x MOD hexLimit;
						IF x >= hexLimit DIV 2 THEN x := x - hexLimit END;
						x := x * 16 + k; INC(i); ch := s[i]
					ELSE res := 1
					END;
					INC(digits)
				END;
				IF res = 0 THEN
					IF (ch # "H") & (ch # "X") OR (s[i+1] # 0X) THEN res := 2 END
				END
			ELSE res := 2
			END
		ELSE
			IF ch = "%" THEN
				INC(j); ch := s[j]; base := 0;
				IF ("0" <= ch) & (ch <= "9") THEN
					k := ORD(ch) - ORD("0");
					REPEAT
						base := base * 10 + k;
						INC(j); ch := s[j]; k := ORD(ch) - ORD("0")
					UNTIL (ch < "0") OR (ch > "9") OR (base > (MAX(INTEGER) - k) DIV 10);
					IF ("0" <= ch) & (ch <= "9") THEN base := 0 END
				END
			ELSE
				base := 10
			END;
			
			IF (base < 2) OR (base > 16) THEN
				res := 2
			ELSIF (base <= 10) & (ORD(top) < base + ORD("0"))
			OR (base > 10) & (ORD(top) < base - 10 + ORD("A")) THEN
				x := 0; ch := s[i]; neg := FALSE;
				IF ch = "-" THEN INC(i); ch := s[i]; neg := TRUE ELSIF ch = "+" THEN INC(i); ch := s[i] END;
				WHILE (ch # 0X) & (ch <= " ") DO INC(i); ch := s[i] END; 
				IF ("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F") THEN
					IF ch <= "9" THEN k := ORD(ch) - ORD("0") ELSE k := ORD(ch) - ORD("A") + 10 END;
					WHILE (("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F")) & (res = 0) DO
						IF x >= (MIN(INTEGER) + (base - 1) + k) DIV base THEN
							x := x * base - k; INC(i); ch := s[i];
							IF ch <= "9" THEN k := ORD(ch) - ORD("0") ELSE k := ORD(ch) - ORD("A") + 10 END
						ELSE res := 1
						END
					END
				ELSE res := 2
				END;
				IF res = 0 THEN
					IF ~neg THEN
						IF x > MIN(INTEGER) THEN x := -x ELSE res := 1 END
					END;
					IF (ch # 0X) & (ch # "%") THEN res := 2 END
				END
			ELSE
				res := 2
			END
		END
	END StringToInt;

	PROCEDURE StringToLInt* (IN s: ARRAY OF CHAR; OUT x: LONGINT; OUT res: INTEGER);
		CONST hexLimit = MAX(LONGINT) DIV 8 + 1;
		VAR i, j, k, digits: INTEGER; ch, top: CHAR; neg: BOOLEAN; base: INTEGER;
	BEGIN
		res := 0; i := 0; ch := s[0];
		WHILE (ch # 0X) & (ch <= " ") OR (ch = 8BX) OR (ch = 8FX) OR (ch = 0A0X) DO	(* ignore leading blanks *)
			INC(i); ch := s[i]
		END;
		j := i; top := "0";
		WHILE (ch # 0X) & (ch # "H") & (ch # "X") & (ch # "%") DO 
			IF ch > top THEN top := ch END;
			INC(j); ch := s[j]
		END;
		IF (ch = "H") OR (ch = "X") THEN
			x := 0; ch := s[i];
			IF ("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F") THEN
				WHILE ch = "0" DO INC(i); ch := s[i] END;
				digits := 0;
				WHILE (res = 0) & (("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F")) DO
					IF ch < "A" THEN k := ORD(ch) - ORD("0")
					ELSE k := ORD(ch) - ORD("A") + 10
					END;
					IF digits < 16 THEN
						x := x MOD hexLimit;
						IF x >= hexLimit DIV 2 THEN x := x - hexLimit END;
						x := x * 16 + k; INC(i); ch := s[i]
					ELSE res := 1
					END;
					INC(digits)
				END;
				IF res = 0 THEN
					IF (ch # "H") & (ch # "X") OR (s[i+1] # 0X) THEN res := 2 END
				END
			ELSE res := 2
			END
		ELSE
			IF ch = "%" THEN
				INC(j); ch := s[j]; base := 0;
				IF ("0" <= ch) & (ch <= "9") THEN
					k := ORD(ch) - ORD("0");
					REPEAT
						base := base * 10 + k;
						INC(j); ch := s[j]; k := ORD(ch) - ORD("0")
					UNTIL (ch < "0") OR (ch > "9") OR (base > (MAX(INTEGER) - k) DIV 10);
					IF ("0" <= ch) & (ch <= "9") THEN base := 0 END
				END
			ELSE
				base := 10
			END;
			
			IF (base < 2) OR (base > 16) THEN
				res := 2
			ELSIF (base <= 10) & (ORD(top) < base + ORD("0"))
			OR (base > 10) & (ORD(top) < base -10 + ORD("A")) THEN
				x := 0; ch := s[i]; neg := FALSE;
				IF ch = "-" THEN INC(i); ch := s[i]; neg := TRUE ELSIF ch = "+" THEN INC(i); ch := s[i] END;
				WHILE (ch # 0X) & (ch <= " ") DO INC(i); ch := s[i] END; 
				IF ("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F") THEN
					IF ch <= "9" THEN k := ORD(ch) - ORD("0") ELSE k := ORD(ch) - ORD("A") + 10 END;
					WHILE (("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F")) & (res = 0) DO
						IF x >= (MIN(LONGINT) + (base - 1) + k) DIV base THEN
							x := x * base - k; INC(i); ch := s[i];
							IF ch <= "9" THEN k := ORD(ch) - ORD("0") ELSE k := ORD(ch) - ORD("A") + 10 END
						ELSE res := 1
						END
					END
				ELSE res := 2
				END;
				IF res = 0 THEN
					IF ~neg THEN
						IF x > MIN(LONGINT) THEN x := -x ELSE res := 1 END
					END;
					IF (ch # 0X) & (ch # "%") THEN res := 2 END
				END
			ELSE
				res := 2
			END
		END
	END StringToLInt;


	(* real conversions *)

	PROCEDURE RealToStringForm* (x: REAL; precision, minW, expW: INTEGER; fillCh: CHAR;
														OUT s: ARRAY OF CHAR);
		VAR exp, len, i, j, n, k, p: INTEGER; m: ARRAY 80 OF CHAR; neg: BOOLEAN;
	BEGIN
		ASSERT((precision > 0) (*& (precision <= 18)*), 20);
		ASSERT((minW >= 0) & (minW < LEN(s)), 21);
		ASSERT((expW > -LEN(s)) & (expW <= 3), 22);
		exp := Math.Exponent(x);
		IF exp = MAX(INTEGER) THEN
			IF fillCh = "0" THEN fillCh := digitspace END;
			x := Math.Mantissa(x);
			IF x = -1 THEN m := "-inf"; n := 4
			ELSIF x = 1 THEN m := "inf"; n := 3
			ELSE m := "nan"; n := 3
			END;
			i := 0; j := 0;
			WHILE minW > n DO s[i] := fillCh; INC(i); DEC(minW) END;
			WHILE (j <= n) & (i < LEN(s)) DO s[i] := m[j]; INC(i); INC(j) END
		ELSE
			neg := FALSE; len := 1; m := "00";
			IF x < 0 THEN x := -x; neg := TRUE; DEC(minW) END;
			IF x # 0 THEN
				exp := (exp - 8) * 30103 DIV 100000;	(* * log(2) *)
				IF exp > 0 THEN
					n := SHORT(ENTIER(x / Math.IntPower(10, exp)));
					x := x / Math.IntPower(10, exp) - n
				ELSIF exp > -maxExp THEN
					n := SHORT(ENTIER(x * Math.IntPower(10, -exp)));
					x := x * Math.IntPower(10, -exp) - n
				ELSE
					n := SHORT(ENTIER(x * Math.IntPower(10, -exp - 2 * maxDig) * factor * factor));
					x := x * Math.IntPower(10, -exp - 2 * maxDig) * factor * factor - n
				END;
				(* x0 = (n + x) * 10^exp, 200 < n < 5000 *)
				p := precision - 4;
				IF n < 1000 THEN INC(p) END;
				IF (expW < 0) & (p > exp - expW) THEN p := exp - expW END;
				IF p >= 0 THEN
					x := x + 0.5 / Math.IntPower(10, p);	(* rounding correction *)
					IF x >= 1 THEN INC(n); x := x - 1 END
				ELSIF p = -1 THEN INC(n, 5)
				ELSIF p = -2 THEN INC(n, 50)
				ELSIF p = -3 THEN INC(n, 500)
				END;
				i := 0; k := 1000; INC(exp, 3);
				IF n < 1000 THEN k := 100; DEC(exp) END;
				WHILE (i < precision) & ((k > 0) OR (x # 0)) DO
					IF k > 0 THEN p := n DIV k; n := n MOD k; k := k DIV 10
					ELSE x := x * 10; p := SHORT(ENTIER(x)); x := x - p
					END;
					m[i] := CHR(p + ORD("0")); INC(i);
					IF p # 0 THEN len := i END
				END
			END;
			(* x0 = m[0].m[1]...m[len-1] * 10^exp *)
			i := 0;
			IF (expW < 0) OR (expW = 0) & (exp >= -3) & (exp <= len + 1) THEN
				n := exp + 1; k := len - n;
				IF n < 1 THEN n := 1 END;
				IF expW < 0 THEN k := -expW ELSIF k < 1 THEN k := 1 END;
				j := minW - n - k - 1; p := -exp;
				IF neg & (p >= MAX(0, n) + MAX(0, k)) THEN neg := FALSE; INC(j) END
			ELSE
				IF ABS(exp) >= 100 THEN expW := 3
				ELSIF (expW < 2) & (ABS(exp) >= 10) THEN expW := 2
				ELSIF expW < 1 THEN expW := 1
				END;
				IF len < 2 THEN len := 2 END;
				j := minW - len - 3 - expW; k := len;
				IF j > 0 THEN
					k := k + j; j := 0;
					IF k > precision THEN j := k - precision; k := precision END
				END;
				n := 1; DEC(k); p := 0
			END;
			IF neg & (fillCh = "0") THEN s[i] := "-"; INC(i); neg := FALSE END;
			WHILE j > 0 DO s[i] := fillCh; INC(i); DEC(j) END;
			IF neg & (i < LEN(s)) THEN s[i] := "-"; INC(i) END;
			j := 0;
			WHILE (n > 0) & (i < LEN(s)) DO
				IF (p <= 0) & (j < len) THEN s[i] := m[j]; INC(j) ELSE s[i] := "0" END;
				INC(i); DEC(n); DEC(p)
			END;
			IF i < LEN(s) THEN s[i] := decimalSign; INC(i) END;
			WHILE (k > 0) & (i < LEN(s)) DO
				IF (p <= 0) & (j < len) THEN s[i] := m[j]; INC(j) ELSE s[i] := "0" END;
				INC(i); DEC(k); DEC(p)
			END;
			IF expW > 0 THEN
				IF i < LEN(s) THEN s[i] := "E"; INC(i) END;
				IF i < LEN(s) THEN
					IF exp < 0 THEN s[i] := "-"; exp := -exp ELSE s[i] := "+" END;
					INC(i)
				END;
				IF (expW = 3) & (i < LEN(s)) THEN s[i] := CHR(exp DIV 100 + ORD("0")); INC(i) END;
				IF (expW >= 2) & (i < LEN(s)) THEN s[i] := CHR(exp DIV 10 MOD 10 + ORD("0")); INC(i) END;
				IF i < LEN(s) THEN s[i] := CHR(exp MOD 10 + ORD("0")); INC(i) END
			END
		END;
		IF i < LEN(s) THEN s[i] := 0X ELSE HALT(23) END
	END RealToStringForm;

	PROCEDURE RealToString* (x: REAL; OUT s: ARRAY OF CHAR);
	BEGIN
		RealToStringForm(x, 16, 0, 0, digitspace, s)
	END RealToString;


	PROCEDURE StringToReal* (IN s: ARRAY OF CHAR; OUT x: REAL; OUT res: INTEGER);
		VAR first, last, point, e, n, i, exp: INTEGER; y: REAL; ch: CHAR; neg, negExp, dig: BOOLEAN;
	BEGIN
		res := 0; i := 0; ch := s[0]; dig := FALSE;
		WHILE (ch # 0X) & (ch <= " ") OR (ch = 8BX) OR (ch = 8FX) OR (ch = 0A0X) DO INC(i); ch := s[i] END;
		IF ch = "+" THEN
			neg := FALSE; INC(i); ch := s[i]
		ELSIF ch = "-" THEN
			neg := TRUE; INC(i); ch := s[i]
		ELSE
			neg := FALSE
		END;
		WHILE ch = "0" DO INC(i); ch := s[i]; dig := TRUE END;
		first := i; e := 0;
		WHILE ("0" <= ch) & (ch <= "9") DO INC(i); ch := s[i]; INC(e); dig := TRUE END;
		point := i;
		IF ch = decimalSign THEN
			INC(i); ch := s[i];
			IF e = 0 THEN
				WHILE ch = "0" DO INC(i); ch := s[i]; DEC(e); dig := TRUE END;
				first := i
			END;
			WHILE ("0" <= ch) & (ch <= "9") DO INC(i); ch := s[i]; dig := TRUE END
		END;
		last := i - 1; exp := 0;
		IF (ch = "E") OR (ch = "D") THEN
			INC(i); ch := s[i]; negExp := FALSE;
			IF ch = "-" THEN negExp := TRUE; INC(i); ch := s[i]
			ELSIF ch = "+" THEN INC(i); ch := s[i]
			END;
			WHILE ("0" <= ch) & (ch <= "9") & (exp < 1000) DO
				exp := exp * 10 + (ORD(ch) - ORD("0"));
				INC(i); ch := s[i]
			END;
			IF negExp THEN exp := -exp END
		END;
		exp := exp + e; x := 0; y := 0; n := 0; 
		WHILE (n < maxDig) & (first <= last) DO
			IF first # point THEN x := x * 10 + (ORD(s[first]) - ORD("0")); INC(n) END;
			INC(first)
		END;
		WHILE last >= first DO
			IF last # point THEN y := (y + (ORD(s[last]) - ORD("0"))) / 10 END;
			DEC(last)
		END;
		IF ~dig OR (ch # 0X) THEN res := 2	(* syntax error *)
		ELSIF exp < -maxExp - maxDig THEN
			x := 0.0
		ELSIF exp < -maxExp + maxDig THEN
			x := (x + y) / Math.IntPower(10, n - exp - 2 * maxDig) / factor / factor
		ELSIF exp < n THEN
			x := (x + y) / Math.IntPower(10, n - exp)
		ELSIF exp < maxExp THEN
			x := (x + y) * Math.IntPower(10, exp - n)
		ELSIF exp = maxExp THEN
			x := (x + y) * (Math.IntPower(10, exp - n) / 16);
			IF x <= MAX(REAL) / 16 THEN x := x * 16
			ELSE res := 1	(* overflow *)
			END
		ELSE res := 1	(* overflow *)
		END;
		IF neg THEN x := -x END
	END StringToReal;


	(* set conversions *)

	PROCEDURE SetToString* (x: SET; OUT str: ARRAY OF CHAR);
		VAR
			len, i: INTEGER;

		PROCEDURE AppendChar(ch: CHAR);
		BEGIN ASSERT(len < LEN(str), 23); str[len] := ch; INC(len)
		END AppendChar;

		PROCEDURE AppendInt (x: INTEGER);
			VAR i: INTEGER; ch: CHAR; buf: ARRAY 32 OF CHAR;
		BEGIN
			IntToString(x, buf);
			ch := buf[0]; i := 0;
			WHILE ch # 0X DO AppendChar(ch); INC(i); ch := buf[i] END
		END AppendInt;

	BEGIN
		len := 0; AppendChar('{'); i := MIN(SET);
		WHILE x # {} DO
			IF i IN x THEN AppendInt(i); EXCL(x, i);
				IF (i <= MAX(SET) - 2) & (i+1 IN x) & (i+2 IN x) THEN AppendChar('.'); AppendChar('.');
					x := x - {i+1, i+2}; INC(i, 3);
					WHILE (i <= MAX(SET)) & (i IN x) DO EXCL(x, i); INC(i) END;
					AppendInt(i-1)
				END;
				IF x # {} THEN AppendChar(","); AppendChar(" ") END
			END;
			INC(i)
		END;
		AppendChar("}"); AppendChar(0X)
	END SetToString;

	PROCEDURE StringToSet* (IN s: ARRAY OF CHAR; OUT x: SET; OUT res: INTEGER);
		VAR
			next, x1: INTEGER; ch: CHAR;
		
		PROCEDURE NextChar;
		BEGIN ch := s[next]; 
			WHILE (ch <= " ") & (ch # 0X) DO INC(next); ch := s[next] END ;
			IF ch # 0X THEN INC(next) END
		END NextChar;
		
		PROCEDURE ParseInt(): INTEGER;
			VAR intval: INTEGER;
		BEGIN
			intval := ORD(ch) - ORD("0");
			ch := s[next];
			WHILE ("0" <= ch) & (ch <= "9") DO
				intval := intval * 10 + ORD(ch) - ORD('0');
				IF intval > MAX(SET) THEN res := 1; intval := 0 END ;
				INC(next); ch := s[next]
			END ;
			NextChar;
			RETURN intval
		END ParseInt;
		
		PROCEDURE ParseRange();
			VAR x2, i: INTEGER;
		BEGIN
			ch := s[next]; INC(next);
			IF ch = "." THEN NextChar; 
				IF ("0" <= ch) & (ch <= "9") THEN x2 := ParseInt();
					IF res = 0 THEN
						IF x2 >= x1 THEN FOR i := x1 TO x2 DO INCL(x, i) END
						ELSE res := 2
						END
					END
				ELSE res := 2
				END
			ELSE res := 2
			END
		END ParseRange;
		
	BEGIN
		x := {}; res := 0; next := 0; NextChar;
		IF ch = "{" THEN
			NextChar;
			WHILE ("0" <= ch) & (ch <= "9") & (res # 2) DO
				x1 := ParseInt();
				IF ch = "." THEN ParseRange() ELSIF res = 0 THEN INCL(x, x1) END ;
				IF ch = "," THEN NextChar END
			END ;
			IF ch = "}" THEN NextChar;
				IF ch # 0X THEN res := 2 END
			ELSE res := 2
			END
		ELSE res := 2
		END
	END StringToSet;


	(* ----- string manipulation routines ----- *)

	PROCEDURE Valid* (IN s: ARRAY OF CHAR): BOOLEAN;
		VAR i: INTEGER;
	BEGIN i := 0;
		WHILE (i < LEN(s)) & (s[i] # 0X) DO INC(i) END;
		RETURN i < LEN(s)
	END Valid;

	PROCEDURE Replace* (VAR s: ARRAY OF CHAR; pos, len: INTEGER; IN rep: ARRAY OF CHAR);
		(* replace stretch s[pos]..s[pos+len-1] with rep *)
		(* insert semantics if len = 0; delete semantics if LEN(rep$) = 0 *)
		VAR i, j, k, max, lenS: INTEGER; ch: CHAR;
	BEGIN
		ASSERT(len >= 0, 20); ASSERT(pos >= 0, 21);
		lenS := LEN(s$); max := LEN(s) - 1;
		IF pos <= lenS THEN i := pos; j := 0; ch := rep[0];
			IF pos+len > lenS THEN len := lenS - pos END;
			WHILE (ch # 0X) & (len > 0) DO
				s[i] := ch; INC(i); INC(j); DEC(len); ch := rep[j]
			END;
			IF len > 0 THEN (* delete the remaining part of the stretch [pos, pos+len') *)
				REPEAT ch := s[i+len]; s[i] := ch; INC(i) UNTIL ch = 0X
			ELSIF ch # 0X THEN (* insert the remaining part of rep *)
				len := LEN(rep$) - j; k := lenS + len;
				IF k > max THEN k := max END;
				s[k] := 0X; DEC(k);
				WHILE k - len >= i DO s[k] := s[k-len]; DEC(k) END;
				WHILE (rep[j] # 0X) & (i < max) DO s[i] := rep[j]; INC(i); INC(j) END
			END
		END
	END Replace;

	PROCEDURE Extract* (s: ARRAY OF CHAR; pos, len: INTEGER; OUT res: ARRAY OF CHAR);
		VAR i, j, max: INTEGER;
	BEGIN
		ASSERT(len >= 0, 20); ASSERT(pos >= 0, 21);
		i := 0; j := 0; max := LEN(res) - 1;
		WHILE (i < pos) & (s[i] # 0X) DO INC(i) END;
		WHILE (j < len) & (j < max) & (s[i] # 0X) DO res[j] := s[i]; INC(j); INC(i) END;
		res[j] := 0X
	END Extract;

	PROCEDURE Find* (IN s: ARRAY OF CHAR; IN pat: ARRAY OF CHAR; start: INTEGER; OUT pos: INTEGER);
		VAR j: INTEGER;
	BEGIN
		ASSERT(start >= 0, 20);
		IF (start = 0) OR (start <= LEN(s$) - LEN(pat$)) THEN
			(* start = 0 is optimization: need not call Len *)
			pos := start;
			WHILE s[pos] # 0X DO j := 0;
				WHILE (s[pos+j] = pat[j]) & (pat[j] # 0X) DO INC(j) END;
				IF pat[j] = 0X THEN RETURN END;
				INC(pos)
			END
		END;
		pos := -1	(* pattern not found *)
	END Find;
	
	PROCEDURE SetDecimalSign* (sign: CHAR);
	BEGIN
		ASSERT((sign = ".") OR (sign = ","), 21);
		decimalSign := sign
	END SetDecimalSign;

	PROCEDURE GetDecimalSign* (): CHAR;
	BEGIN
		RETURN decimalSign
	END GetDecimalSign;

	PROCEDURE Upper* (ch: CHAR): CHAR;
	BEGIN
		CASE ch OF
		| 061X..07AX, 0E0X..0F6X, 0F8X..0FEX, 03B1X..03C1X, 03C3X..03CBX, 0430X..044FX, 0FF41X..0FF5AX: RETURN CHR(ORD(ch) - 32)
		| 0FFX: RETURN 0178X
		| 0101X..0130X, 0133X..0138X, 014BX..0178X, 0183X..0186X, 01A1X..01A6X, 01DFX..01F0X, 01F9X..0220X, 0223X..0234X, 0247X..024EX, 0371X..0374X, 03D9X..03EEX, 0461X..0482X, 048BX..04C0X, 04D1X..0530X, 01E01X..01E96X, 01EA1X..01EFEX, 02C81X..02CE4X, 0A641X..0A66EX, 0A681X..0A69CX, 0A723X..0A730X, 0A733X..0A770X, 0A77FX..0A788X, 0A791X..0A794X, 0A797X..0A7AAX, 0A7B5X..0A7B8X: IF ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) - 1) ELSE RETURN ch END
		| 0131X: RETURN 049X
		| 013AX..0149X, 017AX..017DX, 01B4X..01B7X, 01CEX..01DBX, 04C2X..04CDX, 02C68X..02C6DX, 02CECX..02CEFX, 0A77AX..0A77DX: IF ~ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) - 1) ELSE RETURN ch END
		| 017EX, 0188X, 018CX, 0192X, 0199X, 01A8X, 01ADX, 01B0X, 01B9X, 01BDX, 01C5X, 01C8X, 01CBX, 01DCX, 01F2X, 01F5X, 023CX, 0242X, 024FX, 0377X, 03EFX, 03F8X, 03FBX, 04CEX, 01EFFX, 02184X, 02C61X, 02C73X, 02C76X, 02CF3X, 0A78CX: RETURN CHR(ORD(ch) - 1)
		| 017FX: RETURN 053X
		| 0180X: RETURN 0243X
		| 0195X: RETURN 01F6X
		| 019AX: RETURN 023DX
		| 019EX, 037BX..037DX: RETURN CHR(ORD(ch) + 130)
		| 01BFX: RETURN 01F7X
		| 01C6X, 01C9X, 01CCX, 01F3X: RETURN CHR(ORD(ch) - 2)
		| 01DDX: RETURN 018EX
		| 023FX..0240X: RETURN CHR(ORD(ch) + 10815)
		| 0250X: RETURN 02C6FX
		| 0251X: RETURN 02C6DX
		| 0252X: RETURN 02C70X
		| 0253X: RETURN 0181X
		| 0254X: RETURN 0186X
		| 0256X..0257X, 0260X: RETURN CHR(ORD(ch) - 205)
		| 0259X: RETURN 018FX
		| 025BX: RETURN 0190X
		| 025CX: RETURN 0A7ABX
		| 0261X: RETURN 0A7ACX
		| 0263X: RETURN 0194X
		| 0265X: RETURN 0A78DX
		| 0266X, 026AX: RETURN CHR(ORD(ch) + 42308)
		| 0268X: RETURN 0197X
		| 0269X, 026FX: RETURN CHR(ORD(ch) - 211)
		| 026BX: RETURN 02C62X
		| 026CX: RETURN 0A7ADX
		| 0271X: RETURN 02C6EX
		| 0272X: RETURN 019DX
		| 0275X: RETURN 019FX
		| 027DX: RETURN 02C64X
		| 0280X, 0283X, 0288X: RETURN CHR(ORD(ch) - 218)
		| 0287X: RETURN 0A7B1X
		| 0289X: RETURN 0244X
		| 028AX..028BX: RETURN CHR(ORD(ch) - 217)
		| 028CX: RETURN 0245X
		| 0292X: RETURN 01B7X
		| 029DX: RETURN 0A7B2X
		| 029EX: RETURN 0A7B0X
		| 0345X: RETURN 0399X
		| 03ACX: RETURN 0386X
		| 03ADX..03AFX: RETURN CHR(ORD(ch) - 37)
		| 03C2X: RETURN 03A3X
		| 03CCX: RETURN 038CX
		| 03CDX..03CEX: RETURN CHR(ORD(ch) - 63)
		| 03D0X: RETURN 0392X
		| 03D1X: RETURN 0398X
		| 03D5X: RETURN 03A6X
		| 03D6X: RETURN 03A0X
		| 03D7X, 013F8X..013FDX: RETURN CHR(ORD(ch) - 8)
		| 03F0X: RETURN 039AX
		| 03F1X, 0450X..045FX: RETURN CHR(ORD(ch) - 80)
		| 03F2X, 01FE5X: RETURN CHR(ORD(ch) + 7)
		| 03F3X: RETURN 037FX
		| 03F5X: RETURN 0395X
		| 04CFX: RETURN 04C0X
		| 0561X..0586X, 02C30X..02C5EX: RETURN CHR(ORD(ch) - 48)
		| 01C80X: RETURN 0412X
		| 01C81X: RETURN 0414X
		| 01C82X: RETURN 041EX
		| 01C83X..01C84X: RETURN CHR(ORD(ch) - 6242)
		| 01C85X: RETURN 0422X
		| 01C86X: RETURN 042AX
		| 01C87X: RETURN 0462X
		| 01C88X: RETURN 0A64AX
		| 01D79X: RETURN 0A77DX
		| 01D7DX: RETURN 02C63X
		| 01E9BX: RETURN 01E60X
		| 01F00X..01F07X, 01F10X..01F15X, 01F20X..01F27X, 01F30X..01F37X, 01F40X..01F45X, 01F60X..01F67X, 01F80X..01F87X, 01F90X..01F97X, 01FA0X..01FA7X, 01FB0X..01FB1X, 01FD0X..01FD1X, 01FE0X..01FE1X: RETURN CHR(ORD(ch) + 8)
		| 01F51X..01F58X: IF ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) + 8) ELSE RETURN ch END
		| 01F70X..01F71X: RETURN CHR(ORD(ch) + 74)
		| 01F72X..01F75X: RETURN CHR(ORD(ch) + 86)
		| 01F76X..01F77X: RETURN CHR(ORD(ch) + 100)
		| 01F78X..01F79X: RETURN CHR(ORD(ch) + 128)
		| 01F7AX..01F7BX: RETURN CHR(ORD(ch) + 112)
		| 01F7CX..01F7DX: RETURN CHR(ORD(ch) + 126)
		| 01FB3X, 01FC3X, 01FF3X: RETURN CHR(ORD(ch) + 9)
		| 01FBEX: RETURN 0399X
		| 0214EX: RETURN 02132X
		| 02170X..0217FX: RETURN CHR(ORD(ch) - 16)
		| 024D0X..024E9X: RETURN CHR(ORD(ch) - 26)
		| 02C65X: RETURN 023AX
		| 02C66X: RETURN 023EX
		| 02D00X..02D25X, 02D27X, 02D2DX: RETURN CHR(ORD(ch) - 7264)
		| 0AB53X: RETURN 0A7B3X
		| 0AB70X..0ABBFX: RETURN CHR(ORD(ch) - 38864)
		ELSE RETURN ch
		END
	END Upper;

	PROCEDURE Lower* (ch: CHAR): CHAR;
	BEGIN
		CASE ch OF
		| 041X..05AX, 0C0X..0D6X, 0D8X..0DEX, 0391X..03A1X, 03A3X..03ABX, 0410X..042FX, 0FF21X..0FF3AX: RETURN CHR(ORD(ch) + 32)
		| 0100X..012FX, 0132X..0137X, 014AX..0177X, 0182X..0185X, 01A0X..01A5X, 01DEX..01EFX, 01F2X..01F5X, 01F8X..021FX, 0222X..0233X, 0246X..024FX, 0370X..0373X, 03D8X..03EFX, 0460X..0481X, 048AX..04BFX, 04D0X..052FX, 01E00X..01E95X, 01EA0X..01EFFX, 02C80X..02CE3X, 0A640X..0A66DX, 0A680X..0A69BX, 0A722X..0A72FX, 0A732X..0A76FX, 0A77EX..0A787X, 0A790X..0A793X, 0A796X..0A7A9X, 0A7B4X..0A7B7X: IF ~ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) + 1) ELSE RETURN ch END
		| 0130X: RETURN 069X
		| 0139X..0148X, 0179X..017EX, 01B3X..01B6X, 01CBX..01DCX, 04C1X..04CEX, 02C67X..02C6CX, 02CEBX..02CEEX, 0A779X..0A77CX: IF ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) + 1) ELSE RETURN ch END
		| 0178X: RETURN 0FFX
		| 0181X: RETURN 0253X
		| 0186X: RETURN 0254X
		| 0187X, 018BX, 0191X, 0198X, 01A7X, 01ACX, 01AFX, 01B8X, 01BCX, 01C5X, 01C8X, 023BX, 0241X, 0376X, 03F7X, 03FAX, 02183X, 02C60X, 02C72X, 02C75X, 02CF2X, 0A78BX: RETURN CHR(ORD(ch) + 1)
		| 0189X..018AX, 0193X: RETURN CHR(ORD(ch) + 205)
		| 018EX: RETURN 01DDX
		| 018FX: RETURN 0259X
		| 0190X: RETURN 025BX
		| 0194X: RETURN 0263X
		| 0196X, 019CX: RETURN CHR(ORD(ch) + 211)
		| 0197X: RETURN 0268X
		| 019DX: RETURN 0272X
		| 019FX: RETURN 0275X
		| 01A6X, 01A9X, 01AEX: RETURN CHR(ORD(ch) + 218)
		| 01B1X..01B2X: RETURN CHR(ORD(ch) + 217)
		| 01B7X: RETURN 0292X
		| 01C4X, 01C7X, 01CAX, 01F1X: RETURN CHR(ORD(ch) + 2)
		| 01F6X: RETURN 0195X
		| 01F7X: RETURN 01BFX
		| 0220X, 03FDX..03FFX: RETURN CHR(ORD(ch) - 130)
		| 023AX: RETURN 02C65X
		| 023DX: RETURN 019AX
		| 023EX: RETURN 02C66X
		| 0243X: RETURN 0180X
		| 0244X: RETURN 0289X
		| 0245X: RETURN 028CX
		| 037FX: RETURN 03F3X
		| 0386X: RETURN 03ACX
		| 0388X..038AX: RETURN CHR(ORD(ch) + 37)
		| 038CX: RETURN 03CCX
		| 038EX..038FX: RETURN CHR(ORD(ch) + 63)
		| 03CFX, 013F0X..013F5X: RETURN CHR(ORD(ch) + 8)
		| 03F4X: RETURN 03B8X
		| 03F9X, 01FECX: RETURN CHR(ORD(ch) - 7)
		| 0400X..040FX: RETURN CHR(ORD(ch) + 80)
		| 04C0X: RETURN 04CFX
		| 0531X..0556X, 02C00X..02C2EX: RETURN CHR(ORD(ch) + 48)
		| 010A0X..010C5X, 010C7X, 010CDX: RETURN CHR(ORD(ch) + 7264)
		| 013A0X..013EFX: RETURN CHR(ORD(ch) + 38864)
		| 01E9EX: RETURN 0DFX
		| 01F08X..01F0FX, 01F18X..01F1DX, 01F28X..01F2FX, 01F38X..01F3FX, 01F48X..01F4DX, 01F68X..01F6FX, 01F88X..01F8FX, 01F98X..01F9FX, 01FA8X..01FAFX, 01FB8X..01FB9X, 01FD8X..01FD9X, 01FE8X..01FE9X: RETURN CHR(ORD(ch) - 8)
		| 01F59X..01F60X: IF ODD(ORD(ch)) THEN RETURN CHR(ORD(ch) - 8) ELSE RETURN ch END
		| 01FBAX..01FBBX: RETURN CHR(ORD(ch) - 74)
		| 01FBCX, 01FCCX, 01FFCX: RETURN CHR(ORD(ch) - 9)
		| 01FC8X..01FCBX: RETURN CHR(ORD(ch) - 86)
		| 01FDAX..01FDBX: RETURN CHR(ORD(ch) - 100)
		| 01FEAX..01FEBX: RETURN CHR(ORD(ch) - 112)
		| 01FF8X..01FF9X: RETURN CHR(ORD(ch) - 128)
		| 01FFAX..01FFBX: RETURN CHR(ORD(ch) - 126)
		| 02126X: RETURN 03C9X
		| 0212AX: RETURN 06BX
		| 0212BX: RETURN 0E5X
		| 02132X: RETURN 0214EX
		| 02160X..0216FX: RETURN CHR(ORD(ch) + 16)
		| 024B6X..024CFX: RETURN CHR(ORD(ch) + 26)
		| 02C62X: RETURN 026BX
		| 02C63X: RETURN 01D7DX
		| 02C64X: RETURN 027DX
		| 02C6DX: RETURN 0251X
		| 02C6EX: RETURN 0271X
		| 02C6FX: RETURN 0250X
		| 02C70X: RETURN 0252X
		| 02C7EX..02C7FX: RETURN CHR(ORD(ch) - 10815)
		| 0A77DX: RETURN 01D79X
		| 0A78DX: RETURN 0265X
		| 0A7AAX, 0A7AEX: RETURN CHR(ORD(ch) - 42308)
		| 0A7ABX: RETURN 025CX
		| 0A7ACX: RETURN 0261X
		| 0A7ADX: RETURN 026CX
		| 0A7B0X: RETURN 029EX
		| 0A7B1X: RETURN 0287X
		| 0A7B2X: RETURN 029DX
		| 0A7B3X: RETURN 0AB53X
		ELSE RETURN ch
		END
	END Lower;

	PROCEDURE IsAlpha* (ch: CHAR): BOOLEAN;
	BEGIN
		CASE ch OF
		| 00X..040X, 05BX..060X, 07BX..0A9X, 0ABX..0B4X, 0B6X..0B9X, 0BBX..0BFX, 0D7X, 0F7X, 02C2X..02C5X, 02D2X..02DFX, 02E5X..02EBX, 02EFX..0344X, 0346X..036FX, 0375X, 0378X..0379X, 0380X..0385X, 0387X, 03A2X, 03F6X, 0482X..0489X, 0530X, 0557X..0558X, 055AX..0560X, 0588X..05AFX, 05C3X, 05C8X..05CFX, 05EBX..05EFX, 05F3X..060FX, 061BX..061FX, 0658X, 066AX..066DX, 06D4X, 06DDX..06E0X, 06E9X..06ECX, 06FDX..06FEX, 0700X..070FX, 0740X..074CX, 07B2X..07BFX, 07EBX..07F3X, 07F6X..07F9X, 07FBX..07FFX, 0818X..0819X, 082DX..083FX, 0859X..085FX, 086BX..089FX, 08B5X, 08BEX..08D3X, 08E0X..08E2X, 08EAX..08EFX, 093CX, 094DX, 0951X..0954X, 0964X..0965X, 0970X, 0984X, 098DX..098EX, 0991X..0992X, 09A9X, 09B3X..09B5X, 09BAX..09BCX, 09C5X..09C6X, 09C9X..09CAX, 09CFX..09D6X, 09D8X..09DBX, 09DEX, 09E4X..09E5X, 09F2X..09FBX, 09FDX..0A00X, 0A04X, 0A0BX..0A0EX, 0A11X..0A12X, 0A29X, 0A31X, 0A34X, 0A37X, 0A3AX..0A3DX, 0A43X..0A46X, 0A49X..0A4AX, 0A4DX..0A50X, 0A52X..0A58X, 0A5FX..0A65X, 0A76X..0A80X, 0A84X, 0A8EX, 0A92X, 0AA9X, 0AB1X, 0AB4X, 0ABAX..0ABCX, 0AC6X, 0ACAX, 0ACDX..0ACFX, 0AD1X..0ADFX, 0AE4X..0AE5X, 0AF0X..0AF8X, 0AFDX..0B00X, 0B04X, 0B0DX..0B0EX, 0B11X..0B12X, 0B29X, 0B31X, 0B34X, 0B3AX..0B3CX, 0B45X..0B46X, 0B49X..0B4AX, 0B4DX..0B55X, 0B58X..0B5BX, 0B5EX, 0B64X..0B65X, 0B72X..0B81X, 0B84X, 0B8BX..0B8DX, 0B91X, 0B96X..0B98X, 0BA0X..0BA2X, 0BA5X..0BA7X, 0BABX..0BADX, 0BBAX..0BBDX, 0BC3X..0BC5X, 0BC9X, 0BCDX..0BCFX, 0BD1X..0BD6X, 0BD8X..0BE5X, 0BF0X..0BFFX, 0C04X, 0C0DX, 0C11X, 0C29X, 0C3AX..0C3CX, 0C45X, 0C49X, 0C4DX..0C54X, 0C57X, 0C5BX..0C5FX, 0C64X..0C65X, 0C70X..0C7FX, 0C84X, 0C8DX, 0C91X, 0CA9X, 0CB4X, 0CBAX..0CBCX, 0CC5X, 0CC9X, 0CCDX..0CD4X, 0CD7X..0CDDX, 0CDFX, 0CE4X..0CE5X, 0CF0X, 0CF3X..0CFFX, 0D04X, 0D0DX, 0D11X, 0D3BX..0D3CX, 0D45X, 0D49X, 0D4FX..0D53X, 0D58X..0D5EX, 0D64X..0D65X, 0D70X..0D79X, 0D80X..0D81X, 0D84X, 0D97X..0D99X, 0DB2X, 0DBEX..0DBFX, 0DC7X..0DCEX, 0DE0X..0DE5X, 0DF0X..0DF1X, 0DF4X..0E00X, 0E3BX..0E3FX, 0E47X..0E4CX, 0E4EX..0E4FX, 0E5AX..0E80X, 0E85X..0E86X, 0E8BX..0E8CX, 0E8EX..0E93X, 0E98X, 0EA0X, 0EA8X..0EA9X, 0EACX, 0EBAX, 0EBEX..0EBFX, 0EC7X..0ECCX, 0ECEX..0ECFX, 0EDAX..0EDBX, 0EE0X..0EFFX, 0F01X..0F1FX, 0F2AX..0F3FX, 0F48X, 0F6DX..0F70X, 0F82X..0F87X, 0F98X, 0FBDX..0FFFX, 01039X..0103AX, 0104AX..0104FX, 01063X..01064X, 01069X..0106DX, 01087X..0108DX, 0108FX, 0109AX..0109BX, 0109EX..0109FX, 010C8X..010CCX, 010CEX..010CFX, 010FBX, 01249X, 0124EX..0124FX, 0125EX..0125FX, 01289X, 0128EX..0128FX, 012B1X, 012B6X..012B7X, 012C6X..012C7X, 012D7X, 01311X, 01316X..01317X, 0135BX..0135EX, 01360X..0137FX, 01390X..0139FX, 013F6X..013F7X, 013FEX..01400X, 0166DX..0166EX, 01680X, 0169BX..0169FX, 016EBX..016EDX, 016F9X..016FFX, 0170DX, 01714X..0171FX, 01734X..0173FX, 01754X..0175FX, 0176DX, 01771X, 01774X..0177FX, 017B4X..017B5X, 017C9X..017D6X, 017D8X..017DBX, 017DDX..017DFX, 017EAX..0180FX, 0181AX..0181FX, 01878X..0187FX, 018ABX..018AFX, 018F6X..018FFX, 0191FX, 0192CX..0192FX, 01939X..01945X, 0196EX..0196FX, 01975X..0197FX, 019ACX..019AFX, 019CAX..019CFX, 019DAX..019FFX, 01A1CX..01A1FX, 01A5FX..01A60X, 01A75X..01A7FX, 01A8AX..01A8FX, 01A9AX..01AA6X, 01AA8X..01AFFX, 01B34X, 01B44X, 01B4CX..01B4FX, 01B5AX..01B7FX, 01BAAX..01BABX, 01BE6X, 01BF2X..01BFFX, 01C36X..01C3FX, 01C4AX..01C4CX, 01C7EX..01C7FX, 01C89X..01CE8X, 01CEDX, 01CF4X, 01CF7X..01CFFX, 01DC0X..01DE6X, 01DF5X..01DFFX, 01F16X..01F17X, 01F1EX..01F1FX, 01F46X..01F47X, 01F4EX..01F4FX, 01F7EX..01F7FX, 01FB5X, 01FBFX..01FC1X, 01FC5X, 01FCDX..01FCFX, 01FD4X..01FD5X, 01FDCX..01FDFX, 01FEDX..01FF1X, 01FF5X, 01FFDX..02070X, 02072X..0207EX, 02080X..0208FX, 0209DX..02101X, 02103X..02106X, 02108X..02109X, 02116X..02118X, 0211EX..02123X, 0212EX, 0213AX..0213BX, 02140X..02144X, 0214AX..0214DX, 0214FX..0215FX, 02189X..024B5X, 024EAX..02BFFX, 02C2FX, 02C5FX, 02CE5X..02CEAX, 02CEFX..02CF1X, 02CF4X..02CFFX, 02D28X..02D2CX, 02D2EX..02D2FX, 02D68X..02D6EX, 02D70X..02D7FX, 02D97X..02D9FX, 02DA7X, 02DAFX, 02DB7X, 02DBFX, 02DC7X, 02DCFX, 02DD7X, 02DDFX, 02E00X..02E2EX, 02E30X..03004X, 03008X..03020X, 0302AX..03030X, 03036X..03037X, 0303DX..03040X, 03097X..0309CX, 030A0X, 030FBX, 03100X..03104X, 0312FX..03130X, 0318FX..0319FX, 031BBX..031EFX, 03200X..033FFX, 04DB6X..04DFFX, 09FEBX..09FFFX, 0A48DX..0A4CFX, 0A4FEX..0A4FFX, 0A60DX..0A60FX, 0A62CX..0A63FX, 0A66FX..0A673X, 0A67CX..0A67EX, 0A6F0X..0A716X, 0A720X..0A721X, 0A789X..0A78AX, 0A7AFX, 0A7B8X..0A7F6X, 0A802X, 0A806X, 0A80BX, 0A828X..0A83FX, 0A874X..0A87FX, 0A8C6X..0A8CFX, 0A8DAX..0A8F1X, 0A8F8X..0A8FAX, 0A8FEX..0A8FFX, 0A92BX..0A92FX, 0A953X..0A95FX, 0A97DX..0A97FX, 0A9B3X, 0A9C0X..0A9CEX, 0A9DAX..0A9DFX, 0A9E5X, 0A9FFX, 0AA37X..0AA3FX, 0AA4EX..0AA4FX, 0AA5AX..0AA5FX, 0AA77X..0AA79X, 0AA7BX..0AA7DX, 0AAC3X..0AADAX, 0AADEX..0AADFX, 0AAF0X..0AAF1X, 0AAF6X..0AB00X, 0AB07X..0AB08X, 0AB0FX..0AB10X, 0AB17X..0AB1FX, 0AB27X, 0AB2FX, 0AB5BX, 0AB66X..0AB6FX, 0ABEBX..0ABEFX, 0ABFAX..0ABFFX, 0D7A4X..0D7AFX, 0D7C7X..0D7CAX, 0D7FCX..0F8FFX, 0FA6EX..0FA6FX, 0FADAX..0FAFFX, 0FB07X..0FB12X, 0FB18X..0FB1CX, 0FB29X, 0FB37X, 0FB42X, 0FB45X, 0FBB2X..0FBD2X, 0FD3EX..0FD4FX, 0FD90X..0FD91X, 0FDC8X..0FDEFX, 0FDFCX..0FE6FX, 0FE75X, 0FEFDX..0FF0FX, 0FF1AX..0FF20X, 0FF3BX..0FF40X, 0FF5BX..0FF65X, 0FFBFX..0FFC1X, 0FFC8X..0FFC9X, 0FFD0X..0FFD1X, 0FFD8X..0FFD9X, 0FFDDX..0FFFFX: RETURN FALSE
		| 02EDX..02EEX, 038BX..038EX, 09B1X..09B2X, 09CDX..09CEX, 0A5DX..0A5EX, 0B9BX..0B9EX, 0D4DX..0D4EX, 0DD5X..0DD8X, 0E83X..0E84X, 0E89X..0E8AX, 0EC5X..0EC6X, 01037X..01038X, 01257X..0125AX, 012BFX..012C2X, 01FBDX..01FBEX, 02125X..0212AX, 0AABFX..0AAC2X, 0FB3DX..0FB40X: RETURN ~ODD(ORD(ch))
		| 037EX..037FX, 05BEX..05C1X, 05C6X..05C7X, 0B70X..0B71X, 0DBCX..0DBDX, 0EA4X..0EA7X, 010C6X..010C7X, 01F58X..01F5FX, 02114X..02115X, 02D26X..02D27X, 0A8C4X..0A8C5X, 0A8FCX..0A8FDX: RETURN ODD(ORD(ch))
		ELSE RETURN TRUE
		END
	END IsAlpha;

	PROCEDURE IsUpper* (ch: CHAR): BOOLEAN;
	BEGIN
		CASE ch OF
		| 041X..05AX, 0C0X..0D6X, 0D8X..0DEX, 0178X..0179X, 0181X..0182X, 0186X..0187X, 0189X..018BX, 018EX..0191X, 0193X..0194X, 0196X..0198X, 019CX..019DX, 019FX..01A0X, 01A6X..01A7X, 01A9X, 01AEX..01AFX, 01B1X..01B3X, 01B7X..01B8X, 01BCX, 01C4X..01C5X, 01C7X..01C8X, 01CAX..01CBX, 01F1X..01F2X, 01F6X..01F8X, 023AX..023BX, 023DX..023EX, 0243X..0246X, 0376X, 037FX, 0388X..038AX, 038EX..038FX, 0391X..03A1X, 03A3X..03ABX, 03CFX, 03D2X..03D4X, 03F4X, 03F9X..03FAX, 03FDX..042FX, 04C0X..04C1X, 0531X..0556X, 010A0X..010C5X, 010C7X, 010CDX, 013A0X..013F5X, 01F08X..01F0FX, 01F18X..01F1DX, 01F28X..01F2FX, 01F38X..01F3FX, 01F48X..01F4DX, 01F68X..01F6FX, 01F88X..01F8FX, 01F98X..01F9FX, 01FA8X..01FAFX, 01FB8X..01FBCX, 01FC8X..01FCCX, 01FD8X..01FDBX, 01FE8X..01FECX, 01FF8X..01FFCX, 02102X, 02107X, 0210BX..0210DX, 02110X..02112X, 02115X, 02119X..0211DX, 0212AX..0212DX, 02130X..02133X, 0213EX..0213FX, 02145X, 02160X..0216FX, 02183X, 024B6X..024CFX, 02C00X..02C2EX, 02C62X..02C64X, 02C6DX..02C70X, 02C72X, 02C75X, 02C7EX..02C80X, 02CF2X, 0A77DX..0A77EX, 0A7AAX..0A7AEX, 0A7B0X..0A7B4X, 0A7B6X, 0FF21X..0FF3AX: RETURN TRUE
		| 0100X..0137X, 014AX..0177X, 0184X..0185X, 01A2X..01A5X, 01ACX..01ADX, 01DEX..01EFX, 01F4X..01F5X, 01FAX..0233X, 0248X..024FX, 0370X..0373X, 0386X..0387X, 038CX..038DX, 03D8X..03EFX, 0460X..0481X, 048AX..04BFX, 04D0X..052FX, 01E00X..01E95X, 01E9EX..01EFFX, 02124X..02129X, 02C60X..02C61X, 02C82X..02CE3X, 0A640X..0A66DX, 0A680X..0A69BX, 0A722X..0A72FX, 0A732X..0A76FX, 0A780X..0A787X, 0A790X..0A793X, 0A796X..0A7A9X: RETURN ~ODD(ORD(ch))
		| 0139X..0148X, 017BX..017EX, 01B5X..01B6X, 01CDX..01DCX, 0241X..0242X, 03F7X..03F8X, 04C3X..04CEX, 01F59X..01F60X, 02C67X..02C6CX, 02CEBX..02CEEX, 0A779X..0A77CX, 0A78BX..0A78EX: RETURN ODD(ORD(ch))
		ELSE RETURN FALSE
		END
	END IsUpper;

	PROCEDURE IsLower* (ch: CHAR): BOOLEAN;
	BEGIN
		CASE ch OF
		| 061X..07AX, 0E0X..0F6X, 0F8X..0FFX, 0137X..0138X, 0148X..0149X, 017EX..0180X, 0188X, 018CX..018DX, 0192X, 0195X, 0199X..019BX, 019EX, 01AAX..01ABX, 01ADX, 01B0X, 01B9X..01BAX, 01BDX..01BFX, 01C5X..01C6X, 01C8X..01C9X, 01CBX..01CCX, 01DCX..01DDX, 01EFX..01F0X, 01F2X..01F3X, 01F5X, 0233X..0239X, 023CX, 023FX..0240X, 0242X, 024FX..0293X, 0295X..02B8X, 02C0X..02C1X, 02E0X..02E4X, 0345X, 0377X, 037AX..037DX, 0390X, 03ACX..03CEX, 03D0X..03D1X, 03D5X..03D7X, 03EFX..03F3X, 03F5X, 03F8X, 03FBX..03FCX, 0430X..045FX, 04CEX..04CFX, 0561X..0587X, 013F8X..013FDX, 01C80X..01C88X, 01D00X..01DBFX, 01E95X..01E9DX, 01EFFX..01F07X, 01F10X..01F15X, 01F20X..01F27X, 01F30X..01F37X, 01F40X..01F45X, 01F50X..01F57X, 01F60X..01F67X, 01F70X..01F7DX, 01F80X..01F87X, 01F90X..01F97X, 01FA0X..01FA7X, 01FB0X..01FB4X, 01FB6X..01FB7X, 01FBEX, 01FC2X..01FC4X, 01FC6X..01FC7X, 01FD0X..01FD3X, 01FD6X..01FD7X, 01FE0X..01FE7X, 01FF2X..01FF4X, 01FF6X..01FF7X, 02071X, 0207FX, 02090X..0209CX, 0210AX, 0210EX..0210FX, 02113X, 0212FX, 02134X, 02139X, 0213CX..0213DX, 02146X..02149X, 0214EX, 02170X..0217FX, 02184X, 024D0X..024E9X, 02C30X..02C5EX, 02C61X, 02C65X..02C66X, 02C73X..02C74X, 02C76X..02C7DX, 02CE3X..02CE4X, 02CF3X, 02D00X..02D25X, 02D27X, 02D2DX, 0A69BX..0A69DX, 0A72FX..0A731X, 0A76FX..0A778X, 0A793X..0A795X, 0A7F8X..0A7FAX, 0AB30X..0AB5AX, 0AB5CX..0AB65X, 0AB70X..0ABBFX, 0FB00X..0FB06X, 0FB13X..0FB17X, 0FF41X..0FF5AX: RETURN TRUE
		| 0101X..0136X, 014BX..0178X, 0183X..0186X, 01A1X..01A6X, 01DFX..01EEX, 01F9X..0232X, 0247X..024EX, 0371X..0374X, 03D9X..03EEX, 0461X..0482X, 048BX..04C0X, 04D1X..0530X, 01E01X..01E94X, 01E9FX..01EFEX, 02C71X..02C72X, 02C81X..02CE2X, 0A641X..0A66EX, 0A681X..0A69AX, 0A723X..0A72EX, 0A733X..0A76EX, 0A77FX..0A788X, 0A791X..0A792X, 0A797X..0A7AAX, 0A7B5X..0A7B8X: RETURN ODD(ORD(ch))
		| 013AX..0147X, 017AX..017DX, 01A8X..01A9X, 01B4X..01B7X, 01CEX..01DBX, 04C2X..04CDX, 02C68X..02C6DX, 02CECX..02CEFX, 0A77AX..0A77DX, 0A78CX..0A78FX: RETURN ~ODD(ORD(ch))
		ELSE RETURN FALSE
		END
	END IsLower;
	
	
	PROCEDURE ToUpper* (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR);
		VAR i, max: INTEGER;
	BEGIN i := 0; max := LEN(out)-1;
		WHILE (in[i] # 0X) & (i < max) DO
			out[i] := Upper(in[i]);
			INC(i)
		END;
		out[i] := 0X
	END ToUpper;

	PROCEDURE ToLower* (IN in: ARRAY OF CHAR; OUT out: ARRAY OF CHAR);
		VAR i, max: INTEGER;
	BEGIN i := 0; max := LEN(out)-1;
		WHILE (in[i] # 0X) & (i < max) DO
			out[i] := Lower(in[i]);
			INC(i)
		END;
		out[i] := 0X
	END ToLower;
	
	PROCEDURE IsIdentStart* (ch: CHAR): BOOLEAN;
	BEGIN
		CASE ch OF
		"a".."z", "A".."Z", "_": RETURN TRUE
		ELSE
			IF ch > 7FX THEN RETURN IsAlpha(ch) ELSE RETURN FALSE END
		END
	END IsIdentStart;

	PROCEDURE IsIdent* (ch: CHAR): BOOLEAN;
	BEGIN
	(* returns IsIdentStart(ch) OR IsNumeric(ch); optimized because heavily used in the compiler *)
		CASE ch OF
		"a".."z", "A".."Z", "_", "0".."9": RETURN TRUE
		ELSE
			IF ch > 7FX THEN RETURN IsAlpha(ch) ELSE RETURN FALSE END
		END
	END IsIdent;
	
	PROCEDURE IsNumeric* (ch: CHAR): BOOLEAN;
	BEGIN
		RETURN ("0" <= ch) & (ch <= "9")
	END IsNumeric;

	PROCEDURE IsAlphaNumeric* (ch: CHAR): BOOLEAN;
	BEGIN
		RETURN IsAlpha(ch) OR IsNumeric(ch)
	END IsAlphaNumeric;

	PROCEDURE Init;
	BEGIN
		decimalSign := ".";
		digits := "0123456789ABCDEF"; 
		maxExp := SHORT(ENTIER(Math.Log(MAX(REAL)))) + 1;
		maxDig := SHORT(ENTIER(-Math.Log(Math.Eps())));
		factor := Math.IntPower(10, maxDig)
	END Init;

BEGIN
	Init
END Strings.
