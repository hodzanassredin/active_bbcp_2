# Акторы

Подсистема `Actor` строит программу из объектов, которые обмениваются сообщениями и не делят
состояние. Глава описывает первый этап: акторы одного процесса, их исполнение общим пулом задач и
надзор родителя за детьми. Задачи, мониторы и точки переключения определены в
`Docu/ru/Active-CP-Lang.md` и `Docu/ru/Tasks.md`, сообщения и каналы — в `Docu/ru/Async.md`.
Подсистема собирается только для AMD64, как и `Async`.

## 1. Счёт, который трогают все

Возьмём банковский счёт, с которого снимают деньги несколько задач. Если остаток лежит в общей
переменной, каждая задача проверяет его и уменьшает, и между проверкой и уменьшением другая задача
успевает сделать то же самое: остаток уходит в минус. Монитор закрывает эту дыру, но приносит
другую: задача, трапнувшая внутри исключающей процедуры, оставляет счёт в том состоянии, в каком
застал её трап, и следующая задача работает с ним дальше.

Актор решает обе задачи одним правилом. *Актор* — объект с собственным состоянием и обработчиком
`Receive`; состояние актора меняет только его обработчик, а другие части программы посылают ему
сообщения. Сообщения ждут обработки в *ящике* — очереди актора. Обработчик получает их по одному,
поэтому между проверкой остатка и его уменьшением чужого вмешательства нет. Трап обработчика
ловится и становится сообщением *супервизору* — родителю актора, который решает, что делать с
отказавшим: продолжить, перезапустить с чистым состоянием или остановить.

Отправитель знает получателя по *адресу* — значению типа `ActorCore.Ref`. Адрес состоит из имени
узла и имени актора. На первом этапе узел всегда свой, но отправка уже идёт через абстрактный
метод адреса, и адрес другого процесса встанет на то же место (раздел 9).

## 2. Первый актор

Счётчик принимает `Inc` и отвечает на `Get` текущим значением. Модуль ниже создаёт его, посылает
тысячу `Inc` из задачи-клиента и спрашивает итог.

```
MODULE ActorDocCounter;
	IMPORT Services, Tasks, Console, Strings, ActorCore, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Get = POINTER TO RECORD (ActorCore.Msg) END;
		Num = POINTER TO RECORD (ActorCore.Msg) n: INTEGER END;
		Counter = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Task) END;

	VAR counter: ActorCore.Ref; answer: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Counter) Receive (c: ActorCore.Context; m: ActorCore.Msg);
		VAR r: Num;
	BEGIN
		WITH m: Inc DO INC(a.n)
		| m: Get DO NEW(r); r.n := a.n; c.Respond(r)
		ELSE	(* Started и прочие сообщения жизненного цикла *)
		END
	END Receive;

	PROCEDURE New (): ActorCore.Actor;
		VAR a: Counter;
	BEGIN
		NEW(a); RETURN a
	END New;

	PROCEDURE (t: Client) Run;
		VAR i: INTEGER; inc: Inc; get: Get; r: ActorCore.Msg;
	BEGIN
		NEW(inc); NEW(get);
		FOR i := 1 TO 1000 DO ActorCore.Tell(counter, inc) END;
		IF ActorCore.Ask(counter, get, 1000, r) = ActorCore.ok THEN answer := r(Num).n ELSE answer := -1 END;
		done := TRUE
	END Run;

	PROCEDURE Run*;
		VAR t: Client; s: ARRAY 16 OF CHAR;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		counter := ActorCore.Spawn(NIL, ActorCore.NewProps(New), "counter");
		done := FALSE; NEW(t); Tasks.Start(t);
		WHILE ~done DO Services.actionHook.Step END;
		Strings.IntToString(answer, s);
		Console.WriteStr("ActorDocCounter: "); Console.WriteStr(s); Console.WriteLn;
		ActorCore.StopActor(counter)
	END Run;

END ActorDocCounter.
```

Команда `ActorDocCounter.Run` печатает `ActorDocCounter: 1000`. Актор описывается записью `Props`:
фабрика экземпляров, предел ящика, признак собственной задачи и стратегия надзора за детьми.
`Spawn` создаёт экземпляр фабрикой, заводит ящик и возвращает адрес; первым сообщением обработчик
получает `Started`. `Tell` кладёт сообщение в ящик и возвращается сразу. `Ask` посылает сообщение
с временным адресом ответа и ждёт первого письма на этот адрес не дольше заданного числа
миллисекунд; обработчик отвечает через `c.Respond`, который адресует письмо отправителю текущего
сообщения. `Ask` ждёт, поэтому клиент — задача: на главном стеке `Ask` трапует с кодом 20.

