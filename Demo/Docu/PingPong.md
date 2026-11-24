# Обзор на примерах: DemoPingPong

Пинг-понг через один канал.

Две задачи по очереди отбивают мяч. Пример взят из R. Pike, *Go Concurrency Patterns* (Google,
2012, <https://go.dev/talks/2012/concurrency.slide#14>) и показывает встречу: отправитель ждёт,
пока получатель не возьмёт сообщение.

```
MODULE DemoPingPong;
(** Пинг-понг по каналу.

	Перенесено из доклада Rob Pike "Go Concurrency Patterns" (Google,
	2012), пример ping-pong: две горутины перекидывают мяч по общему
	каналу, счётчик ударов растёт до заданного предела.
	Источник: https://go.dev/talks/2012/concurrency.slide#14
	Здесь две задачи Tasks и канал AsyncChan.Chan (встреча); мяч —
	сообщение AsyncSelect.Msg, после отправки не меняется.

	Команда DemoPingPong.Run: 10 ударов. Итог детерминирован:
	10 ударов, сумма номеров 45. *)

	IMPORT Tasks, AsyncSelect, AsyncChan, DemoSupport;

	CONST
		total = 10;

	TYPE
		Ball = POINTER TO RECORD (AsyncSelect.Msg)
			n: INTEGER
		END;

		Player = POINTER TO RECORD (Tasks.Task)
			table: AsyncChan.Chan;
			first: BOOLEAN	(* этот игрок подаёт первый мяч *)
		END;

	VAR
		Hits*: INTEGER;
		Sum*: INTEGER;
		Failed*: BOOLEAN;
		a, b: Player;

	PROCEDURE NewBall (n: INTEGER): Ball;
		VAR m: Ball;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END NewBall;

	PROCEDURE (p: Player) Run;
		VAR m: AsyncSelect.Msg; n: INTEGER;
	BEGIN
		IF p.first THEN p.table.Send(NewBall(0)) END;
		LOOP
			p.table.Recv(m);
			WITH m: Ball DO n := m.n ELSE n := -1 END;
			IF n < 0 THEN EXIT END;
			INC(Hits); INC(Sum, n);
			IF n + 1 >= total THEN
				p.table.Send(NewBall(-1))	(* мяч закончился *)
			ELSE
				p.table.Send(NewBall(n + 1))
			END;
			IF n + 1 >= total THEN EXIT END
		END
	END Run;

	PROCEDURE Run*;
	(** Демонстрация: два игрока, 10 ударов. *)
		VAR ok: BOOLEAN;
	BEGIN
		Hits := 0; Sum := 0; Failed := FALSE;
		NEW(a); a.table := AsyncChan.NewChan(); a.first := TRUE;
		NEW(b); b.table := a.table; b.first := FALSE;
		Tasks.Start(a); Tasks.Start(b);
		ok := DemoSupport.Pump(10000);
		Failed := Tasks.Failed(a) OR Tasks.Failed(b) OR ~ok;
		DemoSupport.LogInt("DemoPingPong: hits=", Hits);
		DemoSupport.LogInt(" sum=", Sum);
		IF Failed THEN DemoSupport.Log(" failed") END;
		DemoSupport.LogLn
	END Run;

END DemoPingPong.
```

Модуль собирает `tools/build64.sh` (исходник — `Demo/Mod/PingPong.cp`; подсистема `Demo` — только для
amd64). В окне BlackBox команду исполняет коммандер:

```
   DemoPingPong.Run
```

В консоли:

```sh
echo 'DemoPingPong.Run' | tools/run64.sh
```

В журнале появится:

```
DemoPingPong: hits=10 sum=45
```

## Что произошло

Игроки — задачи `Tasks.Task`. Стол — канал `AsyncChan.Chan`, то есть встреча: каждая отправка
ждёт приёма. Мяч — сообщение `AsyncSelect.Msg`, расширенное полем `n`. Игрок принимает мяч,
увеличивает счёт и отправляет новый мяч; после отправки сообщение не меняется, поэтому на каждый
удар выделяется новое.

Мяч с `n = -1` заканчивает игру. Десять ударов с номерами 0…9 дают `hits=10` и сумму 45.

## Тест

`Demo_devExamples.TestProcPingPong` проверяет 10 ударов и сумму 45.

В этом примере мы увидели канал-встречу и то, что после отправки сообщение принадлежит
получателю.
