# Обзор на примерах: ObxTasks4

Тупик: планировщик печатает, кто чего ждёт.

Задача ждёт `Unpark`, который никто не пошлёт. Нить ОС в таком положении просто висела бы.
Планировщик задач замечает, что живые задачи есть, а будить их нечем, и печатает журнал. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxTasks4;
	IMPORT Tasks;

	TYPE Lonely = POINTER TO RECORD (Tasks.Task) END;

	PROCEDURE (l: Lonely) Run;
	BEGIN
		Tasks.Park	(* разбудить некому *)
	END Run;

	PROCEDURE Do*;
		VAR l: Lonely;
	BEGIN
		NEW(l); Tasks.SetName(l, "lonely"); Tasks.Start(l);
		Tasks.Step;
		IF Tasks.Deadlocked() THEN Tasks.Cancel(l); Tasks.Step END
	END Do;

END ObxTasks4.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Tasks4.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxTasks4.Do
```

В консоли:

```sh
echo 'ObxTasks4.Do' | tools/run64.sh
```

В журнале появится:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Tasks: cancelled
```

## Что произошло

Команда запускает задачу `lonely` и делает один шаг. Задача вызывает `Tasks.Park` и засыпает.
Готовых задач нет, спящих со сроком нет, а живая задача есть, — шаг печатает строку
`Tasks: deadlock: …` и дамп мониторов (их здесь нет) и поднимает `Tasks.Deadlocked()`.

Журнал — диагностика, а не авария. Команда отменяет задачу и делает ещё шаг: `Cancel` будит её,
`Park` проверяет флаг после сна и прерывает задачу трапом 127.

В этом примере мы увидели, как планировщик обнаруживает полный тупик и как из него выйти отменой.
Тупик, в котором хоть одна задача спит со сроком, так не обнаруживается.
