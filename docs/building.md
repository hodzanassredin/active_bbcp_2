# Сборка и запуск

Практическое руководство: как собрать мир, запустить команду, прогнать тесты, пересобрать
компилятор, подготовить 32-битный тулчейн и вернуться к ядру без задач. Все команды
выполняются из корня репозитория.

## 1. Что нужно

| Что | Зачем | Без него |
|---|---|---|
| Linux x86-64 | основная цель | — |
| `dev0Linux` в корне | загрузочный компилятор и линковщик | сборка невозможна |
| `cc`, `python3` | `libtest64.so`, сравнение бутстрапа | часть тестов и самосборка |
| GTK4 | окно `blackbox` | работает только консоль |
| `wine` | Windows x64 | `tools/test-win64.sh` печатает `SKIP` |
| 32-битный C-тулчейн | `libtest32.so` | сопрограммы i386 пропускаются |

## 2. Каталоги

| Каталог | Что в нём | Переопределяется |
|---|---|---|
| `.work/BB64LIN` | мир x64, `blackbox-console`, `blackbox`, `libtest64.so` | `BB64INSTALL` |
| `build/BB32LIN` | мир i386 и `blackbox-console` | `BB32INSTALL` |
| `.work/i386` | база i386 для быстрого цикла и бутстрапа | — |
| `build/BB64WIN` | мир Windows x64 (под wine) | — |
| `.work/wine64` | префикс wine | — |
| `.work/i386dev` | распакованный 32-битный C-тулчейн | `BB_I386DEV` |

`tools/env.sh` задаёт `BB` (корень), `INSTALL` (`.work/BB64LIN`), `INTERP`
(`blackbox-console`) и `DEV0` (`dev0Linux`).

## 3. Сборка Linux x64

```sh
tools/build64.sh                  # все подсистемы, консоль и GUI
tools/build64.sh --clean          # стереть .ocf/.osf установки
tools/build64.sh --console-only   # только blackbox-console
tools/build64.sh System Lin       # выборочно, поверх готовой установки
```

`build64.sh`:

1. Включает `DevLibrarian.EnableDevLib('<INSTALL>', 'Mod64')`: исходник берётся из
   `<Sub>/Mod64`, иначе из `<Sub>/Mod`; `Code` и `Sym` пишутся в установку.
2. Компилирует подсистемы `Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket Unit`
   через `DevCompiler64.CompileSubs @Lin`.
3. Отдельно компилирует `Taskarch` и `Tasks`.
4. Проверяет журнал на `error:`, `N error(s) detected`, `~TRAP`, `syntax error`, `not found`.
5. Линкует консоль `Kernel+ LinKernel Files LinFiles StdLoader LinIntInit` и, если не указан
   `--console-only`, окно `Kernel+ LinKernel Files LinFiles StdLoader LinLoader`.
6. Копирует `Rsrc` подсистем и каталоги `Docu` в установку.

Линковка обязана напечатать `ok`. `CompileSubs` разрешает зависимости сам, поэтому первый
прогон должен быть полным списком, а не одной подсистемой.

## 4. Запуск

```sh
echo 'StdLog.String("hi"); StdLog.Ln' | tools/run64.sh
echo 'System_devTasksObj.TestMutualExclusion' | tools/run64.sh
printf 'UnitRunner.Run System_devMain proc quit\n' | tools/run64.sh
tools/run64.sh commands.txt
```

`run64.sh` запускает `<INSTALL>/blackbox-console` из каталога установки с
`LD_LIBRARY_PATH=<INSTALL>`; `StdLoader` ищет `<Подсистема>/Code` рядом с процессом. Команды
читаются со stdin.

Внешние переменные передаются как `--ИМЯ=значение`:

| Переменная | Смысл |
|---|---|
| `BB_COMPILE_TIME=ГГГГ-ММ-ДД ЧЧ:ММ:СС` | фиксированная метка времени в `.ocf` |

Параметры команде читаются из `DevCommanders.par`; пример — `DemoParameters.Do`:

```sh
echo 'DemoParameters.Do some parameters' | tools/run64.sh
```

Окно запускается только с GTK4:

```sh
cd .work/BB64LIN && LD_LIBRARY_PATH=. ./blackbox
```

`blackbox --console` консольный режим не выбирает: консоль — отдельный бинарник
`blackbox-console`.

## 5. Быстрый цикл

```sh
tools/go64.sh System_devBasic Kernel   # amd64: DevCompiler64.CompileThis
tools/go32.sh DevCPE DevCPT            # i386: DevCompiler.CompileThis
```

`go64.sh` пишет в `.work/BB64LIN`, `go32.sh` — в `.work/i386`. Оба снимают завершающий `^` с
имени модуля и передают его как ключ точек уступки. Первый `go32.sh` собирает базу i386
(`DevCompiler.CompileSubs @Lin`), если её нет.

