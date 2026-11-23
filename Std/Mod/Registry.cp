MODULE StdRegistry;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, P. Kushnir, I.Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT 
		Kernel;

	TYPE
		Hook* = POINTER TO ABSTRACT RECORD END;

	VAR
		hook: Hook;

	PROCEDURE (h: Hook) ReadString* (IN key: ARRAY OF CHAR; OUT str: ARRAY OF CHAR; VAR res: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) ReadInt* (IN key: ARRAY OF CHAR; OUT x: INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) ReadBool* (IN key: ARRAY OF CHAR; OUT x: BOOLEAN; OUT res: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) ReadIntList* (IN key: ARRAY OF CHAR; OUT x: ARRAY OF INTEGER; OUT res: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) WriteString* (IN key: ARRAY OF CHAR; IN str: ARRAY OF CHAR), NEW, ABSTRACT;
	PROCEDURE (h: Hook) WriteInt* (IN key: ARRAY OF CHAR; x: INTEGER), NEW, ABSTRACT;
	PROCEDURE (h: Hook) WriteBool* (IN key: ARRAY OF CHAR; x: BOOLEAN), NEW, ABSTRACT;
	PROCEDURE (h: Hook) WriteIntList* (IN key: ARRAY OF CHAR; IN x: ARRAY OF INTEGER), NEW, ABSTRACT;


	PROCEDURE ReadString* (IN key: ARRAY OF CHAR; OUT str: ARRAY OF CHAR; VAR res: INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.ReadString(key, str, res)
		ELSE res := -1 END
	END ReadString;

	PROCEDURE ReadInt* (IN key: ARRAY OF CHAR; OUT x: INTEGER; OUT res: INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.ReadInt(key, x, res)
		ELSE res := -1 END
	END ReadInt;

	PROCEDURE ReadBool* (IN key: ARRAY OF CHAR; OUT x: BOOLEAN; OUT res: INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.ReadBool(key, x, res)
		ELSE res := -1 END
	END ReadBool;

	PROCEDURE ReadIntList* (IN key: ARRAY OF CHAR; OUT x: ARRAY OF INTEGER; OUT res: INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.ReadIntList(key, x, res)
		ELSE res := -1 END
	END ReadIntList;

	PROCEDURE WriteString* (IN key: ARRAY OF CHAR; IN str: ARRAY OF CHAR);
	BEGIN
		IF hook # NIL THEN
			hook.WriteString(key, str)
		END
	END WriteString;

	PROCEDURE WriteInt* (IN key: ARRAY OF CHAR; x: INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.WriteInt(key, x)
		END
	END WriteInt;

	PROCEDURE WriteBool* (IN key: ARRAY OF CHAR; x: BOOLEAN);
	BEGIN
		IF hook # NIL THEN
			hook.WriteBool(key, x)
		END
	END WriteBool;

	PROCEDURE WriteIntList* (IN key: ARRAY OF CHAR; IN x: ARRAY OF INTEGER);
	BEGIN
		IF hook # NIL THEN
			hook.WriteIntList(key, x)
		END
	END WriteIntList;

	PROCEDURE SetHook* (h: Hook);
	BEGIN
		hook := h
	END SetHook;

	PROCEDURE Ready*(): BOOLEAN;
	BEGIN
		RETURN hook # NIL
	END Ready;

END StdRegistry.