# Обзор на примерах: ObxDownChan

Скачать и сохранить на канале с пропуском.

Задача та же, что в `ObxDownCtx`: загрузчик кладёт двадцать кусков, сохранятель их берёт, срок
отменяет работу, вторая фаза отменяет детей явно. Разница в трубе: здесь это
`AsyncChan.Buffer(3)`, а не монитор-объект с флагами. Загрузчик зовёт `SendCtx`, сохранятель —
`RecvCtx`; обе процедуры возвращают `FALSE`, когда пропуск остановлен, и задача сворачивается без
трапа. В коде нет ни записей `Op`, ни `Ready`, ни `Await`: очередь и ожидание взял на себя канал.
Общие понятия пропуска разобраны в `Obx/Docu/ru/BufCtx.md`, мониторная версия — в
`Obx/Docu/ru/DownCtx.md`; пример относится к руководству `Docu/ru/Async.md`.

```
MODULE ObxDownChan;
(** Скачать и сохранить на канале с пропуском.

	То же, что ObxDownCtx, но труба — AsyncChan.Buffer(3), а не
	монитор-объект с флагами. Загрузчик кладёт куски через SendCtx,
	сохранятель берёт их через RecvCtx; остановка возвращает FALSE и
	задача сворачивается. Родитель через Spawn и срок SetDeadline
	отменяет группу, когда срок кончился; вторая фаза отменяет детей явно.

	Сравнение кода: здесь нет ни Op-записей, ни Ready, ни Await — канал
	берёт очередь и ожидание на себя. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, Console, Strings;

	CONST
		cap = 3;
		total = 20;
		chunkMs = 5;
		deadlineMs = 40;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		Downloader = POINTER TO RECORD (Tasks.Job)
			out: AsyncChan.Buffer; sent: INTEGER; stopped: BOOLEAN
		END;

		Saver = POINTER TO RECORD (Tasks.Job)
			in: AsyncChan.Buffer; saved: INTEGER; stopped: BOOLEAN
		END;

		DeadlineParent = POINTER TO RECORD (Tasks.Job)
			pipe: AsyncChan.Buffer; dl: Downloader; sv: Saver; expired: BOOLEAN
		END;

		CancelParent = POINTER TO RECORD (Tasks.Job)
			pipe: AsyncChan.Buffer; dl: Downloader; sv: Saver
		END;

		Killer = POINTER TO RECORD (Tasks.Job) dl, sv: Tasks.Job END;

	VAR
		deadlineExpired, deadlinePartial: BOOLEAN;
		cancelDl, cancelSv, cancelPartial: BOOLEAN;

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

	PROCEDURE SayBool (IN what: ARRAY OF CHAR; yes: BOOLEAN);
	BEGIN
		Console.WriteStr(what);
		IF yes THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END;
		Console.WriteLn
	END SayBool;

	PROCEDURE (d: Downloader) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		i := 0;
		LOOP
			IF i >= total THEN EXIT END;
			INC(i);
			Tasks.Sleep(chunkMs);		(* имитация источника *)
			IF ctx.Stopped() THEN EXIT END;
			IF ~d.out.SendCtx(ctx, NewNum(i)) THEN EXIT END;
			d.sent := i
		END;
		d.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (s: Saver) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		LOOP
			IF (s.saved >= total) OR ctx.Stopped() THEN EXIT END;
			IF ~s.in.RecvCtx(ctx, m) THEN EXIT END;
			n := GetNum(m);
			IF n > 0 THEN INC(s.saved) END
		END;
		s.stopped := ctx.Stopped()
	END Body;

	PROCEDURE (p: DeadlineParent) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, p.dl);
		Tasks.Spawn(ctx, p.sv);
		Tasks.JoinAll(ctx);
		p.expired := ctx.expired
	END Body;

	PROCEDURE (k: Killer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(15);
		Tasks.Cancel(k.dl);
		Tasks.Cancel(k.sv)
	END Body;

	PROCEDURE (p: CancelParent) Body- (VAR ctx: Tasks.Ctx);
		VAR k: Killer;
	BEGIN
		NEW(k); k.dl := p.dl; k.sv := p.sv;
		Tasks.Spawn(ctx, p.dl);
		Tasks.Spawn(ctx, p.sv);
		Tasks.Spawn(ctx, k);
		Tasks.JoinAll(ctx)
	END Body;

	PROCEDURE Do*;
		VAR p1: DeadlineParent; p2: CancelParent;
	BEGIN
		NEW(p1); p1.pipe := AsyncChan.NewBuffer(cap);
		NEW(p1.dl); p1.dl.out := p1.pipe;
		NEW(p1.sv); p1.sv.in := p1.pipe;
		Tasks.Start(p1);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		deadlineExpired := p1.expired;
		deadlinePartial := p1.sv.saved < total;

		NEW(p2); p2.pipe := AsyncChan.NewBuffer(cap);
		NEW(p2.dl); p2.dl.out := p2.pipe;
		NEW(p2.sv); p2.sv.in := p2.pipe;
		Tasks.Start(p2);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		cancelDl := Tasks.State(p2.dl) = Tasks.Cancelled;
		cancelSv := Tasks.State(p2.sv) = Tasks.Cancelled;
		cancelPartial := p2.sv.saved < total;

		SayBool("channel deadline expired: ", deadlineExpired);
		SayBool("channel deadline partial: ", deadlinePartial);
		SayBool("channel cancel downloader: ", cancelDl);
		SayBool("channel cancel saver: ", cancelSv);
		SayBool("channel cancel partial: ", cancelPartial)
	END Do;

END ObxDownChan.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/DownChan.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxDownChan.Do
```