Модули, которые линкуются в `blackbox-console` (`Kernel`, `LinKernel` и их импорты, в том числе
`Tasks`), загрузчик берёт из исполняемого файла, а не из `Code`: после `go64.sh` такого модуля
нужна перелинковка (`tools/build64.sh --console-only`). Какие модули грузятся с диска, показывает
`strace -e trace=openat` вокруг `tools/run64.sh`.

Файл `tools/env.sh:clean_host_temp` перед запуском `dev0Linux` удаляет из `/tmp` имена `odc` с
пятью цифрами. Готовый `dev0Linux` содержит `LinFiles`, который создаёт такие имена и перебирает
не более 200 вариантов; накопленный блок имён запирает конвертацию `.cp`. Живой процесс держит
свой файл по дескриптору, поэтому удаление ему не мешает.

## 6. Тесты

```sh
tools/test.sh                    # все модули из tools/tests.txt
tools/test.sh --list
tools/test.sh System_devMain     # выборочно
tools/unit.sh Check_devMain --marks proc --compile "CheckMath64 CheckRealFormat64 CheckCCallAlias"
```

`tools/tests.txt` — манифест в формате `<модуль>|<marks>|<таймаут>|<доп. модули>|<опции>`.
`tools/test.sh` для каждого модуля зовёт `tools/unit.sh`: тот собирает `libtest64.so`,
компилирует `Unit` и зависимости (до трёх проходов), запускает отдельный `blackbox-console`
командой `UnitRunner.Run <модуль> <marks> quit` и требует итог `failed 0` и код 0.

Правила тестов — `Unit/Docu/ru/Testing.md`. Тесты лежат в подсистемах `<Sub>_dev`; поставочная
сборка их не включает.

### 6.1. 32-битная цель

```sh
tools/build32.sh              # мир и консоль в build/BB32LIN
tools/test32.sh               # Unit_devMain, Files, Tasks, CoTests, UnloadGuard, OpenArr, PtrInit
tools/test32.sh --list
tools/test32.sh System_devTasks   # только указанные модули (мир не собирается)
```

`build32.sh` компилирует `Mod`-исходники компилятором `DevCompiler` и линкует ELF32
`blackbox-console` (`Kernel+ LinKernel Files LinFiles StdLoader LinIntInit`); `Tasks`
компилируется отдельным `CompileThis`. `test32.sh` компилирует тестовые модули по требованию и
запускает каждый отдельным процессом.

### 6.2. Сопрограммы i386

```sh
FLAGS=$(tools/i386-toolchain.sh)
gcc $FLAGS -shared -fPIC -O2 -o build/BB32LIN/libtest32.so tools/test64.c
```

`tools/i386-toolchain.sh` без sudo и установки в систему:

1. Скачивает `apt-get download libc6-dev-i386` и `lib32gcc-<N>-dev` в `.work/i386dev/debs`.
2. Распаковывает их `dpkg-deb -x` в `.work/i386dev/root`.
3. Правит путь к `libc_nonshared.a` внутри `root/usr/lib32/libc.so`.
4. Проверяет сборку и запуск пробника с `ucontext` и печатает строку флагов gcc.

Нет сети, пакета или 32-битного рантайма — `SKIP i386-toolchain: причина`, пустой stdout и код 0.
`build32.sh` и `test32.sh` тогда пропускают `libtest32.so` и `System_devCoTests`.

### 6.3. Windows x64 под wine

```sh
tools/build64.sh                       # нужен готовый мир x64
tools/test-win64.sh                    # сборка @Win и прогоны
tools/test-win64.sh --rebuild          # пересобрать каркас @Win
```

Сценарий собирает подсистемы `@Win` в `build/BB64WIN`, компилирует `Unit`, `Taskarch`, `Tasks`,
`WinIntInit`, `WinInit`, `WinKernel` и тесты, линкует консольный PE
`Kernel+ WinKernel Files WinFiles StdLoader WinIntInit` и переключает подсистему PE с GUI на
console. Под wine идут `Unit_devMain` (7/7), `System_devWinTasks` (5/5), `Dev_devOpenArr` (8/8)
и `Dev_devPtrInit` (7/7). Нет wine — `SKIP`.

### 6.5. Негативные проверки компилятора

```sh
tools/check-reject.sh
```

Сценарий по очереди кладёт фикстуры `Dev_dev/Reject/*.cp` на путь, который выберет
`DevLibrarian`, компилирует их и ждёт `error:`. Проверяются обычные правила языка на `Tasks.Ctx`:
переменная `Ctx` вне `Tasks`, копия пропуска и вызов `Exclusive` снаружи.

### 6.6. Замер сборщика

```sh
tools/bench-gc.sh System_devGcBench.DoSmall   # 1 и 10 млн
tools/bench-gc.sh System_devGcBench.Do        # с 50 млн
```

Сценарий собирает `libtest64.so` и `System_devGcBench`, запускает команду под `/usr/bin/time -v`
и печатает строки `BENCH`, RSS и время. Тайм-аут — `BENCH_TIMEOUT`, по умолчанию 1800 с.

## 7. Самосборка компилятора

```sh
tools/go32.sh DevCPE   # один раз: создать базу .work/i386, если её нет
tools/bootstrap.sh
```

