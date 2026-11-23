MODULE MdiLoader;
	(* The executable contains the runtime; MdiInit and the GUI remain dynamic. *)
	IMPORT SYSTEM, Kernel, WinKernel, Files, WinFiles, StdLoader, Trace := WinTrace;
	PROCEDURE Load (a, b, c: SYSTEM.ADDRESS);
		VAR mod: Kernel.Module; res: INTEGER;
			importing, imported, object: ARRAY 256 OF CHAR; error: ARRAY 1024 OF CHAR;
	BEGIN
		Trace.Write("before loading MdiInit");
		mod := Kernel.ThisMod("MdiInit");
		Trace.Write("MdiInit returned");
		IF mod = NIL THEN
			Kernel.GetLoaderResult(res, importing, imported, object);
			Trace.Value("loader result", res); Trace.Write(importing); Trace.Write(imported); Trace.Write(object);
			error := "Cannot load MdiInit: " + importing + " IMPORT " + imported + " " + object;
			IF res = 0 THEN res := 1 END;
			Kernel.FatalError(res, error)
		END
	END Load;
BEGIN
	Kernel.Try(Load, 0, 0, 0);
	(* MdiInit owns the event loop and calls Kernel.Quit on normal exit. *)
	Kernel.FatalError(1, "MdiInit returned or trapped during startup")
END MdiLoader.
