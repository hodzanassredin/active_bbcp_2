MODULE DemoPhilo;
(** Обедающие философы.

	Перенесено из книги: P. Reali, Active Oberon Language Report,
	ETH Zurich, 27.10.2004, приложение C.1 "Dining Philosophers" и B.4
	"Binary and Generic Semaphores".
	Источник: https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf
	Оригинал: активный объект Philosopher с телом {ACTIVE} LOOP ...
	и семафоры Semaphores.Sem (P/V на AWAIT). Здесь тело активности
	стало методом Run задачи Tasks, семафор — записью с [exclusive]
	методами P и V.

	Асимметрия оригинальная: философ N-1 берёт вилки в обратном порядке,
	поэтому циклического ожидания нет. Команда DemoPhilo.Run: 5
	философов по 3 обеда, итог — 15. *)

	IMPORT ACTIVE, Tasks, DemoSupport;

	CONST
		NofPhilo = 5;
		meals = 3;

	TYPE
		Sem* = POINTER TO RECORD
			taken: BOOLEAN
		END;

		Philosopher = POINTER TO RECORD (Tasks.Task)
			first, second: INTEGER	(* вилки этого философа *)
		END;

	VAR
		fork: ARRAY NofPhilo OF Sem;
		philo: ARRAY NofPhilo OF Philosopher;
		Eaten*: INTEGER;	(* сколько обедов всего *)
		Failed*: BOOLEAN;

	PROCEDURE [exclusive] (s: Sem) P*, NEW;
	(** Взять семафор; ждать, пока он свободен. *)
	BEGIN
		ACTIVE.AWAIT(~s.taken);
		s.taken := TRUE
	END P;

	PROCEDURE [exclusive] (s: Sem) V*, NEW;
	(** Отпустить семафор. *)
	BEGIN
		s.taken := FALSE
	END V;

	PROCEDURE (p: Philosopher) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 1 TO meals DO
			Tasks.Yield;	(* .... Think .... *)
			fork[p.first].P;
			fork[p.second].P;
			INC(Eaten);	(* .... Eat .... *)
			Tasks.Yield;
			fork[p.first].V;
			fork[p.second].V
		END
	END Run;

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
