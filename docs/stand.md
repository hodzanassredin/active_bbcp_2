# Стенд active2: снимок Ивана + этапы 1–2

`active2` — ветка от `ivan-dia261123` (снимок Ивана DIA261123 поверх
`b3e3b9d0`) с нашими этапами 1–6 и тестами. Исходники —
UTF-8 `.cp`, сборка — `dev0Linux`, консоль — `blackbox-console`
(`LinIntInit`). Ветка `active` не переписывалась и остаётся на `.odc`.

Разбор снимка и таблица изменений — `docs/ivan-dia261123.md`; замер —
`docs/gc-bench.md`.

## 1. Итог

| Что | Команда | Состояние |
|---|---|---|
| Сборка x64 из `.cp` | `tools/build64.sh` | работает; первый `--clean` с нуля, в т.ч. после залежей `/tmp/odcNNNNN` |
| Консольный запуск | `echo 'Команда' \| tools/run64.sh` | работает |
| Быстрый цикл | `tools/go64.sh`, `tools/go32.sh` | работает (`CompileThis`) |
| Тесты без окон | `tools/test.sh` (Unit) | 19 этапов, 87 проверок, все зелёные |
| Временные файлы `LinFiles` | `Test64Files` (заняты все `odcNNNNN`) | 3 теста, зелёные; имена «odc» + 9 цифр |
| i386: сборка и тесты | `tools/build32.sh`, `tools/test32.sh` | 8 этапов: UnitTests 4/4, Files 3/3, Tasks 14/14, Monitors 9/9, Active 6/6, Yield 3/3, CoTests 2/2, UnloadGuard 2/2 |
| i386: C-тулчейн | `tools/i386-toolchain.sh` | без sudo; libtest32.so, ucontext |
| Бутстрап i386 | `tools/bootstrap.sh` | три прохода, 98 файлов, B и C — 0 различий |
| Этап 1: очередь и цикл | `Test64Tests.TestProcActionTrap` | зелёный |
| Этап 1/3: GUI | `tools/test-gui.sh` (gtk4-broadwayd) | `ok=1`, 20 тиков, maxgap 108 мс |
| Этап 4: мониторы | `Test64Monitors` | 9 тестов, зелёные |
| Этап 6: `[exclusive]`/`AWAIT` | `Test64Active` | 6 тестов, зелёные |
| Этап 6: ключ уступок | `Test64Yield` | 3 теста, зелёные |
| Этап 7: каналы и выбор | `Test64Channels` | 9 тестов, зелёные |
| Этап 7: ввод-вывод | `Test64IO` | 2 теста, зелёные (1000 соединений) |
| Этап 8: рабочие нити | `Test64Workers` | 4 теста, зелёные |
| Этап 8: резолвер без сети | `Test64Resolver` (`tools/test-resolver.sh`) | 1 тест: 10 010 мс, 9 449 шагов цикла |
| Этап 9: компутоны | `Test64Computons` | 9 тестов, зелёные (1/4/16 процессов) |
| Этап 9: `RobustParallel` | `tools/test-robust.sh` | сумма 500500 на 1/4/16 процессах |
| Этап 9: MPI | `tools/test-mpi.sh` (mpiexec -n 5) | 12 квадратов, 252 мс |
| Этап 2: стеки | `Test64Tests.TestProcGcStack`, `TestProcTrapStack` | зелёные |
| Шаг 2а: индекс кластеров | `Test64Tests.TestProcInHeap` | зелёный |
| Замер 1/10/50 млн | `tools/bench-gc.sh Test64GcBench.DoSmall` / `.Do` | выполнен |
| Windows x64 | `tools/test-win64.sh` (wine) | UnitTests 4/4, Test64WinTasks 5/5, Test64WinWorkers 4/4 |

## 2. Раскладка

```
active_bb/                     репозиторий (ветка active2)
  <Подсистема>/Mod/*.cp        исходники (Mod64 — приоритетный вариант amd64)
  Unit/Mod/*.cp                UnitAssert, UnitRunner, UnitTests
  Test64/Mod/*.cp              сценарии Test64, Test64Tests, Test64Tasks
  System/Mod64/Tasks.cp        задачи со своим стеком (этап 3, amd64)
  System/Mod/Tasks.cp          то же для i386 (переключатель на x86)
  Test64/Mod64/Co.cp           сопрограммы amd64 (libtest64.so)
  Test64/Mod/Co.cp             сопрограммы i386 (libtest32.so)
  Test64/Mod64/CRaw.cp         внешние объявления tools/test64.c (amd64)
  Test64/Mod/CRaw.cp           то же для i386 (libtest32.so)
  Test64/Mod64/Clock.cp        наносекунды amd64 (clock_gettime)
  Test64/Mod/Clock.cp          время i386 по Kernel.Time (Tasks, GcBench)
  Async/Mod64/Work.cp          фасад пула рабочих нитей (этап 8)
  Async/Mod64/WorkLin.cp       делегат pthread/pipe/Poller (Linux)
  Async/Mod64/WorkWin.cp       делегат CreateThread/события (Windows)
  Async/Mod64/WinSock.cp       WS2_32: getaddrinfo для ResolveJob
  Async/Mod64/Computon.cp      компутоны: pipe/TCP/внешний транспорт (этап 9)
  Async/Mod64/Mpi.cp           MPI-транспорт через MPICH (этап 9)
  Robust/Mod64/Parallel.cp     фасад RobustParallel (этап 9)
  tools/                       стенд
  .work/BB64LIN/               каталог установки (в git не входит)
    <Подсистема>/{Code,Sym}    .ocf/.osf, собранные dev0Linux
    blackbox-console           консольный ELF64 (Kernel+ … LinIntInit)
    blackbox                   GUI ELF64 (Kernel+ … LinLoader), не запускаем
    libtest64.so               помощник тестов (ucontext)
  build/BB32LIN/               i386-мир (tools/build32.sh, в git не входит)
    blackbox-console           консольный ELF32 (Kernel+ … LinIntInit)
    libtest32.so               помощник тестов i386 (ucontext)
  .work/i386dev/               32-битный C-тулчейн без sudo (в git не входит)
    root/usr/lib32, root/usr/lib/gcc/…  libc6-dev-i386 и lib32gcc-N-dev
    flags.txt                  флаги gcc из tools/i386-toolchain.sh
```

