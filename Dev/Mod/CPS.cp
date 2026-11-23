MODULE DevCPS;
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	references	= "https://doi.org/10.3929/ethz-a-000945227"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, Math, Strings, Utf, DevCPM, DevCPT;
	
	CONST
		MaxIdLen = LEN(DevCPT.Name);
	
	(* name, str, numtyp, intval, realval, realval are implicit results of Get *)

	VAR
		name*: DevCPT.Name;
		str*: DevCPT.String;
		lstr*: POINTER TO ARRAY OF CHAR;
		numtyp*: SHORTINT; (* 1 = char, 2 = integer, 4 = real, 5 = int64, 6 = real32, 7 = real64 *)
		intval*: INTEGER;	(* integer value or string length (incl. 0X) *)
		realval*: REAL;


	CONST
		(* numtyp values *)
		char = 1; integer = 2; real = 4; int64 = 5; real32 = 6; real64 = 7;

		(* symbol values *)
		null* = 0; times* = 1; slash* = 2; div* = 3; mod* = 4;
		and* = 5; plus* = 6; minus* = 7; or* = 8; eql* = 9;
		neq* = 10; lss* = 11; leq* = 12; gtr* = 13; geq* = 14;
		in* = 15; is* = 16; arrow* = 17; dollar* = 18; period* = 19;
		comma* = 20; colon* = 21; upto* = 22; rparen* = 23; rbrak* = 24;
		rbrace* = 25; of* = 26; then* = 27; do* = 28; to* = 29;
		by* = 30; not* = 33;
		lparen* = 40; lbrak* = 41; lbrace* = 42; becomes* = 44;
		number* = 45; nil* = 46; string* = 47; ident* = 48; semicolon* = 49;
		bar* = 50; end* = 51; else* = 52; elsif* = 53; until* = 54;
		if* = 55; case* = 56; while* = 57; repeat* = 58; for* = 59;
		loop* = 60; with* = 61; exit* = 62; return* = 63; array* = 64;
		record* = 65; pointer* = 66; begin* = 67; const* = 68; type* = 69;
		var* = 70; out* = 71; procedure* = 72; close* = 73; import* = 74;
		module* = 75; eof* = 76;


	VAR
		ch: CHAR;     (*current character*)
		checkKeyword*: PROCEDURE (IN name: ARRAY OF CHAR; VAR sym: BYTE);

	PROCEDURE err(n: SHORTINT);
	BEGIN DevCPM.err(n)
	END err;
	
	PROCEDURE Str(VAR sym: BYTE);
		VAR i: INTEGER; och: CHAR; lch: CHAR; long: BOOLEAN;
			s: ARRAY 256 OF CHAR; t: POINTER TO ARRAY OF CHAR;
	BEGIN i := 0; och := ch; long := FALSE;
		LOOP DevCPM.Get(lch);
			IF lch = och THEN EXIT END ;
			IF (lch < " ") & (lch # 9X) THEN err(3); EXIT END;
			IF lch > 0FFX THEN long := TRUE END;
			IF i < LEN(s) - 1 THEN s[i] := lch
			ELSIF i = LEN(s) - 1 THEN s[i] := 0X; NEW(lstr, 2 * LEN(s)); lstr^ := s$; lstr[i] := lch
			ELSIF i < LEN(lstr^) - 1 THEN lstr[i] := lch
			ELSE t := lstr; t[i] := 0X; NEW(lstr, 2 * LEN(t^)); lstr^ := t^$; lstr[i] := lch
			END;
			INC(i)
		END ;
		IF i = 1 THEN sym := number; numtyp := 1; intval := ORD(s[0])
		ELSE
			sym := string; numtyp := 0; intval := i + 1; NEW(str, intval);
			IF long THEN
				IF i < LEN(s) THEN s[i] := 0X; NEW(lstr, intval); lstr^ := s$
				ELSE lstr[i] := 0X
				END;
				str^ := SHORT(lstr$)
			ELSE
				IF i < LEN(s) THEN s[i] := 0X; str^ := SHORT(s$);
				ELSE lstr[i] := 0X; str^ := SHORT(lstr$)
				END;
				lstr := NIL
			END
		END;
		DevCPM.Get(ch)
	END Str;
	
	PROCEDURE StdKeywords* (VAR sym: BYTE);
	BEGIN
    CASE name[0] OF
    | "A": 
          IF name = "ARRAY" THEN sym := array 
          END
    | "B": 
          IF name = "BEGIN" THEN sym := begin
          ELSIF name = "BY" THEN sym := by
          END
    | "C": 
          IF name = "CASE" THEN sym := case
          ELSIF name = "CONST" THEN sym := const
          ELSIF name = "CLOSE" THEN sym := close
          END
    | "D": 
          IF name = "DO" THEN sym := do
          ELSIF name = "DIV" THEN sym := div
          END
    | "E": 
          IF name = "END" THEN sym := end
          ELSIF name = "ELSE" THEN sym := else
          ELSIF name = "ELSIF" THEN sym := elsif
          ELSIF name = "EXIT" THEN sym := exit
          END
    | "F": IF name = "FOR" THEN sym := for END
    | "I": 
          IF name = "IF" THEN sym := if
          ELSIF name = "IN" THEN sym := in
          ELSIF name = "IS" THEN sym := is
          ELSIF name = "IMPORT" THEN sym := import
          END
    | "L": 
          IF name = "LOOP" THEN sym := loop 
          END
    | "M": 
          IF name = "MOD" THEN sym := mod
          ELSIF name = "MODULE" THEN sym := module
          END
    | "N": IF name = "NIL" THEN sym := nil END
    | "O": 
          IF name = "OR" THEN sym := or
          ELSIF name = "OF" THEN sym := of
          ELSIF name = "OUT" THEN sym := out
          END
    | "P": 
          IF name = "PROCEDURE" THEN sym := procedure
          ELSIF name = "POINTER" THEN sym := pointer
          END
    | "R": 
          IF name = "RECORD" THEN sym := record
          ELSIF name = "REPEAT" THEN sym := repeat
          ELSIF name = "RETURN" THEN sym := return
          END
    | "T": 
          IF name = "THEN" THEN sym := then
          ELSIF name = "TO" THEN sym := to
          ELSIF name = "TYPE" THEN sym := type
          END
    | "U": IF name = "UNTIL" THEN sym := until END
    | "V": IF name = "VAR" THEN sym := var END
    | "W": 
          IF name = "WHILE" THEN sym := while
          ELSIF name = "WITH" THEN sym := with
          END
		ELSE 
		END;
	END StdKeywords;

	PROCEDURE Identifier(VAR sym: BYTE);
		VAR i, res: INTEGER; n: ARRAY MaxIdLen OF CHAR;
	BEGIN i := 0;
		REPEAT
			n[i] := ch; INC(i); DevCPM.Get(ch)
		UNTIL ~Strings.IsIdent(ch) OR (i = MaxIdLen);
		IF i = MaxIdLen THEN err(240); DEC(i) END ;
		n[i] := 0X; Utf.StringToUtf8(n, name, res); sym := ident;
		IF res = 1 (*truncated*) THEN 
			err(240) 
		ELSIF checkKeyword # NIL THEN 
			checkKeyword(n, sym)
		ELSE
			StdKeywords(sym)
		END
	END Identifier;

	PROCEDURE Number;
		VAR i, j, m, n, d, e, a: INTEGER; f, g: REAL; tch: CHAR; neg: BOOLEAN; r: SHORTREAL;
			dig: ARRAY 30 OF CHAR; arr: ARRAY 2 OF INTEGER;

		PROCEDURE Ord(ch: CHAR; hex: BOOLEAN): INTEGER;
		BEGIN (* ("0" <= ch) & (ch <= "9") OR ("A" <= ch) & (ch <= "F") *)
			IF ch <= "9" THEN RETURN ORD(ch) - ORD("0")
			ELSIF hex THEN RETURN ORD(ch) - ORD("A") + 10
			ELSE err(2); RETURN 0
			END
		END Ord;
		
	BEGIN (* ("0" <= ch) & (ch <= "9") *)
		i := 0; m := 0; n := 0; d := 0;
		LOOP (* read mantissa *)
			IF ("0" <= ch) & (ch <= "9") OR (d = 0) & ("A" <= ch) & (ch <= "F") THEN
				IF (m > 0) OR (ch # "0") THEN (* ignore leading zeros *)
					IF n < LEN(dig) THEN dig[n] := ch; INC(n) END;
					INC(m)
				END;
				DevCPM.Get(ch); INC(i)
			ELSIF ch = "." THEN DevCPM.Get(ch);
				IF ch = "." THEN (* ellipsis *) ch := 7FX; EXIT
				ELSIF d = 0 THEN (* i > 0 *) d := i
				ELSE err(2)
				END
			ELSE EXIT
			END
		END; (* 0 <= n <= m <= i, 0 <= d <= i *)
		IF d = 0 THEN (* integer *) realval := 0; numtyp := integer;
			IF n = m THEN intval := 0; i := 0;
				IF ch = "X" THEN (* character *) DevCPM.Get(ch); numtyp := char;
					IF n <= 4 THEN
						WHILE i < n DO intval := intval*10H + Ord(dig[i], TRUE); INC(i) END
					ELSE err(203)
					END
				ELSIF (ch = "H") OR (ch = "S") THEN	(* hex 32bit *)
					tch := ch; DevCPM.Get(ch);
					IF (ch = "L") & (DevCPM.oberon IN DevCPM.options) THEN	(* old syntax: hex 64bit *)
						DevCPM.searchpos := DevCPM.curpos - 2; DevCPM.Get(ch);
						IF n <= 16 THEN
							IF (n = 16) & (dig[0] > "7") THEN realval := -1 END;
							WHILE (i < n) & (i < 10) DO realval := realval * 10H + Ord(dig[i], TRUE); INC(i) END;
							WHILE i < n DO realval := realval * 10H; intval := intval * 10H + Ord(dig[i], TRUE); INC(i) END;
							numtyp := int64
						ELSE err(203)
						END
					ELSIF n <= 8 THEN
						IF (n = 8) & (dig[0] > "7") THEN (* prevent overflow *) intval := -1 END;
						WHILE i < n DO intval := intval*10H + Ord(dig[i], TRUE); INC(i) END;
						IF tch = "S" THEN	(* 32 bit hex float *)
							r := SYSTEM.VAL(SHORTREAL, intval);
							realval := r; intval := 0; numtyp := real32
						END
					ELSE err(203)
					END
				ELSIF ch = "L" THEN	(* hex 64bit *)
					DevCPM.searchpos := DevCPM.curpos - 1; DevCPM.Get(ch);
					IF n <= 16 THEN
						IF (n = 16) & (dig[0] > "7") THEN realval := -1 END;
						WHILE (i < n) & (i < 10) DO realval := realval * 10H + Ord(dig[i], TRUE); INC(i) END;
						WHILE i < n DO realval := realval * 10H; intval := intval * 10H + Ord(dig[i], TRUE); INC(i) END;
						numtyp := int64
					ELSE err(203)
					END
				ELSIF ch = "R" THEN	(* hex float 64bit *)
					DevCPM.searchpos := DevCPM.curpos - 1; DevCPM.Get(ch);
					IF n <= 16 THEN
						a := 0; IF (n = 16) & (dig[0] > "7") THEN (* prevent overflow *) a := -1 END;
						WHILE i < n-8 DO a := a*10H + Ord(dig[i], TRUE); INC(i) END;
						IF DevCPM.LEHost THEN arr[1] := a ELSE arr[0] := a END;
						a := 0; IF (n >= 8) & (dig[i] > "7") THEN (* prevent overflow *) a := -1 END;
						WHILE i < n DO a := a*10H + Ord(dig[i], TRUE); INC(i) END;
						IF DevCPM.LEHost THEN arr[0] := a ELSE arr[1] := a END;
						realval := SYSTEM.VAL(REAL, arr);
						intval := 0; numtyp := real64
					ELSE err(203)
					END
				ELSE	(* decimal *) 
					WHILE i < n DO d := Ord(dig[i], FALSE); INC(i);
						a := (MAX(INTEGER) - d) DIV 10;
						IF intval > a THEN
							a := (intval - a + 65535) DIV 65536 * 65536;
							realval := realval + a; intval := intval - a
						END;
						realval := realval * 10; intval := intval * 10 + d
					END;
					IF realval = 0 THEN numtyp := integer
					ELSIF intval < 9223372036854775808.0E0 - realval THEN numtyp := int64	(* 2^63 *)
					ELSE intval := 0; err(203)
					END
				END
			ELSE err(203)
			END
		ELSE (* fraction *)
			f := 0; g := 0; e := 0; j := 0;
			WHILE (j < 15) & (j < n) DO g := g * 10 + Ord(dig[j], FALSE); INC(j) END;	(* !!! *)
			WHILE n > j DO (* 0 <= f < 1 *) DEC(n); f := (Ord(dig[n], FALSE) + f)/10 END;
			IF (ch = "E") OR (ch = "D") & (DevCPM.oberon IN DevCPM.options) THEN
				DevCPM.searchpos := DevCPM.curpos - 1; DevCPM.Get(ch); neg := FALSE;
				IF ch = "-" THEN neg := TRUE; DevCPM.Get(ch)
				ELSIF ch = "+" THEN DevCPM.Get(ch)
				END;
				IF ("0" <= ch) & (ch <= "9") THEN
					REPEAT n := Ord(ch, FALSE); DevCPM.Get(ch);
						IF e <= (MAX(SHORTINT) - n) DIV 10 THEN e := SHORT(e*10 + n)
						ELSE err(203)
						END
					UNTIL (ch < "0") OR ("9" < ch);
					IF neg THEN e := -e END
				ELSE err(2)
				END
			END;
			DEC(e, i-d-m); (* decimal point shift *)
			IF e < -308 - 16 THEN
				realval := 0.0
			ELSIF e < -308 + 14 THEN
				realval := (f + g) / Math.IntPower(10, j-e-30) / 1.0E15 / 1.0E15
			ELSIF e < j THEN
				realval := (f + g) / Math.IntPower(10, j-e)	(* Ten(j-e) *)
			ELSIF e <= 308 THEN
				realval := (f + g) * Math.IntPower(10, e-j)	(* Ten(e-j) *)
			ELSIF e = 308 + 1 THEN
				realval := (f + g) * (Math.IntPower(10, e-j) / 16);
				IF realval <= DevCPM.MaxReal64 / 16 THEN realval := realval * 16
				ELSE err(203)
				END
			ELSE err(203)
			END;
			numtyp := real
		END
	END Number;

	PROCEDURE Get*(VAR sym: BYTE);
		VAR s: BYTE; old: INTEGER;

		PROCEDURE Comment;	(* do not read after end of file *)
		BEGIN DevCPM.Get(ch);
			LOOP
				LOOP
					WHILE ch = "(" DO DevCPM.Get(ch);
						IF ch = "*" THEN Comment END
					END ;
					IF ch = "*" THEN DevCPM.Get(ch); EXIT END ;
					IF ch = DevCPM.Eot THEN EXIT END ;
					DevCPM.Get(ch)
				END ;
				IF ch = ")" THEN DevCPM.Get(ch); EXIT END ;
				IF ch = DevCPM.Eot THEN err(5); EXIT END
			END
		END Comment;

	BEGIN
		DevCPM.errpos := DevCPM.curpos-1;
		WHILE (ch <= " ") OR (ch = 0A0X) DO (*ignore control characters*)
			IF ch = DevCPM.Eot THEN sym := eof; RETURN
			ELSE DevCPM.Get(ch)
			END
		END ;
		DevCPM.startpos := DevCPM.curpos - 1;
		CASE ch OF   (* ch > " " *)
			| 22X, 27X  : Str(s)
			| "#"  : s := neq; DevCPM.Get(ch)
			| "&"  : s :=  and; DevCPM.Get(ch)
			| "("  : DevCPM.Get(ch);
					 IF ch = "*" THEN Comment; old := DevCPM.errpos; Get(s); DevCPM.errpos := old; 
					 ELSE s := lparen
					 END
			| ")"  : s := rparen; DevCPM.Get(ch)
			| "*"  : s :=  times; DevCPM.Get(ch)
			| "+"  : s :=  plus; DevCPM.Get(ch)
			| ","  : s := comma; DevCPM.Get(ch)
			| "-"  : s :=  minus; DevCPM.Get(ch)
			| "."  : DevCPM.Get(ch);
							 IF ch = "." THEN DevCPM.Get(ch); s := upto ELSE s := period END
			| "/"  : s := slash;  DevCPM.Get(ch)
			| "0".."9": Number; s := number
			| ":"  : DevCPM.Get(ch);
							 IF ch = "=" THEN DevCPM.Get(ch); s := becomes ELSE s := colon END
			| ";"  : s := semicolon; DevCPM.Get(ch)
			| "<"  : DevCPM.Get(ch);
							 IF ch = "=" THEN DevCPM.Get(ch); s := leq ELSE s := lss END
			| "="  : s :=  eql; DevCPM.Get(ch)
			| ">"  : DevCPM.Get(ch);
							 IF ch = "=" THEN DevCPM.Get(ch); s := geq ELSE s := gtr END
			| "A".."Z", "a".."z", "_": Identifier(s)
			| "["  : s := lbrak; DevCPM.Get(ch)
			| "]"  : s := rbrak; DevCPM.Get(ch)
			| "^"  : s := arrow; DevCPM.Get(ch)
			| "$"  : s := dollar; DevCPM.Get(ch)
			| "{"  : s := lbrace; DevCPM.Get(ch);
			| "|"  : s := bar; DevCPM.Get(ch)
			| "}"  : s := rbrace; DevCPM.Get(ch)
			| "~"  : s := not; DevCPM.Get(ch)
			| 7FX  : s := upto; DevCPM.Get(ch)
		ELSE
			IF Strings.IsIdent(ch) THEN Identifier(s) ELSE s :=  null; DevCPM.Get(ch) END
		END ;
		sym := s
	END Get;

	PROCEDURE Init*;
	BEGIN
		ch := " "
	END Init;

END DevCPS.