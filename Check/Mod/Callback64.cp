MODULE CheckCallback64;
 IMPORT S := SYSTEM, A := CheckCallback64Api, P := CheckCallback64Probe;
 VAR once, context, veh, active: S.ADDRESS; calls, result, traps: INTEGER; cb: A.Callback;
 PROCEDURE [ms64] Callback (p, arg, out: S.ADDRESS): INTEGER;
 BEGIN
  IF (p # S.ADR(once)) OR (arg # 100000004L) THEN A.ExitProcess(71) END;
  INC(calls); S.PUT(out, arg); RETURN 1
 END Callback;
 PROCEDURE [ms64] Six (a,b,c,d,e,f: S.ADDRESS): S.ADDRESS;
 VAR space: ARRAY 20000 OF BYTE; real: REAL;
 BEGIN
  real := 1.25; IF real < 1.0 THEN A.ExitProcess(78) END;
  space[0] := 12; space[19999] := 34;
  active := S.ADR(Six); A.RaiseException(0E0424242H, 0, 0, 0);
  P.Walk(S.ADR(Six));
  IF (space[0] # 12) OR (space[19999] # 34) THEN A.ExitProcess(75) END;
  RETURN a+b+c+d+e+f
 END Six;
 PROCEDURE [ms64] Handler (info: S.ADDRESS): INTEGER;
  VAR rec, ctx: S.ADDRESS; code: INTEGER;
 BEGIN
  S.GET(info, rec); S.GET(rec, code);
  IF code # 0E0424242H THEN RETURN 0 END;
  S.GET(info+8, ctx); P.FaultWalk(ctx, active); INC(traps); RETURN -1
 END Handler;
BEGIN
 veh := A.AddVectoredExceptionHandler(1, S.ADR(Handler));
 IF veh = 0 THEN A.ExitProcess(74) END;
 P.Probe(S.ADR(Six));
 result := A.RemoveVectoredExceptionHandler(veh);
 IF (result = 0) OR (traps # 1) THEN A.ExitProcess(76) END;
 cb := Callback;
 result := A.InitOnceExecuteOnce(S.ADR(once), cb, 100000004L, S.ADR(context));
 IF (result = 0) OR (context # 100000004L) OR (calls # 1) THEN A.ExitProcess(72) END;
 result := cb(S.ADR(once), 100000004L, S.ADR(context));
 IF (result # 1) OR (calls # 2) THEN A.ExitProcess(73) END;
 A.ExitProcess(0)
END CheckCallback64.