`bootstrap.sh` делает три прохода:

1. A — упакованный `dev0Linux` собирает весь `Dev` по `.cp`.
2. B — собранный компилятор собирает `Dev` с `BB_COMPILE_TIME="2000-01-01 00:00:00"`.
3. C — компилятор из B собирает `Dev` с той же меткой.

`DevCPE` пишет эту метку в каждый `.ocf` вместо текущего времени, поэтому B и C обязаны совпасть
побайтно. `tools/bootstrap-compare.py` сравнивает `.ocf` и `.osf`; результат B устанавливается в
`Dev/Code` и `Dev/Sym`. Прежний `Dev/Code` при ошибке возвращается на место. Бутстрап собирает
только `Dev`; символы базовых модулей он берёт из `.work/i386`, поэтому база нужна заранее.

## 8. Параллельные рабочие копии

Две задачи удобно вести в двух копиях репозитория: в одной идёт долгая сборка или замер, в другой
правится код. Копию делает `git worktree add`; она делит с основной историю git, но не каталог
`.work`. Собранного компилятора в новой копии нет: `Dev/Code` в git не хранится. Если сразу
запустить `tools/build64.sh`, мир соберёт загрузочный компилятор из `dev0Linux` без исправлений
генератора. Поэтому первой идёт самосборка `tools/bootstrap.sh` (раздел 7), и только за ней —
`tools/build64.sh`.

Большие общие инструменты — база i386 и 32-битный C-тулчейн — копировать незачем. Новая копия
ссылается на каталоги основной: `.work/i386`, `.work/i386dev`. Мир `.work/BB64LIN` у каждой копии
свой.

Все шаги делает один сценарий:

```sh
tools/worktree.sh ../active_bb-x feature-x          # новая ветка от HEAD
tools/worktree.sh ../active_bb-x feature-x active2  # от другой базы
```

Он создаёт копию и ветку (существующую ветку берёт как есть), ставит ссылки на те инструменты,
которые есть в основной копии, выполняет `tools/bootstrap.sh` и `tools/build64.sh` и печатает
каталог, ветку и список ссылок; журнал сборки — `.work/worktree.log` новой копии. Копию убирают
командами `git worktree remove --force <каталог>` и `git branch -D <ветка>`; ключ `--force` нужен,
потому что в копии остались собранные файлы.

Сборки в двух копиях нельзя запускать с общим `/tmp`. Хост `dev0Linux` пишет текст исходника во
временный файл `/tmp/odcNNNNN`, а `tools/env.sh` перед каждым запуском хоста убирает такие файлы
(раздел 5). Вторая сборка стирает или занимает файлы первой, и компилятор первой получает пустой
текст: в журнале появляется `MODULE expected`, и `CompileSubs` обрывается посреди списка. Поэтому
в параллельных копиях каждую команду `tools/*` запускают со своим `/tmp`:

```sh
tools/iso.sh tools/build64.sh
tools/iso.sh sh -c "echo 'System_devTasksObj.TestMutualExclusion' | tools/run64.sh"
```

`tools/iso.sh` — то же, что `unshare -rm sh -c 'mount -t tmpfs tmpfs /tmp && …'`: команда
получает своё пространство имён монтирования с пустым `tmpfs` на `/tmp`. Привилегий для этого не
нужно, а по окончании команды её `/tmp` исчезает вместе с мусором хоста. `tools/worktree.sh` сам
запускает самосборку и сборку через `tools/iso.sh`.

## 9. Возврат к ядру без задач

Мир без задач получается из снимка `f03eaab7`. Порядок:

1. Создайте отдельное дерево: `git worktree add --detach .work/rollback f03eaab7`.
2. Соберите его обычным способом: `tools/build64.sh`.
3. Либо в текущем дереве верните файлы снимка: `git checkout f03eaab7 -- Dev/Mod`, `System/Mod64/Kernel.cp`,
   `System/Mod/Kernel.cp`, `Lin/Mod64/Kernel.cp`, `Lin/Mod/Kernel.cp`, `Win/Mod64/Kernel.cp`,
   `System/Mod/Loop.cp`, `System/Mod/Services.cp`, `System/Mod/Console.cp`, `Lin/Mod/Console.cp`,
   `Cons/Mod/Interp.cp`; удалите `System/Mod/Tasks.cp`, `System/Mod/Taskarch.cp`,
   `System/Mod64/Taskarch.cp`, `System/Mod/Poller.cp` и тестовые подсистемы `*_dev`, `Unit`.
4. Проведите `tools/bootstrap.sh` и соберите мир списком без `Unit`:

```
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket
DevLinker1.LinkElfExe64 '<каталог>/blackbox-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit
```

`Cons/Interp.cp` возвращается к снимку вместе с остальными: он зовёт `Console.WaitInput`, которой
в исходной консоли нет. Формат `.ocf`/`.osf` при этом не меняется; правки компилятора (слоты
указателей и длины открытых массивов) можно оставить — они исправляют ошибки генератора, а не
добавляют язык.
