MODULE Async_devBench;
(** Парные замеры задач, мониторов и каналов; ориентир — tools/bench-go.sh pair
	(Go, GOMAXPROCS=1). Каждый замер печатает строку
	BENCH <имя> ops=… totalNs=… nsPerOp=…; имена совпадают с Go без
	приставки go-.

	Команды без параметров (PingPong, Buffer64, …) печатают одну строку и
	годятся для tools/prof.sh; тесты TestSlow… зовут их и проверяют
	только итог обмена, а не время. Главный стек крутит Tasks.Step. *)

	IMPORT ACTIVE, Kernel, Tasks, AsyncSelect, AsyncChan, UnitAssert, System_devClock, Console, Strings;

	CONST
		million = 1000000;
		spawnN = 100000;
		stack = 64 * 1024;

	TYPE
		M = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Pinger = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;
		Ponger = POINTER TO RECORD (Tasks.Task) a, b: AsyncChan.Chan; n: INTEGER END;

		BProd = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		BCons = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;

		Box = POINTER TO RECORD n: INTEGER END;
		Locker = POINTER TO RECORD (Tasks.Task) b: Box; n: INTEGER END;

		SSend = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Chan; n: INTEGER END;
		SRecv = POINTER TO RECORD (Tasks.Task) cs: ARRAY 8 OF AsyncChan.Chan; k, n: INTEGER END;

		Short = POINTER TO RECORD (Tasks.Task) END;
		Yielder = POINTER TO RECORD (Tasks.Task) n: INTEGER END;

	VAR
		sum, got: INTEGER;
		allocs0: LONGINT;	(* Kernel.AllocCount в начале замера *)

	(* --- вывод ------------------------------------------------------------ *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN
		Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ops: INTEGER; ns: LONGINT);
	BEGIN
		W("BENCH "); W(name); W(" ops="); N(ops); W(" totalNs="); N(ns);
		W(" nsPerOp="); N(ns DIV ops);
		W(" allocsPer1000="); N((Kernel.AllocCount() - allocs0) * 1000 DIV ops); Console.WriteLn
	END Report;

	PROCEDURE Run (IN name: ARRAY OF CHAR; ops: INTEGER; t0: LONGINT);
	(* Пред: задачи замера запущены, t0 — время до их запуска.
	   Пост: все задачи завершены, строка BENCH напечатана. *)
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Report(name, ops, System_devClock.now() - t0)
	END Run;

	PROCEDURE Now (): LONGINT;
	(* Начало замера: время и счётчик выделений. *)
	BEGIN
		allocs0 := Kernel.AllocCount();
		RETURN System_devClock.now()
	END Now;

	PROCEDURE Go (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, stack); Tasks.Start(t)
	END Go;

	(* --- пинг-понг через встречу ------------------------------------------ *)

	PROCEDURE (t: Pinger) Run;
		VAR i: INTEGER; m: M; x: AsyncSelect.Msg;
	BEGIN
		NEW(m); x := m;
		FOR i := 1 TO t.n DO t.a.Send(x); t.b.Recv(x) END;
		sum := x(M).n
	END Run;

	PROCEDURE (t: Ponger) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.a.Recv(x); INC(x(M).n); t.b.Send(x) END
	END Run;

	PROCEDURE PingPongN (n: INTEGER);
		VAR p: Pinger; q: Ponger; t0: LONGINT;
	BEGIN
		sum := 0;
		NEW(p); NEW(q);
		p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan(); p.n := n;
		q.a := p.a; q.b := p.b; q.n := n;
		t0 := Now();
		Go(p); Go(q);
		Run("pingpong", n, t0)
	END PingPongN;

	PROCEDURE PingPong*;
	BEGIN
		PingPongN(million)
	END PingPong;

	(* --- буфер на 64 ------------------------------------------------------ *)

	PROCEDURE (t: BProd) Run;
		VAR i: INTEGER; m: M;
	BEGIN
		FOR i := 1 TO t.n DO NEW(m); m.n := i; t.c.Send(m) END
	END Run;

	PROCEDURE (t: BCons) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); INC(sum, x(M).n MOD 2) END
	END Run;

	PROCEDURE Buffer64N (n: INTEGER);
		VAR p: BProd; q: BCons; t0: LONGINT;
	BEGIN
		sum := 0;
		NEW(p); NEW(q); p.c := AsyncChan.NewBuffer(64); p.n := n; q.c := p.c; q.n := n;
		t0 := Now();
		Go(p); Go(q);
		Run("buffer64", n, t0)
	END Buffer64N;

	PROCEDURE Buffer64*;
	BEGIN
		Buffer64N(million)
	END Buffer64;

	(* --- свободный монитор ------------------------------------------------ *)

	PROCEDURE [exclusive] (b: Box) Touch, NEW;
	(* Тело — одно присваивание: у пустого тела компилятор не берёт монитор. *)
	BEGIN
		INC(b.n)
	END Touch;

	PROCEDURE (t: Locker) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO t.b.Touch END;
		sum := t.n
	END Run;

	PROCEDURE MonitorN (n: INTEGER);
		VAR t: Locker; t0: LONGINT;
	BEGIN
		sum := 0;
		NEW(t); NEW(t.b); t.n := n;
		t0 := Now();
		Go(t);
		Run("monitor", n, t0)
	END MonitorN;

	PROCEDURE Monitor*;
	BEGIN
		MonitorN(million)
	END Monitor;

	(* --- выбор из k каналов ----------------------------------------------- *)

	PROCEDURE (t: SSend) Run;
		VAR i: INTEGER; m: M;
	BEGIN
		NEW(m); m.n := 1;
		FOR i := 1 TO t.n DO t.c.Send(m) END
	END Run;

	PROCEDURE (t: SRecv) Run;
		VAR i, j, w: INTEGER; ops2: ARRAY 2 OF AsyncSelect.Op; ops8: ARRAY 8 OF AsyncSelect.Op;
	BEGIN
		FOR i := 1 TO t.n DO
			IF t.k = 2 THEN
				FOR j := 0 TO 1 DO AsyncChan.RecvAlt(t.cs[j], ops2[j]) END;
				w := AsyncChan.Select(ops2); INC(got, ops2[w].got(M).n)
			ELSE
				FOR j := 0 TO 7 DO AsyncChan.RecvAlt(t.cs[j], ops8[j]) END;
				w := AsyncChan.Select(ops8); INC(got, ops8[w].got(M).n)
			END
		END
	END Run;

	PROCEDURE SelectN (k, n: INTEGER; IN name: ARRAY OF CHAR);
	(* Пред: k = 2 или k = 8, n кратно k. *)
		VAR r: SRecv; s: SSend; j: INTEGER; t0: LONGINT;
	BEGIN
		ASSERT((k = 2) OR (k = 8), 20); ASSERT(n MOD k = 0, 21);
		got := 0;
		NEW(r); r.k := k; r.n := n;
		t0 := Now();
		FOR j := 0 TO k - 1 DO
			r.cs[j] := AsyncChan.NewChan();
			NEW(s); s.c := r.cs[j]; s.n := n DIV k; Go(s)
		END;
		Go(r);
		Run(name, n, t0)
	END SelectN;

	PROCEDURE Select2*;
	BEGIN
		SelectN(2, million, "select2")
	END Select2;

	PROCEDURE Select8*;
	BEGIN
		SelectN(8, million, "select8")
	END Select8;

	(* --- создание коротких задач ------------------------------------------ *)

	PROCEDURE (t: Short) Run;
	BEGIN
		INC(sum)
	END Run;

	PROCEDURE SpawnN (n: INTEGER; IN name: ARRAY OF CHAR);
		VAR i: INTEGER; t: Short; t0: LONGINT;
	BEGIN
		sum := 0;
		t0 := Now();
		FOR i := 1 TO n DO NEW(t); Go(t) END;
		Run(name, n, t0)
	END SpawnN;

	PROCEDURE Spawn*;
	(** Первый прогон берёт стеки у ядра, второй — из пула. *)
	BEGIN
		SpawnN(spawnN, "spawn");
		SpawnN(spawnN, "spawn-warm")
	END Spawn;

	(* --- чистое переключение ---------------------------------------------- *)

	PROCEDURE (t: Yielder) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO Tasks.Yield END;
		INC(sum, t.n)
	END Run;

	PROCEDURE YieldN (n: INTEGER);
		VAR a, b: Yielder; t0: LONGINT;
	BEGIN
		sum := 0;
		NEW(a); NEW(b); a.n := n DIV 2; b.n := n DIV 2;
		t0 := Now();
		Go(a); Go(b);
		Run("yield", n, t0)
	END YieldN;

	PROCEDURE Yield*;
	BEGIN
		YieldN(million)
	END Yield;

	PROCEDURE All*;
	BEGIN
		PingPong; Buffer64; Monitor; Select2; Select8; Spawn; Yield
	END All;

	(* --- тесты: итог обмена, время только печатается ---------------------- *)

	PROCEDURE TestSlowPingPong*;
	BEGIN
		PingPong; UnitAssert.EqualInt(million, sum, "every exchange incremented")
	END TestSlowPingPong;

	PROCEDURE TestSlowBuffer64*;
	BEGIN
		Buffer64; UnitAssert.EqualInt(million DIV 2, sum, "every message received")
	END TestSlowBuffer64;

	PROCEDURE TestSlowMonitor*;
	BEGIN
		Monitor; UnitAssert.EqualInt(million, sum, "loop finished")
	END TestSlowMonitor;

	PROCEDURE TestSlowSelect*;
	BEGIN
		Select2; UnitAssert.EqualInt(million, got, "select2 received all");
		Select8; UnitAssert.EqualInt(million, got, "select8 received all")
	END TestSlowSelect;

	PROCEDURE TestSlowSpawn*;
	BEGIN
		Spawn; UnitAssert.EqualInt(spawnN, sum, "every task ran")
	END TestSlowSpawn;

	PROCEDURE TestSlowYield*;
	BEGIN
		Yield; UnitAssert.EqualInt(million, sum, "every yield returned")
	END TestSlowYield;

END Async_devBench.
