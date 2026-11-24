# Модульные тесты в BlackBox

Программист меняет процедуру `Strings.IntToString` и хочет знать, не сломал ли он отрицательные
числа. Без тестов он открывает рабочее окно, набирает команду, смотрит на вывод и через неделю забывает
повторить эту проверку. Подсистема `Unit` превращает такую проверку в модуль, который запускается одной
командой и печатает однозначный итог. Подсистема состоит из двух модулей: `UnitAssert` проверяет
значения, `UnitRunner` находит тесты, исполняет их по одному и печатает отчёт.

## 1. Первый тест

*Тест* — экспортированная процедура без параметров, имя которой начинается с `Test`. Регистрировать её
нигде не нужно: исполнитель находит такие процедуры сам, перебирая экспорт модуля через `Meta.Scanner`.

Тесты живут не в проверяемой подсистеме, а рядом с ней, в подсистеме с суффиксом `_dev`. Тесты
подсистемы `Text` лежат в `Text_dev/Mod/<Тема>.cp`, модуль называется `Text_dev<Тема>`. Первый
тест положим в подсистему `Unit_dev`, файл `Unit_dev/Mod/Example.cp`:

```
MODULE Unit_devExample;

	IMPORT Strings, UnitAssert;

	VAR s: ARRAY 32 OF CHAR;

	PROCEDURE Setup*;
	BEGIN s := ""
	END Setup;

	PROCEDURE TestIntToStringNegative*;
	BEGIN
		Strings.IntToString(-42, s);
		UnitAssert.EqualStr("-42", s, "знак и цифры")
	END TestIntToStringNegative;

	PROCEDURE TestFindMissing*;
		VAR pos: INTEGER;
	BEGIN
		Strings.Find("abc", "x", 0, pos);
		UnitAssert.EqualInt(-1, pos, "нет образца")
	END TestFindMissing;

END Unit_devExample.
```

Каждый тест вызывает проверяемую процедуру и сравнивает результат с ожидаемым. Первым аргументом
сравнения идёт ожидаемое значение, вторым — полученное, третьим — пояснение, которое попадёт в отчёт при
провале. Процедура `Setup` — не тест: исполнитель вызывает её перед каждым тестом, поэтому
каждый тест начинает с пустой строки `s`.

Запустим модуль внешним запускателем. Он компилирует `Unit` и модуль теста, запускает отдельный
консольный BlackBox и выполняет в нём `UnitRunner.Run`:

```
$ tools/unit.sh Unit_devExample
== Unit: run Unit_devExample
PASS Unit_devExample.TestFindMissing 0 ms
PASS Unit_devExample.TestIntToStringNegative 0 ms
== Unit: passed 2, failed 0, empty 0, skipped 0
```

На каждый тест печатается одна строка: исход, имя и время. Тесты идут в порядке таблицы экспорта
модуля, то есть по алфавиту, а не в порядке текста. Итоговая строка считает исходы. Код завершения
`tools/unit.sh` равен 0, только если в итоге стоит `failed 0` и процесс вышел с кодом 0.

## 2. Как устроен прогон

Тест, который трапует, не должен уносить с собой остальные тесты, а его мусор не должен достаться
следующему. Поэтому `UnitRunner` исполняет каждый тест и каждую фикстуру под `Kernel.Try`. Трап
становится провалом этого теста с сообщением `test trapped`; ядро печатает свой отчёт о трапе, а
исполнитель зовёт `Kernel.Collect` и переходит к следующему тесту. Зритель трапов исполнитель не
подменяет: `Kernel.Try` ловит трап и без него.

*Фикстуры* — необязательные процедуры с особыми именами. `Setup` и `Teardown` идут вокруг каждого
теста, `SetupModule` и `TeardownModule` — один раз на модуль, до первого теста и после последнего.
`Teardown` выполняется и после трапа теста: он закрывает то, что открыл `Setup`. Если трапует
`SetupModule`, тестам модуля не на чем исполняться: исполнитель печатает провал `SetupModule trapped`, а
тесты модуля помечает `SKIP` с той же причиной.

Тест провален, если трапнул он сам, `Setup` или `Teardown` или если не прошла одна из проверок
`UnitAssert`; сообщение берётся из последней неудачной проверки. Тест, который не трапнул и не сделал
ни одной проверки, получает пометку `EMPTY`. Такой тест ничего не доказывает: он прошёл бы и при
сломанном коде. Например, тест

```
PROCEDURE TestNothing*;
BEGIN
	Strings.IntToString(1, s)
END TestNothing;
```

даёт строку `EMPTY Unit_devExample.TestNothing 0 ms: no checks`. Итоговая строка показывает пустые тесты
отдельно и включает их в число проваленных: `passed 2, failed 1, empty 1`. Процесс с ключом `quit`
завершается кодом 1.

Обычный `ASSERT` в тесте тоже работает: неожиданный трап — провал. Однако тест только с `ASSERT` при
успехе окажется пустым. Если тест проверяет, что код не трапует, он отмечает проверку явно вызовом
`UnitAssert.Checked`. Если тест проверяет, что код трапует, он зовёт `UnitAssert.Traps(p, …)`: процедура
`p` исполняется под `Kernel.Try`, и провал засчитывается, когда трапа не было.

Сообщения провала имеют вид `expected X, got Y: пояснение`, для строк — `expected <X>, got <Y>:
пояснение`. Сообщение обрезается до 255 знаков (`UnitAssert.Message`); длинная строка в сравнении не
трапует.

## 3. Пометки и отдельный процесс

