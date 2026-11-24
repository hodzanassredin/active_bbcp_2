# Обзор на примерах: ObxOnce

Пример показывает однократную переменную `AsyncOnce.Once`: её заполняют один раз, а читают сколько угодно раз; чтение до заполнения ждёт.

Исходный текст модуля `ObxOnce` (`Obx/Mod64/Once.cp`):

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Task) END;
		Writer = POINTER TO RECORD (Tasks.Task) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Run;
	BEGIN
		got := o.ReadInt(); done := TRUE	(* ждёт, пока Writer не заполнит *)
	END Run;

	PROCEDURE (w: Writer) Run;
	BEGIN
		Tasks.Sleep(5); o.FillInt(42)
	END Run;

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
2. Читатель первым доходит до `ReadInt` и ждёт в `ACTIVE.AWAIT(o.done)`.
3. Писатель спит 5 мс и зовёт `FillInt(42)`; выход из исключающей процедуры будит читателя.
4. Читатель возвращает 42 и ставит `done`.

Подробнее — `Docu/ru/Async.md`.

В этом примере мы увидели, как одна задача обещает значение другим и как читатель ждёт обещанного.
