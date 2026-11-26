# Обзор на примерах: ObxPipeCtx

Конвейер из трёх стадий на каналах с пропуском.

Источник шлёт числа во встречу a, обработчик берёт их из a и шлёт удвоенными во встречу b, приёмник
считает. Все три стадии — задачи `Job` с пропуском. Родитель запускает их через `Spawn` и ставит
срок `SetDeadline`; срок кончается до конца потока чисел, и `JoinAll` отменяет группу. Каждая
операция `SendCtx` и `RecvCtx` возвращает `FALSE`, когда пропуск остановлен, и стадия выходит из
цикла без трапа. Пример показывает каналы с пропуском и то, как срок сворачивает конвейер целиком.
Общие понятия пропуска разобраны в `Obx/Docu/ru/BufCtx.md`, канал с пропуском — в
`Obx/Docu/ru/DownChan.md`; пример относится к руководству `Docu/ru/Async.md`.

```
MODULE ObxPipeCtx;
(** Конвейер из трёх стадий на каналах с пропуском.

	Источник шлёт числа во встречу a, обработчик берёт их из a и шлёт
	удвоенными во встречу b, приёмник считает. Все три — задачи с
	пропуском; каждая операция SendCtx/RecvCtx возвращает FALSE, когда
	пропуск остановлен, и стадия сворачивается, не трапуя.

	Родитель запускает стадии через Spawn и ставит срок SetDeadline.
	Срок кончается раньше, чем поток чисел: JoinAll отменяет группу, и все
	стадии выходят по остановке. Output — счётчики и признаки. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, Console, Strings;

	CONST
		total = 1000000;	(* заведомо больше, чем успеет пройти *)
		deadlineMs = 30;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Source = POINTER TO RECORD (Tasks.Job)
			out: AsyncChan.Chan; sent: INTEGER; stopped: BOOLEAN
		END;

		Worker = POINTER TO RECORD (Tasks.Job)
			in, out: AsyncChan.Chan; done: INTEGER; stopped: BOOLEAN
		END;

		Sink = POINTER TO RECORD (Tasks.Job)
			in: AsyncChan.Chan; got: INTEGER; stopped: BOOLEAN
		END;

		Parent = POINTER TO RECORD (Tasks.Job)
			a, b: AsyncChan.Chan; src: Source; wrk: Worker; snk: Sink;
			expired: BOOLEAN
		END;

	VAR
		sent, done, got: INTEGER;
		stoppedSrc, stoppedWrk, stoppedSnk, parentExpired: BOOLEAN;

	PROCEDURE NewNum (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewNum;

	PROCEDURE GetNum (m: AsyncSelect.Msg): INTEGER;
	BEGIN
		IF m = NIL THEN RETURN -1 END;
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END GetNum;

	PROCEDURE SayInt (x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s); Console.WriteStr(s)
	END SayInt;

	PROCEDURE SayBool (b: BOOLEAN);
	BEGIN
		IF b THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END
	END SayBool;

	PROCEDURE (s: Source) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		LOOP
			IF (i >= total) OR ctx.Stopped() THEN EXIT END;
			INC(i);
			IF ~s.out.SendCtx(ctx, NewNum(i)) THEN EXIT END;
			s.sent := i
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (w: Worker) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			IF ~w.in.RecvCtx(ctx, m) THEN EXIT END;
			n := GetNum(m);
			IF n < 0 THEN EXIT END;
			IF ~w.out.SendCtx(ctx, NewNum(2 * n)) THEN EXIT END;
			INC(w.done)
		END;
		w.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (s: Sink) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			IF ~s.in.RecvCtx(ctx, m) THEN EXIT END;
			IF GetNum(m) < 0 THEN EXIT END;
			INC(s.got)
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (p: Parent) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, p.src);
		Tasks.Spawn(ctx, p.wrk);
		Tasks.Spawn(ctx, p.snk);
		Tasks.JoinAll(ctx);	(* срок кончился — группа отменена и доведена *)
		p.expired := ctx.expired
	END Body;

	PROCEDURE Do*;
		VAR p: Parent;
	BEGIN
		NEW(p);
		p.a := AsyncChan.NewChan(); p.b := AsyncChan.NewChan();
		NEW(p.src); p.src.out := p.a;
		NEW(p.wrk); p.wrk.in := p.a; p.wrk.out := p.b;
		NEW(p.snk); p.snk.in := p.b;
		Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		sent := p.src.sent; done := p.wrk.done; got := p.snk.got;
		stoppedSrc := p.src.stopped; stoppedWrk := p.wrk.stopped;
		stoppedSnk := p.snk.stopped; parentExpired := p.expired;

		Console.WriteStr("pipeline sent="); SayInt(sent);
		Console.WriteStr(" processed="); SayInt(done);
		Console.WriteStr(" received="); SayInt(got); Console.WriteLn;
		Console.WriteStr("pipeline moved: "); SayBool(sent > 0); Console.WriteLn;
		Console.WriteStr("pipeline balanced: "); SayBool((done = got) OR (done = got + 1)); Console.WriteLn;
		Console.WriteStr("pipeline stopped: "); SayBool(stoppedSrc); Console.WriteStr(" ");
		SayBool(stoppedWrk); Console.WriteStr(" "); SayBool(stoppedSnk); Console.WriteLn;
		Console.WriteStr("pipeline deadline: "); SayBool(parentExpired); Console.WriteLn
	END Do;

END ObxPipeCtx.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/PipeCtx.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxPipeCtx.Do
```

