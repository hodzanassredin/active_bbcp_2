MODULE Async_devBenchObj;
(** Замеры монитора в базовом классе (AsyncObject): пара «вход — выход»
	свободного объекта через Do, через [exclusive] и обычный метод;
	производитель — потребитель через буфер на Do+Wait, на [exclusive]+
	AWAIT и на AsyncChan.Buffer.

	Строка BENCH: ops, totalNs, nsPerOp, allocsPer1000, switchesPer1000.
	Замеры идут в задаче, главный стек крутит Tasks.Step. Для команды:
		Async_devBenchObj.DoFree
	и т. п.; медиана — по пяти прогонам снаружи. *)

	IMPORT ACTIVE, Kernel, Tasks, AsyncObject, AsyncSelect, AsyncChan,
		System_devClock, Console, Strings;

	CONST
		freeN = 1000000;	(* пар вход — выход *)
		bufN = 200000;		(* предметов через буфер *)
		cap = 64;

	TYPE
		(* --- свободный объект --- *)

		NoOp = RECORD (AsyncObject.Op) END;
		DoBox = POINTER TO RECORD (AsyncObject.Object) n: INTEGER END;

		ExBox = POINTER TO RECORD n: INTEGER END;

		DoTask = POINTER TO RECORD (Tasks.Task) b: DoBox; n: INTEGER END;
		ExTask = POINTER TO RECORD (Tasks.Task) b: ExBox; n: INTEGER END;
		PlainTask = POINTER TO RECORD (Tasks.Task) b: ExBox; n: INTEGER END;

		(* --- буфер на Do + Wait --- *)

		PutOp = RECORD (AsyncObject.Op) v: INTEGER END;
		GetOp = RECORD (AsyncObject.Op) v: INTEGER END;

		DoBuf = POINTER TO RECORD (AsyncObject.Object)
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER
		END;
		DoProd = POINTER TO RECORD (Tasks.Task) b: DoBuf; n: INTEGER END;
		DoCons = POINTER TO RECORD (Tasks.Task) b: DoBuf; n: INTEGER END;

		(* --- буфер на [exclusive] + AWAIT --- *)

		ExBuf = POINTER TO RECORD
			ring: ARRAY cap OF INTEGER; head, tail, count: INTEGER
		END;
		ExProd = POINTER TO RECORD (Tasks.Task) b: ExBuf; n: INTEGER END;
		ExCons = POINTER TO RECORD (Tasks.Task) b: ExBuf; n: INTEGER END;

		(* --- буфер AsyncChan --- *)

		M = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		ChProd = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;
		ChCons = POINTER TO RECORD (Tasks.Task) c: AsyncChan.Buffer; n: INTEGER END;

	VAR
		allocs0, switches0: LONGINT;
		sum: INTEGER;

	(* -------------------- вывод ------------------------------------------- *)

	PROCEDURE W (IN s: ARRAY OF CHAR);
	BEGIN Console.WriteStr(s)
	END W;

	PROCEDURE N (x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN Strings.IntToString(x, s); W(s)
	END N;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ops: INTEGER; ns: LONGINT);
	BEGIN
		W("BENCH "); W(name); W(" ops="); N(ops); W(" totalNs="); N(ns);
		W(" nsPerOp="); N(ns DIV ops);
		W(" allocsPer1000="); N((Kernel.AllocCount() - allocs0) * 1000 DIV ops);
		W(" switchesPer1000="); N((Tasks.Switches() - switches0) * 1000 DIV ops);
		Console.WriteLn
	END Report;

	PROCEDURE Now (): LONGINT;
	BEGIN
		allocs0 := Kernel.AllocCount(); switches0 := Tasks.Switches();
		RETURN System_devClock.now()
	END Now;

	PROCEDURE Go (t: Tasks.Task);
	BEGIN
		Tasks.SetStackSize(t, 64 * 1024); Tasks.Start(t)
	END Go;

	PROCEDURE Finish (IN name: ARRAY OF CHAR; ops: INTEGER; t0: LONGINT);
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Report(name, ops, System_devClock.now() - t0)
	END Finish;

	(* -------------------- свободный объект -------------------------------- *)

	PROCEDURE (b: DoBox) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: NoOp DO INC(b.n) END
	END Exclusive;

	PROCEDURE (t: DoTask) Run;
		VAR i: INTEGER; op: NoOp;
	BEGIN
		FOR i := 1 TO t.n DO t.b.Do(op) END;
		sum := t.n
	END Run;

	PROCEDURE [exclusive] (b: ExBox) Touch, NEW;
	BEGIN INC(b.n)
	END Touch;

	PROCEDURE (b: ExBox) TouchPlain, NEW;
	BEGIN INC(b.n)
	END TouchPlain;

	PROCEDURE (t: ExTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO t.b.Touch END;
		sum := t.n
	END Run;

	PROCEDURE (t: PlainTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO t.b.TouchPlain END;
		sum := t.n
	END Run;

	PROCEDURE DoFree*;
		VAR b: DoBox; t: DoTask; op: NoOp; t0: LONGINT; i: INTEGER;
	BEGIN
		NEW(b); NEW(t); t.b := b; t.n := freeN;
		FOR i := 1 TO 8 DO b.Do(op) END;	(* свободный список записей *)
		t0 := Now(); Go(t); Finish("do-free", freeN, t0)
	END DoFree;

	PROCEDURE ExFree*;
		VAR b: ExBox; t: ExTask; t0: LONGINT;
	BEGIN
		NEW(b); NEW(t); t.b := b; t.n := freeN;
		t0 := Now(); Go(t); Finish("exclusive-free", freeN, t0)
	END ExFree;

	PROCEDURE MethodFree*;
		VAR b: ExBox; t: PlainTask; t0: LONGINT;
	BEGIN
		NEW(b); NEW(t); t.b := b; t.n := freeN;
		t0 := Now(); Go(t); Finish("method-free", freeN, t0)
	END MethodFree;

	(* -------------------- буфер на Do + Wait ------------------------------- *)

	PROCEDURE (b: DoBuf) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO b.Wait END;
			b.ring[b.tail] := op.v;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO b.Wait END;
			op.v := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		END
	END Exclusive;

	PROCEDURE (t: DoProd) Run;
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO t.n DO op.v := i; t.b.Do(op) END
	END Run;

	PROCEDURE (t: DoCons) Run;
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO t.n DO t.b.Do(op); sum := op.v END
	END Run;

	PROCEDURE BufferDo*;
		VAR b: DoBuf; p: DoProd; c: DoCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		NEW(p); p.b := b; p.n := bufN; NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Go(p); Go(c); Finish("buffer-do", bufN, t0)
	END BufferDo;

	(* -------------------- буфер на [exclusive] + AWAIT --------------------- *)

	PROCEDURE [exclusive] PutEx (b: ExBuf; v: INTEGER);
	BEGIN
		ACTIVE.AWAIT(b.count < cap);
		b.ring[b.tail] := v;
		b.tail := (b.tail + 1) MOD cap; INC(b.count)
	END PutEx;

	PROCEDURE [exclusive] GetEx (b: ExBuf; VAR v: INTEGER);
	BEGIN
		ACTIVE.AWAIT(b.count > 0);
		v := b.ring[b.head];
		b.head := (b.head + 1) MOD cap; DEC(b.count)
	END GetEx;

	PROCEDURE (t: ExProd) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO PutEx(t.b, i) END
	END Run;

	PROCEDURE (t: ExCons) Run;
		VAR i, v: INTEGER;
	BEGIN
		FOR i := 1 TO t.n DO GetEx(t.b, v); sum := v END
	END Run;

	PROCEDURE BufferEx*;
		VAR b: ExBuf; p: ExProd; c: ExCons; t0: LONGINT;
	BEGIN
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		NEW(p); p.b := b; p.n := bufN; NEW(c); c.b := b; c.n := bufN;
		t0 := Now(); Go(p); Go(c); Finish("buffer-exclusive", bufN, t0)
	END BufferEx;

	(* -------------------- буфер AsyncChan --------------------------------- *)

	PROCEDURE (t: ChProd) Run;
		VAR i: INTEGER; m: M;
	BEGIN
		NEW(m);
		FOR i := 1 TO t.n DO m.n := i; t.c.Send(m) END
	END Run;

	PROCEDURE (t: ChCons) Run;
		VAR i: INTEGER; x: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO t.n DO t.c.Recv(x); sum := x(M).n END
	END Run;

	PROCEDURE BufferChan*;
		VAR c: AsyncChan.Buffer; p: ChProd; q: ChCons; t0: LONGINT;
	BEGIN
		c := AsyncChan.NewBuffer(cap);
		NEW(p); p.c := c; p.n := bufN; NEW(q); q.c := c; q.n := bufN;
		t0 := Now(); Go(p); Go(q); Finish("buffer-chan", bufN, t0)
	END BufferChan;

END Async_devBenchObj.
