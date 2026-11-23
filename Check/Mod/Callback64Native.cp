MODULE CheckCallback64Native;
 (* Production-style example: imports kernel32.dll only, no native helper. *)
 IMPORT S := SYSTEM, A := CheckCallback64Api;
 VAR once, context, veh: S.ADDRESS; calls, traps, result: INTEGER; cb: A.Callback;
 PROCEDURE [ms64] Callback (p, arg, out: S.ADDRESS): INTEGER;
 BEGIN
  IF (p # S.ADR(once)) OR (arg # 100000004L) THEN A.ExitProcess(71) END;
  INC(calls); S.PUT(out, arg); RETURN 1
 END Callback;
 PROCEDURE [ms64] Handler (info: S.ADDRESS): INTEGER;
  VAR rec: S.ADDRESS; code: INTEGER;
 BEGIN
  S.GET(info, rec); S.GET(rec, code);
  IF code # 0E0424242H THEN RETURN 0 END;
  INC(traps); RETURN -1
 END Handler;
BEGIN
 cb := Callback;
 result := A.InitOnceExecuteOnce(S.ADR(once), cb, 100000004L, S.ADR(context));
 IF (result = 0) OR (context # 100000004L) OR (calls # 1) THEN A.ExitProcess(72) END;
 result := cb(S.ADR(once), 100000004L, S.ADR(context));
 IF (result # 1) OR (calls # 2) THEN A.ExitProcess(73) END;
 result := Callback(S.ADR(once), 100000004L, S.ADR(context));
 IF (result # 1) OR (calls # 3) THEN A.ExitProcess(77) END;
 veh := A.AddVectoredExceptionHandler(1, S.ADR(Handler));
 IF veh = 0 THEN A.ExitProcess(74) END;
 A.RaiseException(0E0424242H, 0, 0, 0);
 result := A.RemoveVectoredExceptionHandler(veh);
 IF (result = 0) OR (traps # 1) THEN A.ExitProcess(76) END;
 A.ExitProcess(0)
END CheckCallback64Native.
