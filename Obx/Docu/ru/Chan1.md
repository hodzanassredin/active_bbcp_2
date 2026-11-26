# Обзор на примерах: ObxChan1

Пример показывает ограниченный буфер `AsyncChan.Buffer`. Производитель быстрее потребителя, и буфер на четыре сообщения даёт ему забежать вперёд, но не дальше четырёх сообщений.

Исходный текст модуля `ObxChan1` (`Obx/Mod64/Chan1.cp`):

```
MODULE ObxChan1;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; max: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO
			NEW(m); m.n := i; p.c.Send(m);
			IF p.c.count > p.max THEN p.max := p.c.count END
		END;
		p.c.Send(NIL)
	END Body;

	PROCEDURE (k: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			k.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO k.sum := k.sum + m.n END;
			Tasks.Sleep(1)	(* потребитель медленнее производителя *)
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Buffer; p: Producer; k: Consumer; s: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewBuffer(4);
		NEW(p); p.c := c; NEW(k); k.c := c; done := FALSE;
		Tasks.Start(p); Tasks.Start(k);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxChan1: sum="); Strings.IntToString(k.sum, s); Console.WriteStr(s);
		Console.WriteStr(" max="); Strings.IntToString(p.max, s); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan1.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxChan1.Do
```

```sh
echo 'ObxChan1.Do' | tools/run64.sh
```

В журнале появится:

```
ObxChan1: sum=55 max=4
```

## Что произошло

1. `Do` создаёт буфер ёмкостью 4 и две задачи.
2. Производитель кладёт сообщения, пока буфер не полон; после каждого `Send` он запоминает наибольшее значение `count`.
3. Пятый `Send` находит буфер полным и ждёт.
4. Потребитель забирает сообщение, спит миллисекунду и берёт следующее; каждое освобождённое место будит производителя.
5. Сумма чисел от 1 до 10 — 55; наибольшее заполнение — 4, ёмкость буфера.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как буфер сглаживает разницу скоростей двух задач и как его ёмкость ограничивает запас.
