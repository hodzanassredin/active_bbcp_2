# Задачи

Руководство по модулю `Tasks`. Оно для программиста, который пишет на Component Pascal и хочет
делать в одной программе несколько дел сразу. Язык остаётся обычным Component Pascal: задачи и
мониторы — библиотечные типы `Task`, `Job` и `Object`. Монитор-объект и пропуск описаны подробнее
в `docs/monitor-object.md`. Примеры руководства встроены в текст; отдельной подсистемы примеров
нет.

## 1. Зачем нужна задача

Программа обслуживает сто клиентов. Каждый клиент присылает запрос и ждёт ответа; сервер берёт
запросы по одному и отвечает. Если писать это одной последовательной процедурой, программист сам
держит, на каком месте остановился каждый клиент: заводит таблицу состояний и пишет цикл, который
по ней ходит. Код клиента — «послать запрос, дождаться, сложить результат» — рассыпается по
ветвям этого цикла.

*Задача* возвращает такому коду его естественную форму. Задача — последовательное исполнение
процедуры на собственном стеке. Пока задача ждёт, её локальные переменные и место в коде лежат на
её стеке, и программисту не нужно их сохранять. Сто клиентов — это сто задач, и каждая записана
как прямой текст из трёх строк.

Задание описывают расширением записи `Tasks.Job` и методом `Body-`; `Tasks.Start` его запускает.
Задание получает пропуск `Tasks.Ctx` — через него оно входит в монитор-объект и ждёт условия.
Все задачи исполняет одна нить операционной системы, и в каждый момент исполняется не больше
одной задачи. Задачу никто не прерывает: она отдаёт процессор сама и только в известных местах.
Эти места называются *точками переключения*:

1. вызов `Yield`, `Sleep`, `Park` или `Join`;
2. вход в монитор-объект, который держит другая задача;
3. ожидание условия в `Await` или `AwaitCond`.

Из этого следует главное свойство задач. Две задачи увеличивают общий счётчик: каждая читает
его, прибавляет единицу и записывает обратно. В системе с нитями ОС одно прибавление иногда
теряется, потому что нить могут прервать между чтением и записью. Здесь между чтением и записью
нет точки переключения, и всё, что задача делает между двумя точками, другие задачи видят как
одно неделимое действие. Поэтому счётчик обходится без монитора:

```
MODULE TasksSample0;
	IMPORT Tasks, Console, Strings;

	TYPE Counter = POINTER TO RECORD (Tasks.Job) END;

	VAR count: INTEGER;

	PROCEDURE (c: Counter) Body- (VAR ctx: Tasks.Ctx);
		VAR i, x: INTEGER;
	BEGIN
		FOR i := 1 TO 1000 DO
			x := count; count := x + 1;	(* чтение и запись без точки переключения между ними *)
			IF i MOD 10 = 0 THEN Tasks.Yield END
		END
	END Body;

	PROCEDURE Do*;
		VAR a, b: Counter; s: ARRAY 16 OF CHAR;
	BEGIN
		count := 0;
		NEW(a); NEW(b); Tasks.Start(a); Tasks.Start(b);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(count, s);
		Console.WriteStr("count = "); Console.WriteStr(s); Console.WriteLn
	END Do;

END TasksSample0.
```

Команда `TasksSample0.Do` печатает `count = 2000` при любом числе прогонов. Задачи чередуются
через каждые десять прибавлений, но ни одно прибавление не разорвано.

`Tasks.Task` — нижний уровень той же библиотеки. У задачи программа пишет `Run` сама и пропуска не
получает. Так пишут код, который сам управляет задачами: планировщик, пул исполнителей, библиотека
над задачами. Прикладному коду `Run` не нужен: `Job` создаёт пропуск, отпускает взятые мониторы
при трапе и даёт звать `Do` и `Await`. Поэтому в примерах главы используется `Job`.

Свойство держится, пока между чтением и записью нет точки переключения. Если поставить
`Tasks.Yield` между `x := count` и `count := x + 1`, вторая задача успеет прочитать старое
значение, и итог станет меньше 2000. Компилятор этого не проверяет. Когда общее состояние
меняется через ожидание, его защищают монитором: исключающая процедура не пускает вторую задачу,
пока первая не вышла.

Сервер со ста клиентами так и устроен. Очередь запросов — монитор-объект: клиент входит в неё
через `Do` и ждёт, пока в очереди есть место; сервер берёт запрос тем же `Do` и считает квадрат.

