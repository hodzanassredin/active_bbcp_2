# Стенд active2: снимок Ивана + этапы 1–2

`active2` — ветка от `ivan-dia261123` (снимок Ивана DIA261123 поверх
`b3e3b9d0`) с перенесёнными нашими этапами 1–2 и тестами. Исходники —
UTF-8 `.cp`, сборка — `dev0Linux`, консоль — `blackbox-console`
(`LinIntInit`). Ветка `active` не переписывалась и остаётся на `.odc`.

Разбор снимка и таблица изменений — `docs/ivan-dia261123.md`; замер —
`docs/gc-bench.md`.

## 1. Итог

| Что | Команда | Состояние |
|---|---|---|
| Сборка x64 из `.cp` | `tools/build64.sh` | работает, проверено на чистом `.work/BB64LIN` |
| Консольный запуск | `echo 'Команда' \| tools/run64.sh` | работает |
| Быстрый цикл | `tools/go64.sh`, `tools/go32.sh` | работает (`CompileThis`) |
| Тесты без окон | `tools/test.sh` (Unit) | 4 модуля, 27 тестов, все зелёные |
| Этап 1: очередь и цикл | `Test64Tests.TestProcActionTrap` | зелёный |
| Этап 2: стеки | `Test64Tests.TestProcGcStack`, `TestProcTrapStack` | зелёные |
| Шаг 2а: индекс кластеров | `Test64Tests.TestProcInHeap` | зелёный |
| Замер 1/10/50 млн | `tools/bench-gc.sh Test64GcBench.DoSmall` / `.Do` | выполнен |
| GUI | `blackbox` линкуется | не запускался (стенд без GUI) |

## 2. Раскладка

```
active_bb/                     репозиторий (ветка active2)
  <Подсистема>/Mod/*.cp        исходники (Mod64 — приоритетный вариант amd64)
  Unit/Mod/*.cp                UnitAssert, UnitRunner, UnitTests
  Test64/Mod/*.cp              сценарии Test64, Test64Tests, Test64Tasks
  System/Mod64/Tasks.cp        задачи со своим стеком (этап 3)
  tools/                       стенд
  .work/BB64LIN/               каталог установки (в git не входит)
    <Подсистема>/{Code,Sym}    .ocf/.osf, собранные dev0Linux
    blackbox-console           консольный ELF64 (Kernel+ … LinIntInit)
    blackbox                   GUI ELF64 (Kernel+ … LinLoader), не запускаем
    libtest64.so               помощник тестов (ucontext)
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

echo 'StdLog.String("hi"); StdLog.Ln' | tools/run64.sh
echo 'Test64Basic.Do' | tools/run64.sh
```

`build64.sh` проверяет журнал на `N error(s) detected`, `~TRAP`,
`syntax error`, `not found`; линковка обязана напечатать `ok`.
Каталог установки переопределяется `BB64INSTALL`.

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

Результат: **4 модуля, 27 тестов, 0 провалов** —
- `Test64Tests` (marks=proc): Basic, Heap, Trap, ActionTrap, GcStack,
  TrapStack, InHeap (сценарии `Test64*`, точки входа в
  `Test64/Mod/Tests.cp`);
- `Test64Tasks` (marks=proc slow): 13 тестов задач — Yield,
  производитель-потребитель, философы, отмена, изоляция трапа,
  проверки ожидания, группа, действие планировщика, нагрузка
  1 тыс. и 10 тыс. задач (`Test64/Mod/Tasks.cp`, см. `docs/tasks.md`);
- `UnitTests`: самопроверка Unit, включая вложенный прогон
  `UnitTestsProbe` (PASS/FAIL/EMPTY/SKIP);
- `CheckTests` (marks=proc): три пробы Ивана — RealFormat, Math,
  CCallAlias.

`Test64Support` остался адаптером: `Check`/`Fail` учитываются в
`UnitAssert`, маркеры `TEST64-PASS/FAIL` сохранены для `bench-gc.sh`.
`Test64Pump` и `Test64GcBench` в манифест не входят; замер —
`tools/bench-gc.sh`.

### Ловушка консоли

Консольный init не ставит `Kernel.trapViewer`, поэтому при
`trapViewer = NIL` любой трап фатален даже внутри `Kernel.Try`.
`Test64Support.Begin` ставит пустой viewer — это дефект каркаса, не ядра
теста. В норме viewer должен ставить консольный init.

## 6. Этапы 1–2 на `.cp`

- `System/Mod/Services.cp`: `Work`, `Post`, `Wake`, `NextDeadline`,
  `SetWakeHook`; `executing` возвращает действие в очередь, если `Do`
  трапнуло без `DoLater`.
- `System/Mod/Console.cp` + `Lin/Mod/Console.cp`: `WaitInput(timeout)`;
  `LinConsole` использует `poll` на stdin и `setvbuf(_IONBF)`.
- `Cons/Mod/Interp.cp`: между командами и во время ожидания ввода
  вызывает `Services.actionHook.Step`.
- `System/Mod64/Kernel.cp`: реестр `Stack`, `RegisterStack`,
  `RegisterSignalStack`, `UnregisterStack(At)`, `StackOf`,
  `InRegisteredStack`, `CurrentStack`, `SwitchSave/SwitchRestore`;
  `MarkLocals` сканирует текущий стек от FP и спящие от SP;
  `UnloadMod` не выгружает модуль, если его код есть в кадрах.
- `Lin/Mod64/Kernel.cp`: `Get/SetTryContext` поверх
  `currentTryContext`, `InActivationStack` учитывает зарегистрированные
  стеки, сигнальный стек регистрируется после `Kernel.SetPlatform`.
- Шаг 2а: `System/Mod64/Kernel.cp` — индекс кластеров для `InHeap`
  (`IndexInsert/IndexRemove`, отладочные `InHeapAddr`, `ClusterBounds`,
  `ClusterIndexCheck`), счётчик `AllocCount`; `System/Mod/Loop.cp` —
  пропуск периодической сборки без выделений, диагностика `Loop.Stats`.
- Этап 3: `System/Mod64/Tasks.cp` — задачи и [code]-переключатель;
  `Kernel.AllocStack/FreeStack` и `Platform.AllocateStackMem`
  (в `LinKernel` — mmap + сторожевая страница); индекс `Kernel.StackOf`;
  `Services.executingAction`/`executing` и trap-cleaner;
  `Models.InDomainCast`. Подробности — `docs/tasks.md`.

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

## 8. Что открыто

- GUI не запускался; GTK4-сборка только линкуется.
- `pack-dev0-linux` в снимке отсутствует; пересборка bootstrap не
  автоматизирована.
- Отдельного `[code]`-переключателя стеков нет; `SwitchSave/Restore`
  зовут обёртки `Test64Co`.
- Нет нагрузочного теста «1000 задач час», проверки трапа в чужой нити
  ОС, отложенных финализаторов вне главного стека.
- `UnitRunner.TempDir` только возвращает путь `UnitTmp`: каталог создаёт
  внешний `tools/unit.sh` (в процессе BlackBox нет переносимого mkdir).
- `Tasks`: Windows x64 без `Platform.AllocateStackMem` (нужен
  `VirtualAlloc` + `PAGE_NOACCESS`); вытеснения нет; мониторов нет
  (этап 4).
