MODULE DemoPhilo;
(** Обедающие философы.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение C.1 "Dining Philosophers" и B.4
	"Binary and Generic Semaphores".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал: активный объект Philosopher с телом {ACTIVE} LOOP ...
	и семафоры Semaphores.Sem (P/V на AWAIT). Здесь тело активности
	стало методом Body задачи Tasks.Job, семафор — монитором-объектом
	Sem с операцией Take/Release: P ждёт флага «свободен», V отпускает.

	Асимметрия оригинальная: философ N-1 берёт вилки в обратном порядке,
	поэтому циклического ожидания нет. Команда DemoPhilo.Run: 5
	философов по 3 обеда, итог — 15. *)

	IMPORT Tasks, DemoSupport;

	CONST
		NofPhilo = 5;
		meals = 3;
		isFree = 0;		(* флаг Ready: семафор свободен *)

	TYPE
		SemOp = RECORD (Tasks.Op) take: BOOLEAN END;

		Sem* = POINTER TO RECORD (Tasks.Object)
			taken: BOOLEAN
		END;

		Philosopher = POINTER TO RECORD (Tasks.Job)
			first, second: INTEGER	(* вилки этого философа *)
		END;

	VAR
		fork: ARRAY NofPhilo OF Sem;
		philo: ARRAY NofPhilo OF Philosopher;
		Eaten*: INTEGER;	(* сколько обедов всего *)
		Failed*: BOOLEAN;

	PROCEDURE (s: Sem) Ready* (): SET;
	BEGIN
		IF ~s.taken THEN RETURN {isFree} ELSE RETURN {} END
	END Ready;

	PROCEDURE (s: Sem) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: SemOp DO
			IF op.take THEN
				WHILE s.taken DO
					IF ~s.Await(ctx, {isFree}) THEN RETURN END
				END;
				s.taken := TRUE
			ELSE
				s.taken := FALSE
			END
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE P (s: Sem; VAR ctx: Tasks.Ctx);
		VAR op: SemOp;
	BEGIN
		op.take := TRUE; s.Do(ctx, op)
	END P;

	PROCEDURE V (s: Sem; VAR ctx: Tasks.Ctx);
		VAR op: SemOp;
	BEGIN
		op.take := FALSE; s.Do(ctx, op)
	END V;

	PROCEDURE (p: Philosopher) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO meals DO
			Tasks.Yield;	(* .... Think .... *)
			P(fork[p.first], ctx);
			P(fork[p.second], ctx);
			INC(Eaten);	(* .... Eat .... *)
			Tasks.Yield;
			V(fork[p.first], ctx);
			V(fork[p.second], ctx)
		END
	END Body;

	PROCEDURE Run*;
	(** Демонстрация: 5 философов по 3 обеда. *)
		VAR i, id: INTEGER; ok: BOOLEAN;
	BEGIN
		Eaten := 0; Failed := FALSE;
		FOR i := 0 TO NofPhilo - 1 DO
			NEW(fork[i]); fork[i].taken := FALSE;
			NEW(philo[i]);
			id := i;
			IF id # NofPhilo - 1 THEN
				philo[i].first := id; philo[i].second := (id + 1) MOD NofPhilo
			ELSE
				philo[i].first := 0; philo[i].second := NofPhilo - 1
			END
		END;
		FOR i := 0 TO NofPhilo - 1 DO Tasks.Start(philo[i]) END;
		ok := DemoSupport.Pump(15000);
		FOR i := 0 TO NofPhilo - 1 DO
			IF Tasks.Failed(philo[i]) THEN Failed := TRUE END
		END;
		Failed := Failed OR ~ok;
		DemoSupport.LogInt("DemoPhilo: eaten=", Eaten);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoPhilo.