`DevLibrarian.EnableDevLib('.work/BB64LIN', 'Mod64')` выбирает
`<Sub>/Mod64/*.cp`, иначе `<Sub>/Mod/*.cp`, и пишет Code/Sym в каталог
установки. `CompileSubs` компилирует подсистемы в порядке списка и сам
разрешает зависимости; поэтому первый прогон должен быть полным списком,
а не одной подсистемой. `dev0Linux` запускается из корня репозитория.

## 3. Сборка и запуск

```sh
tools/build64.sh                  # все подсистемы, консоль + GUI
tools/build64.sh --clean          # стереть .ocf/.osf установки
tools/build64.sh --console-only   # только blackbox-console
tools/build64.sh System Lin       # выборочно, поверх готовой установки

tools/build32.sh                  # i386-мир и консоль в build/BB32LIN
tools/build32.sh --clean

echo 'StdLog.String("hi"); StdLog.Ln' | tools/run64.sh
echo 'Test64Basic.Do' | tools/run64.sh
```

`build64.sh` проверяет журнал на `N error(s) detected`, `~TRAP`,
`syntax error`, `not found`; линковка обязана напечатать `ok`.
Каталог установки переопределяется `BB64INSTALL`, i386-мир —
`BB32INSTALL`. `build32.sh` линкует `DevLinker1.LinkElfExe` (ELF32,
`Kernel+ … LinIntInit`); отдельно компилирует `Tasks` и тесты `Test64`.

32-битный C-помощник `libtest32.so` собирается тем же `tools/test64.c`
флагами `tools/i386-toolchain.sh`: сценарий идемпотентно скачивает
`apt-get download libc6-dev-i386` и `lib32gcc-<N>-dev` (N — версия
системного gcc) в `.work/i386dev/debs`, распаковывает `dpkg-deb -x` в
`.work/i386dev/root` и правит `root/usr/lib32/libc.so` (путь к
`libc_nonshared.a` — внутри root). Установки в систему и sudo не нужно;
готовность проверяется пробником ucontext. Нет сети, пакета или
32-битного рантайма — `SKIP i386-toolchain: причина` (код 0, пустой
stdout), `build32.sh`/`test32.sh` тогда пропускают `libtest32.so` и
`Test64CoTests`, остальные тесты i386 идут.

### Временные файлы `LinFiles` (`/tmp/odc…`)

Прежняя причина падения первого `tools/build64.sh --clean` — временные
файлы хоста. Конвертер `.cp` в `dev0Linux` отдаёт текст через
`Files.dir.Temp()`: `LinFiles.GetTempFileName` строил имя «odc» + пять
цифр из `Libc.clock()`, а `LinFiles.CreateFile` перебирал не более 200
следующих имён. После `HALT` (обрыв `CompileSubs` по ошибке) и других
аварий файлы оставались в `/tmp`; когда накопленный сплошной блок
занимал больше 200 имён, временный файл не создавался, конвертер
возвращал пустой текст, компилятор печатал `MODULE expected`, а
`CompileSubs` обрывался. Обрыв оставлял новую порцию файлов, поэтому
дефект воспроизводился и зависел от `clock()` («второй проход
проходит»).

Исправлена **причина** в обеих целях (`Lin/Mod/Files.cp`,
`Lin/Mod64/Files.cp`):
- имя теперь «odc» + **9 цифр** (`tempName = "odcxxxxxxxxx"`, длина 12);
- стартовый номер — `clock() * 1000 + time() + tempSeq`, где `tempSeq`
  — счётчик процесса; совпадение с чужим именем лишь увеличивает номер;
- **предела 200 попыток больше нет**: `CreateFile` повторяет, пока
  `res = fileExistsErr`; `Delete` при переименовании в скрытое имя — так
  же;