В консоли тот же пример запускается командой:

```
echo 'ObxPipeCtx.Do' | tools/run64.sh
```

Число прошедших чисел зависит от времени и от планировщика, поэтому два прогона не дают
одинаковых счётчиков. `N` ниже означает неотрицательное целое.

В журнале появится:

<!-- check-examples: numbers-vary -->
```
pipeline sent=N processed=N received=N
pipeline moved: yes
pipeline balanced: yes
pipeline stopped: yes yes yes
pipeline deadline: yes
```

Строки после счётчиков устойчивы: конвейер движется, остаётся сбалансированным, все три стадии
останавливаются, срок истекает.

## Что произошло

Обе встречи — `AsyncChan.NewChan()`, канал без хранения сообщений: `SendCtx` отдаёт сообщение
только тогда, когда его забирает получатель, и до этого ждёт. Источник шлёт числа 1, 2, 3 и так
далее до миллиона; обработчик принимает число, удваивает и шлёт дальше; приёмник считает принятые.
Все три задачи спят и просыпаются на канале, поэтому процессор они отдают только в точках
переключения.

Родитель ставит срок 30 мс и запускает три стадии в свою группу. `JoinAll` ждёт, пока группа
опустеет. По истечении срока пропуск родителя становится просроченным, `JoinAll` отменяет группу,
и каждая стадия получает `FALSE` от ближайшего `SendCtx` или `RecvCtx`. Предложения, которые стадии
уже успели поставить в канал, снимаются; задача выходит из цикла и записывает `ctx.Stopped()` в своё
поле `stopped`. Поэтому все три ответа в строке `pipeline stopped` — `yes`, а `pipeline deadline`
подтверждает, что срок родителя истёк.

Счётчики `sent`, `done` и `got` считают звенья одной цепи. Источник увеличивает `sent` после
успешной отправки. Обработчик увеличивает `done` после успешной отправки в b. Приёмник увеличивает
`got` после приёма. Между отправкой и приёмом есть точка переключения, поэтому `done` и `got`
расходятся не больше чем на единицу: либо приёмник успел принять и посчитать, либо ещё нет. Отсюда
`pipeline balanced: yes` при условии `done = got` или `done = got + 1`. Сами числа зависят от
машины и от того, сколько стадии успели за 30 мс; устойчивы признаки, а не числа.

Отмена и срок здесь не трапуют. `SendCtx` и `RecvCtx` устроены как одно предложение выбора:
`AsyncSelect.SelectCtx` проверяет остановку до сна и возвращает −1, если решения не было. Тогда
операция возвращает `FALSE`, и стадия сама решает, что делать. Так же устроен `Await` у
монитора-объекта: `FALSE` — задача остановлена. Разница в том, что канал снимает своё предложение и
не оставляет очереди, а `Await` возвращает монитор задаче.

Повторный запуск стадий здесь не показан: каждую задачу создают один раз командой `Do`. Повторный
запуск того же объекта разобран в `ObxRestart`.

В этом примере мы увидели конвейер из трёх задач на каналах с пропуском, срок, который сворачивает
всю группу, и то, как остановка возвращается из `SendCtx` и `RecvCtx` значением `FALSE`.