Сообщение — расширение `AsyncSelect.Msg` (в подсистеме оно называется `ActorCore.Msg`) и после
отправки не меняется. Счётчик выше посылает тысячу раз один и тот же объект `inc`: это допустимо
именно потому, что объект неизменяем. Если бы клиент менял поле отправленного сообщения, обработчик
увидел бы значение на момент обработки, и порядок отправок перестал бы что-либо значить.

## 3. Исполнители

У актора нет своей задачи. Ящики с работой обслуживает *исполнитель* — задача пула `ActorRun`,
которая берёт ящик из общей очереди, вызывает `ActorCore.Process` и возвращает ящик в очередь,
если в нём остались письма. `ActorRun.Start(n)` заводит n исполнителей; за один вызов `Process`
актор обрабатывает все системные сообщения и до ста пользовательских. Тысяча акторов поэтому
занимает тысячу ящиков и столько стеков, сколько исполнителей в пуле.

Обработчик актора может ждать: спать, принимать из канала, спрашивать другого актора через `Ask`.
Пока он ждёт, действует правило: один обработчик на актор за раз. Ящик ждущего актора помечен как
занятый, и следующее письмо этому актору не выдаётся ни этому, ни другому исполнителю. Цена правила
— занятый исполнитель. Пусть в пуле четыре исполнителя, а четыре актора одновременно уснули в
`Receive` на секунду: пятый актор, которому пришло письмо, ждёт секунду, хотя работы у него на
микросекунду. Тест `TestBusyActorGetsNoSecondMessage` проверяет обе стороны правила: два письма
`Nap` одному актору обрабатываются друг за другом, а письмо второму актору обрабатывается во время
первого сна.

Актор, которому ожидание нужно постоянно, получает собственную задачу: `Props.dedicated := TRUE`.
Его ящик обслуживает только она, и исполнители пула остаются свободны; в тесте
`TestDedicatedLeavesPoolFree` пул из одного исполнителя отвечает другому актору, пока актор со своей
задачей спит.

Свободный исполнитель ждёт в `Tasks.Park`, и `Tell` будит его через `Tasks.Unpark`. Если `Tell`
сделан вне задачи — из команды или из действия `Services`, — планировщик задач может быть уже
назначен на срок самой ранней спящей задачи, и разбуженный исполнитель ждал бы этого срока. Поэтому
первый исполнитель пула вместо `Park` спит по 10 мс: письмо, посланное с главного стека,
начинает обрабатываться не позже чем через 10 мс. Письмо, посланное из задачи, задержки не имеет.

Спросить самого себя через `Ask` нельзя: вопрос ляжет в собственный ящик, а ящик занят ждущим
обработчиком. `Ask` вернёт `timedOut` по истечении срока, и ни одного ответа не будет.

## 4. Доставка

Доставка идёт по правилу «не более одного раза». Письмо, которое не может быть обработано, не
возвращается отправителю и не повторяется; оно становится *мёртвым письмом*, а счётчик
`ActorCore.deadLetters` увеличивается на единицу. Мёртвым становится письмо остановленному актору,
письмо сверх предела ящика `Props.capacity`, ответ `Respond` на сообщение без отправителя и всё,
что осталось в ящике при остановке актора. Отправитель при этом не трапует и не ждёт.

Ящик без предела растёт, пока есть память. С пределом `capacity = 5` двадцать писем, посланных
подряд до первой обработки, дают пять обработанных и пятнадцать мёртвых
(`TestCapacityOverflowGoesToDeadLetters`).

Письма одного отправителя одному получателю приходят в порядке отправки: ящик — одна очередь, и
каждый адресат кладёт письмо в её конец. Тест счётчика посылает тысячу пронумерованных `Inc` и
проверяет, что номера идут подряд. Письма разных отправителей перемешиваются в порядке, в котором
отправители получали управление.

`Ask` возвращает один из трёх ответов:

| Результат | Условие |
|---|---|
| `ok` | на адрес ответа пришло письмо, оно в `reply` |
| `timedOut` | за отведённое время ответа нет |
| `dead` | вопрос стал мёртвым письмом |

