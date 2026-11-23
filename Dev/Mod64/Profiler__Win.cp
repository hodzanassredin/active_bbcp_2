MODULE DevProfiler__Win;
(**
	project	= "BlackBox 2.0"
	organizations	= "Oberon microsystems, BBWFC, OberonCore, BBCP Team"
	contributors	= "Contributors, Ilya E. Ermakov, Ivan Denisov"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT
		S := SYSTEM,
		A := WinApi, WinMM, DevProfiler;

	TYPE
		ProfilerHook = POINTER TO RECORD (DevProfiler.ProfilerHook) END;

	VAR
		id, period: INTEGER;
		hook: ProfilerHook;
		samples: LONGINT;
		main: A.HANDLE;

	PROCEDURE [ms64] Recorder (timer, message: INTEGER; user, dw1, dw2: S.ADDRESS);
		VAR
			storage: ARRAY SIZE(A.CONTEXT)+15 OF BYTE; 
			context: A.PtrCONTEXT;
			ignored: INTEGER;
	BEGIN
		context := S.VAL(A.PtrCONTEXT, (S.ADR(storage)+15) DIV 16*16);
		context.ContextFlags := A.CONTEXT_CONTROL;
		IF A.SuspendThread(user) # -1 THEN
			IF A.GetThreadContext(user, context^) # 0 THEN
				hook.Count(context.Rip);
				INC(samples)
			END;
			ignored := A.ResumeThread(user)
		END
	END Recorder;

	PROCEDURE (p: ProfilerHook) StartRecording;
		VAR result, ignored: INTEGER; caps: WinMM.TIMECAPS;
	BEGIN
		ASSERT(id = 0, 20); samples := 0;
		result := A.DuplicateHandle(A.GetCurrentProcess(), A.GetCurrentThread(),
			A.GetCurrentProcess(), main, A.THREAD_SUSPEND_RESUME+A.THREAD_GET_CONTEXT, 0, {});
		ASSERT(result # 0, 100);
		result := WinMM.timeGetDevCaps(caps, SIZE(WinMM.TIMECAPS));
		IF result # 0 THEN ignored := A.CloseHandle(main); main := 0 END;
		ASSERT(result = 0, 101);
		period := caps.wPeriodMin*3;
		result := WinMM.timeBeginPeriod(period);
		IF result # 0 THEN ignored := A.CloseHandle(main); main := 0 END;
		ASSERT(result = 0, 102);
		id := WinMM.timeSetEvent(period, 0, Recorder, main, WinMM.TIME_PERIODIC+WinMM.TIME_KILL_SYNCHRONOUS);
		IF id = 0 THEN
			ignored := WinMM.timeEndPeriod(period); ignored := A.CloseHandle(main); main := 0
		END;
		ASSERT(id # 0, 103)
	END StartRecording;

	PROCEDURE (p: ProfilerHook) StopRecording;
		(* stop calling Count *)
		VAR res: INTEGER;
	BEGIN
		IF id # 0 THEN
			res := WinMM.timeKillEvent(id);
			ASSERT(res = 0, 104);
			id := 0;
			res := WinMM.timeEndPeriod(period);
			res := A.CloseHandle(main);
			main := 0
		END
	END StopRecording;

	PROCEDURE GetSampleCount* (): LONGINT;
	BEGIN RETURN samples END GetSampleCount;

BEGIN
	NEW(hook); DevProfiler.SetProfilerHook(hook)
CLOSE
	hook.StopRecording
END DevProfiler__Win.
