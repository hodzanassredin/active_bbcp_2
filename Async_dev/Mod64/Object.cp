MODULE Async_devObject;
(** Тесты монитора в базовом классе (AsyncObject).

	Проверяется: взаимное исключение через ожидание в другом объекте;
	порядок и сохранность данных буфера при N производителях и M
	потребителях; трап внутри Exclusive отпускает монитор; Wait вне
	Exclusive трапует; повторный вход разрешён и считает глубину;
	Tasks.Cancel ждущего убирает его запись из очереди. *)

	IMPORT AsyncObject, Kernel, Tasks, UnitAssert;

	CONST
		producers = 3;
		consumers = 2;
		per = 20;
		cap = 3;

	TYPE
		(* --- общий объект для проверок входа, ожидания и трапа --- *)

		Cell = POINTER TO RECORD (AsyncObject.Object)
			flag, inside: BOOLEAN;
			entries: INTEGER
		END;

		SetOp = RECORD (AsyncObject.Op) v: BOOLEAN END;
		WaitOp = RECORD (AsyncObject.Op) END;
		BumpOp = RECORD (AsyncObject.Op) END;
		CrashOp = RECORD (AsyncObject.Op) END;
		OuterOp = RECORD (AsyncObject.Op) END;
		InnerOp = RECORD (AsyncObject.Op) END;
		HoldOp = RECORD (AsyncObject.Op) y: Cell END;

		(* --- ограниченный буфер --- *)

		PutOp = RECORD (AsyncObject.Op) id, seq: INTEGER END;
		GetOp = RECORD (AsyncObject.Op) item: INTEGER END;

		Buf = POINTER TO RECORD (AsyncObject.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		(* --- задачи --- *)

		Waiter = POINTER TO RECORD (Tasks.Task) c: Cell; done: BOOLEAN END;
		Bumper = POINTER TO RECORD (Tasks.Task) c: Cell; done: BOOLEAN END;
		Holder = POINTER TO RECORD (Tasks.Task) x, y: Cell; done: BOOLEAN END;

		Prod = POINTER TO RECORD (Tasks.Task) b: Buf; id, n: INTEGER END;
		Cons = POINTER TO RECORD (Tasks.Task) b: Buf; n: INTEGER END;

	VAR
		(* Cell *)
		waits: INTEGER;
		cellInside, innerSeen: BOOLEAN;
		d1, d2, d3: INTEGER;
		(* буфер *)
		bufBroken: BOOLEAN;
		bufCount: INTEGER;
		nextSeq: ARRAY producers OF INTEGER;
		got: ARRAY producers OF INTEGER;
		(* трап *)
		trapCell: Cell;

	PROCEDURE Setup*;
		VAR i: INTEGER;
	BEGIN
		waits := 0; cellInside := FALSE; innerSeen := FALSE; d1 := 0; d2 := 0; d3 := 0;
		bufBroken := FALSE; bufCount := 0; trapCell := NIL;
		FOR i := 0 TO producers - 1 DO nextSeq[i] := 0; got[i] := 0 END
	END Setup;

	(* -------------------- вспомогательное -------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO Tasks.Step END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE NilCrash;
		VAR p: Cell;
	BEGIN
		p := NIL; p.entries := 1
	END NilCrash;

	(* -------------------- Cell -------------------------------------------- *)

	PROCEDURE (c: Cell) Exclusive- (VAR op: AsyncObject.Op);
		VAR inner: InnerOp; w: WaitOp;
	BEGIN
		WITH op: SetOp DO
			c.flag := op.v
		| op: WaitOp DO
			INC(waits);
			WHILE ~c.flag DO c.Wait END
		| op: BumpOp DO
			INC(c.entries)
		| op: CrashOp DO
			NilCrash
		| op: OuterOp DO
			d1 := AsyncObject.Depth(c);
			c.inside := TRUE;
			c.Do(inner);
			d3 := AsyncObject.Depth(c);
			c.inside := FALSE
		| op: InnerOp DO
			d2 := AsyncObject.Depth(c);
			innerSeen := c.inside
		| op: HoldOp DO
			cellInside := TRUE;
			op.y.Do(w);	(* X взят, Y ждёт *)
			cellInside := FALSE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (w: Waiter) Run;
		VAR op: WaitOp;
	BEGIN
		w.c.Do(op);
		w.done := TRUE
	END Run;

	PROCEDURE (b: Bumper) Run;
		VAR op: BumpOp;
	BEGIN
		b.c.Do(op);
		b.done := TRUE
	END Run;

	PROCEDURE (h: Holder) Run;
		VAR op: HoldOp;
	BEGIN
		op.y := h.y;
		h.x.Do(op);
		h.done := TRUE
	END Run;

	PROCEDURE SetFlag (c: Cell; v: BOOLEAN);
		VAR op: SetOp;
	BEGIN
		op.v := v; c.Do(op)
	END SetFlag;

	PROCEDURE WaitOutside;
		VAR c: Cell;
	BEGIN
		NEW(c); c.Wait
	END WaitOutside;

	PROCEDURE TrapInside;
		VAR op: CrashOp;
	BEGIN
		trapCell.Do(op)
	END TrapInside;

	(* -------------------- тесты ------------------------------------------- *)

	PROCEDURE TestMutualExclusionThroughWait*;
	(* A держит X и ждёт в Y; второй Do к X не входит. *)
		VAR x, y: Cell; a: Holder; b: Bumper;
	BEGIN
		NEW(x); NEW(y); NEW(a); a.x := x; a.y := y; NEW(b); b.c := x;
		Tasks.Start(a);
		Tasks.Step;
		UnitAssert.True(waits > 0, "A ждёт в Y");
		UnitAssert.True(cellInside, "A внутри Exclusive X");
		Tasks.Start(b);
		Tasks.Step;
		UnitAssert.EqualInt(0, x.entries, "второй Do не вошёл в X");
		UnitAssert.True(cellInside, "A всё ещё внутри X");
		SetFlag(y, TRUE);	(* выход Do из Y будит A *)
		UnitAssert.True(Pump(1000), "задачи завершились");
		UnitAssert.EqualInt(1, x.entries, "второй Do вошёл после выхода A");
		UnitAssert.True(a.done & b.done, "обе задачи дошли до конца")
	END TestMutualExclusionThroughWait;

	PROCEDURE TestWaitOutsideExclusiveTraps*;
	BEGIN
		UnitAssert.Traps(WaitOutside, "Wait без Exclusive трапует")
	END TestWaitOutsideExclusiveTraps;

	PROCEDURE TestTrapReleasesMonitor*;
		VAR c: Cell; op: BumpOp;
	BEGIN
		NEW(c); trapCell := c;
		UnitAssert.Traps(TrapInside, "трап внутри Exclusive");
		UnitAssert.True(~AsyncObject.Held(c), "после трапа монитор свободен");
		UnitAssert.EqualInt(0, AsyncObject.Queued(c), "очереди пусты");
		c.Do(op);
		UnitAssert.EqualInt(1, c.entries, "следующий Do входит")
	END TestTrapReleasesMonitor;

	PROCEDURE TestReentrantDo*;
		VAR c: Cell; op: OuterOp;
	BEGIN
		NEW(c);
		c.Do(op);
		UnitAssert.EqualInt(1, d1, "внешний вход: глубина 1");
		UnitAssert.EqualInt(2, d2, "повторный вход: глубина 2");
		UnitAssert.True(innerSeen, "повторный вход видит внешний");
		UnitAssert.EqualInt(1, d3, "после внутреннего выхода: глубина 1");
		UnitAssert.True(~AsyncObject.Held(c), "оба выхода отпустили монитор")
	END TestReentrantDo;

	PROCEDURE TestDoDoesNotAllocate*;
	(* После прогрева пара вход — выход берёт запись из свободного списка. *)
		VAR c: Cell; op: BumpOp; i: INTEGER; a0, a1: LONGINT;
	BEGIN
		NEW(c);
		c.Do(op);	(* прогрев: запись ожидания попала в свободный список *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO 1000 DO c.Do(op) END;
		a1 := Kernel.AllocCount();
		UnitAssert.EqualInt(0, a1 - a0, "вход-выход без выделений")
	END TestDoDoesNotAllocate;

	PROCEDURE TestCancelWaiting*;
		VAR c: Cell; w: Waiter; op: BumpOp;
	BEGIN
		NEW(c); NEW(w); w.c := c;
		Tasks.Start(w);
		Tasks.Step;
		UnitAssert.True(waits > 0, "задача ждёт условия");
		UnitAssert.EqualInt(1, AsyncObject.Queued(c), "запись в очереди ожидания");
		Tasks.Cancel(w);
		IF Pump(1000) THEN END;
		UnitAssert.True(Tasks.State(w) IN {Tasks.Finished, Tasks.Cancelled}, "задача отменена");
		UnitAssert.True(~AsyncObject.Held(c), "монитор свободен");
		UnitAssert.EqualInt(0, AsyncObject.Queued(c), "очередь очищена");
		c.Do(op);
		UnitAssert.EqualInt(1, c.entries, "следующий Do входит")
	END TestCancelWaiting;

	(* -------------------- буфер ------------------------------------------- *)

	PROCEDURE (b: Buf) Exclusive- (VAR op: AsyncObject.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE b.count = cap DO b.Wait END;
			b.ring[b.tail] := op.id * 100 + op.seq;
			b.tail := (b.tail + 1) MOD cap; INC(b.count)
		| op: GetOp DO
			WHILE b.count = 0 DO b.Wait END;
			op.item := b.ring[b.head];
			b.head := (b.head + 1) MOD cap; DEC(b.count)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: Prod) Run;
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO p.n DO
			op.id := p.id; op.seq := i;
			p.b.Do(op)
		END
	END Run;

	PROCEDURE (c: Cons) Run;
		VAR i, id, seq: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO c.n DO
			op.item := -1;
			c.b.Do(op);
			IF op.item < 0 THEN bufBroken := TRUE
			ELSE
				id := op.item DIV 100; seq := op.item MOD 100;
				IF (id < 0) OR (id >= producers) OR (seq # nextSeq[id] + 1) THEN bufBroken := TRUE END;
				INC(nextSeq[id]); INC(got[id])
			END;
			INC(bufCount)
		END
	END Run;

	PROCEDURE TestBufferOrder*;
		VAR b: Buf; p: Prod; c: Cons; i: INTEGER; total: INTEGER;
	BEGIN
		total := producers * per;
		NEW(b); b.head := 0; b.tail := 0; b.count := 0;
		FOR i := 0 TO consumers - 1 DO
			NEW(c); c.b := b; c.n := total DIV consumers; Tasks.Start(c)
		END;
		FOR i := 0 TO producers - 1 DO
			NEW(p); p.b := b; p.id := i; p.n := per; Tasks.Start(p)
		END;
		UnitAssert.True(Pump(2000), "производители и потребители завершились");
		UnitAssert.EqualInt(total, bufCount, "принято предметов");
		UnitAssert.True(~bufBroken, "порядок каждого производителя сохранён");
		FOR i := 0 TO producers - 1 DO UnitAssert.EqualInt(per, got[i], "все предметы производителя") END;
		UnitAssert.EqualInt(0, b.count, "буфер пуст");
		UnitAssert.True(~AsyncObject.Held(b), "монитор свободен")
	END TestBufferOrder;

END Async_devObject.
