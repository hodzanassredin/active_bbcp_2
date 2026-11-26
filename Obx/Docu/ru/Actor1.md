# Обзор на примерах: ObxActor1

Надзор: трап в обработчике, директива `Restart` и потеря состояния при перезапуске.

Рабочий актор считает письма и трапует на письме `Boom`. Его родитель — супервизор со стратегией
`OneForOne` и директивой `Restart`. Пример показывает, что перезапуск создаёт экземпляр с начальным
состоянием. Пример относится к руководству `Actor/Docu/ru/Actors.md` (раздел 6).

```
MODULE ObxActor1;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;
		Client = POINTER TO RECORD (Tasks.Job) END;

	VAR worker: ActorCore.Ref; starts, last: INTEGER;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO INC(starts)
		| m: Inc DO INC(a.n); last := a.n
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); RETURN a
	END NewWorker;

	PROCEDURE (a: Parent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS ActorCore.Started THEN
			worker := ActorCore.Spawn(c, ActorCore.NewProps(NewWorker), "worker")
		END
	END Receive;

	PROCEDURE NewParent (): ActorCore.Actor;
		VAR a: Parent;
	BEGIN
		NEW(a); RETURN a
	END NewParent;

	PROCEDURE (t: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR inc: Inc; boom: Boom; s: ARRAY 16 OF CHAR;
	BEGIN
		WHILE worker = NIL DO Tasks.Sleep(1) END;
		NEW(inc); NEW(boom);
		ActorCore.Tell(worker, inc); ActorCore.Tell(worker, inc);
		ActorCore.Tell(worker, boom);
		ActorCore.Tell(worker, inc);
		Tasks.Sleep(100);
		Strings.IntToString(starts, s); Console.WriteStr("worker: Started "); Console.WriteStr(s);
		Strings.IntToString(last, s); Console.WriteStr(", count after restart "); Console.WriteStr(s);
		Console.WriteLn;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR p: ActorCore.Props; t: Client;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		ASSERT(ActorCore.Spawn(NIL, p, "parent") # NIL, 100);
		NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor1.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Actor1.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxActor1.Do
```

В консоли:

```sh
echo 'ObxActor1.Do' | tools/run64.sh
```

В журнале появится:

```
worker: Started 2, count after restart 1
```

## Что произошло

Команда создаёт родителя с именем `parent` и стратегией `ActorSup.NewOneForOne(ActorCore.Restart, 3,
1000)`: на отказ ребёнка — перезапуск, не больше трёх за секунду. Получив `Started`, родитель
создаёт ребёнка; его полное имя — `parent/worker`.

Клиент ждёт, пока ребёнок появится, и шлёт ему `Inc`, `Inc`, `Boom`, `Inc`. Два `Inc` доводят счёт
до 2. На `Boom` обработчик трапует 42. Трап не роняет исполнителя: обработчик идёт под `Kernel.Try`,
ящик ребёнка приостанавливается, и родитель получает системное сообщение об отказе. Стратегия
отвечает `Restart`. Старый экземпляр получает `Restarting`, фабрика `NewWorker` создаёт новый, и он
получает `Started` — второй раз за пример. Письмо-виновник пропущено, остальные письма ящика
сохранены: последний `Inc` обрабатывает новый экземпляр, и его счёт — 1.

Через 100 мс клиент печатает итог и зовёт `ActorRun.Shutdown`: родитель и ребёнок останавливаются,
задачи пула завершаются.

В этом примере мы увидели, что отказ актора решает родитель, а перезапуск без журнала теряет
состояние экземпляра.
