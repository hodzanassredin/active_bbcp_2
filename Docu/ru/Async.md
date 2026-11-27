# Библиотека Async

Это руководство по подсистеме `Async`. Оно написано для программиста, который знает Component Pascal
и уже прочёл руководство по задачам (`Docu/ru/Tasks.md`). Библиотека построена на каркасе как он
есть: задачи, монитор-объект `Tasks.Object` и пропуск `Tasks.Ctx`.

Подсистема собирается для AMD64 и для i386 (`tools/build32.sh`, `tools/test32.sh`); только для AMD64 —
транспорт MPI `AsyncMpi`.
Каждый полный пример руководства лежит в `Obx/Mod64` и запускается командой `Do`; у каждого есть
своя страница в `Obx/Docu`.

## 1. От задачи к каналу

Две задачи делят работу: одна читает строки из файла, другая их разбирает. Первой нужно куда-то
класть прочитанное, второй — откуда-то брать. С монитором-объектом это делается так: заводят запись с
массивом и счётчиком, расширяют `Tasks.Object`, пишут операции `Put` и `Get` в `Exclusive`, а в
`Await` ждут флагов «есть элемент» и «есть место». Получается два десятка строк, и их
пишут заново для каждой пары задач, которой надо что-то передать.

Эти строки почти всегда одинаковы. Меняется только тип того, что передают, и размер запаса. Поэтому
библиотека даёт их готовыми под именем *канал*: объект с методами `Send` и `Recv`. Задача, которая
отправляет, не знает, кто получит; задача, которая получает, не знает, кто отправил. Обе знают только
канал. Так связь между задачами становится видна в одном месте — там, где канал создан и раздан, — а
не размазана по условиям `Await` в разных модулях.

Монитор остаётся нужен, когда задачи делят состояние, которое меняют по месту: таблицу, счётчик,
дерево. Канал нужен, когда задачи передают друг другу значения. Различие видно по вопросу, который
задаёт себе программист. «Кто сейчас может менять эту таблицу?» — вопрос для монитора. «Кому отдать
этот результат?» — вопрос для канала.

Примеры каналов в главе описывают задачи через `Tasks.Task` с методом `Run`: `Send` и `Recv` не
берут пропуск. Код, который входит в монитор-объект (`AsyncOnce`, `Tasks.Object`), пишут через
`Tasks.Job` с `Body-`: пропуск `Tasks.Ctx` создаёт библиотека и отпускает взятые мониторы при
трапе. `Task` — нижний уровень для планировщика, пула исполнителей и библиотеки над задачами.

По каналу идёт *сообщение* — указатель на запись, расширяющую `AsyncSelect.Msg`:

```
TYPE Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
```

Канал передаёт сам указатель и не копирует запись. Поэтому отправитель после `Send` запись больше не
меняет: получатель видит тот же объект, и правка после отправки дойдёт до него в непредсказуемый
момент. Компилятор этого не проверяет, и библиотека тоже. Получатель узнаёт вид сообщения
охраной типа `WITH m: Num DO … END`; поле `n` доступно только внутри охраны.

Каналы бывают трёх видов. Они различаются тем, сколько сообщений канал держит у себя, и потому
тем, когда отправитель ждёт:

| Вид | Конструктор | Держит | `Send` ждёт |
|---|---|---|---|
| встреча `Chan` | `AsyncChan.NewChan()` | ничего | пока получатель не заберёт |
| буфер `Buffer` | `AsyncChan.NewBuffer(n)` | до `n` | пока буфер полон |
| ящик `Mailbox` | `AsyncChan.NewMailbox()` | сколько угодно | не ждёт |

Следующие три раздела разбирают их по очереди.

## 2. Встреча

Задача-производитель считает числа, задача-потребитель их складывает. Если потребитель отстаёт,
производитель не должен убегать вперёд и копить числа в памяти: пусть подождёт. Если отстаёт
производитель, ждёт потребитель. Каждое число переходит из рук в руки в тот момент, когда обе
задачи готовы. Такая передача называется *встречей*.

Встреча — канал `AsyncChan.Chan`. Он не держит ни одного сообщения. Задача, пришедшая к каналу
первой, ждёт вторую: `Send` ждёт получателя, `Recv` — отправителя. Вторая задача забирает сообщение
у первой (или отдаёт ей своё) и будит её. Поэтому после возврата из `Send` отправитель знает больше,
чем «сообщение где-то лежит»: он знает, что получатель его уже взял. Несколько ждущих отправителей
обслуживаются в порядке прихода.

```
MODULE ObxChan0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Sender = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan END;
		Receiver = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Chan; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (s: Sender) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 5 DO NEW(m); m.n := i; s.c.Send(m) END;
		s.c.Send(NIL)
	END Body;

	PROCEDURE (r: Receiver) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			r.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO r.sum := r.sum + m.n END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; s: Sender; r: Receiver; d: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(s); s.c := c; NEW(r); r.c := c; done := FALSE;
		Tasks.Start(s); Tasks.Start(r);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(r.sum, d);
		Console.WriteStr("ObxChan0: sum="); Console.WriteStr(d); Console.WriteLn
	END Do;

END ObxChan0.
```

Команда `ObxChan0.Do` печатает `ObxChan0: sum=15`. Отправитель кончает поток сообщением `NIL`, и
получатель по нему выходит из цикла. Сама команда исполняется на главном стеке и ждать не может;
она крутит `Tasks.Step`, пока получатель не поставит `done`. В оконном BlackBox тот же цикл крутит
главный цикл каркаса, и команда возвращается сразу после `Tasks.Start`.

Ждать можно только задаче. Если команда сама позовёт `c.Recv(m)` и отправителя у канала не
окажется, `Recv` дойдёт до `Tasks.Park` и трапнет с кодом 20: на главном стеке ждать некому
уступать. Обработчик действия каркаса (`Services.Action`) и рассылка сообщений отображений тоже
ждать не могут; там тот же `Recv` трапнет с кодом 21. Если же отправитель у канала уже ждёт,
`Recv` с главного стека заберёт сообщение без ожидания и трапа не будет.

Второе нарушение — потерянная пара. Если в примере убрать отправку `NIL`, получатель навсегда
останется в `Recv`. Других задач нет, сроков пробуждения нет, и планировщик печатает в консоль
`Tasks: deadlock: no ready tasks and no wake-up deadlines`. Команда
`Do` при этом не вернётся, потому что `done` никто не поставит.

Встреча стоит двух переключений стека на сообщение: задача уходит к планировщику, планировщик
переходит к другой задаче. Замер `pingpong` (`docs/measurements.md`, раздел 2.5) — обмен туда и
обратно через две встречи — даёт 389–483 нс на обмен при загрузке машины около 15, вдвое медленнее Go
с одной нитью. Памяти встреча не выделяет: ждущая сторона берёт запись ожидания из свободного списка.

## 3. Буфер

Встреча заставляет производителя ждать каждого числа, даже когда потребитель отстаёт на мгновение.
Если скорости задач колеблются, лучше дать между ними небольшой запас: производитель забегает вперёд
на несколько сообщений, потребитель потом догоняет. Запас ограничен, чтобы быстрый производитель не
занял всю память.

Буфер `AsyncChan.Buffer` держит до `capacity` сообщений в порядке отправки. `Send` в неполный буфер
кладёт сообщение и возвращается сразу; `Send` в полный ждёт, пока потребитель не освободит место.
`Recv` из непустого буфера берёт первое сообщение; из пустого — ждёт. Поле `count` показывает,
сколько сообщений лежит в буфере сейчас.

```
MODULE ObxChan1;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Num = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Producer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; max: INTEGER END;
		Consumer = POINTER TO RECORD (Tasks.Job) c: AsyncChan.Buffer; sum: INTEGER END;

	VAR done: BOOLEAN;

	PROCEDURE (p: Producer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: Num; i: INTEGER;
	BEGIN
		FOR i := 1 TO 10 DO
			NEW(m); m.n := i; p.c.Send(m);
			IF p.c.count > p.max THEN p.max := p.c.count END
		END;
		p.c.Send(NIL)
	END Body;

	PROCEDURE (k: Consumer) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			k.c.Recv(m);
			IF m = NIL THEN EXIT END;
			WITH m: Num DO k.sum := k.sum + m.n END;
			Tasks.Sleep(1)	(* потребитель медленнее производителя *)
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: AsyncChan.Buffer; p: Producer; k: Consumer; s: ARRAY 16 OF CHAR;
	BEGIN
		c := AsyncChan.NewBuffer(4);
		NEW(p); p.c := c; NEW(k); k.c := c; done := FALSE;
		Tasks.Start(p); Tasks.Start(k);
		WHILE ~done DO Tasks.Step END;
		Console.WriteStr("ObxChan1: sum="); Strings.IntToString(k.sum, s); Console.WriteStr(s);
		Console.WriteStr(" max="); Strings.IntToString(p.max, s); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan1.
```

Команда печатает `ObxChan1: sum=55 max=4`. Потребитель после каждого сообщения спит миллисекунду,
производитель быстрее, и буфер заполняется до ёмкости. Больше четырёх сообщений в нём не бывает:
пятый `Send` ждёт.

Ёмкость должна быть положительной. `NewBuffer(0)` трапает с кодом 20: буфер без места был бы
встречей, а встреча — отдельный вид канала. Верхней границы ёмкости библиотека не ставит.

Буфер дешевле встречи, когда он не пуст и не полон: отправка и приём тогда обходятся без
переключения. В замере `buffer64` (буфер на 64 сообщения) приходится 0,06 переключения на сообщение
и 117–186 нс на сообщение; почти половину этого времени занимает `NEW` сообщения в самом замере.

## 4. Почтовый ящик

Бывает отправитель, которому ждать нельзя совсем. Обработчик действия каркаса, команда меню,
процедура, вызванная из рассылки сообщений, исполняются на главном стеке, и любое ожидание там —
трап. Такому отправителю нужен канал, `Send` в который никогда не ждёт.

Почтовый ящик `AsyncChan.Mailbox` не ограничен: он держит любое число сообщений, и `Send` в него
возвращается сразу. `Recv` ждёт, пока ящик пуст. Сообщения выходят в порядке отправки.

```
MODULE ObxChan2;
	IMPORT AsyncChan, AsyncSelect, Tasks, Console, Strings;

	TYPE
		Event = POINTER TO RECORD (AsyncSelect.Msg) code: INTEGER END;
		Handler = POINTER TO RECORD (Tasks.Job) n: INTEGER END;

	VAR box: AsyncChan.Mailbox; done: BOOLEAN;

	PROCEDURE Post (code: INTEGER);
	(* Вызывается с главного стека: ящик не заставляет ждать. *)
		VAR e: Event;
	BEGIN
		NEW(e); e.code := code; box.Send(e)
	END Post;

	PROCEDURE (h: Handler) Body- (VAR ctx: Tasks.Ctx);
		VAR m: AsyncSelect.Msg;
	BEGIN
		LOOP
			box.Recv(m);
			WITH m: Event DO
				IF m.code = 0 THEN EXIT END;
				INC(h.n)
			END
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR h: Handler; s: ARRAY 16 OF CHAR;
	BEGIN
		box := AsyncChan.NewMailbox();
		Post(1); Post(2); Post(3); Post(0);
		NEW(h); done := FALSE; Tasks.Start(h);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(h.n, s);
		Console.WriteStr("ObxChan2: events="); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxChan2.
```

Команда печатает `ObxChan2: events=3`. Процедура `Post` вызывается с главного стека до того, как
задача-обработчик запущена: ящик принимает все четыре сообщения и ничего не ждёт. Задача потом
вынимает их по одному и выходит на событии с кодом 0.

Ящик не ждёт места: `Send` только дописывает сообщение. Поэтому отправка в ящик с главного стека
допустима.

Цена неограниченности — память. Если потребитель отстаёт навсегда, ящик растёт без предела: кольцо
сообщений удваивается при заполнении и назад не сжимается. Ящик подходит для потоков событий, у
которых есть естественный предел, — нажатий клавиш, ответов на свои же запросы, — а не для
перекачки данных между задачами разной скорости. Для этого есть буфер.

## 5. Выбор

Задача-клиент посылает запрос серверу и ждёт ответа. Сервер может зависнуть, и клиент не хочет ждать
вечно: через 50 мс он сдаётся и делает что-то другое. Ни `Recv`, ни `Sleep` здесь не годятся: `Recv`
ждёт без срока, а `Sleep` не замечает ответа. Нужно ждать сразу двух событий и узнать, какое
случилось первым.

Это делает *выбор* — процедура `AsyncChan.Select`. Она получает массив *альтернатив* типа
`AsyncSelect.Op`, ждёт, пока исполнится ровно одна из них, и возвращает её индекс. Альтернативу
строят процедуры:

```
AsyncChan.RecvAlt(c, op)        (* принять из канала c *)
AsyncChan.SendAlt(c, m, op)     (* отправить m в канал c *)
AsyncChan.TimeoutAlt(ms, op)    (* истечь через ms мс *)
AsyncSelect.AltSignal(sig, op)  (* дождаться внешнего сигнала *)
```