Ответ `dead` приходит без ожидания срока: в тесте `TestStoppedActorGivesDeadLetters` вопрос
остановленному актору со сроком 1000 мс возвращается быстрее 500 мс. Ответ,
пришедший после тайм-аута, теряется.

## 5. Жизненный цикл

Актор проходит состояния «работает», «перезапускается», «останавливается» и «остановлен». О каждом
переходе обработчик узнаёт сообщением:

| Сообщение | Когда |
|---|---|
| `Started` | первым после создания и первым после перезапуска |
| `Restarting` | старому экземпляру перед перезапуском |
| `Stopping` | в начале остановки |
| `Stopped` | последним, после остановки всех детей |
| `Terminated` | наблюдателю, когда наблюдаемый актор остановлен |
| `ReceiveTimeout` | после простоя дольше `SetReceiveTimeout` |

Кроме ящика пользовательских писем у актора есть системная очередь: остановка, наблюдение,
отказ ребёнка, уход ребёнка, директивы супервизора. Перед каждым пользовательским письмом
исполнитель разбирает всю системную очередь. Поэтому `StopActor` останавливает актор, даже если в
его ящике тысяча писем: все они станут мёртвыми.

Остановка идёт сверху вниз и завершается снизу вверх. Родитель получает `Stopping`, посылает
остановку детям и ждёт ухода каждого; `Stopped` родитель получает, только когда детей не осталось.
Если бы порядок был обратным, обработчик родителя в `Stopped` освобождал бы ресурсы, которыми дети
ещё пользуются в своих `Stopping`. Тест `TestStopParentStopsChildrenFirst` проверяет по журналу,
что `Stopping` и `Stopped` обоих детей записаны раньше `Stopped` родителя.

`c.Watch(r)` подписывает актор `c` на остановку `r`: когда `r` остановлен, обработчик `c` получает
`Terminated` с полем `who = r`. Подписка на уже остановленный актор отвечает `Terminated` сразу.

`c.SetReceiveTimeout(ms)` просит сообщить о простое. После `ms` миллисекунд без пользовательских
писем обработчик получает `ReceiveTimeout` один раз; следующий отсчёт начинает следующее письмо.
В тесте `TestReceiveTimeoutOnceAfterIdle` при сроке 100 мс десять писем с паузой 20 мс не дают ни
одного `ReceiveTimeout`, затем 250 мс тишины дают ровно один, и ещё 250 мс тишины — ни одного.
Сообщения жизненного цикла и сам `ReceiveTimeout` отсчёт не сбрасывают.

`c.Become(b)` подменяет обработчик: следующие письма получает `b.Receive(a, c, m)`, где `a` —
экземпляр актора. `Become(NIL)` возвращает `Receive` экземпляра, перезапуск — тоже.

## 6. Надзор

Каждый актор, созданный `Spawn(c, ...)`, — ребёнок актора `c`; созданный `Spawn(NIL, ...)` —
ребёнок корня. Обработчик исполняется под `Kernel.Try`, и трап в нём не роняет исполнителя:
ящик приостанавливается, а родитель получает системное сообщение об отказе с номером трапа.
Родитель спрашивает стратегию из своего `Props.strategy` и получает *директиву* — решение о судьбе
отказавшего ребёнка:

| Директива | Действие |
|---|---|
| `Resume` | ящик снимается с паузы, состояние сохранено, письмо-виновник пропущено |
| `Restart` | дети ребёнка останавливаются, затем фабрика создаёт новый экземпляр, он получает `Started`; письма ящика сохранены, виновник пропущен |
| `Stop` | ребёнок останавливается, его письма становятся мёртвыми |
| `Escalate` | отказывает сам родитель с тем же номером трапа; ребёнок ждёт его судьбы |

Без стратегии директива — `Stop`. `ActorSup.NewOneForOne(d, max, ms)` отвечает директивой `d` и
применяет её только к отказавшему ребёнку. Для `Restart` она считает перезапуски в окне: окно
открывает первый отказ, отказ после конца окна открывает новое, а отказ с номером `max + 1` в окне
даёт `Stop`. Без предела ребёнок, трапующий на каждом письме, перезапускался бы бесконечно и
занимал исполнителя. Тест `TestTooManyRestartsStopsAndNotifiesWatcher` при `max = 2` посылает три
трапующих письма: ребёнок получает `Started` три раза, затем останавливается, и наблюдатель
получает `Terminated`.

