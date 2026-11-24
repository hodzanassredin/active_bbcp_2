# Обзор на примерах: ObxBank0

Действие и задача делают одну работу: выписку по трём счетам.

Пример печатает остаток трёх счетов по одному счёту за раз дважды: сначала действием
`Services.Action`, потом задачей `Tasks.Task`. Он показывает, чем отличаются эти две вещи, когда
работа разбита на куски. Пример относится к главе 1 учебника `Docu/ru/Tut-Active-1.md`.

```
MODULE ObxBank0;
	IMPORT Services, Tasks, Console, Strings;

	CONST n = 3;	(* число счетов *)

	TYPE
		Statement = POINTER TO RECORD (Services.Action) i: INTEGER END;
		StatementTask = POINTER TO RECORD (Tasks.Task) END;

	VAR balance: ARRAY n OF INTEGER; done: BOOLEAN;

	PROCEDURE Line (IN who: ARRAY OF CHAR; i: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr(who); Console.WriteStr(": account ");
		Strings.IntToString(i, s); Console.WriteStr(s);
		Console.WriteStr(" balance "); Strings.IntToString(balance[i], s); Console.WriteStr(s);
		Console.WriteLn
	END Line;

	(* Действие: один счёт за вызов, место в работе хранит поле i *)
	PROCEDURE (a: Statement) Do;
	BEGIN
		Line("action", a.i);
		INC(a.i);
		IF a.i < n THEN Services.DoLater(a, Services.now) ELSE done := TRUE END
	END Do;

	(* Задача: обычный цикл, место в работе лежит на её стеке *)
	PROCEDURE (t: StatementTask) Run;
		VAR i: INTEGER;
	BEGIN
		FOR i := 0 TO n - 1 DO Line("task", i); Tasks.Yield END;
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Statement; t: StatementTask;
	BEGIN
		balance[0] := 100; balance[1] := 250; balance[2] := 40;
		NEW(a); a.i := 0; done := FALSE; Services.DoLater(a, Services.now);
		WHILE ~done DO Services.actionHook.Step END;
		NEW(t); done := FALSE; Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END
	END Do;

END ObxBank0.
```

Модуль собирает `tools/build64.sh` (исходник — `Obx/Mod64/Bank0.cp`; он только для amd64). В окне
BlackBox команду исполняет коммандер:

```
   ObxBank0.Do
```

В консоли:

```sh
echo 'ObxBank0.Do' | tools/run64.sh
```

В журнале появится:

```
action: account 0 balance 100
action: account 1 balance 250
action: account 2 balance 40
task: account 0 balance 100
task: account 1 balance 250
task: account 2 balance 40
```

## Что произошло

Команда `Do` ставит действие `Statement` в очередь вызовом `Services.DoLater(a, Services.now)` и
крутит главный цикл вызовами `Services.actionHook.Step`. Каждый проход главного цикла вызывает
`a.Do` один раз. `Do` печатает счёт номер `a.i`, увеличивает `a.i` и, если счета не кончились,
ставит себя в очередь снова. Последний вызов поднимает флаг `done`.

Затем команда запускает задачу `StatementTask` и крутит тот же главный цикл. Задачу исполняет
планировщик `Tasks`, который сам — действие главного цикла. `Run` проходит циклом `FOR` по
счетам и после каждого счёта зовёт `Tasks.Yield`; переменная `i` лежит на стеке задачи и переживает
уступку.

Действию нельзя ждать: `Tasks.Sleep` внутри `Do` трапует 21. Задача ждёт где угодно в `Run`.

В этом примере мы увидели, что действие хранит место в работе в полях объекта и начинает каждый
вызов сначала, а задача пишет ту же работу обычным циклом, и что задачи исполняются внутри того же
главного цикла, что и действия.