Альтернатива приёма, которая победила, оставляет сообщение в поле `got`; остальные альтернативы не
исполняются вовсе. Отправка, проигравшая выбор, сообщения не отправила.

```
MODULE ObxSelect0;
	IMPORT AsyncChan, AsyncSelect, Tasks, Services, AsyncIO, Console;

	TYPE
		Reply = POINTER TO RECORD (AsyncSelect.Msg) END;
		Server = POINTER TO RECORD (Tasks.Job) delay: INTEGER; out: AsyncChan.Mailbox END;
		Client = POINTER TO RECORD (Tasks.Job) END;

	VAR fast, slow: ARRAY 16 OF CHAR; done: BOOLEAN;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Reply;
	BEGIN
		Tasks.Sleep(s.delay); NEW(r); s.out.Send(r)
	END Body;

	PROCEDURE Ask (delay: INTEGER; OUT res: ARRAY OF CHAR);
		VAR s: Server; ops: ARRAY 2 OF AsyncSelect.Op;
	BEGIN
		NEW(s); s.delay := delay; s.out := AsyncChan.NewMailbox(); Tasks.Start(s);
		AsyncChan.RecvAlt(s.out, ops[0]);
		AsyncChan.TimeoutAlt(50, ops[1]);
		IF AsyncChan.Select(ops) = 0 THEN res := "reply" ELSE res := "timeout" END
	END Ask;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Ask(10, fast); Ask(200, slow); done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR c: Client;
	BEGIN
		NEW(c); done := FALSE; Tasks.Start(c);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxSelect0: fast="); Console.WriteStr(fast);
		Console.WriteStr(" slow="); Console.WriteStr(slow); Console.WriteLn
	END Do;

END ObxSelect0.
```

Команда печатает `ObxSelect0: fast=reply slow=timeout`. Первый сервер отвечает через 10 мс, и
побеждает приём. Второй отвечает через 200 мс, и первым истекает тайм-аут. Сервер отвечает в
почтовый ящик, а не во встречу, и в этом есть смысл: опоздавший ответ ложится в ящик, и сервер
завершается. Ответь он во встречу, он бы навсегда остался ждать получателя, которого уже нет.

В этом примере команда крутит не `Tasks.Step`, а `Services.actionHook.Step` и `AsyncIO.Poll(1)`.
Тайм-аут выбора — действие `Services`, и `Tasks.Step` его не исполняет; без шага действий срок
истёк бы незамеченным, и второй выбор тоже дождался бы ответа. Главный цикл BlackBox делает оба шага
сам; командам с тайм-аутами, рабочими нитями и вводом-выводом приходится повторять его.

Порядок решает, когда готовы сразу несколько альтернатив. `Select` проверяет их по возрастанию
индекса, и побеждает первая, которая может исполниться немедленно. Тайм-аут готов только тогда, когда
не готово ничего другое: тайм-аут с `ms <= 0` превращает выбор в опрос без ожидания. Из нескольких
тайм-аутов действует наименьший; при равных — первый по индексу.

Нарушения выбора ловятся до того, как он что-либо сделает. Вызов не из задачи трапает с кодом 20;
больше 32 альтернатив — с кодом 21; альтернатива с недопустимым видом — с кодом 22; приём или
отправка без канала — с кодом 23; сигнал `NIL` — с кодом 24. Незаполненная альтернатива с нулевым
полем вида выглядит как отправка без канала и даёт трап 23.

Отмена задачи, которая ждёт в выборе, трапает её с кодом 127 в `Tasks.Park`. Её предложения
остаются в каналах до первого обращения к ним: канал считает предложение мёртвым, когда его задача
дошла до `Finished` или `Cancelled`, и пропускает его.

Выбор из двух встреч стоит 315–400 нс на операцию, из восьми — 568–653 нс (`select2`, `select8`,
`docs/measurements.md`, раздел 2.5). Выбор без тайм-аута памяти не выделяет.

### 5.1. События

Выбор через `AsyncChan.Select` — плоский массив, собранный в месте вызова; результат — индекс.
Этого достаточно для одного ожидания, но не тогда, когда ожидания передают как значения: сервер
соединяет своё ожидание с ожиданием, полученным от клиента, а процедура возвращает «приём из этого
канала», и вызывающий добавляет срок.

*Событие* — значение типа `AsyncEvent.Event`, описывающее ожидание. `AsyncEvent.Sync(e)` исполняет
его и возвращает результат: сообщение приёма, значение, полученное действием, или `NIL` для
отправки, тайм-аута, сигнала, `Always` и уведомления. Базовые события строят:

```
AsyncEvent.Send(c, m)          (* отправить m в канал c *)
AsyncEvent.Recv(c)             (* принять из c *)
AsyncEvent.Timeout(ms)         (* ms мс от начала синхронизации *)
AsyncEvent.Signal(sig)         (* готовность внешнего AsyncSelect.Signal *)
AsyncEvent.Always()            (* готово сразу *)
AsyncEvent.Never()             (* не готово никогда *)
AsyncEvent.Choose(events)      (* одно из событий, по порядку *)
AsyncEvent.Wrap(e, action)     (* пропустить результат через action.Do *)
AsyncEvent.Guard(builder)      (* построить событие при синхронизации *)
AsyncEvent.WithNack(e, nack)   (* уведомление, если e проиграет *)
```

`Choose` соединяет события в одно ожидание и вкладывается на любую глубину. Его порядок — порядок
плоских альтернатив в `Select`: среди готовых ветвей побеждает первая, готовая отправка или приём
побеждает тайм-аут, даже если тайм-аут стоит перед ним, а `Always` готов, когда до него дошла проверка.
`Never` даёт альтернативу, которая не решает никогда. Предел — предел `Select`: не больше 32
атомарных операций в одной синхронизации; большее дерево трапает с кодом 23.

Действие `Wrap` исполняется после решения и только в победившей ветви, от внутреннего события к
внешнему. Действие — объект с методом `Do`; оно может ждать, как отправка в канале обмена. Его
значение становится результатом события, поэтому вложенные обёртки преобразуют результат изнутри
наружу.

`Guard` зовёт `Builder.Make` при каждой синхронизации, по одному разу на каждое вхождение
построителя в дерево. Так появляется свежий канал или свежий тайм-аут в момент ожидания; событие
срока, переиспользованное для двух вызовов, каждый раз заново считает остаток до одного и того же
абсолютного срока.

`WithNack(e, nack)` возвращает событие `e` и уведомление `nack`. Уведомление зажигается, когда
ветвь `e` проигрывает выбор, когда синхронизацию останавливает отмена или срок и когда её прерывает
трап. Уведомление — обычное событие: его можно выбирать вместе с другими, и другая задача может
синхронизироваться на нём. Памяти у него нет, как в Concurrent ML: если в момент зажигания никто не
ждёт, уведомление теряется. `withNackJob` в Hopac возвращает обещание, которое помнит исход; здесь
уведомление — событие, и позднее ожидание ждёт следующего проигрыша.

Синхронизация — это один `AsyncSelect.Select` под деревом. Простое событие канала памяти не
выделяет; `Wrap` и `WithNack` берут записи из свободных списков, а `Guard` строит только то, что
строит его построитель.
`SyncCtx` — пара к `SelectCtx`: остановленный пропуск возвращает `FALSE` и зажигает уведомления всех
проигравших ветвей; трап передаётся дальше после уведомлений. Синхронизация вне задачи трапает с
кодом 20, как выбор.

```
MODULE ObxEvent0;
(** Канал обмена: два процесса отдают друг другу значения одним событием.

	Идея из Concurrent ML (Reppy): канал передаёт пару «значение и канал
	ответа». Синхронизация — выбор из двух ветвей: принять чужое
	предложение и ответить в чужой канал либо послать своё и дождаться
	ответа. Канал ответа создаётся заново на каждую синхронизацию, поэтому
	событие строит Guard. *)

	IMPORT Services, Tasks, AsyncSelect, AsyncChan, AsyncEvent, Console;

	TYPE
		NumMsg = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;

		SwapMsg = POINTER TO RECORD (AsyncSelect.Msg)
			n: INTEGER;
			reply: AsyncChan.Chan
		END;

		TakeAction = POINTER TO RECORD (AsyncEvent.Action)
			n: INTEGER
		END;

		AwaitAction = POINTER TO RECORD (AsyncEvent.Action)
			reply: AsyncChan.Chan
		END;

		SwapBuilder = POINTER TO RECORD (AsyncEvent.Builder)
			c: AsyncChan.Chan; n: INTEGER
		END;

		Swapper = POINTER TO RECORD (Tasks.Task)
			c: AsyncChan.Chan; n, got: INTEGER; done: BOOLEAN
		END;

	VAR
		(** Результаты последнего обмена: кто что получил. *)
		First*, Second*: INTEGER;

	PROCEDURE Num (n: INTEGER): NumMsg;
		VAR m: NumMsg;
	BEGIN
		NEW(m); m.n := n;
		RETURN m
	END Num;

	PROCEDURE (a: TakeAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: NumMsg;
	BEGIN
		WITH m: SwapMsg DO
			m.reply.Send(Num(a.n));	(* ответ в канал чужого предложения *)
			NEW(r); r.n := m.n; y := r
		ELSE y := NIL
		END
	END Do;

	PROCEDURE (a: AwaitAction) Do (m: AsyncSelect.Msg; OUT y: AsyncSelect.Msg);
		VAR r: AsyncSelect.Msg;
	BEGIN
		a.reply.Recv(r); y := r
	END Do;

	PROCEDURE (b: SwapBuilder) Make (OUT e: AsyncEvent.Event);
		VAR reply: AsyncChan.Chan; offer: SwapMsg;
			take: TakeAction; await: AwaitAction;
			evs: ARRAY 2 OF AsyncEvent.Event;
	BEGIN
		reply := AsyncChan.NewChan();
		NEW(offer); offer.n := b.n; offer.reply := reply;
		NEW(take); take.n := b.n;
		NEW(await); await.reply := reply;
		evs[0] := AsyncEvent.Wrap(AsyncEvent.Recv(b.c), take);
		evs[1] := AsyncEvent.Wrap(AsyncEvent.Send(b.c, offer), await);
		e := AsyncEvent.Choose(evs)
	END Make;

	PROCEDURE Swap (c: AsyncChan.Chan; n: INTEGER): INTEGER;
		VAR b: SwapBuilder; m: AsyncSelect.Msg;
	BEGIN
		NEW(b); b.c := c; b.n := n;
		m := AsyncEvent.Sync(AsyncEvent.Guard(b));
		WITH m: NumMsg DO RETURN m.n ELSE RETURN -1 END
	END Swap;

	PROCEDURE (s: Swapper) Run;
	BEGIN
		s.got := Swap(s.c, s.n); s.done := TRUE
	END Run;

	PROCEDURE Do*;
		VAR c: AsyncChan.Chan; a, b: Swapper;
	BEGIN
		c := AsyncChan.NewChan();
		NEW(a); a.c := c; a.n := 1; a.done := FALSE;
		NEW(b); b.c := c; b.n := 2; b.done := FALSE;
		Tasks.Start(a); Tasks.Start(b);
		WHILE ~(a.done & b.done) DO Services.actionHook.Step END;
		First := a.got; Second := b.got;
		Console.WriteStr("ObxEvent0: a=");
		IF First = 2 THEN Console.WriteStr("2") ELSE Console.WriteStr("?") END;
		Console.WriteStr(" b=");
		IF Second = 1 THEN Console.WriteStr("1") ELSE Console.WriteStr("?") END;
		Console.WriteLn
	END Do;

END ObxEvent0.
```

Команда `ObxEvent0.Do` печатает `ObxEvent0: a=2 b=1`: первая задача отдала 1 и получила 2, вторая
отдала 2 и получила 1. Действие победившей ветви ждёт во встрече, поэтому синхронизация кончается
только после того, как оба значения перешли из рук в руки.

События отличаются от Concurrent ML и Hopac в нескольких местах. В Component Pascal нет замыканий,
поэтому `Wrap` и `Guard` принимают объекты с методами `Do` и `Make`. `WithNack` принимает событие, а
не функцию; ленивое построение даёт `Guard`: `WithNack(Guard(builder), nack)`. Уведомление — событие
без памяти, как в CML, а не однократное обещание, как в Hopac. Нить ОС одна, вытеснения нет:
действие, которое ждёт, — точка переключения, и задача исполняет одну синхронизацию за раз. Порядок
`Choose` такой же, как у `Select`; CML оставляет порядок `choose` неопределённым, а `Alt.choose` в
Hopac перебирает альтернативы по порядку. Синхронизация начинается в задаче; неблокирующего опроса
события нет. `Sync` с готовыми ветвями возвращается без переключения, но всё равно идёт через
`Select` и потому требует задачу.

Цена синхронизации — цена `Select` плюс обход дерева; отдельного замера событий это руководство не
даёт. События не меняют `AsyncSelect`, его замеры описаны в разделе 2.5.

## 6. Протокол диалога

Клиент и сервер разговаривают по каналу: клиент здоровается, шлёт данные, прощается. Если клиент
по ошибке пришлёт данные до приветствия, сервер получит то, чего не ждёт, и ошибка проявится далеко
от места, где её сделали. Лучше остановить неверное сообщение в момент отправки.