Перезапуск без журнала теряет состояние. Модуль ниже считает письма, трапует на третьем и
перезапускается: после перезапуска счёт начинается заново.

```
MODULE ActorDocRestart;
	IMPORT Kernel, Services, Console, Strings, ActorCore, ActorSup, ActorRun;

	TYPE
		Inc = POINTER TO RECORD (ActorCore.Msg) END;
		Boom = POINTER TO RECORD (ActorCore.Msg) END;
		Worker = POINTER TO RECORD (ActorCore.Actor) n: INTEGER END;
		Parent = POINTER TO RECORD (ActorCore.Actor) END;

	VAR worker: ActorCore.Ref; starts, last: INTEGER;

	PROCEDURE (a: Worker) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		WITH m: ActorCore.Started DO INC(starts)
		| m: Inc DO INC(a.n); last := a.n
		| m: Boom DO HALT(42)
		ELSE
		END
	END Receive;

	PROCEDURE NewWorker (): ActorCore.Actor;
		VAR a: Worker;
	BEGIN
		NEW(a); RETURN a
	END NewWorker;

	PROCEDURE (a: Parent) Receive (c: ActorCore.Context; m: ActorCore.Msg);
	BEGIN
		IF m IS ActorCore.Started THEN
			worker := ActorCore.Spawn(c, ActorCore.NewProps(NewWorker), "worker")
		END
	END Receive;

	PROCEDURE NewParent (): ActorCore.Actor;
		VAR a: Parent;
	BEGIN
		NEW(a); RETURN a
	END NewParent;

	PROCEDURE Run*;
		VAR p: ActorCore.Props; parent: ActorCore.Ref; inc: Inc; boom: Boom; t0: LONGINT;
			s: ARRAY 16 OF CHAR;
	BEGIN
		IF ActorRun.n = 0 THEN ActorRun.Start(4) END;
		starts := 0; last := 0; worker := NIL;
		p := ActorCore.NewProps(NewParent);
		p.strategy := ActorSup.NewOneForOne(ActorCore.Restart, 3, 1000);
		parent := ActorCore.Spawn(NIL, p, "parent");
		WHILE worker = NIL DO Services.actionHook.Step END;
		NEW(inc); NEW(boom);
		ActorCore.Tell(worker, inc); ActorCore.Tell(worker, inc);
		ActorCore.Tell(worker, boom);
		ActorCore.Tell(worker, inc);
		t0 := Kernel.Time();
		WHILE Kernel.Time() - t0 < 100 DO Services.actionHook.Step END;
		Strings.IntToString(starts, s); Console.WriteStr("ActorDocRestart: Started "); Console.WriteStr(s);
		Strings.IntToString(last, s); Console.WriteStr(", счёт после перезапуска "); Console.WriteStr(s);
		Console.WriteLn;
		ActorCore.StopActor(parent)
	END Run;

END ActorDocRestart.
```

Команда `ActorDocRestart.Run` печатает `ActorDocRestart: Started 2, счёт после перезапуска 1`: два
письма `Inc` до трапа пропали вместе со старым экземпляром. Пример `ActorDemoBank` (команда
`ActorDemoBank.Run`) показывает, почему для счетов выбран `Resume`. Банк — супервизор счетов и
переводов; каждый сотый перевод несёт нулевую сумму, и счёт трапует на предусловии. При `Resume`
остаток счёта сохраняется, перевод не получает ответа и по `ReceiveTimeout` считается отказанным, а
в конце сумма остатков равна начальной. При `Restart` новый экземпляр счёта начал бы с начального
остатка, и сумма денег изменилась бы на величину всех операций, прошедших через этот счёт.

`Escalate` передаёт отказ вверх: родитель приостанавливается и отказывает перед своим родителем.
Если затем родителя возобновят, вместе с ним возобновятся и его приостановленные дети; если
перезапустят или остановят — дети остановятся. Отказ, дошедший до корня, корень решает одной
директивой: он останавливает своего ребёнка, то есть актор верхнего уровня, через который прошёл
отказ, и печатает в `Console` строку `Actor: root stops <имя> after trap <номер>`. Остановка
уносит всё поддерево. Корень не перезапускает: у него нет фабрики, которой он мог бы доверять
больше, чем фабрике упавшего актора, а бесконечный цикл перезапусков на верхнем уровне некому
прервать. Тест `TestEscalateReachesRootWhichStops` проверяет, что наблюдатель актора верхнего
уровня со стратегией `Escalate` получает `Terminated`, а ребёнок получает `Stopped`.

