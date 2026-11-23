MODULE CheckCallback64Api ["kernel32.dll"];
 IMPORT S := SYSTEM;
 TYPE Callback* = PROCEDURE [ms64] (once, param, context: S.ADDRESS): INTEGER;
 PROCEDURE [ms64] InitOnceExecuteOnce* (once: S.ADDRESS; cb: Callback; param, context: S.ADDRESS): INTEGER;
 PROCEDURE [ms64] AddVectoredExceptionHandler* (first: INTEGER; handler: S.ADDRESS): S.ADDRESS;
 PROCEDURE [ms64] RemoveVectoredExceptionHandler* (handler: S.ADDRESS): INTEGER;
 PROCEDURE [ms64] RaiseException* (code, flags, count: INTEGER; args: S.ADDRESS);
 PROCEDURE [ms64] ExitProcess* (code: INTEGER);
END CheckCallback64Api.
