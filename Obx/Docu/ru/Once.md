# Обзор на примерах: ObxOnce

Пример показывает однократную переменную `AsyncOnce.Once`: её заполняют один раз, а читают сколько угодно раз; чтение до заполнения ждёт.

Исходный текст модуля `ObxOnce` (`Obx/Mod64/Once.cp`):

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Job) END;
		Writer = POINTER TO RECORD (Tasks.Job) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		done := o.ReadInt(ctx, got)	(* ждёт, пока Writer не заполнит; FALSE — задачу остановили *)
	END Body;

	PROCEDURE (w: Writer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(5); o.FillInt(ctx, 42)
	END Body;

	PROCEDURE Do*;
		VAR r: Reader; w: Writer; s: ARRAY 16 OF CHAR;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); NEW(w); done := FALSE;
		Tasks.Start(r); Tasks.Start(w);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxOnce.
```

Команда запуска (в оконном BlackBox — коммандер, в консольном — строка ввода):

```
   ObxOnce.Do
```

```sh
echo 'ObxOnce.Do' | tools/run64.sh
```

В журнале появится:

```
ObxOnce: 42
```

## Что произошло

1. `Do` создаёт переменную и две задачи: читателя и писателя.
2. Читатель первым доходит до `ReadInt` и ждёт в `Await` флага `filled`.
3. Писатель спит 5 мс и зовёт `FillInt(42)`; выход из исключающей процедуры будит читателя.
4. Читатель возвращает 42 и ставит `done`.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как одна задача обещает значение другим и как читатель ждёт обещанного.