Трап в обработчике `Restarting`, `Stopping` или `Stopped` отказом не считается: актор уже уходит, и
надзор ничего не может к этому добавить. Такой трап отмечается строкой
`Actor: trap in lifecycle of <имя>`. Трап внутри `Strategy.Decide` считается отказом родителя.

## 7. Отмена исполнителя

Задачу исполнителя можно отменить: `ActorRun.Stop` отменяет весь пул, а отменённая задача
прерывается трапом 127 в ближайшей точке переключения. Если эта точка внутри `Receive`, трап 127
пролетает через `Kernel.Try` обработчика. Отмена исполнителя не означает отказа актора: актор
ни в чём не виноват, его прервали снаружи. Поэтому трап 127 супервизору не передаётся. Письмо,
которое обрабатывалось, теряется по правилу «не более одного раза», ящик снимается с исполнителя
и, если в нём остались письма, снова ставится в очередь пула, а исполнитель завершается тем же
трапом 127. После следующего `ActorRun.Start` актор продолжает работу с прежним состоянием и
следующего письма (`TestCancelledExecutorIsNotFailure`: `Started` получен один раз, прерванное
письмо не завершено, следующее обработано).

Если бы трап 127 считался отказом, остановка пула при завершении программы превращалась бы в
лавину отказов, перезапусков и сообщений супервизорам, которые к тому же некому исполнять.

## 8. Актор и хранилище

`ActorCore.Actor` расширяет `Stores.Store`. Состояние экземпляра поэтому можно выносить и вносить
методами `Externalize` и `Internalize`, как состояние любой модели BlackBox; второму этапу
(журнал событий и снимки) не придётся менять базовый тип. Помех расширение не создаёт: у
`Stores.Store` нет абстрактных методов, поля `dlink`, `era`, `id` скрыты, а домен (`Domain`)
у экземпляра актора пуст, пока сам актор его не заведёт. Фабрика создаёт экземпляр обычным `NEW`.

## 9. Адрес и каталог

Адрес — абстрактная запись `Ref` с полями `node` и `name` и одним методом `Post(m, sender)`.
Локальный адрес — сам ящик (`Context` расширяет `Ref`). `Tell` зовёт `Post` и не знает, где
получатель. Модуль транспорта будущего этапа расширит `Ref` своим типом, чей `Post` упаковывает
письмо и отправляет его в другой процесс, а `ActorCore.InitRef` заполнит узел и имя.

Адрес по имени ищет каталог `ActorCore.dir` — по образцу `Files.dir`: абстрактный тип
`Directory` с методом `Lookup(node, name)`, переменные `dir` и `stdDir` и процедура `SetDir`.
Стандартный каталог знает только свой узел (`ActorCore.node = "local"`) и отвечает `NIL` на чужой.
Транспорт установит свой каталог, который чужой узел обслужит сам, а свой передаст `stdDir`.

Имя актора — строка из `Spawn`: `"counter"` у актора верхнего уровня, `"parent/worker"` у ребёнка,
`"$7"` у актора без имени. Два актора с одним именем трапуют в `Spawn` с кодом 24. Остановленный
актор уходит из каталога, и имя освобождается.

`Watch` и `StopActor` на первом этапе принимают только локальный адрес (трап 20): наблюдение и
остановку через сеть добавит транспорт.

## 10. Замеры

`Actor_devBench` печатает строки `BENCH` того же вида, что `Async_devBench`. На машине разработки
(Linux x86-64, три прогона подряд, `tools/test.sh Actor_devBench`) два актора, отвечающих друг
другу через `Respond`, тратят 78–82 нс на письмо, то есть 156–164 нс на обмен туда и обратно, при
одном исполнителе и при четырёх; обмен через две встречи `AsyncChan` в `docs/measurements.md`
стоит 389–483 нс. Разница объясняется переключениями: обмен задач через каналы переключает стек
четыре раза, а исполнитель обрабатывает оба ящика подряд и переключается 62 раза на тысячу писем
при одном исполнителе и 39 раз при четырёх. Выделений памяти в обмене нет.