```
MODULE TasksSample2;
	IMPORT Tasks, Console, Strings;

	CONST clients = 100; cap = 8;

	TYPE
		Request = POINTER TO RECORD arg, res: INTEGER END;
		SubmitOp = RECORD (Tasks.Op) r: Request END;
		TakeOp = RECORD (Tasks.Op) r: Request END;
		Queue = POINTER TO RECORD (Tasks.Object)
			ring: ARRAY cap OF Request; head, tail, count: INTEGER
		END;
		Client = POINTER TO RECORD (Tasks.Job) n: INTEGER; q: Queue END;
		Server = POINTER TO RECORD (Tasks.Job) q: Queue END;

	VAR sum: INTEGER;

	PROCEDURE (q: Queue) Ready (): SET;
		VAR s: SET;
	BEGIN
		s := {};
		IF q.count < cap THEN INCL(s, 0) END;
		IF q.count > 0 THEN INCL(s, 1) END;
		RETURN s
	END Ready;

	PROCEDURE (q: Queue) Exclusive- (VAR ctx: Tasks.Ctx; VAR op: Tasks.Op);
	BEGIN
		WITH op: SubmitOp DO
			IF ~q.Await(ctx, {0}) THEN RETURN END;
			q.ring[q.tail] := op.r;
			q.tail := (q.tail + 1) MOD cap; INC(q.count)
		| op: TakeOp DO
			IF ~q.Await(ctx, {1}) THEN RETURN END;
			op.r := q.ring[q.head];
			q.head := (q.head + 1) MOD cap; DEC(q.count)
		END
	END Exclusive;

	PROCEDURE (c: Client) Body- (VAR ctx: Tasks.Ctx);
		VAR r: Request; op: SubmitOp;
	BEGIN
		NEW(r); r.arg := c.n;
		op.r := r; c.q.Do(ctx, op)
	END Body;

	PROCEDURE (s: Server) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER; op: TakeOp;
	BEGIN
		FOR i := 1 TO clients DO
			s.q.Do(ctx, op);
			INC(sum, op.r.arg * op.r.arg)
		END
	END Body;

	PROCEDURE Do*;
		VAR i: INTEGER; c: Client; s: Server; q: Queue; str: ARRAY 16 OF CHAR;
	BEGIN
		sum := 0; NEW(q);
		NEW(s); s.q := q; Tasks.Start(s);
		FOR i := 1 TO clients DO NEW(c); c.n := i; c.q := q; Tasks.Start(c) END;
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		Strings.IntToString(sum, str);
		Console.WriteStr("sum = "); Console.WriteStr(str); Console.WriteLn
	END Do;

END TasksSample2.
```

Команда печатает `sum = 338350` — сумму квадратов от 1 до 100. Текст клиента остался прямым:
положить запрос и выйти. Монитор `Queue` держит очередь; `Await` ждёт флаги «есть место» и
«есть запрос», а порядок очередей FIFO сохраняет порядок запросов.

Команда `Do` исполняется вне задачи и сама крутит планировщик вызовами `Tasks.Step`, пока живы
задачи. В окне BlackBox так делать не нужно: планировщик сам встроен в главный цикл (раздел 5).

## 2. Жизнь задачи

Задачу создаёт `NEW`, и она находится в состоянии `New`. Пока задача не запущена, ей можно задать
имя (`SetName`) и размер стека (`SetStackSize`). `Start` берёт для задачи стек, ставит её в конец
очереди готовых и переводит в `Ready`. Планировщик снимает задачу с головы очереди, переключается
на её стек, и задача становится `Running`.

Исполняемая задача покидает это состояние тремя путями. Она может уступить и сразу встать в
очередь (`Yield`) — тогда она снова `Ready`. Она может ждать: `Sleep` переводит её в `Sleeping`,
`Join` — в `Waiting`, `Park` и ожидание монитора — в `Parked`. Наконец, тело задания (`Body`)
или задачи (`Run`) может закончиться: обычным возвратом, трапом или отменой. Ждущую задачу будит
событие, которого она ждёт, `Unpark` или `Cancel`; разбуженная задача снова встаёт в очередь
готовых.

Состояний восемь, их значения экспортирует модуль, а `State(t)` возвращает текущее:

| Из | В | Что происходит |
|---|---|---|
| `New` (0) | `Ready` (1) | `Start` или `StartIn` |
| `Ready` | `Running` (2) | планировщик взял задачу из очереди |
| `Running` | `Ready` | `Yield`; `Sleep` со сроком не больше нуля |
| `Running` | `Sleeping` (3) | `Sleep` с положительным сроком |
| `Running` | `Waiting` (4) | `Join`, а цель ещё жива |
| `Running` | `Parked` (5) | `Park`; занятый монитор; ожидание в `Await`; ожидание своей группы |
| `Sleeping` | `Ready` | срок истёк; `Unpark`; `Cancel` |
| `Waiting` | `Ready` | цель завершилась; `Unpark`; `Cancel` |
| `Parked` | `Ready` | `Unpark`; `Cancel`; задаче передан монитор |
| `Running` | `Finished` (6) | тело `Body` или `Run` вернулось или трапнуло |
| `Running` | `Cancelled` (7) | задача отменена |

`Finished` и `Cancelled` — конечные состояния. Задача, тело которой завершилось трапом, тоже
`Finished`; отличить её помогает `Failed(t)`. Отмена задачи в состояниях `New`, `Ready` и
`Running` состояния не меняет, а только ставит флаг: исполняемая задача заметит его в ближайшей
точке переключения, а незапущенная — при запуске, и тогда её тело не исполняется вовсе.

Завершённая задача возвращается на главный стек, и планировщик её убирает: снимает с реестра
стеков ядра, отдаёт стек в пул, будит задачи, ждущие её в `Join`, и уменьшает счётчик живых.
`Count()` после этого задачу не учитывает. Объект задачи остаётся у программы, и `State`,
`Failed` и `GetName` по нему работают и дальше.

## 3. Ожидание

Задача ждёт, когда ей нечего делать: истекает пауза, нужен результат другой задачи, нужен сигнал.
На каждый из этих поводов в модуле есть своя операция.

