MODULE Async_devStm;
(** Тесты транзакционной памяти AsyncStm.

	Сценарии с ожиданием идут в задачах Tasks; главный стек крутит
	планировщик через Pump. Задача Runner исполняет транзакцию под своим
	Kernel.Try и запоминает номер трапа: так тест видит, какой трап дошёл
	до вызывающего Atomically. *)

	IMPORT S := SYSTEM, Kernel, Services, Tasks, AsyncStm, AsyncChan, AsyncSelect,
		UnitAssert, Console, Strings;

	CONST
		accounts = 4;
		workers = 10;
		perWorker = 100;
		benchN = 1000000;

	TYPE
		(* перевод n со счёта from на счёт to; fail — трап 42 между снятием и зачислением *)
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER; fail: BOOLEAN
		END;
		(* два перевода одной транзакцией *)
		Both = POINTER TO RECORD (AsyncStm.Tx) first, second: Transfer END;
		(* снять n с from, когда хватает денег и счёт open открыт (open = NIL — всегда) *)
		Withdraw = POINTER TO RECORD (AsyncStm.Tx)
			from: AsyncStm.IntVar; open: AsyncStm.BoolVar; mark: AsyncStm.IntVar; n: INTEGER
		END;
		(* снять с a, иначе с b *)
		Either = POINTER TO RECORD (AsyncStm.Tx) a, b: Withdraw END;
		(* записать x в v, затем сделать то, что велит what *)
		Violate = POINTER TO RECORD (AsyncStm.Tx)
			v: AsyncStm.IntVar; what: INTEGER; c: AsyncChan.Chan
		END;
		(* прочитать свою запись *)
		ReadOwn = POINTER TO RECORD (AsyncStm.Tx) v: AsyncStm.IntVar; seen: INTEGER END;
		(* Retry без чтений *)
		BareRetry = POINTER TO RECORD (AsyncStm.Tx) END;
		(* своя переменная: точка *)
		PointVar = POINTER TO RECORD (AsyncStm.Var) x, y: INTEGER END;
		Point = POINTER TO RECORD x, y: INTEGER END;
		MovePoint = POINTER TO RECORD (AsyncStm.Tx) p: PointVar; fail: BOOLEAN END;
		WaitPoint = POINTER TO RECORD (AsyncStm.Tx) p: PointVar; x: INTEGER END;

		Runner = POINTER TO RECORD (Tasks.Task)
			tx: AsyncStm.Tx; delay: INTEGER; err: INTEGER; done: BOOLEAN
		END;
		Worker = POINTER TO RECORD (Tasks.Task) id: INTEGER END;
		Setter = POINTER TO RECORD (Tasks.Task) v: AsyncStm.IntVar; x, delay: INTEGER END;

		(* Замер свободного вызова монитора-объекта Tasks.Object. *)
		Counter = POINTER TO RECORD (Tasks.Object) n: INTEGER END;
		MoveOp = RECORD (Tasks.Op) to: Counter; k: INTEGER END;
		MoveJob = POINTER TO RECORD (Tasks.Job) x, y: Counter END;

	VAR
		acc: ARRAY accounts OF AsyncStm.IntVar;
		transfers: INTEGER;

	(* --- инструменты ------------------------------------------------------- *)

	PROCEDURE Pump (ms: LONGINT): BOOLEAN;
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE (Tasks.Count() > 0) & (Kernel.Time() - t0 <= ms) DO
			Services.actionHook.Step
		END;
		RETURN Tasks.Count() = 0
	END Pump;

	PROCEDURE PumpFor (ms: LONGINT);
		VAR t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		WHILE Kernel.Time() - t0 <= ms DO Services.actionHook.Step END
	END PumpFor;

	PROCEDURE AtomBody (a, b, c: S.ADDRESS);
		VAR t: AsyncStm.Tx;
	BEGIN
		t := S.VAL(AsyncStm.Tx, b);
		AsyncStm.Atomically(t);
		S.PUT(a, TRUE)
	END AtomBody;

	PROCEDURE TryAtomically (t: AsyncStm.Tx; OUT err: INTEGER): BOOLEAN;
	(* Пост: TRUE — транзакция завершилась; FALSE — трап с номером err. *)
		VAR ok: BOOLEAN;
	BEGIN
		ok := FALSE; err := 0;
		Kernel.Try(S.VAL(Kernel.TryHandler, S.ADR(AtomBody)), S.ADR(ok), S.VAL(S.ADDRESS, t), 0);
		IF ~ok THEN err := Kernel.err END;
		RETURN ok
	END TryAtomically;

	PROCEDURE (r: Runner) Run;
	BEGIN
		IF r.delay > 0 THEN Tasks.Sleep(r.delay) END;
		r.done := TryAtomically(r.tx, r.err)
	END Run;

	PROCEDURE Start (tx: AsyncStm.Tx; delay: INTEGER): Runner;
		VAR r: Runner;
	BEGIN
		NEW(r); r.tx := tx; r.delay := delay; r.err := -1; r.done := FALSE;
		Tasks.Start(r);
		RETURN r
	END Start;

	PROCEDURE (s: Setter) Run;
	BEGIN
		Tasks.Sleep(s.delay); s.v.Set(s.x)
	END Run;

	PROCEDURE SetLater (v: AsyncStm.IntVar; x, delay: INTEGER);
		VAR s: Setter;
	BEGIN
		NEW(s); s.v := v; s.x := x; s.delay := delay; Tasks.Start(s)
	END SetLater;

	(* --- транзакции -------------------------------------------------------- *)

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);
		IF t.fail THEN HALT(42) END;
		t.to.Set(t.to.Get() + t.n)
	END Do;

	PROCEDURE NewTransfer (from, to: AsyncStm.IntVar; n: INTEGER; fail: BOOLEAN): Transfer;
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := from; t.to := to; t.n := n; t.fail := fail;
		RETURN t
	END NewTransfer;

	PROCEDURE (t: Both) Do;
	BEGIN
		AsyncStm.Atomically(t.first); AsyncStm.Atomically(t.second)
	END Do;

	PROCEDURE (t: Withdraw) Do;
	BEGIN
		IF t.mark # NIL THEN t.mark.Set(t.mark.Get() + 1) END;
		IF (t.from.Get() < t.n) OR (t.open # NIL) & ~t.open.Get() THEN AsyncStm.Retry END;
		t.from.Set(t.from.Get() - t.n)
	END Do;

	PROCEDURE NewWithdraw (from: AsyncStm.IntVar; n: INTEGER): Withdraw;
		VAR t: Withdraw;
	BEGIN
		NEW(t); t.from := from; t.n := n; t.open := NIL; t.mark := NIL;
		RETURN t
	END NewWithdraw;

	PROCEDURE (t: Either) Do;
	BEGIN
		AsyncStm.OrElse(t.a, t.b)
	END Do;

	PROCEDURE (t: Violate) Do;
		VAR m: AsyncSelect.Msg;
	BEGIN
		t.v.Set(t.v.Get() + 1);
		CASE t.what OF
			0: Tasks.Yield
		| 1: Tasks.Sleep(1)
		| 2: t.c.Recv(m)
		| 4: Tasks.Step
		| 5: HALT(126)
		END
	END Do;

	PROCEDURE (t: ReadOwn) Do;
	BEGIN
		t.v.Set(7); t.seen := t.v.Get();
		HALT(43)
	END Do;

	PROCEDURE (t: BareRetry) Do;
	BEGIN
		AsyncStm.Retry
	END Do;

	(* --- своя переменная --------------------------------------------------- *)

	PROCEDURE (v: PointVar) Get (OUT x, y: INTEGER), NEW;
	BEGIN
		AsyncStm.Read(v); x := v.x; y := v.y
	END Get;

	PROCEDURE (v: PointVar) Set (x, y: INTEGER), NEW;
	BEGIN
		AsyncStm.Write(v); v.x := x; v.y := y
	END Set;

	PROCEDURE (v: PointVar) Save (OUT s: AsyncStm.Saved);
		VAR p: Point;
	BEGIN
		NEW(p); p.x := v.x; p.y := v.y; s.p := p
	END Save;

	PROCEDURE (v: PointVar) Load (IN s: AsyncStm.Saved);
	BEGIN
		v.x := s.p(Point).x; v.y := s.p(Point).y
	END Load;

	PROCEDURE (t: MovePoint) Do;
		VAR x, y: INTEGER;
	BEGIN
		t.p.Get(x, y); t.p.Set(x + 1, y + 2);
		IF t.fail THEN HALT(44) END
	END Do;

	PROCEDURE (t: WaitPoint) Do;
		VAR x, y: INTEGER;
	BEGIN
		t.p.Get(x, y);
		IF x = 0 THEN AsyncStm.Retry END;
		t.x := x
	END Do;

	(* --- 1. перевод ------------------------------------------------------- *)

	PROCEDURE Sum (): INTEGER;
		VAR i, s: INTEGER;
	BEGIN
		s := 0;
		FOR i := 0 TO accounts - 1 DO s := s + acc[i].Get() END;
		RETURN s
	END Sum;

	PROCEDURE (w: Worker) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO perWorker DO
			AsyncStm.Atomically(NewTransfer(acc[(w.id + i) MOD accounts], acc[(w.id + 2 * i + 1) MOD accounts], i MOD 7, FALSE));
			INC(transfers);
			IF i MOD 3 = 0 THEN Tasks.Yield END
		END
	END Run;

	PROCEDURE TestTransfersKeepSum*;
		VAR i: INTEGER; w: Worker;
	BEGIN
		FOR i := 0 TO accounts - 1 DO acc[i] := AsyncStm.NewInt(1000) END;
		transfers := 0;
		FOR i := 0 TO workers - 1 DO NEW(w); w.id := i; Tasks.Start(w) END;
		UnitAssert.True(Pump(5000), "workers finish");
		UnitAssert.EqualInt(workers * perWorker, transfers, "all transfers done");
		UnitAssert.EqualInt(accounts * 1000, Sum(), "sum of accounts after 1000 transfers")
	END TestTransfersKeepSum;

	(* --- 2. трап посередине ------------------------------------------------ *)

	PROCEDURE TestTrapRollsBackAndPasses*;
		VAR a, b: AsyncStm.IntVar; err: INTEGER;
	BEGIN
		a := AsyncStm.NewInt(100); b := AsyncStm.NewInt(0);
		UnitAssert.True(~TryAtomically(NewTransfer(a, b, 30, TRUE), err), "transaction traps");
		UnitAssert.EqualInt(42, err, "the same trap number reaches the caller");
		UnitAssert.EqualInt(100, a.Get(), "withdrawal rolled back");
		UnitAssert.EqualInt(0, b.Get(), "deposit not made");
		UnitAssert.True(TryAtomically(NewTransfer(a, b, 30, FALSE), err), "next transaction runs");
		UnitAssert.EqualInt(70, a.Get(), "a after transfer"); UnitAssert.EqualInt(30, b.Get(), "b after transfer")
	END TestTrapRollsBackAndPasses;

	(* --- 3. отмена в Retry -------------------------------------------------- *)

	PROCEDURE TestCancelInRetry*;
		VAR a, mark: AsyncStm.IntVar; w: Withdraw; r: Runner;
	BEGIN
		a := AsyncStm.NewInt(0); mark := AsyncStm.NewInt(0);
		w := NewWithdraw(a, 50); w.mark := mark;
		r := Start(w, 0);
		PumpFor(30);
		UnitAssert.EqualInt(Tasks.Parked, Tasks.State(r), "task waits in Retry");
		UnitAssert.EqualInt(0, mark.Get(), "write before Retry rolled back while waiting");
		Tasks.Cancel(r);
		UnitAssert.True(Pump(1000), "cancelled task ends");
		UnitAssert.EqualInt(Tasks.Cancelled, Tasks.State(r), "task cancelled");
		UnitAssert.EqualInt(127, r.err, "trap 127 reached the caller");
		UnitAssert.EqualInt(0, mark.Get(), "writes rolled back");
		a.Set(100);
		UnitAssert.EqualInt(100, a.Get(), "variable still works after cancelled waiter")
	END TestCancelInRetry;

	(* --- 4. Retry -------------------------------------------------------- *)

	PROCEDURE TestRetryWaitsForDeposit*;
		VAR a, b: AsyncStm.IntVar; r, d: Runner;
	BEGIN
		a := AsyncStm.NewInt(10); b := AsyncStm.NewInt(100);
		r := Start(NewWithdraw(a, 50), 0);
		PumpFor(30);
		UnitAssert.True(~r.done, "not enough money: waits");
		d := Start(NewTransfer(b, a, 60, FALSE), 20);
		UnitAssert.True(Pump(2000), "both tasks finish");
		UnitAssert.True(r.done & d.done, "withdraw done after deposit");
		UnitAssert.EqualInt(20, a.Get(), "10 + 60 - 50");
		UnitAssert.EqualInt(40, b.Get(), "100 - 60")
	END TestRetryWaitsForDeposit;

	PROCEDURE TestRetryWakesOnEitherVar*;
		VAR a: AsyncStm.IntVar; open: AsyncStm.BoolVar; w: Withdraw; r: Runner;
	BEGIN
		a := AsyncStm.NewInt(0); open := AsyncStm.NewBool(FALSE);
		w := NewWithdraw(a, 50); w.open := open;
		r := Start(w, 0);
		PumpFor(20);
		a.Set(100);
		PumpFor(20);
		UnitAssert.True(~r.done, "money is there, account closed: still waits");
		open.Set(TRUE);
		UnitAssert.True(Pump(1000), "opening the account wakes the task");
		UnitAssert.True(r.done, "withdraw done");
		UnitAssert.EqualInt(50, a.Get(), "100 - 50")
	END TestRetryWakesOnEitherVar;

	(* --- 5. OrElse ------------------------------------------------------- *)

	PROCEDURE NewEither (a, b: AsyncStm.IntVar; n: INTEGER; mark: AsyncStm.IntVar): Either;
		VAR e: Either;
	BEGIN
		NEW(e); e.a := NewWithdraw(a, n); e.b := NewWithdraw(b, n); e.a.mark := mark;
		RETURN e
	END NewEither;

	PROCEDURE TestOrElseTakesSecond*;
		VAR a, b, mark: AsyncStm.IntVar; err: INTEGER;
	BEGIN
		a := AsyncStm.NewInt(0); b := AsyncStm.NewInt(100); mark := AsyncStm.NewInt(0);
		UnitAssert.True(TryAtomically(NewEither(a, b, 30, mark), err), "OrElse on the main stack");
		UnitAssert.EqualInt(0, a.Get(), "a untouched"); UnitAssert.EqualInt(70, b.Get(), "taken from b");
		UnitAssert.EqualInt(0, mark.Get(), "write of the first branch rolled back")
	END TestOrElseTakesSecond;

	PROCEDURE TestOrElseWaitsOnUnion*;
		VAR a, b: AsyncStm.IntVar; r1, r2: Runner;
	BEGIN
		a := AsyncStm.NewInt(0); b := AsyncStm.NewInt(0);
		r1 := Start(NewEither(a, b, 30, NIL), 0);
		PumpFor(20);
		UnitAssert.True(~r1.done, "both branches retry: waits");
		b.Set(100);
		UnitAssert.True(Pump(1000), "change of b (second branch) wakes");
		UnitAssert.EqualInt(70, b.Get(), "taken from b");
		r2 := Start(NewEither(a, b, 80, NIL), 0);
		PumpFor(20);
		UnitAssert.True(~r2.done, "70 < 80 on b, 0 on a: waits");
		a.Set(100);
		UnitAssert.True(Pump(1000), "change of a (first branch) wakes");
		UnitAssert.EqualInt(20, a.Get(), "taken from a")
	END TestOrElseWaitsOnUnion;

	(* --- 6. вложенные транзакции ------------------------------------------ *)

	PROCEDURE TestNestedRollsBackBoth*;
		VAR a, b, c: AsyncStm.IntVar; t: Both; err: INTEGER;
	BEGIN
		a := AsyncStm.NewInt(100); b := AsyncStm.NewInt(0); c := AsyncStm.NewInt(0);
		NEW(t); t.first := NewTransfer(a, b, 10, FALSE); t.second := NewTransfer(b, c, 5, FALSE);
		UnitAssert.True(TryAtomically(t, err), "two transfers in one");
		UnitAssert.EqualInt(90, a.Get(), "a"); UnitAssert.EqualInt(5, b.Get(), "b"); UnitAssert.EqualInt(5, c.Get(), "c");
		t.second.fail := TRUE;
		UnitAssert.True(~TryAtomically(t, err), "second traps");
		UnitAssert.EqualInt(42, err, "trap of the inner transaction passes");
		UnitAssert.EqualInt(90, a.Get(), "first transfer rolled back: a");
		UnitAssert.EqualInt(5, b.Get(), "first transfer rolled back: b");
		UnitAssert.EqualInt(5, c.Get(), "c")
	END TestNestedRollsBackBoth;

	(* --- 7. точки переключения запрещены --------------------------------- *)

	PROCEDURE NewViolate (v: AsyncStm.IntVar; what: INTEGER): Violate;
		VAR t: Violate;
	BEGIN
		NEW(t); t.v := v; t.what := what;
		RETURN t
	END NewViolate;

	PROCEDURE CheckViolationCode (what, code: INTEGER; IN m: ARRAY OF CHAR);
		VAR v: AsyncStm.IntVar; r: Runner;
	BEGIN
		v := AsyncStm.NewInt(0);
		r := Start(NewViolate(v, what), 0);
		UnitAssert.True(Pump(1000), m);
		UnitAssert.EqualInt(code, r.err, m);
		UnitAssert.EqualInt(0, v.Get(), m)
	END CheckViolationCode;

	PROCEDURE CheckViolation (what: INTEGER; IN m: ARRAY OF CHAR);
	BEGIN
		CheckViolationCode(what, 25, m)
	END CheckViolation;

	PROCEDURE TestNoYieldInside*;
	BEGIN
		CheckViolation(0, "Yield in a transaction: trap 25, rollback")
	END TestNoYieldInside;

	PROCEDURE TestNoSleepInside*;
	BEGIN
		CheckViolation(1, "Sleep in a transaction: trap 25, rollback")
	END TestNoSleepInside;

	PROCEDURE TestNoChannelWaitInside*;
		VAR v: AsyncStm.IntVar; t: Violate; r: Runner;
	BEGIN
		v := AsyncStm.NewInt(0);
		t := NewViolate(v, 2); t.c := AsyncChan.NewChan();
		r := Start(t, 0);
		UnitAssert.True(Pump(1000), "task ends");
		UnitAssert.EqualInt(25, r.err, "receive with no sender: trap 25");
		UnitAssert.EqualInt(0, v.Get(), "rollback")
	END TestNoChannelWaitInside;

	PROCEDURE TestNoStepInside*;
		VAR v: AsyncStm.IntVar; err: INTEGER;
	BEGIN
		v := AsyncStm.NewInt(0);
		UnitAssert.True(~TryAtomically(NewViolate(v, 4), err), "Step in a transaction traps");
		UnitAssert.EqualInt(22, err, "Tasks.Step: trap 22");
		UnitAssert.EqualInt(0, v.Get(), "rollback")
	END TestNoStepInside;

	(* --- 8. своя запись ---------------------------------------------------- *)

	PROCEDURE TestUserHalt126IsNotRetry*;
	(** HALT с номером тихого трапа Retry — обычный трап: откат и передача,
		а не ожидание. *)
	BEGIN
		CheckViolationCode(5, 126, "HALT(126) in a transaction: rollback, trap passes")
	END TestUserHalt126IsNotRetry;

	PROCEDURE TestReadOwnWrite*;
		VAR t: ReadOwn; err: INTEGER;
	BEGIN
		NEW(t); t.v := AsyncStm.NewInt(1); t.seen := 0;
		UnitAssert.True(~TryAtomically(t, err), "traps");
		UnitAssert.EqualInt(7, t.seen, "inside: own write is seen");
		UnitAssert.EqualInt(1, t.v.Get(), "after rollback: old value")
	END TestReadOwnWrite;

	(* --- 9. предусловия Retry --------------------------------------------- *)

	PROCEDURE TestRetryWithoutReadsTraps*;
		VAR t: BareRetry; r: Runner;
	BEGIN
		NEW(t); r := Start(t, 0);
		UnitAssert.True(Pump(1000), "task ends");
		UnitAssert.EqualInt(21, r.err, "Retry with nothing read: trap 21")
	END TestRetryWithoutReadsTraps;

	PROCEDURE TestRetryOnMainStackTraps*;
		VAR a: AsyncStm.IntVar; err: INTEGER; w: Withdraw;
	BEGIN
		a := AsyncStm.NewInt(0); w := NewWithdraw(a, 1); w.mark := AsyncStm.NewInt(0);
		UnitAssert.True(~TryAtomically(w, err), "traps");
		UnitAssert.EqualInt(22, err, "Retry on the main stack: trap 22");
		UnitAssert.EqualInt(0, w.mark.Get(), "rollback")
	END TestRetryOnMainStackTraps;

	PROCEDURE RetryOutside;
	BEGIN
		AsyncStm.Retry
	END RetryOutside;

	PROCEDURE TestRetryOutsideTraps*;
	BEGIN
		UnitAssert.Traps(RetryOutside, "Retry outside a transaction")
	END TestRetryOutsideTraps;

	(* --- 10. тупик ------------------------------------------------------- *)

	PROCEDURE TestAllInRetryDeadlock*;
		VAR a, b: AsyncStm.IntVar; r1, r2: Runner;
	BEGIN
		a := AsyncStm.NewInt(0); b := AsyncStm.NewInt(0);
		r1 := Start(NewWithdraw(a, 1), 0); r2 := Start(NewWithdraw(b, 1), 0);
		PumpFor(30);
		UnitAssert.True(Tasks.Deadlocked(), "scheduler reports deadlock");
		Tasks.Cancel(r1); Tasks.Cancel(r2);
		UnitAssert.True(Pump(1000), "cancelled tasks end");
		UnitAssert.True(~Tasks.Deadlocked(), "no deadlock after cancel")
	END TestAllInRetryDeadlock;

	(* --- 11. своя переменная ---------------------------------------------- *)

	PROCEDURE TestOwnVarType*;
		VAR p: PointVar; m: MovePoint; w: WaitPoint; r: Runner; x, y, err: INTEGER;
	BEGIN
		NEW(p); p.x := 1; p.y := 1;
		NEW(m); m.p := p; m.fail := TRUE;
		UnitAssert.True(~TryAtomically(m, err), "traps");
		p.Get(x, y);
		UnitAssert.EqualInt(1, x, "x restored"); UnitAssert.EqualInt(1, y, "y restored");
		p.Set(0, 0);
		NEW(w); w.p := p; r := Start(w, 0);
		PumpFor(20);
		UnitAssert.True(~r.done, "waits for x # 0");
		m.fail := FALSE; UnitAssert.True(TryAtomically(m, err), "move commits");
		UnitAssert.True(Pump(1000), "commit wakes the waiter");
		UnitAssert.EqualInt(1, w.x, "waiter sees the committed point")
	END TestOwnVarType;

	(* --- 12. цена -------------------------------------------------------- *)

	PROCEDURE Now (): LONGINT;
	(* Наносекунды по Kernel.Time: миллисекунд хватает на миллион операций. *)
	BEGIN
		RETURN Kernel.Time() * 1000000
	END Now;

	PROCEDURE Report (IN name: ARRAY OF CHAR; ns: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Console.WriteStr("BENCH "); Console.WriteStr(name);
		Strings.IntToString(benchN, s); Console.WriteStr(" ops="); Console.WriteStr(s);
		Strings.IntToString(ns DIV benchN, s); Console.WriteStr(" nsPerOp="); Console.WriteStr(s);
		Console.WriteLn
	END Report;

	PROCEDURE (c: Counter) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: MoveOp DO
			DEC(c.n, op.k); INC(op.to.n, op.k)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (j: MoveJob) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: MoveOp; t0: LONGINT;
	BEGIN
		op.to := j.y; op.k := 1;
		t0 := Now();
		FOR i := 1 TO benchN DO j.x.Do(ctx, op) END;
		Report("monitor-transfer", Now() - t0)
	END Body;

	PROCEDURE TestSlowBench*;
		VAR a, b: AsyncStm.IntVar; t: Transfer; i: INTEGER; t0: LONGINT; j: MoveJob;
	BEGIN
		a := AsyncStm.NewInt(benchN); b := AsyncStm.NewInt(0); t := NewTransfer(a, b, 1, FALSE);
		t0 := Now();
		FOR i := 1 TO benchN DO AsyncStm.Atomically(t) END;
		Report("stm-transfer", Now() - t0);
		UnitAssert.EqualInt(benchN, b.Get(), "all transfers done");
		t0 := Now();
		FOR i := 1 TO benchN DO a.Set(a.Get() - 1); b.Set(b.Get() + 1) END;
		Report("plain-var-transfer", Now() - t0);
		NEW(j); NEW(j.x); NEW(j.y); j.x.n := benchN; j.y.n := 0;
		Tasks.Start(j);
		UnitAssert.True(Pump(20000), "monitor-transfer job finishes");
		UnitAssert.EqualInt(benchN, j.y.n, "monitor transfers done")
	END TestSlowBench;

END Async_devStm.