- `Append(..., 255, ...)`: прежняя передача `12` включала в `Append`
  режим DOS 8.3 (`IF max = 12 THEN m := 8`) и обрезала имя до 8
  символов — это и запирало подбор;
- временный файл удаляют `CloseFile` (состояния `temp/new/hidden`) и
  `FINALIZE`; `Kernel.Quit` вызывает финализаторы, поэтому нормальный
  выход файлов не оставляет.

Красный/зелёный тест — `Test64Files` (`Test64/Mod/Files.cp`, marks
`proc slow`): занимает все 100000 имён `odc00000`…`odc99999` и требует,
чтобы временный файл всё равно создался, а после закрытия/финализации
в каталоге не осталось имён «odc» + 9 цифр. На старом коде оба
сценария падали (`expected 100001, got 100000`; `expected 1, got 0`),
на новом — 3/3.

`clean_host_temp` в `tools/env.sh` остаётся **только страховкой для
старого `dev0Linux`**: это готовый бинарник со старым `LinFiles`
внутри, и его запуски (`build64.sh`, `go64.sh`, `go32.sh`,
`bootstrap.sh`, `test-win64.sh`) по-прежнему пишут пятизначные имена.
Пересобранные `blackbox-console` (x64 и i386) работают на новом коде и
чистки не требуют. Удаление безопасно и при параллельной сборке: живой
процесс держит файл по дескриптору, а `CloseFile` получает `ENOENT` и
игнорирует его.

## 4. Быстрый цикл

```sh
tools/go64.sh Test64Basic Kernel   # amd64: DevCompiler64.CompileThis
tools/go32.sh DevCPE DevCPT        # i386: DevCompiler.CompileThis
```

Источник — `.cp`; `Mod64` приоритетнее. Вывод amd64 — в
`.work/BB64LIN/<Sub>/{Code,Sym}`, i386 — в `.work/i386/<Sub>/{Code,Sym}`
(чтобы i386 `.osf` не подмешивались в amd64-сборку). Первый `go32`
собирает i386-базу `DevCompiler.CompileSubs` по тем же `.cp`.
`DevOnce` из ветки `active` не перенесён: `Views.Old` в консольном x64 не
открывает `.cp`; `CompileThis` делает то же без промежуточного модуля.

## 5. Тесты

```sh
tools/test.sh              # все модули из Test64/tests.txt
tools/test.sh --list
tools/test.sh CheckTests   # выборочно
```

Тесты идут через подсистему `Unit` (`UnitAssert`, `UnitRunner`,
правила — `Unit/Docu/ru/Testing.md`). Манифест `Test64/tests.txt`:
`<модуль>|<marks>|<таймаут>|<доп. модули>`. `tools/test.sh` для каждого
модуля зовёт `tools/unit.sh`: тот собирает `libtest64.so`, компилирует
модуль и зависимости через `go64` (до трёх проходов), запускает
отдельный процесс `blackbox-console` командой
`UnitRunner.Run <модуль> <marks>` и требует итог `failed 0` и код 0.
TestProc-сценарии (ядро, сборщик, стеки, сигналы) помечены `proc` и
потому не уносят остальные модули.

Результат: **19 этапов, 87 проверок, 0 провалов** (манифест — 15
модулей, 81 тест; остальные — отдельные сценарии) —
- `Test64Files` (marks=proc slow): заняты все имена `odc00000`…`odc99999`;
  временный файл создаётся (красный на старом коде), после
  создания/закрытия и после финализатора имён «odc» + 9 цифр не
  остаётся (см. §3);
- `Test64Tests` (marks=proc): Basic, Heap, Trap, ActionTrap, GcStack,
  TrapStack, InHeap (сценарии `Test64*`, точки входа в
  `Test64/Mod/Tests.cp`);
- `Test64Tasks` (marks=proc slow): 14 тестов задач — Yield,
  производитель-потребитель, философы, отмена, изоляция трапа,
  проверки ожидания, группа, действие планировщика, нагрузка
  1 тыс. и 10 тыс. задач, уборка списка `all` в Reap после 10 тыс.
  коротких задач (`Test64/Mod/Tasks.cp`, см. `docs/tasks.md`);
- `UnitTests`: самопроверка Unit, включая вложенный прогон
  `UnitTestsProbe` (PASS/FAIL/EMPTY/SKIP);
- `Test64Active` (marks=proc): 6 тестов этапа 6 — AWAIT в методе,
  в процедуре модуля и с локальным условием, ложное условие не будит,
  результаты всех видов, RETURN из середины, снятие при трапе и во
  вложенном Try (`Test64/Mod/Active.cp`);
- `Test64Yield` (marks=proc, ключ `^`): 3 теста — счётный цикл
  уступает другим задачам, не уступает в мониторе и вне задачи,
  отмена срабатывает в точке уступки (`Test64/Mod/Yield.cp`);
- `CheckTests` (marks=proc): три пробы Ивана — RealFormat, Math,
  CCallAlias;
