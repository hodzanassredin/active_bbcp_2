# Модульные тесты в BlackBox

Подсистема `Unit`: `UnitAssert` — проверки, `UnitRunner` — поиск тестов,
фикстуры, изоляция и отчёты. Тест — экспортированная процедура без
параметров с именем `Test…`; регистрировать её не нужно.

## Как написать тест

```
MODULE Text_testsWriter;
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

END Text_testsWriter.
```

Тесты живут в отдельной подсистеме `<Подсистема>_tests` рядом с
проверяемой: файл `<Подсистема>_tests/Mod/<Тема>.cp`, модуль
`<Подсистема>_tests<Тема>` (пример — `Text_testsWriter` в
`Text_tests/Mod/Writer.cp`). Точка входа без темы — `<Подсистема>_testsMain`
(`Text_tests/Mod/Main.cp`). Только для amd64 — `Mod64` в той же
подсистеме (`Async_tests/Mod64/Channels.cp`, модуль
`Async_testsChannels`). Поставочная сборка подсистемы `*_tests` не
включает: тесты собирает `tools/unit.sh` по требованию.

## Правила

1. **Поиск.** Исполнитель находит через `Meta.Scanner` экспортированные
   процедуры без параметров с именем `Test…` (порядок — как в таблице
   экспорта модуля). Регистрация не нужна, как `test_*` у pytest.
2. **Фикстуры.** Необязательные `Setup*`/`Teardown*` — вокруг каждого
   теста, `SetupModule*`/`TeardownModule*` — один раз на модуль.
   `Teardown` выполняется и после трапа.
3. **Проверки.** `UnitAssert.True`, `EqualInt`, `EqualReal`,
   `EqualStr`, `EqualSet`, `NotNil`, `Same`, `Traps`, `Fail`. Обычный
   `ASSERT` тоже работает: неожиданный трап = провал теста (ядро
   печатает свой TRAP REPORT). Текст провала — «ожидалось X, получено
   Y: msg».
4. **Без проверок — провал.** Тест, не сделавший ни одного вызова
   `UnitAssert` и не трапнувший, помечается `EMPTY` и считается
   провалом.
5. **Изоляция.** Каждый тест и каждая фикстура идут под `Kernel.Try`;
   трап валит только этот тест, после трапа — `Kernel.Collect`.
   `UnitRunner` на время прогона ставит пустой trap viewer (иначе в
   консоли x64 любой трап фатален даже внутри `Try`) и восстанавливает
   прежний после.
6. **Пометки префиксом имени.** `TestSlow…` (долгий), `TestGui…` (нужен
   экран), `TestProc…` (лучше отдельным процессом: ядро, сборщик,
   сигналы, `Kernel.Quit`). По умолчанию идут только обычные тесты;
   пометки включаются явно. Эмуляция перетаскивания мышью запрещена
   правилами стенда.
7. **Отчёт.** Строка на тест: `PASS Модуль.Тест 0 ms`,
   `FAIL Модуль.Тест 0 ms: сообщение`, `EMPTY …`, `SKIP … (пометка)`;
   итог `== Unit: passed N, failed M, empty K, skipped S`. В консоли
   исполнитель зовёт `Kernel.Quit(0|1)` — оболочка получает код.
   `tap` печатает TAP, `junit=ФАЙЛ` пишет JUnit XML (путь относительно
   рабочего каталога консоли).
8. **Временные файлы.** `UnitRunner.TempDir` возвращает `UnitTmp`.
   Переносимого `mkdir` внутри BlackBox нет: каталог создаёт и чистит
   внешний запускатель `tools/unit.sh`.
9. **Тест на дефект сначала красный.** В описании изменения отметить
   «был красный».
10. **Одно поведение — один тест.** Имя говорит, что проверяется:
    `TestDeleteOutOfRangeTraps`, а не `Test3`.

## Запуск

```
echo 'UnitRunner.Run Unit_testsMain' | tools/run64.sh
echo 'UnitRunner.Run Text_testsWriter slow proc' | tools/run64.sh

tools/test.sh                 # модули из tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_testsMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

Аргументы `UnitRunner.Run`: имя модуля, имя подсистемы или `all`;
`slow`, `gui`, `proc` включают пометки; `junit=ФАЙЛ`, `tap`, `noquit`.
Имя подсистемы выбирает загруженные модули её тестов: `System` находит
`System_tests*`, `Async` — `Async_tests*`. `all` — все загруженные
модули, в имени которых есть `_tests`. Модули должны быть уже загружены
(слинкованы) в мир.

`tools/unit.sh` компилирует модуль через `tools/go64.sh` (до трёх
проходов), запускает его отдельным `blackbox-console` и считает успехом
только итог `failed 0` и код 0. Это внешний запускатель для `TestProc…`:
тест, который зовёт `Kernel.Quit` или роняет процесс, не уносит
остальные модули.

## Самопроверка

`Unit_testsMain` (`Unit_tests/Mod/Main.cp`) проверяет сам исполнитель:
`Unit_testsProbe` содержит проходящий, трапующий, падающий, пустой и
медленный тесты плюс фикстуры; `TestNestedRunner` гоняет пробу тихо и
сверяет счётчики `PASS/FAIL/EMPTY/SKIP`.
