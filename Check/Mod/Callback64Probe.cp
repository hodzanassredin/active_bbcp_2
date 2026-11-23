MODULE CheckCallback64Probe ["callback64_probe.dll"];
 IMPORT S := SYSTEM;
 PROCEDURE [ms64] Probe* (fn: S.ADDRESS);
 PROCEDURE [ms64] FaultWalk* (context, fn: S.ADDRESS);
 PROCEDURE [ms64] Walk* (fn: S.ADDRESS);
END CheckCallback64Probe.