Для этого к каналу прикрепляют *протокол* — автомат состояний, записанный текстом:

```
start: Hello > talk; talk: Data > talk; talk: Bye > done; done:
```

Пункт `состояние: Сообщение > состояние` разрешает из состояния отправку сообщения и называет
следующее состояние. Стрелку можно писать `>` или `->`; пункты разделяет `;` или перевод строки.
Первое упомянутое состояние — начальное. Пункт `done:` без стрелки объявляет состояние, из которого
переходов нет; такое состояние конечно. Сообщение называют именем его типа записи, как в
объявлении `TYPE Hello = POINTER TO RECORD …`; `NIL` называют `NIL`.

```
MODULE ObxProto0;
	IMPORT AsyncChan, AsyncSelect, AsyncProto, Console;

	CONST desc = "start: Hello > talk; talk: Data > talk; talk: Bye > done; done:";

	TYPE
		Hello = POINTER TO RECORD (AsyncSelect.Msg) END;
		Data = POINTER TO RECORD (AsyncSelect.Msg) n: INTEGER END;
		Bye = POINTER TO RECORD (AsyncSelect.Msg) END;

	PROCEDURE Do*;
		VAR p: AsyncProto.Protocol; box: AsyncChan.Mailbox;
			h: Hello; d: Data; b: Bye; name: ARRAY 32 OF CHAR;
	BEGIN
		p := AsyncProto.New(desc);
		box := AsyncChan.NewMailbox(); AsyncChan.SetProtocol(box, p);
		NEW(h); box.Send(h);
		NEW(d); d.n := 1; box.Send(d);
		NEW(d); d.n := 2; box.Send(d);
		NEW(b); box.Send(b);
		AsyncProto.StateName(p, box.protoState, name);
		Console.WriteStr("ObxProto0: state="); Console.WriteStr(name); Console.WriteLn
	END Do;

END ObxProto0.
```

Команда печатает `ObxProto0: state=done`: четыре отправки прошли по автомату от `start` до `done`.
Ящик здесь выбран затем, чтобы команда могла отправлять с главного стека.

Нарушение выглядит так. Если вставить в пример `NEW(d); box.Send(d)` до приветствия, консоль
покажет строку `AsyncChan protocol: …` с текстом нарушения и ожидаемыми сообщениями, а `Send`
трапнет с кодом 21. Сообщение не доставлено, состояние протокола осталось прежним; текст нарушения
хранит `AsyncProto.LastViolation`. Трап получает всегда отправитель. Если отправитель ждал во
встрече и его сообщение забирает получатель, трапнет отправитель, проснувшись, а получатель
продолжит ждать следующего.

Протокол проверяет сообщение в тот момент, когда отправка совершается: при входе в `Send`, если
получатель ждёт или есть место; когда получатель забирает ждущую отправку; когда в буфере
освобождается место. Отправка, проигравшая выбор, протокол не двигает. `AsyncChan.SetProtocol`
сбрасывает состояние в начальное; `SetProtocol(c, NIL)` снимает протокол. Канал без протокола
платит за проверку одним сравнением с `NIL`.

У описания есть пределы: до 32 состояний и 128 переходов, имя — до 31 символа. Ошибка описания
(пункт без `:`, длинное имя, два перехода из одного состояния по одному сообщению) даёт `NIL` из
`AsyncProto.New`, а текст ошибки — из `AsyncProto.LastError`. Автомат детерминирован; повторений
`*`, `+` и выбора `|` в записи нет — их выражают состояниями.

## 7. Однократная переменная

Задача запускает долгий расчёт и раздаёт нескольким другим задачам обещание результата. Каждая
из них в нужный момент читает результат; если он ещё не готов, ждёт. Канал здесь не подходит:
сообщение из канала получает один получатель, а результат нужен всем.

*Однократная переменная* `AsyncOnce.Once` заполняется один раз и читается сколько угодно раз.
`FillInt(ctx, x)` или `FillMsg(ctx, m)` её заполняют; `ReadInt(ctx, x)` или `ReadMsg(ctx, m)` ждут
заполнения и возвращают значение. Переменная устроена как монитор-объект: операции идут в
`Once.Exclusive`, а чтение ждёт в `Await` флага `filled`. Всем четырём методам нужен пропуск
`Tasks.Ctx`.

```
MODULE ObxOnce;
	IMPORT AsyncOnce, Tasks, Console, Strings;

	TYPE
		Reader = POINTER TO RECORD (Tasks.Job) END;
		Writer = POINTER TO RECORD (Tasks.Job) END;

	VAR o: AsyncOnce.Once; got: INTEGER; done: BOOLEAN;

	PROCEDURE (r: Reader) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		done := o.ReadInt(ctx, got)	(* ждёт, пока Writer не заполнит; FALSE — задачу остановили *)
	END Body;

	PROCEDURE (w: Writer) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(5); o.FillInt(ctx, 42)
	END Body;

	PROCEDURE Do*;
		VAR r: Reader; w: Writer; s: ARRAY 16 OF CHAR;
	BEGIN
		o := AsyncOnce.New();
		NEW(r); NEW(w); done := FALSE;
		Tasks.Start(r); Tasks.Start(w);
		WHILE ~done DO Tasks.Step END;
		Strings.IntToString(got, s);
		Console.WriteStr("ObxOnce: "); Console.WriteStr(s); Console.WriteLn
	END Do;

END ObxOnce.
```

Команда печатает `ObxOnce: 42`. Читатель приходит первым и ждёт; писатель через 5 мс заполняет
переменную, и читатель просыпается.

Второе заполнение — ошибка. `FillInt` по заполненной переменной трапает с кодом 21, `FillMsg` — с
кодом 22. Признак заполнения у переменной один на оба вида значения, поэтому `FillMsg` после
`FillInt` — тоже второе заполнение. Вид при чтении не проверяется: `ReadInt` после `FillMsg` даст 0,
`ReadMsg` после `FillInt` — `NIL`, оба с результатом `TRUE`.

И заполнение, и чтение требуют задачи и пропуска `Tasks.Ctx`: `Do` с главного стека трапает 22.
Чтение незаполненной переменной ждёт в `Await`; пропуск может остановить ожидание, и тогда
`ReadInt` и `ReadMsg` вернут `FALSE` (значение — 0 или `NIL`), как `RecvCtx` у канала. Своего срока у чтения нет; если срок нужен, результат
передают по каналу и ждут его в выборе с тайм-аутом.

## 8. Транзакции

Задача переводит деньги со счёта `a` на счёт `b`: снимает сумму с `a`, затем зачисляет на `b`.
Между двумя действиями нет точки переключения, поэтому другие задачи половины перевода не видят.
Но между ними может случиться трап — например, счёт `b` не принимает больше 150, и проверка
предела трапует. Задача погибает, сумма уже снята с `a` и никуда не зачислена: деньги пропали.
Монитор здесь не помогает: он исключает чужие задачи, а трап происходит в своей.

Вторая задача ждёт, пока на счёте покупателя наберётся цена и откроется магазин. Счёт и признак
«открыто» — разные объекты со своими мониторами-объектами. `Await` ждёт условия внутри одного
монитора, и условие над двумя объектами им не выразить, не слив оба объекта в один монитор.

Обе задачи решает *транзакция* — модуль `AsyncStm`. Транзакция — объект-расширение
`AsyncStm.Tx` с методом `Do`; данные транзакции лежат в полях объекта, как данные задачи — в
полях `Tasks.Task`. `AsyncStm.Atomically(t)` исполняет `t.Do` так, что остальные задачи видят либо
все его записи, либо ни одной. Пишет транзакция в *транзакционные переменные*: `IntVar`,
`BoolVar`, `RealVar`, `RefVar` (`ANYPTR`) или своё расширение `Var`. Первая запись в переменную
внутри транзакции кладёт её прежнее значение в журнал отката, затем меняет значение на месте. Если
`Do` трапует, `Atomically` возвращает переменным значения из журнала и передаёт трап вызывающему с
тем же номером. Пример перевода:

```
MODULE ObxStm0;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST limit = 150;	(* больше этого счёт b не принимает *)

	TYPE
		Transfer = POINTER TO RECORD (AsyncStm.Tx)
			from, to: AsyncStm.IntVar; n: INTEGER
		END;
		Clerk = POINTER TO RECORD (Tasks.Job) END;

	VAR a, b: AsyncStm.IntVar;

	PROCEDURE (t: Transfer) Do;
	BEGIN
		t.from.Set(t.from.Get() - t.n);	(* снять *)
		ASSERT(t.to.Get() + t.n <= limit, 30);	(* трап между «снять» и «зачислить» *)
		t.to.Set(t.to.Get() + t.n)	(* зачислить *)
	END Do;

	PROCEDURE Move (n: INTEGER);
		VAR t: Transfer;
	BEGIN
		NEW(t); t.from := a; t.to := b; t.n := n;
		AsyncStm.Atomically(t)
	END Move;

	PROCEDURE (c: Clerk) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Move(100); Move(100)
	END Body;

	PROCEDURE Show;
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Console.WriteStr("a = "); Strings.IntToString(a.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", b = "); Strings.IntToString(b.Get(), s); Console.WriteStr(s);
		Console.WriteStr(", sum = "); Strings.IntToString(a.Get() + b.Get(), s); Console.WriteStr(s);
		Console.WriteLn
	END Show;

	PROCEDURE Do*;
		VAR c: Clerk;
	BEGIN
		a := AsyncStm.NewInt(300); b := AsyncStm.NewInt(0);
		NEW(c); Tasks.Start(c);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.Failed(c) THEN Console.WriteStr("clerk trapped") ELSE Console.WriteStr("clerk done") END;
		Console.WriteLn;
		Show
	END Do;

END ObxStm0.
```

Команда печатает `clerk trapped` и `a = 200, b = 100, sum = 300`. Первый перевод прошёл, второй
трапнул с кодом 30 после снятия; `Atomically` вернул `a` значение 200, и сумма не изменилась. Разбор
по шагам — на странице `Obx/Docu/ru/Stm0.md`.

Журнал отката выбран вместо отложенных записей из-за цены чтения. При отложенных записях новое
значение лежит в журнале до конца транзакции, и каждое чтение сначала ищет переменную в журнале,
чтобы транзакция видела свою запись. При журнале отката значение всегда в самой переменной, и
чтение стоит одного сравнения. Отложенные записи нужны там, где чужая нить может прочитать
переменную посреди транзакции; здесь её никто не прочтёт, потому что внутри транзакции нет точек
переключения. По той же причине транзакциям нечем столкнуться, и проверка версий при фиксации не
нужна.

Ожидание условия даёт `AsyncStm.Retry`. Транзакция зовёт его, когда продолжать рано. `Retry`
откатывает транзакцию, и `Atomically` усыпляет задачу, пока не изменится одна из переменных,
которые транзакция успела прочитать; затем `Do` исполняется заново. Каждый `Get` вносит переменную
в множество прочитанных, поэтому условие может охватывать сколько угодно переменных из разных
объектов:

```
MODULE ObxStm1;
	IMPORT Tasks, AsyncStm, Console, Strings;

	CONST price = 250;

	TYPE
		Buy = POINTER TO RECORD (AsyncStm.Tx) tries: INTEGER END;
		Buyer = POINTER TO RECORD (Tasks.Job) END;
		Payroll = POINTER TO RECORD (Tasks.Job) END;
		Opener = POINTER TO RECORD (Tasks.Job) END;

	VAR
		money: AsyncStm.IntVar;	(* счёт покупателя *)
		open: AsyncStm.BoolVar;	(* магазин открыт *)

	PROCEDURE Say (IN what: ARRAY OF CHAR; x: INTEGER);
		VAR s: ARRAY 16 OF CHAR;
	BEGIN
		Strings.IntToString(x, s);
		Console.WriteStr(what); Console.WriteStr(s); Console.WriteLn
	END Say;

	PROCEDURE (t: Buy) Do;
	BEGIN
		INC(t.tries);	(* обычное поле: откат его не трогает *)
		IF (money.Get() < price) OR ~open.Get() THEN AsyncStm.Retry END;
		money.Set(money.Get() - price)
	END Do;

	PROCEDURE (b: Buyer) Body- (VAR ctx: Tasks.Ctx);
		VAR t: Buy;
	BEGIN
		NEW(t); t.tries := 0;
		AsyncStm.Atomically(t);
		Say("bought, money = ", money.Get());
		Say("transaction ran times: ", t.tries)
	END Body;

	PROCEDURE (p: Payroll) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(10);
		money.Set(money.Get() + 200);
		Say("salary paid, money = ", money.Get())
	END Body;

	PROCEDURE (o: Opener) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(20);
		open.Set(TRUE);
		Console.WriteStr("shop opened"); Console.WriteLn
	END Body;

	PROCEDURE Do*;
		VAR b: Buyer; p: Payroll; o: Opener;
	BEGIN
		money := AsyncStm.NewInt(100); open := AsyncStm.NewBool(FALSE);
		NEW(b); NEW(p); NEW(o);
		Tasks.Start(b); Tasks.Start(p); Tasks.Start(o);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END ObxStm1.
```

