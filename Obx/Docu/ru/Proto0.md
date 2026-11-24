# Обзор на примерах: ObxProto0

Пример показывает протокол диалога. К ящику прикреплён автомат «приветствие, данные сколько угодно раз, прощание», и каждая отправка проверяется по нему в момент, когда совершается.

Исходный текст модуля `ObxProto0` (`Obx/Mod64/Proto0.cp`):

```
MODULE ObxProto0;
	IMPORT AsyncChan, AsyncSelect, AsyncProto, Console;

	CONST desc = "start: Hello > talk; talk: Data > talk; talk: Bye > done; done:";

	TYPE
		Hello = POINTER TO RECORD (AsyncSelect.Msg) END;
		Data = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Bye = POINTER TO RECORD (AsyncSelect.Msg) END;

	PROCEDURE Do*;
		VAR p: AsyncProto.Protocol; box: AsyncChan.Mailbox;
			h: Hello; d: Data; b: Bye; name: ARRAY 32 OF CHAR;
	BEGIN
		p := AsyncProto.New(desc);
		box := AsyncChan.NewMailbox(); AsyncChan.SetProtocol(box, p);
		NEW(h); box.Send(h);
		NEW(d); d.n := 1; box.Send(d);
		NEW(d); d.n := 2; box.Send(d);
		NEW(b); box.Send(b);
		AsyncProto.StateName(p, box.protoState, name);
		Console.WriteStr("ObxProto0: state="); Console.WriteStr(name); Console.WriteLn
	END Do;

END ObxProto0.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxProto0.Do
```

```sh
echo 'ObxProto0.Do' | tools/run64.sh
```

В журнале появится:

```
ObxProto0: state=done
```

## Что произошло

1. `AsyncProto.New` строит автомат из текста `desc`.
2. `AsyncChan.SetProtocol` прикрепляет его к ящику и ставит начальное состояние `start`.
3. Отправки `Hello`, `Data`, `Data`, `Bye` переводят автомат в `talk`, `talk`, `talk`, `done`.
4. `AsyncProto.StateName` называет конечное состояние по полю `protoState` канала.
5. Отправка `Data` до `Hello` напечатала бы строку `AsyncChan protocol: …` и трапнула бы с кодом 21.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как записать порядок сообщений диалога текстом и поручить его проверку каналу.
