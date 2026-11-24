# Обзор на примерах: ObxWork0

Пример показывает рабочую нить. Разрешение имени узла блокирует нить ОС, поэтому задача отдаёт его пулу `AsyncWork` и ждёт результата, не останавливая главный цикл.

Исходный текст модуля `ObxWork0` (`Obx/Mod64/Work0.cp`):

```
MODULE ObxWork0;
	IMPORT S := SYSTEM, AsyncWork, Tasks, Services, AsyncIO, Console, Strings;

	TYPE Asker = POINTER TO RECORD (Tasks.Task) END;

	VAR req: AsyncWork.AddrReq; status: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Asker) Run;
	BEGIN
		req.host := "localhost"; req.count := 0; req.status := -1;
		status := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR a: Asker; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(a); done := FALSE; Tasks.Start(a);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxWork0: status="); Strings.IntToString(req.status, s); Console.WriteStr(s);
		Console.WriteLn
	END Do;

END ObxWork0.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxWork0.Do
```

```sh
echo 'ObxWork0.Do' | tools/run64.sh
```

В журнале появится:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Kernel: monitor dump
  (no monitors)
ObxWork0: status=0
```

## Что произошло

1. Задача заполняет глобальную запись `req` и зовёт `AsyncWork.Execute` с адресом задания `ResolveJob`.
2. Пул при первом вызове ставит делегата `AsyncWorkLin` и отдаёт задание свободной нити; задача засыпает в `Tasks.Park`.
3. Задача спит без срока, и планировщик печатает журнал тупика — первые три строки вывода. Это ложная тревога: пробуждение придёт из рабочей нити.
4. Нить кончает работу и пишет байт в канал ОС; `AsyncIO.Poll(1)` в цикле `Do` видит его, и пул будит задачу.
5. Статус 0 значит, что имя `localhost` разрешено.
6. Запись `req` глобальна, а не выделена `NEW`: рабочей нити нельзя касаться кучи.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как увести блокирующий вызов в рабочую нить, почему его аргумент лежит вне кучи и почему планировщик может напечатать ложный журнал тупика.