`Yield` ничего не ждёт. Задача встаёт в конец очереди готовых, и задачи, готовые раньше, успевают
исполниться. Её зовут в длинном вычислении, чтобы не держать процессор: без `Yield` задача,
которая считает минуту, на минуту останавливает все остальные и окно BlackBox.

`Sleep(ms)` усыпляет задачу не меньше чем на `ms` миллисекунд по часам `Kernel.Time`. Задача
проснётся, когда планировщик в очередном шаге увидит, что срок прошёл, поэтому сон бывает дольше
заказанного на время шага. `Sleep` с неположительным сроком равен `Yield`.

`Park` и `Unpark` — пара для сигнала. Задача вызывает `Park` и спит, пока другая задача не
вызовет `Unpark` для неё. Сигнал не теряется: если `Unpark` пришёл раньше `Park`, флаг
пробуждения уже стоит, и `Park` возвращается сразу, сбросив его. Флаг один, поэтому два `Unpark`
подряд дают одно пробуждение. `Join(t)` ждёт, пока задача `t` завершится или будет отменена; если
она уже завершена, `Join` возвращается сразу.

Пример соединяет все три операции. Главная задача запускает соню и будильник и ждёт обоих:

```
MODULE TasksSample1;
	IMPORT Tasks, Console;

	TYPE
		Sleeper = POINTER TO RECORD (Tasks.Job) END;
		Waker = POINTER TO RECORD (Tasks.Job) who: Tasks.Task END;
		Main = POINTER TO RECORD (Tasks.Job) END;

	PROCEDURE (s: Sleeper) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Console.WriteStr("sleeper: park"); Console.WriteLn;
		Tasks.Park;
		Console.WriteStr("sleeper: woken"); Console.WriteLn
	END Body;

	PROCEDURE (w: Waker) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Sleep(20);
		Console.WriteStr("waker: unpark"); Console.WriteLn;
		Tasks.Unpark(w.who)
	END Body;

	PROCEDURE (m: Main) Body- (VAR ctx: Tasks.Ctx);
		VAR s: Sleeper; w: Waker;
	BEGIN
		NEW(s); Tasks.Start(s);
		NEW(w); w.who := s; Tasks.Start(w);
		Tasks.Join(s); Tasks.Join(w);
		Console.WriteStr("main: both finished"); Console.WriteLn
	END Body;

	PROCEDURE Do*;
		VAR m: Main;
	BEGIN
		NEW(m); Tasks.Start(m);
		WHILE Tasks.Count() > 0 DO Tasks.Step END
	END Do;

END TasksSample1.
```

Вывод:

```
sleeper: park
waker: unpark
sleeper: woken
main: both finished
```

Пробуждения разного рода не смешиваются. `Unpark` будит задачу в любом ожидании, но `Join` и
ожидание в `Await` считают такое пробуждение ложным и ждут дальше, а флаг остаётся следующему
`Park`. Поэтому посторонний `Unpark` не выводит задачу из монитора раньше времени и не прерывает
`Join`. `Sleep` пробуждение потребляет: проснувшись от `Unpark` раньше срока, задача сбрасывает
флаг, и её следующий `Park` будет ждать.

Ждать можно только в задаче. `Join`, вызванный из команды, трапует 20 и печатает в консоль
`Tasks: wait outside a task`: у команды нет своего стека, и уступить процессор ей некуда. Поэтому
в примере ждёт главная задача, а команда только запускает её и крутит `Step`. `Join` на самой
себе трапует 23 — задача ждала бы собственного конца вечно, — а `Join` на незапущенной задаче
трапует 24.

## 4. Отмена и группы

Задача запустила трёх помощников, и один из них упал. Двое других продолжают работать на
результат, который уже никому не нужен. Задача-родитель закончила тело, а помощники ещё живы и
держат ресурсы, о которых родитель думает, что освободил. Обе беды лечит одно правило: дети не
переживают родителя, а провал одного ребёнка отменяет остальных. Такое правило называют
структурной конкуренцией.

Отмена в этом модуле — просьба, а не удар. `Cancel(t)` ставит задаче флаг и будит её, если она
спит или ждёт. Задача замечает флаг в ближайшей точке переключения: каждая ждущая операция
проверяет его до и после ожидания и прерывает задачу трапом 127. Трап разматывает стек обычным
путём, поэтому мониторы-объекты, которые держала задача, отпускаются, а её записи в очередях
снимаются. Задача переходит в `Cancelled`. Задача, которая считает без точек переключения, флага
не увидит, пока не дойдёт до одной из них.

Отмена одной задачи не трогает других. Чтобы отмена шла по дереву, задачи объединяют в *группу*.
`NewGroup`, вызванный в задаче, создаёт её собственную группу (повторный вызов вернёт ту же, у
задачи одна группа), а `StartIn(t, g)` запускает `t` участником группы `g`. Владелец группы — та
задача, которая её создала. С группой связаны четыре правила:

1. Владелец, вернувшийся из тела, не завершается, пока в группе есть живые участники: он
   переходит в `Parked` и ждёт последнего.
2. Трап участника помечает группу проваленной (`GroupFailed`) и отменяет остальных участников.
3. Трап или отмена владельца отменяет всех участников его группы. Владелец их конца не ждёт;
   каждый ребёнок завершается в своей ближайшей точке переключения.
4. Отмена владельца, пока он ждёт группу, отменяет участников, и владелец становится `Cancelled`.

