MODULE DemoModuleMonitor;
(** Производитель-потребитель на одном мониторе-объекте.

	Перенесено из примера лекции System Construction (ETH Zurich,
	"System Construction", 2019, раздел про AWAIT):
		PROCEDURE Produce (x: object);
		BEGIN {EXCLUSIVE} AWAIT(free # 0); DEC(free);
			buf[tail] := x; tail := (tail + 1) MOD N; INC(available) END;
		PROCEDURE Consume (): object;
		BEGIN {EXCLUSIVE} AWAIT(available # 0); DEC(available);
			x := buf[head]; head := (head + 1) MOD N; INC(free); RETURN x END
	В оригинале процедуры — методы объекта; монитор у них общий. Здесь
	монитор — объект Store в переменной модуля: очередь и счётчики —
	его поля, Produce и Consume — операции PutOp/GetOp над ним.

	Команда DemoModuleMonitor.Run: 3 производителя и 3 потребителя по
	40 элементов. Итог детерминирован: 120, сумма 2460. *)

	IMPORT Tasks, DemoSupport;

	CONST
		cap = 8;
		items = 120;
		producers = 3;
		consumers = 3;
		per = items DIV producers;
		freeFlag = 0;		(* флаг Ready: есть место *)
		availableFlag = 1;	(* флаг Ready: есть элемент *)

	TYPE
		PutOp = RECORD (Tasks.Op) x: INTEGER END;
		GetOp = RECORD (Tasks.Op) x: INTEGER END;

		Store = POINTER TO RECORD (Tasks.Object)
			free, available: INTEGER;
			buf: ARRAY cap OF INTEGER;
			head, tail: INTEGER
		END;

		Producer = POINTER TO RECORD (Tasks.Job) store: Store; first: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) store: Store END;

	VAR
		store: Store;

		Produced*: INTEGER;
		Consumed*: INTEGER;
		Sum*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE (s: Store) Ready* (): SET;
		VAR r: SET;
	BEGIN
		r := {};
		IF s.free # 0 THEN INCL(r, freeFlag) END;
		IF s.available # 0 THEN INCL(r, availableFlag) END;
		RETURN r
	END Ready;

	PROCEDURE (s: Store) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			WHILE s.free = 0 DO
				IF ~s.Await(ctx, {freeFlag}) THEN RETURN END
			END;
			DEC(s.free);
			s.buf[s.tail] := op.x;
			s.tail := (s.tail + 1) MOD cap;
			INC(s.available)
		| op: GetOp DO
			WHILE s.available = 0 DO
				IF ~s.Await(ctx, {availableFlag}) THEN RETURN END
			END;
			DEC(s.available);
			op.x := s.buf[s.head];
			s.head := (s.head + 1) MOD cap;
			INC(s.free)
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO per DO
			op.x := p.first + i;
			p.store.Do(ctx, op);
			INC(Produced)
		END
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO per DO
			op.x := 0;
			c.store.Do(ctx, op);
			INC(Consumed); INC(Sum, op.x)
		END
	END Body;

	PROCEDURE Run*;
	(** Демонстрация: общий монитор у Produce и Consume. *)
		VAR p: ARRAY producers OF Producer; c: ARRAY consumers OF Consumer;
			i: INTEGER; ok: BOOLEAN;
	BEGIN
		NEW(store);
		store.free := cap; store.available := 0; store.head := 0; store.tail := 0;
		Produced := 0; Consumed := 0; Sum := 0; Failed := FALSE;
		FOR i := 0 TO producers - 1 DO
			NEW(p[i]); p[i].store := store; p[i].first := i * per; Tasks.Start(p[i])
		END;
		FOR i := 0 TO consumers - 1 DO
			NEW(c[i]); c[i].store := store; Tasks.Start(c[i])
		END;
		ok := DemoSupport.Pump(15000);
		FOR i := 0 TO producers - 1 DO
			IF Tasks.Failed(p[i]) THEN Failed := TRUE END
		END;
		FOR i := 0 TO consumers - 1 DO
			IF Tasks.Failed(c[i]) THEN Failed := TRUE END
		END;
		Failed := Failed OR ~ok;
		DemoSupport.LogInt("DemoModuleMonitor: produced=", Produced);
		DemoSupport.LogInt(" consumed=", Consumed);
		DemoSupport.LogInt(" sum=", Sum);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoModuleMonitor.
