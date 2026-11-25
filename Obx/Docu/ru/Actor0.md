# Обзор на примерах: ObxActor0

Первый актор: `Spawn`, `Tell`, `Ask`, `Respond` и общая остановка `ActorRun.Shutdown`.

Счётчик — актор с одним полем. Клиент шлёт ему тысячу писем `Inc`, потом спрашивает итог письмом
`Get`. Состояние счётчика меняет только его обработчик, поэтому монитор не нужен. Пример относится к
руководству `Actor/Docu/ru/Actors.md` (разделы 2 и 3).

```
MODULE ObxActor0;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Num = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR counter: ActorCore.Ref;

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: Num;
	BEGIN
		WITH m: Inc DO INC(a.n)
		| m: Get DO NEW(r); r.n := a.n; c.Respond(r)
		ELSE	(* Started и прочие сообщения жизненного цикла *)
		END
	END Receive;

	PROCEDURE New (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END New;

	PROCEDURE (t: Client) Run;
		VAR i: INTEGER; inc: Inc; get: Get; r: ActorCore.Msg; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(inc); NEW(get);
		FOR i := 1 TO 1000 DO ActorCore.Tell(counter, inc) END;
		IF ActorCore.Ask(counter, get, 1000, r) = ActorCore.ok THEN
			Strings.IntToString(r(Num).n, s); Console.WriteStr("counter: "); Console.WriteStr(s)
		ELSE Console.WriteStr("counter: no answer")
		END;
		Console.WriteLn;
		ActorRun.Shutdown
	END Run;

	PROCEDURE Do*;
		VAR t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(New), "counter");
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor0.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Actor0.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxActor0.Do
```

В консоли:

```sh
echo 'ObxActor0.Do' | tools/run64.sh
```

В журнале появится:

```
counter: 1000
```

## Что произошло

Команда запускает пул из четырёх исполнителей (`ActorRun.Start`), если он ещё не запущен, и создаёт
актор: `Spawn` вызывает фабрику `New`, заводит ящик с именем `counter` и возвращает адрес. Первым
письмом обработчик получит `Started`; счётчик его пропускает в ветке `ELSE`.

Клиент — задача, потому что `Ask` ждёт ответа, а ждать можно только в задаче. `Tell` кладёт письмо в
ящик и возвращается сразу; тысяча `Inc` ложится в ящик раньше, чем исполнитель берёт первое из них.
Клиент посылает один и тот же объект `inc` тысячу раз: сообщение после отправки не меняется, поэтому
это допустимо.

`Ask` посылает `Get` с временным адресом ответа и ждёт первого письма на этот адрес не дольше
1000 мс. Письма одного отправителя одному получателю приходят по порядку, поэтому `Get`
обрабатывается после всех `Inc`. Обработчик отвечает через `c.Respond`: ответ уходит отправителю
текущего письма, то есть адресу ответа клиента. Клиент печатает `counter: 1000`.

Последним клиент зовёт `ActorRun.Shutdown`. Она посылает `Stop` каждому актору без родителя, ждёт,
пока все акторы остановятся, и завершает задачи пула без отмены. Команда крутит главный цикл, пока
есть задачи (`Tasks.Count() > 0`), и возвращается. Без `Shutdown` исполнители остались бы в
`Tasks.Park`, цикл команды не кончился бы, а планировщик напечатал бы `Tasks: deadlock`.

В этом примере мы увидели, как создать актор, послать ему письма, спросить его и остановить всё
одним вызовом.