- `Test64Channels` (marks=proc): 9 тестов этапа 7 — Once (в т.ч.
  повторный Fill), встреча на 100 сообщениях, ограниченный буфер,
  почтовый ящик с `WITH`, Select, тайм-аут, отсутствие протухших
  предложений, отправка в полный буфер;
- `Test64IO` (marks=proc): эхо-сервер и HTTP-ответ на localhost;
  1000 одновременных соединений, при простое — 7–50 мс CPU за 1 с;
- `Test64Workers` (marks=proc): медленный вызов в рабочей нити не
  замораживает цикл (300 мс, 285 шагов), `getaddrinfo`, файловый
  обмен;
- `Test64Computons` (marks=proc): 12 квадратов на 1, 4 и 16 локальных
  процессах, повтор после `SIGKILL` исполнителя (414 мс), ошибка `Do`, альтернатива `AltPull` в `Select`;
- `Test64Protocol` (marks=proc): 6 тестов этапа 10 — корректный диалог,
  нарушение порядка, канал без протокола, отказ разбора описания,
  правильная и нарушающая порядок отправка через `Select` (`SendAlt`);
- `Test64Resolver` (marks=proc, отдельный сценарий): в `unshare -rmn`
  с dummy-интерфейсом и своим resolv.conf несуществующее имя ждёт
  тайм-аут резолвера (10 010 мс) в рабочей нити, главный цикл делает
  9 449 шагов; без `unshare`/`ip`/`mount` — `SKIP` с причиной;
- `Test64RobustParallel` (отдельный сценарий): клиент, написанный
  только против определения `RobustParallel`, считает сумму 1..1000
  на 1, 4 и 16 процессах (`ROBUST_SIZE`); все три прогона дают 500500;
- `Test64Mpi` (отдельный сценарий): 12 квадратов на 4 исполнителях
  через `mpiexec -n 5` (MPICH 5.0.2 из `.work/venv`), 252 мс;
- `Test64Gui` (отдельный сценарий): GTK4-хост под `gtk4-broadwayd`,
  `--BB_RUN=Test64Gui.Run`; производитель-потребитель на
  `[exclusive]`+`ACTIVE.AWAIT` и тикер 100 мс: `ok=1`, 20 тиков,
  maxgap 108 мс, 100 элементов, сумма 5050;
- `Test64Unload` (marks=proc): перекомпиляция `Test64UnloadCode`,
  выгрузка у исполнителей и загрузка новой версии; отказ для занятых
  модулей;
- `Test64UnloadGuard` (marks=proc): модуль-мишень загружен через `Meta`
  (refcnt = 0) и не выгружается, пока его кадр на главном стеке или на
  стеке спящей задачи; без кадров — выгружается (`Test64/Mod/UnloadGuard.cp`);
- `Test64WinWorkers` (отдельный сценарий `tools/test-win64.sh`): под
  wine — медленный `Sleep(300)` в рабочей нити (задача ждёт, главный
  цикл делает шаги), запись/чтение файла, `getaddrinfo("localhost")` и
  4 нити пула (`Test64/Mod/WinWorkers.cp`).

### i386 (32-битная цель)

```sh
tools/build32.sh              # мир и консоль в build/BB32LIN
tools/test32.sh               # сборка + UnitTests, Files, Tasks, Monitors, Active, Yield, CoTests, UnloadGuard
tools/test32.sh --list
tools/test32.sh Test64Tasks   # только указанные модули (без сборки)
```

`build32.sh` компилирует `DevCompiler`-ом из `dev0Linux` подсистемы
`Mod` в `build/BB32LIN` и линкует `blackbox-console` через
`DevLinker1.LinkElfExe` (ELF32, `Kernel+ … LinIntInit`). Отдельно
компилируются `Tasks` и тесты `Test64`: `CompileSubs` спотыкается о
псевдомодуль `ACTIVE`, а `Test64Yield` нужен ключ `^`. `test32.sh`
запускает каждый модуль отдельным процессом и требует `failed 0` и
хотя бы один `PASS` (иначе все `SKIP` прошли бы как успех).

На i386 работают этапы 2–6: реестр стеков и `Kernel.Try` на стеке
(`System/Mod/Kernel.cp`), мониторы `Lock/Unlock/Wait/Await`, `Poll`,
`CallCond` (кадр в `BX`), `Tasks` со своим стеком (mmap + сторожевая
страница в `LinKernel`), `[exclusive]` (пролог/эпилог `CPC486`),
`ACTIVE.AWAIT` и ключ `^`. Тест `Test64Files` и временные имена
проверены и на i386. `Test64UnloadGuard` подтверждает и на i386, что
`Kernel.UnloadMod` не выгружает модуль с живым кадром на главном стеке
и на стеке спящей задачи (положительный контроль — без кадров модуль
выгружается). Сопрограммы этапа 2 проверяются и на i386: обёртки —
`Test64/Mod/Co.cp` над `libtest32.so` (тот же `tools/test64.c`;
собирается `tools/build32.sh` флагами `tools/i386-toolchain.sh`, см.
§3). Точки входа Unit — `Test64CoTests` (`TestProcGcStack`,
`TestProcTrapStack`): сборка мусора на втором стеке и изоляция
контекста `Kernel.Try` между стеками. `co_now` на i386 остаётся по
`Kernel.Time` — для `Test64Tasks` этого достаточно; отдельный
`Test64Clock` не даёт задачам тянуть C-библиотеку. Без 32-битного
тулчейна `test32.sh` пропускает только `Test64CoTests` (SKIP с
причиной). Для amd64 обёртки лежат в `Test64/Mod64/Co.cp`.