Покупатель просыпается дважды: после зарплаты (денег 300, магазин закрыт — снова `Retry`) и после
открытия. Поле `tries` показывает, что `Do` исполнялся три раза; это обычное поле, и откат его не
трогает. Запись вне транзакции, как здесь `money.Set` в задаче `Payroll`, — обычная запись в
переменную, которая будит ждущих её транзакций. Запрет ничего бы не дал: между точками
переключения такая запись и так неделима.

Две готовые транзакции складываются в одну, не открывая их. Вызов `Atomically` внутри транзакции —
часть внешней транзакции: перевод `Move(a, b)` и перевод `Move(b, c)`, вызванные из одного `Do`,
исполняются как одно целое, и трап во втором откатывает и первый. `AsyncStm.OrElse(x, y)`
исполняет `x`; если `x` сделала `Retry`, `OrElse` откатывает записи `x` и исполняет `y`. Если
`Retry` сделали обе, ждёт вся транзакция — до изменения переменной, прочитанной в любой из
ветвей. Пример «плати с первой карты, иначе со второй» — `ObxStm2`, страница
`Obx/Docu/ru/Stm2.md`.

Нарушение первое: точка переключения внутри транзакции. Если `Do` зовёт `Tasks.Yield`,
`Tasks.Sleep` или ждёт в канале, другая задача увидела бы записи незаконченной транзакции.
`Atomically` открывает на время `Do` неделимый участок `Tasks.BeginAtomic`; ожидание на нём
трапует 25 с текстом `Tasks: wait inside an atomic section`, вызов `Tasks.Step` — 22. Записи
откатываются, трап 25 уходит вызывающему. У тела транзакции нет пропуска `Tasks.Ctx`, поэтому
операцию монитора-объекта внутри неё вызвать нельзя вовсе: прежнее различие «свободный или занятый
монитор» относилось к языковым мониторам и здесь не выражается.

Нарушение второе: `Retry`, которому нечего ждать. Транзакция, не прочитавшая ни одной переменной,
ждала бы вечно; `Atomically` трапует 21 с текстом `AsyncStm: Retry with nothing read`. Ждать может
только задача: `Retry` в транзакции с главного стека трапует 22, `Retry` вне транзакции — 20. Если
все задачи ждут в `Retry` и сроков сна нет, планировщик видит тупик тем же способом, что и для
мониторов: печатает `Tasks: deadlock` и поднимает `Tasks.Deadlocked()`.

Нарушение третье: действие, которое нельзя откатить. Откат возвращает только транзакционные
переменные. Вывод в консоль, запись в файл, отправка в канал или ящик, `Unpark`, обычные поля
объектов остаются сделанными, а при `Retry` повторяются при каждом исполнении `Do`. Правило: в
транзакции читать и писать только транзакционные переменные, а внешнее действие делать после
`Atomically` по результату, который `Do` оставил в полях объекта. Компилятор этого не проверяет;
ожидающая операция канала трапует 25, неожидающая — нет.

Своя переменная — расширение `AsyncStm.Var`. Её `Get` зовёт `AsyncStm.Read(v)` до чтения, `Set` —
`AsyncStm.Write(v)` до записи; методы `Save` и `Load` копируют значение в запись `AsyncStm.Saved`
(поля `i`, `x`, `p`) и обратно. Значение записью кладут в `p` копией, выделенной в `Save`.

Цена. Перевод «два чтения, две записи» транзакцией на AMD64 стоит около 80 нс, тот же перевод
через `IntVar` без транзакции — около 20 нс, прямой записью в поля — меньше 1 нс (одна нить,
миллион операций, машина под нагрузкой; `docs/measurements.md`, раздел 11). Транзакция
дороже операции монитора-объекта на `Kernel.Try` и журнал; она окупается откатом и
ожиданием по нескольким объектам, а не скоростью. Ждущая задача записана только у прочитанных
переменных и снимается с них при пробуждении или отмене; журнал живёт до конца транзакции. Трап с
номером больше 128 (ошибка исполнения) доходит до вызывающего как трап 128: `HALT` принимает
номера 0–128.

Границы. Транзакция неделима внутри одного процесса BlackBox, потому что держится на одной нити
без вытеснения. Между процессами BlackBox, между компутонами и между акторами на разных машинах её
нет: там перевод делают обменом сообщениями по схеме «снять, зачислить, при отказе вернуть», и
возврат пишет программист.

## 9. Ввод-вывод без блокировки

Сервер на BlackBox обслуживает сотню соединений, по задаче на соединение. Задача читает запрос из
сокета. Если она позовёт обычный блокирующий `read`, остановится вся нить, а с ней главный цикл и
остальные девяносто девять задач. Нужно чтение, которое ждёт данных так же, как `Recv` ждёт
сообщения: отдавая процессор другим.

Модуль `AsyncIO` даёт такие операции поверх неблокирующих потоков `CommStreams`:

```
AsyncIO.Read(s, buf, beg, len, read)      (* прочитать хотя бы байт *)
AsyncIO.Write(s, buf, beg, len, written)  (* записать хотя бы байт *)
AsyncIO.Accept(l, s, timeout)             (* принять соединение *)
AsyncIO.WaitFd(fd, events)                (* дождаться готовности дескриптора *)
```

`Read` пробует прочитать. Если данных нет, задача записывает дескриптор потока в реестр `Poller` и
засыпает в `Tasks.Park`. Главный цикл одним вызовом `poll` проверяет стандартный ввод и все
записанные дескрипторы; готовность будит задачу, и она пробует снова. `Read` возвращается, как только
прочитан хотя бы байт (`read > 0`); `read = 0` значит конец потока или обрыв. `Write` устроен так же.
Регистрация одноразовая: после готовности запись снимается, и если байта не хватило, задача
регистрируется снова.

`Accept` с отрицательным `timeout` ждёт соединения без срока. С неотрицательным он ждёт не дольше
`timeout` мс и возвращает `FALSE`, если соединения не было; задача ждёт готовности в
`Tasks.Sleep` на остаток срока.

Готовность к чтению узнаётся по дескриптору, а дескриптор отдаёт метод потока `Handle`. По
умолчанию он возвращает −1, и перекрыт только в `CommTCP` для Linux. Без дескриптора ожидание
вырождается в `Tasks.Sleep(1)` между попытками: главный цикл не стоит, но задача опрашивает поток
каждую миллисекунду. Поэтому на других платформах `AsyncIO` работает, но тратит процессор впустую.

`WaitFd` с главного стека трапает с кодом 20. Реестр держит до 4096 ожиданий; при переполнении
ожидающий сразу получает готовность и повторяет попытку. Реестр построен на `poll`, а не на `select`,
потому что `fd_set` ограничен 1024 дескрипторами. `CommTCP` для Linux работает по IPv4 с очередью
приёма на 128 соединений.

## 10. Рабочие нити

Не всякий системный вызов умеет не блокировать. Разрешение имени узла (`getaddrinfo`), чтение файла
с медленного диска, вызов внешней библиотеки останавливают нить, и неблокирующего варианта у них
нет. Такой вызов уводят в отдельную нить ОС — *рабочую нить* — и ждут его завершения, как ждут
сообщения.

Пул `AsyncWork` держит до четырёх рабочих нитей. `Execute(proc, arg)` отдаёт процедуру `proc` с
аргументом `arg` свободной нити, усыпляет задачу и возвращает статус, когда нить закончит. Задача
ждёт, остальные задачи и главный цикл работают. Готовые задания — `ReadFileJob`, `WriteFileJob` и
`ResolveJob`; их аргумент — адрес записи `FileReq` или `AddrReq`.

```
MODULE ObxWork0;
	IMPORT S := SYSTEM, AsyncWork, Tasks, Services, AsyncIO, Console, Strings;

	TYPE Asker = POINTER TO RECORD (Tasks.Job) END;

	VAR req: AsyncWork.AddrReq; status: INTEGER; done: BOOLEAN;

	PROCEDURE (a: Asker) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		req.host := "localhost"; req.count := 0; req.status := -1;
		status := AsyncWork.Execute(S.ADR(AsyncWork.ResolveJob), S.ADR(req));
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR a: Asker; s: ARRAY 16 OF CHAR;
	BEGIN
		NEW(a); done := FALSE; Tasks.Start(a);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxWork0: status="); Strings.IntToString(req.status, s); Console.WriteStr(s);
		Console.WriteLn
	END Do;

END ObxWork0.
```

Команда печатает:

```
ObxWork0: status=0
```

Пока рабочая нить разрешает имя, задача ждёт в `Tasks.ParkExternal`: пробуждение придёт из рабочей
нити через `Poller`, а не от другой задачи. Такое ожидание планировщик тупиком не считает, и журнал
не печатается.

Запись `req` — глобальная переменная модуля, а не объект кучи, и это обязательно. Рабочая нить
исполняет код вне планировщика и вне сборщика мусора. Ей нельзя обращаться к куче: звать `NEW`,
`Kernel.Try`, журнал, процедуры каркаса. Куча не защищена замком: выделение в рабочей нити
портит списки свободных блоков, а сборщик может освободить объект, в который нить ещё пишет.
Компилятор этого не проверяет, и библиотека тоже; нарушение проявляется порчей памяти в случайный
момент.

`Execute` — точка переключения; вне задачи он трапает с кодом 21. Для заданий, результат которых
нужен не сразу, есть `Submit`, `Done`, `Result` и `Release`: они не ждут, но слот после `Submit`
занят, пока его не освободит `Release`. Слотов 64; без `Release` они кончаются, и следующие `Submit` и
`Execute` возвращают −1.

Пул ставится при первом `Execute` или `Submit`: `AsyncWork` ищет модуль `AsyncWorkLin`, затем
`AsyncWorkWin`. Пул есть на Linux AMD64 и Windows x64. На Linux готовность приходит байтом в канал
ОС, его видит `Poller`. На Windows готовность забирает действие `Services`, которое опрашивает
счётчик раз в 5 мс; блокирующий консольный ввод останавливает такое действие, а с ним и доставку
готовности.

Статусы заданий: `ReadFileJob` возвращает −30, если чтение не дало ни байта; `WriteFileJob` — −20
при неполной записи; `ResolveJob` — −1 при ошибке. Путь — до 255 байт, буфер файла — 4096 байт,
имя узла — до 127 байт. Задание делает один вызов чтения или записи. `ResolveJob` считает адреса, но
не возвращает их. Тест `Async_devWorkers` (`tools/test.sh Async_devWorkers`) отдаёт рабочей нити
задание на 300 мс; задача ждёт его около 301 мс, а главный цикл за это время успевает 286 шагов.

## 11. Компутоны

Расчёт делится на независимые куски, и машина многоядерна. Задачи BlackBox исполняются одной
нитью, рабочим нитям нельзя трогать кучу. Чтобы считать на нескольких ядрах код Component Pascal с
`NEW`, его исполняют в отдельных процессах BlackBox — у каждого своя куча и свой сборщик.

*Компутон* — объект, который пересылают в такой процесс-*исполнитель*, там исполняют и возвращают
назад. Его тип расширяет `AsyncComputon.Computon` — запись `Stores.Store` с абстрактным методом
`Do`. Тип реализует `Externalize` и `Internalize`, потому что объект пересылается в сериализованном
виде.

### 11.1. Запрос и ответ

`AsyncComputon.Start(n)` запускает `n` исполнителей, от 1 до 16. `Push(c, rank, tag)` сериализует
компутон `c` и отправляет его исполнителю с номером `rank`; `tag` — номер запроса, по которому
потом забирают ответ. `Pull(rank, tag)` ждёт ответа и возвращает компутон после `Do`. `Stop`
гасит исполнителей.

```
MODULE ObxComputon;
	IMPORT AsyncComputon, Stores, Tasks, Services, AsyncIO, Console, Strings;

	TYPE
		Square = POINTER TO RECORD (AsyncComputon.Computon) x, y: INTEGER END;
		Client = POINTER TO RECORD (Tasks.Job) END;

	VAR done: BOOLEAN; d: ARRAY 16 OF CHAR;

	PROCEDURE (c: Square) Do*;
	BEGIN
		c.y := c.x * c.x
	END Do;

	PROCEDURE (c: Square) Externalize (VAR wr: Stores.Writer);
	BEGIN
		wr.WriteInt(c.x); wr.WriteInt(c.y)
	END Externalize;

	PROCEDURE (c: Square) Internalize (VAR rd: Stores.Reader);
	BEGIN
		rd.ReadInt(c.x); rd.ReadInt(c.y)
	END Internalize;

	PROCEDURE (cl: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR c: Square; r: AsyncComputon.Computon;
	BEGIN
		NEW(c); c.x := 12; c.y := 0;
		AsyncComputon.Push(c, 0, 1);
		r := AsyncComputon.Pull(0, 1);
		IF r # NIL THEN
			WITH r: Square DO Strings.IntToString(r.y, d) END
		ELSE
			Strings.IntToString(AsyncComputon.LastError(), d)
		END;
		done := TRUE
	END Body;

	PROCEDURE Do*;
		VAR cl: Client;
	BEGIN
		AsyncComputon.Start(1);
		NEW(cl); done := FALSE; Tasks.Start(cl);
		WHILE ~done DO Services.actionHook.Step; AsyncIO.Poll(1) END;
		Console.WriteStr("ObxComputon: "); Console.WriteStr(d); Console.WriteLn;
		AsyncComputon.Stop
	END Do;

END ObxComputon.
```

