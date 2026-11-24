# Модульные тесты в BlackBox

Подсистема `Unit`: `UnitAssert` — проверки, `UnitRunner` — поиск тестов,
фикстуры, изоляция и отчёты. Тест — экспортированная процедура без
параметров с именем `Test…`; регистрировать её не нужно.

## Как написать тест

```
MODULE Text_devWriter;
	IMPORT TextModels, UnitAssert;

	VAR t: TextModels.Model;

	PROCEDURE Setup*;	(* необязательно, перед каждым тестом *)
	BEGIN t := TextModels.dir.New()
	END Setup;

	PROCEDURE TestWriteAtEnd*;
		VAR w: TextModels.Writer;
	BEGIN
		w := t.NewWriter(NIL); w.WriteChar("a"); w.WriteChar("b");
		UnitAssert.EqualInt(2, t.Length(), "длина после двух символов")
	END TestWriteAtEnd;

END Text_devWriter.
```

Тесты живут в отдельной подсистеме `<Подсистема>_dev` рядом с
проверяемой: файл `<Подсистема>_dev/Mod/<Тема>.cp`, модуль
`<Подсистема>_dev<Тема>` (пример — `Text_devWriter` в
`Text_dev/Mod/Writer.cp`). Точка входа без темы — `<Подсистема>_devMain`
(`Text_dev/Mod/Main.cp`). Только для amd64 — `Mod64` в той же
подсистеме (`Async_dev/Mod64/Channels.cp`, модуль
`Async_devChannels`). Поставочная сборка подсистемы `*_dev` не
включает: тесты собирает `tools/unit.sh` по требованию.

## Правила

1. **Поиск.** Исполнитель находит через `Meta.Scanner` экспортированные
   процедуры без параметров с именем `Test…` (порядок — как в таблице
   экспорта модуля). Регистрация не нужна, как `test_*` у pytest.
2. **Фикстуры.** Необязательные `Setup*`/`Teardown*` — вокруг каждого
   теста, `SetupModule*`/`TeardownModule*` — один раз на модуль.
   `Teardown` выполняется и после трапа.
3. **Проверки.** Процедуры `UnitAssert` перечислены в разделе
   «Интерфейс». Обычный
   `ASSERT` тоже работает: неожиданный трап = провал теста (ядро
   печатает свой TRAP REPORT). Текст провала — `expected X, got Y: msg`;
   для строк — `expected <...>, got <...>`.
4. **Без проверок — провал.** Тест, не сделавший ни одного вызова
   `UnitAssert` и не трапнувший, помечается `EMPTY` и считается
   провалом.
5. **Изоляция.** Каждый тест и каждая фикстура идут под `Kernel.Try`;
   трап валит только этот тест, после трапа — `Kernel.Collect`.
   Зритель трапов исполнитель не подменяет: `Try` ловит трап и без него.
6. **Пометки префиксом имени.** `TestSlow…` (долгий), `TestGui…` (нужен
   экран), `TestProc…` (лучше отдельным процессом: ядро, сборщик,
   сигналы, `Kernel.Quit`). По умолчанию идут только обычные тесты;
   пометки включаются явно. Эмуляция перетаскивания мышью запрещена
   правилами стенда.
7. **Отчёт.** Строка на тест: `PASS Модуль.Тест 0 ms`,
   `FAIL Модуль.Тест 0 ms: сообщение`, `EMPTY …`, `SKIP … (пометка)`;
   итог `== Unit: passed N, failed M, empty K, skipped S`. С ключом
   `quit` исполнитель зовёт `Kernel.Quit(0|1)`, и оболочка получает код.
   `tap` печатает TAP, `junit=ФАЙЛ` пишет JUnit XML (путь относительно
   рабочего каталога консоли).
8. **Временные файлы.** `UnitRunner.TempDir` возвращает `UnitTmp`.
   Переносимого `mkdir` внутри BlackBox нет: каталог создаёт и чистит
   внешний запускатель `tools/unit.sh`.
9. **Тест на дефект сначала падает.** Убедитесь, что тест падает на
    старом коде и проходит на новом.
10. **Одно поведение — один тест.** Имя говорит, что проверяется:
    `TestDeleteOutOfRangeTraps`, а не `Test3`.
11. **Тест не требует правок проверяемого модуля и ядра.** Тест
    пользуется только тем, чем пользуется обычный клиент: публичными
    процедурами, глобалами и наблюдаемыми следствиями (счётчики,
    вывод, состояние). Внутренний инвариант ядро проверяет само
    (`ASSERT` в отладочном месте), а не экспортирует проверяльщика;
    диагностический экспорт без пользователей вне `*_dev` убирается.

## Запуск

