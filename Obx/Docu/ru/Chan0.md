# Обзор на примерах: ObxChan0

Пример показывает встречу — канал `AsyncChan.Chan`, который не хранит сообщений. Отправитель передаёт пять чисел, получатель их складывает. Встреча нужна, когда отправитель не должен убегать вперёд получателя: каждое сообщение переходит из рук в руки, когда готовы обе задачи.

Исходный текст модуля `ObxChan0` (`Obx/Mod64/Chan0.cp`):

```
MODULE ObxChan0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Sender = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan END;
		Receiver = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Body;

	PROCEDURE (r: Receiver) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan0: sum="); Console.WriteStr(d); Console.WriteLn
	END Do;

END ObxChan0.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxChan0.Do
```

```sh
echo 'ObxChan0.Do' | tools/run64.sh
```

В журнале появится:

```
ObxChan0: sum=15
```

## Что произошло

1. `Do` создаёт канал и две задачи и запускает их `Tasks.Start`.
2. Отправитель доходит до `s.c.Send(m)` и ждёт: получателя у канала ещё нет.
3. Получатель зовёт `r.c.Recv(m)`, забирает сообщение ждущего отправителя и будит его.
4. Так повторяется пять раз; затем отправитель шлёт `NIL`, и получатель выходит из цикла и ставит `done`.
5. `Do` исполняется на главном стеке и ждать не может, поэтому крутит `Tasks.Step`, пока `done` не станет истинным.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как две задачи передают значения по встрече, как поток сообщений заканчивают условным сообщением `NIL` и почему команда на главном стеке крутит планировщик, а не ждёт сама.
