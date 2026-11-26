# Обзор на примерах: DemoSelectTimeout

Выбор с тайм-аутом.

Задача ждёт сообщения, но не дольше заданного срока. В Go это `case <-time.After(d)` внутри
`select` (*Tour of Go*, «Select», <https://go.dev/tour/concurrency/5>; gobyexample «Timeouts»,
<https://gobyexample.com/timeouts>).

```
MODULE DemoSelectTimeout;
(** Select с тайм-аутом.

	Перенесено из Tour of Go ("Select", "Default Selection") и
	gobyexample "Timeouts": выбор между приёмом из канала и таймером.
	Источники:
	  https://go.dev/tour/concurrency/5
	  https://gobyexample.com/timeouts
	В Go тайм-аут — `case <-time.After(d)`. Здесь та же альтернатива
	AsyncChan.TimeoutAlt: Select возвращает индекс сработавшей.

	Команда DemoSelectTimeout.Run проверяет два случая: без отправителя
	побеждает тайм-аут, с отправителем — приём. Итог детерминирован
	по индексам и значению; времена проверяются с допуском. *)

	IMPORT Kernel, Tasks, AsyncSelect, AsyncChan, DemoSupport;

	TYPE
		IntMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Receiver = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan;
			timeout: LONGINT;
			idx, val: INTEGER;
			elapsed: LONGINT
		END;

		Sender = POINTER TO RECORD (Tasks.Job)
			c: AsyncChan.Chan; delay: LONGINT; val: INTEGER
		END;

	VAR
		TimeoutIdx*, GotIdx*: INTEGER;
		TimeoutElapsed*, GotElapsed*: LONGINT;
		GotValue*: INTEGER;
		Failed*: BOOLEAN;

	PROCEDURE (r: Receiver) Body- (VAR ctx: Tasks.Ctx);
		VAR ops: ARRAY 2 OF AsyncSelect.Op; m: AsyncSelect.Msg;
			t0: LONGINT;
	BEGIN
		t0 := Kernel.Time();
		AsyncChan.RecvAlt(r.c, ops[0]);
		AsyncChan.TimeoutAlt(r.timeout, ops[1]);
		r.idx := AsyncChan.Select(ops);
		r.elapsed := Kernel.Time() - t0;
		r.val := -1;
		IF r.idx = 0 THEN
			m := ops[0].got;
			WITH m: IntMsg DO r.val := m.n END
		END
	END Body;

	PROCEDURE (s: Sender) Body- (VAR ctx: Tasks.Ctx);
		VAR m: IntMsg;
	BEGIN
		Tasks.Sleep(s.delay);
		NEW(m); m.n := s.val;
		s.c.Send(m)
	END Body;

	PROCEDURE Run*;
	(** Демонстрация: тайм-аут 30 мс, затем отправитель через 5 мс. *)
		VAR c: AsyncChan.Chan; r: Receiver; s: Sender; ok: BOOLEAN;
	BEGIN
		Failed := FALSE; GotValue := -1;
		c := AsyncChan.NewChan();

		NEW(r); r.c := c; r.timeout := 30;
		Tasks.Start(r);
		ok := DemoSupport.Pump(5000);
		TimeoutIdx := r.idx; TimeoutElapsed := r.elapsed;
		IF Tasks.Failed(r) OR ~ok THEN Failed := TRUE END;

		NEW(s); s.c := c; s.delay := 5; s.val := 42;
		NEW(r); r.c := c; r.timeout := 500;
		Tasks.Start(r); Tasks.Start(s);
		ok := DemoSupport.Pump(5000);
		GotIdx := r.idx; GotElapsed := r.elapsed; GotValue := r.val;
		IF Tasks.Failed(r) OR Tasks.Failed(s) OR ~ok THEN Failed := TRUE END;

		DemoSupport.LogInt("DemoSelectTimeout: timeoutIdx=", TimeoutIdx);
		DemoSupport.LogInt(" timeoutMs=", TimeoutElapsed);
		DemoSupport.LogInt(" gotIdx=", GotIdx);
		DemoSupport.LogInt(" gotMs=", GotElapsed);
		DemoSupport.LogInt(" value=", GotValue);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoSelectTimeout.
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/SelectTimeout.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoSelectTimeout.Run
```

В консоли:

```sh
echo 'DemoSelectTimeout.Run' | tools/run64.sh
```

В журнале появится:

```
DemoSelectTimeout: timeoutIdx=1 timeoutMs=30 gotIdx=0 gotMs=5 value=42
```

## Что произошло

`AsyncChan.Select` принимает массив альтернатив и возвращает индекс сработавшей. Приём —
`AsyncChan.RecvAlt`, тайм-аут — `AsyncChan.TimeoutAlt`; принятое сообщение лежит в `ops[i].got`.
Команда проверяет два случая.

Сначала отправителя нет, и через 30 мс побеждает тайм-аут: индекс 1. Срок тайм-аута планировщик
задач не видит: таймер `Select` — действие `Services`, а не сон задачи. Получатель ждёт в
`Tasks.ParkExternal`, и планировщик тупиком такое ожидание не считает: таймер — событие извне задач,
он срабатывает и будит получателя. Затем отправитель приходит через 5 мс, и побеждает приём: индекс 0, значение 42.
Времена в строке — миллисекунды по `Kernel.Time`; на загруженной машине они могут быть больше на
несколько миллисекунд.

## Тест

`Demo_devExamples.TestProcSelectTimeout` проверяет индексы, значение 42 и то, что тайм-аут ждал не меньше 20 мс.

В этом примере мы увидели выбор между приёмом и тайм-аутом и ожидание, которое тупиком не считается,
когда единственный срок задаёт таймер `Select`.
