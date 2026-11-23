MODULE CheckCCallAlias;

	IMPORT DevCPM, Log;

	PROCEDURE Check (amd64: BOOLEAN);
		VAR flag, expected: SHORTINT;
	BEGIN
		DevCPM.options := {10}; expected := -12;
		IF amd64 THEN INCL(DevCPM.options, DevCPM.sysAMD64); expected := -10 END;
		flag := 0; DevCPM.GetProcSysFlag("ccall16", 0, flag); ASSERT(flag = expected);
		flag := 0; DevCPM.GetProcSysFlag("", -12, flag); ASSERT(flag = expected);
		flag := 0; DevCPM.GetProcTypSysFlag("ccall16", 0, flag); ASSERT(flag = expected);
		flag := 0; DevCPM.GetProcTypSysFlag("", -12, flag); ASSERT(flag = expected);
		flag := 0; DevCPM.GetProcSysFlag("ccall", 0, flag); ASSERT(flag = -10);
		flag := 0; DevCPM.GetProcTypSysFlag("ccall", 0, flag); ASSERT(flag = -10);
		IF amd64 THEN
			flag := 0; DevCPM.GetProcSysFlag("ms64", 0, flag); ASSERT(flag = -14);
			flag := 0; DevCPM.GetProcTypSysFlag("ms64", 0, flag); ASSERT(flag = -14)
		END
	END Check;

	PROCEDURE Do*;
		VAR saved: SET;
	BEGIN
		saved := DevCPM.options;
		Check(FALSE); Check(TRUE);
		DevCPM.options := saved;
		Log.String("ccall16 alias: i386 and AMD64 OK"); Log.Ln
	END Do;

END CheckCCallAlias.
