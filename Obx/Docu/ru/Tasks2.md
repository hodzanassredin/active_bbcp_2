# Обзор на примерах: ObxTasks2

Сервер и сто клиентов на мониторе модуля.

Сто клиентов присылают серверу запросы и ждут ответа. Каждый клиент — задача с прямым текстом:
положить запрос, дождаться, прибавить ответ. Общую очередь запросов защищают исключающие процедуры,
а ожидание записано условием `AwaitCond`. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxTasks2;
	IMPORT Tasks, Console, Strings;

	CONST clients = 100;

	TYPE
		Request = POINTER TO RECORD
			arg, res: INTEGER;
			done: BOOLEAN;
			next: Request
		END;

		SubmitOp = RECORD (Tasks.Op) r: Request; cond: Done END;
		TakeOp = RECORD (Tasks.Op) r: Request END;
		AnswerOp = RECORD (Tasks.Op) r: Request; res: INTEGER END;

		Done = POINTER TO RECORD (Tasks.Cond) r: Request END;

		Queue = POINTER TO RECORD (Tasks.Object)
			head, tail: Request
		END;

		Client = POINTER TO RECORD (Tasks.Job) q: Queue; n: INTEGER END;
		Server = POINTER TO RECORD (Tasks.Job) q: Queue END;

	VAR queue: Queue; sum: INTEGER;

	PROCEDURE (c: Done) Ready (o: Tasks.Object): BOOLEAN;
	BEGIN
		RETURN c.r.done
	END Ready;

	PROCEDURE (q: Queue) Ready* (): SET;
	BEGIN
		IF q.head # NIL THEN RETURN {0} ELSE RETURN {} END
	END Ready;

	PROCEDURE (q: Queue) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: SubmitOp DO
			op.r.next := NIL;
			IF q.tail = NIL THEN q.head := op.r ELSE q.tail.next := op.r END;
			q.tail := op.r;
			op.cond.r := op.r;
			IF ~q.AwaitCond(ctx, op.cond) THEN RETURN END
		| op: TakeOp DO
			IF ~q.Await(ctx, {0}) THEN op.r := NIL; RETURN END;
			op.r := q.head; q.head := op.r.next;
			IF q.head = NIL THEN q.tail := NIL END
		| op: AnswerOp DO
			op.r.res := op.res; op.r.done := TRUE
		ELSE
			HALT(20)
		END
	END Exclusive;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Request; cond: Done; op: SubmitOp;
	BEGIN
		NEW(r); r.arg := c.n; NEW(cond);
		op.r := r; op.cond := cond;
		c.q.Do(ctx, op);
		sum := sum + r.res
	END Body;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; take: TakeOp; answer: AnswerOp;
	BEGIN
		FOR i := 1 TO clients DO
			take.r := NIL; s.q.Do(ctx, take);
			answer.r := take.r; answer.res := take.r.arg * take.r.arg;
			s.q.Do(ctx, answer)
		END
	END Body;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; NEW(queue);
		NEW(s); s.q := queue; Tasks.Start(s);
		FOR i := 1 TO clients DO
			NEW(c); c.q := queue; c.n := i; Tasks.Start(c)
		END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(sum, str);
		Console.WriteStr("sum = "); Console.WriteStr(str); Console.WriteLn
	END Do;

END ObxTasks2.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Tasks2.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxTasks2.Do
```

В консоли:

```sh
echo 'ObxTasks2.Do' | tools/run64.sh
```

В журнале появится:

```
sum = 338350
```

## Что произошло

Команда запускает сервер и сто клиентов. Клиент вызывает исключающую процедуру `Submit`: она ставит
запрос в конец очереди и ждёт `AwaitCond` с условием `Done` для своей заявки. Пока условие ложно, клиент отпускает монитор
модуля и спит; монитор берёт следующий клиент или сервер.

Сервер в цикле вызывает `Take`. Если очередь пуста, `AWAIT(head # NIL)` усыпляет его; когда клиент
выходит из монитора, условия ждущих вычисляются заново, и сервер просыпается. `Answer` записывает
ответ и ставит `r.done`. Условие `r.done` ссылается на параметр клиента и вычисляется на кадре
ждущего клиента, поэтому просыпается ровно тот клиент, чей запрос исполнен.

Строка `sum := sum + r.res` стоит вне монитора: между чтением и записью `sum` нет точки
переключения. Итог — сумма квадратов от 1 до 100.

В этом примере мы увидели операции общего монитора-объекта, ожидание условия в `AwaitCond` с
параметром ждущего и то, что сто задач-клиентов записываются тем же прямым текстом, что и одна.