Создание 10 000 акторов на четырёх исполнителях вместе с обработкой `Started` занимает
10,9–11,0 мкс на актор, доставка каждому по одному письму — 2,8–4,3 мкс на письмо. Профиль
(`tools/prof.sh`) отдаёт сборщику мусора больше 70 % выборок: у каждого ящика свои кольца очередей,
а адрес хранит два имени по 128 знаков.

## 11. Ограничения

- Одна нить ОС: акторы исполняются по очереди, вычислительная работа одного актора задерживает
  остальных до точки переключения исполнителя (исполнитель уступает каждые 64 ящика).
- Нет персистентности: `Restart` создаёт экземпляр с начальным состоянием.
- Нет транспорта: `Ref` другого узла можно описать, но доставлять ему некому.
- Стратегия одна — `OneForOne`; «все за одного» не реализована.
- Письмо с главного стека ждёт исполнителя до 10 мс (раздел 3).
- Отправка письма самому себе через `Ask` всегда кончается `timedOut`.

## 12. Интерфейс

Номера трапов: 20–25 — нарушение предусловия (номер указан у процедуры), 60 — фабрика вернула
`NIL`, 100 — нарушен инвариант модуля, 127 — задача отменена.

### 12.1. ActorCore

```
DEFINITION ActorCore;

	IMPORT Stores, AsyncSelect;

	CONST
		Resume = 0; Restart = 1; Stop = 2; Escalate = 3;
		ok = 0; timedOut = 1; dead = 2;
		nameLen = 128;

	TYPE
		Msg = AsyncSelect.Msg;
		Name = ARRAY 128 OF CHAR;

		Ref = POINTER TO ABSTRACT RECORD
			node-, name-: Name;
			(r: Ref) Post (m: Msg; sender: Ref), NEW, ABSTRACT
		END;

		Actor = POINTER TO ABSTRACT RECORD (Stores.Store)
			(a: Actor) Receive (c: Context; m: Msg), NEW, ABSTRACT
		END;

		Behavior = POINTER TO ABSTRACT RECORD
			(b: Behavior) Receive (a: Actor; c: Context; m: Msg), NEW, ABSTRACT
		END;

		Stats = RECORD
			failures: INTEGER;
			since: LONGINT
		END;

		Strategy = POINTER TO ABSTRACT RECORD
			(s: Strategy) Decide (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER, NEW, ABSTRACT
		END;

		Factory = PROCEDURE (): Actor;

		Props = POINTER TO RECORD
			new: Factory;
			capacity: INTEGER;
			dedicated: BOOLEAN;
			strategy: Strategy
		END;

		Dispatcher = POINTER TO ABSTRACT RECORD
			(d: Dispatcher) Schedule (c: Context), NEW, ABSTRACT
		END;

		Directory = POINTER TO ABSTRACT RECORD
			(d: Directory) Lookup (IN node, name: ARRAY OF CHAR): Ref, NEW, ABSTRACT
		END;

		Started = POINTER TO RECORD (Msg) END;
		Stopping = POINTER TO RECORD (Msg) END;
		Stopped = POINTER TO RECORD (Msg) END;
		Restarting = POINTER TO RECORD (Msg) END;
		ReceiveTimeout = POINTER TO RECORD (Msg) END;
		Terminated = POINTER TO RECORD (Msg)
			who-: Ref
		END;

		Context = POINTER TO LIMITED RECORD (Ref)
			host: ANYPTR;
			dedicated-: BOOLEAN;
			(c: Context) Post (m: Msg; sender: Ref);
			(c: Context) Tell (to: Ref; m: Msg), NEW;
			(c: Context) Sender (): Ref, NEW;
			(c: Context) Respond (m: Msg), NEW;
			(c: Context) Parent (): Ref, NEW;
			(c: Context) Watch (r: Ref), NEW;
			(c: Context) SetReceiveTimeout (ms: LONGINT), NEW;
			(c: Context) Become (b: Behavior), NEW;
			(c: Context) Pending (): BOOLEAN, NEW;
			(c: Context) IsStopped (): BOOLEAN, NEW
		END;

	VAR
		dispatcher-: Dispatcher;
		dir-, stdDir-: Directory;
		node-: Name;
		deadLetters-: LONGINT;

	PROCEDURE Process (c: Context; max: INTEGER): BOOLEAN;
	PROCEDURE NewProps (f: Factory): Props;
	PROCEDURE Spawn (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref;
	PROCEDURE Tell (to: Ref; m: Msg);
	PROCEDURE StopActor (r: Ref);
	PROCEDURE Ask (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER;
	PROCEDURE InitRef (r: Ref; IN node, name: ARRAY OF CHAR);
	PROCEDURE SetDispatcher (d: Dispatcher);
	PROCEDURE SetDir (d: Directory);

END ActorCore.
```

