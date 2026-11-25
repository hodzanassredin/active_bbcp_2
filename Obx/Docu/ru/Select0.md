# Обзор на примерах: ObxSelect0

Пример показывает выбор с тайм-аутом. Клиент ждёт ответа сервера не дольше 50 мс: `AsyncChan.Select` ждёт сразу приёма из ящика ответа и истечения срока и сообщает, что случилось первым.

Исходный текст модуля `ObxSelect0` (`Obx/Mod64/Select0.cp`):

```
MODULE ObxSelect0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Services, AsyncIO, Console;

	TYPE
		Reply = POINTER TO RECORD (AsyncSelect.Msg) END;
		Server = POINTER TO RECORD (Tasks.Task) delay: INTEGER; out: AsyncChan.Mailbox END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR fast, slow: ARRAY 16 OF CHAR; done: BOOLEAN;

	PROCEDURE (s: Server) Run;
		VAR r: Reply;
	BEGIN
		Tasks.Sleep(s.delay); NEW(r); s.out.Send(r)
	END Run;

	PROCEDURE Ask (delay: INTEGER; OUT res: ARRAY OF CHAR);
		VAR s: Server; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox(); Tasks.Start(s);
		AsyncChan.RecvAlt(s.out, ops[0]);
		AsyncChan.TimeoutAlt(50, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN res := "reply" ELSE res := "timeout" END
	END Ask;

	PROCEDURE (c: Client) Run;
	BEGIN
		Ask(10, fast); Ask(200, slow); done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: Client;
	BEGIN
		NEW(c); done := FALSE; Tasks.Start(c);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxSelect0: fast="); Console.WriteStr(fast);
		Console.WriteStr(" slow="); Console.WriteStr(slow); Console.WriteLn
	END Do;

END ObxSelect0.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxSelect0.Do
```

```sh
echo 'ObxSelect0.Do' | tools/run64.sh
```

В журнале появится:

```
ObxSelect0: fast=reply slow=timeout
Tasks: cancelled
```

Вторую строку печатает отмена медленного исполнителя: когда команда кончается, он ещё спит, и при
завершении программы `Tasks` отменяет оставшиеся задачи (трап 127 в `Sleep`).

## Что произошло

1. Задача-клиент дважды зовёт `Ask`: с сервером, который отвечает через 10 мс, и с сервером, который отвечает через 200 мс.
2. `Ask` запускает сервер и строит две альтернативы: `RecvAlt` по ящику ответа и `TimeoutAlt(50)`.
3. `Select` возвращает 0, если первым пришёл ответ, и 1, если первым истёк срок.
4. Опоздавший сервер всё равно кладёт ответ в ящик и завершается; ответь он во встречу, он остался бы ждать навсегда.
5. `Do` крутит `Services.actionHook.Step` и `AsyncIO.Poll(1)`, как главный цикл BlackBox: тайм-аут выбора — действие `Services`, и одного `Tasks.Step` для него мало.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как ждать нескольких событий сразу, как ограничить ожидание сроком и почему ответ с возможным опозданием отправляют в ящик.