Пример показывает второе правило. Родитель запускает трёх детей и выходит из тела; второй
ребёнок через 10 мс трапует, двое других спят в бесконечном цикле:

```
MODULE TasksSample3;
	IMPORT Tasks, Console, Strings;

	TYPE
		Child = POINTER TO RECORD (Tasks.Job) n: INTEGER END;
		Parent = POINTER TO RECORD (Tasks.Job) g: Tasks.Group END;

	VAR kids: ARRAY 3 OF Child;

	PROCEDURE (c: Child) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		IF c.n = 1 THEN Tasks.Sleep(10); HALT(30) END;
		LOOP Tasks.Sleep(1000) END
	END Body;

	PROCEDURE (p: Parent) Body- (VAR ctx: Tasks.Ctx);
		VAR i: INTEGER;
	BEGIN
		p.g := Tasks.NewGroup();
		FOR i := 0 TO 2 DO NEW(kids[i]); kids[i].n := i; Tasks.StartIn(kids[i], p.g) END
	END Body;

	PROCEDURE Do*;
		VAR p: Parent; i: INTEGER; s: ARRAY 8 OF CHAR;
	BEGIN
		NEW(p); Tasks.Start(p);
		WHILE Tasks.Count() > 0 DO Tasks.Step END;
		IF Tasks.GroupFailed(p.g) THEN Console.WriteStr("group failed") ELSE Console.WriteStr("group ok") END;
		Console.WriteLn;
		FOR i := 0 TO 2 DO
			Strings.IntToString(Tasks.State(kids[i]), s);
			Console.WriteStr("child state "); Console.WriteStr(s); Console.WriteLn
		END
	END Do;

END TasksSample3.
```

Вывод:

```
Tasks: cancelled
Tasks: cancelled
group failed
child state 7
child state 6
child state 7
```

Без группы двое спящих детей жили бы вечно, и цикл `WHILE Tasks.Count() > 0` не кончился бы.
С группой трап второго ребёнка (состояние 6, `Finished` с `Failed`) будит соседей, они ловят
трап 127 в `Sleep` и становятся `Cancelled` (7); строки `Tasks: cancelled` печатает их отмена.
Родитель всё это время ждал группу по правилу 1 и завершился последним.

Трап в задаче не роняет программу. Каждое тело исполняется под `Kernel.Try`: трап
завершает только эту задачу, остальные живут дальше. Поэтому о провале узнают по `Failed(t)` и
`GroupFailed(g)`, а не по остановке BlackBox.

## 5. Планировщик и главный цикл

Задачи исполняет *планировщик* — действие `Services.Action`, которое главный цикл BlackBox
вызывает между событиями окна. Каждый вызов — один шаг `Step`. Шаг будит спящих, чей срок
наступил, и исполняет готовые задачи по очереди, пока очередь не опустеет или не пройдёт квант в
5 мс, после чего возвращает управление главному циклу. Окно поэтому остаётся живым, пока работают
задачи. Консольный BlackBox вызывает то же действие между командами и во время ожидания ввода.

Квант проверяется только тогда, когда задача сама вернула управление. Вытеснения нет: задача,
которая считает без точек переключения, держит и планировщик, и окно до конца счёта. Поэтому
длинный счёт разбивают явными `Yield` (или `Sleep`) — других точек переключения в языке нет.

Где ждать нельзя. Обработчик события, рассылка по домену документа, команда меню — это код
каркаса BlackBox. Он рассчитывает, что исполнится до конца, прежде чем начнётся следующее событие,
и все задачи видят его как одно действие. Если задача зовёт `Sleep` внутри такого обработчика,
каркас оказался бы остановленным посередине. Поэтому ожидание внутри действия `Services` (кроме
действия самого планировщика) или внутри `Models.Domain` рассылки трапует 21 с текстом
`Tasks: wait inside an action handler or domain broadcast`. Задача говорит с каркасом через
действия и цикл событий: сделала работу, поставила действие, ждёт снова у себя.

Вызывать `Step` из задачи нельзя (трап 20), повторно изнутри шага — тоже (трап 21). Команды и
тесты, которые хотят дождаться задач без главного цикла, вызывают `Step` в цикле, как примеры
этого руководства.

Ждать нельзя и внутри *неделимого участка* — между `BeginAtomic` и парным `EndAtomic`. Участок
защищает записи, которые не должна увидеть другая задача, пока он не кончился. Ожидание на участке
трапует 25 с текстом `Tasks: wait inside an atomic section`, `Step` — 22.

## 6. Тупик

Задачи ждут друг друга по кругу, или единственная задача ждёт `Unpark`, который никто не пошлёт.
Нить ОС в таком положении просто висела бы. Планировщик видит его: живые задачи есть, готовых
нет, и ни одна спящая не ждёт срока. Тогда шаг один раз печатает журнал «кто чего ждёт» и
поднимает признак `Deadlocked()`:

```
MODULE TasksSample4;
	IMPORT Tasks;

	TYPE Lonely = POINTER TO RECORD (Tasks.Job) END;

	PROCEDURE (l: Lonely) Body- (VAR ctx: Tasks.Ctx);
	BEGIN
		Tasks.Park	(* разбудить некому *)
	END Body;

	PROCEDURE Do*;
		VAR l: Lonely;
	BEGIN
		NEW(l); Tasks.SetName(l, "lonely"); Tasks.Start(l);
		Tasks.Step;
		IF Tasks.Deadlocked() THEN Tasks.Cancel(l); Tasks.Step END
	END Do;

END TasksSample4.
```