`Test64Support` остался адаптером: `Check`/`Fail` учитываются в
`UnitAssert`, маркеры `TEST64-PASS/FAIL` сохранены для `bench-gc.sh`.
`Test64Pump` и `Test64GcBench` в манифест не входят; замер —
`tools/bench-gc.sh`.

### Ловушка консоли

Консольный init не ставит `Kernel.trapViewer`, поэтому при
`trapViewer = NIL` любой трап фатален даже внутри `Kernel.Try`.
`Test64Support.Begin` ставит пустой viewer — это дефект каркаса, не ядра
теста. В норме viewer должен ставить консольный init.

## 6. Этапы 1–6 на `.cp`

- `System/Mod/Services.cp`: `Work`, `Post`, `Wake`, `NextDeadline`,
  `SetWakeHook`; действие, трапнувшее в `Do`, в очередь не
  возвращается (как в исходном BlackBox — это способ остановить
  сломанное действие); флаг `executingAction` снимает trap-cleaner.
- `System/Mod/Console.cp` + `Lin/Mod/Console.cp`: `WaitInput(timeout)`;
  `LinConsole` использует `poll` на stdin и `setvbuf(_IONBF)`.
- `Cons/Mod/Interp.cp`: между командами и во время ожидания ввода
  вызывает `Services.actionHook.Step`.
- `System/Mod64/Kernel.cp`: реестр `Stack`, `RegisterStack`,
  `RegisterSignalStack`, `UnregisterStack(At)`, `StackOf`,
  `InRegisteredStack`, `CurrentStack`, `SwitchSave/SwitchRestore`;
  `MarkLocals` сканирует текущий стек от FP и спящие от SP;
  `UnloadMod` не выгружает модуль, если его код есть в кадрах.
  Обход кадров в `FrameInModule`: текущий стек — от `FP`, спящий — от
  `Stack.fp` (его пишет `SwitchSave` — FP **вызывающего**, потому что
  свой кадр к моменту сырого переключения уже мёртв, а область
  сохранения регистров затирает его слова); выравнивание проверяется
  по 4, а не по 8 — кадры CP пакуются по 4 и бывают ≡4 (mod 8).
  Тот же обход (в т.ч. отложенный FP прерванного стека при трапе)
  используют `GetRefFrame` и `UnloadMod`.
- `Lin/Mod64/Kernel.cp`: `Get/SetTryContext` поверх
  `currentTryContext`, `InActivationStack` учитывает зарегистрированные
  стеки, сигнальный стек регистрируется после `Kernel.SetPlatform`.
- Шаг 2а: `System/Mod64/Kernel.cp` — индекс кластеров для `InHeap`
  (`IndexInsert/IndexRemove`); наружу из диагностики оставлена одна
  `ClusterIndexCheck`, счётчик `AllocCount`; `System/Mod/Loop.cp` —
  пропуск периодической сборки без выделений, диагностика `Loop.Stats`.
- Этап 3: `System/Mod64/Tasks.cp` — задачи и [code]-переключатель;
  `Kernel.AllocStack/FreeStack` и `Platform.AllocateStackMem`
  (в `LinKernel` — mmap + сторожевая страница); индекс `Kernel.StackOf`;
  `Services.executingAction`/`executing` и trap-cleaner;
  `Models.InDomainCast`. Подробности — `docs/tasks.md`.
- Этап 4: `System/Mod64/Kernel.cp` — побочная таблица мониторов по
  адресу объекта (`Lock/Unlock/Wait/Await`, `IsLocked`,
  `WaitersCount`, `DumpMonitors`), цепочка взятий у стека,
  `Kernel.Try` снимает до своей глубины, сборщик держит записи и
  объекты; `Tasks.SetScheduler`, передача владения спящей задаче,
  журнал тупика. Подробности — `Docu/Async.md`.
- Этап 5: `Docu/Async.md` и `Docu/ru/Async.md` — описание расширения.
- Этап 6: компилятор (`Dev/Mod`): псевдомодуль `ACTIVE` и
  `ACTIVE.AWAIT` (скрытая процедура условия `@awaitN`, кадр в `BX`
  через `Kernel.CallCond`), `[exclusive]` (пролог/эпилог
  `Kernel.Lock/Unlock`, монитор модуля), ключ точек уступки `^`
  (`Kernel.Poll` на обратном переходе циклов), скрытый импорт `"$$"`.
  Проверки: `tools/bootstrap.sh` (два прохода `Dev`, `.osf` побайтно,
  `.ocf` без метки времени), `tools/check-reject.sh` (8 отказов).

Конфликтов с правками Ивана нет: его `InitSignalStack`/`InstallSignals`
сохранены, регистрация сигнального стека добавлена после
`SetPlatform`.

