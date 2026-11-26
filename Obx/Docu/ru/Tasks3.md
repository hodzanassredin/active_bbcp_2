# Обзор на примерах: ObxTasks3

Группа задач: провал ребёнка отменяет остальных.

Родитель запускает трёх помощников. Один падает; двое других без присмотра работали бы вечно.
Группа задач связывает их судьбы: провал участника отменяет остальных, а родитель не завершается,
пока живы дети. Пример относится к руководству `Docu/ru/Tasks.md`.

```
MODULE ObxTasks3;
(** Задачи нижнего уровня: пример показывает группу и ожидание её в TaskBoot. *)
	IMPORT Tasks, Console, Strings;

	TYPE
		Child = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Task) g: Tasks.Group END;

	VAR kids: ARRAY 3 OF Child;

	PROCEDURE (c: Child) Run;
	BEGIN
		IF c.n = 1 THEN Tasks.Sleep(10); HALT(30) END;
		LOOP Tasks.Sleep(1000) END
	END Run;

	PROCEDURE (p: Parent) Run;
		VAR i: INTEGER;
	BEGIN
		p.g := Tasks.NewGroup();
		FOR i := 0 TO 2 DO NEW(kids[i]); kids[i].n := i; Tasks.StartIn(kids[i], p.g) END
	END Run;

	PROCEDURE Do*;
		VAR p: Parent; i: INTEGER; s: ARRAY 8 OF CHAR;
	BEGIN
		NEW(p); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.GroupFailed(p.g) THEN Console.WriteStr("group failed") ELSE Console.WriteStr("group ok") END;
		Console.WriteLn;
		FOR i := 0 TO 2 DO
			Strings.IntToString(Tasks.State(kids[i]), s);
			Console.WriteStr("child state "); Console.WriteStr(s); Console.WriteLn
		END
	END Do;

END ObxTasks3.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Tasks3.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxTasks3.Do
```

В консоли:

```sh
echo 'ObxTasks3.Do' | tools/run64.sh
```

В журнале появится:

```
Tasks: cancelled
Tasks: cancelled
group failed
child state 7
child state 6
child state 7
```

## Что произошло

Родитель вызывает `Tasks.NewGroup` и получает свою группу, затем запускает трёх детей
`Tasks.StartIn(kids[i], p.g)` и выходит из `Run`. Он не завершается: по правилу группы владелец ждёт
последнего участника в состоянии `Parked`.

Второй ребёнок (`n = 1`) спит 10 мс и трапует `HALT(30)`. Тело задачи исполняется под `Kernel.Try`,
поэтому трап завершает только эту задачу: её состояние `Finished` (6), `Failed` — `TRUE`. Трап
помечает группу проваленной и отменяет соседей. Они спят в `Sleep`; отмена будит их, `Sleep`
проверяет флаг после сна и прерывает задачу трапом 127 со строкой `Tasks: cancelled`. Их состояние —
`Cancelled` (7). Когда последний участник убран, родитель завершается, и цикл команды кончается.

В этом примере мы увидели группу задач, отмену трапом 127 в точке ожидания и то, что трап в
задаче не роняет программу, а становится признаком `GroupFailed`.
