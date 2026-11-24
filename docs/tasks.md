# Tasks: задачи со своим стеком (этап 3)

`System/Mod64/Tasks.cp` — задачи в одной нити ОС: у каждой свой стек,
`Run`, `Start`, `Yield`, `Sleep`, `Join`, `Cancel`, `Park`/`Unpark`,
состояние и имя. Мониторы этапа 4 (`Kernel.Lock/Unlock/Wait/Await`)
стоят на этих задачах: `Tasks` ставит ядру объект-крючок
`Kernel.Scheduler` (`Kernel.SetScheduler`) и реализует `current`,
`park`, `unpark`, `name`, `poll`. С s18 сама реализация мониторов —
обычный модуль `System/Mod/Monitors.cp`; цепочки взятий привязаны к
токену задачи, а не к записи стека ядра. Философы в тестах по-прежнему
берут вилки ручными `Park`/`Unpark` — как пример без мониторов;
буфер и `Await` проверяются в `System_testsMonitors`.

## Мониторы вне ядра (s18)

`System/Mod/Monitors.cp` — таблица мониторов по адресу объекта,
очереди входящих и ждущих, свободные списки, цепочки взятий по токену
задачи и диагностика (`IsLocked`, `MonitorCount`, `WaitersCount`,
`DumpMonitors`, `LockDepth`). Ядро оставляет четыре экспортируемых
имени, которые зовёт компилятор, — `Kernel.Lock/Unlock/Wait/Await` —
как заглушки над `Kernel.MonitorHook`; без крючка они трапают
«monitors module is not loaded». Штатные `Lin`/`Win` init-модули
импортируют `Monitors`, поэтому в обычном мире он загружен.

Таблица и записи — обычные глобальные/кучевые объекты модуля,
`MarkGlobals` видит их сам; `MarkMonitors`/`MarkLocks` из ядра удалены.
Объект под монитором хранится полем `ANYPTR` (иначе сборщик не видел
бы его корнем). `Kernel.Try` через крючок запоминает глубину цепочки
и снимает взятия при трапе; `Kernel.TrapCleanup` через крючок чистит
очереди трапнувшей задачи. `Kernel.Stack` о мониторах больше не знает.

## API

```
TYPE Task* = POINTER TO ABSTRACT RECORD ... END;
     Run*: PROCEDURE (t: Task), NEW, ABSTRACT;
     Group* = POINTER TO RECORD ... END;

Tasks.Start(t);                  Tasks.StartIn(t, g);
Tasks.Yield;                     Tasks.Sleep(ms);
Tasks.Join(t);                   Tasks.Cancel(t);
Tasks.Park;                      Tasks.Unpark(t);
Tasks.State(t); Tasks.GetName(t, s); Tasks.SetName(t, s);
Tasks.Failed(t); Tasks.Current(); Tasks.InTask();
Tasks.NewGroup(); Tasks.GroupFailed(g); Tasks.GroupCount(g);
Tasks.OwnGroup(t);                 Tasks.InFramework();
Tasks.Step; Tasks.NextDeadline(); Tasks.Count(); Tasks.List;
Tasks.SetTimeSlice(t, ms);
Tasks.Switches / Tasks.ResetStats.
```

Состояния: `New`, `Ready`, `Running`, `Sleeping`, `Waiting`, `Parked`,
`Finished`, `Cancelled`.

Задача — расширяемая запись:

```
TYPE Worker = POINTER TO RECORD (Tasks.Task) n: INTEGER END;
PROCEDURE (w: Worker) Run;
BEGIN Tasks.Sleep(10); INC(w.n) END Run;
...
NEW(w); Tasks.SetName(w, "worker"); Tasks.Start(w)
```

## Стек

Память выделяет ядро: `Kernel.AllocStack(size, lo, hi)` →
`Platform.AllocateStackMem`, в `LinKernel` — `mmap` с одной сторожевой
страницей `PROT_NONE` снизу. Страницы физически выделяются по касанию,
поэтому 10 тыс. задач занимают 640 МБ адресного пространства, но
~48 МБ RSS. Размер задаётся `Tasks.SetStackSize` (по умолчанию 256 КБ).
После завершения задачи `Kernel.UnregisterStack` + `Kernel.FreeStack`
(`munmap`) на главном стеке.

`RegisterStack` заносит стек в реестр этапа 2; `MarkLocals` сканирует
спящие стеки от сохранённого SP. `Kernel.StackOf` теперь ищет по
отсортированному индексу стеков (`stackIndex`, двоичный поиск) — иначе
на 10 тыс. задач линейный обход реестра на каждом переключении стоил
десятки микросекунд.

## Переключатель

`PROCEDURE [code] Switch (VAR from, to)` — 71 байт x86-64:

- снимает второй параметр (`&to`) со стека;
- `lea r11,[rip+56]` — адрес продолжения сразу после байтов;
- `push r11` — синтетический адрес возврата: [code]-процедура
  встраивается в вызывающую, собственного адреса возврата у неё нет,
  поэтому `ret` иначе вернулся бы в произвольное слово кадра;
- сохраняет rax, rbx, rbp, rsi, rdi, r8-r15 (13 регистров; XMM в CP
  вызываемая сторона не сохраняет и живых значений через вызов нет);