Команда печатает `ObxComputon: 144`. Исполнитель получил компутон с `x = 12`, вызвал `Do` и вернул
его с `y = 144`.

Ответ сопоставляется запросу только по `tag`. `rank` в `Pull` нужен лишь для сообщения обработчику
ошибок; два одновременных запроса с одинаковым `tag` путаются. Каждый `Push` получает и свой номер
кадра на проводе. Поэтому поздний ответ на запрос, брошенный по тайм-ауту, не достанется новому
запросу с тем же `tag`.

`Pull` ждёт не дольше 10 с (`PullTimeout` — со своим сроком) и ждёт в `Tasks.Sleep(10)`, то есть
задача спит со сроком, и планировщик тупика не объявляет. Вне задачи `Pull` трапает с кодом 21. По
тайм-ауту он возвращает `NIL`, а `LastError()` — −4. `Try` не ждёт: он возвращает `TRUE`, если
ответ есть, и сам ответ или `NIL` с кодом ошибки.

`AltPull(rank, tag)` даёт сигнал для выбора. С ним задача ждёт ответа компутона вместе с
сообщениями каналов и тайм-аутом. Сам ответ выбор не забирает: после победы `AltPull` его берут
`Pull` или `Try`.

Коды `LastError`: −1 — нет такого запроса, пустая сериализация или нет свободного слота запроса
(их 128); −2 — живых исполнителей не осталось; −3 — отправка не удалась; −4 — истёк срок; −5 — не
разобран ответ; −6 — результат не сериализуется; 1 — трап в `Do`; 20 — модуль занят при выгрузке.
`Push` с отрицательным `tag` трапает с кодом 20 (отрицательные номера служебные), с `rank` вне
`0 .. Size() - 1` — с кодом 21.

### 11.2. Гибель исполнителя

Исполнитель — отдельный процесс, и он может умереть: трап в коде ядра, нехватка памяти, `kill`.
Управляющий видит гибель по концу файла в канале ответов. Все незавершённые запросы умершего он
повторяет на следующем живом исполнителе. Повтор возможен после каждой следующей гибели, счётчика
попыток нет. Если живых не осталось, запрос завершается с кодом −2, и обработчик `Hook.Failed`, если
он поставлен, узнаёт об этом.

Из этого следует семантика «хотя бы один раз». Исполнитель мог посчитать `Do` и умереть, не успев
ответить; тогда `Do` исполнится ещё раз на другом. Поэтому `Do` не должен иметь последствий вне
своего объекта — писать в файлы, слать сообщения по сети, — или они должны выдерживать повтор.

Трап в самом `Do` исполнителя не убивает: исполнитель ловит его и отвечает кодом 1. `Stop`
убивает исполнителей сигналом `SIGKILL` и закрывает каналы; висящие запросы он завершает с кодом
−2, и ждущие их `Pull`, `Try` и выбор видят их завершёнными. Повтор после гибели в тесте
`Async_devComputons` занимает 415 мс против 11 мс у расчёта без гибели (`docs/measurements.md`,
раздел 5).

`Unload(module)` просит исполнителей выгрузить код модуля, чтобы следующий `Push` загрузил новый.
Модуль, который импортирован или чей кадр есть на стеке, выгрузить нельзя: `Unload` возвращает
`FALSE`, а `LastError()` — 20.

### 11.3. Транспорты

Транспорт выбирают до `Start` вызовом `SetTransport` или внешней переменной `ASYNC_TRANSPORT`:

- `pipe` (по умолчанию) — исполнитель запускается как копия текущего процесса
  (`/proc/self/exe`) с тремя каналами ОС: команды, запросы, ответы. В консольном хосте это тот же
  `blackbox-console`.
- `tcp` — управляющий слушает `127.0.0.1` на свободном порту, исполнитель подключается к адресу из
  параметра `--ASYNC_TCP=…`.
- `mpi` — внешний транспорт `AsyncMpi` (раздел 11.4).

Кадр состоит из заголовка в 12 байт (длина, номер кадра, вид или код) и тела. Тело ответа по `pipe`
и `tcp` ограничено так, что результат не может быть больше примерно 59 988 байт.

### 11.4. MPI

`AsyncMpi.Install()` загружает обёртку `libbbmpi.so` над MPICH и ставит транспорт. После
`SetTransport("mpi")` вызов `Start(n)` ожидает мир ровно из `n+1` рангов: ранг 0 — управляющий,
ранги 1..n исполняют `AsyncMpi.WorkerLoop`. Мир другого размера даёт `Size() = 0`.

MPI не сообщает о гибели ранга (mpiexec обычно гасит весь мир), поэтому повтора здесь нет.
`Unload` по MPI не поддерживается и возвращает `FALSE` с кодом −1; `WorkerPid` возвращает 0,
`WorkerDead` — `TRUE`.

### 11.5. Фасад RobustParallel

Модуль `RobustParallel` из подсистемы `Robust` повторяет определение из доклада И. Денисова (2017,
с. 19), чтобы программы, написанные под это определение, работали поверх `AsyncComputon`:

```
CONST tag* = 1;
TYPE Computon* = AsyncComputon.Computon;
     Hook* = AsyncComputon.Hook;
VAR rank-, size-: INTEGER;
PROCEDURE LogMsg* (a: ARRAY OF CHAR);
PROCEDURE Push* (store: Computon; rank, tag: INTEGER);
PROCEDURE Pull* (rank, tag: INTEGER): Computon;
PROCEDURE Try* (rank, tag: INTEGER): Computon;
PROCEDURE SetHook* (h: Hook);
PROCEDURE Wait*;
PROCEDURE Stop*;
PROCEDURE Unload* (module: ARRAY OF CHAR);
```

Ранги здесь как в MPI: 0 — управляющий, 1..size−1 — исполнители; фасад сдвигает ранг на −1
перед вызовом `AsyncComputon`. Ранг вне `1 .. size - 1` трапает с кодом 20. Число исполнителей
задаёт внешняя переменная `ROBUST_SIZE`: при `n > 1` фасад запускает `n − 1` исполнителей на
транспорте `pipe` (или заданном `ASYNC_TRANSPORT`); мир MPI здесь ни при чём. В процессе-исполнителе
(задана `ASYNC_IN`, `ASYNC_OUT` или `ASYNC_TCP`) фасад пул не запускает. `Wait` ничего не делает:
ожидание выражено в `Pull` и `Try`. `Unload` результата не возвращает и печатает отказ в консоль.

## 12. Цена и границы

Библиотека собирается для AMD64 и i386 (кроме `AsyncMpi`). Код Component Pascal исполняет одна нить ОС; рабочие нити
и процессы компутонов общей кучи не касаются. Поэтому операция над каналом между двумя точками
переключения неделима без всякого замка: канал берёт свой монитор, только если его уже держит
какая-то задача.

Ждут только задачи. С главного стека можно отправлять в ящик, заполнять однократную переменную,
звать `Push`, `Try` и `Unload` компутонов, а также `Recv` и `Send`, если другая сторона уже готова.
Всё остальное — выбор, `AsyncIO`, `Execute`, `Pull` — требует задачи и трапает вне её.

Отмена задачи во время ожидания трапает её с кодом 127. Предложения её выбора снимаются при
следующем обращении к каналу; незавершённые запросы компутонов остаются занятыми до `Pull`, `Try`
или `Stop`.

Блокирующая операция канала и выбор памяти не выделяют: записи ожидания берутся из свободных
списков. Числа замеров и их условия собраны в `docs/measurements.md`, разделы 2.5 и 5.

Синхронизация события идёт через `AsyncSelect.Select` и ограничена его 32 альтернативами. Простое
событие канала памяти не выделяет; `Wrap` и `WithNack` используют записи из свободных списков, а
`Guard` строит только то, что строит его построитель.

## 13. Устройство

Раздел описывает, как получено поведение предыдущих разделов; поведения он не определяет.

Раздел не определяет поведение; он описывает, как оно получено.

### 13.1. Канал и выбор

`AsyncSelect.Core` — расширяемая запись с видом, ёмкостью, очередью сообщений и головами двух
очередей предложений (`waitS`, `waitR`). Предложение (`Offer`) хранит общий контекст `Ctx`,
индекс альтернативы, вид операции и сообщение.

Предложения и контексты берутся из свободных списков `freeOffers` и `freeCtx` и возвращаются
туда после операции. Запись свободного списка не лежит ни в одной очереди канала. Выбор без
тайм-аута памяти не выделяет. Очередь сообщений `Buffer` и `Mailbox` — кольцо в массиве; у `Buffer` его длина
равна ёмкости, у `Mailbox` кольцо удваивается при заполнении.

Операция над каналом между двумя точками переключения исполняется без переключений: нить одна,
вытеснения нет. Поэтому монитор не нужен: операция над очередью завершается до того, как может
случиться переключение.

Две процедуры исполняют операцию над каналом. `Put` отдаёт сообщение ждущему получателю или
кладёт его в кольцо и проверяет протокол. `Take` берёт сообщение из кольца или у ждущего
отправителя. После `Take` из кольца `CompleteSend` исполняет ждущие отправки, пока есть место.
Обе процедуры пропускают предложения с решением вызывающего: предложение не исполняется против
предложения с тем же `Ctx`.

`Select` обходит альтернативы в порядке индексов. Для отправки и приёма он берёт предложение и
вызывает `Register`. `Register` исполняет операцию через `Put` или `Take` либо ставит предложение
в `waitS` или `waitR`. Тайм-аут с наименьшим `ms` ставит действие `Services`. Задача спит в
`Tasks.Park`, пока `Ctx.decided` не станет истинным.

Решение принимает сторона, которая первой исполнила предложение. Она записывает `ctx.decided` и
`ctx.winner`, забирает или отдаёт сообщение и будит задачу через `Tasks.Unpark`. Монитор канала
при этом взят, только если его держал кто-то другой. `Wake` будит только при `ctx.ready = TRUE`.
Во время регистрации задача может ждать монитор другого канала, и `Unpark` перевёл бы её в
`Ready` без монитора. После пробуждения владелец снимает предложения (`Withdraw`). Снятие
идемпотентно, поэтому победитель снимается тем же вызовом.

Прямые `Send` и `Recv` всех трёх видов — `QueueSend` и `QueueRecv`. Операция исполняется сразу
через `Put` или `Take`, иначе предложение встаёт в `waitS` или `waitR`, а задача спит в
`Tasks.Park`. Другая сторона исполняет предложение и будит задачу без передачи монитора. Встреча —
канал ёмкости 0: отправка без получателя всегда ждёт в `waitS`. Мёртвые предложения снимаются при
просмотре очереди. Мёртвым считается предложение задачи в состоянии `Finished` или `Cancelled`.

### 13.2. Ввод-вывод

`Poller` хранит только указатель на платформенный хук. Реестр дескрипторов и `poll` лежат в
`LinConsole`: массив на 4096 записей, stdin первым элементом, одноразовая регистрация. Готовые
записи снимаются до вызова `Waiter.Ready`, чтобы повторная регистрация не испортила обход.

### 13.3. Рабочие нити

`AsyncWork` — фасад; `AsyncWorkLin` и `AsyncWorkWin` — делегаты. Linux: до четырёх pthread,
статический массив слотов под мьютексом и условной переменной, кольцевая очередь на 256 заданий,
байт в pipe как сигнал готовности. Windows: до четырёх `CreateThread`, слоты под
`CRITICAL_SECTION`, событие очереди; готовность забирает действие `Services`, опрашивающее
`done` каждые 5 мс.

Примитивы синхронизации лежат в куче, а не в глобальных записях модуля: глобальные записи не
получают гарантированного выравнивания, а futex и `CRITICAL_SECTION` его требуют.

### 13.4. Компутоны

`AsyncComputon` держит до 16 исполнителей и до 128 запросов. Запрос (`Req`) хранит `tag`, ранг,
буфер, задачу и контекст `Select`. `Push` сериализует объект через `Stores` во временный файл и
отправляет кадр. Ответ читает главный цикл: на Linux — через `Poller`, на Windows — действием
`Services`; `Pull` ждёт `Tasks.Sleep(10)` с дедлайном.

`MarkDead` закрывает канал умершего исполнителя и повторяет его незавершённые запросы на живом.
`Resend` выбирает следующего живого; если живых нет, запрос получает код `-2`. `Unload`
отправляет вид `kindUnload` и ждёт ответов; исполнитель вызывает `Kernel.UnloadMod` и очищает
указатель на объект выгруженного модуля, чтобы следующая сборка не разыменовала его таблицу
методов.

MPI-транспорт (`AsyncMpi`) загружает `libbbmpi.so` через `LinDl` и привязывает семь функций.
Управляющий проверяет готовность ответов `MPI_Iprobe` из действия `Services`; исполнитель
`WorkerLoop` принимает кадры на stdin-команду и отвечает через MPI.

### 13.5. Транзакции

