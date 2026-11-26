# Обзор на примерах: DemoWorkerPool

Пул рабочих.

Заданий больше, чем стоит заводить задач; их раздают нескольким рабочим. Пример взят из
gobyexample «Worker Pools» (<https://gobyexample.com/worker-pools>): канал заданий, канал
результатов и несколько рабочих.

```
MODULE DemoWorkerPool;
(** Пул рабочих задач.

	Перенесено из gobyexample "Worker Pools": канал заданий, несколько
	рабочих горутин, канал результатов; признак конца — отдельное
	сообщение на каждого рабочего.
	Источник: https://gobyexample.com/worker-pools
	Здесь задания и результаты — сообщения AsyncSelect.Msg, канал
	заданий ограничен (AsyncChan.Buffer), результаты складываются в
	почтовый ящик AsyncChan.Mailbox.

	Команда DemoWorkerPool.Run: 3 рабочих, 9 заданий. Порядок
	результатов не фиксирован, итог детерминирован: 9 результатов,
	сумма квадратов 285. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		Workers* = 3;
		jobs = 9;

	TYPE
		IntMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Worker = POINTER TO RECORD (Tasks.Job)
			jobs: AsyncChan.Buffer;
			results: AsyncChan.Mailbox
		END;

		Controller = POINTER TO RECORD (Tasks.Job)
			jobs: AsyncChan.Buffer;
			results: AsyncChan.Mailbox
		END;

	VAR
		Sum*: INTEGER;
		Count*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE NewInt (n: INTEGER): IntMsg;
		VAR m: IntMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewInt;

	PROCEDURE (w: Worker) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			w.jobs.Recv(m);
			WITH m: IntMsg DO n := m.n ELSE n := -1 END;
			IF n < 0 THEN EXIT END;
			w.results.Send(NewInt(n * n))
		END
	END Body;

	PROCEDURE (c: Controller) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; m: AsyncSelect.Msg;
	BEGIN
		FOR i := 1 TO jobs DO c.jobs.Send(NewInt(i)) END;
		FOR i := 1 TO Workers DO c.jobs.Send(NewInt(-1)) END;
		FOR i := 1 TO jobs DO
			c.results.Recv(m);
			WITH m: IntMsg DO INC(Sum, m.n); INC(Count) END
		END
	END Body;

	PROCEDURE Run*;
	(** Демонстрация: 3 рабочих, 9 заданий n -> n*n. *)
		VAR j: AsyncChan.Buffer; r: AsyncChan.Mailbox;
			w: ARRAY Workers OF Worker; c: Controller;
			i: INTEGER; ok: BOOLEAN;
	BEGIN
		Sum := 0; Count := 0; Failed := FALSE;
		j := AsyncChan.NewBuffer(3);
		r := AsyncChan.NewMailbox();
		FOR i := 0 TO Workers - 1 DO
			NEW(w[i]); w[i].jobs := j; w[i].results := r; Tasks.Start(w[i])
		END;
		NEW(c); c.jobs := j; c.results := r; Tasks.Start(c);
		ok := DemoSupport.Pump(15000);
		Failed := ~ok OR Tasks.Failed(c);
		FOR i := 0 TO Workers - 1 DO
			IF Tasks.Failed(w[i]) THEN Failed := TRUE END
		END;
		DemoSupport.LogInt("DemoWorkerPool: workers=", Workers);
		DemoSupport.LogInt(" count=", Count);
		DemoSupport.LogInt(" sum=", Sum);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoWorkerPool.
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/WorkerPool.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoWorkerPool.Run
```

В консоли:

```sh
echo 'DemoWorkerPool.Run' | tools/run64.sh
```

В журнале появится:

```
DemoWorkerPool: workers=3 count=9 sum=285
```

## Что произошло

Канал заданий — ограниченный буфер `AsyncChan.Buffer(3)`, канал результатов — неограниченный
ящик `AsyncChan.Mailbox`, поэтому рабочие никогда не ждут на отправке результата. Три рабочих
возводят числа в квадрат; управляющий посылает девять заданий и три признака конца, затем
собирает девять результатов.

Порядок результатов не фиксирован, сумма — да: 1² + … + 9² = 285.

## Тест

`Demo_devExamples.TestProcWorkerPool` проверяет трёх рабочих, девять результатов и сумму 285.

В этом примере мы увидели буфер и ящик в одной программе и то, где каждый из них уместен.