Вывод:

```
Tasks: deadlock: no ready tasks and no wake-up deadlines
Tasks: cancelled
```

Журнал — диагностика, а не авария. Программа может выйти из тупика: `Unpark` или `Cancel` снова
делают задачу готовой, и признак снимается. В примере команда отменяет одинокую задачу, и та
завершается трапом 127.

Планировщик обнаруживает только полный тупик. Если хоть одна задача спит со сроком или крутится
в цикле с `Yield`, часть задач может стоять навсегда, а журнала не будет. Задача, которая вообще
не уступает, не обнаруживается тоже: планировщик просто не получает управления.

Тупиком не считается ожидание события извне задач: таймер, готовность дескриптора, сигнал из
другого процесса. Задача, которую будят такие события, ждёт в `ParkExternal`, а не в `Park`.
Пока хоть одна задача ждёт в `ParkExternal`, планировщик тупика не объявляет и журнала не печатает.
Свой примитив, который будят извне задач, должен ждать в `ParkExternal`: в `Park` планировщик напечатает журнал, хотя
пробуждение придёт. Обратная ошибка опаснее: `ParkExternal` там, где будить некому, скрывает
настоящий тупик, пока ждёт хотя бы одна такая задача. Компилятор и `Tasks` различить эти случаи не
могут.

## 7. Цена

Стек задачи по умолчанию — 256 КБ, `SetStackSize` задаёт другой размер до запуска. Ядро выделяет
стек через `mmap` и закрывает нижнюю страницу: переполнение упирается в сторожевую страницу и
трапует, а не портит соседнюю память. Физические страницы выделяются по касанию, поэтому 10 тыс.
задач со стеком 64 КБ занимают 640 МБ адресного пространства и десятки мегабайт памяти. Предел
размера — 64 МБ: стек больше этого ядро не выделяет, и `StartIn` трапует 22, оставив задачу в
состоянии `New`. Тот же трап и текст `no stack memory (platform does not support it)` получает
программа, если платформа не дала памяти.

Первое создание задачи дорого: системный вызов на стек и, чаще всего, сборка мусора, которая
сканирует все живые стеки. Замер `System_devTasks.TestSlowLoadTenThousand` (Linux x86-64, одна
нить, load average около 7) даёт для 10 тыс. задач со стеком 64 КБ 52 мс на свежих стеках и 4,3 мс
на стеках из пула. Поэтому стек завершённой задачи не возвращается платформе, а уходит в пул, и
следующая задача того же размера берёт его без системного вызова. Пул держит до 1024 свободных
стеков; излишек планировщик возвращает ядру понемногу, не больше 16 стеков за шаг, чтобы массовое
завершение задач не стоило массы системных вызовов разом.

`System_devBench` даёт цену одной уступки `Yield` — 29–30 нс, повторного запуска задачи — 196–207 нс
против 507–523 нс при `NEW` каждый раз. Сборка мусора при стоящих задачах: 9–11 мс при 10 тыс.,
72–77 мс при 50 тыс., 202–223 мс при 100 тыс. (`System_devGcStacks`).

```sh
printf 'UnitRunner.Run System_devTasks slow noquit\n' | tools/run64.sh
tools/iso.sh sh -c "echo 'System_devBench.Yield' | tools/run64.sh"
```

Границы модели следуют из одной нити: задачи не дают параллельной работы на нескольких ядрах.
## 8. Интерфейс

