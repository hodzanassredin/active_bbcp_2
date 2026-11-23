MODULE WinEnv;

	IMPORT SYSTEM, WinApi;

	CONST
		ok* = 0; tooLong* = 1; notFound* = 2; badChars* = 3; 

	VAR
		argc: INTEGER;
		argv: WinApi.RetCommandLineToArgvW;

	PROCEDURE GetEnv (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
		VAR	res1: INTEGER;
	BEGIN
		res1 := WinApi.GetEnvironmentVariableW(SYSTEM.ADR(key), SYSTEM.ADR(val), LEN(val));
		IF res1 > LEN(val) THEN
			res := tooLong
		ELSIF res1 = 0 THEN
			res1 := WinApi.GetLastError();
			IF res1 = WinApi.ERROR_ENVVAR_NOT_FOUND THEN
				res := notFound
			ELSIF res1 # 0 THEN
				res := badChars
			ELSE
				res := ok; val := ""
			END
		ELSE
			res := ok
		END;
		ASSERT(res IN {0..3})
	END GetEnv;

	PROCEDURE SubStr (IN s: ARRAY OF CHAR; IN sub: ARRAY OF CHAR): BOOLEAN; 
		VAR	i: INTEGER;
	BEGIN
		i := 0;
		WHILE (s[i] # 0X) & (sub[i] # 0X) & (s[i] = sub[i]) DO INC(i) END;
		RETURN ~(sub[i] # 0X)
	END SubStr;

	PROCEDURE Extract (IN s: ARRAY OF CHAR; pos: INTEGER;
		OUT t: ARRAY OF CHAR; OUT res: INTEGER);
		VAR	i: INTEGER;
	BEGIN
		ASSERT(0 <= pos);
		i := 0; WHILE (i < LEN(t)) & (s[pos] # 0X) DO t[i] := s[pos]; INC(i); INC(pos) END;
		IF i < LEN(t) THEN
			t[i] := 0X; res := ok
		ELSE
			t[i - 1] := 0X; res := tooLong
		END
	END Extract;

	PROCEDURE GetPar* (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
		VAR i: INTEGER;
	BEGIN
		res := -1; i := 1;
		IF (key = "") & (argc = 2) THEN
			IF LEN(argv[1]$) < LEN(val) THEN
				res := ok; val := argv[1]$
			ELSE
				res := tooLong; val := ""
			END
		ELSE
			WHILE (res = -1) & (i < argc) DO
				IF (argv[i,0] = "-") & (argv[i]$ = "-" + key)
					OR (argv[i,0] = "/") & (argv[i]$ = "/" + key) THEN
					INC(i);
					IF (i < argc) & (argv[i,0] # "-") & (argv[i,0] # "/") THEN
						Extract(argv[i]$, 0, val, res)
					ELSE
						res := ok; val := ""
					END
				ELSIF (argv[i,0] = "-") & (argv[i,1] = "-") & SubStr(argv[i]$, "--" + key + "=") THEN
					Extract(argv[i]$, LEN(key$) + 3, val, res)
				ELSE
					INC(i)
				END
			END;
			IF res = -1 THEN GetEnv(key, val, res) END;
		END;
		ASSERT(res IN {0..3});
		IF ~(res IN {0..1}) THEN val := "" END
	END GetPar;

BEGIN
	argv := WinApi.CommandLineToArgvW(WinApi.GetCommandLineW(), argc);
	IF argv = NIL THEN argc := 0 END
CLOSE
	IF argv # NIL THEN
		IF WinApi.LocalFree(SYSTEM.VAL(WinApi.HLOCAL, argv)) # 0 THEN HALT(100) END;
		argv := NIL; argc := 0
	END
END WinEnv.