- `from^ := rsp`, `rsp := to^`, восстанавливает регистры, `ret` —
  продолжение того места, откуда контекст сохранили. Для свежей задачи
  в слоте возврата лежит адрес `TaskBoot`.

Вокруг сырого переключения — `Kernel.SwitchSave`/`SwitchRestore`:
контекст `Kernel.Try`, `trapStack` и `tryCount` хранится в записи стека
ядра. Внешней `.so` нет; `ucontext` из System_testsCo не используется.

## Планировщик

Действие `Services.Action`: `Tasks.Start` ставит задачу в очередь
готовых и регистрирует действие через `Services.DoLater(sched,
Services.now)`. `Step` исполняет готовые задачи не дольше
`timeSlice` (5 мс по умолчанию) и возвращает управление циклу;
действие переставляет себя на ближайший срок (`NextDeadline`) или на
`now`. Из задачи `Step` звать нельзя — трап.

## Отмена, ожидание, группа

- `Cancel` ставит флаг; каждая точка ожидания (`Yield`, `Sleep`,
  `Join`, `Park`) проверяет его и трапает `ASSERT(FALSE, 21)`.
  `TaskBoot` ловит трап на стеке задачи: состояние `Cancelled`,
  остальные задачи живут. Работающий без ожиданий код не вытесняется.
- Ожидание вне задачи — трап «wait outside a task».
- Ожидание внутри обработчика действия (`Services.executingAction`,
  кроме собственного планировщика) или рассылки по домену
  (`Models.InDomainCast`) — трап «wait inside an action handler or
  domain broadcast». `Services.Exec` ставит флаг и снимает его
  trap-cleaner'ом; `Tasks` дополнительно очищает возможный остаток в
  `TaskBoot` и в начале `Step`.
- `Park`/`Unpark`: `Unpark` до `Park` не теряется (флаг `wake`).
- Группа: `NewGroup` запоминает текущую задачу владельцем;
  `StartIn(t, g)` учитывает участников. Родитель после `Run` ждёт
  (`Parked`), пока `g.alive > 0`; трап участника помечает группу
  `failed` и отменяет остальных.
- Трап в задаче ловится её `Kernel.Try` в `TaskBoot`, задача
  помечается `Failed`; `Kernel.Collect` не требуется — следующая
  сборка сама увидит стеки.

## Проверка ABB о VAR-параметрах

В ABB старт активности проверял, что VAR-параметры не ссылаются в
чужой стек. У нас `Run` без параметров; проверка сведена к тому, что
объект задачи не лежит на стеке (`Kernel.StackOf(ADR(t^)) = NIL`).
Полный обход полей расширения не делается — см. «Решения без
владельца» в отчёте.

## Тесты и цифры

`System_tests/Mod/Tasks.cp` (модуль `System_testsTasks`, marks=`proc slow`):
Yield, производитель-потребитель, философы (ручные Park/Unpark),
отмена, изоляция трапа, ожидание вне задачи и в обработчике,
потерянное пробуждение, группа, действие планировщика, список задач,
нагрузка 1 тыс. и 10 тыс. задач, уборка списка `all` в `Reap`
(после 10 тыс. коротких задач список пуст, объекты задач собраны — это
видно по `FINALIZE`).

`Reap` снимает задачу из двузвенного списка `all` за O(1); раньше
список только рос и удерживал все когда-либо запущенные задачи.
`CLOSE` модуля снимает планировщик (`Kernel.SetScheduler` с `NIL`),
убирает действие планировщика из `Services` и освобождает стеки живых
задач. Спящих `Step` просматривает не чаще раза на изменившееся время
(`lastWake`) — при 10 тыс. спящих это главный расход шага.

Замер (Linux x64, `UnitRunner.Run System_testsTasks proc slow`):

| Задачи | Стек | Setup | Шаги | Переключений | На переключение | RSS процесса |
|---:|---:|---:|---:|---:|---:|---:|
| 1 000 | 64 КБ | 7.1 мс | 6.9 мс | 4 000 | 1.7 мкс | — |
| 10 000 | 64 КБ | 120 мс | 185 мс | 40 000 | 4.6 мкс | 47.7 МБ |

Виртуальная память под стеки — 64 МБ и 640 МБ; физическая — по
касанию. Каждая задача в нагрузочном тесте делает `Sleep(0)` (уступку)
и завершается, то есть на задачу приходится два входа и четыре
переключения (вход/выход планировщика).

## Ограничения

- Одна нить ОС, кооперативная многозадачность: `Cancel` и `Yield`
  работают только в точках ожидания. Точка `Kernel.Poll`, вставленная
  ключом `^`, тоже считается точкой отмены (если она уступает).
- Мониторы — этап 4 (`Kernel.Lock/Unlock/Wait/Await`), языковое
  расширение — этап 6 (`[exclusive]`, `ACTIVE.AWAIT`, ключ `^`,
  мониторы модуля). Описание — `Docu/Async.md`,
  `Docu/ru/Async.md`.
- `Platform.AllocateStackMem` по умолчанию возвращает 0; на Windows
  x64 нужен свой `VirtualAlloc` + `PAGE_NOACCESS` (задача тогда
  отказывается стартовать с понятным сообщением).
- Консольный init не ставит `trapViewer`; без Unit/System_testsSupport трап
  в задаче фатален, как и любой трап в консоли (общий дефект стенда).
