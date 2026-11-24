MODULE DemoBuffer;
(** Ограниченный буфер на ACTIVE.AWAIT.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение B.6 "Bounded Buffer".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал на Active Oberon:
		PROCEDURE Put* (i: INTEGER);
		BEGIN {EXCLUSIVE} AWAIT ((in + 1) MOD BufLen # out);
			data[in] := i; in := (in + 1) MOD BufLen END Put;
	Запись {EXCLUSIVE} здесь стала флагом [exclusive], AWAIT —
	ACTIVE.AWAIT. Инициализатор & Init не нужен: NEW обнуляет поля.

	Команда DemoBuffer.Run запускает трёх производителей и трёх
	потребителей; итог детерминирован — 120 элементов, сумма 2460. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		BufLen* = 8;		(* ёмкость кольца, как BufLen в оригинале *)
		items = 120;
		producers = 3;
		consumers = 3;
		per = items DIV producers;

	TYPE
		Buffer* = POINTER TO RECORD
			data: ARRAY BufLen OF INTEGER;
			in*, out*: LONGINT
		END;

		Producer = POINTER TO RECORD (Tasks.Task) b: Buffer END;
		Consumer = POINTER TO RECORD (Tasks.Task) b: Buffer END;

	VAR
		Produced*: INTEGER;	(* сколько элементов положили *)
		Consumed*: INTEGER;	(* сколько забрали *)
		Sum*: INTEGER;		(* сумма забранных элементов *)
		Failed*: BOOLEAN;	(* трап хотя бы в одной задаче *)

	PROCEDURE [exclusive] (b: Buffer) Put* (i: INTEGER), NEW;
	(** Положить элемент; ждать, пока в кольце есть место. *)
	BEGIN
		ACTIVE.AWAIT((b.in + 1) MOD BufLen # b.out);	(* AWAIT ~full *)
		b.data[b.in] := i;
		b.in := (b.in + 1) MOD BufLen
	END Put;

	PROCEDURE [exclusive] (b: Buffer) Get* (VAR i: INTEGER), NEW;
	(** Забрать элемент; ждать, пока кольцо не пусто. *)
	BEGIN
		ACTIVE.AWAIT(b.in # b.out);	(* AWAIT ~empty *)
		i := b.data[b.out];
		b.out := (b.out + 1) MOD BufLen
	END Get;

	PROCEDURE (p: Producer) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO per DO
			p.b.Put(i);
			INC(Produced)
		END
	END Run;

	PROCEDURE (c: Consumer) Run;
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO per DO
			c.b.Get(x);
			INC(Consumed); INC(Sum, x)
		END
	END Run;

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