## 7. Замер

`tools/bench-gc.sh Test64GcBench.DoSmall` после шага 2а (1 и 10 млн):

| Сценарий | Время | На объект | Кластеров |
|---|---:|---:|---:|
| `NEW` 1 млн | 88.1 мс | 88 нс | 124 |
| `NEW` +9 млн | 719.1 мс | 80 нс | 1 222 |
| `Collect` живых 1 млн | 28.1 мс | 28 нс | 124 |
| `Collect` живых 10 млн | 0.313 с | 31 нс | 1 222 |
| `Collect` мёртвых 1 млн | 7.8 мс | — | — |
| `Collect` мёртвых 10 млн | 79.4 мс | — | — |
| Цикл 10 млн живых, 500 итераций | 0.069 с | 0 сборок | — |
| Цикл пустой, 500 итераций | 0.066 мс | 0 сборок | — |

Полный `Do` с 50 млн: `Collect` живых — 1.77 с, RSS 1.5 ГБ, всё
7.1 с. Пиковый RSS `DoSmall` — 316 МБ, полное время — 2 с. До шага 2а
было 24.96 с на 10 млн и 92 с на цикл (4 сборки). Причина — линейный
`InHeap` по кластерам; теперь двоичный поиск по индексу, а `System/Loop`
пропускает периодическую сборку без выделений. Подробности и таблица
«до/после» — `docs/gc-bench.md`.

### Задачи (этап 3)

`UnitRunner.Run Test64Tasks proc slow`:

| Задачи | Стек | Setup | Шаги | Переключений | На переключение | RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 1 000 | 64 КБ | 7.1 мс | 6.9 мс | 4 000 | 1.7 мкс | — |
| 10 000 | 64 КБ | 120 мс | 185 мс | 40 000 | 4.6 мкс | 47.7 МБ |

Виртуально под стеки — 64 МБ и 640 МБ (страницы по касанию),
процесс с 10 тыс. задач занимает 0.33 с. После индекса `StackOf`
переключение перестало зависеть от числа зарегистрированных стеков
линейно.

### Этапы 8–9 и GUI

| Сценарий | Что меряем | Результат |
|---|---|---|
| `Test64Resolver` | ожидание `getaddrinfo` в нити, шаги цикла | 10 010 мс, 9 449 шагов |
| `Test64RobustParallel` | сумма на 1/4/16 процессах | 500500, 1/11/13 мс |
| `Test64Mpi` | 12 квадратов, `mpiexec -n 5` | 252 мс, 0 ошибок |
| `Test64Gui` | 100 элементов, тикер 100 мс | 2 015 мс, maxgap 111 мс |
| `Test64WinTasks` (wine) | Yield, буфер, трап, Join, стек | 5/5, 32 мс худший |
| `Test64WinWorkers` (wine) | Sleep 300 мс в нити, файл, getaddrinfo | 4/4, 304 мс медленный |
| `UnitTests` (wine) | самопроверка Unit | 4/4 |

## 8. Что открыто

- GUI запускается и проверяется автоматически: `tools/test-gui.sh`
  поднимает `gtk4-broadwayd` в своём `XDG_RUNTIME_DIR`, без экрана
  пользователя. Системный `broadwayd` — от GTK3 и с клиентом GTK4
  падает; нужен `gtk4-broadwayd` (пакет `libgtk-4-bin`), в стенде он
  распакован в `.work/gtk4-bin`. Меню мышью не проверяются (запрет на
  эмуляцию перетаскивания и мыши).
- `pack-dev0-linux` в снимке отсутствует, поэтому сам `dev0Linux` не
  пересобирается: бутстрап проверяет только компилятор
  (`tools/bootstrap.sh` ставит `Dev/Code`, который загрузчик читает
  раньше упакованного).
- Отдельного `[code]`-переключателя стеков нет; `SwitchSave/Restore`
  зовут обёртки `Test64Co`.
- `MPI` ставится в пользовательское пространство (`uv pip install
  mpich` в `.work/venv`), транспорт — `AsyncMpi`; mpiexec запускает
  ранг 0 как тест, ранги 1..n как `AsyncMpi.WorkerLoop`. Гибель ранга
  не повторяется (mpiexec гасит мир), `Unload` по MPI возвращает
  отказ. `AsyncTask` из фреймворка Ивана в снимке отсутствует (его
  роль играют `Tasks.Task` и `Select`).
- Windows x64: стеки задач (`VirtualAlloc` + `PAGE_NOACCESS`), контекст
  `Try` на стеке, переключение и рабочие нити этапа 8 сделаны. Фасад
  `AsyncWork` с делегатами `AsyncWorkLin`/`AsyncWorkWin`; делегат
  ставится лениво, при первом `AsyncWork.Execute` (`Dialog.Call` из
  `AsyncWork.Ensure`), базовые init пул не заводят. `AsyncWorkWin` держит
  очередь под `CRITICAL_SECTION`, готовность забирает действие
  `Services` (окна не блокируются), файлы и `getaddrinfo` — через
  `CreateFileA`/WS2_32. Под wine идут UnitTests 4/4, Test64WinTasks 5/5
  и Test64WinWorkers 4/4. GUI-прогон Windows отдельным тестом не
  проверяется.