```
DEFINITION Tasks;

	IMPORT SYSTEM;

	CONST
		New = 0; Ready = 1; Running = 2; Sleeping = 3;
		Waiting = 4; Parked = 5; Finished = 6; Cancelled = 7;

	TYPE
		Task = POINTER TO ABSTRACT RECORD END;
		Group = POINTER TO RECORD END;
		Op = ABSTRACT RECORD END;
		Ctx = LIMITED RECORD END;
		Job = POINTER TO ABSTRACT RECORD (Task) END;
		Object = POINTER TO ABSTRACT RECORD END;
		Cond = POINTER TO ABSTRACT RECORD END;
		StackDir = POINTER TO ABSTRACT RECORD END;

	PROCEDURE (t: Task) Run (), NEW, ABSTRACT;
	PROCEDURE (t: Job) Body- (VAR ctx: Ctx), NEW, ABSTRACT;
	PROCEDURE (t: Job) Run (), NEW;
	PROCEDURE (o: Object) Exclusive- (VAR ctx: Ctx; VAR op: Op), NEW, ABSTRACT;
	PROCEDURE (o: Object) Do (VAR ctx: Ctx; VAR op: Op), NEW;
	PROCEDURE (o: Object) Await (VAR ctx: Ctx; need: SET): BOOLEAN, NEW;
	PROCEDURE (o: Object) AwaitCond (VAR ctx: Ctx; c: Cond): BOOLEAN, NEW;
	PROCEDURE (o: Object) Ready (): SET, NEW, EXTENSIBLE;
	PROCEDURE (o: Object) HoldsWhileWaiting (): BOOLEAN, NEW, EXTENSIBLE;
	PROCEDURE (o: Object) TransferOnRelease (): BOOLEAN, NEW, EXTENSIBLE;
	PROCEDURE (c: Cond) Ready (o: Object): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (VAR c: Ctx) Stopped (): BOOLEAN, NEW;
	PROCEDURE (VAR c: Ctx) TaskOf (): Task, NEW;
	PROCEDURE (VAR c: Ctx) GroupOf (): Group, NEW;
	PROCEDURE (VAR c: Ctx) HeldCount (): INTEGER, NEW;
	PROCEDURE (d: StackDir) NewStack- (size: INTEGER; OUT lo, hi: SYSTEM.ADDRESS): BOOLEAN, NEW, ABSTRACT;
	PROCEDURE (d: StackDir) FreeStack- (lo, hi: SYSTEM.ADDRESS), NEW, ABSTRACT;
	PROCEDURE (d: StackDir) Drain-, NEW, EMPTY;

	PROCEDURE BeginAtomic;
	PROCEDURE Cancel (t: Task);
	PROCEDURE Count (): INTEGER;
	PROCEDURE Current (): Task;
	PROCEDURE Deadlocked (): BOOLEAN;
	PROCEDURE Depth (o: Object): INTEGER;
	PROCEDURE EndAtomic;
	PROCEDURE Failed (t: Task): BOOLEAN;
	PROCEDURE GetName (t: Task; OUT s: ARRAY OF CHAR);
	PROCEDURE GroupCount (g: Group): INTEGER;
	PROCEDURE GroupFailed (g: Group): BOOLEAN;
	PROCEDURE Held (o: Object): BOOLEAN;
	PROCEDURE InTask (): BOOLEAN;
	PROCEDURE Join (t: Task);
	PROCEDURE JoinAll (VAR ctx: Ctx);
	PROCEDURE List;
	PROCEDURE ListSize (): INTEGER;
	PROCEDURE NewGroup (): Group;
	PROCEDURE NewGuardDir (): StackDir;
	PROCEDURE NewSlabDir (): StackDir;
	PROCEDURE NextDeadline (): LONGINT;
	PROCEDURE OwnGroup (t: Task): Group;
	PROCEDURE Park;
	PROCEDURE ParkCtx (VAR ctx: Ctx; external: BOOLEAN): BOOLEAN;
	PROCEDURE ParkExternal;
	PROCEDURE PoolCount (): INTEGER;
	PROCEDURE Queued (o: Object): INTEGER;
	PROCEDURE ResetStats;
	PROCEDURE SetDeadline (VAR ctx: Ctx; ms: LONGINT);
	PROCEDURE SetName (t: Task; IN s: ARRAY OF CHAR);
	PROCEDURE SetStackDir (d: StackDir);
	PROCEDURE SetStackSize (t: Task; size: INTEGER);
	PROCEDURE Sleep (ms: LONGINT);
	PROCEDURE Spawn (VAR ctx: Ctx; t: Task);
	PROCEDURE StackDirOf (): StackDir;
	PROCEDURE StackDirOfTask (t: Task): StackDir;
	PROCEDURE Start (t: Task);
	PROCEDURE StartIn (t: Task; g: Group);
	PROCEDURE State (t: Task): INTEGER;
	PROCEDURE Step;
	PROCEDURE Switches (): LONGINT;
	PROCEDURE Unpark (t: Task);
	PROCEDURE UseGuardStacks;
	PROCEDURE UseSlabStacks;
	PROCEDURE Yield;

END Tasks.
```

Монитор-объект, пропуск и задание описаны выше; правила и примеры монитора-объекта —
`docs/monitor-object.md`.

Перед каждым трапом модуль пишет в консоль строку `Tasks: <текст>`. Операции, которые
приостанавливают задачу (`Yield`, `Sleep`, `Join`, `Park`), начинают с одной проверки; ниже она
называется «проверка ожидания»:

```
Пред: вызов из задачи — 20
Пред: задача не отменена — 127
Пред: не идёт действие Services (кроме планировщика) и рассылка по домену — 21
Пред: нет неделимого участка (BeginAtomic) — 25
```

Вход в занятый монитор-объект и ожидание в `Await` проходят ту же проверку.

`Sleep`, `Join` и `Park` повторяют проверку после пробуждения, поэтому отмена спящей или ждущей
задачи прерывает её трапом 127.

**CONST New, Ready, Running, Sleeping, Waiting, Parked, Finished, Cancelled**
Состояния задачи (раздел 2).

**TYPE Task**
ABSTRACT
Задача нижнего уровня. Программу, которая не управляет задачами сама, пишут через `Job`
(раздел 1); `Run` нужен планировщику, пулу исполнителей и библиотеке над задачами.

**PROCEDURE (t: Task) Run**
NEW, ABSTRACT
Тело задачи. Исполняется на стеке задачи под `Kernel.Try`.

**TYPE Group**
Группа задач (раздел 4). Состояние читают `GroupCount` и `GroupFailed`.

**TYPE Job**
ABSTRACT
Задание: задача с пропуском. Наследник пишет `Body-`; `Run` реализован библиотекой. Прикладной код
начинает с `Job`, а не с `Task` (раздел 1).

**PROCEDURE (t: Job) Body- (VAR ctx: Ctx)**
NEW, ABSTRACT
Тело задания. Исполняется на стеке задачи под `Kernel.Try`; `ctx` — пропуск этого запуска.

