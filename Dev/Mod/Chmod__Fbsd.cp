MODULE DevChmod__Fbsd;

	IMPORT Libc := FbsdLibc, Files, DevLnkBase, Utf;

	TYPE
		ChmodHook = POINTER TO RECORD (DevLnkBase.ChmodHook) END;

	VAR
		hook: ChmodHook;

	PROCEDURE (hook: ChmodHook) Chmod* (name0: Files.Name; mode: SET);
	VAR name: ARRAY LEN(Files.Name) OF SHORTCHAR; res_: INTEGER;
	BEGIN
		IF DevLnkBase.outputType # "" THEN
			Files.dir.GetFileName(name0, DevLnkBase.outputType, name0)
		END;
		Utf.StringToUtf8(name0, name, res_);
		res_ := Libc.chmod(name, SHORT(ORD(mode))) (* rwxrwxr-x *)
	END Chmod;

BEGIN
	NEW(hook);
	DevLnkBase.SetChmodHook(hook)

END DevChmod__Fbsd.