- i386: этапы 2–6 перенесены (реестр стеков, мониторы, `Tasks`,
  `[exclusive]`, `ACTIVE.AWAIT`, ключ `^`) — см. §5. Осталось
  x64-специфичным: библиотека `Async` (`Async/Mod64`: каналы, рабочие
  нити, компутоны — на 32-битной цели не собирается), оптимизация
  шага 2а (индекс кластеров) — она осталась только в Mod64. Старый
  `dev0Linux` содержит дореформенный `LinFiles`, поэтому
  `clean_host_temp` для его запусков сохранён (§3).
  Условие `AWAIT` не проверяется на обращения к `VAR`-параметрам чужих
  стеков (задача не может держать ссылку на чужой стек по API `Tasks`).
- Нет нагрузочного теста «1000 задач час», проверки трапа в чужой нити
  ОС, отложенных финализаторов вне главного стека.

## 9. Решения без владельца (этап 6)

Развилки, которых не было в плане или которые план оставлял открытыми.
Все обратимы; в скобках — чем менять.

| Вопрос | Решение | Отмена |
|---|---|---|
| Как ядро зовёт условие AWAIT | `[code] Kernel.CallCond` кладёт кадр ждущего в `BX` (статическая связь CP) и зовёт `@awaitN`; компилятор берёт кадр через `SYSTEM.GETREG(BP)` | переписать условие в процедуру верхнего уровня с явным кадром и доступом к переменным через `SYSTEM.GET` |
| Где проверяется, что условие «только читает» | компилятор отвергает `Ncall`/`Nassign` в дереве условия | ослабить до предупреждения или проверять список чистых процедур |
| Где разрешён AWAIT | только в непосредственно объемлющей `[exclusive]`-процедуре; во вложенной — ошибка | разрешить цепочку, если метод держит монитор |
| Получатель `VAR`-записи | поддержан: `ADR(получателя)` как объект монитора | запретить `[exclusive]` на `VAR`-получателе |
| Монитор модуля | адрес скрытой глобальной `INT32`, без объекта в куче и без инициализации в теле | сделать скрытый указатель и `NEW` в начале тела модуля |
| Символ ключа точек уступки | `^` (в сканере уже был, но не обрабатывался) | любой свободный символ и бит 9–28 |
| Как определяется «модуль каркаса» | по первому слову имени: односложные — System, иначе Std/Text/Form/Host/Dev/Lin/Win. Односложный пользовательский модуль (например `CtlPump`) с ключом отвергается | список имён модулей или явная директива в тексте |
| Номера ошибок расширения | 225 («illegal sys flag») для всех нарушений контекста/флагов, 120 — для не-BOOLEAN условия | добавить сообщения в `Dev/Rsrc/Errors.odc` (сейчас `odcey` умеет только читать) |
| Период флага Poll | 5 мс, как `defaultSlice`; флаг поднимает `Tasks.Step` в начале каждой итерации планировщика | вынести период в константу `Tasks` или `Kernel` |
| Отмена в Poll | `Poll`, который действительно уступает, — точка отмены (трап в `Yield`/`CheckWait`) | проверять отмену только в явных `Wait/Sleep/Join` |
| Побайтность бутстрапа | сравниваются `.osf` и `.ocf` с обнулённой 12-байтовой меткой времени компиляции | писать в `.ocf` детерминированное время (например, 0) |
| 32-битная цель | расширение работает: ядро `System/Mod` получило стеки/мониторы/Poll/`CallCond`, `CPC486` — пролог/эпилог `[exclusive]`, `CPT` и `Compiler` разрешают `ACTIVE` и `^` для i386; тесты — `tools/test32.sh` | вернуть отказ, если i386-ядро снова станет неподдерживаемым |
| `[exclusive]` и `[abstract]/[empty]` | не проверяется; кода нет, замок не берётся | ошибка компиляции при сочетании |
| Выгрузка модуля с монитором | отдельной проверки нет; её закрывает запрет этапа 2 (кадры на стеках) для занятого/ждущего монитора | проверять таблицу мониторов при `UnloadMod` |
| Отпечаток `Kernel.CondProc` | псевдозапись `GlbMod` с именем `Kernel` создаётся после импортов (иначе перехватывает импорт Kernel) | зашить константу отпечатка, как у COM `AddRef` |
| Имя временной переменной кадра | `@frm` (не длиннее 4 символов: буфер `GetTempVar`) | расширить `GetTempVar` до `NewName` нужной длины |

## 10. Решения без владельца (этапы 7–9)

Полная таблица — `docs/async-library.md`. Главное:

