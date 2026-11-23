MODULE LinEnv;

	IMPORT Utf, HostKernel := LinKernel, Libc := LinLibc;

	PROCEDURE GetEnv (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
		VAR	sbuf: ARRAY 4096 OF SHORTCHAR; p: Libc.PtrSTR;
	BEGIN
		Utf.StringToUtf8(key, sbuf, res); ASSERT(res = 0, 100);
		p := Libc.getenv(sbuf);
		IF p = NIL THEN
			res := 2
		ELSE
			Utf.Utf8ToString(p$, val, res);
			ASSERT((0 <= res) & (res <= 2), 100);
			IF res = 2 THEN res := 3	(* ошибка преобразования *) END
		END;
		ASSERT(res IN {0..3})
	END GetEnv;

	PROCEDURE GetCmdLineArg (i: INTEGER; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
	BEGIN
		IF ~((0 <= i) & (i < HostKernel.argc)) THEN
			res := 2	(* не найден *)
		ELSE
			Utf.Utf8ToString(HostKernel.argv[i]$, val, res);
			ASSERT((0 <= res) & (res <= 2), 100);
			IF res = 2 THEN res := 3	(* ошибка преобразования *) END
		END;
		ASSERT(res IN {0..3})
	END GetCmdLineArg;

	PROCEDURE SubStr (VAR s: ARRAY OF CHAR; IN sub: ARRAY OF CHAR): BOOLEAN; 
		VAR	ch: CHAR; l: INTEGER; res: BOOLEAN;
	BEGIN
		l := LEN(sub$);
		res := l < LEN(s);
		IF res THEN ch := s[l]; s[l] := 0X; res := s$ = sub$; s[l] := ch END;
		RETURN res
	END SubStr;

	PROCEDURE Extract (IN s: ARRAY OF CHAR; pos: INTEGER;OUT t: ARRAY OF CHAR; OUT res: INTEGER);
		VAR	i: INTEGER;
	BEGIN
		ASSERT(0 <= pos);
		i := 0; WHILE (i < LEN(t)) & (s[pos] # 0X) DO t[i] := s[pos]; INC(i); INC(pos) END;
		IF i < LEN(t) THEN
			t[i] := 0X; res := 0
		ELSE
			t[i - 1] := 0X; res := 1
		END
	END Extract;

	PROCEDURE GetPar* (IN key: ARRAY OF CHAR; OUT val: ARRAY OF CHAR; OUT res: INTEGER);
		VAR	i, res1: INTEGER; buf: ARRAY 1024 OF CHAR;
	BEGIN
		res := -1; i := 1; GetCmdLineArg(i, buf, res1);
		WHILE (res = -1) & (res1 # 2) DO
			IF (buf[0] = "-") & (buf = "-" + key) THEN
				INC(i); GetCmdLineArg(i, buf, res1);
				IF res1 = 3 THEN
					res := 3
				ELSIF (res1 IN {0..1}) & (buf[0] # "-") THEN
					Extract(buf, 0, val, res)
				ELSE
					res := 0; val := ""
				END
			ELSIF (buf[0] = "-") & (buf[1] = "-") & SubStr(buf, "--" + key + "=") THEN
				Extract(buf, LEN(key$) + 3, val, res)
			ELSE
				INC(i); GetCmdLineArg(i, buf, res1)
			END
		END;
		IF res = -1 THEN GetEnv(key, val, res) END;
		ASSERT(res IN {0..3});
		IF ~(res IN {0..1}) THEN val := "" END
	END GetPar;

END LinEnv.
