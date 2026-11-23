MODULE WinRegistry;
(**
	project	= "BlackBox 2.0"
	organization	= ""
	contributors	= ""
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Kernel, Dialog, Strings, Meta, StdRegistry, 
		WinApi, WinFiles;

	TYPE
		Hook = POINTER TO RECORD (StdRegistry.Hook) END;

	VAR 
		localeId*: INTEGER;
		hook: Hook;

	PROCEDURE (h: Hook)  ReadString (
		IN key: ARRAY OF CHAR; OUT str: ARRAY OF CHAR; VAR res: INTEGER
	);
		VAR r, len: INTEGER; sw, comp, appl, vers: WinApi.HANDLE;
	BEGIN
		res := WinApi.RegOpenKeyW(WinApi.HKEY_CURRENT_USER, "Software", sw);
		IF res = 0 THEN
			res := WinApi.RegOpenKeyW(sw, "Oberon", comp);
			IF res = 0 THEN
				res := WinApi.RegOpenKeyW(comp, Dialog.appName, appl);
				IF res = 0 THEN
					res := WinApi.RegOpenKeyW(appl, "CurrentVersion", vers);
					IF res = 0 THEN
						len := LEN(str);
						res := WinApi.RegQueryValueW(vers, key, str, len);
						r := WinApi.RegCloseKey(vers)
					END;
					r := WinApi.RegCloseKey(appl)
				END;
				r := WinApi.RegCloseKey(comp)
			END;
			r := WinApi.RegCloseKey(sw)
		END
	END ReadString;

	PROCEDURE (h: Hook) ReadInt (IN key: ARRAY OF CHAR; OUT x: INTEGER; OUT res: INTEGER);
		VAR str: ARRAY 64 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN Strings.StringToInt(str, x, res) END;
	END ReadInt;

	PROCEDURE (h: Hook) ReadBool (IN key: ARRAY OF CHAR; OUT x: BOOLEAN; OUT res: INTEGER);
		VAR str: ARRAY 4 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN x := str = "1" END
	END ReadBool;

	PROCEDURE (h: Hook) ReadIntList (IN key: ARRAY OF CHAR; OUT x: ARRAY OF INTEGER; OUT res: INTEGER);
		VAR i, n, val, sig: INTEGER; str: ARRAY 256 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN
			n := 0; i := 0;
			WHILE str[i] # 0X DO
				IF n >= LEN(x) THEN res := 2; RETURN END;
				val := 0; sig := 1;
				IF str[i] = "-" THEN sig := -1; INC(i) END;
				WHILE (str[i] >= "0") & (str[i] <= "9") DO
					val := 10 * val + ORD(str[i]) - ORD("0"); INC(i)
				END;
				x[n] := sig * val;
				IF str[i] = "," THEN INC(i)
				ELSIF str[i] # 0X THEN res := 1; RETURN
				END;
				INC(n)
			END
		END
	END ReadIntList;

	PROCEDURE (h: Hook) WriteString (IN key: ARRAY OF CHAR; IN str: ARRAY OF CHAR);
		VAR res: INTEGER; sw, comp, appl, vers: WinApi.HANDLE;
	BEGIN
		res := WinApi.RegCreateKeyW(WinApi.HKEY_CURRENT_USER, "Software", sw);
		IF res = 0 THEN
			res := WinApi.RegCreateKeyW(sw, "Oberon", comp);
			IF res = 0 THEN
				res := WinApi.RegCreateKeyW(comp, Dialog.appName, appl);
				IF res = 0 THEN
					res := WinApi.RegCreateKeyW(appl, "CurrentVersion", vers);
					IF res = 0 THEN
						res := WinApi.RegSetValueW(vers, key, WinApi.REG_SZ, str, LEN(str$) * 2)
					END;
					res := WinApi.RegCloseKey(appl)
				END;
				res := WinApi.RegCloseKey(comp)
			END;
			res := WinApi.RegCloseKey(sw)
		END
	END WriteString;

	PROCEDURE (h: Hook) WriteInt (IN key: ARRAY OF CHAR; x: INTEGER);
		VAR str: ARRAY 64 OF CHAR;
	BEGIN
		Strings.IntToString(x, str);
		h.WriteString(key, str)
	END WriteInt;

	PROCEDURE (h: Hook) WriteBool (IN key: ARRAY OF CHAR; x: BOOLEAN);
	BEGIN
		IF x THEN h.WriteString(key, "1") ELSE h.WriteString(key, "0") END
	END WriteBool;

	PROCEDURE (h: Hook) WriteIntList (IN key: ARRAY OF CHAR; IN x: ARRAY OF INTEGER);
		VAR i,j,  n: INTEGER; str: ARRAY 256 OF CHAR; s: ARRAY 16 OF CHAR;
	BEGIN
		n := 0; i := 0;
		WHILE n < LEN(x) DO
			IF (n > 0) & (i < LEN(str)) THEN str[i] := ","; INC(i) END;
			Strings.IntToString(x[n], s); j := 0;
			WHILE (s[j] # 0X) & (i < LEN(str)) DO
				str[i] := s[j]; INC(i); INC(j)
			END;
			INC(n)
		END;
		IF i < LEN(str) THEN str[i] := 0X ELSE str[LEN(str) - 1] := 0X END;
		h.WriteString(key, str)
	END WriteIntList;

	PROCEDURE ReadMeasure;
		VAR res, len: INTEGER; cp, int: WinApi.HANDLE; str: ARRAY 4 OF CHAR;
	BEGIN
		res := WinApi.GetProfileIntW("intl", "iMeasure", 1);
		Dialog.metricSystem := res = 0;
		NEW(hook); 
		StdRegistry.SetHook(hook);
	END ReadMeasure;

	PROCEDURE SearchVar (IN name: Kernel.Name; OUT cmd: Kernel.Name): BOOLEAN;
		VAR m: Kernel.Module; module, variable: Meta.Item; ok: BOOLEAN;
			var: RECORD (Meta.Value) cmd: Kernel.Name END;
	BEGIN
		m := Kernel.modList;
		ok := FALSE;
		WHILE (m # NIL) & ~ ok DO
			IF ~ (m.refcnt < 0) THEN
				Meta.Lookup(m.name$, module);
				IF module.obj = Meta.modObj THEN
					module.Lookup(name, variable);
					IF variable.obj = Meta.varObj THEN
						variable.GetVal(var, ok);
						IF ok THEN
							cmd := var.cmd
						END
					END
				END
			END;
			m := m.next
		END;
		RETURN ok
	END SearchVar;

	PROCEDURE Init;
		VAR name, bname: Dialog.String; res, i, v: INTEGER;
			startupCmd: Kernel.Name;
	BEGIN
		v := WinApi.GetVersion();
		localeId := 1024;	(* user default *)
		name := WinFiles.appName$;
		bname := "BLACKBOX"; i := 0;
		WHILE (i < 8) & (CAP(name[i]) = bname[i]) DO INC(i) END;
		IF i = 8 THEN
			name := "BlackBox"
		ELSIF (v < 0) & (v MOD 256 < 4) THEN
			i := 1;
			WHILE name[i] # 0X DO
				IF (name[i] >= "A") & (name[i] <= "Z") THEN
					name[i] := CHR(ORD(name[i]) + ORD("a") - ORD("A"))
				END;
				INC(i)
			END
		END;
		Dialog.appName := name$;
		IF ~ SearchVar("appStartupProcedure", startupCmd) THEN
			startupCmd := "Startup.Setup"
		END;
		Dialog.Call(startupCmd, "", res);
		ReadMeasure
	END Init;

BEGIN
	Init
END WinRegistry.