MODULE CheckLog;	

	(*
	AD: Aux interface to module Console - platform-independent console output
	*)
	
	IMPORT Console;
	
	CONST max = 25;
	
	TYPE Buffer = ARRAY max OF CHAR;
	
	VAR 
		indent: INTEGER;
		indentation: ARRAY 64 OF CHAR;
	
	
	PROCEDURE Ln*; 
		VAR i: INTEGER; buf: ARRAY 64 OF CHAR;
	BEGIN 
		Console.WriteLn; 
		IF indent > 0 THEN Console.WriteStr(indentation) END
	END Ln;
	
	PROCEDURE String* (IN str: ARRAY OF CHAR); BEGIN Console.WriteStr(str) END String;

	PROCEDURE Char* (ch: CHAR); BEGIN Console.WriteChar(ch) END Char;

	PROCEDURE IntToString (x: LONGINT; OUT a: Buffer);
		VAR b: Buffer; i, j: INTEGER; y: LONGINT;
	BEGIN a[0] := ' '; IF x < 0 THEN x := -x; a[1] := '-'; j := 2 ELSE j := 1 END;
		i := 0; WHILE x # 0 DO y := x MOD 10; b[i] := SHORT(CHR(y + ORD('0'))); x := x DIV 10; INC(i) END;
		DEC(i); WHILE (i >= 0) & (j < max) DO a[j] := b[i]; INC(j); DEC(i) END;
		ASSERT(j < max, 20);
		a[j] := 0X; 
	END IntToString;
	
	PROCEDURE Int* (x: LONGINT);
		VAR buf: Buffer; res: INTEGER;
	BEGIN IF x = 0 THEN buf := " 0" ELSE IntToString(x, buf) END; Console.WriteStr(buf)
	END Int;
	
	
	PROCEDURE SetToString (x: SET; OUT str: ARRAY OF CHAR);
		VAR len, i: INTEGER;

		PROCEDURE AppendChar(ch: CHAR);
		BEGIN ASSERT(len < LEN(str), 23); str[len] := ch; INC(len)
		END AppendChar;

		PROCEDURE AppendInt (x: INTEGER);
			VAR i: INTEGER; ch: CHAR; buf: Buffer;
		BEGIN IntToString(x, buf); ch := buf[0]; i := 0;
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

	PROCEDURE Set* (set: SET);
		VAR s: ARRAY 128 OF CHAR; res: INTEGER;
	BEGIN SetToString(set, s); Console.WriteStr(s)
	END Set;
	
	PROCEDURE Bool* (bool: BOOLEAN);
	BEGIN IF bool THEN Console.WriteStr("$TRUE") ELSE Console.WriteStr("$FALSE") END
	END Bool;

	PROCEDURE HexN* (x: LONGINT; n: INTEGER);
		CONST max = 25;
		VAR i: INTEGER; y: LONGINT; a: Buffer;
	BEGIN ASSERT(n < max, 20); a[n] := 0X;
		i := n - 1;
		WHILE i >= 0 DO
			y := x MOD 16; x := x DIV 16;
			IF y > 9 THEN INC(y, (ORD("A") - ORD("0") - 10)) END;
			a[i] := SHORT(CHR(y + ORD("0"))); DEC(i)
		END;
		Console.WriteStr(a)
	END HexN;

	PROCEDURE Hex* (x: LONGINT); BEGIN HexN(x, 8) END Hex;
	PROCEDURE Str* (IN str: ARRAY OF CHAR); BEGIN Console.WriteStr(str) END Str;
	PROCEDURE Msg* (IN str: ARRAY OF CHAR); BEGIN Console.WriteStr(str) END Msg;
	PROCEDURE MsgLn* (IN str: ARRAY OF CHAR); 
	BEGIN Console.WriteStr(str); Console.WriteLn 
	END MsgLn;
	
	PROCEDURE Indent*; 
	BEGIN 
		indentation[indent] := ' '; INC(indent); indentation[indent] := 0X
	END Indent;
	
	PROCEDURE Unindent*; 
	BEGIN DEC(indent); indentation[indent] := 0X 
	END Unindent;
	
	
END CheckLog.