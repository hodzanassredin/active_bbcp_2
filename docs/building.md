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
| `gtk4-broadwayd` (`libgtk-4-bin`) | окно без экрана пользователя | `tools/test-gui.sh` и `tools/test-demo-gui.sh` печатают `SKIP` |
| `wine` | Windows x64 | `tools/test-win64.sh` печатает `SKIP` |
| 32-битный C-тулчейн | `libtest32.so` | сопрограммы i386 пропускаются |
| MPICH в `.work/venv` (`uv pip install mpich`) | `tools/test-mpi.sh` | тест печатает `SKIP` |
| `unshare`, `ip`, `mount` | `tools/test-resolver.sh` | тест печатает `SKIP` |
| компилятор Go | `tools/bench-go.sh` | ориентир Go не снимается |

## 2. Каталоги

| Каталог | Что в нём | Переопределяется |
|---|---|---|
| `.work/BB64LIN` | мир x64, `blackbox-console`, `blackbox`, `libtest64.so` | `BB64INSTALL` |
| `build/BB32LIN` | мир i386 и `blackbox-console` | `BB32INSTALL` |
| `.work/i386` | база i386 для быстрого цикла и бутстрапа | — |
| `build/BB64WIN` | мир Windows x64 (под wine) | — |
| `.work/wine64` | префикс wine | — |
| `.work/i386dev` | распакованный 32-битный C-тулчейн | `BB_I386DEV` |
| `.work/venv` | MPICH для `tools/test-mpi.sh` | — |
| `.work/mpi` | `libbbmpi.so` для теста MPI | — |
| `.work/gtk4-bin` | `gtk4-broadwayd` для GUI-тестов | — |
| `.work/go` | компилятор Go для `tools/bench-go.sh` | `GO` |
| `.work/go-cache` | кеш сборки Go | — |

`tools/env.sh` задаёт `BB` (корень), `INSTALL` (`.work/BB64LIN`), `INTERP`
(`blackbox-console`) и `DEV0` (`dev0Linux`).

## 3. Сборка Linux x64

```sh
tools/build64.sh                  # все подсистемы, консоль, GUI и справка .odc
tools/build64.sh --clean          # стереть .ocf/.osf установки
tools/build64.sh --console-only   # только blackbox-console
tools/build64.sh --no-help        # без преобразования Docu/*.md в .odc
tools/build64.sh System Lin       # выборочно, поверх готовой установки
```

`build64.sh`:

1. Включает `DevLibrarian.EnableDevLib('<INSTALL>', 'Mod64')`: исходник берётся из
   `<Sub>/Mod64`, иначе из `<Sub>/Mod`; `Code` и `Sym` пишутся в установку.
2. Компилирует подсистемы `Lin System Std Cons Text Fig Form Dev Comm Cas Obx Xhtml Sql Paket Unit`
   через `DevCompiler64.CompileSubs @Lin`. `Demo` из списка исключена: её модули импортируют
   `Async` и `Kv`, которые собираются позже.
3. Отдельно компилирует `Taskarch` и `Tasks`, затем `LinThreads` и `LinNative` — они нужны
   библиотеке `Async`.
4. Компилирует `Async` (`CompileSubs`) и модули `AsyncComputon`, `AsyncMpi`, `AsyncWork`,
   `AsyncWorkLin` из `Async/Mod64`; затем `Robust`, `Kv`, `Actor` и `Raft` в этом порядке.
5. Компилирует примеры: каждый модуль `Obx/Mod64` и список модулей `Demo/Mod` по одному, потому
   что они импортируют собранные выше библиотеки.
6. Проверяет журнал на `error:`, `N error(s) detected`, `~TRAP`, `syntax error`, `not found`.
7. Линкует консоль `Kernel+ LinKernel Files LinFiles StdLoader LinIntInit` и, если не указан
   `--console-only`, окно `Kernel+ LinKernel Files LinFiles StdLoader LinLoader`.
8. Копирует `Rsrc` подсистем и каталоги `Docu`, `Obx/Docu`, `Demo/Docu`, `Lin/Docu`, `Unit/Docu`,
   `Cas/Docu`, `Actor/Docu`, `Raft/Docu` в установку.
9. Если собран `Obx` и не указан `--no-help`, зовёт `tools/gui-world.sh`: раскладывает
   `Docu/*.md` и `Mod/*.cp` в установке как `.odc` с разметкой и ссылками и проверяет
   `Obx/Docu/Sys-Map.odc` через `Views.OldView`. Отдельный запуск — `tools/gui-world.sh`
   после готовой сборки.

