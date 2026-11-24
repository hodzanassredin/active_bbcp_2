MODULE DemoModuleMonitor;
(** Производитель-потребитель на мониторе модуля.

	Перенесено из примера лекции System Construction (ETH Zurich,
	"System Construction", 2019, раздел про AWAIT):
		PROCEDURE Produce (x: object);
		BEGIN {EXCLUSIVE} AWAIT(free # 0); DEC(free);
			buf[tail] := x; tail := (tail + 1) MOD N; INC(available) END;
		PROCEDURE Consume (): object;
		BEGIN {EXCLUSIVE} AWAIT(available # 0); DEC(available);
			x := buf[head]; head := (head + 1) MOD N; INC(free); RETURN x END;
	В оригинале процедуры — методы объекта; здесь они top-level и
	помечены [exclusive], поэтому монитор у них общий — монитор модуля
	(скрытый объект, который компилятор заводит на модуль).

	Команда DemoModuleMonitor.Run: 3 производителя и 3 потребителя по
	40 элементов. Итог детерминирован: 120, сумма 2460. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		cap = 8;
		items = 120;
		producers = 3;
		consumers = 3;
		per = items DIV producers;

	TYPE
		Producer = POINTER TO RECORD (Tasks.Task) first: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Task) END;

	VAR
		free, available: INTEGER;
		buf: ARRAY cap OF INTEGER;
		head, tail: INTEGER;

		Produced*: INTEGER;
		Consumed*: INTEGER;
		Sum*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE [exclusive] Produce* (x: INTEGER);
	(** Положить элемент; монитор — весь модуль. *)
	BEGIN
		ACTIVE.AWAIT(free # 0);
		DEC(free);
		buf[tail] := x;
		tail := (tail + 1) MOD cap;
		INC(available)
	END Produce;

	PROCEDURE [exclusive] Consume* (VAR x: INTEGER);
	(** Забрать элемент; монитор — весь модуль. *)
	BEGIN
		ACTIVE.AWAIT(available # 0);
		DEC(available);
		x := buf[head];
		head := (head + 1) MOD cap;
		INC(free)
	END Consume;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO per DO
			Produce(p.first + i);
			INC(Produced)
		END
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO per DO
			Consume(x);
			INC(Consumed); INC(Sum, x)
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: общий монитор модуля у Produce и Consume. *)
		VAR p: ARRAY producers OF Producer; c: ARRAY consumers OF Consumer;
			i: INTEGER; ok: BOOLEAN;
	BEGIN
		free := cap; available := 0; head := 0; tail := 0;
		Produced := 0; Consumed := 0; Sum := 0; Failed := FALSE;
		FOR i := 0 TO producers - 1 DO
			NEW(p[i]); p[i].first := i * per; Tasks.Start(p[i])
		END;
		FOR i := 0 TO consumers - 1 DO
			NEW(c[i]); Tasks.Start(c[i])
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
