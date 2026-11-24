# Обзор на примерах: ObxTasks2

Сервер и сто клиентов на мониторе модуля.

Сто клиентов присылают серверу запросы и ждут ответа. Каждый клиент — задача с прямым текстом:
положить запрос, дождаться, прибавить ответ. Общую очередь запросов защищают исключающие процедуры,
а ожидание записано условием `ACTIVE.AWAIT`. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxTasks2;
	IMPORT ACTIVE, Tasks, Console, Strings;

	CONST clients = 100;

	TYPE
		Request = POINTER TO RECORD
			arg, res: INTEGER;
			done: BOOLEAN;
			next: Request
		END;
		Client = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Server = POINTER TO RECORD (Tasks.Task) END;

	VAR head, tail: Request; sum: INTEGER;

	PROCEDURE [exclusive] Submit (r: Request);
	BEGIN
		r.next := NIL;
		IF tail = NIL THEN head := r ELSE tail.next := r END;
		tail := r;
		ACTIVE.AWAIT(r.done)
	END Submit;

	PROCEDURE [exclusive] Take (VAR r: Request);
	BEGIN
		ACTIVE.AWAIT(head # NIL);
		r := head; head := r.next;
		IF head = NIL THEN tail := NIL END
	END Take;

	PROCEDURE [exclusive] Answer (r: Request; res: INTEGER);
	BEGIN
		r.res := res; r.done := TRUE
	END Answer;

	PROCEDURE (c: Client) Run;
		VAR r: Request;
	BEGIN
		NEW(r); r.arg := c.n;
		Submit(r);
		sum := sum + r.res
	END Run;

	PROCEDURE (s: Server) Run;
		VAR i: INTEGER; r: Request;
	BEGIN
		FOR i := 1 TO clients DO Take(r); Answer(r, r.arg * r.arg) END
	END Run;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; head := NIL; tail := NIL;
		NEW(s); Tasks.Start(s);
		FOR i := 1 TO clients DO NEW(c); c.n := i; Tasks.Start(c) END;
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
запрос в конец очереди и ждёт `ACTIVE.AWAIT(r.done)`. Пока условие ложно, клиент отпускает монитор
модуля и спит; монитор берёт следующий клиент или сервер.

Сервер в цикле вызывает `Take`. Если очередь пуста, `AWAIT(head # NIL)` усыпляет его; когда клиент
выходит из монитора, условия ждущих вычисляются заново, и сервер просыпается. `Answer` записывает
ответ и ставит `r.done`. Условие `r.done` ссылается на параметр клиента и вычисляется на кадре
ждущего клиента, поэтому просыпается ровно тот клиент, чей запрос исполнен.

Строка `sum := sum + r.res` стоит вне монитора: между чтением и записью `sum` нет точки
переключения. Итог — сумма квадратов от 1 до 100.

В этом примере мы увидели исключающие процедуры модуля, ожидание условия в `ACTIVE.AWAIT` с
параметром ждущего и то, что сто задач-клиентов записываются тем же прямым текстом, что и одна.