`Context` — ящик актора, контекст его обработчика и локальный адрес. Поле `host` принадлежит
исполнителю (`ActorRun` хранит в нём собственную задачу актора). Модуль мониторов не берёт:
операции над ящиком между двумя точками переключения неделимы.

**PROCEDURE (r: Ref) Post (m: Msg; sender: Ref)**
Пред: `m # NIL` — трап 20 у `Context`.
Пост: `m` в ящике адресата или учтено как мёртвое письмо; отправитель не ждёт.

**PROCEDURE (a: Actor) Receive (c: Context; m: Msg)**
Пред: зовёт только исполнитель, один вызов на актор за раз.
Пост: письмо обработано; трап — отказ актора, кроме трапа 127 (раздел 7).

**PROCEDURE (b: Behavior) Receive (a: Actor; c: Context; m: Msg)**
То же, что `Actor.Receive`, после `c.Become(b)`; `a` — экземпляр актора.

**PROCEDURE (s: Strategy) Decide (child: Ref; reason: INTEGER; VAR st: Stats): INTEGER**
Пред: ребёнок `child` отказал с трапом `reason`; `st` — его счёт отказов.
Пост: директива; значение вне `Resume..Escalate` исполняется как `Stop`; трап — отказ родителя.

**PROCEDURE (d: Dispatcher) Schedule (c: Context)**
Пред: у `c` есть работа, `c` не стоит у исполнителя.
Пост: исполнитель зовёт `Process(c, ...)`, пока тот возвращает `TRUE`.

**PROCEDURE (d: Directory) Lookup (IN node, name: ARRAY OF CHAR): Ref**
Пост: адрес актора `name` на узле `node`; `NIL` — неизвестен. Стандартный каталог знает только
`node = ActorCore.node`.

**PROCEDURE (c: Context) Tell (to: Ref; m: Msg)**
Пред: `to # NIL` — трап 20; `m # NIL` — трап 21.
Пост: `m` послано `to`, отправитель — `c`.

**PROCEDURE (c: Context) Sender (): Ref**
Пост: отправитель письма в обработке; `NIL` — без отправителя.

**PROCEDURE (c: Context) Respond (m: Msg)**
Пред: `m # NIL` — трап 20.
Пост: `m` послано отправителю письма в обработке; без отправителя — мёртвое письмо.

**PROCEDURE (c: Context) Parent (): Ref**
Пост: адрес родителя; `NIL` — родитель корень.

**PROCEDURE (c: Context) Watch (r: Ref)**
Пред: `r` — локальный адрес — трап 20.
Пост: после остановки `r` обработчик `c` получит `Terminated` с `who = r`, один раз на вызов;
`r` уже остановлен — получит сразу.

**PROCEDURE (c: Context) SetReceiveTimeout (ms: LONGINT)**
Пост: `ms > 0` — после `ms` мс без пользовательских писем обработчик получит `ReceiveTimeout`
один раз, следующий отсчёт начнёт следующее письмо; `ms <= 0` — тайм-аута нет. Перезапуск
снимает тайм-аут.

**PROCEDURE (c: Context) Become (b: Behavior)**
Пост: следующие письма обрабатывает `b.Receive`; `b = NIL` и перезапуск возвращают `Actor.Receive`.

**PROCEDURE (c: Context) Pending (): BOOLEAN**
Пост: `c` стоит у исполнителя.

**PROCEDURE (c: Context) IsStopped (): BOOLEAN**
Пост: `c` остановлен; письма ему — мёртвые.

**PROCEDURE Process (c: Context; max: INTEGER): BOOLEAN**
Зовёт исполнитель.
Пред: `c # NIL` — трап 20; `c` стоит у исполнителя и не в обработке — трап 21; `max > 0` —
трап 22.
Пост: обработаны все системные сообщения и не более `max` пользовательских; `TRUE` — работа
осталась, `c` по-прежнему у исполнителя; `FALSE` — работы нет, `c` снят с исполнителя. Отмена
задачи исполнителя внутри обработчика — трап 127 после того, как ящик снят с исполнителя и,
если в нём есть работа, снова отдан `Schedule`.