`AsyncStm` хранит состояние текущей транзакции в переменных модуля: транзакция одна, потому что
внутри неё нет переключений. Номер транзакции `rser` и номер уровня `wser` берутся из одного
счётчика. Переменная помнит, какой транзакцией прочитана (`v.rser`) и каким уровнем записана
(`v.wser`); совпадение номера избавляет от поиска в множестве прочитанных и от второй записи в
журнал.

`Atomically` открывает `Tasks.BeginAtomic` и исполняет `Do` под `Kernel.Try`. Нормальный конец —
фиксация: журнал очищается, и задачи, ждущие записанных переменных, будятся. Трап — откат журнала
с конца к началу и передача номера дальше через `HALT` с тем же номером (`HALT` принимает только
константу, поэтому номер выбирается оператором `CASE`). `Retry` — тоже трап, но тихий: номер 128 — `silentTrap` ядра, отчёт о нём не печатается;
`Atomically` узнаёт его по `Kernel.err`.

`OrElse` открывает новый уровень: запоминает длину журнала и берёт новый номер уровня, поэтому
первая запись в ветви кладёт в журнал значение на момент начала ветви. После `Retry` ветви журнал
откатывается до отметки, а прочитанное ветвью остаётся в множестве — ожидание охватывает обе
ветви.

Ожидание `Retry` — запись `Wait` (задача, признак пробуждения, массив прочитанных переменных),
внесённая в список ждущих каждой прочитанной переменной. Запись переменной вне транзакции или
фиксация будит всех из её списка и очищает список. Задача спит в `Tasks.Park`; пробуждение без
признака — чужой `Unpark`, он возвращается задаче после ожидания, как в `AsyncSelect`. Проснувшись,
задача снимает себя со списков остальных переменных. `Wait` — уборщик трапа
(`Kernel.PushTrapCleaner`), поэтому отмена в `Park` тоже снимает записи, и переменная не держит
отменённую задачу.

### 13.6. События

`AsyncEvent` разворачивает дерево событий в один массив альтернатив. Внутренний метод `Collect`
обходит дерево и добавляет `AsyncSelect.Op` на каждый лист: отправка и приём ссылаются на ядро
канала, тайм-аут несёт миллисекунды, сигнал называет `AsyncSelect.Signal`, а `Always` и `Never`
используют два общих сигнала, чей `Arm` возвращает `TRUE` и `FALSE`. `Choose` обходит детей по
порядку; `Wrap` и `WithNack` кладут запись шага на путь своего поддерева; `Guard` зовёт `Make` и
обходит результат. Обход останавливается после 32 листьев, и синхронизация трапает с кодом 23.

Массив добивается до 32 альтернативой `Never`: `Select` принимает длину массива, а не число
альтернатив. Шаг — запись с видом: действие `Wrap` или отметка выбора `WithNack`; шаги связаны от
листа к корню. Когда `Select` возвращает победителя, сначала ставятся отметки на пути победителя,
затем зажигаются уведомления непомеченных ветвей `WithNack`, затем исполняются действия от листа к
корню. Поэтому трап в действии не делает выбранную ветвь проигравшей: её отметка уже стоит.
Уведомления зажигаются после отметки и до действий, так что проигравший партнёр узнаёт о решении
до того, как действие победителя успеет уснуть.

Уведомление — свой `AsyncSelect.Signal` на каждое вхождение в синхронизацию: он записывает контекст
решения в список ожидающих владельца, а `Fire` будит всех, кто записан в этот момент, и очищает
список. Вхождение, которое не вооружили, в списке не стоит. Сигналы той же синхронизации
разоружаются до зажигания уведомлений, поэтому ветвь не может уведомить сама себя.

`Sync` и `SyncCtx` исполняют обход и `Select` под `Kernel.Try`. Трап ловится там, незажжённые
уведомления срабатывают, а `HALT` передаёт трап дальше с тем же номером (номер больше 128 — как
128; `Kernel.err` превращается в константу оператором `CASE`, как в `AsyncStm`). `SyncCtx`
получает −1 из `SelectCtx`, когда пропуск остановлен, и зажигает все уведомления. Шаги,
уведомления и их сигналы — записи из свободных списков, поэтому повторные синхронизации памяти не
выделяют; синхронизация без `Wrap` и `WithNack` из списков ничего не берёт.

## 14. Интерфейс

Раздел перечисляет экспорт модулей в форме документации BlackBox: блок `DEFINITION`, затем
описание каждой процедуры. Строка «Пред» называет условие и номер трапа при его нарушении.
Условие с пометкой «не проверяется» код не проверяет; его нарушение даёт неопределённый результат
или трап разыменования `NIL`. Строка «Пост» называет состояние после возврата.

### 14.1. AsyncSelect

```
DEFINITION AsyncSelect;

	IMPORT AsyncProto;

	CONST
		Rendezvous = 0; Bounded = 1; Mailbox = 2;

	TYPE
		Msg = POINTER TO ABSTRACT RECORD END;

		Ctx = POINTER TO RECORD END;

		Core = POINTER TO EXTENSIBLE RECORD
			count-: INTEGER;
			protoState-: INTEGER
		END;

		Signal = POINTER TO ABSTRACT RECORD
			(s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(s: Signal) Disarm, NEW, ABSTRACT
		END;

		Op = RECORD
			got-: Msg
		END;

	PROCEDURE InitCore (c: Core; kind, capacity: INTEGER);
	PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol);
	PROCEDURE QueueSend (c: Core; m: Msg);
	PROCEDURE QueueRecv (c: Core; OUT m: Msg);
	PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op);
	PROCEDURE AltRecv (core: Core; OUT op: Op);
	PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op);
	PROCEDURE AltSignal (sig: Signal; OUT op: Op);
	PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER;
	PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER);

END AsyncSelect.
```

`Msg` — базовый тип сообщения. `Core` — ядро канала: `count` — число сообщений в кольце,
`protoState` — состояние протокола (0 без протокола). `Op` — альтернатива `Select`; `got` —
сообщение победившего приёма. `Ctx` — решение одного `Select` или одного ожидания.

**PROCEDURE InitCore (c: Core; kind, capacity: INTEGER)**
Задаёт вид и ёмкость нового ядра канала.
Пред: `c` только что создано `NEW` — не проверяется; `kind` — `Rendezvous`, `Bounded` или
`Mailbox` — трап 20; `capacity > 0` для `Bounded`, иначе `capacity = 0` — трап 21.
Пост: `c` — пустой канал вида `kind` без протокола.

**PROCEDURE SetProtocol (c: Core; p: AsyncProto.Protocol)**
Прикрепляет протокол к каналу или снимает его.
Пред: `c # NIL` — не проверяется.
Пост: каждая отправка в `c` проверяется протоколом `p` в момент, когда совершается;
`protoState` — начальное состояние `p`. При `p = NIL` протокола нет, `protoState = 0`.

**PROCEDURE QueueSend (c: Core; m: Msg)**
Отправляет `m` в канал; ждёт, если получателя или места нет.
Пред: `c # NIL` — не проверяется; ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` отдано получателю или лежит в кольце. Протокол отверг `m` — трап 21, `m` не
отправлено.

**PROCEDURE QueueRecv (c: Core; OUT m: Msg)**
Принимает сообщение; ждёт, если сообщений и ждущих отправителей нет.
Пред: `c # NIL` — не проверяется; ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` — первое сообщение кольца или сообщение первого ждущего отправителя.

**PROCEDURE AltSend (core: Core; msg: Msg; OUT op: Op)**
Строит альтернативу отправки.
Пред: `core # NIL` — трап 23 в `Select`.
Пост: `op` — отправка `msg` в `core`.

**PROCEDURE AltRecv (core: Core; OUT op: Op)**
Строит альтернативу приёма.
Пред: `core # NIL` — трап 23 в `Select`.
Пост: `op` — приём из `core`.

**PROCEDURE AltTimeout (ms: LONGINT; OUT op: Op)**
Строит альтернативу тайм-аута.
Пост: `op` — тайм-аут `ms` мс от начала ожидания; `ms <= 0` срабатывает сразу.

**PROCEDURE AltSignal (sig: Signal; OUT op: Op)**
Строит альтернативу внешнего сигнала.
Пред: `sig # NIL` — трап 24 в `Select`.
Пост: `op` — ожидание сигнала `sig`.

**PROCEDURE Select (VAR ops: ARRAY OF Op): INTEGER**
Ждёт, пока исполнится ровно одна альтернатива, и возвращает её индекс.
Пред: вызов из задачи — трап 20; `LEN(ops) <= 32` — трап 21; вид каждой альтернативы допустим —
трап 22; у отправки и приёма канал не `NIL` — трап 23; у сигнала сигнал не `NIL` — трап 24.
Пост: исполнена ровно одна альтернатива `i`, результат — `i`. У приёма `ops[i].got` — сообщение.
Проигравшие предложения сняты, сигналы разоружены. Протокол отверг отправку — трап 21 после
снятия всех предложений.

**PROCEDURE SignalReady (ctx: Ctx; idx: INTEGER)**
Сообщает о готовности внешнего сигнала.
Пред: вызывает владелец сигнала; `ctx` и `idx` получены в `Arm`; `Disarm` ещё не вызван — не
проверяется.
Пост: если решения не было, победила альтернатива `idx`; ждущая задача разбужена.

**PROCEDURE (s: Signal) Arm (ctx: Ctx; idx: INTEGER): BOOLEAN**
Абстрактный метод; его вызывает `Select` при регистрации.
Пост: `TRUE` — сигнал готов. `FALSE` — владелец позже вызовет `SignalReady(ctx, idx)` не более
одного раза, если до того не вызван `Disarm`.

**PROCEDURE (s: Signal) Disarm**
Абстрактный метод; его вызывает `Select` после решения.
Пост: владелец сигнала больше не использует `ctx` из `Arm` и не вызывает с ним `SignalReady`.

### 14.2. AsyncChan

```
DEFINITION AsyncChan;

	IMPORT AsyncSelect, AsyncProto;

	TYPE
		Chan = POINTER TO RECORD (AsyncSelect.Core)
			(c: Chan) Send (m: AsyncSelect.Msg), NEW;
			(c: Chan) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Buffer = POINTER TO RECORD (AsyncSelect.Core)
			(c: Buffer) Send (m: AsyncSelect.Msg), NEW;
			(c: Buffer) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

		Mailbox = POINTER TO RECORD (AsyncSelect.Core)
			(c: Mailbox) Send (m: AsyncSelect.Msg), NEW;
			(c: Mailbox) Recv (OUT m: AsyncSelect.Msg), NEW
		END;

	PROCEDURE NewChan (): Chan;
	PROCEDURE NewBuffer (capacity: INTEGER): Buffer;
	PROCEDURE NewMailbox (): Mailbox;
	PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol);
	PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op);
	PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op);
	PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op);
	PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER;

END AsyncChan.
```

**PROCEDURE NewChan (): Chan**
Пост: новая пустая встреча без протокола.

**PROCEDURE NewBuffer (capacity: INTEGER): Buffer**
Пред: `capacity > 0` — трап 20.
Пост: новый пустой буфер на `capacity` сообщений без протокола.

**PROCEDURE NewMailbox (): Mailbox**
Пост: новый пустой неограниченный ящик без протокола.

**PROCEDURE (c: Chan) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Send (m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Send (m: AsyncSelect.Msg)**
Отправляют `m`; то же, что `AsyncSelect.QueueSend(c, m)`.
Пред: ожидание только из задачи — трап 20 в `Tasks.Park`. `Mailbox.Send` не ждёт.
Пост: встреча — получатель забрал `m`; буфер и ящик — `m` последнее в очереди. Протокол отверг
`m` — трап 21, `m` не отправлено.

**PROCEDURE (c: Chan) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Buffer) Recv (OUT m: AsyncSelect.Msg)**
**PROCEDURE (c: Mailbox) Recv (OUT m: AsyncSelect.Msg)**
Принимают сообщение; то же, что `AsyncSelect.QueueRecv(c, m)`.
Пред: ожидание только из задачи — трап 20 в `Tasks.Park`.
Пост: `m` — первое сообщение очереди или первого ждущего отправителя.

**PROCEDURE SetProtocol (c: AsyncSelect.Core; p: AsyncProto.Protocol)**
То же, что `AsyncSelect.SetProtocol`.

**PROCEDURE SendAlt (c: AsyncSelect.Core; m: AsyncSelect.Msg; OUT op: AsyncSelect.Op)**
**PROCEDURE RecvAlt (c: AsyncSelect.Core; OUT op: AsyncSelect.Op)**
**PROCEDURE TimeoutAlt (ms: LONGINT; OUT op: AsyncSelect.Op)**
**PROCEDURE Select (VAR ops: ARRAY OF AsyncSelect.Op): INTEGER**
То же, что `AsyncSelect.AltSend`, `AltRecv`, `AltTimeout` и `Select` (раздел 14.1).

### 14.3. AsyncOnce