| Вопрос | Решение | Отмена |
|---|---|---|
| Где библиотека | отдельная подсистема `Async` (Mod и Mod64) | разложить по System/Comm |
| Сообщение канала | `AsyncSelect.Msg`, указатель на расширяемую запись; после отправки не меняется | копирование значения |
| Отправитель встречи | двухфазный вклад и `taken`; прямой Send/Recv обслуживает и предложения Select | свести прямые операции к `Select` |
| Отмена в `Select` | протухшие предложения снимаются лениво по `Tasks.State` | trap-cleaner |
| Новые POSIX-вызовы | `LinNative`, а не `LinLibc`: символы хоста не обновить | пересобрать хост-мир |
| TCP-готовность | `poll` вместо `select` (fd_set < 1024) и очередь accept 128 | вернуть select с ограничением |
| Сигналы в нитях | блокируются в теле рабочей нити | блокировать в главной до создания |
| Проверка кучи из нити | отладочный флаг/счётчик удалён из ядра: задания не выделяют память, тест оставляет медленный вызов, файл и `getaddrinfo` | вернуть TLS-счётчик через `pthread_getspecific` |
| Остановка исполнителей | SIGKILL и закрытие pipe, без `waitpid` (SIGCHLD игнорируется) | мягкая остановка с ожиданием |
| Ожидание `Pull` | `Sleep(10)` с дедлайном | таймер `Services` |
| Сборка `Async` | отдельный `CompileSubs @Lin Async` в `build64.sh` | починить `CompileSubs`/`ACTIVE` |
| Протокол канала | автомат состояний текстом; проверяются и прямые `Send`, и `SendAlt` (в момент исполнения предложения) | проверка в `Select` через явный вид `Msg` |
| Транспорт компутонов | `SetTransport("pipe"/"tcp"/"mpi")` или `ASYNC_TRANSPORT`; TCP по localhost, MPI — внешний объект `AsyncComputon.Transport` от `AsyncMpi` | поле транспорта в `Start` или отдельный модуль |
| `Unload` у исполнителей | свой модуль — явный отказ, остальные — по `refcnt`/кадрам ядра; по MPI `Unload` не поддерживается | экспорт из ядра «модуль на стеке» |
| Метка времени компиляции | `BB_COMPILE_TIME` в `DevCPE`; бутстрап сравнивает B и C побайтно без маски | нулевая метка или прежняя маска |
| Фасад `RobustParallel` | тонкий модуль `Robust/Mod64/Parallel.cp`; `Computon`/`Hook` — синонимы `AsyncComputon`, ранги MPI: 0 — управляющий, 1..size-1 — исполнители (`Push`/`Pull` сдвигают на −1); размер мира — `ROBUST_SIZE` | собственный тип с копированием методов |
| Запуск GUI-команды | внешняя переменная `BB_RUN=Модуль.Процедура` в `LinInit`, вызов до `Loop.Start` | отдельный модуль-автозапуск |
| Стеки задач на Windows | `VirtualAlloc` + `PAGE_NOACCESS`; `Get/SetTryContext` = голова цепочки `current` | `PAGE_GUARD` и `_resetstkoflw` |
| Рабочие нити Windows | фасад `AsyncWork` (`Async/Mod64/Work.cp`) с делегатами `AsyncWorkLin`/`AsyncWorkWin`; Win64: `CreateThread`, `CRITICAL_SECTION`/события в куче, готовность — действие `Services`, файлы и `getaddrinfo` через WinApi/`AsyncWinSock`; `Install` ставится лениво из `AsyncWork.Ensure` при первом `Execute` | отдельная подсистема `AsyncWin` |

## 11. Откат ядра

Опыт (24.11.2026): отдельный `git worktree add --detach .work/rollback HEAD`.
В нём сначала `tools/bootstrap.sh` — новый компилятор (B), затем
`git checkout f03eaab7 -- <файлы>` и удаление надстроек.

Откатывать вместе (проверено сборкой; список шире, чем «Kernel,
LinKernel, Loop, Services, Console, Init/IntInit», потому что `Cons/Interp`
зовёт `Console.WaitInput`):

- `System/Mod64/Kernel.cp`, `System/Mod/Kernel.cp`
- `Lin/Mod64/Kernel.cp`, `Lin/Mod/Kernel.cp`
- `System/Mod/Loop.cp`
- `System/Mod/Services.cp`
- `System/Mod/Console.cp`, `Lin/Mod/Console.cp`
- `Cons/Mod/Interp.cp` (иначе `Console.WaitInput` не найден)
- `Lin/Mod/Init.cp`, `Lin/Mod/IntInit.cp`
- `Win/Mod/Init.cp`, `Win/Mod/IntInit.cp`

Убрать `Async/`, `Robust/`, `Test64/`, `System/Mod/Tasks.cp`,
`System/Mod64/Tasks.cp`. `Unit` в снимке отсутствует и зовёт
`Kernel.GetTrapViewer` — в базовый мир не входит.

Результат: новый компилятор собрал базовый мир `@Lin`
(`Lin System Std Cons Text Fig Form Dev Comm Obx Xhtml Sql Paket`),
линковка `blackbox-console` прошла, `StdLog.String` отработал; бутстрап
в этом дереве — 98 файлов, B и C совпали. Формат `.ocf`/`.osf` и
структур ядра не менялся.