**PROCEDURE NewProps (f: Factory): Props**
Пред: `f # NIL` — трап 20.
Пост: описание с фабрикой `f`, ящик без предела, исполнитель пула, без стратегии.

**PROCEDURE Spawn (parent: Context; p: Props; IN name: ARRAY OF CHAR): Ref**
Пред: `p # NIL` и `p.new # NIL` — трап 20; `p.capacity >= 0` — трап 21; исполнитель установлен —
трап 22; `parent = NIL` или `parent` работает — трап 23; имя свободно — трап 24; полное имя
короче `nameLen` — трап 25; фабрика не трапует и не возвращает `NIL` — трап 60.
Пост: новый актор, ребёнок `parent` (`NIL` — корня); имя — `name`, у ребёнка — «имя родителя/
name», пустое `name` — `"$номер"`; первым обработчик получит `Started`.

**PROCEDURE Tell (to: Ref; m: Msg)**
Пред: `to # NIL` — трап 20; `m # NIL` — трап 21.
Пост: `m` послано `to` без отправителя; ожидания нет.

**PROCEDURE StopActor (r: Ref)**
Пред: `r` — локальный адрес — трап 20.
Пост: `r` получит остановку раньше своих пользовательских писем; остановленный `r` не меняется.

**PROCEDURE Ask (to: Ref; m: Msg; ms: LONGINT; OUT reply: Msg): INTEGER**
Ждёт ответа.
Пред: вызов из задачи — трап 20; `to # NIL` — трап 21; `m # NIL` — трап 22; отмена задачи —
трап 127.
Пост: `ok` — `reply` — первое письмо на адрес ответа; `timedOut` — за `ms` мс ответа нет; `dead`
— вопрос стал мёртвым письмом. Кроме `ok`, `reply = NIL`.

**PROCEDURE InitRef (r: Ref; IN node, name: ARRAY OF CHAR)**
Для модулей транспорта.
Пред: `r # NIL` — трап 20; `node` и `name` короче `nameLen` — трап 21.
Пост: `r.node = node`, `r.name = name`.

**PROCEDURE SetDispatcher (d: Dispatcher)**
Пред: `d # NIL` — трап 20.
Пост: `dispatcher = d`.

**PROCEDURE SetDir (d: Directory)**
Пред: `d # NIL` — трап 20.
Пост: `dir = d`; первый установленный каталог остаётся в `stdDir`.

### 12.2. ActorSup

```
DEFINITION ActorSup;

	IMPORT ActorCore;

	PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy;

END ActorSup.
```

**PROCEDURE NewOneForOne (directive, max: INTEGER; withinMs: LONGINT): ActorCore.Strategy**
Пред: `directive` — `Resume`, `Restart`, `Stop` или `Escalate` — трап 20; `max >= 0` — трап 21;
`withinMs > 0` — трап 22.
Пост: стратегия отвечает на отказ ребёнка директивой `directive`; для `Restart` — `Stop`, если
это отказ с номером `max + 1` за `withinMs` мс от первого отказа окна.

### 12.3. ActorRun

```
DEFINITION ActorRun;

	VAR
		n-: INTEGER;

	PROCEDURE Start (k: INTEGER);
	PROCEDURE Stop;

END ActorRun.
```

`n` — число исполнителей пула; 0 — пул остановлен.

**PROCEDURE Start (k: INTEGER)**
Пред: `0 < k <= 64` — трап 20; пул остановлен — трап 21.
Пост: пул из `k` исполнителей установлен в `ActorCore`; ящики, ждавшие в очереди, обслуживаются.

**PROCEDURE Stop**
Пост: задачи пула отменены (трап 127 в их точке переключения); ящики с работой ждут следующего
`Start`. Собственные задачи акторов не затронуты.

### 12.4. ActorDemoBank

```
DEFINITION ActorDemoBank;

	PROCEDURE Run;

END ActorDemoBank.
```

**PROCEDURE Run**
Команда. Запускает пул из четырёх исполнителей, если он остановлен.
Пост: тысяча переводов между десятью счетами завершена; напечатаны число выполненных и
отказанных переводов и сверка суммы остатков с начальной.
