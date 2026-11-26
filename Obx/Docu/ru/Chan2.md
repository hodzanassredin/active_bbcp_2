# Обзор на примерах: ObxChan2

Пример показывает почтовый ящик `AsyncChan.Mailbox`. Отправка в ящик никогда не ждёт, поэтому в него можно отправлять с главного стека — из команды, обработчика действия или рассылки сообщений, где ждать нельзя.

Исходный текст модуля `ObxChan2` (`Obx/Mod64/Chan2.cp`):

```
MODULE ObxChan2;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Event = POINTER TO RECORD (AsyncSelect.Msg) code: INTEGER END;
		Handler = POINTER TO RECORD (Tasks.Job) n: INTEGER END;

	VAR box: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Post (code: INTEGER);
	(* Вызывается с главного стека: ящик не заставляет ждать. *)
		VAR e: Event;
	BEGIN
		NEW(e); e.code := code; box.Send(e)
	END Post;

	PROCEDURE (h: Handler) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			box.Recv(m);
			WITH m: Event DO
				IF m.code = 0 THEN EXIT END;
				INC(h.n)
			END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR h: Handler; s: ARRAY 16 OF CHAR;
	BEGIN
		box := AsyncChan.NewMailbox();
		Post(1); Post(2); Post(3); Post(0);
		NEW(h); done := FALSE; Tasks.Start(h);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(h.n, s);
		Console.WriteStr("ObxChan2: events="); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan2.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxChan2.Do
```

```sh
echo 'ObxChan2.Do' | tools/run64.sh
```

В журнале появится:

```
ObxChan2: events=3
```

## Что произошло

1. `Do` создаёт ящик и четыре раза зовёт `Post` с главного стека. Ящик принимает все сообщения сразу.
2. Затем `Do` запускает задачу-обработчик.
3. Обработчик вынимает события по одному в порядке отправки и считает их, пока не встретит код 0.
4. Три события с ненулевым кодом дают `events=3`.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как главный стек отдаёт события задаче через почтовый ящик, не ожидая ни разу.
