MODULE NbsdRegistry;
(**
	project	= "BlackBox 2.0"
	organization	= "www.oberon.ch blackbox.oberon.org"
	contributors	= "Oberon microsystems, A.Shiryaev, I.Kuzmitski, I.Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		Kernel, Dialog, Strings, Meta, StdRegistry, 
		Files, Stores, Libc := NbsdLibc;

	CONST
		minVersion = 1; maxVersion = 1;
	
	TYPE
		Key = ARRAY 32 OF CHAR;
		Val = ARRAY 256 OF CHAR;

		Hook = POINTER TO RECORD (StdRegistry.Hook) END;

	VAR
		localeId*: INTEGER;
		debug*: BOOLEAN;
		hook: Hook;

	PROCEDURE Msg (IN s: ARRAY OF SHORTCHAR);
		VAR res_: INTEGER;
	BEGIN
		IF debug THEN
			res_ := Libc.printfs("%s" + 0AX, s)
		END
	END Msg;

	PROCEDURE LoadKV (IN key: ARRAY OF CHAR; OUT str: ARRAY OF CHAR; VAR res: INTEGER);
		VAR ss: Libc.PtrSTR; loc: Files.Locator; f: Files.File;
			rd: Stores.Reader;
			x: INTEGER; k: Key; v: Val;
	BEGIN
		ss := Libc.getenv("HOME");
		IF ss # NIL THEN
			(* TODO: encoding translation *)
			loc := Files.dir.This(ss + "/."+Dialog.appName);
			IF loc.res = 0 THEN
				f := Files.dir.Old(loc, "registry", TRUE);
				IF f # NIL THEN
					rd.ConnectTo(f);
					rd.ReadVersion(minVersion, maxVersion, x);
					IF x = maxVersion THEN
						rd.ReadString(k); rd.ReadString(v);
						WHILE ~rd.rider.eof DO
							IF k = key THEN f.Close;
								str := v$; res := 0;
								Msg(SHORT("NbsdRegistry.LoadKV: found: " + key + ": " + v));
								RETURN
							END;
							rd.ReadString(k); rd.ReadString(v)
						END
					ELSE Msg("NbsdRegistry.LoadKV: ~readVersion")
					END;
					f.Close
				END
			END
		END;
		Msg(SHORT("NbsdRegistry.LoadKV: ~found: " + key))
	END LoadKV;

	PROCEDURE SaveKV (IN key, val: ARRAY OF CHAR);
		VAR ss: Libc.PtrSTR; of, nf: Files.File;
			loc: Files.Locator;
			rd: Stores.Reader; wr: Stores.Writer;
			x, res: INTEGER;
			k: Key; v: Val;
			found, register: BOOLEAN;
	BEGIN
		ss := Libc.getenv("HOME");
		IF ss # NIL THEN
			(* TODO: encoding translation *)
			loc := Files.dir.This(ss + "/."+Dialog.appName);
			IF loc.res = 0 THEN
				nf := Files.dir.New(loc, FALSE);
				IF nf # NIL THEN
					wr.ConnectTo(nf); register := TRUE;
					wr.WriteVersion(maxVersion);
					of := Files.dir.Old(loc, "registry", TRUE);
					IF of # NIL THEN
						rd.ConnectTo(of);
						rd.ReadVersion(minVersion, maxVersion, x);
						IF x = maxVersion THEN
							found := FALSE;
							rd.ReadString(k); rd.ReadString(v);
							WHILE ~rd.rider.eof DO
								wr.WriteString(k);
								IF k = key THEN found := TRUE;
									IF v = val THEN register := FALSE END;
									wr.WriteString(val)
								ELSE wr.WriteString(v)
								END;
								rd.ReadString(k); rd.ReadString(v)
							END;
							IF ~found THEN wr.WriteString(key); wr.WriteString(val) END
						ELSE wr.WriteString(key); wr.WriteString(val)
						END;
						of.Close
					ELSE wr.WriteString(key); wr.WriteString(val)
					END;
					IF register THEN nf.Register("registry", "", FALSE, res)
						; IF res = 0 THEN Msg(SHORT("NbsdRegistry: written: " + key + ": " + val))
						ELSE Msg("NbsdRegistry: register failed!")
						END
					ELSE Msg(SHORT("NbsdRegistry: ~modified: " + key + ": " + val))
					END;
					nf.Close
				END
			END
		END
	END SaveKV;

	PROCEDURE (h: Hook) ReadString* (
		IN key: ARRAY OF CHAR; OUT str: ARRAY OF CHAR; VAR res: INTEGER
	);
	BEGIN
		str := ""; res := -1;
		LoadKV(key, str, res)
	END ReadString;
	
	PROCEDURE (h: Hook) ReadInt* (IN key: ARRAY OF CHAR; OUT x: INTEGER; OUT res: INTEGER);
		VAR str: ARRAY 64 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN Strings.StringToInt(str, x, res) END;
	END ReadInt;
	
	PROCEDURE (h: Hook) ReadBool* (IN key: ARRAY OF CHAR; OUT x: BOOLEAN; OUT res: INTEGER);
		VAR str: ARRAY 4 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN x := str = "1" END
	END ReadBool;
	
	PROCEDURE (h: Hook) ReadIntList* (IN key: ARRAY OF CHAR; OUT x: ARRAY OF INTEGER; OUT res: INTEGER);
		VAR i, n, val, sig: INTEGER; str: ARRAY 256 OF CHAR;
	BEGIN
		h.ReadString(key, str, res);
		IF res = 0 THEN
			n := 0; i := 0;
			WHILE str[i] # 0X DO
				IF n >= LEN(x) THEN res := 2; RETURN END;
				val := 0; sig := 1;
				IF str[i] = "-" THEN sig := -1; INC(i) END;
				WHILE (str[i] >= "0") & (str[i] <= "9") DO val := 10 * val + ORD(str[i]) - ORD("0"); INC(i) END;
				x[n] := sig * val;
				IF str[i] = "," THEN INC(i)
				ELSIF str[i] # 0X THEN res := 1; RETURN
				END;
				INC(n)
			END
		END
	END ReadIntList;
	
	PROCEDURE (h: Hook) WriteString* (IN key: ARRAY OF CHAR; IN str: ARRAY OF CHAR);
	BEGIN
		SaveKV(key, str)
	END WriteString;
	
	PROCEDURE (h: Hook) WriteInt* (IN key: ARRAY OF CHAR; x: INTEGER);
		VAR str: ARRAY 64 OF CHAR;
	BEGIN
		Strings.IntToString(x, str);
		h.WriteString(key, str)
	END WriteInt;
	
	PROCEDURE (h: Hook) WriteBool* (IN key: ARRAY OF CHAR; x: BOOLEAN);
	BEGIN
		IF x THEN h.WriteString(key, "1") ELSE h.WriteString(key, "0") END
	END WriteBool;
	
	PROCEDURE (h: Hook) WriteIntList* (IN key: ARRAY OF CHAR; IN x: ARRAY OF INTEGER);
		VAR i,j,  n: INTEGER; str: ARRAY 256 OF CHAR; s: ARRAY 16 OF CHAR;
	BEGIN
		n := 0; i := 0;
		WHILE n < LEN(x) DO
			IF (n > 0) & (i < LEN(str)) THEN str[i] := ","; INC(i) END;
			Strings.IntToString(x[n], s); j := 0;
			WHILE (s[j] # 0X) & (i < LEN(str)) DO str[i] := s[j]; INC(i); INC(j) END;
			INC(n)
		END;
		IF i < LEN(str) THEN str[i] := 0X ELSE str[LEN(str) - 1] := 0X END;
		h.WriteString(key, str)
	END WriteIntList;

	PROCEDURE ReadMeasure;
	BEGIN
(*
		res := WinApi.GetProfileIntW("intl", "iMeasure", 1);
		Dialog.metricSystem := res = 0
*)
		Dialog.metricSystem := TRUE
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
		VAR res: INTEGER; startupCmd: Kernel.Name;
	BEGIN
		localeId := 1024;	(* user default *)
		IF ~ SearchVar("appStartupProcedure", startupCmd) THEN
			startupCmd := "Startup.Setup"
		END;
		Dialog.Call(startupCmd, "", res);
		ReadMeasure;
		NEW(hook); 
		StdRegistry.SetHook(hook);
	END Init;

BEGIN
	debug := FALSE;
	Init
END NbsdRegistry.