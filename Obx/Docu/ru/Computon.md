# Обзор на примерах: ObxComputon

Пример показывает компутон — объект, который исполняется в отдельном процессе BlackBox со своей кучей. Так код Component Pascal с `NEW` считается на нескольких ядрах.

Исходный текст модуля `ObxComputon` (`Obx/Mod64/Computon.cp`):

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Services, AsyncIO, Console, Strings;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Job) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (cl: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		IF r # NIL THEN
			WITH r: Square DO Strings.IntToString(r.y, d) END
		ELSE
			Strings.IntToString(AsyncComputon.LastError(), d)
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Do;

END ObxComputon.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxComputon.Do
```

```sh
echo 'ObxComputon.Do' | tools/run64.sh
```

В журнале появится:

```
ObxComputon: 144
```

## Что произошло

1. Тип `Square` расширяет `AsyncComputon.Computon`, реализует `Do` и сохраняет поля в `Externalize` и `Internalize`.
2. `Do` модуля запускает одного исполнителя `AsyncComputon.Start(1)` и задачу-клиента.
3. Клиент сериализует компутон с `x = 12` и отправляет его `Push(c, 0, 1)` исполнителю 0 под номером запроса 1.
4. `Pull(0, 1)` ждёт ответа; исполнитель вызывает `Do` и возвращает объект с `y = 144`.
5. Ответ читается из канала ОС через `Poller`, поэтому `Do` модуля крутит `AsyncIO.Poll(1)` вместе с шагом действий.
6. `Stop` гасит исполнителя.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как описать компутон, отправить его исполнителю и получить результат.
