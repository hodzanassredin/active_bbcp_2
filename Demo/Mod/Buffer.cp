MODULE DemoBuffer;
(** Ограниченный буфер на мониторе-объекте Tasks.Object.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение B.6 "Bounded Buffer".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал на Active Oberon:
		PROCEDURE Put* (i: INTEGER);
		BEGIN {EXCLUSIVE} AWAIT ((in + 1) MOD BufLen # out);
			data[in] := i; in := (in + 1) MOD BufLen END Put;
	Запись {EXCLUSIVE} здесь стала монитором-объектом Buffer, AWAIT —
	Buffer.Await по флагам Ready. Операции Put и Get — записи-наследники
	Tasks.Op на стеке задачи; монитор освобождается на время ожидания.
	Инициализатор & Init не нужен: NEW обнуляет поля.

	Команда DemoBuffer.Run запускает трёх производителей и трёх
	потребителей; итог детерминирован — 120 элементов, сумма 2460. *)

	IMPORT Tasks, DemoSupport;

	CONST
		BufLen* = 8;		(* ёмкость кольца, как BufLen в оригинале *)
		items = 120;
		producers = 3;
		consumers = 3;
		per = items DIV producers;
		empty = 0;		(* флаг Ready: есть место *)
		full = 1;		(* флаг Ready: есть элемент *)

	TYPE
		PutOp = RECORD (Tasks.Op) i: INTEGER END;
		GetOp = RECORD (Tasks.Op) i: INTEGER END;

		Buffer* = POINTER TO RECORD (Tasks.Object)
			data: ARRAY BufLen OF INTEGER;
			in*, out*: LONGINT
		END;

		Producer = POINTER TO RECORD (Tasks.Job) b: Buffer END;
		Consumer = POINTER TO RECORD (Tasks.Job) b: Buffer END;

	VAR
		Produced*: INTEGER;	(* сколько элементов положили *)
		Consumed*: INTEGER;	(* сколько забрали *)
		Sum*: INTEGER;		(* сумма забранных элементов *)
		Failed*: BOOLEAN;	(* трап хотя бы в одной задаче *)

	PROCEDURE (b: Buffer) Ready* (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF (b.in + 1) MOD BufLen # b.out THEN INCL(s, empty) END;
		IF b.in # b.out THEN INCL(s, full) END;
		RETURN s
	END Ready;

	PROCEDURE (b: Buffer) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			(* AWAIT ~full *)
			WHILE (b.in + 1) MOD BufLen = b.out DO
				IF ~b.Await(ctx, {empty}) THEN RETURN END
			END;
			b.data[b.in] := op.i;
			b.in := (b.in + 1) MOD BufLen
		| op: GetOp DO
			(* AWAIT ~empty *)
			WHILE b.in = b.out DO
				IF ~b.Await(ctx, {full}) THEN RETURN END
			END;
			op.i := b.data[b.out];
			b.out := (b.out + 1) MOD BufLen
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO per DO
			op.i := i;
			p.b.Do(ctx, op);
			INC(Produced)
		END
	END Body;

	PROCEDURE (c: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		FOR i := 1 TO per DO
			op.i := 0;
			c.b.Do(ctx, op);
			INC(Consumed); INC(Sum, op.i)
		END
	END Body;

	PROCEDURE Run*;
	(** Демонстрация: 3 производителя и 3 потребителя по 40 элементов. *)
		VAR b: Buffer; p: ARRAY producers OF Producer;
			c: ARRAY consumers OF Consumer; i: INTEGER; ok: BOOLEAN;
	BEGIN
		Produced := 0; Consumed := 0; Sum := 0; Failed := FALSE;
		NEW(b);
		FOR i := 0 TO producers - 1 DO
			NEW(p[i]); p[i].b := b; Tasks.Start(p[i])
		END;
		FOR i := 0 TO consumers - 1 DO
			NEW(c[i]); c[i].b := b; Tasks.Start(c[i])
		END;
		ok := DemoSupport.Pump(15000);
		FOR i := 0 TO producers - 1 DO
			IF Tasks.Failed(p[i]) THEN Failed := TRUE END
		END;
		FOR i := 0 TO consumers - 1 DO
			IF Tasks.Failed(c[i]) THEN Failed := TRUE END
		END;
		Failed := Failed OR ~ok;
		DemoSupport.LogInt("DemoBuffer: produced=", Produced);
		DemoSupport.LogInt(" consumed=", Consumed); DemoSupport.LogInt(" sum=", Sum);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoBuffer.