В консоли:

```sh
echo 'ObxDownChan.Do' | tools/run64.sh
```

В журнале появится:

```
Tasks: cancelled
Tasks: cancelled
channel deadline expired: yes
channel deadline partial: yes
channel cancel downloader: yes
channel cancel saver: yes
channel cancel partial: yes
```

## Что произошло

Канал `AsyncChan.Buffer` держит до трёх сообщений. `SendCtx` в неполный буфер кладёт сообщение и
возвращается сразу; в полный — ждёт, пока сохранятель освободит место. `RecvCtx` из непустого
буфера берёт первое сообщение; из пустого — ждёт. Обе процедуры получают пропуск и умеют
прерваться: при остановке `SendCtx` не отправляет сообщение и возвращает `FALSE`, `RecvCtx`
возвращает `FALSE` и `NIL`. Внутри обе делают одно предложение выбора и зовут `SelectCtx`; при
остановке `SelectCtx` снимает предложение и возвращает −1.

Первая фаза повторяет расписание `ObxDownCtx`. Родитель ставит срок 40 мс, запускает загрузчика и
сохранятеля через `Spawn` и ждёт `JoinAll`. Загрузчик спит 5 мс на кусок и не успевает положить
двадцать кусков: срок кончается, `JoinAll` отменяет группу, и обе задачи выходят по `FALSE` от
`SendCtx` и `RecvCtx`. Отсюда `channel deadline expired: yes` и `channel deadline partial: yes`.

Вторая фаза запускает ещё и `Killer` — обычную задачу `Tasks.Job` с методом `Body`. Через 15 мс она зовёт
`Tasks.Cancel` для загрузчика и сохранятеля. Отмена ставит флаг и будит задачу; канал видит
остановку в ближайшей точке переключения, снимает предложение и возвращает `FALSE`. После `JoinAll`
родитель проверяет `Tasks.State`: обе задачи в состоянии `Cancelled`, сохранятель взял меньше
двадцати кусков. Отсюда три ответа `yes` во второй группе.

Строки `Tasks: cancelled` печатает модуль `Tasks`, когда отменённая задача просыпается внутри
`Tasks.Sleep`. Здесь их две: загрузчик спит в каждой из двух фаз, и обе отмены прерывают сон трапом
127. Сохранятель ждёт в `RecvCtx`, где остановка возвращает `FALSE` без строки в журнале.

Сравнение с `ObxDownCtx` видно по коду. Там труба — объект с двумя операциями и `Ready`, а
ожидание — `Await`; здесь труба — канал, а ожидание спрятано в `SendCtx` и `RecvCtx`. Вызывающий
пишет одинаково коротко, но владелец состояния разный: в первом случае его пишет автор объекта, во
втором — библиотека каналов.

В этом примере мы увидели, как те же родитель, дети, срок и отмена записываются на канале с
пропуском и чем канал отличается от монитора-объекта по объёму кода вызывающего.
