# Обзор на примерах: ObxRestart

Задача создаётся один раз и запускается многократно.

Одна задача лежит в переменной модуля и создана при загрузке; команда `Do` запускает её десять
тысяч раз подряд. Набор из восьми задач создан один раз массивом; команда `Batch` запускает всю
пачку сто раз. После прогрева повторный запуск не выделяет памяти: стек возвращается в пул и
берётся оттуда, запись реестра стека привязывается заново. Пример показывает повторный запуск
завершённой задачи и цену этого пути. Общие понятия задачи и пропуска разобраны в
`Obx/Docu/ru/BufCtx.md`; пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxRestart;
(** Задача создаётся один раз и запускается многократно.

	Одна задача — указатель в переменной модуля, созданный при загрузке.
	Команда Do запускает её n раз подряд. Набор N задач создан один раз
	массивом; Batch запускает всю пачку rounds раз.

	После прогрева повторный запуск не выделяет памяти: стек возвращается
	в пул и берётся оттуда, запись реестра стека привязывается заново.
	Output — число прогонов и выделений. *)

	IMPORT Tasks, Kernel, Console, Strings;

	CONST
		n = 10000;		(* прогонов одной задачи *)
		batchN = 8;		(* задач в наборе *)
		rounds = 100;		(* пачек *)

	(* Задача нижнего уровня: пример про повторный Start одной задачи. *)
	TYPE
		RepeatTask = POINTER TO RECORD (Tasks.Task) runs: INTEGER END;

	VAR
		one: RepeatTask;
		batch: ARRAY batchN OF RepeatTask;
		k: INTEGER;

	PROCEDURE (t: RepeatTask) Run;
	BEGIN
		INC(t.runs)
	END Run;

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: LONGINT);
		VAR s: ARRAY 32 OF CHAR;
	BEGIN
		Console.WriteStr(what); Strings.IntToString(x, s);
		Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE Drain;
	BEGIN
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Drain;

	PROCEDURE Do*;
		VAR i: INTEGER; a0: LONGINT;
	BEGIN
		Tasks.Start(one); Drain;	(* прогрев *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO n DO Tasks.Start(one); Drain END;
		Say("single task runs=", one.runs);
		Say("single task allocations=", Kernel.AllocCount() - a0)
	END Do;

	PROCEDURE Batch*;
		VAR i: INTEGER; a0, total: LONGINT;
	BEGIN
		FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
		Drain;	(* прогрев всех стеков *)
		a0 := Kernel.AllocCount();
		FOR i := 1 TO rounds DO
			FOR k := 0 TO batchN - 1 DO Tasks.Start(batch[k]) END;
			Drain
		END;
		total := 0;
		FOR k := 0 TO batchN - 1 DO INC(total, batch[k].runs) END;
		Say("batch runs=", total);
		Say("batch allocations=", Kernel.AllocCount() - a0)
	END Batch;

BEGIN
	NEW(one);
	FOR k := 0 TO batchN - 1 DO NEW(batch[k]) END
END ObxRestart.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Restart.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxRestart.Do
```

В консоли:

```sh
echo 'ObxRestart.Do' | tools/run64.sh
```

В журнале появится:

```
single task runs=10001
single task allocations=0
```

Вторая команда запускает пачку:

```
   ObxRestart.Batch
```

```sh
echo 'ObxRestart.Batch' | tools/run64.sh
```

Она напечатает:

```
batch runs=808
batch allocations=0
```

## Что произошло

Задача `RepeatTask` — наследник `Tasks.Task` с методом `Run`, который только увеличивает счётчик
прогонов. Объект `one` создан в инициализации модуля, до первой команды. `Do` сначала запускает
задачу один раз и прокручивает планировщик до пустой очереди: это прогрев. Затем запоминает
`Kernel.AllocCount()` и запускает тот же объект ещё десять тысяч раз, каждый раз доводя очередь до
пустой. Счётчик показывает 10001: один прогрев и десять тысяч прогонов.

`Tasks.Start` для завершённой задачи не создаёт новый объект и не выделяет новый стек. Стек
завершённой задачи вернулся в пул; `Start` берёт его оттуда, а запись реестра стека привязывает
заново. Поэтому разница `Kernel.AllocCount()` после прогрева — ноль. Число выделений на прогон,
измеренное отдельно, — 196–207 нс против 507–523 нс, когда каждый раз создаётся новый объект
(`docs/monitor-object.md`, раздел 5).

`Batch` устроен так же, только задач восемь. Восемь прогонов прогревают все восемь стеков, затем
сто кругов запускают каждую задачу по разу и ждут пустой очереди. Всего 8 · 100 = 800 прогонов
плюс восемь прогревочных, отсюда 808. Выделений после прогрева снова ноль: восемь стеков остаются
в пуле и переиспользуются.

Повторный запуск работает и для задачи с пропуском, `Tasks.Job`: `Job.Run` заводит новый пропуск,
сбрасывает отмену и срок и зовёт `Body` заново. `ObxRestart` использует простую `Tasks.Task` без
пропуска, поэтому его прогон дешевле: нет ни пропуска, ни мониторов.

Отмена и срок относятся к одному запуску. Завершённая или отменённая задача запускается снова как
новая: `Run` не помнит отмену прошлого прогона. Счётчик `runs` — поле объекта, поэтому он и
накапливает 10001.

В этом примере мы увидели, что завершённую задачу можно запускать снова тем же объектом, что стек
берётся из пула и что после прогрева повторный запуск не выделяет памяти.