**PROCEDURE (t: Job) Run**
NEW
Создаёт `Ctx` на стеке задачи, зовёт `Body`, ждёт детей, запущенных через `Spawn`, отпускает
оставшиеся мониторы и завершает пропуск. Повторный `Start` того же задания начинает новый запуск.

**TYPE Ctx**
LIMITED
Пропуск задания. Создаётся `Job.Run` на стеке задачи и уходит наружу только `VAR`-параметром
`Body`. Через него идут `Do`, `Await`, `AwaitCond`, `Spawn`, `JoinAll`, `SetDeadline` и `ParkCtx`.

**PROCEDURE (VAR c: Ctx) Stopped (): BOOLEAN**
`TRUE`, если задание отменено или его срок истёк.

**PROCEDURE (VAR c: Ctx) TaskOf (): Task**
Задача этого пропуска.

**PROCEDURE (VAR c: Ctx) GroupOf (): Group**
Группа детей задания; `NIL`, если их не запускали.

**PROCEDURE (VAR c: Ctx) HeldCount (): INTEGER**
Число мониторов, взятых заданием.

**TYPE Object**
ABSTRACT
Монитор-объект: наследник добавляет поля и пишет `Exclusive-`. Подробнее — `docs/monitor-object.md`.

**TYPE Op**
ABSTRACT
Запрос к монитору: запись-наследник на стеке вызывающего, передаётся в `Do` и `Exclusive-`.

**TYPE Cond**
ABSTRACT
Условие-объект для `AwaitCond`: наследник решает `Ready(o)` по состоянию монитора `o`.

**PROCEDURE (o: Object) Do (VAR ctx: Ctx; VAR op: Op)**
Входит в монитор — свободный, свой повторно или после ожидания — и зовёт `Exclusive-` с пропуском
и запросом.

**PROCEDURE (o: Object) Await (VAR ctx: Ctx; need: SET): BOOLEAN**
Отпускает монитор и ждёт, пока `Ready()` не покроет `need`; `FALSE` — пропуск остановлен.

**PROCEDURE (o: Object) AwaitCond (VAR ctx: Ctx; c: Cond): BOOLEAN**
То же, но условие решает `c.Ready(o)`.

**PROCEDURE (o: Object) Ready (): SET**
Множество истинных флагов объекта; наследник переопределяет.

**PROCEDURE (o: Object) HoldsWhileWaiting (): BOOLEAN**
`TRUE` — ждущая задача держит монитор (правило `Await`).

**PROCEDURE (o: Object) TransferOnRelease (): BOOLEAN**
`TRUE` — при выходе монитор передаётся первому ждущему, а не ставится в очередь входа.

**PROCEDURE (c: Cond) Ready (o: Object): BOOLEAN**
NEW, ABSTRACT
Истинно, если условие выполнено для состояния монитора `o`.

**PROCEDURE Spawn (VAR ctx: Ctx; t: Task)**
Запускает `t` в группе детей пропуска; `JoinAll(ctx)` ждёт их конца.

**PROCEDURE JoinAll (VAR ctx: Ctx)**
Ждёт завершения детей пропуска, затем снимает владельца группы.

**PROCEDURE ParkCtx (VAR ctx: Ctx; external: BOOLEAN): BOOLEAN**
Ждёт как `Park`, сохраняя пропуск; `external` — ожидание события извне задач. `FALSE` — пропуск
остановлен.

**PROCEDURE SetDeadline (VAR ctx: Ctx; ms: LONGINT)**
Срок ожидания в `Await` и `ParkCtx` от текущего момента; `ms <= 0` — срок уже истёк.

**PROCEDURE Held (o: Object): BOOLEAN**
`TRUE`, если монитор кем-то занят.

**PROCEDURE Depth (o: Object): INTEGER**
Глубина повторных входов владельца.

**PROCEDURE Queued (o: Object): INTEGER**
Число задач в очередях входа, флагов и условий.

**TYPE StackDir**
ABSTRACT
Поставщик стеков. Методы `NewStack-` и `FreeStack-` выделяют и освобождают память; `Drain-`
опустошает пул. По умолчанию стоит плита.

**PROCEDURE NewGuardDir (): StackDir**
Поставщик со сторожевыми страницами: отдельный `mmap` на стек.

**PROCEDURE NewSlabDir (): StackDir**
Поставщик-плита: 64 стека одним отображением, сторожевая страница под каждым.

**PROCEDURE SetStackDir (d: StackDir)**
Ставит поставщика для новых задач; живые задачи возвращают стеки своим поставщикам.

**PROCEDURE StackDirOf (): StackDir**
Поставщик для новых задач.

**PROCEDURE StackDirOfTask (t: Task): StackDir**
Поставщик, выдавший стек задачи `t`; `NIL` при `t = NIL`.

**PROCEDURE UseGuardStacks**
Выбирает поставщика со сторожевыми страницами.

**PROCEDURE UseSlabStacks**
Выбирает плиту.

**PROCEDURE PoolCount (): INTEGER**
Число свободных стеков в пуле поставщика.

**PROCEDURE StartIn (t: Task; g: Group)**
Выделяет задаче стек и ставит её в конец очереди готовых; при `g # NIL` записывает её в группу `g`.

```
Пред: t # NIL — 20
Пред: State(t) = New — 21
Пред: стек выделен (размер не больше 64 МБ, платформа дала память) — 22
Пост: State(t) = Ready; при g # NIL GroupCount(g) увеличен на 1
```

