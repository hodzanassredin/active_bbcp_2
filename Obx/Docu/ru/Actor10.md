# Обзор на примерах: ObxActor10

Маршрутизатор посылает письмо одному выбранному маршруту или всем: `NewGroup` с выбором по
кругу, широковещанием и рандеву-хешем.

Три работника считают свои письма. Групповой маршрутизатор с `ActorRouter.roundRobin` отдаёт
каждому работнику два из шести писем. Второй маршрутизатор с `ActorRouter.broadcast` посылает
одно письмо всем трём. Третий с `ActorRouter.consistentHash` посылает четыре письма с одним
ключом одному маршруту. Пример относится к руководству `Actor/Docu/ru/Actors.md` (раздел 4).

```
MODULE ObxActor10;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun, ActorRouter;

	TYPE
		Put = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) id: INTEGER END;
		Driver = POINTER TO RECORD (Tasks.Job) END;

	VAR
		refs: ARRAY 3 OF ActorCore.Ref;
		counts: ARRAY 3 OF INTEGER;
		served, nextId: INTEGER;
		put: Put;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: Put DO INC(counts[a.id]); INC(served)
		| m: ActorRouter.Keyed DO INC(counts[a.id]); INC(served)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); a.id := nextId; INC(nextId); RETURN a
	END NewWorker;

	PROCEDURE SpawnWorkers;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO 2 DO
			refs[i] := ActorCore.Spawn(NIL, ActorCore.NewProps(NewWorker), "")
		END
	END SpawnWorkers;

	PROCEDURE Wait (n: INTEGER);
	BEGIN
		WHILE served < n DO Tasks.Sleep(1) END
	END Wait;

	PROCEDURE Print (IN s: ARRAY OF CHAR; a, b, c: INTEGER);
		VAR d: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(s);
		Strings.IntToString(a, d); Console.WriteStr(d);
		Strings.IntToString(b, d); Console.WriteStr(" "); Console.WriteStr(d);
		Strings.IntToString(c, d); Console.WriteStr(" "); Console.WriteStr(d); Console.WriteLn
	END Print;

	PROCEDURE (t: Driver) Body- (VAR ctx: Tasks.Ctx);
		VAR r: ActorCore.Ref; i, b0, b1, b2: INTEGER; m: ActorRouter.Keyed;
	BEGIN
		r := ActorRouter.NewGroup(refs, ActorRouter.roundRobin, 1);
		FOR i := 1 TO 6 DO ActorCore.Tell(r, put) END;
		Wait(6);
		Print("round-robin: ", counts[0], counts[1], counts[2]);
		r := ActorRouter.NewGroup(refs, ActorRouter.broadcast, 1);
		ActorCore.Tell(r, put);
		Wait(9);
		Print("after broadcast: ", counts[0] - 2, counts[1] - 2, counts[2] - 2);
		r := ActorRouter.NewGroup(refs, ActorRouter.consistentHash, 1);
		b0 := counts[0]; b1 := counts[1]; b2 := counts[2];
		FOR i := 1 TO 4 DO
			NEW(m); m.key := "order-7"; ActorCore.Tell(r, m)
		END;
		Wait(13);
		IF (counts[0] - b0 = 4) OR (counts[1] - b1 = 4) OR (counts[2] - b2 = 4) THEN
			Console.WriteStr("hash: one route got 4 letters"); Console.WriteLn
		ELSE Console.WriteStr("hash: letters spread"); Console.WriteLn
		END;
		ActorRun.Shutdown
	END Body;

	PROCEDURE Do*;
		VAR t: Driver;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		nextId := 0; served := 0;
		SpawnWorkers; NEW(put); NEW(t); Tasks.Start(t);
		WHILE Tasks.Count() > 0 DO Services.actionHook.Step END
	END Do;

END ObxActor10.
```

Запуск в компоненте:

```
   ObxActor10.Do
```

или в консоли:

```
echo 'ObxActor10.Do' | tools/run64.sh
```

В журнале появится:

```
round-robin: 2 2 2
after broadcast: 1 1 1
hash: one route got 4 letters
```

## Что произошло

`SpawnWorkers` рождает три корневых актора; каждый считает полученные письма. Первый
маршрутизатор — группа из этих трёх с выбором `roundRobin`: шесть писем идут по кругу, по два
каждому.

Второй маршрутизатор рассылает одно письмо всем; каждый маршрут получает его, и у каждого
работника прибавляется одно письмо.

Третий маршрутизатор хеширует ключ письма. Четыре письма несут один ключ `order-7`, поэтому
`ActorHash` даёт им одного владельца и все четыре приходят одному работнику. Другой ключ дал бы
другого владельца; новый маршрут меняет владельца только у тех ключей, которые он выиграл.

## В этом примере мы увидели

Маршрутизатор выбирает маршрут по правилу и пересылает письмо с прежним отправителем. Выбор по
кругу делит нагрузку, широковещание доходит до всех, а рандеву-хеш держит один ключ на одном
маршруте.
