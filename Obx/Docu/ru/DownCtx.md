# Обзор на примерах: ObxDownCtx

Скачать и сохранить: родитель, дети и срок.

Родительская задача запускает две дочерние: загрузчик кладёт куски в буфер, сохранятель их берёт.
Обе дочерние задачи — `Job`, у каждой свой пропуск. Родитель ждёт `JoinAll`. В первой
фазе срок `SetDeadline` кончается до конца потока кусков, и родитель отменяет детей, сообщая, что
результат неполный. Во второй фазе детей отменяет отдельная задача явно; состояния детей проверяются
после `JoinAll`. Пример показывает `Spawn`, `JoinAll`, срок и отмену. Общие понятия пропуска и
операции разобраны в `Obx/Docu/ru/BufCtx.md`; пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxDownCtx;
(** Скачать и сохранить: родитель, дети и срок.

	Родительская задача с пропуском запускает через Tasks.Spawn две
	дочерние: загрузчик кладёт куски в буфер, сохранятель их берёт. Обе —
	тоже задачи с пропуском, у каждой свой Ctx. Родитель ждёт JoinAll.
	Первая фаза кончается сроком SetDeadline: родитель отменяет детей и
	сообщает, что результат неполный. Вторая фаза отменяет загрузчика и
	сохранителя явно; состояния детей проверяются после JoinAll. *)

	IMPORT Tasks, Console;

	CONST
		cap = 3;
		total = 20;		(* кусков в источнике *)
		chunkMs = 5;		(* задержка источника на кусок *)
		deadlineMs = 40;

	TYPE
		PutOp = RECORD (Tasks.Op) seq: INTEGER END;
		GetOp = RECORD (Tasks.Op) seq: INTEGER; ok: BOOLEAN END;

		Pipe = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF INTEGER;
			head, tail, count: INTEGER
		END;

		Downloader = POINTER TO RECORD (Tasks.Job) pipe: Pipe END;
		Saver = POINTER TO RECORD (Tasks.Job) pipe: Pipe; saved: INTEGER END;
		DeadlineParent = POINTER TO RECORD (Tasks.Job) pipe: Pipe; sv: Saver END;
		CancelParent = POINTER TO RECORD (Tasks.Job) pipe: Pipe END;
		Killer = POINTER TO RECORD (Tasks.Job) dl, sv: Tasks.Job END;

	VAR
		deadlineExpired, deadlinePartial: BOOLEAN;
		cancelDl, cancelSv, cancelPartial: BOOLEAN;

	PROCEDURE SayBool (IN what: ARRAY OF CHAR; yes: BOOLEAN);
	BEGIN
		Console.WriteStr(what);
		IF yes THEN Console.WriteStr("yes") ELSE Console.WriteStr("no") END;
		Console.WriteLn
	END SayBool;

	PROCEDURE (p: Pipe) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF p.count < cap THEN INCL(s, 0) END;
		IF p.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (p: Pipe) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: PutOp DO
			IF ~p.Await(ctx, {0}) THEN RETURN END;
			p.ring[p.tail] := op.seq;
			p.tail := (p.tail + 1) MOD cap; INC(p.count)
		| op: GetOp DO
			IF ~p.Await(ctx, {1}) THEN op.ok := FALSE; RETURN END;
			op.seq := p.ring[p.head];
			p.head := (p.head + 1) MOD cap; DEC(p.count); op.ok := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (d: Downloader) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: PutOp;
	BEGIN
		FOR i := 1 TO total DO
			Tasks.Sleep(chunkMs);		(* имитация источника *)
			IF ctx.Stopped() THEN RETURN END;
			op.seq := i; d.pipe.Do(ctx, op)
		END
	END Body;

	PROCEDURE (s: Saver) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: GetOp;
	BEGIN
		i := 0;
		LOOP
			IF (i >= total) OR ctx.Stopped() THEN EXIT END;
			op.ok := FALSE;
			s.pipe.Do(ctx, op);
			IF ~op.ok THEN EXIT END;
			INC(i)
		END;
		s.saved := i
	END Body;

	PROCEDURE (p: DeadlineParent) Body- (VAR ctx: Tasks.Ctx);
		VAR dl: Downloader; sv: Saver;
	BEGIN
		NEW(dl); dl.pipe := p.pipe;
		NEW(sv); sv.pipe := p.pipe;
		p.sv := sv;
		Tasks.SetDeadline(ctx, deadlineMs);
		Tasks.Spawn(ctx, dl);
		Tasks.Spawn(ctx, sv);
		Tasks.JoinAll(ctx);
		deadlineExpired := ctx.expired;
		deadlinePartial := sv.saved < total
	END Body;

	PROCEDURE (k: Killer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(15);
		Tasks.Cancel(k.dl);
		Tasks.Cancel(k.sv)
	END Body;

	PROCEDURE (p: CancelParent) Body- (VAR ctx: Tasks.Ctx);
		VAR dl: Downloader; sv: Saver; k: Killer;
	BEGIN
		NEW(dl); dl.pipe := p.pipe;
		NEW(sv); sv.pipe := p.pipe;
		NEW(k); k.dl := dl; k.sv := sv;
		Tasks.Spawn(ctx, dl);
		Tasks.Spawn(ctx, sv);
		Tasks.Spawn(ctx, k);
		Tasks.JoinAll(ctx);
		cancelDl := Tasks.State(dl) = Tasks.Cancelled;
		cancelSv := Tasks.State(sv) = Tasks.Cancelled;
		cancelPartial := sv.saved < total
	END Body;

	PROCEDURE Do*;
		VAR pipe: Pipe; p1: DeadlineParent; p2: CancelParent;
	BEGIN
		NEW(pipe); pipe.head := 0; pipe.tail := 0; pipe.count := 0;
		NEW(p1); p1.pipe := pipe;
		Tasks.Start(p1);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		NEW(pipe); pipe.head := 0; pipe.tail := 0; pipe.count := 0;
		NEW(p2); p2.pipe := pipe;
		Tasks.Start(p2);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;

		SayBool("deadline expired: ", deadlineExpired);
		SayBool("deadline partial: ", deadlinePartial);
		SayBool("cancel downloader: ", cancelDl);
		SayBool("cancel saver: ", cancelSv);
		SayBool("cancel partial: ", cancelPartial)
	END Do;

END ObxDownCtx.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/DownCtx.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxDownCtx.Do
```

В консоли:

```sh
echo 'ObxDownCtx.Do' | tools/run64.sh
```

В журнале появится:

```
Tasks: cancelled
Tasks: cancelled
deadline expired: yes
deadline partial: yes
cancel downloader: yes
cancel saver: yes
cancel partial: yes
```

## Что произошло

Труба — монитор-объект `Pipe` с кольцом на три места; `PutOp` кладёт кусок, `GetOp` берёт. Обе
операции ждут флага в `Await`: загрузчик ждёт `{0}` («есть место»), сохранятель — `{1}` («есть
предмет»). Это тот же монитор-объект, что в `ObxBufCtx`, только с одним производителем и одним
потребителем.

Родитель — задача `Job`; её тело получает пропуск по `VAR`. `SetDeadline(ctx, 40)` ставит срок на
этот пропуск. `Spawn(ctx, dl)` запускает дочернюю задачу и вносит её в группу пропуска; то же
происходит с сохранятелем. У каждой дочерней задачи `Job.Run` заводит свой пропуск, а группа
связывает детей с пропуском родителя. `JoinAll(ctx)` ждёт, пока группа опустеет. Когда срок
родителя истекает, `JoinAll` отменяет группу и ждёт, пока дети завершатся.

Загрузчик спит 5 мс на кусок и должен положить двадцать кусков, то есть работать около 100 мс.
Срок в 40 мс кончается до завершения работы, поэтому в первой фазе `deadlineExpired` истинно, а
`deadlinePartial` истинно: сохранятель успел взять меньше двадцати кусков. Перед каждой отправкой
загрузчик проверяет `ctx.Stopped()` и при остановке выходит из цикла. Сохранятель проверяет то же в
своём цикле. Остановка видна и в ожидании: `Await` возвращает `FALSE`, операция не исполняется, и
`Exclusive` выходит.

Строки `Tasks: cancelled` печатает сам модуль `Tasks`, когда отменённая задача просыпается внутри
`Tasks.Sleep`. Здесь их две: загрузчик спит в `Sleep` в каждой из двух фаз, и обе отмены прерывают
его сон трапом 127. Сохранятель в это время ждёт в `Await`; там отмена возвращает `FALSE` без
строки в журнале. Задача, отменённая во время ожидания, заканчивает состояние `Cancelled`, даже
если тело вышло обычным `RETURN`.

Вторая фаза устроена иначе. `CancelParent` запускает загрузчика, сохранятеля и `Killer` — обычную
задачу `Tasks.Job` с методом `Body`. `Killer` спит 15 мс и зовёт `Tasks.Cancel` для обеих дочерних
задач. `Cancel` ставит флаг и будит задачу; следующая точка переключения замечает флаг. После
`JoinAll` родитель читает `Tasks.State`: обе задачи в состоянии `Cancelled`, а сохранятель успел
взять меньше двадцати кусков. Отсюда три ответа `yes` во второй группе.

Проверка состояния возможна потому, что объект задачи живёт дальше: после завершения планировщик
снимает задачу со счёта и возвращает стек в пул, но сам объект остаётся у программы. Поэтому
`Tasks.State(dl)` и `sv.saved` читаются после `JoinAll`.

Без срока и без явной отмены загрузчик положил бы все двадцать кусков, сохранятель взял бы их, а
`JoinAll` вернулся бы после завершения обеих задач; тогда `expired` и `partial` были бы ложны.

В этом примере мы увидели, как родительский пропуск запускает детей через `Spawn`, как `JoinAll`
ждёт группу, как срок останавливает весь пропуск и как отмена и срок отражаются в состояниях
дочерних задач.