Линковка обязана напечатать `ok`. `CompileSubs` разрешает зависимости сам, поэтому первый
прогон должен быть полным списком, а не одной подсистемой. Ключ с подсистемами сужает только
основной список: библиотеки `Async`, `Robust`, `Kv`, `Actor`, `Raft` и примеры `Obx/Mod64`,
`Demo` собираются отдельными вызовами при наличии их каталогов.

## 4. Запуск

```sh
echo 'StdLog.String("hi"); StdLog.Ln' | tools/run64.sh
echo 'System_devTasksObj.TestMutualExclusion' | tools/run64.sh
echo 'ObxTasks0.Do' | tools/run64.sh
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
| `ROBUST_SIZE=N` | размер мира `RobustParallel` |
| `ASYNC_TRANSPORT=pipe\|tcp\|mpi` | транспорт компутонов |
| `ASYNC_TCP=адрес` | адрес TCP-исполнителя компутонов |

Параметры команде читаются из `DevCommanders.par`; пример — `DemoParameters.Do`:

```sh
echo 'DemoParameters.Do some parameters' | tools/run64.sh
```

Окно запускается только с GTK4:

```sh
cd .work/BB64LIN && LD_LIBRARY_PATH=. ./blackbox
```

Тексты для `Help → Examples` готовит `tools/gui-world.sh`: он копирует `Docu` и исходники
подсистем в установку и кладёт рядом с каждым текстом `.odc` командой `Dev_devOdc.All`.

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

В манифест входят тесты ядра и `Tasks` (`System_dev*`, `Unit_devMain`), библиотеки `Async`,
хранилищ `Cas` и `Kv`, акторов и Raft (`Actor_dev*`, `Raft_dev*`), примеров (`Demo_devExamples`).
Модули `*_dev` не входят в поставочную сборку: их собирает `tools/unit.sh` по требованию.

Правила тестов — `Unit/Docu/ru/Testing.md`. Тесты лежат в подсистемах `<Sub>_dev`; поставочная
сборка их не включает.

### 6.1. 32-битная цель

```sh
tools/build32.sh              # мир и консоль в build/BB32LIN
tools/test32.sh               # Unit_devMain, задачи, Cas, Kv, CoTests, UnloadGuard, OpenArr, PtrInit
tools/test32.sh --list
tools/test32.sh System_devTasks   # только указанные модули (мир не собирается)
```

`build32.sh` компилирует `Mod`-исходники компилятором `DevCompiler` и линкует ELF32
`blackbox-console` (`Kernel+ LinKernel Files LinFiles StdLoader LinIntInit`); `Tasks` и `AsyncStm`
компилируются отдельными `CompileThis`, `Kv` — отдельным `CompileSubs`. `test32.sh` компилирует
тестовые модули по требованию и запускает каждый отдельным процессом.

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
`AsyncWinSock`, `AsyncWork`, `AsyncWorkWin`, `WinIntInit`, `WinInit`, `WinKernel`, `AsyncProto`,
`AsyncSelect`, `AsyncChan`, `AsyncStm` и тесты, линкует консольный PE
`Kernel+ WinKernel Files WinFiles StdLoader WinIntInit` и переключает подсистему PE с GUI на
console. Под wine идут `Unit_devMain` (7/7), `System_devWinTasks` (5/5), `Async_devWinWorkers`
(4/4), `Async_devStm` (19/19), `Dev_devOpenArr` (8/8) и `Dev_devPtrInit` (7/7). Нет wine — `SKIP`.

### 6.4. Окно без экрана пользователя

```sh
tools/test-gui.sh          # задачи и монитор-объект в главном цикле окна
tools/test-demo-gui.sh     # DemoAnts: 1000 задач и таймер в окне
```

Оба сценария поднимают `gtk4-broadwayd` в своём `XDG_RUNTIME_DIR`, запускают `blackbox` с
`--BB_RUN=<команда>` и проверяют итоговую строку `ok=1`. `DISPLAY` пользователя не используется,
мышь не эмулируется. Нет `gtk4-broadwayd` — `SKIP`.

### 6.4. Негативные проверки компилятора

```sh
tools/check-reject.sh
```

Сценарий по очереди кладёт фикстуры `Dev_dev/Reject/*.cp` на путь, который выберет
`DevLibrarian`, компилирует их и ждёт `error:`. Проверяются обычные правила языка на `Tasks.Ctx`:
переменная `Ctx` вне `Tasks`, копия пропуска и вызов `Exclusive` снаружи.

### 6.5. Замер сборщика

```sh
tools/bench-gc.sh System_devGcBench.DoSmall   # 1 и 10 млн
tools/bench-gc.sh System_devGcBench.Do        # с 50 млн
```

Сценарий собирает `libtest64.so` и `System_devGcBench`, запускает команду под `/usr/bin/time -v`
и печатает строки `BENCH`, RSS и время. Тайм-аут — `BENCH_TIMEOUT`, по умолчанию 1800 с.

### 6.7. Сверка примеров в документации

```sh
tools/check-examples.sh          # тексты примеров против файлов модулей
tools/check-examples.sh --run    # ещё и прогон страниц примеров
tools/check-links.py             # относительные ссылки в .md
```

Каждый блок, который начинается строкой `MODULE Имя;`, обязан совпадать с файлом
`<Sub>/Mod64/*.cp` или `<Sub>/Mod/*.cp`. С `--run` сценарий исполняет команду страницы
`Obx/Docu/*.md`, `Obx/Docu/ru/*.md` или `Demo/Docu/*.md` и сверяет её вывод с блоком
«В журнале появится:» (`The log shows:`). Нужна сборка `tools/build64.sh`.
`tools/check-links.py` проверяет относительные ссылки `[текст](путь)` во всех `.md`, кроме
`OLD/`, и печатает битые пары «файл:строка: путь».

### 6.8. Окно и справка

```sh
tools/gui-world.sh
```

Сценарий копирует `Docu` и исходники подсистем в установку и командой `Dev_devOdc.All` кладёт
рядом с каждым текстом `.odc`; после него `Help → Examples` открывает карту примеров. Нужны
собранный мир и GTK4.

### 6.9. Необязательные внешние условия

```sh
tools/test-mpi.sh         # MPI-транспорт компутонов, нужен MPICH в .work/venv
tools/test-resolver.sh    # резолвер без сети, нужны unshare, ip, mount
tools/test-robust.sh      # фасад RobustParallel на 1, 4 и 16 процессах
tools/bench-go.sh         # ориентир Go для задач и каналов
```

`tools/test-mpi.sh` и `tools/test-resolver.sh` печатают `SKIP` с причиной, если условия не
выполнены, и выходят с кодом 0. `tools/test-robust.sh` требует собранного мира и без него
завершается ошибкой; `tools/bench-go.sh` требует компилятора Go.

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

Большие общие инструменты — база i386, 32-битный C-тулчейн, Go, GTK4 Broadway и MPICH в
`.work/venv` — копировать незачем. Новая копия ссылается на каталоги основной: `.work/i386`,
`.work/i386dev`, `.work/go`, `.work/gtk4-bin`, `.work/venv`, `.work/mpi`. Мир `.work/BB64LIN` у
каждой копии свой.

Все шаги делает один сценарий:

```sh
tools/worktree.sh ../active_bb-x feature-x          # новая ветка от HEAD
tools/worktree.sh ../active_bb-x feature-x main    # от другой базы
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
2. Соберите снимок прямо `dev0Linux`: в каталоге дерева выполните

```
DevCompiler64.InitLogMarks
DevLibrarian.EnableDevLib('<каталог>', 'Mod64')
DevCompiler64.CompileSubs @Lin Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket
DevLibrarian.DisableDevLib
DevLinker1.LinkElfExe64 '<каталог>/blackbox-console' := Kernel+ LinKernel Files LinFiles StdLoader LinIntInit
```

Снимок не содержит `tools/`: сценарии текущей ветки рассчитаны на мир с задачами.
3. Либо в текущем дереве верните файлы снимка: `git checkout f03eaab7 -- Dev/Mod`, `System/Mod64/Kernel.cp`,
   `System/Mod/Kernel.cp`, `Lin/Mod64/Kernel.cp`, `Lin/Mod/Kernel.cp`, `Win/Mod64/Kernel.cp`,
   `System/Mod/Loop.cp`, `System/Mod/Services.cp`, `System/Mod/Console.cp`, `Lin/Mod/Console.cp`,
   `Cons/Mod/Interp.cp`. Удалите `System/Mod/Tasks.cp`, `System/Mod/Taskarch.cp`,
   `System/Mod64/Taskarch.cp`, `System/Mod/Poller.cp`, тестовые подсистемы `*_dev`, `Unit`, а также
   подсистемы поверх задач: `Async`, `Actor`, `Cas`, `Kv`, `Raft`, `Robust` и `Obx/Mod64`. В
   `Demo/Mod` верните файлы снимка (`git checkout f03eaab7 -- Demo/Mod`) и удалите добавленные
   модули примеров.
4. Соберите мир командами шага 2 без `Unit`.

`Cons/Interp.cp` возвращается к снимку вместе с остальными: он зовёт `Console.WaitInput`, которой
в исходной консоли нет. Формат `.ocf`/`.osf` при этом не меняется; правки компилятора (слоты
указателей и длины открытых массивов) можно оставить — они исправляют ошибки генератора, а не
добавляют язык.