**PROCEDURE Start (t: Task)**
`StartIn(t, NIL)`.

**PROCEDURE Yield**
Ставит текущую задачу в конец очереди готовых и отдаёт процессор.

```
Пред: проверка ожидания
Пост: задачи, готовые раньше, исполнились
```

**PROCEDURE Sleep (ms: LONGINT)**
Усыпляет задачу не меньше чем на `ms` мс; при `ms <= 0` равна `Yield`.

```
Пред: проверка ожидания, до и после сна
Пост: срок истёк или пришёл Unpark; флаг пробуждения сброшен
```

**PROCEDURE Join (t: Task)**
Ждёт конца задачи `t`.

```
Пред: проверка ожидания, до и после каждого пробуждения
Пред: t # NIL — 22
Пред: t # Current() — 23
Пред: State(t) # New — 24
Пост: State(t) IN {Finished, Cancelled}; флаг пробуждения не тронут
```

**PROCEDURE Park**
Усыпляет задачу до `Unpark`; если флаг пробуждения уже стоит, возвращается сразу.

```
Пред: проверка ожидания, до и после сна
Пост: флаг пробуждения сброшен
```

**PROCEDURE ParkExternal**
То же, что `Park`, но для ожидания события извне задач (раздел 6): пока задача так ждёт, планировщик не
объявляет тупик. `Unpark` будит её как обычно.

```
Пред: проверка ожидания, до и после сна
Пост: флаг пробуждения сброшен
```

**PROCEDURE Unpark (t: Task)**
Ставит флаг пробуждения и будит `t`, если она спит или ждёт.

```
Пред: t # NIL — 20
Пост: t готова, или её следующий Park вернётся сразу
```

**PROCEDURE Cancel (t: Task)**
Отмечает незавершённую задачу отменённой и будит её, если она спит или ждёт.

```
Пред: t # NIL — 20
Пост: t отмечена; в ближайшей точке переключения она получит трап 127
```

**PROCEDURE NewGroup (): Group**
В задаче — её собственная группа (создаётся при первом вызове); вне задачи — новая группа без
владельца.

**PROCEDURE GroupCount (g: Group): INTEGER**
Число живых участников; 0 при `g = NIL`.

**PROCEDURE GroupFailed (g: Group): BOOLEAN**
`TRUE`, если трапнул хотя бы один участник; `FALSE` при `g = NIL`.

**PROCEDURE OwnGroup (t: Task): Group**
Собственная группа `t`; `NIL`, если её нет или `t = NIL`.

**PROCEDURE Step**
Один шаг планировщика: будит спящих, исполняет готовые задачи не дольше 5 мс.

```
Пред: вызов вне задачи — 20
Пред: Step сейчас не исполняется — 21
Пред: нет неделимого участка — 22
```

**PROCEDURE NextDeadline (): LONGINT**
Ближайший срок спящей задачи по `Kernel.Time`; −1, если спящих нет.

**PROCEDURE Current (): Task**
Исполняемая задача; `NIL` вне задачи.

**PROCEDURE InTask (): BOOLEAN**
`Current() # NIL`.

**PROCEDURE State (t: Task): INTEGER**
Состояние задачи; `New` при `t = NIL`.

**PROCEDURE Failed (t: Task): BOOLEAN**
`TRUE`, если тело `t` завершилось трапом; `FALSE` при `t = NIL`.

**PROCEDURE GetName (t: Task; OUT s: ARRAY OF CHAR)**
Имя задачи; пустая строка при `t = NIL`.

**PROCEDURE SetName (t: Task; IN s: ARRAY OF CHAR)**
Задаёт имя до 63 знаков; при `t = NIL` ничего не делает. Длина `s` не проверяется: более длинное
имя трапует при копировании.

**PROCEDURE SetStackSize (t: Task; size: INTEGER)**
Размер стека в байтах; 0 — 256 КБ.

```
Пред: t # NIL — 20
Пред: State(t) = New — 21
Пред: size >= 0 — 22
```

Верхний предел здесь не проверяется; стек больше 64 МБ отвергает `StartIn`.

**PROCEDURE Count (): INTEGER**
Число запущенных и ещё не убранных задач.

**PROCEDURE BeginAtomic**
Открывает неделимый участок; участки вкладываются. Участок, внутри которого задача кончилась трапом,
кончается вместе с ней.

```
Пост: до парного EndAtomic проверка ожидания трапует 25, Step — 22, точка уступки не переключает
```

**PROCEDURE EndAtomic**
Закрывает неделимый участок.

```
Пред: участок открыт — 20
```

**PROCEDURE Deadlocked (): BOOLEAN**
`TRUE`, если последний шаг нашёл тупик и напечатал журнал (раздел 6).

**PROCEDURE Switches (): LONGINT**
Число переключений стека после последнего `ResetStats`.

**PROCEDURE ResetStats**
Обнуляет счётчик `Switches`.

**PROCEDURE ListSize (): INTEGER**
Длина списка живых задач.

**PROCEDURE List**
Печатает в консоль по строке на задачу: имя, состояние, границы стека, срок сна.

Устройство модуля — переключатель `Taskarch.Switch`, реестр стеков, куча спящих, пул — описано
в `docs/implementation.md`.
