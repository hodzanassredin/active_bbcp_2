# Обзор на примерах: DemoPhilo

Обедающие философы на семафорах.

Пять философов сидят за круглым столом, между соседями лежит одна вилка, а есть можно только
двумя. Если каждый возьмёт левую вилку и будет ждать правую, все ждут вечно. Пример переносит
решение из P. Reali, *Active Oberon Language Report* (приложения C.1 «Dining Philosophers» и B.4
«Binary and Generic Semaphores»;
<https://oberoncore.ru/_media/wiki/lang/reali_p.active_oberon_language_report.en.pdf>).

```
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
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/Philo.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoPhilo.Run
```

В консоли:

```sh
echo 'DemoPhilo.Run' | tools/run64.sh
```

В журнале появится:

```
DemoPhilo: eaten=15
```

## Что произошло

Вилка — бинарный семафор `Sem`: запись с исключающими методами `P` и `V`. `P` ждёт
`ACTIVE.AWAIT(~s.taken)` и занимает вилку, `V` её освобождает. Активное тело философа из оригинала
(`{ACTIVE} LOOP`) стало методом `Run` задачи.

Тупик разорван порядком: философ `N-1` берёт первой вилку 0, а не свою левую, поэтому круговое
ожидание невозможно. `Think` и `Eat` вызывают `Tasks.Yield`, чтобы философы чередовались. Каждый
обедает три раза, итог — 15.

## Тест

`Demo_devExamples.TestProcPhilosophers` проверяет 15 обедов и отсутствие трапов.

В этом примере мы увидели семафор на `ACTIVE.AWAIT` и то, что тупик предотвращают порядком
захвата ресурсов, а не механизмом.
