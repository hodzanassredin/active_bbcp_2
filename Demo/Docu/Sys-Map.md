# Подсистема Demo — карта

`Demo` содержит небольшие исполняемые переводы известных примеров параллелизма. Каждый пример —
отдельный модуль с командой без параметров; у каждой команды есть тест в
`Demo_dev/Mod/Examples.cp` с детерминированным результатом.

Примеры используют расширение языка и библиотеку:

- `Tasks` — кооперативные задачи с собственным стеком (`Docu/ru/Tasks.md`);
- `[exclusive]`, `ACTIVE.AWAIT` и мониторы (`Docu/ru/Active-CP-Lang.md`);
- `AsyncChan`, `AsyncSelect` и действия `Services` (`Docu/ru/Async.md`).

Примеры собираются только для AMD64: библиотека каналов `Async/Mod` не собирается для i386.
Задачи, мониторы и `ACTIVE.AWAIT` работают и на i386.

## Сборка и запуск

```sh
tools/build64.sh Demo          # DemoSupport и все примеры
echo 'DemoBuffer.Run' | tools/run64.sh
tools/test.sh Demo_devExamples # тесты всех примеров
tools/test-demo-gui.sh         # окно DemoAnts без экрана
```

`tools/build64.sh` без аргументов собирает `Demo` тоже: она входит в список подсистем по
умолчанию. `tools/test.sh` запускает `Demo_devExamples` из `tools/tests.txt` и отдельный сценарий
`DemoAntsGui` (`tools/test-demo-gui.sh`).

## Примеры

### Active Oberon

| Модуль | Команда | Источник |
|---|---|---|
| `DemoBuffer` | `DemoBuffer.Run` | Reali, *Active Oberon Language Report*, B.6 «Bounded Buffer» |
| `DemoPhilo` | `DemoPhilo.Run` | Reali, C.1 «Dining Philosophers» и B.4 «Semaphores» |
| `DemoReaderWriter` | `DemoReaderWriter.Run` | Reali, B.1 «Readers and Writers» |
| `DemoBarber` | `DemoBarber.Run` | Klemm/ETH *System Construction*, монитор «Barber Shop», переведён на `AWAIT` |
| `DemoModuleMonitor` | `DemoModuleMonitor.Run` | *System Construction*, производитель-потребитель с `{EXCLUSIVE}` |

### Go

| Модуль | Команда | Источник |
|---|---|---|
| `DemoPingPong` | `DemoPingPong.Run` | R. Pike, *Go Concurrency Patterns*, ping-pong |
| `DemoPipeline` | `DemoPipeline.Run` | S. Ajmani, *Go Concurrency Patterns: Pipelines and Cancellation*; fan-in Пайка |
| `DemoSieve` | `DemoSieve.Run` | Pike, *Sieve of Eratosthenes*; также Reali C.2 |
| `DemoSelectTimeout` | `DemoSelectTimeout.Run` | *Tour of Go* «Select»; gobyexample «Timeouts» |
| `DemoWorkerPool` | `DemoWorkerPool.Run` | gobyexample «Worker Pools» |
| `DemoBoring` | `DemoBoring.Run` | Pike, *boring* generator и мультиплексор |
| `DemoGroup` | `DemoGroup.Run` | Ajmani, отмена; `golang.org/x/sync/errgroup` |

### Координация с окном

| Модуль | Команды | Описание |
|---|---|---|
| `DemoAnts` | `DemoAnts.Open`, `Start`, `Stop`, `Pause`, `Resume`, `Step`, `Faster`, `Slower`, `Reset`, `Run`, `Simulate`, `Bench` | 1000 муравьёв на поле 160×100 со стенами и проходами: еда, гнездо, феромон; скорость модели 1–200 шагов/с (по умолчанию 10) отделена от перерисовки; монитор поля, задача-испаритель, `Views.View` и таймер `Services` |

`DemoSupport` — общий помощник (насос задач и вывод в консоль); своей команды у него нет. `MD5` —
существующий пример хеширования, к параллелизму не относится.

## Тесты

| Тест | Что проверяет |
|---|---|
| `Demo_devExamples.TestProcBoundedBuffer` | 120 элементов, сумма 2460 |
| `Demo_devExamples.TestProcPhilosophers` | 15 обедов, без трапов |
| `Demo_devExamples.TestProcReadersWriters` | 15 чтений, 6 записей, нет пересечения |
| `Demo_devExamples.TestProcBarber` | 3 обслужено, 3 отказано |
| `Demo_devExamples.TestProcModuleMonitor` | 120 элементов, сумма 7260 |
| `Demo_devExamples.TestProcPingPong` | 10 ударов, сумма 45 |
| `Demo_devExamples.TestProcPipeline` | 10 результатов, сумма 385 |
| `Demo_devExamples.TestProcSieve` | 25 простых, сумма 1060, максимум 97 |
| `Demo_devExamples.TestProcSelectTimeout` | индекс тайм-аута 1, индекс приёма 0, значение 42 |
| `Demo_devExamples.TestProcWorkerPool` | 9 результатов, сумма 285 |
| `Demo_devExamples.TestProcBoring` | 16 сообщений, по 8 от каждого генератора |
| `Demo_devExamples.TestProcGroupCancel` | один трапнул, трое отменены, группа failed и пуста |
| `Demo_devExamples.TestProcAnts` | 1000 муравьёв × 300 шагов: еда > 0, число сохранено, все завершились |
| `Demo_devExamples.TestProcAntsWalls` | не менее 3 стен, ни один муравей не в стене, стены отклоняют ходы |
| `Demo_devExamples.TestProcAntsReachable` | поиск в ширину от гнезда достигает каждой клетки еды |
| `Demo_devExamples.TestProcAntsSpeed` | `Faster` останавливается на 200, `Slower` — на 1 |
| `Demo_devExamples.TestProcAntsFoodWalls` | 200 муравьёв × 1200 шагов: еда доставлена в обход стен |
| `Demo_devExamples.TestProcAntsMany` | 1000 муравьёв завершаются, `Tasks.Count() = 0`, без трапов |
| `Demo_devExamples.TestProcAntsPauseStep` | `Pause` + `Step` делает ровно один круг |

GUI-сценарий `tools/test-demo-gui.sh` открывает окно `DemoAnts` под `gtk4-broadwayd`, печатает
ASCII-кадр каждые 40 тиков перерисовки и требует `DemoAnts: ok=1` (тики, еда, кадры, время
кадра, живые муравьи). Мышь не эмулируется, экран пользователя не используется. Окно — около
1000×650 px; размер клетки следует за окном и не меньше 3 px.