```
DEFINITION AsyncOnce;

	IMPORT Tasks, AsyncSelect;

	TYPE
		Once = POINTER TO RECORD (Tasks.Object)
			done-: BOOLEAN;
			(o: Once) FillInt (VAR ctx: Tasks.Ctx; x: INTEGER), NEW;
			(o: Once) ReadInt (VAR ctx: Tasks.Ctx; OUT x: INTEGER): BOOLEAN, NEW;
			(o: Once) FillMsg (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg), NEW;
			(o: Once) ReadMsg (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg): BOOLEAN, NEW
		END;

	PROCEDURE New (): Once;

END AsyncOnce.
```

Все четыре метода — операции монитора-объекта, им нужен пропуск `Tasks.Ctx`. Поле `done`
экспортировано только для чтения.

**PROCEDURE New (): Once**
Пост: незаполненная переменная, `done = FALSE`.

**PROCEDURE (o: Once) FillInt (VAR ctx: Tasks.Ctx; x: INTEGER)**
Пред: `~o.done` — трап 21.
Пост: `o.done`; значение — `x`.

**PROCEDURE (o: Once) ReadInt (VAR ctx: Tasks.Ctx; OUT x: INTEGER): BOOLEAN**
Ждёт заполнения в `Await` флага `filled`.
Пред: вызов из задачи со своим пропуском — трапы 22 и 25.
Пост: `TRUE` — `x` — целое из `FillInt` (после `FillMsg` — 0); `FALSE` — пропуск остановлен до заполнения, `x` = 0.

**PROCEDURE (o: Once) FillMsg (VAR ctx: Tasks.Ctx; m: AsyncSelect.Msg)**
Пред: `~o.done` — трап 22.
Пост: `o.done`; значение — `m`.

**PROCEDURE (o: Once) ReadMsg (VAR ctx: Tasks.Ctx; OUT m: AsyncSelect.Msg): BOOLEAN**
Ждёт заполнения в `Await` флага `filled`.
Пред: вызов из задачи со своим пропуском — трапы 22 и 25.
Пост: `TRUE` — `m` — сообщение из `FillMsg` (после `FillInt` — `NIL`); `FALSE` — пропуск остановлен до заполнения, `m` = `NIL`.

### 14.4. AsyncProto

```
DEFINITION AsyncProto;

	TYPE
		Message = ARRAY 256 OF CHAR;
		Protocol = POINTER TO RECORD END;

	PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol;
	PROCEDURE LastError (OUT msg: Message);
	PROCEDURE Start (p: Protocol): INTEGER;
	PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR);
	PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN;
	PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN);
	PROCEDURE LastViolation (p: Protocol; OUT msg: Message);

END AsyncProto.
```

**PROCEDURE New (IN desc: ARRAY OF CHAR): Protocol**
Строит автомат из описания (раздел 6).
Пост: автомат или `NIL` при ошибке описания; текст ошибки — в `LastError`.

**PROCEDURE LastError (OUT msg: Message)**
Пост: `msg` — текст последней ошибки разбора.

**PROCEDURE Start (p: Protocol): INTEGER**
Пост: индекс начального состояния; при `p = NIL` — 0.

**PROCEDURE StateName (p: Protocol; state: INTEGER; OUT name: ARRAY OF CHAR)**
Пост: `name` — имя состояния; `""`, если `p = NIL` или индекса нет.

**PROCEDURE IsFinal (p: Protocol; state: INTEGER): BOOLEAN**
Пред: `p # NIL` — трап 20.
Пост: `TRUE`, если из `state` нет исходящих переходов.

**PROCEDURE Step (p: Protocol; VAR state: INTEGER; IN label: ARRAY OF CHAR; OUT ok: BOOLEAN)**
Проверяет очередное сообщение с меткой `label`.
Пост: `ok` — `state` переведено по переходу `label`. `~ok` — `state` прежнее, текст нарушения — в
`LastViolation`. При `p = NIL` — `ok`.

**PROCEDURE LastViolation (p: Protocol; OUT msg: Message)**
Пост: `msg` — текст последнего нарушения; при `p = NIL` — `""`.

### 14.5. AsyncIO

```
DEFINITION AsyncIO;

	IMPORT CommStreams;

	PROCEDURE WaitFd (fd: INTEGER; events: SET);
	PROCEDURE Poll (timeout: LONGINT);
	PROCEDURE Read (s: CommStreams.Stream; VAR x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT read: INTEGER);
	PROCEDURE Write (s: CommStreams.Stream; IN x: ARRAY OF BYTE; beg, len: INTEGER;
		OUT written: INTEGER);
	PROCEDURE Accept (l: CommStreams.Listener; OUT s: CommStreams.Stream;
		timeout: LONGINT): BOOLEAN;

END AsyncIO.
```

**PROCEDURE WaitFd (fd: INTEGER; events: SET)**
Ждёт готовности дескриптора; регистрация одноразовая.
Пред: вызов из задачи — трап 20.
Пост: `fd` готов к `events`; при `fd < 0` прошёл `Tasks.Sleep(1)`.

**PROCEDURE Poll (timeout: LONGINT)**
Прогоняет ожидания `Poller` для тестового насоса.
Пост: готовые ожидающие не дольше `timeout` мс разбужены.

**PROCEDURE Read (s; VAR x; beg, len: INTEGER; OUT read: INTEGER)**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `read > 0` — прочитано `read` байт; `read = 0` — конец или обрыв.

**PROCEDURE Write (s; IN x; beg, len: INTEGER; OUT written: INTEGER)**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `written > 0` — записано `written` байт; `written = 0` — обрыв.

**PROCEDURE Accept (l; OUT s; timeout: LONGINT): BOOLEAN**
Пред: ожидание только из задачи — трап 20 в `WaitFd` или `Tasks.Sleep`.
Пост: `TRUE` — `s` принятое соединение; `FALSE` — истёк `timeout` мс, `s = NIL`. При
`timeout < 0` срока нет.

### 14.6. AsyncWork

```
DEFINITION AsyncWork;

	IMPORT SYSTEM;

	CONST
		maxPath = 256; maxBuf = 4096; maxHost = 128;

	TYPE
		Proc = PROCEDURE [ccall] (arg: SYSTEM.ADDRESS);

		FileReq = RECORD [untagged]
			path: ARRAY maxPath OF SHORTCHAR;
			buf: ARRAY maxBuf OF SHORTCHAR;
			len, status: INTEGER
		END;

		AddrReq = RECORD [untagged]
			host: ARRAY maxHost OF SHORTCHAR;
			count, status: INTEGER
		END;

		FileReqPtr = POINTER [untagged] TO FileReq;
		AddrReqPtr = POINTER [untagged] TO AddrReq;

		Impl = POINTER TO ABSTRACT RECORD
			(i: Impl) Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Done (id: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(i: Impl) Result (id: INTEGER): INTEGER, NEW, ABSTRACT;
			(i: Impl) Release (id: INTEGER), NEW, ABSTRACT;
			(i: Impl) Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER, NEW, ABSTRACT;
			(i: Impl) Workers (): INTEGER, NEW, ABSTRACT;
			(i: Impl) Scan, NEW, ABSTRACT;
			(i: Impl) ReadFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) WriteFileJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT;
			(i: Impl) ResolveJob (arg: SYSTEM.ADDRESS), NEW, ABSTRACT
		END;

	PROCEDURE Install (i: Impl);
	PROCEDURE Active (): BOOLEAN;
	PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Done (id: INTEGER): BOOLEAN;
	PROCEDURE Result (id: INTEGER): INTEGER;
	PROCEDURE Release (id: INTEGER);
	PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER;
	PROCEDURE Workers (): INTEGER;
	PROCEDURE Scan;
	PROCEDURE [ccall] ReadFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] WriteFileJob (arg: SYSTEM.ADDRESS);
	PROCEDURE [ccall] ResolveJob (arg: SYSTEM.ADDRESS);

END AsyncWork.
```

`Impl` — платформенный делегат; его ставят `AsyncWorkLin` и `AsyncWorkWin`. Номера трапов
делегата приведены для Linux и Windows; они совпадают.

**PROCEDURE Install (i: Impl)**
Ставит делегата. Его вызывает `Install` делегата.
Пост: пул обслуживает `i`.

**PROCEDURE Active (): BOOLEAN**
Пост: `TRUE`, если делегат поставлен.