```
echo 'UnitRunner.Run Unit_devMain quit' | tools/run64.sh
echo 'UnitRunner.Run Text_devWriter slow proc quit' | tools/run64.sh

tools/test.sh                 # модули из tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

Аргументы `UnitRunner.Run`: имя модуля, имя подсистемы или `all`;
`slow`, `gui`, `proc` включают пометки; `junit=ФАЙЛ`, `tap`; `quit` —
в конце вызвать `Kernel.Quit(0|1)` (так внешний консольный запускатель
получает код), `noquit` отменяет это. Имя подсистемы выбирает
загруженные модули её тестов: `System` находит
`System_dev*`, `Async` — `Async_dev*`. `all` — все загруженные
модули, в имени которых есть `_dev`. Модули должны быть уже загружены
(слинкованы) в мир.

`tools/unit.sh` компилирует модуль через `tools/go64.sh` (до трёх
проходов), запускает его отдельным `blackbox-console` и считает успехом
только итог `failed 0` и код 0. Это внешний запускатель для `TestProc…`:
тест, который зовёт `Kernel.Quit` или роняет процесс, не уносит
остальные модули.

## Интерфейс

```
DEFINITION UnitAssert;

	TYPE
		Message = ARRAY 256 OF CHAR;
		State = RECORD
			checks-: LONGINT;
			failures-: INTEGER;
			msg-: Message
		END;

	PROCEDURE Reset;
	PROCEDURE Save (OUT s: State);
	PROCEDURE Restore (IN s: State);
	PROCEDURE Checks (): LONGINT;
	PROCEDURE Failures (): INTEGER;
	PROCEDURE LastMessage (OUT m: Message);
	PROCEDURE Checked;
	PROCEDURE True (cond: BOOLEAN; IN m: ARRAY OF CHAR);
	PROCEDURE Fail (IN m: ARRAY OF CHAR);
	PROCEDURE EqualInt (expected, actual: LONGINT; IN m: ARRAY OF CHAR);
	PROCEDURE EqualReal (expected, actual, eps: REAL; IN m: ARRAY OF CHAR);
	PROCEDURE EqualStr (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR);
	PROCEDURE EqualSet (expected, actual: SET; IN m: ARRAY OF CHAR);
	PROCEDURE NotNil (p: ANYPTR; IN m: ARRAY OF CHAR);
	PROCEDURE Same (expected, actual: ANYPTR; IN m: ARRAY OF CHAR);
	PROCEDURE Traps (p: PROCEDURE; IN m: ARRAY OF CHAR);

END UnitAssert.
```

Процедуры `UnitAssert` предусловий не проверяют и не трапают. Каждая проверка увеличивает
счётчик проверок. Провал увеличивает счётчик провалов и запоминает сообщение.

**PROCEDURE Reset**
Пост: счётчики равны 0, сообщение пусто.

**PROCEDURE Save (OUT s: State)**, **Restore (IN s: State)**
Пост: `s` — копия счётчиков и сообщения; `Restore` возвращает их из `s`.

**PROCEDURE Checks (): LONGINT**, **Failures (): INTEGER**, **LastMessage (OUT m: Message)**
Пост: число проверок, число провалов, сообщение последнего провала.

**PROCEDURE Checked**
Учитывает проверку без условия.
Пост: счётчик проверок увеличен на 1.

**PROCEDURE True (cond: BOOLEAN; IN m: ARRAY OF CHAR)**
Пост: `~cond` — провал с сообщением `m`.

**PROCEDURE Fail (IN m: ARRAY OF CHAR)**
Пост: провал с сообщением `m`.

**PROCEDURE EqualInt (expected, actual: LONGINT; IN m: ARRAY OF CHAR)**
Пост: `expected # actual` — провал `expected X, got Y: m`.

**PROCEDURE EqualReal (expected, actual, eps: REAL; IN m: ARRAY OF CHAR)**
Пост: провал, если `ABS(actual - expected) > eps`. При `eps <= 0` или `expected = 0`
сравнение точное.

**PROCEDURE EqualStr (IN expected, actual: ARRAY OF CHAR; IN m: ARRAY OF CHAR)**
Пост: строки различны — провал `expected <X>, got <Y>: m`.

**PROCEDURE EqualSet (expected, actual: SET; IN m: ARRAY OF CHAR)**
Пост: множества различны — провал `expected set X, got Y: m`; множества печатаются числами.

**PROCEDURE NotNil (p: ANYPTR; IN m: ARRAY OF CHAR)**
Пост: `p = NIL` — провал.

**PROCEDURE Same (expected, actual: ANYPTR; IN m: ARRAY OF CHAR)**
Пост: `expected # actual` — провал.

**PROCEDURE Traps (p: PROCEDURE; IN m: ARRAY OF CHAR)**
Вызывает `p` под `Kernel.Try`.
Пост: `p` не трапнула или `p = NIL` — провал.

```
DEFINITION UnitRunner;

	TYPE
		Counts = RECORD
			passed, failed, empty, skipped: INTEGER
		END;

	PROCEDURE RunModuleCounts (IN module: ARRAY OF CHAR; IN marks: ARRAY OF CHAR;
		quietRun: BOOLEAN; OUT c: Counts);
	PROCEDURE TempDir (OUT path: ARRAY OF CHAR);
	PROCEDURE Run;

END UnitRunner.
```

**PROCEDURE RunModuleCounts (IN module, marks: ARRAY OF CHAR; quietRun: BOOLEAN; OUT c: Counts)**
Исполняет тесты модуля `module` с пометками `marks`; `quietRun` подавляет вывод.
Пост: `c` — итоги; состояние `UnitAssert` восстановлено. Модуль не найден — один провал.

**PROCEDURE TempDir (OUT path: ARRAY OF CHAR)**
Пост: `path = "UnitTmp"`.

**PROCEDURE Run**
Команда; аргументы перечислены в разделе «Запуск».
Пост: итог напечатан. С ключом `quit` процесс завершён кодом 1 при провалах и пустых тестах,
иначе кодом 0.

## Самопроверка

`Unit_devMain` (`Unit_dev/Mod/Main.cp`) проверяет сам исполнитель:
`Unit_devProbe` содержит проходящий, трапующий, падающий, пустой и
медленный тесты плюс фикстуры; `TestNestedRunner` гоняет пробу тихо и
сверяет счётчики `PASS/FAIL/EMPTY/SKIP`.