Некоторые тесты нельзя гонять при каждой правке. Долгий тест задерживает прогон, тест с окнами требует
экрана, а тест, который зовёт `Kernel.Quit` или проверяет сборщик, может уронить процесс. Такие тесты
помечаются префиксом имени: `TestSlow…` — долгий, `TestGui…` — нужен экран, `TestProc…` — лучше в
отдельном процессе. По умолчанию исполнитель их пропускает и печатает `SKIP` с причиной `slow`, `gui`
или `proc`; одноимённые ключи команды `UnitRunner.Run` их включают.

Отдельный процесс даёт `tools/unit.sh`: один модуль — один процесс `blackbox-console`. Если тест уронит
процесс, `tools/unit.sh` сообщит код завершения и хвост вывода, а остальные модули списка
`tools/test.sh` пройдут в своих процессах.

## 4. Правила

1. Одно поведение — один тест. Имя говорит, что проверяется: `TestDeleteOutOfRangeTraps`, а не
   `Test3`. Тогда строка `FAIL` в отчёте сама называет сломанное поведение.
2. Тест на дефект сначала падает. Его запускают на коде с дефектом и видят `FAIL`, затем на
   исправленном и видят `PASS`. Тест, который ни разу не падал, мог не проверять ничего.
3. Тест не требует правок проверяемого модуля и ядра. Тест пользуется тем же, чем обычный клиент:
   экспортом, глобальными переменными и наблюдаемыми следствиями — счётчиками, выводом, состоянием.
4. Временные файлы лежат в каталоге `UnitRunner.TempDir` (`UnitTmp`). Переносимого `mkdir` в BlackBox
   нет, поэтому каталог создаёт и чистит `tools/unit.sh`.

Нарушение третьего правила сразу не видно. Пусть тест хочет знать, что куча спящих задач `Tasks`
упорядочена. Проще всего добавить в `Tasks` экспортную процедуру `HeapOk*` и вызвать её из теста. Тест
пройдёт, но ядро получит экспорт, которым не пользуется ни один клиент. Новый экспорт меняет отпечаток
символьного файла, и все модули, импортирующие `Tasks`, требуют перекомпиляции; поставка несёт
процедуру, которую нельзя удалить или изменить, не сломав тест. Правильный путь другой. Внутренний
инвариант ядро проверяет само, `ASSERT` в том месте, где инвариант может нарушиться (в `Tasks` это
`ASSERT` с номером 100 при уплотнении кучи), а тест создаёт тысячи спящих задач с разными сроками и
проверяет то, что видит клиент: задачи просыпаются в порядке сроков. Трап 100 при этом тоже
обнаружится, как провал `test trapped`. Диагностический экспорт без пользователей вне `*_dev`
убирается.

## 5. Подсистемы `_dev`

Тесты отделены от кода потому, что поставочная сборка подсистем `*_dev` не включает: пользователь не
получает ни тестов, ни их зависимостей. Тесты собирает `tools/unit.sh` по требованию.

Имя модуля определяет его файл по правилам BlackBox: `Text_devWriter` лежит в `Text_dev/Mod/Writer.cp`.
Точка входа подсистемы без темы — `<Подсистема>_devMain`, например `Text_dev/Mod/Main.cp`. Тесты только
для amd64 лежат в `Mod64` той же подсистемы: модуль `Async_devChannels` — в
`Async_dev/Mod64/Channels.cp`.

Сам исполнитель проверяет `Unit_devMain`. Модуль `Unit_devProbe` содержит проходящий, трапующий,
падающий, пустой и медленный тесты и фикстуры; `TestNestedRunner` гоняет пробу тихо через
`UnitRunner.RunModuleCounts` и сверяет счётчики.

## 6. Запуск и отчёты

Внутри BlackBox тесты запускает команда `UnitRunner.Run` с аргументами через пробел. Первый аргумент —
цель: имя модуля, имя подсистемы или `all`. Имя подсистемы выбирает загруженные модули её тестов:
`System` находит `System_dev*`, `Async` — `Async_dev*`; `all` выбирает все загруженные модули, в имени
которых есть `_dev`. Модули должны быть уже загружены в мир. Ключи `slow`, `gui`, `proc` включают
пометки; `tap` печатает отчёт в формате TAP; `junit=ФАЙЛ` пишет отчёт JUnit XML; `quit` в конце вызывает
`Kernel.Quit(0)` или `Kernel.Quit(1)`, чтобы внешний запускатель получил код; `noquit` отменяет `quit`.

```
echo 'UnitRunner.Run Unit_devMain quit' | tools/run64.sh
echo 'UnitRunner.Run Text_devWriter slow proc quit' | tools/run64.sh

tools/test.sh                 # модули из tools/tests.txt
tools/test.sh --list
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

`tools/unit.sh` компилирует `Unit`, модули из `--compile` и модуль теста через `tools/go64.sh` (до трёх
проходов, потому что порядок зависимостей заранее неизвестен) и выполняет `UnitRunner.Run` с ключом
`quit` и пометками из `--marks`. `tools/test.sh` проходит список `tools/tests.txt`, где у каждого модуля
указаны пометки, тайм-аут, дополнительные модули и опции компиляции.

Отчёт JUnit читает система непрерывной сборки: она показывает провалы по именам тестов. Файл пишется в
UTF-8, путь считается от рабочего каталога консоли. Каждый тест становится элементом `<testcase>`;
пропущенный тест несёт причину в `<skipped message="…"/>`.

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

Процедуры `UnitAssert` предусловий не проверяют и не трапают. Каждая проверка увеличивает счётчик
проверок; провал увеличивает счётчик провалов и запоминает сообщение.

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
Пост: провал, если `ABS(actual - expected) > eps`. При `eps <= 0` или `expected = 0` сравнение
точное.

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
Команда; аргументы описаны в разделе 6.
Пост: итог напечатан. С ключом `quit` процесс завершён кодом 1 при провалах или пустых тестах, иначе
кодом 0.