**PROCEDURE Submit (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Ставит задание в очередь; при необходимости ставит делегата.
Пред: делегат найден — трап 22.
Пред: в пуле есть рабочая нить (`Workers() > 0`) — трап 23.
Пост: номер слота; −1 — нет свободного слота или очередь полна.

**PROCEDURE Done (id: INTEGER): BOOLEAN**
Пред: делегат поставлен — трап 21; `0 <= id <` числа слотов — трап 20 в делегате.
Пост: `TRUE`, если задание `id` завершено.

**PROCEDURE Result (id: INTEGER): INTEGER**
Пред: те же, что у `Done`.
Пост: статус задания `id`.

**PROCEDURE Release (id: INTEGER)**
Пред: те же, что у `Done`.
Пост: слот `id` свободен.

**PROCEDURE Execute (procAdr, arg: SYSTEM.ADDRESS): INTEGER**
Исполняет задание в рабочей нити и ждёт его; точка переключения.
Пред: делегат найден — трап 22; в пуле есть рабочая нить — трап 23; вызов из задачи — трап 21 в
делегате.
Пост: статус задания, слот освобождён; −1 — нет свободного слота.

**PROCEDURE Workers (): INTEGER**
Пост: число созданных рабочих нитей; без делегата — 0.

**PROCEDURE Scan**
Разбирает завершённые задания; его вызывает главный цикл.
Пост: задачи, чьи задания завершены, разбужены.

**PROCEDURE ReadFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE WriteFileJob (arg: SYSTEM.ADDRESS)**
**PROCEDURE ResolveJob (arg: SYSTEM.ADDRESS)**
Задания для рабочей нити; `arg` — адрес `FileReq` или `AddrReq`.
Пред: делегат поставлен — трап 21.
Пост: статус задания — в поле `status` записи (коды — в разделе 10).

### 14.7. AsyncComputon

```
DEFINITION AsyncComputon;

	IMPORT Stores, AsyncSelect;

	TYPE
		Computon = POINTER TO ABSTRACT RECORD (Stores.Store)
			(c: Computon) Do, NEW, ABSTRACT
		END;

		Hook = POINTER TO ABSTRACT RECORD
			(h: Hook) Failed (rank, tag, err: INTEGER), NEW, ABSTRACT
		END;

		Transport = POINTER TO ABSTRACT RECORD
			(t: Transport) Start (n: INTEGER; OUT size: INTEGER), NEW, ABSTRACT;
			(t: Transport) SendFrame (rank, tag, kind: INTEGER;
				buf: POINTER TO ARRAY OF SHORTCHAR; len: INTEGER): BOOLEAN, NEW, ABSTRACT;
			(t: Transport) Poll, NEW, ABSTRACT;
			(t: Transport) Stop, NEW, ABSTRACT;
			(t: Transport) Alive (rank: INTEGER): BOOLEAN, NEW, ABSTRACT
		END;

	PROCEDURE Serialize (c: Computon; OUT buf: POINTER TO ARRAY OF SHORTCHAR; OUT len: INTEGER);
	PROCEDURE Deserialize (VAR b: ARRAY OF SHORTCHAR; len: INTEGER): Computon;
	PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE SetTransportObject (t: Transport);
	PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal;
	PROCEDURE Push (c: Computon; rank, tag: INTEGER);
	PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon;
	PROCEDURE Pull (rank, tag: INTEGER): Computon;
	PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN;
	PROCEDURE Wait;
	PROCEDURE LastError (): INTEGER;
	PROCEDURE SetHook (h: Hook);
	PROCEDURE LogMsg (IN msg: ARRAY OF CHAR);
	PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN;
	PROCEDURE Size (): INTEGER;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorkerPid (rank: INTEGER): INTEGER;
	PROCEDURE WorkerDead (rank: INTEGER): BOOLEAN;
	PROCEDURE Stop;
	PROCEDURE Start (n: INTEGER);
	PROCEDURE MarkDead (idx: INTEGER);
	PROCEDURE DeliverFrame (rank, tag, status: INTEGER; VAR b: ARRAY OF SHORTCHAR; len: INTEGER);
	PROCEDURE RunDo (c: Computon; OUT err: INTEGER);
	PROCEDURE WorkerLoop;

END AsyncComputon.
```

Коды `LastError` перечислены в разделе 11.1. `Transport`, `SetTransportObject`, `MarkDead`,
`DeliverFrame`, `RunDo`, `Serialize` и `Deserialize` служат транспортам и исполнителю.

**PROCEDURE Start (n: INTEGER)**
Останавливает прежний пул и запускает `n` исполнителей.
Пред: `1 <= n <= 16` — трап 20; для транспорта `mpi` поставлен внешний транспорт — трап 21.
Пост: `Size()` — число запущенных исполнителей.

**PROCEDURE Stop**
Пост: исполнители убиты, каналы закрыты, `Size() = 0`; незавершённые запросы завершены с кодом
`-2`.

**PROCEDURE SetTransport (IN name: ARRAY OF CHAR): BOOLEAN**
Пост: `TRUE` — выбран транспорт `"pipe"` (и `""`), `"tcp"` или `"mpi"`; `FALSE` — имя неизвестно.

**PROCEDURE Push (c: Computon; rank, tag: INTEGER)**
Сериализует `c` и отправляет исполнителю `rank`.
Пред: `tag >= 0` — трап 20 (отрицательные номера служебные).
Пред: `0 <= rank < Size()` — трап 21.
Пост: запрос `tag` поставлен; при ошибке `LastError = -1`, отправка не удалась — повтор на живом.

**PROCEDURE PullTimeout (rank, tag: INTEGER; ms: LONGINT): Computon**
**PROCEDURE Pull (rank, tag: INTEGER): Computon**
Ждут результат запроса `tag`; `Pull` — с `ms = 10000`.
Пред: вызов из задачи — трап 21.
Пост: результат или `NIL` с кодом в `LastError`; запрос освобождён.

**PROCEDURE Try (rank, tag: INTEGER; OUT c: Computon): BOOLEAN**
Пост: `TRUE` — запрос завершён и освобождён, `c` — результат или `NIL` с кодом в `LastError`;
`FALSE` — результата нет, `c = NIL`.

**PROCEDURE AltPull (rank, tag: INTEGER): AsyncSelect.Signal**
Пост: сигнал для `Select`, готовый, когда завершён запрос `tag`.

**PROCEDURE Unload (IN module: ARRAY OF CHAR): BOOLEAN**
Пост: `TRUE` — все живые исполнители выгрузили модуль или пул не запущен; `FALSE` — отказ, код в
`LastError`.

**PROCEDURE Size (): INTEGER**, **Rank (): INTEGER**, **WorkerPid (rank): INTEGER**,
**WorkerDead (rank): BOOLEAN**, **LastError (): INTEGER**
Пост: число исполнителей; 0; pid исполнителя или −1; признак гибели (`TRUE` вне диапазона);
код последней ошибки.

**PROCEDURE SetHook (h: Hook)**, **LogMsg (IN msg)**, **Wait**
Ставят обработчик ошибок, пишут строку в консоль; `Wait` ничего не делает.

### 14.8. AsyncMpi

```
DEFINITION AsyncMpi;

	PROCEDURE Install (): BOOLEAN;
	PROCEDURE Available (): BOOLEAN;
	PROCEDURE Rank (): INTEGER;
	PROCEDURE WorldSize (): INTEGER;
	PROCEDURE WorkerLoop;

END AsyncMpi.
```

**PROCEDURE Install (): BOOLEAN**
Пост: `TRUE` — `libbbmpi.so` привязана, транспорт поставлен в `AsyncComputon`.

**PROCEDURE Available (): BOOLEAN**
Пост: `TRUE` — библиотека MPICH и обёртка найдены; `MPI_Init` не вызывается.

**PROCEDURE Rank (): INTEGER**, **WorldSize (): INTEGER**
Пост: ранг и размер мира MPI; до инициализации — −1 и 0.

**PROCEDURE WorkerLoop**
Точка входа процесса-исполнителя ранга больше 0.

### 14.9. AsyncStm

```
DEFINITION AsyncStm;

	TYPE
		Saved = RECORD
			i: LONGINT;
			x: REAL;
			p: ANYPTR
		END;

		Var = POINTER TO ABSTRACT RECORD
			(v: Var) Save- (OUT s: Saved), NEW, ABSTRACT;
			(v: Var) Load- (IN s: Saved), NEW, ABSTRACT
		END;

		IntVar = POINTER TO RECORD (Var)
			(v: IntVar) Get (): INTEGER, NEW;
			(v: IntVar) Set (x: INTEGER), NEW
		END;

		BoolVar = POINTER TO RECORD (Var)
			(v: BoolVar) Get (): BOOLEAN, NEW;
			(v: BoolVar) Set (x: BOOLEAN), NEW
		END;

		RealVar = POINTER TO RECORD (Var)
			(v: RealVar) Get (): REAL, NEW;
			(v: RealVar) Set (x: REAL), NEW
		END;

		RefVar = POINTER TO RECORD (Var)
			(v: RefVar) Get (): ANYPTR, NEW;
			(v: RefVar) Set (x: ANYPTR), NEW
		END;

		Tx = POINTER TO ABSTRACT RECORD
			(t: Tx) Do-, NEW, ABSTRACT
		END;

	PROCEDURE Atomically (t: Tx);
	PROCEDURE Retry;
	PROCEDURE OrElse (a, b: Tx);
	PROCEDURE Read (v: Var);
	PROCEDURE Write (v: Var);
	PROCEDURE NewInt (x: INTEGER): IntVar;
	PROCEDURE NewBool (x: BOOLEAN): BoolVar;
	PROCEDURE NewReal (x: REAL): RealVar;
	PROCEDURE NewRef (x: ANYPTR): RefVar;

END AsyncStm.
```

Методы `Save`, `Load` и `Do` экспортированы только для реализации: расширение их пишет, зовёт их
сам `AsyncStm`. Модуль собирается и для i386.

**TYPE Tx**
ABSTRACT
Тело транзакции; данные — в полях расширения.

**PROCEDURE (t: Tx) Do**
NEW, ABSTRACT
Действия транзакции. Исполняется из `Atomically` или `OrElse`.

**PROCEDURE Atomically (t: Tx)**
Исполняет `t.Do` неделимо; внутри транзакции — как её часть.

```
Пред: t # NIL — 20
Пред: Retry только после чтения переменной — 21
Пред: Retry только в задаче — 22
Пред: в t.Do нет точки переключения — 25 (Tasks), нет Tasks.Step — 22 (Tasks)
Пост: записи t.Do видны; при трапе в t.Do записи откатаны, трап передан с тем же номером
	(номер больше 128 — как 128)
```

**PROCEDURE Retry**
Откатывает транзакцию; `Atomically` ждёт изменения прочитанных переменных и исполняет её заново.

```
Пред: идёт транзакция — 20
Пост: не возвращается
```

**PROCEDURE OrElse (a, b: Tx)**
Исполняет `a`; если `a` сделала `Retry`, откатывает её записи и исполняет `b`. Вне транзакции —
отдельной транзакцией.

```
Пред: a # NIL — 20
Пред: b # NIL — 21
Пост: исполнена a или b; Retry в b ждёт изменения переменных, прочитанных a или b
```

**PROCEDURE Read (v: Var)**
Зовётся из `Get` расширения `Var` до чтения значения.

```
Пред: v # NIL — 20
Пост: в транзакции v в множестве прочитанных
```

**PROCEDURE Write (v: Var)**
Зовётся из `Set` расширения `Var` до изменения значения.

```
Пред: v # NIL — 20
Пост: в транзакции прежнее значение v в журнале отката; вне транзакции задачи, ждущие v в Retry,
	разбужены
```

**PROCEDURE (v: Var) Save (OUT s: Saved)**, **Load (IN s: Saved)**
NEW, ABSTRACT
Копия значения в журнал и обратно. Значение-запись кладут в `s.p` копией.

**PROCEDURE NewInt (x: INTEGER): IntVar**, **NewBool**, **NewReal**, **NewRef**
Пост: переменная со значением `x`.

**PROCEDURE (v: IntVar) Get (): INTEGER**, **Set (x: INTEGER)** и такие же у `BoolVar`, `RealVar`,
`RefVar`
`Get` — `Read(v)` и значение; `Set` — `Write(v)` и запись значения.

### 14.10. AsyncEvent

```
DEFINITION AsyncEvent;

	IMPORT AsyncSelect, Tasks;

	TYPE
		Event = POINTER TO EXTENSIBLE RECORD END;

		Action = POINTER TO ABSTRACT RECORD
			(a: Action) Do- (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg), NEW, ABSTRACT
		END;

		Builder = POINTER TO ABSTRACT RECORD
			(b: Builder) Make- (OUT e: Event), NEW, ABSTRACT
		END;

	PROCEDURE Send (c: AsyncSelect.Core; m: AsyncSelect.Msg): Event;
	PROCEDURE Recv (c: AsyncSelect.Core): Event;
	PROCEDURE Timeout (ms: LONGINT): Event;
	PROCEDURE Signal (sig: AsyncSelect.Signal): Event;
	PROCEDURE Always (): Event;
	PROCEDURE Never (): Event;
	PROCEDURE Choose (events: ARRAY OF Event): Event;
	PROCEDURE Wrap (e: Event; a: Action): Event;
	PROCEDURE Guard (b: Builder): Event;
	PROCEDURE WithNack (e: Event; OUT nack: Event): Event;
	PROCEDURE NackWaiters (e: Event): INTEGER;
	PROCEDURE Sync (e: Event): AsyncSelect.Msg;
	PROCEDURE SyncCtx (VAR ctx: Tasks.Ctx; e: Event; OUT x: AsyncSelect.Msg): BOOLEAN;

END AsyncEvent.
```

`Event` — непрозрачное значение события; его строят конструкторы модуля. `Action` и `Builder` —
точки расширения для `Wrap` и `Guard`.

**TYPE Event**
Событие. Устройство закрыто; событие передают, хранят и синхронизируют, но не разбирают.

**PROCEDURE (a: Action) Do (x: AsyncSelect.Msg; OUT y: AsyncSelect.Msg)**
NEW, ABSTRACT
Действие обёртки. Зовётся после выбора и только в победившей ветви; `x` — результат внутреннего
события, `y` — новый результат. Действие может ждать.

**PROCEDURE (b: Builder) Make (OUT e: Event)**
NEW, ABSTRACT
Строит событие защиты. Зовётся один раз на каждое вхождение защиты в синхронизацию.

**PROCEDURE Send (c: AsyncSelect.Core; m: AsyncSelect.Msg): Event**
Пред: `c # NIL` — 20.
Пост: событие отправки `m` в `c`; результат — `NIL`.

**PROCEDURE Recv (c: AsyncSelect.Core): Event**
Пред: `c # NIL` — 20.
Пост: событие приёма из `c`; результат — сообщение.

**PROCEDURE Timeout (ms: LONGINT): Event**
Пост: событие, срабатывающее через `ms` мс от начала синхронизации; `ms <= 0` срабатывает сразу.
Результат — `NIL`.

**PROCEDURE Signal (sig: AsyncSelect.Signal): Event**
Пред: `sig # NIL` — 20.
Пост: событие готовности `sig`; результат — `NIL`.

**PROCEDURE Always (): Event**
Пост: событие, готовое, когда до него дошла проверка.

**PROCEDURE Never (): Event**
Пост: событие, которое не срабатывает никогда.

**PROCEDURE Choose (events: ARRAY OF Event): Event**
Пред: каждый элемент `# NIL` — 20.
Пост: событие одной из ветвей по порядку; пустой массив даёт `Never`. Развёрнутое дерево
синхронизации содержит не больше 32 атомарных операций — трап 23 в `Sync`.

**PROCEDURE Wrap (e: Event; a: Action): Event**
Пред: `e # NIL` — 20; `a # NIL` — 21.
Пост: событие `e`; если его ветвь победила, результат пропускается через `a.Do`.

**PROCEDURE Guard (b: Builder): Event**
Пред: `b # NIL` — 21.
Пост: событие, которое строит `b.Make` при каждой синхронизации; `Make` зовётся один раз на
вхождение.

**PROCEDURE WithNack (e: Event; OUT nack: Event): Event**
Пред: `e # NIL` — 20.
Пост: событие `e`; `nack` — уведомление, которое срабатывает, когда ветвь `e` проигрывает выбор,
когда синхронизацию останавливает отмена или срок и когда её прерывает трап. Памяти у уведомления
нет; его синхронизируют, как любое событие.

**PROCEDURE NackWaiters (e: Event): INTEGER**
Диагностика: сколько задач ждут уведомление `e` прямо сейчас. `−1` — `e` не событие-уведомление
или список ждущих не удалось пройти до конца, что означает порчу. В обычной программе процедура не
нужна: уведомление не имеет памяти, и ждать его имеет смысл только в момент, когда его зажигают.

**PROCEDURE Sync (e: Event): AsyncSelect.Msg**
Исполняет одну ветвь события и возвращает её результат.
Пред: вызов из задачи — 20 (`Select`); `e # NIL` — 20; не больше 32 атомарных операций в дереве —
23; `Make` вернул `NIL` — 22. Протокол отверг отправку — трап 21 после снятия предложений.
Пост: исполнена ровно одна ветвь; у приёма результат — сообщение, у отправки, тайм-аута, сигнала,
`Always` и уведомления — `NIL`. Проигравшие ветви не исполнялись, их предложения сняты;
уведомления проигравших ветвей зажжены; трап внутри синхронизации передан дальше с тем же номером
(номер больше 128 — как 128) после зажигания уведомлений.

**PROCEDURE SyncCtx (VAR ctx: Tasks.Ctx; e: Event; OUT x: AsyncSelect.Msg): BOOLEAN**
То же, что `Sync`, с пропуском.
Пред: `ctx` — пропуск этой задачи — 25 (`SelectCtx`); остальное как у `Sync`.
Пост: `TRUE` — исполнена одна ветвь, `x` — её результат; `FALSE` — пропуск остановлен отменой или
сроком и ни одна ветвь не исполнена, `x = NIL`; уведомления всех проигравших ветвей зажжены в
обоих случаях.
