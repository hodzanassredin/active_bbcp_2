MODULE System_devPerf;
(** Замеры для отчёта по ревью (не входят в tools/test.sh).

	Sleepers*  — 10 000 спящих задач + 1 активная, 100 000 переключений:
	             стоимость линейного WakeSleepers на каждой итерации Step.

	Запуск: echo 'System_devPerf.Sleepers' | tools/run64.sh.
	Наносекунды — System_devClock (на amd64 clock_gettime). *)

	IMPORT Kernel, Tasks, System_devClock, Console, Strings;

	CONST
		sleepers = 10000;
		switches = 100000;
		stackSize = 64 * 1024;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Task) END;
		Spinner = POINTER TO RECORD (Tasks.Task) done: BOOLEAN END;

	VAR spinner: Spinner; started: INTEGER;

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE (t: Sleeper) Run;
	BEGIN
		INC(started);
		Tasks.Sleep(600000)	(* 10 минут: за время замера не проснётся *)
	END Run;

	PROCEDURE (t: Spinner) Run;
	BEGIN
		WHILE ~t.done DO Tasks.Yield END
	END Run;

	PROCEDURE Sleepers*;
		VAR i: INTEGER; t: Sleeper; t0, t1, ns, sw0, sw: LONGINT;
	BEGIN
		FOR i := 1 TO sleepers DO
			NEW(t); Tasks.SetStackSize(t, stackSize); Tasks.Start(t)
		END;
		NEW(spinner); Tasks.SetStackSize(spinner, stackSize);
		Tasks.Start(spinner);
		started := 0;
		WHILE started < sleepers DO Tasks.Step END;	(* все ушли в Sleep *)

		Tasks.ResetStats();
		t0 := System_devClock.now();
		sw0 := Tasks.Switches();
		REPEAT Tasks.Step; sw := Tasks.Switches() - sw0 UNTIL sw >= switches;
		t1 := System_devClock.now();
		ns := t1 - t0;
		W("BENCH sleepers n="); N(sleepers);
		W(" switches="); N(sw);
		W(" ns="); N(ns);
		W(" nsPerSwitch="); N(ns DIV sw);
		Console.WriteLn;
		spinner.done := TRUE
	END Sleepers;

END System_devPerf.
