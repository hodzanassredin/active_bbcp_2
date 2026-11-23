MODULE ConsLog;

	IMPORT Log, Strings, Dialog, Console;

	TYPE
		LogHook = POINTER TO RECORD (Log.Hook) END;
		ShowHook = POINTER TO RECORD (Dialog.ShowHook) END;

	VAR
		hook: LogHook;
		s: ARRAY 1024 OF CHAR;

	PROCEDURE (log: LogHook) Guard* (o: ANYPTR): BOOLEAN;
	BEGIN RETURN TRUE
	END Guard;

	PROCEDURE (log: LogHook) ClearBuf*;
	BEGIN (* has no buffer *)
	END ClearBuf;

	PROCEDURE (log: LogHook) FlushBuf*;
	BEGIN (* has no buffer *)
	END FlushBuf;

	PROCEDURE (log: LogHook) Beep*;
	BEGIN Dialog.Beep
	END Beep;

	PROCEDURE (log: LogHook) Char* (ch: CHAR);
	BEGIN
		Console.WriteChar(ch)
	END Char;

	PROCEDURE (log: LogHook) Int* (n: LONGINT);
	BEGIN
		Strings.IntToString(n, s);
		Console.WriteStr(" " + s)
	END Int;

	PROCEDURE (log: LogHook) Real* (x: REAL);
	BEGIN
		Strings.RealToString(x, s);
		Console.WriteStr(" " + s)
	END Real;

	PROCEDURE (log: LogHook) String* (IN str: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(str)
	END String;

	PROCEDURE (log: LogHook) Bool* (x: BOOLEAN);
	BEGIN
		IF x THEN s := "$TRUE" ELSE s := "$FALSE" END;
		Console.WriteStr(s)
	END Bool;

	PROCEDURE (log: LogHook) Set* (x: SET);
	BEGIN
		Strings.SetToString(x, s);
		Console.WriteStr(s)
	END Set;

	PROCEDURE (log: LogHook) IntForm* (x: LONGINT; base, minWidth: INTEGER; fillCh: CHAR; showBase: BOOLEAN);
	BEGIN
		Strings.IntToStringForm(x, base, minWidth, fillCh, showBase, s);
		Console.WriteStr(s)
	END IntForm;

	PROCEDURE (log: LogHook) RealForm* (x: REAL; precision, minW, expW: INTEGER; fillCh: CHAR);
	BEGIN
		Strings.RealToStringForm(x, precision, minW, expW, fillCh, s);
		Console.WriteStr(s)
	END RealForm;

	PROCEDURE (log: LogHook) Tab*;
	BEGIN
		Console.WriteChar(09X)
	END Tab;

	PROCEDURE (log: LogHook) Ln*;
	BEGIN
		Console.WriteLn
	END Ln;

	PROCEDURE (log: LogHook) Para*;
	BEGIN
		log.Ln
	END Para;

	PROCEDURE (log: LogHook) View* (v: ANYPTR);
	BEGIN
		log.String("View not implemented."); log.Ln
	END View;

	PROCEDURE (log: LogHook) ViewForm* (v: ANYPTR; w, h: INTEGER);
	BEGIN
		log.String("ViewForm not implemented."); log.Ln
	END ViewForm;

	PROCEDURE (log: LogHook) ParamMsg* (IN s, p0, p1, p2: ARRAY OF CHAR);
		VAR msg: ARRAY 256 OF CHAR; i: INTEGER; ch: CHAR;
	BEGIN
		Dialog.MapParamString(s, p0, p1, p2, msg);
		i := 0; ch := msg[0];
		WHILE ch # 0X DO
			IF ch = 0DX THEN log.Ln
			ELSIF ch = 0EX THEN log.Para
			ELSIF ch = 9X THEN log.Tab
			ELSIF ch >= " " THEN log.Char(ch)
			END;
			INC(i); ch := msg[i];
		END;
		log.Ln
	END ParamMsg;
	
	(* ShowHook *)

	PROCEDURE (shook: ShowHook) ShowParamMsg (IN s, p0, p1, p2: ARRAY OF CHAR);
	BEGIN
		hook.ParamMsg(s, p0, p1, p2)
	END ShowParamMsg;
	
	PROCEDURE (shook: ShowHook) ShowParamStatus (IN s, p0, p1, p2: ARRAY OF CHAR);
	BEGIN
		IF Dialog.showsStatus THEN
			hook.ParamMsg(s, p0, p1, p2)
		END
	END ShowParamStatus;

	PROCEDURE Open*;
		VAR showHook: ShowHook;
	BEGIN
		NEW(showHook); 
		Log.SetHook(hook);
		Dialog.SetShowHook(showHook)
	END Open;

BEGIN
	NEW(hook); 
	Open
END ConsLog.
