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
		WinApi, WinMM, DevProfiler;

	TYPE
		ProfilerHook = POINTER TO RECORD (DevProfiler.ProfilerHook) END;

	VAR
		id, periode: INTEGER;
		hook: ProfilerHook;

	PROCEDURE Recorder (id_, msg_, user, dw1_, dw2_: INTEGER);
		VAR res_: INTEGER; context: WinApi.CONTEXT;
	BEGIN
		context.ContextFlags := WinApi.CONTEXT_CONTROL;
		res_ := WinApi.GetThreadContext(user, context);
		hook.Count(context.Eip)	(* current pc of main thread *)
	END Recorder;
	
	PROCEDURE (p: ProfilerHook) StartRecording;
		(* Start calling Count periodically by an interrupt handler *)
		VAR res_: INTEGER; main: WinApi.HANDLE; tc: WinMM.TIMECAPS;
	BEGIN
		res_ := WinApi.DuplicateHandle(WinApi.GetCurrentProcess(), WinApi.GetCurrentThread(),
					WinApi.GetCurrentProcess(), main, {1, 3, 4, 16..19}, 0, {});
		res_ := WinMM.timeGetDevCaps(tc, SIZE(WinMM.TIMECAPS));
		periode := tc.wPeriodMin * 3;
		res_ := WinMM.timeBeginPeriod(periode);
		id := WinMM.timeSetEvent(periode, 0, Recorder, main, WinMM.TIME_PERIODIC);
		ASSERT(id # 0, 100)
	END StartRecording;
	
	PROCEDURE (p: ProfilerHook) StopRecording;
		(* stop calling Count *)
		VAR res_: INTEGER;
	BEGIN
		res_ := WinMM.timeKillEvent(id);
		res_ := WinMM.timeEndPeriod(periode)
	END StopRecording;

BEGIN
	NEW(hook);
	DevProfiler.SetProfilerHook(hook)

END DevProfiler__Win.
