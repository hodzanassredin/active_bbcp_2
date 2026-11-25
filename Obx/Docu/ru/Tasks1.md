# Обзор на примерах: ObxTasks1

Ожидание: `Park` и `Unpark`, `Sleep`, `Join`.

Задачам приходится ждать: паузы, сигнала от другой задачи, конца другой задачи. Пример соединяет
три вида ожидания в одной программе. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxTasks1;
	IMPORT Tasks, Console;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Task) END;
		Waker = POINTER TO RECORD (Tasks.Task) who: Tasks.Task END;
		Main = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (s: Sleeper) Run;
	BEGIN
		Console.WriteStr("sleeper: park"); Console.WriteLn;
		Tasks.Park;
		Console.WriteStr("sleeper: woken"); Console.WriteLn
	END Run;

	PROCEDURE (w: Waker) Run;
	BEGIN
		Tasks.Sleep(20);
		Console.WriteStr("waker: unpark"); Console.WriteLn;
		Tasks.Unpark(w.who)
	END Run;

	PROCEDURE (m: Main) Run;
		VAR s: Sleeper; w: Waker;
	BEGIN
		NEW(s); Tasks.Start(s);
		NEW(w); w.who := s; Tasks.Start(w);
		Tasks.Join(s); Tasks.Join(w);
		Console.WriteStr("main: both finished"); Console.WriteLn
	END Run;

	PROCEDURE Do*;
		VAR m: Main;
	BEGIN
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxTasks1.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Tasks1.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxTasks1.Do
```

В консоли:

```sh
echo 'ObxTasks1.Do' | tools/run64.sh
```

В журнале появится:

```
sleeper: park
waker: unpark
sleeper: woken
main: both finished
```

## Что произошло

Команда запускает одну задачу `Main`: ждать можно только в задаче, а `Join` из команды трапнул бы 20
с текстом `Tasks: wait outside a task`. `Main` запускает соню `Sleeper` и будильник `Waker` и ждёт обоих
вызовами `Tasks.Join`.

Соня печатает `sleeper: park` и вызывает `Tasks.Park`: она спит, пока кто-нибудь не вызовет для неё
`Unpark`. Будильник спит 20 мс в `Tasks.Sleep`; за это время планировщику нечего исполнять, и шаги
идут вхолостую до срока. Проснувшись, будильник печатает `waker: unpark` и будит соню. Соня печатает
`sleeper: woken` и завершается. `Join` возвращается, когда обе задачи завершены, и `Main` печатает
последнюю строку.

Если бы будильник успел вызвать `Unpark` раньше, чем соня — `Park`, сигнал бы не потерялся: `Unpark`
ставит флаг пробуждения, и `Park` вернулся бы сразу.

В этом примере мы увидели три вида ожидания, правило «ждать можно только в задаче» и то, что
сигнал `Unpark` сохраняется до следующего `Park`.
